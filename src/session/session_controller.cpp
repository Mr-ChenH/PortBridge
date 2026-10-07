#include "portbridge/session_controller.hpp"
#include "portbridge/network_engine.hpp"
#include "recorder.hpp"
#include "serial_engine.hpp"
#include <QElapsedTimer>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <chrono>
#include <condition_variable>
#include <thread>
#include <limits>

namespace portbridge {
struct SessionController::Impl {
    SessionController* owner;
    mutable std::mutex mutex;
    ConnectionConfig configuration;
    Statistics stats;
    bool isConnected = false, isConnecting = false, wanted = false, high = false, paused = false;
    QString status = QStringLiteral("Disconnected"), error;
    Endpoint local;
    std::map<std::uint64_t, ClientInfo> clientMap;
    bool stateDirty = false, clientsDirty = false, dataDirty = false;
    std::deque<QString> errors;
    std::deque<DataRecord> samples;
    size_t sampleBytes = 0, intervalSamples = 0;
    std::uint64_t ordinal = 0, udpSession = 0, generation = 0, nextTcpId = 1;
    std::map<std::uint64_t, std::uint64_t> tcpIds;
    struct SequenceState {
        bool initialized = false;
        std::uint64_t base = 0, highest = 0, baseEpoch = 0, highestEpoch = 0;
        std::set<std::pair<std::uint64_t, std::uint64_t>> seen;
    };
    std::map<std::string, SequenceState> sequences;
    size_t sequenceEntries = 0;
    void addMissing(std::uint64_t n) {
        const auto maximum = std::numeric_limits<std::uint64_t>::max();
        stats.sequenceMissing = n > maximum - stats.sequenceMissing ? maximum : stats.sequenceMissing + n;
    }
    void addExpected(std::uint64_t n) {
        const auto maximum = std::numeric_limits<std::uint64_t>::max();
        stats.sequenceExpected = n > maximum - stats.sequenceExpected ? maximum : stats.sequenceExpected + n;
    }
    void finishSequence(const SequenceState& s) {
        if (s.initialized) {
            const auto span = s.highest - s.base;
            addMissing(span - std::uint64_t(s.seen.size() - 1));
            addExpected(span + 1);
        }
    }
    void finishSequences() {
        for (const auto& stream : sequences) finishSequence(stream.second);
        sequences.clear(); sequenceEntries = 0;
    }
    detail::Recorder recorder;
    std::unique_ptr<NetworkEngine> network;
    std::unique_ptr<detail::SerialEngine> serial;
    struct Engines { std::unique_ptr<NetworkEngine> network; std::unique_ptr<detail::SerialEngine> serial; };
    struct CleanupState {
        std::mutex mutex;
        std::condition_variable wake;
        std::deque<Engines> retired;
        size_t count = 0;
        bool stopping = false, finished = false;
    };
    struct CallbackGate { std::mutex mutex; bool alive = true; };
    std::shared_ptr<CleanupState> cleanup = std::make_shared<CleanupState>();
    std::shared_ptr<CallbackGate> callbackGate = std::make_shared<CallbackGate>();
    std::thread reaper;
    QTimer pump, periodic;
    QByteArray periodicPayload;
    std::optional<Endpoint> periodicUdpTarget;
    int sent = 0, count = 0;
    std::uint64_t target = 0;
    bool broadcast = false, wasRecording = false;
    std::uint64_t previousRx = 0, previousTx = 0, previousDatagrams = 0, previousFailures = 0;
    QElapsedTimer rateTimer;
    explicit Impl(SessionController* q) : owner(q) {
        reaper = std::thread([state = cleanup] {
            for (;;) {
                Engines old;
                {
                    std::unique_lock<std::mutex> lock(state->mutex);
                    state->wake.wait(lock, [&] { return state->stopping || !state->retired.empty(); });
                    if (state->retired.empty() && state->stopping) { state->finished = true; state->wake.notify_all(); break; }
                    old = std::move(state->retired.front()); state->retired.pop_front();
                }
                old.network.reset(); old.serial.reset();
                { std::lock_guard<std::mutex> lock(state->mutex); --state->count; }
            }
        });
        createEngines();
        pump.setInterval(50); pump.setTimerType(Qt::PreciseTimer);
        QObject::connect(&pump, &QTimer::timeout, q, [this] { poll(); });
        periodic.setTimerType(Qt::PreciseTimer);
        QObject::connect(&periodic, &QTimer::timeout, q, [this] {
            if (!owner->connected() || !owner->send(periodicPayload, target, broadcast, periodicUdpTarget)) { owner->stopPeriodic(); return; }
            if (sent < std::numeric_limits<int>::max()) ++sent;
            emit owner->periodicChanged(); if (count && sent >= count) owner->stopPeriodic();
        });
        rateTimer.start(); pump.start();
    }
    ~Impl() {
        pump.stop(); periodic.stop();
        { std::lock_guard<std::mutex> lock(callbackGate->mutex); callbackGate->alive = false; }
        retireEngines();
        bool finished;
        {
            std::unique_lock<std::mutex> lock(cleanup->mutex); cleanup->stopping = true; cleanup->wake.notify_all();
            finished = cleanup->wake.wait_for(lock, std::chrono::seconds(3), [&] { return cleanup->finished; });
        }
        if (finished) reaper.join(); else reaper.detach();
        // A detached cleanup worker owns its state/engines, while the closed gate prevents access to this object.
        recorder.stop();
    }
    bool canRetire() { std::lock_guard<std::mutex> lock(cleanup->mutex); return cleanup->count < 4; }
    void retireEngines() {
        std::lock_guard<std::mutex> lock(cleanup->mutex);
        cleanup->retired.push_back({std::move(network), std::move(serial)}); ++cleanup->count; cleanup->wake.notify_one();
    }
    void addError(const QString& message) {
        error = message.left(4096); if (errors.size() == 16) errors.pop_front(); errors.push_back(error); stateDirty = true;
    }
    void createEngines() {
        const auto token = generation;
        NetworkEngine::Callbacks callbacks;
        callbacks.onData = [this, token, gate = callbackGate](const DataRecord& record) {
            std::lock_guard<std::mutex> lock(gate->mutex); return !gate->alive || ingest(record, token);
        };
        callbacks.onEvent = [this, token, gate = callbackGate](const TransportEvent& event) {
            std::lock_guard<std::mutex> lock(gate->mutex); if (gate->alive) receiveEvent(event, token);
        };
        network = std::make_unique<NetworkEngine>(callbacks);
        serial = std::make_unique<detail::SerialEngine>(callbacks);
    }
    std::uint64_t tcpIdentity(std::uint64_t raw) {
        if (!tcpIds.count(raw)) tcpIds[raw] = nextTcpId++;
        return tcpIds[raw];
    }
    void recordEvent(const TransportEvent& source, std::uint64_t publicId) {
        // UDP socket lifecycle belongs to connection state, not the data log.
        // Keep actionable errors/rejections/truncation records and stream events.
        if (source.kind == EventKind::UdpTargetReady ||
            (configuration.kind == TransportKind::Udp &&
             (source.kind == EventKind::Bound || source.kind == EventKind::Disconnected))) return;
        const QStringList names{"Connecting", "Connected", "Bound", "Listening", "Disconnected", "ClientAdded", "ClientRemoved", "Error", "SendRejected", "ReceiveTruncated", "UdpTargetReady"};
        const auto message = QJsonDocument(QJsonObject{{"schemaVersion", 1}, {"event", names.value(int(source.kind), QStringLiteral("Unknown"))}, {"message", detail::text(source.message).left(4096)}, {"connectionId", QString::number(publicId)}, {"localAddress", detail::text(source.local.address)}, {"localPort", source.local.port}}).toJson(QJsonDocument::Compact);
        DataRecord r; r.timestampUs = detail::nowUs(); r.connectionId = source.connectionId; r.peer = source.peer; r.transport = configuration.kind; r.direction = Direction::System;
        r.payload = std::make_shared<const Bytes>(reinterpret_cast<const std::uint8_t*>(message.constData()), reinterpret_cast<const std::uint8_t*>(message.constData()) + message.size());
        const bool tcp = configuration.kind == TransportKind::TcpClient || configuration.kind == TransportKind::TcpServer;
        if (tcp) r.connectionId = publicId;
        store(std::move(r), tcp); // Control records never request TCP/serial retry and never affect RX/TX counters.
    }
    void receiveEvent(const TransportEvent& source, std::uint64_t token) {
        std::lock_guard<std::mutex> lock(mutex);
        if (token != generation) return;
        auto e = source;
        if (e.connectionId && (configuration.kind == TransportKind::TcpClient || configuration.kind == TransportKind::TcpServer)) e.connectionId = tcpIdentity(e.connectionId);
        const bool matching = wanted;
        if (!matching && e.kind != EventKind::SendRejected) return;
        switch (e.kind) {
        case EventKind::Connecting: isConnecting = true; isConnected = false; status = detail::text(e.message); break;
        case EventKind::Connected: case EventKind::Bound: case EventKind::Listening:
            isConnecting = false; isConnected = true; local = e.local; status = detail::text(e.message); break;
        case EventKind::Disconnected:
            isConnected = isConnecting = false; status = detail::text(e.message); clientMap.clear(); tcpIds.clear(); finishSequences(); clientsDirty = true; break;
        case EventKind::ClientAdded: clientMap[e.connectionId] = {e.connectionId, e.peer}; clientsDirty = true; break;
        case EventKind::ClientRemoved: clientMap.erase(e.connectionId); tcpIds.erase(source.connectionId); clientsDirty = true; break;
        case EventKind::Error:
            addError(detail::text(e.message));
            // Only terminal transport events change connection state; UDP may remain receive-only.
            break;
        case EventKind::ReceiveTruncated:
            if (stats.receiveTruncatedDatagrams != std::numeric_limits<std::uint64_t>::max()) ++stats.receiveTruncatedDatagrams;
            addError(detail::text(e.message)); break;
        case EventKind::UdpTargetReady: status = detail::text(e.message); break;
        case EventKind::SendRejected: addError(detail::text(e.message)); break;
        }
        recordEvent(source, e.connectionId);
        stateDirty = true;
    }
    void analyze(const DataRecord& r) {
        if (!configuration.sequenceAnalysis || r.transport != TransportKind::Udp || r.direction != Direction::Receive || !r.payload || r.payload->size() < configuration.sequenceOffset + 8) return;
        const auto key = std::to_string(r.connectionId) + ":" + r.peer.address + ":" + std::to_string(r.peer.port);
        if (!sequences.count(key) && sequences.size() >= 64) {
            finishSequence(sequences.begin()->second);
            sequenceEntries -= sequences.begin()->second.seen.size(); sequences.erase(sequences.begin());
            addError("Sequence peer limit reached; an old analysis window was discarded");
        }
        auto& state = sequences[key]; std::uint64_t value = 0;
        for (size_t i = 0; i < 8; ++i) { const auto byte = (*r.payload)[configuration.sequenceOffset + i]; if (configuration.sequenceBigEndian) value = (value << 8) | byte; else value |= std::uint64_t(byte) << (i * 8); }
        if (!state.initialized) { state.initialized = true; state.base = state.highest = value; state.seen.insert({0, value}); ++sequenceEntries; return; }
        const auto forward = value - state.highest; // modulo 2^64; the half range defines the forward epoch
        if (forward == (1ull << 63)) {
            finishSequence(state); sequenceEntries -= state.seen.size(); state = {};
            state.initialized = true; state.base = state.highest = value; state.seen.insert({0, value}); ++sequenceEntries;
            addError("Ambiguous half-range sequence jump; analysis epoch reset"); return;
        }
        const bool newer = forward < (1ull << 63);
        if (!newer && state.highest - value > state.highest - state.base) { ++stats.sequenceReordered; return; }
        const auto epoch = newer ? state.highestEpoch + (value < state.highest ? 1 : 0) : state.highestEpoch - (value > state.highest ? 1 : 0);
        const auto position = std::make_pair(epoch, value);
        if (state.seen.count(position)) { ++stats.sequenceDuplicates; return; }
        if (!newer) ++stats.sequenceReordered;
        if (newer) {
            const auto span = state.highest - state.base;
            const auto available = configuration.sequenceWindow - 1 - span;
            if (forward > available) {
                const auto advance = forward - available;
                const auto base = state.base + advance;
                const auto baseEpoch = state.baseEpoch + (base < state.base ? 1 : 0);
                const auto end = state.seen.lower_bound({baseEpoch, base});
                const auto present = std::uint64_t(std::distance(state.seen.begin(), end));
                addMissing(advance - present);
                addExpected(advance);
                state.seen.erase(state.seen.begin(), end); sequenceEntries -= size_t(present);
                state.base = base; state.baseEpoch = baseEpoch;
            }
            state.highest = value; state.highestEpoch = epoch;
        }
        if (sequenceEntries >= 1048576) {
            auto victim = sequences.begin(); if (victim->first == key) ++victim;
            if (victim != sequences.end()) {
                finishSequence(victim->second);
                sequenceEntries -= victim->second.seen.size(); sequences.erase(victim);
                addError("Sequence memory limit reached; an old analysis window was discarded");
            }
        }
        state.seen.insert(position); ++sequenceEntries;
    }
    bool ingest(const DataRecord& source, std::uint64_t token) {
        std::lock_guard<std::mutex> lock(mutex);
        if (token != generation || !wanted) return true;
        return store(source);
    }
    bool store(DataRecord r, bool normalizedIdentity = false) {
        if (!r.payload) r.payload = std::make_shared<const Bytes>();
        if (!r.timestampUs) r.timestampUs = detail::nowUs();
        // TCP IDs remain identical to clients(); disjoint namespaces prevent serial/UDP collisions.
        if (r.transport == TransportKind::Serial) r.connectionId = (1ull << 63) | generation;
        else if (r.transport == TransportKind::Udp) r.connectionId = (1ull << 62) | udpSession;
        else if (!normalizedIdentity && r.connectionId) r.connectionId = tcpIdentity(r.connectionId);
        r.sequence = ordinal + 1;
        const auto admission = recorder.enqueue(r);
        const bool overflow = admission == detail::Recorder::Admission::Full || admission == detail::Recorder::Admission::Invalid;
        const bool canRetry = r.direction == Direction::Receive && r.transport != TransportKind::Udp;
        if (overflow && canRetry && admission == detail::Recorder::Admission::Full) {
            // A block larger than the total queue cannot ever fit: fail recording instead of deadlocking TCP.
            // The recorder reports Invalid only for format limits; queue impossibility is detected below.
            const auto configuredBudget = recordingBudget;
            if (detail::retainedCost(r) <= configuredBudget) return false;
            recorder.markIncomplete("Record exceeds recording queue budget; capture stopped"); recorder.stop();
        } else if (overflow) {
            recorder.markIncomplete(admission == detail::Recorder::Admission::Invalid ? "Record exceeds capture format limit" : "Recording queue overflow; capture is incomplete");
            if (canRetry) recorder.stop();
        }
        ++ordinal;
        const auto size = r.payload->size();
        if (r.direction == Direction::Receive) { stats.rxBytes += size; if (r.transport == TransportKind::Udp) ++stats.rxDatagrams; }
        else if (r.direction == Direction::Transmit) stats.txBytes += size;
        analyze(r);
        if (overflow && r.transport == TransportKind::Udp && r.direction == Direction::Receive) { ++stats.applicationDroppedRecords; stats.applicationDroppedBytes += size; }
        const size_t byteLimit = high ? 2 * 1024 * 1024 : 10 * 1024 * 1024;
        const size_t recordLimit = high ? 2000 : 50000;
        const auto charge = detail::retainedCost(r);
        if (paused || (high && intervalSamples >= 20) || charge > byteLimit) { ++stats.displayOmitted; return true; }
        ++intervalSamples;
        while (!samples.empty() && (samples.size() >= recordLimit || sampleBytes > byteLimit - charge)) { sampleBytes -= detail::retainedCost(samples.front()); samples.pop_front(); ++stats.displayOmitted; }
        samples.push_back(std::move(r)); sampleBytes += charge; dataDirty = true; return true;
    }
    size_t recordingBudget = 0;
    void poll() {
        bool state, clients, data, active;
        std::uint64_t token;
        std::deque<QString> messages;
        {
            std::lock_guard<std::mutex> lock(mutex);
            token = generation;
            state = stateDirty; clients = clientsDirty; data = dataDirty && !paused; active = isConnected;
            stateDirty = clientsDirty = dataDirty = false; intervalSamples = 0; messages.swap(errors);
            const auto elapsed = rateTimer.restart();
            if (elapsed > 0) {
                stats.rxBytesPerSecond = double(stats.rxBytes - previousRx) * 1000 / double(elapsed);
                stats.txBytesPerSecond = double(stats.txBytes - previousTx) * 1000 / double(elapsed);
                stats.datagramsPerSecond = double(stats.rxDatagrams - previousDatagrams) * 1000 / double(elapsed);
            }
            previousRx = stats.rxBytes; previousTx = stats.txBytes; previousDatagrams = stats.rxDatagrams;
        }
        const auto recording = recorder.snapshot();
        if (!active) { if (periodic.isActive()) owner->stopPeriodic(); if (recording.active) recorder.stop(); }
        auto current = [&] { std::lock_guard<std::mutex> lock(mutex); return generation == token; };
        if (state) emit owner->stateChanged();
        if (!current()) return;
        if (clients) emit owner->clientsChanged();
        if (!current()) return;
        if (data) emit owner->dataAvailable();
        for (const auto& message : messages) { if (!current()) return; emit owner->errorOccurred(message); }
        if (!current()) return;
        if (recording.failures != previousFailures) { previousFailures = recording.failures; if (!recording.error.isEmpty()) { { std::lock_guard<std::mutex> lock(mutex); error = recording.error; } emit owner->errorOccurred(recording.error); } }
        if (recording.active != wasRecording) { wasRecording = recording.active; emit owner->recordingChanged(); }
        emit owner->statisticsChanged();
    }
};
SessionController::SessionController(QObject* parent) : QObject(parent), d(std::make_unique<Impl>(this)) {}
SessionController::~SessionController() { stop(); }
void SessionController::start(const ConnectionConfig& c) {
    stop(); const auto error = detail::validateConfig(c);
    if (!error.isEmpty() || (c.kind == TransportKind::Serial && c.serialPort.empty())) {
        const auto message = error.isEmpty() ? QStringLiteral("Serial port name is required") : error;
        { std::lock_guard<std::mutex> lock(d->mutex); d->error = message; d->status = message; } emit errorOccurred(message); emit stateChanged(); return;
    }
    if (!d->canRetire()) {
        const QString message = "Previous connections are still closing; retry after cleanup";
        { std::lock_guard<std::mutex> lock(d->mutex); d->error = message; d->status = message; }
        emit errorOccurred(message); emit stateChanged(); return;
    }
    std::uint64_t startGeneration;
    {
        std::lock_guard<std::mutex> lock(d->mutex); startGeneration = d->generation; d->configuration = c; d->isConnecting = d->wanted = true; d->isConnected = false; d->local = {}; d->error.clear(); d->status = "Connecting"; d->sequences.clear(); d->sequenceEntries = 0; d->stats.sequenceEnabled = c.sequenceAnalysis; if (c.kind == TransportKind::Udp) ++d->udpSession;
    }
    // OS name resolution can finish slowly after cancellation; retire old engines outside the GUI.
    d->retireEngines(); d->createEngines();
    emit stateChanged();
    // Direct Qt subscribers can stop/select/start another configuration while
    // handling Connecting. Never start their replacement engine with old c.
    { std::lock_guard<std::mutex> lock(d->mutex); if (d->generation != startGeneration || !d->wanted) return; }
    if (c.kind == TransportKind::Serial) d->serial->start(c); else d->network->start(c);
}
void SessionController::selectConfiguration(const ConnectionConfig& c) {
    stop();
    {
        std::lock_guard<std::mutex> lock(d->mutex);
        d->configuration = c; d->stats = {}; d->stats.sequenceEnabled = c.sequenceAnalysis && c.kind == TransportKind::Udp;
        d->error.clear(); d->errors.clear(); d->samples.clear(); d->sampleBytes = d->intervalSamples = 0;
        d->dataDirty = d->stateDirty = d->clientsDirty = false;
        d->previousRx = d->previousTx = d->previousDatagrams = 0;
        d->rateTimer.restart();
    }
    d->recorder.resetStatistics(true); d->previousFailures = 0;
    d->sent = 0; d->periodicPayload.clear();
    emit stateChanged(); emit statisticsChanged(); emit dataAvailable(); emit periodicChanged();
}
void SessionController::stop() {
    stopPeriodic();
    { std::lock_guard<std::mutex> lock(d->mutex);
      if (d->wanted || d->isConnected || d->isConnecting) {
          TransportEvent stopped; stopped.kind = EventKind::Disconnected; stopped.message = "Stopped by user or profile switch"; stopped.local = d->local;
          d->recordEvent(stopped, 0);
      }
      ++d->generation; d->tcpIds.clear(); d->wanted = d->isConnected = d->isConnecting = false; d->status = "Disconnected"; d->local = {}; d->clientMap.clear(); d->clientsDirty = true;
      d->finishSequences(); }
    stopRecording(); d->serial->stop(); d->network->stop(); emit stateChanged(); emit clientsChanged(); emit statisticsChanged();
}
bool SessionController::send(const QByteArray& payload, std::uint64_t clientId, bool broadcast, std::optional<Endpoint> udpTarget) {
    ConnectionConfig c;
    { std::lock_guard<std::mutex> lock(d->mutex); if (!d->isConnected) return false; c = d->configuration;
      if (clientId) { bool found = false; for (const auto& entry : d->tcpIds) if (entry.second == clientId) { clientId = entry.first; found = true; break; } if (!found) return false; } }
    if(c.kind==TransportKind::Udp&&!udpTarget)udpTarget=Endpoint{c.remoteAddress,c.remotePort};
    if (payload.isEmpty() && c.kind != TransportKind::Udp) { emit errorOccurred("Empty stream payload"); return false; }
    auto bytes = std::make_shared<const Bytes>(reinterpret_cast<const std::uint8_t*>(payload.constData()), reinterpret_cast<const std::uint8_t*>(payload.constData()) + payload.size());
    if(udpTarget&&c.kind!=TransportKind::Udp)return false;
    const bool accepted = c.kind == TransportKind::Serial ? d->serial->send(bytes) : d->network->send(bytes, clientId, broadcast, udpTarget);
    if (!accepted) { { std::lock_guard<std::mutex> lock(d->mutex); d->error = "Send rejected: disconnected, invalid target or bounded queue full"; } emit errorOccurred(lastError()); }
    return accepted;
}
bool SessionController::startPeriodic(const QByteArray& payload, int intervalMs, int count, std::uint64_t clientId, bool broadcast, std::optional<Endpoint> udpTarget) {
    stopPeriodic();
    if (!connected() || intervalMs < 1 || intervalMs > 86400000 || count < 0 || (payload.isEmpty() && config().kind != TransportKind::Udp)) { emit errorOccurred("Invalid periodic payload, interval, count or connection"); return false; }
    const auto configuration=config();if(configuration.kind==TransportKind::Udp&&!udpTarget)udpTarget=Endpoint{configuration.remoteAddress,configuration.remotePort};
    if(udpTarget&&(configuration.kind!=TransportKind::Udp||udpTarget->address.empty()||udpTarget->address.size()>253||udpTarget->address.find_first_of(" \t\r\n")!=std::string::npos||!udpTarget->port))return false;
    d->periodicUdpTarget=std::move(udpTarget);
    d->periodicPayload = payload; d->sent = 0; d->count = count; d->target = clientId; d->broadcast = broadcast; d->periodic.start(intervalMs); emit periodicChanged(); return true;
}
bool SessionController::setUdpTarget(const QString& address, int port, QString* error) {
    if (error) error->clear();
    auto reject = [error](const QString& reason) { if (error) *error = reason; return false; };
    if (periodicActive()) return reject(QStringLiteral("请先停止周期发送，再更改 UDP 发送目标。"));
    const auto host = address.trimmed();
    if (host.isEmpty() || host.size() > 253 || std::any_of(host.begin(), host.end(), [](QChar ch) { return ch.isSpace(); }) || port < 1 || port > 65535)
        return reject(QStringLiteral("UDP 发送目标需要有效 IP / 域名和 1–65535 端口。"));
    {
        std::lock_guard<std::mutex> lock(d->mutex);
        if (d->configuration.kind != TransportKind::Udp || !d->isConnected)
            return reject(QStringLiteral("请先绑定 UDP 本地端口。"));
        if (!d->network->setUdpTarget({detail::utf8(host), std::uint16_t(port)}))
            return reject(QStringLiteral("目标正在解析或仍有待发送数据，请稍后重试。"));
        d->configuration.remoteAddress = detail::utf8(host); d->configuration.remotePort = std::uint16_t(port);
    }
    emit stateChanged();
    return true;
}
bool SessionController::udpTargetReady() const { return connected() && config().kind == TransportKind::Udp && d->network->udpTargetReady(); }
void SessionController::stopPeriodic() { if (d->periodic.isActive()) { d->periodic.stop(); emit periodicChanged(); } }
bool SessionController::periodicActive() const { return d->periodic.isActive(); }
int SessionController::periodicSent() const { return d->sent; }
bool SessionController::connected() const { std::lock_guard<std::mutex> lock(d->mutex); return d->isConnected; }
bool SessionController::connecting() const { std::lock_guard<std::mutex> lock(d->mutex); return d->isConnecting; }
QString SessionController::statusText() const { std::lock_guard<std::mutex> lock(d->mutex); return d->status; }
QString SessionController::lastError() const { std::lock_guard<std::mutex> lock(d->mutex); return d->error; }
ConnectionConfig SessionController::config() const { std::lock_guard<std::mutex> lock(d->mutex); return d->configuration; }
Endpoint SessionController::localEndpoint() const { std::lock_guard<std::mutex> lock(d->mutex); return d->local; }
std::vector<ClientInfo> SessionController::clients() const { std::lock_guard<std::mutex> lock(d->mutex); std::vector<ClientInfo> out; for (const auto& item : d->clientMap) out.push_back(item.second); return out; }
void SessionController::disconnectClient(std::uint64_t id) {
    std::uint64_t raw = 0;
    { std::lock_guard<std::mutex> lock(d->mutex); for (const auto& entry : d->tcpIds) if (entry.second == id) raw = entry.first; }
    if (raw) d->network->disconnectClient(raw);
}
void SessionController::setHighSpeed(bool enabled) {
    { std::lock_guard<std::mutex> lock(d->mutex); d->high = enabled; const size_t byteLimit = enabled ? 2 * 1024 * 1024 : 10 * 1024 * 1024; const size_t recordLimit = enabled ? 2000 : 50000; while (!d->samples.empty() && (d->samples.size() > recordLimit || d->sampleBytes > byteLimit)) { d->sampleBytes -= detail::retainedCost(d->samples.front()); d->samples.pop_front(); ++d->stats.displayOmitted; } }
    emit stateChanged();
}
bool SessionController::highSpeed() const { std::lock_guard<std::mutex> lock(d->mutex); return d->high; }
void SessionController::setDisplayPaused(bool paused) { { std::lock_guard<std::mutex> lock(d->mutex); d->paused = paused; if (!paused && !d->samples.empty()) d->dataDirty = true; } emit stateChanged(); }
bool SessionController::displayPaused() const { std::lock_guard<std::mutex> lock(d->mutex); return d->paused; }
void SessionController::clearDisplay() { { std::lock_guard<std::mutex> lock(d->mutex); d->samples.clear(); d->sampleBytes = 0; d->dataDirty = false; } emit dataAvailable(); }
void SessionController::resetStatistics() {
    { std::lock_guard<std::mutex> lock(d->mutex); d->stats = {}; d->stats.sequenceEnabled = d->configuration.sequenceAnalysis; d->sequences.clear(); d->sequenceEntries = 0; d->previousRx = d->previousTx = d->previousDatagrams = 0; }
    d->recorder.resetStatistics(); d->previousFailures = 0; emit statisticsChanged();
}
Statistics SessionController::statistics() const {
    Statistics result; bool serial;
    { std::lock_guard<std::mutex> lock(d->mutex); result = d->stats; result.sampleQueueBytes = d->sampleBytes; serial = d->configuration.kind == TransportKind::Serial; }
    const auto recording = d->recorder.snapshot(); result.recordedBytes = recording.bytes; result.recordedRecords = recording.records; result.recordingFailures = recording.failures; result.recordingQueueBytes = recording.queued; result.recordingQueueHighWater = recording.highWater;
    if (!connected() && !connecting()) return result;
    result.pendingSendBytes = serial ? d->serial->pendingSendBytes() : d->network->pendingSendBytes();
    if (!serial) { result.actualReceiveBufferBytes = d->network->actualReceiveBufferBytes(); result.actualSendBufferBytes = d->network->actualSendBufferBytes(); }
    return result;
}
std::vector<DataRecord> SessionController::takeDisplayRecords() {
    std::lock_guard<std::mutex> lock(d->mutex); std::vector<DataRecord> out; if (d->paused) return out;
    size_t bytes = 0;
    while (!d->samples.empty() && out.size() < 256 && bytes < 1024 * 1024) { bytes += d->samples.front().payload->size(); d->sampleBytes -= detail::retainedCost(d->samples.front()); out.push_back(std::move(d->samples.front())); d->samples.pop_front(); }
    if (!d->samples.empty()) d->dataDirty = true;
    return out;
}
bool SessionController::startRecording(const RecordingOptions& options, QString* error) {
    if (error) error->clear();
    if (!connected()) { if (error) *error = "An active connection is required for recording"; return false; }
    { std::lock_guard<std::mutex> lock(d->mutex); d->recordingBudget = options.queueBytes; }
    if (!d->recorder.start(options, error)) return false;
    d->wasRecording = true; emit recordingChanged(); return true;
}
void SessionController::stopRecording() { const bool was = d->recorder.snapshot().active; d->recorder.stop(); if (was) { d->wasRecording = false; emit recordingChanged(); } }
bool SessionController::recording() const { return d->recorder.snapshot().active; }
std::vector<CaptureInfo> SessionController::captures() const { return d->recorder.captures(); }
void SessionController::refreshCaptures(const QString& directory) { d->recorder.refresh(directory); }

std::uint64_t detailSessionTestAccess::generation(const SessionController& session) {
    std::lock_guard<std::mutex> lock(session.d->mutex); return session.d->generation;
}
void detailSessionTestAccess::event(SessionController& session, const TransportEvent& event, std::uint64_t token) { session.d->receiveEvent(event, token); }
bool detailSessionTestAccess::data(SessionController& session, const DataRecord& record, std::uint64_t token) { return session.d->ingest(record, token); }
}
