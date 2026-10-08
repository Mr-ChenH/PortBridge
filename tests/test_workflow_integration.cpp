#include "portbridge/workflow.hpp"
#include <QtTest>
#include <QUdpSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#ifndef WORKFLOW_CORE_ONLY
#include "ui/main_window.hpp"
#include "ui/workflow_page.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTableView>
#include <QTemporaryDir>
#include <memory>
#endif
using namespace portbridge;
namespace {
WorkflowNode n(QString id, QString type, QJsonObject params = {}) { return {id, type, type, {}, params}; }
WorkflowDocument flow(QVector<WorkflowNode> nodes) {
    WorkflowDocument d; d.id = "integration"; d.name = "integration"; d.nodes = nodes;
    for (int i = 1; i < nodes.size(); ++i) d.edges.append({QString::number(i), nodes[i - 1].id, nodes[i].id, "success"});
    return d;
}
ConnectionConfig udp(quint16 remote, std::string name = "integration") { ConnectionConfig c; c.name = name; c.localAddress = "127.0.0.1"; c.localPort = 0; c.remotePort = remote; return c; }
#ifndef WORKFLOW_CORE_ONLY
WorkflowNode owned(QString id, const ConnectionConfig& c) { return n(id, "raw", {{"ownership", "owned"}, {"config", workflowConnectionConfigToJson(c)}}); }
#endif
WorkflowNode http(QString id, quint16 port) { return n(id, "http", {{"method", "GET"}, {"url", QString("http://127.0.0.1:%1/test").arg(port)}, {"body", ""}, {"headers", ""}, {"expectedStatus", "200"}, {"connectTimeout", "500"}, {"timeout", "1000"}}); }
}
class WorkflowIntegrationTest : public QObject {
    Q_OBJECT
#ifndef WORKFLOW_CORE_ONLY
    std::unique_ptr<QTemporaryDir> storage;
    template<class T> T* get(MainWindow& w, const char* id) { auto* object = w.findChild<T*>(id); if (!object) qFatal("Missing main integration control %s", id); return object; }
    WorkflowPage* page(MainWindow& w) { auto* p = w.findChild<WorkflowPage*>(); if (!p) qFatal("Missing WorkflowPage"); return p; }
    void connectUdp(MainWindow& w, SessionController& c, quint16 port) {
        get<QListWidget>(w, "profileList")->setCurrentRow(3);
        get<QComboBox>(w, "localAddress")->setCurrentText("127.0.0.1");
        get<QSpinBox>(w, "localPort")->setValue(0); get<QLineEdit>(w, "remoteAddress")->setText("127.0.0.1"); get<QSpinBox>(w, "remotePort")->setValue(port);
        get<QPushButton>(w, "connectButton")->click(); QTRY_VERIFY_WITH_TIMEOUT(c.connected(), 3000);
    }
    void runWithConfirmation(MainWindow& w, const QString& button, bool* handled) {
        *handled = false;
        QTimer::singleShot(0, &w, [&, button] {
            auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != "workflowResourceConfirmation") qFatal("Expected workflow resource confirmation");
            for (auto* choice : dialog->buttons()) if (choice->text() == button) { *handled = true; choice->click(); return; }
            qFatal("Missing expected resource confirmation choice");
        });
        page(w)->startRun();
    }
#endif
private slots:
#ifndef WORKFLOW_CORE_ONLY
    void initTestCase() {
        QCoreApplication::setOrganizationName("PortBridgeWorkflowIntegration"); QCoreApplication::setApplicationName("PortBridgeWorkflowIntegration");
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QSettings::setDefaultFormat(QSettings::IniFormat); QStandardPaths::setTestModeEnabled(true);
        storage = std::make_unique<QTemporaryDir>(); QVERIFY(storage->isValid()); QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, storage->path());
    }
    void init() { QSettings settings; settings.clear(); const auto path = storage->path() + "/case"; QDir dir(path); if (dir.exists()) QVERIFY(dir.removeRecursively()); settings.setValue("storage/directory", path); }
