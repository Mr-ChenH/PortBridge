#include "recorder.hpp"
#include <QDir>
#include <QDirIterator>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QUuid>
#include <chrono>
#include <limits>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <map>
#include <algorithm>

namespace portbridge {
namespace {
constexpr std::uint64_t MaxPayload = 64ull * 1024 * 1024;
constexpr std::uint32_t MaxPeer = 4096;
void put(QByteArray& b, std::uint64_t n, int bytes) { for (int i = 0; i < bytes; ++i) b.append(char(n >> (8 * i))); }
std::uint64_t get(const QByteArray& b, int pos, int bytes) { std::uint64_t n = 0; for (int i = 0; i < bytes; ++i) n |= std::uint64_t(static_cast<unsigned char>(b[pos + i])) << (8 * i); return n; }
size_t cost(const DataRecord& r) { return detail::retainedCost(r); }
QByteArray header(std::uint64_t timestamp) { QByteArray b("PBCAP001", 8); put(b, 1, 4); put(b, 24, 4); put(b, timestamp, 8); return b; }
QByteArray frame(const DataRecord& r) {
    QByteArray b("DATA", 4); const auto n = r.payload ? r.payload->size() : 0;
    put(b, 44 + r.peer.address.size() + n, 4); put(b, r.sequence, 8); put(b, r.timestampUs, 8); put(b, r.connectionId, 8);
    put(b, std::uint8_t(r.transport), 1); put(b, std::uint8_t(r.direction), 1); put(b, 0, 2); put(b, r.peer.address.size(), 4); put(b, r.peer.port, 2); put(b, 0, 2); put(b, n, 8);
    b.append(r.peer.address.data(), qsizetype(r.peer.address.size())); if (n) b.append(reinterpret_cast<const char*>(r.payload->data()), qsizetype(n)); return b;
}
QByteArray footer(const CaptureInfo& c) { QByteArray b("DONE", 4); put(b, 24, 4); put(b, c.records, 8); put(b, c.bytes, 8); put(b, c.complete, 1); put(b, 0, 7); return b; }
bool writeCatalog(const CaptureInfo& c) {
    QSaveFile f(detail::text(c.path) + ".meta.json"); if (!f.open(QIODevice::WriteOnly)) return false;
    const auto data = QJsonDocument(QJsonObject{{"schemaVersion", 1}, {"path", detail::text(c.path)}, {"startedUs", QString::number(c.startedUs)}, {"durationUs", QString::number(c.durationUs)}, {"bytes", QString::number(c.bytes)}, {"records", QString::number(c.records)}, {"complete", c.complete}, {"error", detail::text(c.error)}, {"transportSummary", detail::text(c.transportSummary)}}).toJson();
    return f.write(data) == data.size() && f.commit();
}
}
namespace detail {
struct Recorder::State : std::enable_shared_from_this<Recorder::State> {
    State();
    bool start(const RecordingOptions&, QString*);
    void stop();
    Admission enqueue(const DataRecord&);
    void markIncomplete(const QString&);
    RecorderSnapshot snapshot() const;
    std::vector<CaptureInfo> captures() const;
    void resetStatistics(bool isolate);
    void refresh(const QString&);
    void scanLoop();
    void run(std::uint64_t failureEpoch);
    std::vector<CaptureInfo> loadCatalog(const QString&, std::uint64_t);
    void trimCatalog();
    QString initialCatalog_, activePath_, requestedDirectory_;
    std::uint64_t catalogGeneration_ = 0, catalogRevision_ = 0;
    std::map<std::string, std::uint64_t> revisions_;
    bool catalogStop_ = false, catalogFinished_ = false, catalogPending_ = false;
    std::uint64_t statisticsEpoch_ = 0, failureEpoch_ = 0;
    bool isolated_ = false;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    struct QueuedRecord { DataRecord record; std::uint64_t statisticsEpoch; };
    std::deque<QueuedRecord> queue_;
    RecordingOptions options_;
    RecorderSnapshot state_;
    std::vector<CaptureInfo> captures_;
    bool stopping_ = false, incomplete_ = false;
    QString incompleteReason_;
    std::atomic<bool> finished_{true};
};
Recorder::Recorder() : state_(std::make_shared<State>()) {
    if (!state_->initialCatalog_.isEmpty()) state_->refresh(state_->initialCatalog_);
    try { std::thread([self = state_] { self->scanLoop(); }).detach(); }
    catch (const std::system_error& failure) {
        std::lock_guard<std::mutex> lock(state_->mutex_);
        state_->catalogStop_ = state_->catalogFinished_ = true; state_->catalogPending_ = false;
        state_->state_.error = "Cannot start capture catalog scanner: " + QString::fromUtf8(failure.what()); ++state_->state_.failures;
    }
}
Recorder::~Recorder() {
    state_->stop();
    std::unique_lock<std::mutex> lock(state_->mutex_);
    state_->catalogStop_ = true; ++state_->catalogGeneration_; state_->wake_.notify_all();
    if (!state_->wake_.wait_for(lock, std::chrono::seconds(3), [this] { return state_->finished_.load() && state_->catalogFinished_; })) {
        if (!state_->finished_.load()) {
            state_->incomplete_ = true; state_->incompleteReason_ = "Shutdown finalization exceeded 3 seconds; capture is incomplete";
            state_->state_.error = state_->incompleteReason_; ++state_->state_.failures;
            for (auto& capture : state_->captures_) if (text(capture.path) == state_->activePath_) { capture.complete = false; capture.error = utf8(state_->incompleteReason_); }
        }
        // The writer owns State until it finishes; initial metadata remains incomplete if the process exits.
    }
}
bool Recorder::start(const RecordingOptions& o, QString* e) { return state_->start(o, e); }
void Recorder::stop() { state_->stop(); }
Recorder::Admission Recorder::enqueue(const DataRecord& r) { return state_->enqueue(r); }
void Recorder::markIncomplete(const QString& e) { state_->markIncomplete(e); }
RecorderSnapshot Recorder::snapshot() const { return state_->snapshot(); }
std::vector<CaptureInfo> Recorder::captures() const { return state_->captures(); }
void Recorder::resetStatistics(bool isolate) { state_->resetStatistics(isolate); }
void Recorder::refresh(const QString& directory) { state_->refresh(directory); }
Recorder::State::State() { QSettings settings; initialCatalog_ = settings.value("recording/directory").toString(); }
void Recorder::State::trimCatalog() {
    std::stable_sort(captures_.begin(), captures_.end(), [](const CaptureInfo& a, const CaptureInfo& b) { return a.startedUs < b.startedUs; });
    while (captures_.size() > 1024) {
        auto old = captures_.begin(); if (text(old->path) == activePath_) ++old;
        revisions_.erase(old->path); captures_.erase(old);
    }
}
void Recorder::State::refresh(const QString& directory) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (catalogStop_) return;
    requestedDirectory_ = directory.isEmpty() ? QString() : QDir(directory).absolutePath();
    ++catalogGeneration_; catalogPending_ = true; wake_.notify_all();
}
void Recorder::State::scanLoop() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        wake_.wait(lock, [&] { return catalogStop_ || catalogPending_; });
        if (catalogStop_) break;
        const auto directory = requestedDirectory_;
        const auto generation = catalogGeneration_, revision = catalogRevision_;
        catalogPending_ = false;
        lock.unlock(); auto loaded = loadCatalog(directory, generation); lock.lock();
        if (generation != catalogGeneration_ || catalogStop_) continue;
        // Never replace writer-owned state with a snapshot read before publication/finalization.
        for (const auto& capture : captures_) {
            if (text(capture.path) == activePath_ || revisions_[capture.path] > revision) {
                loaded.erase(std::remove_if(loaded.begin(), loaded.end(), [&](const CaptureInfo& c) { return c.path == capture.path; }), loaded.end());
                loaded.push_back(capture);
            }
        }
        captures_ = std::move(loaded); trimCatalog();
        for (auto it = revisions_.begin(); it != revisions_.end();) {
            if (std::none_of(captures_.begin(), captures_.end(), [&](const CaptureInfo& c) { return c.path == it->first; })) it = revisions_.erase(it);
            else ++it;
        }
    }
    catalogFinished_ = true; wake_.notify_all();
}
std::vector<CaptureInfo> Recorder::State::loadCatalog(const QString& directory, std::uint64_t generation) {
    if (directory.isEmpty()) return {};
    auto cancelled = [&] { std::lock_guard<std::mutex> lock(mutex_); return catalogStop_ || generation != catalogGeneration_; };
    // Stream directory entries and retain at most the latest 1024 raw files, before reading metadata.
    std::map<std::pair<qint64, QString>, QString> recent;
    QDirIterator iterator(directory, {"*.pbc"}, QDir::Files);
    while (iterator.hasNext()) {
        if (cancelled()) return {};
        const auto absolute = iterator.next(); const auto info = iterator.fileInfo();
        recent[{info.lastModified().toMSecsSinceEpoch(), absolute}] = absolute;
        if (recent.size() > 1024) recent.erase(recent.begin());
    }
    std::vector<CaptureInfo> loaded; loaded.reserve(recent.size());
    for (const auto& entry : recent) {
        if (cancelled()) return {};
        const auto absolute = entry.second;
        CaptureInfo c; c.path = utf8(absolute); c.complete = false; c.error = "Capture catalog missing or invalid; completeness is unknown";
        QFile metadata(absolute + ".meta.json");
        bool valid = false;
        if (metadata.open(QIODevice::ReadOnly) && metadata.size() <= 65536) {
            const auto data = metadata.read(65537); // Bound the read even if another process grows the sidecar after size().
            const auto o = data.size() <= 65536 ? QJsonDocument::fromJson(data).object() : QJsonObject();
            valid = o["schemaVersion"].toInt() == 1 && o["complete"].isBool() && o["startedUs"].isString() && o["durationUs"].isString() && o["bytes"].isString() && o["records"].isString() && o["error"].isString();
            if (valid) {
                bool numbersValid = true;
                auto number = [&](const char* field) { bool ok = false; const auto n = o[field].toString().toULongLong(&ok); numbersValid &= ok; return n; };
                c.startedUs = number("startedUs"); c.durationUs = number("durationUs"); c.bytes = number("bytes"); c.records = number("records");
                c.complete = o["complete"].toBool(); c.error = utf8(o["error"].toString());
                if (o.contains("transportSummary")) {
                    const auto summary = o["transportSummary"].toString();
                    valid = o["transportSummary"].isString() && QStringList{"", "SERIAL", "TCP CLIENT", "TCP SERVER", "UDP", "MIXED"}.contains(summary);
                    if (valid) c.transportSummary = utf8(summary);
                }
                valid &= numbersValid; c.metadataAvailable = valid;
            }
        }
        if (!valid) {
            // Discard all untrusted numeric metadata before the bounded raw
            // fallback. Duration has no raw-file summary and stays unknown.
            c.startedUs = c.durationUs = c.bytes = c.records = 0;
            c.complete = false; c.metadataAvailable = false; c.transportSummary.clear(); c.error = "Capture catalog missing or invalid; completeness is unknown";
            QFile raw(absolute);
            if (raw.open(QIODevice::ReadOnly)) {
                const auto h = raw.read(24); if (h.size() == 24 && h.left(8) == "PBCAP001") c.startedUs = get(h, 16, 8);
                if (raw.size() >= 56 && raw.seek(raw.size() - 32)) {
                    const auto tail = raw.read(32);
                    if (tail.size() == 32 && tail.left(4) == "DONE" && get(tail, 4, 4) == 24) { c.records = get(tail, 8, 8); c.bytes = get(tail, 16, 8); }
                }
            }
        }
        loaded.push_back(std::move(c));
    }
    return loaded;
}
bool Recorder::State::start(const RecordingOptions& options, QString* error) {
    if (error) error->clear();
    const auto directory = text(options.directory);
    if (directory.isEmpty() || !options.queueBytes || options.queueBytes > 1073741824 || options.rotateBytes < 128 || options.durationSeconds > 7 * 24 * 3600) { if (error) *error = "Invalid recording directory, queue, rotation or duration"; return false; }
    if (!finished_.load()) { if (error) *error = "Recording is active or still finalizing"; return false; }
    std::uint64_t failureEpoch;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        failureEpoch = failureEpoch_;
        options_ = options; options_.directory = utf8(QDir(directory).absolutePath());
        isolated_ = false;
        state_.active = true; state_.queued = 0; state_.error.clear(); queue_.clear(); stopping_ = false; incomplete_ = false; incompleteReason_.clear(); finished_ = false;
    }
    refresh(text(options_.directory));
    QSettings settings; settings.setValue("recording/directory", text(options_.directory));
    try { std::thread([self = shared_from_this(), failureEpoch] { self->run(failureEpoch); }).detach(); }
    catch (const std::system_error& failure) {
        std::lock_guard<std::mutex> lock(mutex_); state_.active = false; finished_ = true; state_.error = QString::fromUtf8(failure.what()); ++state_.failures;
        if (error) *error = state_.error;
        return false;
    }
    return true;
}
void Recorder::State::stop() { std::lock_guard<std::mutex> lock(mutex_); state_.active = false; stopping_ = true; wake_.notify_all(); }
Recorder::Admission Recorder::State::enqueue(const DataRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.active) return Admission::Inactive;
    if ((record.payload && record.payload->size() > MaxPayload) || record.peer.address.size() > MaxPeer) return Admission::Invalid;
    const auto amount = cost(record);
    if (amount > options_.queueBytes || state_.queued > options_.queueBytes - amount || queue_.size() >= 65536) return Admission::Full;
    queue_.push_back({record, statisticsEpoch_}); state_.queued += amount; state_.highWater = std::max(state_.highWater, state_.queued); wake_.notify_all(); return Admission::Accepted;
}
void Recorder::State::markIncomplete(const QString& message) {
    std::lock_guard<std::mutex> lock(mutex_); incomplete_ = true; incompleteReason_ = message; state_.error = message; ++state_.failures;
}
RecorderSnapshot Recorder::State::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_); auto result = state_;
    if (isolated_) { result.bytes = result.records = result.failures = 0; result.queued = result.highWater = 0; result.error.clear(); }
    return result;
}
std::vector<CaptureInfo> Recorder::State::captures() const { std::lock_guard<std::mutex> lock(mutex_); return captures_; }
void Recorder::State::resetStatistics(bool isolate) {
    std::lock_guard<std::mutex> lock(mutex_); ++statisticsEpoch_; isolated_ = isolate || (isolated_ && !state_.active);
    if (isolate) ++failureEpoch_;
    state_.bytes = state_.records = state_.failures = 0; state_.highWater = isolated_ ? 0 : state_.queued; state_.error.clear();
}
void Recorder::State::run(std::uint64_t failureEpoch) {
    QFile file; CaptureInfo current; std::uint64_t fileBytes = 0;
    const auto origin = nowUs(); const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces); unsigned part = 0;
    const auto deadline = options_.durationSeconds ? std::chrono::steady_clock::now() + std::chrono::seconds(options_.durationSeconds) : std::chrono::steady_clock::time_point::max();
    auto error = [&](const QString& reason) {
        std::lock_guard<std::mutex> lock(mutex_); incomplete_ = true; incompleteReason_ = reason;
        if (failureEpoch == failureEpoch_) { state_.error = reason; ++state_.failures; }
        state_.active = false; stopping_ = true; queue_.clear(); state_.queued = 0;
    };
    auto update = [&] {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = std::find_if(captures_.begin(), captures_.end(), [&](const CaptureInfo& c) { return c.path == current.path; });
        if (found != captures_.end()) { *found = current; revisions_[current.path] = ++catalogRevision_; }
    };
    auto open = [&]() -> bool {
        current = {}; current.startedUs = nowUs(); current.complete = false;
        current.path = utf8(QDir(text(options_.directory)).filePath(QString("capture-%1-%2-%3.pbc").arg(origin).arg(token).arg(++part, 4, 10, QChar('0'))));
        { std::lock_guard<std::mutex> lock(mutex_); activePath_ = text(current.path); captures_.push_back(current); revisions_[current.path] = ++catalogRevision_; trimCatalog(); }
        file.setFileName(text(current.path));
        if (!QDir().mkpath(text(options_.directory))) { current.error = "Cannot create recording directory"; error(text(current.error)); update(); return false; }
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) { current.error = utf8(file.errorString()); error(file.errorString()); update(); writeCatalog(current); return false; }
        const auto b = header(current.startedUs);
        if (file.write(b) != b.size()) {
            current.error = utf8(file.errorString()); error(file.errorString()); file.close(); update(); writeCatalog(current); return false;
        }
        fileBytes = std::uint64_t(b.size());
        if (!writeCatalog(current)) { current.error = "Cannot initialize incomplete capture catalog"; error(text(current.error)); file.close(); update(); return false; }
        current.metadataAvailable = true; update();
        return true;
    };
    auto finalize = [&]() -> bool {
        if (!file.isOpen()) return false;
        { std::lock_guard<std::mutex> lock(mutex_); current.complete = !incomplete_; current.error = utf8(incompleteReason_); }
        current.durationUs = nowUs() - current.startedUs;
        const auto b = footer(current);
        if (file.write(b) != b.size() || !file.flush()) { current.complete = false; current.error = utf8(file.errorString()); error(file.errorString()); }
        file.close();
        if (!writeCatalog(current)) {
            current.complete = false; current.metadataAvailable = false; current.error = "Cannot save capture catalog"; error(text(current.error));
            // The footer was written before catalog finalization: make the failure visible offline too.
            QFile repair(text(current.path));
            if (!repair.open(QIODevice::ReadWrite) || !repair.seek(repair.size() - 8) || repair.write(QByteArray(1, '\0')) != 1 || !repair.flush()) current.error += "; cannot mark footer incomplete";
            update(); return false;
        }
        // Publish completeness only after both the binary footer/flush and the atomic catalog commit.
        update();
        return current.error.empty();
    };
    if (open()) {
        bool running = true;
        while (running) {
            std::deque<QueuedRecord> batch;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (queue_.empty() && !stopping_) {
                    if (options_.durationSeconds) wake_.wait_until(lock, deadline, [&] { return stopping_ || !queue_.empty(); });
                    else wake_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
                }
                if (std::chrono::steady_clock::now() >= deadline) { state_.active = false; stopping_ = true; }
                size_t amount = 0;
                while (!queue_.empty() && amount < 4 * 1024 * 1024) { amount += cost(queue_.front().record); state_.queued -= cost(queue_.front().record); batch.push_back(std::move(queue_.front())); queue_.pop_front(); }
                running = !stopping_ || !queue_.empty() || !batch.empty();
            }
            QByteArray buffer; std::uint64_t batchRecords = 0, batchPayload = 0;
            std::map<std::uint64_t, std::pair<std::uint64_t, std::uint64_t>> epochTotals;
            auto flush = [&]() -> bool {
                if (buffer.isEmpty()) return true;
                if (file.write(buffer) != buffer.size()) { current.error = utf8(file.errorString()); error(file.errorString()); buffer.clear(); return false; }
                fileBytes += std::uint64_t(buffer.size()); current.records += batchRecords; current.bytes += batchPayload;
                { std::lock_guard<std::mutex> lock(mutex_); const auto total = epochTotals.find(statisticsEpoch_); if (total != epochTotals.end()) { state_.records += total->second.first; state_.bytes += total->second.second; } }
                epochTotals.clear();
                update(); buffer.clear(); batchRecords = batchPayload = 0; return true;
            };
            for (const auto& queued : batch) {
                const auto& record = queued.record;
                const auto b = frame(record);
                if (fileBytes + std::uint64_t(buffer.size()) + std::uint64_t(b.size()) + 32 > options_.rotateBytes && current.records + batchRecords > 0) {
                    if (!flush()) { running = false; break; }
                    if (!finalize() || !open()) { running = false; break; }
                }
                const auto summary = QStringList{"SERIAL", "TCP CLIENT", "TCP SERVER", "UDP"}.at(int(record.transport));
                if (current.transportSummary.empty()) current.transportSummary = utf8(summary);
                else if (current.transportSummary != utf8(summary)) current.transportSummary = "MIXED";
                buffer.append(b); ++batchRecords; batchPayload += record.payload ? record.payload->size() : 0;
                auto& total = epochTotals[queued.statisticsEpoch]; ++total.first; total.second += record.payload ? record.payload->size() : 0;
            }
            if (!flush()) running = false;
        }
        finalize();
    }
    { std::lock_guard<std::mutex> lock(mutex_); state_.active = false; stopping_ = true; queue_.clear(); state_.queued = 0; }
    { std::lock_guard<std::mutex> lock(mutex_); activePath_.clear(); finished_ = true; wake_.notify_all(); }
}
}

