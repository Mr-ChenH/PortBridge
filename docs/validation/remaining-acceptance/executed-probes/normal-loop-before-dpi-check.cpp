#include "portbridge/network_engine.hpp"
#include "portbridge/session_controller.hpp"
#include "ui/main_window.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFileInfo>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSaveFile>
#include <QScreen>
#include <QSettings>
#include <QSpinBox>
#include <QLineEdit>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QWindow>
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
using namespace portbridge;

class ProgressWriter {
public:
    explicit ProgressWriter(QString file) : file_(std::move(file)), worker_([this] { run(); }) {}
    ~ProgressWriter() {
        { std::lock_guard<std::mutex> lock(mutex_); done_ = true; }
        ready_.notify_one(); worker_.join();
    }
    void submit(QJsonObject value) {
        { std::lock_guard<std::mutex> lock(mutex_); pending_ = std::move(value); }
        ready_.notify_one();
    }
    qint64 maxWriteMs() const { return maxWriteMs_.load(); }
private:
    void run() {
        for (;;) {
            QJsonObject value;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                ready_.wait(lock, [this] { return done_ || pending_.has_value(); });
                if (!pending_ && done_) return;
                value = std::move(*pending_); pending_.reset();
            }
            QElapsedTimer elapsed; elapsed.start();
            QSaveFile file(file_);
            if (file.open(QIODevice::WriteOnly)) {
                const auto data = QJsonDocument(value).toJson();
                if (file.write(data) == data.size()) file.commit();
            }
            maxWriteMs_ = std::max(maxWriteMs_.load(), elapsed.elapsed());
        }
    }
    QString file_;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::optional<QJsonObject> pending_;
    bool done_ = false;
    std::atomic<qint64> maxWriteMs_{0};
    std::thread worker_;
};

class TimedApplication : public QApplication {
public:
    using QApplication::QApplication;
    QJsonArray slowEvents;
    qint64 maxEventMs = 0;
    bool notify(QObject* receiver, QEvent* event) override {
        if (QThread::currentThread() != thread()) return QApplication::notify(receiver, event);
        QPointer<QObject> guard(receiver);
        const int type = event->type();
        QElapsedTimer timing; timing.start();
        const bool result = QApplication::notify(receiver, event);
        const auto elapsed = timing.elapsed();
        maxEventMs = std::max(maxEventMs, elapsed);
        if (elapsed > 50 && slowEvents.size() < 64)
            slowEvents.append(QJsonObject{{"eventType", type}, {"elapsedMs", elapsed},
                {"receiverClass", guard ? QString::fromLatin1(guard->metaObject()->className()) : QStringLiteral("deleted")},
                {"receiverName", guard ? guard->objectName() : QString()}});
        return result;
    }
};