#endif
    void protocolContractValidation() {
        auto d = flow({n("s", "start"), http("h", 9), n("e", "end")}); QVERIFY(d.validate().isEmpty());
        d.nodes[1].parameters["timeout"] = "100"; QVERIFY(!d.validate().isEmpty());
        d.nodes[1].parameters["connectTimeout"] = "50"; QVERIFY(d.validate().isEmpty());
        d.nodes[1].parameters["timeout"] = "60001"; QVERIFY(!d.validate().isEmpty());
        d.nodes[1].parameters["timeout"] = "1000"; d.nodes[1].parameters["limit"] = "9"; QVERIFY(!d.validate().isEmpty());
        d.nodes[1].parameters["limit"] = "8"; d.nodes[1].id = QString(128, 'h'); d.edges[0].to = d.nodes[1].id; d.edges[1].from = d.nodes[1].id; QVERIFY(d.validate().isEmpty());
        d.nodes[1] = n(d.nodes[1].id, "ws", {{"url", "ws://127.0.0.1:9"}, {"headers", ""}, {"timeout", "1000"}}); QVERIFY(d.validate().isEmpty());
    }
    void runnerRejectsRawHttpWsOverlap() {
        QUdpSocket peer; QVERIFY(peer.bind(QHostAddress::LocalHost, 0)); SessionController session; session.start(udp(peer.localPort())); QTRY_VERIFY(session.connected());
        WorkflowRunner runner; runner.setSessionProvider([&] { return &session; }); QString error;
        for (const auto& protocol : {QString("http"), QString("ws")}) {
            auto request = protocol == "http" ? http("p", 9) : n("p", "ws", {{"url", "ws://127.0.0.1:9"}, {"headers", ""}});
            for (const auto& mode : {QString("direct"), QString("borrow"), QString("releasedBorrow")}) {
                QVector<WorkflowNode> nodes{n("s", "start")};
                if (mode != "direct") nodes.append(n("r", "raw"));
                if (mode == "releasedBorrow") nodes.append(n("c", "close"));
                nodes.append(request); nodes.append(n("e", "end"));
                auto d = flow(nodes); QVERIFY2(runner.start(d, &error), qPrintable(error));
                QTRY_COMPARE_WITH_TIMEOUT(runner.state(), WorkflowRunState::Failed, 1000);
                QVERIFY2(runner.result("p").detail.contains("请先"), qPrintable(runner.result("p").detail)); QVERIFY(session.connected()); QVERIFY(!peer.hasPendingDatagrams());
            }
        }
    }