bool exportCapture(const QString& capturePath, const QString& outputPath, QString* error, const CaptureExportProgress& progress) {
    if (error) error->clear();
    auto fail = [&](const QString& message) { if (error) *error = message; return false; };
    const auto inputInfo = QFileInfo(capturePath), outputInfo = QFileInfo(outputPath);
#ifdef Q_OS_WIN
    const auto pathSensitivity = Qt::CaseInsensitive;
#else
    const auto pathSensitivity = Qt::CaseSensitive;
#endif
    if (inputInfo.absoluteFilePath().compare(outputInfo.absoluteFilePath(), pathSensitivity) == 0 ||
        (!inputInfo.canonicalFilePath().isEmpty() && !outputInfo.canonicalFilePath().isEmpty() && inputInfo.canonicalFilePath().compare(outputInfo.canonicalFilePath(), pathSensitivity) == 0)) return fail("Capture and output must be different files");
    QFile input(capturePath); if (!input.open(QIODevice::ReadOnly)) return fail(input.errorString());
    const auto total = std::uint64_t(input.size());
    auto notify = [&](std::uint64_t processed) {
        if (!progress) return true;
        try { if (progress(processed, total)) return true; return fail("Capture export canceled"); }
        catch (...) { return fail("Capture export progress callback failed"); }
    };
    if (!notify(0)) return false;
    const auto h = input.read(24);
    if (h.size() != 24 || h.left(8) != "PBCAP001" || get(h, 8, 4) != 1 || get(h, 12, 4) != 24) return fail("Invalid or unsupported capture header");
    QSaveFile output(outputPath); if (!output.open(QIODevice::WriteOnly)) return fail(output.errorString());
    const bool json = QFileInfo(outputPath).suffix().compare("json", Qt::CaseInsensitive) == 0;
    auto write = [&](const QByteArray& b) { return output.write(b) == b.size(); };
    if (!write(json ? QByteArray("{\"schemaVersion\":1,\"startedUs\":\"") + QByteArray::number(get(h, 16, 8)) + "\",\"records\":[\n" : QByteArray("PortBridge capture v1 startedUs=") + QByteArray::number(get(h, 16, 8)) + "\n")) return fail(output.errorString());
    std::uint64_t records = 0, bytes = 0; bool done = false;
    while (!input.atEnd()) {
        if (!notify(std::uint64_t(input.pos()))) return false;
        const auto tag = input.read(8); if (tag.size() != 8) return fail("Truncated capture tag");
        const auto length = get(tag, 4, 4);
        if (tag.left(4) == "DONE") {
            if (length != 24) return fail("Invalid capture footer size");
            const auto body = input.read(24);
            if (body.size() != 24 || get(body, 0, 8) != records || get(body, 8, 8) != bytes || get(body, 16, 1) > 1 || get(body, 17, 7) || !input.atEnd()) return fail("Invalid or truncated capture footer");
            const bool complete = get(body, 16, 1) == 1;
            if (!write(json ? QByteArray("\n],\"complete\":") + (complete ? "true" : "false") + "}\n" : QByteArray("complete=") + (complete ? "true\n" : "false\n"))) return fail(output.errorString());
            done = true; break;
        }
        if (tag.left(4) != "DATA" || length < 44 || length > 44 + MaxPeer + MaxPayload) return fail("Invalid capture record length or tag");
        const auto fixed = input.read(44); if (fixed.size() != 44) return fail("Truncated capture metadata");
        const auto peerLength = get(fixed, 28, 4), payloadLength = get(fixed, 36, 8);
        if (peerLength > MaxPeer || payloadLength > MaxPayload || length != 44 + peerLength + payloadLength || get(fixed, 24, 1) > 3 || get(fixed, 25, 1) > 2 || get(fixed, 26, 2) || get(fixed, 34, 2)) return fail("Invalid capture metadata");
        const auto peer = input.read(qint64(peerLength)); const auto payload = input.read(qint64(payloadLength));
        if (std::uint64_t(peer.size()) != peerLength || std::uint64_t(payload.size()) != payloadLength) return fail("Truncated capture payload");
        const auto peerText = QString::fromUtf8(peer); if (peerText.toUtf8() != peer) return fail("Invalid UTF-8 peer address");
        const QString transport = QStringList{"serial", "tcpClient", "tcpServer", "udp"}[int(get(fixed, 24, 1))];
        const QString direction = QStringList{"RX", "TX", "SYSTEM"}[int(get(fixed, 25, 1))];
        QByteArray line;
        if (json) {
            const QJsonObject o{{"sequence", QString::number(get(fixed, 0, 8))}, {"timestampUs", QString::number(get(fixed, 8, 8))}, {"connectionId", QString::number(get(fixed, 16, 8))}, {"transport", transport}, {"direction", direction}, {"peer", peerText}, {"port", int(get(fixed, 32, 2))}, {"payloadLength", double(payloadLength)}, {"payloadBase64", QString::fromLatin1(payload.toBase64())}};
            if (records) line.append(",\n");
            line.append(QJsonDocument(o).toJson(QJsonDocument::Compact));
        } else {
            line = QString("sequence=%1 timestampUs=%2 connectionId=%3 transport=%4 direction=%5 peer=%6:%7 length=%8 hex=").arg(get(fixed, 0, 8)).arg(get(fixed, 8, 8)).arg(get(fixed, 16, 8)).arg(transport, direction, peerText).arg(get(fixed, 32, 2)).arg(payloadLength).toUtf8() + payload.toHex(' ') + '\n';
        }
        if (!write(line)) return fail(output.errorString());
        ++records; bytes += payloadLength;
    }
    if (!done) return fail("Capture has no finalization footer");
    if (!notify(total)) return false;
    if (!output.commit()) return fail(output.errorString());
    return true;
}
}