int main(int argc, char** argv) {
    TimedApplication app(argc, argv);
    if (argc < 4) return 2;
    const QString output = QString::fromLocal8Bit(argv[1]);
    const QString captureDir = QString::fromLocal8Bit(argv[2]);
    const QString protocol = QString::fromLocal8Bit(argv[3]);
    const int durationMs = argc > 4 ? QString::fromLocal8Bit(argv[4]).toInt() : 300000;
    const bool moveScreens = argc > 5 && QString::fromLocal8Bit(argv[5]) == "move-screens";
    const bool injectUiPause = argc > 6 && QString::fromLocal8Bit(argv[6]) == "stall-ui";
    if (durationMs < 1000 || durationMs > 3600000 || (protocol != "udp" && protocol != "tcp")) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, captureDir + "/settings");
    app.setOrganizationName("PortBridgeLocalAcceptance"); app.setApplicationName(protocol);
    app.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));
    QStandardPaths::setTestModeEnabled(true);
    QSettings().setValue("storage/directory", captureDir + "/storage");
    ProgressWriter progress(output);
    auto wait = [&](auto predicate, int timeout) {
        if (predicate()) return true;
        QEventLoop loop;
        QTimer check, deadline;
        check.setInterval(5); deadline.setSingleShot(true);
        QObject::connect(&check, &QTimer::timeout, &loop, [&] { if (predicate()) loop.quit(); });
        QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
        check.start(); deadline.start(timeout); loop.exec();
        return predicate();
    };
    ConnectionConfig cfg;
    cfg.name = protocol.toStdString() + " local acceptance";
    cfg.kind = protocol == "tcp" ? TransportKind::TcpServer : TransportKind::Udp;
    cfg.localAddress = "127.0.0.1"; cfg.localPort = 0; cfg.remotePort = 9;
    cfg.sequenceAnalysis = protocol == "udp";
    cfg.receiveBufferBytes = 16 * 1024 * 1024; cfg.sendBufferBytes = 4 * 1024 * 1024;
    QString error;
    if (!saveProfiles(QVector<ConnectionConfig>{cfg}, &error)) return 3;
    SessionController session;
    MainWindow window(&session);
    window.resize(1100, 760);
    window.findChild<QLineEdit*>("recordDirectory")->setText(captureDir);
    window.findChild<QSpinBox*>("recordRotationMiB")->setValue(32);
    window.findChild<QSpinBox*>("recordQueueMiB")->setValue(16);
    window.show(); session.setHighSpeed(true); session.start(cfg);
    if (!wait([&] { return session.connected(); }, 5000)) {
        progress.submit({{"state", "failed"}, {"error", session.lastError()}}); return 4;
    }
    std::atomic<bool> ready{false};
    std::atomic<quint64> completedRecords{0}, completedBytes{0}, admitted{0}, rejected{0};
    NetworkEngine peer({[&](const DataRecord& record) {
        if (record.direction == Direction::Transmit) { ++completedRecords; completedBytes += record.payload->size(); }
        return true;
    }, [&](const TransportEvent& event) {
        if (event.kind == EventKind::Connected || event.kind == EventKind::UdpTargetReady) ready = true;
    }});
    auto peerCfg = cfg; peerCfg.sequenceAnalysis = false;
    peerCfg.kind = protocol == "tcp" ? TransportKind::TcpClient : TransportKind::Udp;
    peerCfg.remotePort = session.localEndpoint().port; peer.start(peerCfg);
    if (!wait([&] { return ready.load(); }, 5000)) return 5;
    RecordingOptions options;
    options.directory = captureDir.toStdString(); options.rotateBytes = 32 * 1024 * 1024;
    options.queueBytes = 16 * 1024 * 1024;
    if (!session.startRecording(options, &error)) {
        progress.submit({{"state", "failed"}, {"error", error}}); return 6;
    }
    constexpr int rate = 1000;
    const int bytesPerFrame = protocol == "tcp" ? 4096 : 1472;
    std::atomic<bool> producerDone{false}, stopProducer{false};
    QElapsedTimer wall, tick, finalization; wall.start(); tick.start();
    std::thread producer([&] {
        using Clock = std::chrono::steady_clock;
        const auto start = Clock::now();
        while (!stopProducer && Clock::now() - start < std::chrono::milliseconds(durationMs)) {
            const auto number = admitted.load();
            auto data = std::make_shared<Bytes>(bytesPerFrame);
            for (int p = 0; p < bytesPerFrame; ++p) (*data)[p] = std::uint8_t((number * 31) ^ (p * 17));
            for (int p = 0; p < 8; ++p) (*data)[p] = std::uint8_t(number >> (56 - 8 * p));
            if (peer.send(data)) {
                ++admitted;
                std::this_thread::sleep_until(start + std::chrono::microseconds(admitted.load() * 1000000 / rate));
            } else { ++rejected; std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        }
        producerDone = true;
    });
    qint64 maxTick = 0, maxAction = 0;
    size_t queuePeak = 0, displayPeak = 0;
    SIZE_T rssPeak = 0;
    QJsonArray samples, screens;
    QJsonObject uiPause;
    QTimer heartbeat, sample, move, interaction, monitor;
    heartbeat.setInterval(10);
    QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] { maxTick = std::max(maxTick, tick.restart()); });
    auto recordScreen = [&] {
        auto* screen = window.windowHandle()->screen();
        if (screens.isEmpty() || screens.last().toObject().value("screen") != screen->name())
            screens.append(QJsonObject{{"screen", screen->name()}, {"devicePixelRatio", screen->devicePixelRatio()},
                {"windowDevicePixelRatio", window.devicePixelRatioF()}, {"logicalDpi", screen->logicalDotsPerInch()},
                {"windowWidth", window.width()}, {"windowHeight", window.height()}, {"visible", window.isVisible()},
                {"exposed", window.windowHandle()->isExposed()}});
    };
    sample.setInterval(1000);
    QObject::connect(&sample, &QTimer::timeout, &app, [&] {
        const auto s = session.statistics();
        PROCESS_MEMORY_COUNTERS pm{};
        GetProcessMemoryInfo(GetCurrentProcess(), &pm, sizeof(pm));
        rssPeak = std::max(rssPeak, pm.WorkingSetSize); recordScreen();
        samples.append(QJsonObject{{"elapsedMs", wall.elapsed()}, {"workingSetBytes", double(pm.WorkingSetSize)},
            {"rxBytes", double(s.rxBytes)}, {"recordedBytes", double(s.recordedBytes)},
            {"recordingQueueBytes", double(s.recordingQueueBytes)}, {"displayQueueBytes", double(s.sampleQueueBytes)},
            {"heartbeatMaxMs", maxTick}});
        progress.submit({{"state", "running"}, {"protocol", protocol}, {"elapsedMs", wall.elapsed()},
            {"admittedFrames", double(admitted.load())}, {"rxBytes", double(s.rxBytes)}, {"recordedBytes", double(s.recordedBytes)},
            {"heartbeatMaxMs", maxTick}, {"longestGuiEventMs", app.maxEventMs}, {"slowGuiEvents", app.slowEvents}, {"samples", samples}});
    });
    int screenIndex = 0;
    move.setInterval(10000);
    QObject::connect(&move, &QTimer::timeout, &window, [&] {
        screenIndex = (screenIndex + 1) % app.screens().size();
        window.move(app.screens()[screenIndex]->availableGeometry().topLeft() + QPoint(20, 20));
    });
    interaction.setInterval(7000);
    auto* pause = window.findChild<QCheckBox*>("pauseDisplay");
    QObject::connect(&interaction, &QTimer::timeout, &window, [&] {
        QElapsedTimer action; action.start(); pause->click(); maxAction = std::max(maxAction, action.elapsed());
        QTimer::singleShot(50, &window, [&, pause] {
            QElapsedTimer action; action.start(); if (pause->isChecked()) pause->click();
            maxAction = std::max(maxAction, action.elapsed());
        });
    });
    if (injectUiPause) QTimer::singleShot(durationMs / 2, &window, [&] {
        const auto before = session.statistics();
        QElapsedTimer pauseWall; pauseWall.start(); QThread::msleep(500);
        const auto after = session.statistics();
        uiPause = {{"pauseMs", pauseWall.elapsed()}, {"rxBytesDuringPause", double(after.rxBytes - before.rxBytes)},
            {"recordedBytesDuringPause", double(after.recordedBytes - before.recordedBytes)},
            {"rawAndDiskAdvancedWhileGuiWasBlocked", after.rxBytes > before.rxBytes && after.recordedBytes > before.recordedBytes}};
        tick.restart(); // The intentional fault is reported separately from normal responsiveness.
    });
    int phase = 0;
    QElapsedTimer drain;
    qint64 stopCallMs = -1;
    bool drained = false, finalized = false;
    int outcome = 1;
    monitor.setInterval(20);
    QObject::connect(&monitor, &QTimer::timeout, &app, [&] {
        const auto s = session.statistics();
        queuePeak = std::max(queuePeak, s.recordingQueueBytes); displayPeak = std::max(displayPeak, s.sampleQueueBytes);
        if (!producerDone) return;
        if (phase == 0) { producer.join(); drain.start(); phase = 1; }
        if (phase == 1) {
            drained = s.rxBytes == admitted * bytesPerFrame && s.recordedBytes == s.rxBytes && completedBytes == s.rxBytes;
            if (!drained && drain.elapsed() < 15000) return;
            finalization.start(); session.stopRecording(); stopCallMs = finalization.elapsed(); phase = 2;
        }
        const auto captures = session.captures();
        finalized = !captures.empty() && std::all_of(captures.begin(), captures.end(), [](const CaptureInfo& c) { return c.complete && c.error.empty(); });
        if (!finalized && finalization.elapsed() < 15000) return;
        heartbeat.stop(); sample.stop(); move.stop(); interaction.stop(); monitor.stop();
        QJsonArray saved; quint64 payloadBytes = 0;
        for (const auto& c : captures) {
            saved.append(QJsonObject{{"file", QFileInfo(QString::fromStdString(c.path)).fileName()},
                {"bytes", double(c.bytes)}, {"records", double(c.records)}, {"complete", c.complete}});
            payloadBytes += c.bytes;
        }
        const bool pausePassed = !injectUiPause || uiPause.value("rawAndDiskAdvancedWhileGuiWasBlocked").toBool();
        const bool passed = drained && finalized && s.applicationDroppedRecords == 0 && s.recordingFailures == 0 &&
            s.sequenceMissing == 0 && s.sequenceDuplicates == 0 && payloadBytes == admitted * bytesPerFrame &&
            s.recordingQueueHighWater <= options.queueBytes && displayPeak <= 2 * 1024 * 1024 &&
            maxTick < 200 && maxAction < 200 && stopCallMs < 200 && pausePassed;
        recordScreen();
        progress.submit({{"state", passed ? "passed" : "failed"}, {"scope", "local loopback; real QApplication event loop, production MainWindow, independent sender and real disk recording"},
            {"protocol", protocol}, {"durationMs", durationMs}, {"observedFinishMs", wall.elapsed()},
            {"rateFramesPerSecond", rate}, {"payloadBytes", bytesPerFrame}, {"admittedFrames", double(admitted.load())},
            {"rejectedAdmissions", double(rejected.load())}, {"transmitCompletionRecords", double(completedRecords.load())},
            {"transmitCompletedBytes", double(completedBytes.load())}, {"rxBytes", double(s.rxBytes)}, {"recordedBytes", double(s.recordedBytes)},
            {"capturePayloadBytes", double(payloadBytes)}, {"droppedRecords", double(s.applicationDroppedRecords)},
            {"recordingFailures", double(s.recordingFailures)}, {"sequenceMissing", double(s.sequenceMissing)},
            {"sequenceDuplicates", double(s.sequenceDuplicates)}, {"displayOmitted", double(s.displayOmitted)},
            {"recordingQueueHighWaterBytes", double(s.recordingQueueHighWater)}, {"recordingQueuePeakBytes", double(queuePeak)},
            {"displayQueuePeakBytes", double(displayPeak)}, {"rssPeakBytes", double(rssPeak)}, {"heartbeatMaxMs", maxTick},
            {"pauseResumeActionMaxMs", maxAction}, {"stopRecordingCallMs", stopCallMs}, {"finalizationMs", finalization.elapsed()},
            {"progressWrittenByWorker", true}, {"senderIndependentOfGui", true}, {"realApplicationEventLoop", true},
            {"progressWriterMaxMs", progress.maxWriteMs()}, {"longestGuiEventMs", app.maxEventMs}, {"slowGuiEvents", app.slowEvents},
            {"moveScreens", moveScreens}, {"uiPauseInjection", uiPause}, {"captures", saved}, {"physicalScreens", screens}, {"samples", samples}});
        session.stop(); peer.stop(); window.close(); outcome = passed ? 0 : 1; app.quit();
    });
    heartbeat.start(); sample.start(); interaction.start(); monitor.start(); if (moveScreens) move.start();
    app.exec();
    stopProducer = true;
    if (producer.joinable()) producer.join();
    return outcome;
}