#ifndef WORKFLOW_CORE_ONLY
    void navigationShellThemeAndIdleShortcutDoNotSend() {
        QUdpSocket peer; QVERIFY(peer.bind(QHostAddress::LocalHost, 0)); SessionController session;
        QByteArray savedShell;
        { MainWindow w(&session); w.show(); connectUdp(w, session, peer.localPort());
          auto* split = get<QSplitter>(w, "mainSplitter"); split->setSizes({280, 1080}); QTest::qWait(50); const int oldSide = split->sizes()[0];
          get<QComboBox>(w, "sendFormat")->setCurrentIndex(1); get<QPlainTextEdit>(w, "sendInput")->setPlainText("must-not-send");
          get<QPushButton>(w, "workflowNavigation")->click(); QCOMPARE(get<QStackedWidget>(w, "mainPages")->currentIndex(), 3); QVERIFY(!get<QWidget>(w, "connectionSidebar")->isVisible()); QVERIFY(get<QPushButton>(w, "railNavigation3")->isChecked());
          QVERIFY(page(w)->setDocument(WorkflowDocument::templateDocument("blank"))); QVERIFY(!page(w)->runner()->active());
          QTest::keyClick(&w, Qt::Key_Return, Qt::ControlModifier); QTest::qWait(100); QVERIFY(!peer.hasPendingDatagrams()); QCOMPARE(session.statistics().txBytes, std::uint64_t(0));
          get<QPushButton>(w, "themeButton")->click(); QCOMPARE(w.property("darkTheme").toBool(), false); QVERIFY(session.connected());
          get<QPushButton>(w, "workspaceNavigation")->click(); QTest::qWait(50); QVERIFY(get<QWidget>(w, "connectionSidebar")->isVisible()); QVERIFY(std::abs(split->sizes()[0] - oldSide) <= 2); savedShell = split->saveState();
          get<QPushButton>(w, "workflowNavigation")->click(); QVERIFY(session.connected());
        }
        QVERIFY(!session.connected()); QCOMPARE(QSettings().value("ui/shell").toByteArray(), savedShell);
        SessionController restored; MainWindow w(&restored); w.show(); QTest::qWait(50); QVERIFY(!restored.connected()); QVERIFY(!restored.connecting()); QVERIFY(!restored.periodicActive()); QVERIFY(!page(w)->runner()->active());
    }
    void borrowConfirmationPreservesRecordingAndBlocksManualCommandSend() {
        QUdpSocket peer; QVERIFY(peer.bind(QHostAddress::LocalHost, 0)); QString error;
        QVERIFY(saveCommands({Command{"command", "command-data", false, "UTF-8", "none"}}, &error)); SessionController session; MainWindow w(&session); w.show(); connectUdp(w, session, peer.localPort());
        RecordingOptions recording; recording.directory = (storage->path() + "/borrow-recording").toStdString(); QVERIFY(session.startRecording(recording, &error));
        QVERIFY(session.startPeriodic("cycle", 1000, 0)); const auto epoch = session.sessionEpoch();
        auto d = flow({n("s", "start"), n("r", "raw"), n("d", "delay", {{"duration", "5000"}}), n("e", "end")}); QVERIFY(page(w)->setDocument(d));
        bool handled; runWithConfirmation(w, QStringLiteral("保留当前任务"), &handled); QVERIFY(handled); QVERIFY(!page(w)->runner()->active()); QVERIFY(session.periodicActive()); QVERIFY(session.recording()); QCOMPARE(session.sessionEpoch(), epoch);
        runWithConfirmation(w, QStringLiteral("停止周期并运行"), &handled); QVERIFY(handled); QTRY_COMPARE(page(w)->runner()->activeNodeId(), QString("d")); QVERIFY(session.connected()); QVERIFY(session.recording()); QVERIFY(!session.periodicActive()); QCOMPARE(session.sessionEpoch(), epoch);
        page(w)->runner()->pause(); get<QPushButton>(w, "commandsNavigation")->click(); get<QPushButton>(w, "commandRowLoad0")->click(); QCOMPARE(get<QPlainTextEdit>(w, "sendInput")->toPlainText(), QString("command-data"));
        QVERIFY(!get<QPushButton>(w, "sendButton")->isEnabled()); QTest::keyClick(&w, Qt::Key_Return, Qt::ControlModifier); QTest::qWait(100); QVERIFY(!peer.hasPendingDatagrams());
        get<QListWidget>(w, "profileList")->setCurrentRow(1); QVERIFY(session.connected()); QCOMPARE(session.sessionEpoch(), epoch); QVERIFY(session.recording()); QVERIFY(page(w)->runner()->usesSession(&session));
        QTimer::singleShot(0, &w, [&] { auto* dialog = w.findChild<QDialog*>("profileDialog"); if (!dialog) qFatal("Expected inactive edit dialog"); dialog->findChild<QLineEdit*>("profileName")->setText("edited-inactive"); dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click(); });
        get<QPushButton>(w, "editProfile")->click(); QCOMPARE(session.sessionEpoch(), epoch); QVERIFY(session.connected()); QVERIFY(session.recording()); QCOMPARE(page(w)->runner()->state(), WorkflowRunState::Paused); QVERIFY(get<QListWidget>(w, "profileList")->currentItem()->text().contains("edited-inactive"));
        const auto profilesBeforeDelete = get<QListWidget>(w, "profileList")->count(); get<QPushButton>(w, "removeProfile")->click(); QCOMPARE(get<QListWidget>(w, "profileList")->count(), profilesBeforeDelete - 1); QCOMPARE(session.sessionEpoch(), epoch); QVERIFY(session.recording());
        QTimer::singleShot(0, &w, [&] { auto* dialog = w.findChild<QDialog*>("profileDialog"); if (!dialog) qFatal("Expected inactive creation dialog"); dialog->findChild<QLineEdit*>("profileName")->setText("new-inactive"); dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click(); });
        get<QPushButton>(w, "addProfile")->click(); QCOMPARE(get<QListWidget>(w, "profileList")->count(), profilesBeforeDelete); QCOMPARE(session.sessionEpoch(), epoch); QVERIFY(session.connected()); QVERIFY(session.recording()); QCOMPARE(page(w)->runner()->state(), WorkflowRunState::Paused); QVERIFY(!peer.hasPendingDatagrams());
        QVERIFY(peer.writeDatagram("background", QHostAddress::LocalHost, session.localEndpoint().port) > 0); QTRY_COMPARE(session.statistics().rxBytes, std::uint64_t(10));
        get<QPushButton>(w, "returnActiveProfile")->click(); QTRY_VERIFY(get<QTableView>(w, "recordTable")->model()->rowCount() > 0); QCOMPARE(session.sessionEpoch(), epoch);
        page(w)->runner()->stop(); QVERIFY(session.connected()); QVERIFY(session.recording());
    }
    void httpConfirmationRetainsOrStopsRawAndRecords() {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost)); int requests = 0;
        connect(&server, &QTcpServer::newConnection, this, [&] { auto* socket = server.nextPendingConnection(); connect(socket, &QTcpSocket::readyRead, socket, [&, socket] { const auto data = socket->readAll(); if (!data.contains("\r\n\r\n")) return; ++requests; socket->write("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK"); socket->disconnectFromHost(); }); });
        QUdpSocket peer; QVERIFY(peer.bind(QHostAddress::LocalHost, 0)); SessionController session; MainWindow w(&session); w.show(); connectUdp(w, session, peer.localPort());
        RecordingOptions recording; recording.directory = (storage->path() + "/replacement-recording").toStdString(); QString error; QVERIFY(session.startRecording(recording, &error)); QVERIFY(session.startPeriodic("cycle", 1000)); const auto epoch = session.sessionEpoch();
        auto d = flow({n("s", "start"), http(QString(128, 'h'), server.serverPort()), n("e", "end")}); QVERIFY(page(w)->setDocument(d));
        bool handled; runWithConfirmation(w, QStringLiteral("保留当前任务"), &handled); QVERIFY(handled); QVERIFY(session.connected()); QVERIFY(session.periodicActive()); QVERIFY(session.recording()); QCOMPARE(session.sessionEpoch(), epoch); QCOMPARE(requests, 0);
        runWithConfirmation(w, QStringLiteral("停止旧任务并运行"), &handled); QVERIFY(handled); QTRY_COMPARE_WITH_TIMEOUT(page(w)->runner()->state(), WorkflowRunState::Completed, 3000); QCOMPARE(requests, 1); QVERIFY(!session.connected()); QVERIFY(!session.recording()); QVERIFY(!session.periodicActive());
    }
    void mainCloseCancelPreservesFlowAndAcceptStopsActivity() {
        QUdpSocket peer; QVERIFY(peer.bind(QHostAddress::LocalHost, 0)); SessionController session; MainWindow w(&session); w.show(); connectUdp(w, session, peer.localPort());
        auto d = flow({n("s", "start"), n("r", "raw"), n("d", "delay", {{"duration", "5000"}}), n("e", "end")}); QVERIFY(page(w)->setDocument(d)); QVERIFY(page(w)->isDirty()); page(w)->startRun();
        QTRY_COMPARE(page(w)->runner()->activeNodeId(), QString("d")); page(w)->runner()->pause(); const auto runId = page(w)->runner()->runId(); const auto epoch = session.sessionEpoch();
        QTimer::singleShot(0, &w, [&] { auto* confirm = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); if (!confirm || !confirm->button(QMessageBox::Cancel)) qFatal("Expected draft Cancel confirmation"); confirm->button(QMessageBox::Cancel)->click(); });
        QVERIFY(!w.close()); QVERIFY(w.isVisible()); QVERIFY(session.connected()); QCOMPARE(session.sessionEpoch(), epoch); QCOMPARE(page(w)->runner()->state(), WorkflowRunState::Paused); QCOMPARE(page(w)->runner()->runId(), runId); QVERIFY(page(w)->isDirty());
        QTimer::singleShot(0, &w, [&] { auto* confirm = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); if (!confirm || !confirm->button(QMessageBox::Discard)) qFatal("Expected draft Discard confirmation"); confirm->button(QMessageBox::Discard)->click(); });
        QVERIFY(w.close()); QVERIFY(!w.isVisible()); QCOMPARE(page(w)->runner()->state(), WorkflowRunState::Stopped); QVERIFY(!session.connected()); QVERIFY(!session.recording()); QVERIFY(!session.periodicActive());
    }
    void mainCloseSaveDestinationCancelPreservesAndSaveStops() {
        QUdpSocket peer; QVERIFY(peer.bind(QHostAddress::LocalHost, 0)); SessionController session; MainWindow w(&session); w.show(); connectUdp(w, session, peer.localPort());
        auto d = flow({n("s", "start"), n("r", "raw"), n("d", "delay", {{"duration", "5000"}}), n("e", "end")}); QVERIFY(page(w)->setDocument(d)); page(w)->startRun(); QTRY_COMPARE(page(w)->runner()->activeNodeId(), QString("d")); page(w)->runner()->pause(); const auto epoch = session.sessionEpoch();
        auto chooseSave = [&](bool acceptDestination) {
            QTimer::singleShot(0, &w, [&, acceptDestination] {
                auto* confirm = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); if (!confirm || !confirm->button(QMessageBox::Save)) qFatal("Expected Stop and Save confirmation");
                QTimer::singleShot(0, &w, [&, acceptDestination] {
                    auto* file = qobject_cast<QFileDialog*>(QApplication::activeModalWidget()); if (!file) qFatal("Expected save destination dialog");
                    if (!acceptDestination) { file->reject(); return; }
                    file->selectFile(storage->path() + "/exit-save.pbflow.json"); static_cast<QDialog*>(file)->accept();
                });
                confirm->button(QMessageBox::Save)->click();
            });
        };
        chooseSave(false); QVERIFY(!w.close()); QVERIFY(w.isVisible()); QCOMPARE(page(w)->runner()->state(), WorkflowRunState::Paused); QCOMPARE(session.sessionEpoch(), epoch); QVERIFY(session.connected()); QVERIFY(page(w)->isDirty());
        chooseSave(true); QVERIFY(w.close()); QCOMPARE(page(w)->runner()->state(), WorkflowRunState::Stopped); QVERIFY(!session.connected()); QVERIFY(!page(w)->isDirty());
        QFile file(storage->path() + "/exit-save.pbflow.json"); QVERIFY(file.open(QIODevice::ReadOnly)); WorkflowDocument saved; QString error; QVERIFY2(WorkflowDocument::fromJson(QJsonDocument::fromJson(file.readAll()).object(), &saved, &error), qPrintable(error)); QCOMPARE(saved.nodes.size(), 4);
    }
    void multipleOwnedResourcesKeepBackgroundIdentityAndCleanup() {
        QUdpSocket peer; QVERIFY(peer.bind(QHostAddress::LocalHost, 0)); int received = 0;
        connect(&peer, &QUdpSocket::readyRead, this, [&] { while (peer.hasPendingDatagrams()) { QByteArray bytes(int(peer.pendingDatagramSize()), '\0'); QHostAddress from; quint16 port; peer.readDatagram(bytes.data(), bytes.size(), &from, &port); ++received; peer.writeDatagram(bytes, from, port); } });
        SessionController session; MainWindow w(&session); w.show();
        const auto first = udp(peer.localPort(), "owned-first"), second = udp(peer.localPort(), "owned-second");
        auto d = flow({n("s", "start"), owned("r1", first), n("send1", "send", {{"format", "text"}, {"payload", "first"}}), n("d1", "delay", {{"duration", "250"}}), n("c", "close"), owned("r2", second), n("send2", "send", {{"format", "text"}, {"payload", "second"}}), n("d2", "delay", {{"duration", "5000"}}), n("e", "end")});
        QVERIFY(page(w)->setDocument(d)); page(w)->startRun(); QTRY_COMPARE(page(w)->runner()->activeNodeId(), QString("d1")); QCOMPARE(session.config().name, std::string("owned-first"));
        get<QListWidget>(w, "profileList")->setCurrentRow(1); QVERIFY(page(w)->runner()->usesSession(&session));
        QTRY_COMPARE_WITH_TIMEOUT(page(w)->runner()->activeNodeId(), QString("d2"), 3000); QTRY_COMPARE(received, 2); QCOMPARE(session.config().name, std::string("owned-second"));
        QTRY_VERIFY2(get<QPushButton>(w, "returnActiveProfile")->toolTip().contains("owned-second"), qPrintable(get<QPushButton>(w, "returnActiveProfile")->toolTip())); get<QPushButton>(w, "returnActiveProfile")->click(); QCOMPARE(get<QLabel>(w, "sessionTitle")->text(), QString("owned-second"));
        auto* list = get<QListWidget>(w, "profileList"); QVERIFY(list->currentItem()->text().contains("owned-second")); QTest::qWait(100);
        auto* model = get<QTableView>(w, "recordTable")->model(); for (int row = 0; row < model->rowCount(); ++row) QVERIFY(model->index(row, 4).data().toInt() != 5);
        page(w)->runner()->stop(); QVERIFY(!session.connected()); QCOMPARE(session.config().name, std::string("owned-second"));
    }
#endif
};
#ifdef WORKFLOW_CORE_ONLY
QTEST_GUILESS_MAIN(WorkflowIntegrationTest)
#else
QTEST_MAIN(WorkflowIntegrationTest)
#endif
#include "test_workflow_integration.moc"
