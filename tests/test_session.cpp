#include "portbridge/session_controller.hpp"
#include "portbridge/network_engine.hpp"
#include "../src/session/recorder.hpp"
#include <QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <atomic>
#include <algorithm>
#include <mutex>
#include <limits>
#include <thread>

using namespace portbridge;
namespace {
SharedBytes raw(const QByteArray& b) { return std::make_shared<const Bytes>(reinterpret_cast<const std::uint8_t*>(b.constData()), reinterpret_cast<const std::uint8_t*>(b.constData()) + b.size()); }
QByteArray bytes(const SharedBytes& b) { return b ? QByteArray(reinterpret_cast<const char*>(b->data()), qsizetype(b->size())) : QByteArray(); }
QString qpath(const std::string& s) { return QString::fromUtf8(s.data(), qsizetype(s.size())); }
std::string path(const QString& s) { const auto b = s.toUtf8(); return std::string(b.constData(), size_t(b.size())); }
bool writeFile(const QString& name, const QByteArray& b) { QFile f(name); return f.open(QIODevice::WriteOnly) && f.write(b) == b.size(); }
QByteArray readFile(const QString& name) { QFile f(name); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }
ConnectionConfig udpConfig(std::uint16_t remote = 9) { ConnectionConfig c; c.localAddress = "127.0.0.1"; c.localPort = 0; c.remotePort = remote; return c; }
struct Peer {
    std::atomic<bool> ready{false};
    std::atomic<size_t> received{0}, sent{0};
    std::mutex mutex;
    QByteArray data;
    NetworkEngine engine;
    Peer() : engine({[this](const DataRecord& r) {
        if (r.direction == Direction::Receive) { std::lock_guard<std::mutex> lock(mutex); data += bytes(r.payload); received += r.payload->size(); }
        else if (r.direction == Direction::Transmit) sent += r.payload->size();
        return true;
    }, [this](const TransportEvent& e) { if (e.kind == EventKind::UdpTargetReady || e.kind == EventKind::Connected || e.kind == EventKind::Listening) ready = true; }}) {}
};
DataRecord record(const QByteArray& b, std::uint64_t n = 1) {
    DataRecord r; r.sequence = n; r.timestampUs = 1700000000000000ull + n; r.connectionId = 42; r.peer = {"127.0.0.1", 1234}; r.direction = n % 2 ? Direction::Receive : Direction::Transmit; r.payload = raw(b); return r;
}
}
class SessionTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("PortBridgeTests"); QCoreApplication::setApplicationName("Session"); QStandardPaths::setTestModeEnabled(true);
    }
    void codec() {
        QString error;
        QCOMPARE(encodePayload("00 01\t7f80\nFF", true, "UTF-8", "CRLF", &error).toHex(), QByteArray("00017f80ff0d0a")); QVERIFY(error.isEmpty());
        QVERIFY(encodePayload("A B C", true, "UTF-8", "none", &error).isEmpty()); QVERIFY(error.contains("pairs"));
        QVERIFY(encodePayload("AA 0xBB", true, "UTF-8", "none", &error).isEmpty()); QVERIFY(error.contains("position"));
        QVERIFY(encodePayload("GG", true, "UTF-8", "none", &error).isEmpty()); QVERIFY(!error.isEmpty());
        const QString chinese = QString::fromUtf8("串口中文😀"); QCOMPARE(encodePayload(chinese, false, "UTF-8", "LF", &error), chinese.toUtf8() + '\n'); QVERIFY(error.isEmpty());
        QVERIFY(encodePayload(chinese, false, "ASCII", "none", &error).isEmpty()); QVERIFY(error.contains("ASCII"));
        QCOMPARE(encodePayload("abc", false, "ASCII", "CR", &error), QByteArray("abc\r"));
        QCOMPARE(encodePayload("abc", false, "UTF-8", "none", &error), QByteArray("abc"));
        QVERIFY(encodePayload("abc", false, "GBK", "none", &error).isEmpty()); QVERIFY(!error.isEmpty());
        QVERIFY(encodePayload("abc", false, "ASCII", "bad", &error).isEmpty()); QVERIFY(!error.isEmpty());
        QVERIFY(encodePayload(QString(QChar(0xd800)), false, "UTF-8", "none", &error).isEmpty()); QVERIFY(!error.isEmpty());
        QCOMPARE(formatHex(raw(QByteArray::fromHex("007f80ff"))), QString("00 7F 80 FF"));
        QCOMPARE(encodePayload("", true, "UTF-8", "none", &error), QByteArray()); QVERIFY(error.isEmpty());
    }
    void profilesAndCommands() {
        QTemporaryDir temp; QVERIFY(temp.isValid()); QSettings().setValue("storage/directory", temp.path());
        QString error; auto profiles = loadProfiles(&error); QCOMPARE(profiles.size(), 4); QVERIFY(error.isEmpty());
        profiles[0].serialPort = path(QString::fromUtf8("测试端口")); profiles[0].parity = "Odd"; profiles[0].flowControl = "Hardware"; profiles[0].stopBits = "2";
        profiles[3].sequenceAnalysis = true; profiles[3].sequenceOffset = 16; profiles[3].sequenceBigEndian = false; profiles[3].sequenceWindow = 128;
        QVERIFY2(saveProfiles(profiles, &error), qPrintable(error)); auto loaded = loadProfiles(&error); QVERIFY(error.isEmpty()); QCOMPARE(loaded[0].serialPort, profiles[0].serialPort); QCOMPARE(loaded[3].sequenceOffset, size_t(16)); QCOMPARE(loaded[3].sequenceBigEndian, false);
        const auto target = temp.path() + QString::fromUtf8("/连接方案.json"); QVERIFY(exportProfiles(target, profiles, &error));
        auto object = QJsonDocument::fromJson(readFile(target)).object(); object["schemaVersion"] = 99; QVERIFY(writeFile(target, QJsonDocument(object).toJson()));
        QVector<ConnectionConfig> retained{profiles[2]}; QVERIFY(!importProfiles(target, &retained, &error)); QCOMPARE(retained.size(), 1); QCOMPARE(retained[0].kind, TransportKind::TcpServer);
        QVERIFY(exportProfiles(target, profiles, &error)); object = QJsonDocument::fromJson(readFile(target)).object(); auto array = object["profiles"].toArray(); auto invalid = array[0].toObject(); invalid["baudRate"] = "115200"; array[0] = invalid; object["profiles"] = array; QVERIFY(writeFile(target, QJsonDocument(object).toJson()));
        QVERIFY(!importProfiles(target, &retained, &error)); QCOMPARE(retained.size(), 1);
        QVERIFY(writeFile(temp.path() + "/profiles.json", "{bad")); loaded = loadProfiles(&error); QCOMPARE(loaded.size(), 4); QVERIFY(!error.isEmpty());
        QVector<Command> commands{{QString::fromUtf8("读设备"), "00 FF", true, "UTF-8", "CRLF"}, {"Text", QString::fromUtf8("中文"), false, "UTF-8", "none"}};
        QVERIFY(saveCommands(commands, &error)); auto restored = loadCommands(&error); QCOMPARE(restored.size(), 2); QCOMPARE(restored[1].input, commands[1].input);
        const auto commandPath = temp.path() + "/commands-import.json"; QVERIFY(exportCommands(commandPath, commands, &error));
        const auto prior = readFile(commandPath); commands[0].input = "0"; QVERIFY(!exportCommands(commandPath, commands, &error)); QCOMPARE(readFile(commandPath), prior);
        QVERIFY(writeFile(commandPath, "{\"schemaVersion\":1,\"commands\":[{\"name\":3}]}")); QVERIFY(!importCommands(commandPath, &restored, &error)); QCOMPARE(restored.size(), 2);
        QVERIFY(writeFile(temp.path() + "/commands.json", "broken")); QVERIFY(loadCommands(&error).isEmpty()); QVERIFY(!error.isEmpty());
    }
    void configSizePreservesPriorFile() {
        QTemporaryDir temp; const auto target = temp.path() + "/commands.json"; QString error;
        QVector<Command> commands{{"small", "ok", false, "UTF-8", "none"}}; QVERIFY(exportCommands(target, commands, &error)); const auto prior = readFile(target);
        commands.clear(); const QString million(1024 * 1024, QChar('a')); for (int i = 0; i < 17; ++i) commands.push_back({"large", million, false, "UTF-8", "none"});
        QVERIFY(!exportCommands(target, commands, &error)); QVERIFY(error.contains("16 MiB")); QCOMPARE(readFile(target), prior);
    }
    void periodicFiniteAndStop() {
        SessionController session; ConnectionConfig c; c.kind = TransportKind::TcpServer; c.localAddress = "127.0.0.1"; c.localPort = 0;
        QVERIFY(!session.connected()); QVERIFY(!session.periodicActive()); session.start(c); QTRY_VERIFY_WITH_TIMEOUT(session.connected(), 3000); QVERIFY(session.localEndpoint().port != 0);
        Peer peer; auto client = c; client.kind = TransportKind::TcpClient; client.remotePort = session.localEndpoint().port; peer.engine.start(client); QTRY_VERIFY_WITH_TIMEOUT(peer.ready.load(), 3000); QTRY_COMPARE_WITH_TIMEOUT(session.clients().size(), size_t(1), 3000);
        const auto id = session.clients()[0].id;
        QVERIFY(session.startPeriodic("abc", 10, 3, id)); QTRY_VERIFY_WITH_TIMEOUT(!session.periodicActive(), 3000); QCOMPARE(session.periodicSent(), 3); QTRY_COMPARE_WITH_TIMEOUT(peer.received.load(), size_t(9), 3000); QTRY_COMPARE_WITH_TIMEOUT(session.statistics().txBytes, std::uint64_t(9), 3000);
        QVERIFY(session.startPeriodic("x", 20, 0, id)); QTRY_VERIFY_WITH_TIMEOUT(session.periodicSent() >= 2, 3000); session.stopPeriodic(); QTest::qWait(100); const auto after = peer.received.load(); QTest::qWait(80); QCOMPARE(peer.received.load(), after);
        QVERIFY(session.startPeriodic("y", 20, 0, id)); session.disconnectClient(id); QTRY_VERIFY_WITH_TIMEOUT(session.clients().empty(), 3000); QTRY_VERIFY_WITH_TIMEOUT(!session.periodicActive(), 3000);
        session.stop(); QVERIFY(!session.connected()); QVERIFY(!session.periodicActive());
    }
    void captureRotationAndDecoder() {
        QTemporaryDir temp; detail::Recorder recorder; RecordingOptions options; options.directory = path(temp.path() + QString::fromUtf8("/原始记录")); options.rotateBytes = 180; options.queueBytes = 1024 * 1024;
        QString error; QVERIFY2(recorder.start(options, &error), qPrintable(error));
        const QByteArray payload = QByteArray::fromHex("00017f80ff0d0a");
        for (int i = 1; i <= 5; ++i) QCOMPARE(recorder.enqueue(record(payload, std::uint64_t(i))), detail::Recorder::Admission::Accepted);
        recorder.stop(); QTRY_COMPARE_WITH_TIMEOUT(recorder.snapshot().records, std::uint64_t(5), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!recorder.captures().empty() && recorder.captures().back().complete, 3000);
        const auto catalog = recorder.captures(); QVERIFY(catalog.size() >= 3); QByteArray reconstructed; std::uint64_t count = 0;
        for (const auto& capture : catalog) {
            QVERIFY(capture.complete); const auto json = qpath(capture.path) + ".json"; QVERIFY2(exportCapture(qpath(capture.path), json, &error), qPrintable(error));
            const auto o = QJsonDocument::fromJson(readFile(json)).object(); QVERIFY(o["complete"].toBool());
            for (const auto& entry : o["records"].toArray()) { const auto r = entry.toObject(); reconstructed += QByteArray::fromBase64(r["payloadBase64"].toString().toLatin1()); QCOMPARE(r["connectionId"].toString(), QString("42")); QCOMPARE(r["peer"].toString(), QString("127.0.0.1")); QCOMPARE(r["port"].toInt(), 1234); QVERIFY(r["direction"] == "RX" || r["direction"] == "TX"); ++count; }
            const auto txt = qpath(capture.path) + ".txt"; QVERIFY(exportCapture(qpath(capture.path), txt, &error)); QVERIFY(readFile(txt).contains("00 01 7f 80 ff 0d 0a"));
        }
        QCOMPARE(count, std::uint64_t(5)); QCOMPARE(reconstructed, payload.repeated(5));
        const auto output = temp.path() + "/preserved.json"; QVERIFY(writeFile(output, "keep")); const auto original = readFile(qpath(catalog[0].path)); const auto bad = temp.path() + "/bad.pbc";
        QVERIFY(writeFile(bad, original.left(original.size() - 1))); QVERIFY(!exportCapture(bad, output, &error)); QCOMPARE(readFile(output), QByteArray("keep"));
        auto corrupted = original; corrupted[8] = char(99); QVERIFY(writeFile(bad, corrupted)); QVERIFY(!exportCapture(bad, output, &error));
        corrupted = original; corrupted[28] = char(0xff); corrupted[29] = char(0xff); corrupted[30] = char(0xff); corrupted[31] = char(0x7f); QVERIFY(writeFile(bad, corrupted)); QVERIFY(!exportCapture(bad, output, &error)); QCOMPARE(readFile(output), QByteArray("keep"));
        QVERIFY(!exportCapture(qpath(catalog[0].path), output, &error, [](std::uint64_t done, std::uint64_t) { return done <= 24; }));
        QVERIFY(error.contains("canceled")); QCOMPARE(readFile(output), QByteArray("keep"));
        std::uint64_t lastProgress = 0, expectedTotal = 0; bool monotonic = true;
        QVERIFY(exportCapture(qpath(catalog[0].path), output, &error, [&](std::uint64_t done, std::uint64_t total) { if (done < lastProgress || done > total) monotonic = false; lastProgress = done; expectedTotal = total; return true; }));
        QVERIFY(monotonic); QCOMPARE(lastProgress, expectedTotal); QVERIFY(expectedTotal > 0);
        detail::Recorder reloaded; QTRY_COMPARE_WITH_TIMEOUT(reloaded.captures().size(), catalog.size(), 3000);
    }
    void captureQueueDurationAndFailures() {
        QTemporaryDir temp; detail::Recorder recorder; RecordingOptions options; options.directory = path(temp.path()); options.queueBytes = 128; options.rotateBytes = 1024;
        QString error; QVERIFY(recorder.start(options, &error)); QCOMPARE(recorder.enqueue(record("payload")), detail::Recorder::Admission::Full); recorder.markIncomplete("queue overflow regression"); recorder.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!recorder.captures().empty() && !recorder.captures().back().error.empty(), 3000); QVERIFY(!recorder.captures().back().complete); QCOMPARE(recorder.snapshot().failures, std::uint64_t(1));
        const auto json = temp.path() + "/incomplete.json"; QTRY_VERIFY_WITH_TIMEOUT(exportCapture(qpath(recorder.captures().back().path), json, &error), 3000); QVERIFY(!QJsonDocument::fromJson(readFile(json)).object()["complete"].toBool());
        detail::Recorder duration; options.queueBytes = 4096; options.durationSeconds = 1; QVERIFY(duration.start(options, &error)); QTRY_VERIFY_WITH_TIMEOUT(!duration.snapshot().active, 2000); QTRY_VERIFY_WITH_TIMEOUT(!duration.captures().empty() && duration.captures().back().complete, 2000);
        detail::Recorder continuous; options.durationSeconds = 0; QVERIFY(continuous.start(options, &error)); QTest::qWait(1100); QVERIFY(continuous.snapshot().active); continuous.stop();
        const auto file = temp.path() + "/blocker"; QVERIFY(writeFile(file, "file")); options.directory = path(file + "/child"); detail::Recorder invalid; QVERIFY(invalid.start(options, &error)); QTRY_VERIFY_WITH_TIMEOUT(invalid.snapshot().failures > 0, 3000); QVERIFY(!invalid.snapshot().error.isEmpty());
        // Force a real QSaveFile catalog error without relying on platform-specific disk permissions.
        detail::Recorder catalogError; options.directory = path(temp.path() + "/catalog-error"); options.rotateBytes = 128;
        const auto priorCount = catalogError.captures().size(); QVERIFY(catalogError.start(options, &error)); QTRY_VERIFY_WITH_TIMEOUT(catalogError.captures().size() > priorCount, 3000);
        const auto metadata = qpath(catalogError.captures().back().path) + ".meta.json";
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(metadata), 3000); QVERIFY(QFile::remove(metadata)); QVERIFY(QDir().mkpath(metadata));
        QCOMPARE(catalogError.enqueue(record("payload", 1)), detail::Recorder::Admission::Accepted);
        QCOMPARE(catalogError.enqueue(record("payload", 2)), detail::Recorder::Admission::Accepted);
        QTRY_VERIFY_WITH_TIMEOUT(catalogError.snapshot().failures > 0, 3000); QTRY_VERIFY_WITH_TIMEOUT(!catalogError.snapshot().active, 3000);
        QCOMPARE(catalogError.captures().size(), priorCount + 1); QVERIFY(!catalogError.captures().back().complete); QVERIFY(!catalogError.snapshot().error.isEmpty());
    }
    void displayPauseSequenceAndOverflow() {
        QTemporaryDir temp; SessionController session; auto config = udpConfig(); config.sequenceAnalysis = true; config.sequenceWindow = 3; session.start(config); QTRY_VERIFY_WITH_TIMEOUT(session.connected(), 3000);
        Peer peer; peer.engine.start(udpConfig(session.localEndpoint().port)); QTRY_VERIFY_WITH_TIMEOUT(peer.ready.load(), 3000);
        auto transmit = [&](std::uint64_t n) { QByteArray b; for (int shift = 56; shift >= 0; shift -= 8) b.append(char(n >> shift)); return peer.engine.send(raw(b)); };
        QVERIFY(transmit(100)); QVERIFY(transmit(102)); QVERIFY(transmit(101)); QVERIFY(transmit(102)); QVERIFY(transmit(106));
        QTRY_COMPARE_WITH_TIMEOUT(session.statistics().rxDatagrams, std::uint64_t(5), 3000); QCOMPARE(session.statistics().sequenceDuplicates, std::uint64_t(1)); QCOMPARE(session.statistics().sequenceReordered, std::uint64_t(1)); QCOMPARE(session.statistics().sequenceMissing, std::uint64_t(1));
        session.setDisplayPaused(true); const auto before = session.statistics().displayOmitted; QVERIFY(transmit(107)); QTRY_COMPARE_WITH_TIMEOUT(session.statistics().rxDatagrams, std::uint64_t(6), 3000); QVERIFY(session.takeDisplayRecords().empty()); QCOMPARE(session.statistics().displayOmitted, before + 1);
        session.clearDisplay(); QCOMPARE(session.statistics().rxBytes, std::uint64_t(48)); session.setDisplayPaused(false); QVERIFY(session.takeDisplayRecords().empty());
        RecordingOptions options; options.directory = path(temp.path()); options.queueBytes = 128; options.rotateBytes = 1024; QString error; QVERIFY(session.startRecording(options, &error)); QVERIFY(transmit(108)); QTRY_COMPARE_WITH_TIMEOUT(session.statistics().rxDatagrams, std::uint64_t(7), 3000); QCOMPARE(session.statistics().applicationDroppedRecords, std::uint64_t(1)); QVERIFY(session.statistics().recordingFailures > 0); session.stopRecording();
        session.setHighSpeed(true); for (int i = 0; i < 100; ++i) QVERIFY(transmit(std::uint64_t(200 + i))); QTRY_VERIFY_WITH_TIMEOUT(session.statistics().rxDatagrams >= 107, 3000); QVERIFY(session.statistics().displayOmitted > before + 1); QVERIFY(session.statistics().sampleQueueBytes <= 2 * 1024 * 1024);
        const auto records = session.takeDisplayRecords(); QVERIFY(records.size() <= 256); QVERIFY(!records.empty()); QVERIFY(records[0].sequence != 200); // local ordinal is separate from protocol sequence
        session.resetStatistics(); QCOMPARE(session.statistics().rxBytes, std::uint64_t(0)); QCOMPARE(session.statistics().sequenceMissing, std::uint64_t(0)); session.stop();
    }
    void normalDisplayByteBoundAndSwitch() {
        SessionController session; auto config = udpConfig(); session.start(config); QTRY_VERIFY_WITH_TIMEOUT(session.connected(), 3000);
        Peer peer; peer.engine.start(udpConfig(session.localEndpoint().port)); QTRY_VERIFY_WITH_TIMEOUT(peer.ready.load(), 3000);
        const auto payload = raw(QByteArray(60000, 'a'));
        // Pace to measure application display eviction rather than uncontrolled kernel UDP loss.
        for (int i = 0; i < 200; ++i) { QVERIFY(peer.engine.send(payload)); QTRY_COMPARE_WITH_TIMEOUT(session.statistics().rxDatagrams, std::uint64_t(i + 1), 3000); }
        QVERIFY(session.statistics().sampleQueueBytes <= 10 * 1024 * 1024); QVERIFY(session.statistics().displayOmitted > 0); QCOMPARE(session.statistics().applicationDroppedRecords, std::uint64_t(0));
        const auto rx = session.statistics().rxBytes; session.clearDisplay(); QCOMPARE(session.statistics().rxBytes, rx);
        session.start(config); QTRY_VERIFY_WITH_TIMEOUT(session.connected(), 3000); session.resetStatistics(); QTest::qWait(80); QCOMPARE(session.statistics().rxBytes, std::uint64_t(0));
        QVERIFY(session.lastError().isEmpty()); session.stop();
    }
    void captureCatalogBoundKeepsFilesAndReloads() {
        QTemporaryDir temp; detail::Recorder recorder; RecordingOptions options; options.directory = path(temp.path()); options.queueBytes = 1024 * 1024; options.rotateBytes = 128; QString error;
        QVERIFY(recorder.start(options, &error));
        for (int i = 1; i <= 1028; ++i) QCOMPARE(recorder.enqueue(record("", std::uint64_t(i))), detail::Recorder::Admission::Accepted);
        recorder.stop(); QTRY_COMPARE_WITH_TIMEOUT(recorder.snapshot().records, std::uint64_t(1028), 15000); QTRY_VERIFY_WITH_TIMEOUT(!recorder.captures().empty() && recorder.captures().back().complete, 3000);
        QCOMPARE(recorder.captures().size(), size_t(1024)); const auto files = QDir(temp.path()).entryList({"*.pbc"}, QDir::Files); QCOMPARE(files.size(), qsizetype(1028));
        QVERIFY(exportCapture(QDir(temp.path()).filePath(files.front()), temp.path() + "/older-file.json", &error));
        detail::Recorder reloaded; QTRY_COMPARE_WITH_TIMEOUT(reloaded.captures().size(), size_t(1024), 5000);
        reloaded.refresh(temp.path()); reloaded.refresh(temp.path()); QTest::qWait(100); QCOMPARE(reloaded.captures().size(), size_t(1024));
    }
    void captureCompletenessPublicationObserver() {
        QTemporaryDir temp; detail::Recorder recorder; RecordingOptions options; options.directory = path(temp.path()); options.rotateBytes = 128; options.queueBytes = 1024 * 1024; QString error;
        QVERIFY(recorder.start(options, &error)); std::atomic<bool> observe{true}, premature{false};
        // Same observer as the independent reproducer: read durable metadata only after complete is published.
        std::thread observer([&] {
            while (observe.load() && !premature.load()) {
                const auto captures = recorder.captures();
                if (captures.empty() || !captures.back().complete) { std::this_thread::yield(); continue; }
                QFile metadata(qpath(captures.back().path) + ".meta.json");
                if (!metadata.open(QIODevice::ReadOnly)) continue;
                const auto o = QJsonDocument::fromJson(metadata.readAll()).object();
                if (o["complete"].isBool() && !o["complete"].toBool()) premature = true;
            }
        });
        unsigned admitted = 0;
        for (unsigned n = 1; n <= 100; ++n) { if (recorder.enqueue(record("data", n)) != detail::Recorder::Admission::Accepted) break; ++admitted; }
        recorder.stop(); QElapsedTimer deadline; deadline.start();
        while (deadline.elapsed() < 10000) {
            const auto state = recorder.snapshot(); const auto captures = recorder.captures();
            if (state.failures || (state.records == admitted && !captures.empty() && captures.back().complete)) break;
            QTest::qWait(1);
        }
        observe = false; observer.join();
        QCOMPARE(admitted, unsigned(100)); QVERIFY(!premature.load()); QCOMPARE(recorder.snapshot().records, std::uint64_t(100)); QCOMPARE(recorder.snapshot().failures, std::uint64_t(0));
    }
    void tcpCaptureBackpressureAndStableClientIds() {
        QTemporaryDir temp; SessionController session; auto config = udpConfig(); config.kind = TransportKind::TcpServer;
        session.start(config); QTRY_VERIFY_WITH_TIMEOUT(session.connected(), 3000);
        Peer peer; auto client = config; client.kind = TransportKind::TcpClient; client.remotePort = session.localEndpoint().port; peer.engine.start(client);
        QTRY_VERIFY_WITH_TIMEOUT(peer.ready.load(), 3000); QTRY_COMPARE_WITH_TIMEOUT(session.clients().size(), size_t(1), 3000); const auto id = session.clients()[0].id;
        RecordingOptions options; options.directory = path(temp.path()); options.queueBytes = 70000; QString error; QVERIFY(session.startRecording(options, &error));
        const QByteArray payload = QByteArray::fromHex("00017f80ff").repeated(209716); QVERIFY(peer.engine.send(raw(payload)));
        QTRY_COMPARE_WITH_TIMEOUT(session.statistics().rxBytes, std::uint64_t(payload.size()), 5000); QTRY_COMPARE_WITH_TIMEOUT(session.statistics().recordedBytes, std::uint64_t(payload.size()), 5000);
        QCOMPARE(session.statistics().applicationDroppedRecords, std::uint64_t(0)); QCOMPARE(session.statistics().recordingFailures, std::uint64_t(0)); QVERIFY(session.statistics().recordingQueueHighWater <= options.queueBytes);
        session.stopRecording(); QTRY_VERIFY_WITH_TIMEOUT(!session.captures().empty() && session.captures().back().complete, 3000);
        const auto json = temp.path() + "/tcp.json"; QVERIFY(exportCapture(qpath(session.captures().back().path), json, &error)); QByteArray reconstructed;
        for (const auto& r : QJsonDocument::fromJson(readFile(json)).object()["records"].toArray()) { const auto o = r.toObject(); QCOMPARE(o["connectionId"].toString().toULongLong(), qulonglong(id)); reconstructed += QByteArray::fromBase64(o["payloadBase64"].toString().toLatin1()); }
        QCOMPARE(reconstructed, payload);
        // Replace a live same-transport session while its old peer continues sending.
        for (int i = 0; i < 8; ++i) peer.engine.send(raw(QByteArray(65536, 'z')));
        QElapsedTimer switchTime; switchTime.start(); session.start(config); QVERIFY(switchTime.elapsed() < 500); QTRY_VERIFY_WITH_TIMEOUT(session.connected(), 3000); session.resetStatistics(); session.clearDisplay(); QTest::qWait(100); QCOMPARE(session.statistics().rxBytes, std::uint64_t(0)); QVERIFY(session.takeDisplayRecords().empty());
        Peer second; client.remotePort = session.localEndpoint().port; second.engine.start(client); QTRY_VERIFY_WITH_TIMEOUT(second.ready.load(), 3000); QTRY_COMPARE_WITH_TIMEOUT(session.clients().size(), size_t(1), 3000); QVERIFY(session.clients()[0].id > id); session.stop();
    }
    void sequenceLittleEndianOffsetAndLargeGap() {
        SessionController session; auto config = udpConfig(); config.sequenceAnalysis = true; config.sequenceOffset = 2; config.sequenceBigEndian = false; config.sequenceWindow = 4;
        session.start(config); QTRY_VERIFY_WITH_TIMEOUT(session.connected(), 3000); Peer peer; peer.engine.start(udpConfig(session.localEndpoint().port)); QTRY_VERIFY_WITH_TIMEOUT(peer.ready.load(), 3000);
        auto transmit = [&](std::uint64_t n) { QByteArray b("PB", 2); for (int shift = 0; shift < 64; shift += 8) b.append(char(n >> shift)); return peer.engine.send(raw(b)); };
        QVERIFY(transmit(0)); QVERIFY(transmit(1)); const std::uint64_t large = 1ull << 60; QVERIFY(transmit(large));
        QTRY_COMPARE_WITH_TIMEOUT(session.statistics().rxDatagrams, std::uint64_t(3), 3000); QCOMPARE(session.statistics().sequenceMissing, large - 5); QCOMPARE(session.statistics().sequenceReordered, std::uint64_t(0)); session.stop(); QCOMPARE(session.statistics().sequenceMissing, large - 2);
    }
    void sequenceWrapAndSourceEviction() {
        SessionController session; auto config = udpConfig(); config.sequenceAnalysis = true; config.sequenceWindow = 4;
        session.start(config); QTRY_VERIFY_WITH_TIMEOUT(session.connected(), 3000); Peer wrap; wrap.engine.start(udpConfig(session.localEndpoint().port)); QTRY_VERIFY_WITH_TIMEOUT(wrap.ready.load(), 3000);
        auto numbered = [](std::uint64_t n) { QByteArray b; for (int shift = 56; shift >= 0; shift -= 8) b.append(char(n >> shift)); return raw(b); };
        const auto maximum = std::numeric_limits<std::uint64_t>::max();
        // Bound precedes the UDP target resolver; latch the first accepted send
        // so QTRY's final assertion cannot enqueue a duplicate datagram.
        bool wrapAdmitted = false;
        QTRY_VERIFY_WITH_TIMEOUT(wrapAdmitted || (wrapAdmitted = wrap.engine.send(numbered(maximum - 1))), 3000);
        for (const auto n : {maximum, std::uint64_t(0), std::uint64_t(2)}) QVERIFY(wrap.engine.send(numbered(n)));
        QTRY_COMPARE_WITH_TIMEOUT(session.statistics().rxDatagrams, std::uint64_t(4), 3000); session.stop(); QCOMPARE(session.statistics().sequenceMissing, std::uint64_t(1)); QCOMPARE(session.statistics().sequenceReordered, std::uint64_t(0));
        config.sequenceWindow = 1024; session.start(config); QTRY_VERIFY_WITH_TIMEOUT(session.connected(), 3000); session.resetStatistics();
        std::vector<std::unique_ptr<Peer>> peers;
        for (int i = 0; i < 65; ++i) {
            auto peer = std::make_unique<Peer>(); peer->engine.start(udpConfig(session.localEndpoint().port)); QTRY_VERIFY_WITH_TIMEOUT(peer->ready.load(), 3000);
            bool admitted = false;
            QTRY_VERIFY_WITH_TIMEOUT(admitted || (admitted = peer->engine.send(numbered(100))), 3000);
            QVERIFY(peer->engine.send(numbered(102))); peers.push_back(std::move(peer));
            QTRY_COMPARE_WITH_TIMEOUT(session.statistics().rxDatagrams, std::uint64_t((i + 1) * 2), 3000);
        }
        QCOMPARE(session.statistics().sequenceMissing, std::uint64_t(1)); QVERIFY(session.lastError().contains("peer limit")); session.stop(); QCOMPARE(session.statistics().sequenceMissing, std::uint64_t(65));
    }
    void retainedCapacityAdmission() {
        QTemporaryDir temp; detail::Recorder recorder; RecordingOptions options; options.directory = path(temp.path()); options.queueBytes = 4096;
        QString error; QVERIFY(recorder.start(options, &error));
        auto allocated = std::make_shared<Bytes>(65536, 0); allocated->resize(1); auto r = record("x"); r.payload = allocated;
        QCOMPARE(recorder.enqueue(r), detail::Recorder::Admission::Full); QCOMPARE(recorder.snapshot().queued, size_t(0));
        r.payload = raw("x"); QCOMPARE(recorder.enqueue(r), detail::Recorder::Admission::Accepted); QVERIFY(recorder.snapshot().highWater <= options.queueBytes); recorder.stop();
    }
    void udpReceiveOnlyResolutionErrorAndStreamSequenceRejected() {
        SessionController session; QSignalSpy errors(&session, &SessionController::errorOccurred); auto config = udpConfig(); config.remoteAddress = "::1"; // IPv6 literal cannot resolve in this IPv4-only transport.
        session.start(config); QTRY_VERIFY_WITH_TIMEOUT(session.connected(), 3000); QTRY_VERIFY_WITH_TIMEOUT(errors.count() > 0, 3000); QVERIFY(session.connected()); QVERIFY(session.localEndpoint().port != 0); QVERIFY(!session.lastError().isEmpty());
        session.stop(); config.kind = TransportKind::TcpClient; config.sequenceAnalysis = true; session.start(config); QVERIFY(!session.connecting()); QVERIFY(!session.connected()); QVERIFY(session.lastError().contains("UDP"));
    }
    void systemEventsAreReconstructableAndDoNotCountTraffic() {
        QTemporaryDir temp; SessionController session; session.start(udpConfig()); QTRY_VERIFY_WITH_TIMEOUT(session.udpTargetReady(), 3000);
        QVERIFY(session.takeDisplayRecords().empty()); QCOMPARE(session.statistics().rxBytes, std::uint64_t(0)); QCOMPARE(session.statistics().txBytes, std::uint64_t(0));
        RecordingOptions options; options.directory = path(temp.path()); QString error; QVERIFY(session.startRecording(options, &error)); QSignalSpy errors(&session, &SessionController::errorOccurred);
        QVERIFY(!session.send(QByteArray(70000, 'x'))); QTRY_VERIFY_WITH_TIMEOUT(errors.count() >= 2, 3000); QTRY_VERIFY_WITH_TIMEOUT(session.statistics().recordedRecords > 0, 3000);
        session.stop(); QTRY_VERIFY_WITH_TIMEOUT(!session.captures().empty() && session.captures().back().complete, 3000);
        QCOMPARE(session.statistics().rxBytes, std::uint64_t(0)); QCOMPARE(session.statistics().txBytes, std::uint64_t(0)); QCOMPARE(session.statistics().rxDatagrams, std::uint64_t(0));
        const auto output = temp.path() + "/system.json"; QVERIFY(exportCapture(qpath(session.captures().back().path), output, &error)); bool sawError = false, sawStop = false;
        for (const auto& entry : QJsonDocument::fromJson(readFile(output)).object()["records"].toArray()) {
            const auto r = entry.toObject(); QCOMPARE(r["direction"].toString(), QString("SYSTEM")); const auto event = QJsonDocument::fromJson(QByteArray::fromBase64(r["payloadBase64"].toString().toLatin1())).object();
            if (event["event"] == "SendRejected") sawError = true; if (event["event"] == "Disconnected") sawStop = true;
        }
        QVERIFY(sawError); QVERIFY(!sawStop);
        // Stream connection lifecycle still carries useful connection identity.
        auto tcp = udpConfig(); tcp.kind = TransportKind::TcpServer; session.start(tcp); QTRY_VERIFY_WITH_TIMEOUT(session.connected(), 3000); session.stop();
        bool sawListen=false; sawStop=false;
        for(const auto& r:session.takeDisplayRecords())if(r.direction==Direction::System){const auto event=QJsonDocument::fromJson(bytes(r.payload)).object();sawListen|=event["event"]=="Listening";sawStop|=event["event"]=="Disconnected";}
        QVERIFY(sawListen);QVERIFY(sawStop);
    }
    void commandPeriodicCompatibilityAndValidation() {
        QTemporaryDir temp; QString error; const auto target = temp.path() + "/commands.json";
        QJsonObject old{{"name", "legacy"}, {"input", "AA"}, {"hex", true}, {"encoding", "UTF-8"}, {"eol", "none"}};
        auto store = [&](const QJsonObject& c) { return writeFile(target, QJsonDocument(QJsonObject{{"schemaVersion", 1}, {"commands", QJsonArray{c}}}).toJson()); };
        QVector<Command> commands;
        QVERIFY(store(old)); QVERIFY(importCommands(target, &commands, &error));
        QCOMPARE(commands[0].periodic, false); QCOMPARE(commands[0].intervalMs, 1000); QCOMPARE(commands[0].count, 10);
        auto modern = old; modern["periodic"] = true; modern["intervalMs"] = 333; modern["count"] = 0;
        QVERIFY(store(modern)); QVERIFY(importCommands(target, &commands, &error));
        QCOMPARE(commands[0].periodic, true); QCOMPARE(commands[0].intervalMs, 333); QCOMPARE(commands[0].count, 0);
        QVERIFY(exportCommands(target, commands, &error)); const auto prior = readFile(target);
        QVector<Command> restored; QVERIFY(importCommands(target, &restored, &error)); QCOMPARE(restored[0].count, 0);
        for (const auto& pair : QList<QPair<QString, QJsonValue>>{{"periodic", 1}, {"periodic", QJsonValue::Null}, {"intervalMs", 0}, {"intervalMs", 86400001}, {"intervalMs", 1.5}, {"intervalMs", "1000"}, {"count", -1}, {"count", 1000001}, {"count", 0.5}, {"surprise", true}}) {
            auto invalid = modern; invalid[pair.first] = pair.second; QVERIFY(store(invalid));
            QVERIFY(!importCommands(target, &restored, &error)); QCOMPARE(restored[0].intervalMs, 333); QCOMPARE(restored[0].count, 0);
        }
        QVERIFY(writeFile(target, prior)); commands[0].intervalMs = 0; QVERIFY(!exportCommands(target, commands, &error)); QCOMPARE(readFile(target), prior);
        commands[0].intervalMs = 1000; commands[0].count = -1; QVERIFY(!exportCommands(target, commands, &error)); QCOMPARE(readFile(target), prior);
        old["count"] = 17; QVERIFY(store(old)); QVERIFY(importCommands(target, &restored, &error)); QCOMPARE(restored[0].periodic, false); QCOMPARE(restored[0].count, 17);
        QSettings().setValue("storage/directory", temp.path()); QVERIFY(saveCommands(restored, &error)); auto loaded = loadCommands(&error); QCOMPARE(loaded[0].count, 17);
        SessionController session; QVERIFY(!session.connected()); QVERIFY(!session.periodicActive());
    }
    void offlineSelectionIsolatesCallbacksAndWriter() {
        QTemporaryDir temp; SessionController session; auto c = udpConfig(); c.sequenceAnalysis = true; c.sequenceWindow = 1;
        session.start(c); QTRY_VERIFY_WITH_TIMEOUT(session.connected(), 3000);
        const auto token = detailSessionTestAccess::generation(session);
        auto inject = [&](std::uint64_t n) { QByteArray b; for (int shift = 56; shift >= 0; shift -= 8) b.append(char(n >> shift)); auto r = record(b); r.direction = Direction::Receive; QVERIFY(detailSessionTestAccess::data(session, r, token)); };
        inject(100); inject(102); QCOMPARE(session.statistics().sequenceMissing, std::uint64_t(1));
        TransportEvent error; error.kind = EventKind::Error; error.message = "Old UDP error"; detailSessionTestAccess::event(session, error, token);
        QVERIFY(!session.lastError().isEmpty());
        std::uint64_t lastOrdinal = 0; for (const auto& r : session.takeDisplayRecords()) lastOrdinal = std::max(lastOrdinal, r.sequence);
        RecordingOptions options; options.directory = path(temp.path()); options.queueBytes = 16 * 1024 * 1024; QString why;
        QVERIFY(session.startRecording(options, &why));
        for (int i = 0; i < 100; ++i) { auto r = record(QByteArray(65536, 'x')); QVERIFY(detailSessionTestAccess::data(session, r, token)); }
        QVERIFY(session.startPeriodic("x", 1000, 0));
        QSignalSpy errors(&session, &SessionController::errorOccurred), states(&session, &SessionController::stateChanged), stats(&session, &SessionController::statisticsChanged);
        auto selected = udpConfig(); selected.kind = TransportKind::TcpClient; selected.name = "offline TCP";
        session.selectConfiguration(selected);
        QVERIFY(!session.connected()); QVERIFY(!session.connecting()); QVERIFY(!session.periodicActive()); QVERIFY(!session.recording()); QCOMPARE(session.config().name, selected.name);
        detailSessionTestAccess::event(session, error, token);
        TransportEvent connected; connected.kind = EventKind::Connected; detailSessionTestAccess::event(session, connected, token);
        QVERIFY(detailSessionTestAccess::data(session, record("stale"), token));
        QVERIFY(detailSessionTestAccess::data(session, record("offline"), detailSessionTestAccess::generation(session)));
        QTRY_VERIFY_WITH_TIMEOUT(!session.captures().empty() && session.captures().back().complete, 5000);
        QTest::qWait(100); QCOMPARE(errors.count(), 0); QVERIFY(states.count() > 0); QVERIFY(stats.count() > 0);
        QVERIFY(session.lastError().isEmpty()); QVERIFY(session.takeDisplayRecords().empty());
        const auto s = session.statistics(); QCOMPARE(s.rxBytes, std::uint64_t(0)); QCOMPARE(s.txBytes, std::uint64_t(0)); QCOMPARE(s.recordedRecords, std::uint64_t(0));
        QCOMPARE(s.recordingFailures, std::uint64_t(0)); QCOMPARE(s.recordingQueueBytes, size_t(0)); QCOMPARE(s.recordingQueueHighWater, size_t(0)); QCOMPARE(s.sampleQueueBytes, size_t(0));
        QCOMPARE(s.sequenceExpected, std::uint64_t(0)); QCOMPARE(s.sequenceMissing, std::uint64_t(0)); QVERIFY(!s.sequenceEnabled); QCOMPARE(s.actualReceiveBufferBytes, size_t(0)); QCOMPARE(s.pendingSendBytes, size_t(0));
        session.start(c); QTRY_VERIFY_WITH_TIMEOUT(session.connected(), 3000);auto fresh=record("fresh");fresh.direction=Direction::Receive;QVERIFY(detailSessionTestAccess::data(session,fresh,detailSessionTestAccess::generation(session)));const auto rows=session.takeDisplayRecords();QCOMPARE(rows.size(),size_t(1));QVERIFY(rows.front().sequence>lastOrdinal+100);
        auto retained=record("before-stop");retained.direction=Direction::Receive;QVERIFY(detailSessionTestAccess::data(session,retained,detailSessionTestAccess::generation(session)));session.stop();const auto preserved=session.takeDisplayRecords();QCOMPARE(preserved.size(),size_t(1));QCOMPARE(bytes(preserved.front().payload),QByteArray("before-stop")); // Ordinary stop preserves real data without adding lifecycle messages.
    }
    void offlineSelectionFencesLateWriterFailureAcrossNextCapture() {
        QTemporaryDir temp; QString error; SessionController session; session.start(udpConfig()); QTRY_VERIFY_WITH_TIMEOUT(session.connected(), 3000);
        RecordingOptions o; o.directory = path(temp.path() + "/broken"); QVERIFY(session.startRecording(o, &error));
        QString oldCapture;
        QTRY_VERIFY_WITH_TIMEOUT(([&] { const auto cs = session.captures(); if (cs.empty()) return false; oldCapture = qpath(cs.back().path); return cs.back().metadataAvailable; })(), 3000);
        const auto sidecar = oldCapture + ".meta.json"; QVERIFY(QFile::remove(sidecar)); QVERIFY(QDir().mkpath(sidecar));
        session.selectConfiguration(udpConfig());
        QTRY_VERIFY_WITH_TIMEOUT(([&] { for (const auto& c : session.captures()) if (qpath(c.path) == oldCapture) return !c.complete && c.error.find("Cannot save") != std::string::npos; return false; })(), 3000);
        QVERIFY(session.lastError().isEmpty()); QCOMPARE(session.statistics().recordingFailures, std::uint64_t(0));
        session.start(udpConfig()); QTRY_VERIFY_WITH_TIMEOUT(session.connected(), 3000); QTRY_VERIFY_WITH_TIMEOUT(session.statusText().contains("UDP send destination ready"), 3000); o.directory = path(temp.path() + "/next");
        bool started = false; QTRY_VERIFY_WITH_TIMEOUT(started || (started = session.startRecording(o, &error)), 3000);
        auto r = record("new"); QVERIFY(detailSessionTestAccess::data(session, r, detailSessionTestAccess::generation(session))); session.stopRecording();
        QTRY_COMPARE_WITH_TIMEOUT(session.statistics().recordedRecords, std::uint64_t(1), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!session.captures().empty() && session.captures().back().complete, 3000);
        QCOMPARE(session.statistics().recordingFailures, std::uint64_t(0)); QVERIFY(session.lastError().isEmpty());
        QVERIFY(session.statistics().recordingQueueHighWater < size_t(4096));
        QVERIFY(exportCapture(oldCapture, temp.path() + "/truthful-incomplete.json", &error));
        QVERIFY(!QJsonDocument::fromJson(readFile(temp.path() + "/truthful-incomplete.json")).object()["complete"].toBool());
    }
    void catalogRefreshReconcilesAndProtectsActiveCapture() {
        QTemporaryDir temp; const auto first = temp.path() + "/first", second = temp.path() + "/second"; QVERIFY(QDir().mkpath(first)); QVERIFY(QDir().mkpath(second));
        QString error; QString original;
        {
            detail::Recorder writer; RecordingOptions o; o.directory = path(first); QVERIFY(writer.start(o, &error));
            QCOMPARE(writer.enqueue(record("template")), detail::Recorder::Admission::Accepted); writer.stop();
            QTRY_VERIFY_WITH_TIMEOUT(!writer.captures().empty() && writer.captures().back().complete, 3000); original = qpath(writer.captures().back().path);
        }
        const auto added = second + "/added.pbc"; QVERIFY(QFile::copy(original, added)); QVERIFY(QFile::copy(original + ".meta.json", added + ".meta.json"));
        QSettings().setValue("recording/directory", first); SessionController session;
        for (int i = 0; i < 100; ++i) { session.refreshCaptures(first); session.refreshCaptures(second); }
        QTRY_VERIFY_WITH_TIMEOUT(session.captures().size() == 1 && qpath(session.captures()[0].path) == added, 3000);
        const auto another = second + "/another.pbc"; QVERIFY(QFile::copy(original, another)); QVERIFY(QFile::copy(original + ".meta.json", another + ".meta.json"));
        session.refreshCaptures(second); QTRY_COMPARE_WITH_TIMEOUT(session.captures().size(), size_t(2), 3000);
        QVERIFY(QFile::remove(added)); QVERIFY(QFile::remove(added + ".meta.json")); session.refreshCaptures(second);
        QTRY_VERIFY_WITH_TIMEOUT(session.captures().size() == 1 && qpath(session.captures()[0].path) == another, 3000);
        session.start(udpConfig()); QTRY_VERIFY_WITH_TIMEOUT(session.connected(), 3000);
        // Finish startup SYSTEM events before this fixture's exact one-record capture.
        QTRY_VERIFY_WITH_TIMEOUT(session.statusText().contains("UDP send destination ready"), 3000);
        RecordingOptions o; o.directory = path(first); QVERIFY(session.startRecording(o, &error));
        QString active;
        QTRY_VERIFY_WITH_TIMEOUT(([&] { for (const auto& c : session.captures()) if (!c.complete && qpath(c.path).startsWith(first)) { active = qpath(c.path); return true; } return false; })(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(active + ".meta.json"), 3000);
        auto r = record("current", 9); r.direction = Direction::Receive;
        QVERIFY(detailSessionTestAccess::data(session, r, detailSessionTestAccess::generation(session)));
        for (int i = 0; i < 100; ++i) session.refreshCaptures(second);
        QTRY_VERIFY_WITH_TIMEOUT(([&] { const auto cs = session.captures(); return cs.size() == 2 && std::any_of(cs.begin(), cs.end(), [&](const CaptureInfo& c) { return qpath(c.path) == active && !c.complete; }); })(), 3000);
        QVERIFY(session.recording()); session.stopRecording();
        QTRY_VERIFY_WITH_TIMEOUT(([&] { for (const auto& c : session.captures()) if (qpath(c.path) == active) return c.complete && c.records == 1 && c.metadataAvailable && c.transportSummary == "UDP"; return false; })(), 3000);
        session.refreshCaptures(first); QTRY_VERIFY_WITH_TIMEOUT(([&] { const auto cs = session.captures(); return cs.size() == 2 && std::all_of(cs.begin(), cs.end(), [&](const CaptureInfo& c) { return qpath(c.path).startsWith(first); }); })(), 3000);
        QVERIFY(exportCapture(original, temp.path() + "/manual-old.json", &error));
        session.refreshCaptures(QString()); QTRY_VERIFY_WITH_TIMEOUT(session.captures().empty(), 3000);
        QElapsedTimer shutdown; shutdown.start();
        { detail::Recorder scanner; for (int i = 0; i < 1000; ++i) scanner.refresh(i % 2 ? first : second); }
        QVERIFY(shutdown.elapsed() < 3500);
    }
    void captureMetadataCompatibilityAndTransportSummary() {
        QTemporaryDir temp; QString error; QString capture;
        {
            detail::Recorder writer; RecordingOptions o; o.directory = path(temp.path()); QVERIFY(writer.start(o, &error));
            auto a = record("udp"); QCOMPARE(writer.enqueue(a), detail::Recorder::Admission::Accepted);
            a.transport = TransportKind::TcpClient; QCOMPARE(writer.enqueue(a), detail::Recorder::Admission::Accepted); writer.stop();
            QTRY_VERIFY_WITH_TIMEOUT(!writer.captures().empty() && writer.captures().back().complete, 3000);
            const auto c = writer.captures().back(); QCOMPARE(c.transportSummary, std::string("MIXED")); QVERIFY(c.metadataAvailable); capture = qpath(c.path);
        }
        const auto metadata = capture + ".meta.json"; auto object = QJsonDocument::fromJson(readFile(metadata)).object(); QCOMPARE(object["transportSummary"].toString(), QString("MIXED"));
        SessionController session; session.refreshCaptures(temp.path()); QTRY_VERIFY_WITH_TIMEOUT(session.captures().size() == 1 && session.captures()[0].transportSummary == "MIXED", 3000); QVERIFY(session.captures()[0].metadataAvailable);
        object.remove("transportSummary"); QVERIFY(writeFile(metadata, QJsonDocument(object).toJson())); session.refreshCaptures(temp.path());
        QTRY_VERIFY_WITH_TIMEOUT(session.captures().size() == 1 && session.captures()[0].transportSummary.empty(), 3000); QVERIFY(session.captures()[0].metadataAvailable); QVERIFY(session.captures()[0].complete);
        object["transportSummary"] = "made-up"; QVERIFY(writeFile(metadata, QJsonDocument(object).toJson())); session.refreshCaptures(temp.path());
        QTRY_VERIFY_WITH_TIMEOUT(!session.captures()[0].metadataAvailable, 3000); QVERIFY(!session.captures()[0].complete);
        object.remove("transportSummary"); object["durationUs"] = "999000000";
        QVERIFY(writeFile(metadata, QJsonDocument(object).toJson())); session.refreshCaptures(temp.path());
        QTRY_VERIFY_WITH_TIMEOUT(session.captures()[0].metadataAvailable && session.captures()[0].durationUs == 999000000, 3000);
        object["bytes"] = "bad-number"; QVERIFY(writeFile(metadata, QJsonDocument(object).toJson())); session.refreshCaptures(temp.path());
        QTRY_VERIFY_WITH_TIMEOUT(!session.captures()[0].metadataAvailable && session.captures()[0].durationUs == 0, 3000);
        QCOMPARE(session.captures()[0].startedUs, object["startedUs"].toString().toULongLong());
        QCOMPARE(session.captures()[0].records, std::uint64_t(2)); QCOMPARE(session.captures()[0].bytes, std::uint64_t(6));
        QVERIFY(QFile::remove(metadata)); session.refreshCaptures(temp.path()); QTest::qWait(100); QVERIFY(!session.captures()[0].metadataAvailable); QCOMPARE(session.captures()[0].records, std::uint64_t(2));
        QVERIFY(exportCapture(capture, temp.path() + "/old-without-sidecar.json", &error));
        for (int i = 0; i < 4; ++i) {
            detail::Recorder writer; RecordingOptions o; o.directory = path(temp.path() + "/kind-" + QString::number(i)); QVERIFY(writer.start(o, &error));
            auto r = record("kind"); r.transport = TransportKind(i); QCOMPARE(writer.enqueue(r), detail::Recorder::Admission::Accepted); writer.stop();
            QTRY_VERIFY_WITH_TIMEOUT(!writer.captures().empty() && writer.captures().back().complete, 3000);
            QCOMPARE(qpath(writer.captures().back().transportSummary), QStringList({"SERIAL", "TCP CLIENT", "TCP SERVER", "UDP"})[i]); QVERIFY(writer.captures().back().metadataAvailable);
        }
    }
    void reentrantStartupSelectionAndReplacementReleaseOldPort() {
        auto availablePort = [&]() {
            std::atomic<std::uint16_t> port{0};
            NetworkEngine reserve({{}, [&](const TransportEvent& e) { if (e.kind == EventKind::Bound) port = e.local.port; }});
            reserve.start(udpConfig()); QElapsedTimer timer; timer.start();
            while (!port && timer.elapsed() < 3000) QTest::qWait(1);
            return port.load(); // reserve joins/closes before returning to the caller.
        };
        auto old = udpConfig(); old.localPort = availablePort(); QVERIFY(old.localPort != 0); old.name = "outer UDP";
        {
            SessionController session; bool selected = false;
            QObject::connect(&session, &SessionController::stateChanged, &session, [&] {
                if (!selected && session.connecting()) { selected = true; auto c = udpConfig(); c.kind = TransportKind::TcpServer; c.name = "offline TCP"; session.selectConfiguration(c); }
            });
            session.start(old); QVERIFY(selected); QVERIFY(!session.connected()); QVERIFY(!session.connecting());
            QTest::qWait(100); Peer claimant; claimant.engine.start(old); QTRY_VERIFY_WITH_TIMEOUT(claimant.ready.load(), 3000);
            QCOMPARE(session.config().name, std::string("offline TCP")); QVERIFY(session.lastError().isEmpty());
        }
        old.localPort = availablePort(); QVERIFY(old.localPort != 0);
        {
            SessionController session; bool replaced = false; auto inner = udpConfig(); inner.name = "inner UDP";
            do { inner.localPort = availablePort(); } while (inner.localPort == old.localPort);
            QVERIFY(inner.localPort != 0);
            QObject::connect(&session, &SessionController::stateChanged, &session, [&] {
                if (!replaced && session.connecting()) { replaced = true; session.start(inner); }
            });
            session.start(old); QVERIFY(replaced); QTRY_VERIFY_WITH_TIMEOUT(session.connected(), 3000);
            QCOMPARE(session.config().name, inner.name); QCOMPARE(session.localEndpoint().port, inner.localPort);
            Peer claimant; claimant.engine.start(old); QTRY_VERIFY_WITH_TIMEOUT(claimant.ready.load(), 3000);
            QVERIFY(session.connected()); session.stop();
        }
    }
    void finalizedSequenceDenominatorAndSaturation() {
        SessionController session; auto c = udpConfig(); c.sequenceAnalysis = true; c.sequenceWindow = 4; session.start(c); QTRY_VERIFY_WITH_TIMEOUT(session.connected(), 3000);
        auto inject = [&](std::uint64_t n, std::uint16_t peer = 1234) { QByteArray b; for (int shift = 56; shift >= 0; shift -= 8) b.append(char(n >> shift)); auto r = record(b); r.direction = Direction::Receive; r.peer.port = peer; return detailSessionTestAccess::data(session, r, detailSessionTestAccess::generation(session)); };
        QVERIFY(inject(100)); QVERIFY(inject(102)); QVERIFY(inject(102)); QVERIFY(inject(101));
        QCOMPARE(session.statistics().sequenceExpected, std::uint64_t(0)); QCOMPARE(session.statistics().sequenceDuplicates, std::uint64_t(1));
        QVERIFY(inject(106)); QCOMPARE(session.statistics().sequenceExpected, std::uint64_t(3)); QCOMPARE(session.statistics().sequenceMissing, std::uint64_t(0));
        QVERIFY(inject(100)); QCOMPARE(session.statistics().sequenceExpected, std::uint64_t(3)); session.stop();
        QCOMPARE(session.statistics().sequenceExpected, std::uint64_t(7)); QCOMPARE(session.statistics().sequenceMissing, std::uint64_t(3));
        session.selectConfiguration(c); QCOMPARE(session.statistics().sequenceExpected, std::uint64_t(0)); session.start(c); QTRY_VERIFY_WITH_TIMEOUT(session.connected(), 3000);
        const auto max = std::numeric_limits<std::uint64_t>::max();
        for (const auto n : {max - 1, max, std::uint64_t(0), std::uint64_t(2)}) QVERIFY(inject(n));
        QCOMPARE(session.statistics().sequenceExpected, std::uint64_t(1)); session.stop(); QCOMPARE(session.statistics().sequenceExpected, std::uint64_t(5)); QCOMPARE(session.statistics().sequenceMissing, std::uint64_t(1));
        session.selectConfiguration(c); session.start(c); QTRY_VERIFY_WITH_TIMEOUT(session.connected(), 3000);
        QVERIFY(inject(10)); QVERIFY(inject(12)); QVERIFY(inject(12 + (1ull << 63)));
        QCOMPARE(session.statistics().sequenceExpected, std::uint64_t(3)); QCOMPARE(session.statistics().sequenceMissing, std::uint64_t(1));
        session.stop(); QCOMPARE(session.statistics().sequenceExpected, std::uint64_t(4));
        c.sequenceWindow = 1; session.selectConfiguration(c); session.start(c); QTRY_VERIFY_WITH_TIMEOUT(session.connected(), 3000);
        const auto step = (1ull << 63) - 1; QVERIFY(inject(0)); QVERIFY(inject(step)); QVERIFY(inject(step + step)); QVERIFY(inject(step + step + step));
        QCOMPARE(session.statistics().sequenceExpected, max); QCOMPARE(session.statistics().sequenceMissing, max); session.stop(); QCOMPARE(session.statistics().sequenceExpected, max);
        session.resetStatistics(); QCOMPARE(session.statistics().sequenceExpected, std::uint64_t(0)); QCOMPARE(session.statistics().sequenceMissing, std::uint64_t(0));
        c.sequenceWindow = 1024; session.start(c); QTRY_VERIFY_WITH_TIMEOUT(session.connected(), 3000);
        for (int i = 0; i < 65; ++i) { QVERIFY(inject(100, std::uint16_t(1000 + i))); QVERIFY(inject(102, std::uint16_t(1000 + i))); }
        QCOMPARE(session.statistics().sequenceExpected, std::uint64_t(3)); session.stop(); QCOMPARE(session.statistics().sequenceExpected, std::uint64_t(195)); QCOMPARE(session.statistics().sequenceMissing, std::uint64_t(65));
    }
    void truncationIsObservableNonterminalAndScoped() {
        QTemporaryDir temp; SessionController session; session.start(udpConfig()); QTRY_VERIFY_WITH_TIMEOUT(session.connected(), 3000); QTRY_VERIFY_WITH_TIMEOUT(session.statusText().contains("UDP send destination ready"), 3000); session.clearDisplay();
        const auto token = detailSessionTestAccess::generation(session); RecordingOptions o; o.directory = path(temp.path()); QString error; QVERIFY(session.startRecording(o, &error));
        TransportEvent truncated; truncated.kind = EventKind::ReceiveTruncated; truncated.message = "Truncated UDP datagram";
        QSignalSpy errors(&session, &SessionController::errorOccurred); detailSessionTestAccess::event(session, truncated, token);
        QCOMPARE(session.statistics().receiveTruncatedDatagrams, std::uint64_t(1)); QVERIFY(session.connected());
        QCOMPARE(session.statistics().rxBytes, std::uint64_t(0)); QCOMPARE(session.statistics().rxDatagrams, std::uint64_t(0)); QCOMPARE(session.statistics().applicationDroppedRecords, std::uint64_t(0));
        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 3000); const auto rows = session.takeDisplayRecords(); QCOMPARE(rows.size(), size_t(1)); QCOMPARE(rows[0].direction, Direction::System);
        QCOMPARE(QJsonDocument::fromJson(bytes(rows[0].payload)).object()["event"].toString(), QString("ReceiveTruncated"));
        auto r = record("next"); r.direction = Direction::Receive; QVERIFY(detailSessionTestAccess::data(session, r, token)); QCOMPARE(session.statistics().rxBytes, std::uint64_t(4));
        session.stop(); QTRY_VERIFY_WITH_TIMEOUT(!session.captures().empty() && session.captures().back().complete, 3000);
        QVERIFY(exportCapture(qpath(session.captures().back().path), temp.path() + "/truncated.json", &error)); QVERIFY(readFile(temp.path() + "/truncated.json").contains("SYSTEM"));
        auto c = udpConfig(); c.kind = TransportKind::TcpServer; session.selectConfiguration(c); detailSessionTestAccess::event(session, truncated, token);
        QTest::qWait(100); QCOMPARE(session.statistics().receiveTruncatedDatagrams, std::uint64_t(0)); QVERIFY(session.lastError().isEmpty());
    }
    void serialOpenErrorIsObservable() {
        SessionController session; QSignalSpy spy(&session, &SessionController::errorOccurred); ConnectionConfig c; c.kind = TransportKind::Serial; c.serialPort = "PortBridge-Definitely-Missing-Serial"; session.start(c);
        QTRY_VERIFY_WITH_TIMEOUT(!session.connecting(), 3000); QVERIFY(!session.connected()); QTRY_VERIFY_WITH_TIMEOUT(spy.count() > 0, 3000); QVERIFY(!session.lastError().isEmpty());
        QVERIFY(!session.startPeriodic("a", 10)); session.stop();
    }
};
QTEST_GUILESS_MAIN(SessionTest)
#include "test_session.moc"
