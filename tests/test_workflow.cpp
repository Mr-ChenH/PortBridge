#include "portbridge/workflow.hpp"
#include "../src/session/session_private.hpp"
#include <QtTest>
#include <QUdpSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSignalSpy>
#include <QTimer>
using namespace portbridge;
namespace {
WorkflowNode node(QString id, QString type, QJsonObject p = {}) { return {id, type, type, {}, p}; }
WorkflowDocument sequence(QVector<WorkflowNode> nodes) {
    WorkflowDocument d; d.id = "test"; d.name = "test"; d.nodes = nodes;
    for (int i = 1; i < nodes.size(); ++i) d.edges.append({QString::number(i), nodes[i - 1].id, nodes[i].id, "success"});
    return d;
}
WorkflowDocument rawFlow(QJsonObject wait) { return sequence({node("s", "start"), node("r", "raw"), node("w", "sendWait", wait), node("e", "end")}); }
ConnectionConfig udpConfig(quint16 remote) { ConnectionConfig c; c.localAddress = "127.0.0.1"; c.localPort = 0; c.remotePort = remote; return c; }
SharedBytes shared(const QByteArray& b) { return std::make_shared<const Bytes>(reinterpret_cast<const std::uint8_t*>(b.constData()), reinterpret_cast<const std::uint8_t*>(b.constData()) + b.size()); }
}
class WorkflowTest : public QObject {
    Q_OBJECT
private slots:
    void templatesAndRoundtrip() {
        for (const auto& key : {"login", "udp", "blank"}) {
            auto d = WorkflowDocument::templateDocument(key); QVERIFY2(d.validate().isEmpty(), qPrintable(d.validate().isEmpty() ? "" : d.validate().first().message)); WorkflowDocument copy; QString error;
            QVERIFY2(WorkflowDocument::fromJson(d.toJson(), &copy, &error), qPrintable(error)); QCOMPARE(copy.toJson(), d.toJson());
        }
        WorkflowRunner runner; QCOMPARE(runner.state(), WorkflowRunState::Idle); QVERIFY(runner.resourceSummary().contains("未绑定"));
    }
    void strictSchemaAndValidation() {
        auto d = WorkflowDocument::templateDocument("blank"); auto json = d.toJson(); WorkflowDocument out; QString error;
        json["prototypeOnly"] = true; QVERIFY(!WorkflowDocument::fromJson(json, &out, &error));
        json = d.toJson(); json["schemaVersion"] = 2; QVERIFY(!WorkflowDocument::fromJson(json, &out, &error));
        d.nodes[1].type = "script"; QVERIFY(!d.validate().isEmpty()); QVERIFY(!WorkflowDocument::fromJson(d.toJson(), &out, &error));
        d = WorkflowDocument::templateDocument("blank"); d.nodes[0].parameters["execute"] = "evil"; QVERIFY(!d.validate().isEmpty());
        d = sequence({node("s", "start"), node("a", "log"), node("b", "log"), node("e", "end")}); d.edges.append({"cycle", "b", "a", "error"}); QVERIFY(!d.validate().isEmpty());
        d = WorkflowDocument::templateDocument("blank"); d.edges[0].to = "missing"; QVERIFY(!d.validate().isEmpty());
        d = WorkflowDocument::templateDocument("blank"); d.limits.maxSteps = 0; QVERIFY(!d.validate().isEmpty());
        ConnectionConfig c; auto snapshot = workflowConnectionConfigToJson(c); ConnectionConfig copy; QVERIFY(workflowConnectionConfigFromJson(snapshot, &copy, &error)); QCOMPARE(workflowConnectionConfigToJson(copy), snapshot);
        snapshot["remotePort"] = 70000; QVERIFY(!workflowConnectionConfigFromJson(snapshot, &copy, &error));
        auto imported = d.toJson(); QJsonObject deep; for (int i = 0; i < 18; ++i) deep = {{"nested", deep}}; imported["variables"] = deep; QVERIFY(!WorkflowDocument::fromJson(imported, &out, &error));
    }
    void variablesBranchLoopAndImmutablePlan() {
        WorkflowDocument d; d.id = "logic"; d.name = "logic";
        d.nodes = {node("s", "start"), node("v", "variable", {{"variable", "status"}, {"value", "ready"}}), node("if", "branch", {{"variable", "status"}, {"expected", "other"}}), node("bad", "end", {{"success", false}}), node("l", "loop", {{"count", "3"}}), node("body", "log", {{"text", "${status}"}}), node("e", "end")};
        d.edges = {{"1", "s", "v"}, {"2", "v", "if"}, {"3", "if", "bad", "true"}, {"4", "if", "l", "false"}, {"5", "l", "body", "body"}, {"6", "body", "l"}, {"7", "l", "e", "done"}};
        QVERIFY2(d.validate().isEmpty(), qPrintable(d.validate().isEmpty() ? "" : d.validate().first().message));
        WorkflowRunner runner; QString error; QVERIFY2(runner.start(d, &error), qPrintable(error)); d.nodes[1].parameters["value"] = "mutated";
        QTRY_COMPARE_WITH_TIMEOUT(runner.state(), WorkflowRunState::Completed, 2000); QCOMPARE(runner.variables()["status"].toString(), QString("ready")); QCOMPARE(runner.result("if").state, WorkflowNodeState::Succeeded); QCOMPARE(runner.result("if").output["matched"].toBool(), false); QCOMPARE(runner.result("bad").state, WorkflowNodeState::Skipped);
        int bodyCount = 0; for (const auto& log : runner.logs()) if (log.nodeId == "body") { ++bodyCount; QCOMPARE(log.detail, QString("ready")); } QCOMPARE(bodyCount, 3);
        d.limits.maxSteps = 4; QVERIFY(runner.start(d, &error)); QTRY_COMPARE_WITH_TIMEOUT(runner.state(), WorkflowRunState::Failed, 2000);
    }
    void extractionAndErrorBranches() {
        auto d = sequence({node("s", "start"), node("x", "extract", {{"source", "response.body"}, {"path", "$.token"}, {"variable", "token"}}), node("a", "assert", {{"source", "token"}, {"expected", "secret"}}), node("e", "end")}); d.initialVariables = {{"response", QJsonObject{{"body", "{\"token\":\"secret\"}"}}}};
        WorkflowRunner runner; QString error; QVERIFY(runner.start(d, &error)); QTRY_COMPARE_WITH_TIMEOUT(runner.state(), WorkflowRunState::Completed, 2000); QCOMPARE(runner.variables()["token"].toString(), QString("secret"));
        d.nodes[1].parameters["path"] = "$.missing"; d.edges.append({"handler", "x", "e", "error"}); QVERIFY(runner.start(d, &error)); QTRY_COMPARE_WITH_TIMEOUT(runner.state(), WorkflowRunState::Completed, 2000); QCOMPARE(runner.result("x").state, WorkflowNodeState::Failed); QCOMPARE(runner.result("a").state, WorkflowNodeState::Skipped);
        d.edges.removeLast(); QVERIFY(runner.start(d, &error)); QTRY_COMPARE_WITH_TIMEOUT(runner.state(), WorkflowRunState::Failed, 2000);
    }
    void pauseCancellationDeadlineAndBounds() {
        auto d = sequence({node("s", "start"), node("d", "delay", {{"duration", "50"}}), node("v", "variable", {{"variable", "after"}, {"value", true}}), node("e", "end")}); WorkflowRunner runner; QString error;
        QVERIFY(runner.start(d, &error)); QTRY_COMPARE(runner.activeNodeId(), QString("d")); runner.pause(); QTest::qWait(100); QCOMPARE(runner.state(), WorkflowRunState::Paused); QCOMPARE(runner.result("d").state, WorkflowNodeState::Succeeded); QVERIFY(!runner.variables().contains("after")); runner.resume(); QTRY_COMPARE(runner.state(), WorkflowRunState::Completed);
        QVERIFY(runner.start(d, &error)); QTRY_COMPARE(runner.activeNodeId(), QString("d")); const auto oldRun = runner.runId(); runner.stop(); QCOMPARE(runner.state(), WorkflowRunState::Stopped); QCOMPARE(runner.result("d").state, WorkflowNodeState::Failed);
        auto fast = sequence({node("s", "start"), node("e", "end")}); QVERIFY(runner.start(fast, &error)); QVERIFY(oldRun != runner.runId()); QTRY_COMPARE(runner.state(), WorkflowRunState::Completed); QTest::qWait(100); QVERIFY(!runner.variables().contains("after")); QCOMPARE(runner.logs().size(), 2);
        d.limits.maxDurationMs = 20; QVERIFY(runner.start(d, &error)); QTRY_COMPARE(runner.state(), WorkflowRunState::Failed);
        d = sequence({node("s", "start"), node("v", "variable", {{"variable", "big"}, {"value", QString(2000, 'x')}}), node("e", "end")}); d.limits.maxVariableBytes = 256; QVERIFY(runner.start(d, &error)); QTRY_COMPARE(runner.state(), WorkflowRunState::Failed); QVERIFY(!runner.variables().contains("big"));
    }
    void udpFastReplyPausedAndSourceFilter() {
        QUdpSocket peer, impostor; QVERIFY(peer.bind(QHostAddress::LocalHost, 0)); QVERIFY(impostor.bind(QHostAddress::LocalHost, 0));
        connect(&peer, &QUdpSocket::readyRead, this, [&] { while (peer.hasPendingDatagrams()) { QByteArray b(int(peer.pendingDatagramSize()), '\0'); QHostAddress from; quint16 port; peer.readDatagram(b.data(), b.size(), &from, &port); impostor.writeDatagram("ready", from, port); peer.writeDatagram("ready", from, port); } });
        SessionController session; session.start(udpConfig(peer.localPort())); QTRY_VERIFY(session.connected()); session.setDisplayPaused(true); session.setHighSpeed(true);
        auto d = rawFlow({{"format", "text"}, {"payload", "ping"}, {"match", "equals"}, {"expected", "ready"}, {"timeout", "1000"}, {"sourceFilter", QJsonObject{{"address", "127.0.0.1"}, {"port", peer.localPort()}}}});
        WorkflowRunner runner; runner.setSessionProvider([&] { return &session; }); QString error; QVERIFY2(runner.start(d, &error), qPrintable(error)); QTRY_VERIFY_WITH_TIMEOUT(!runner.active(), 3000); QVERIFY2(runner.state() == WorkflowRunState::Completed, qPrintable(runner.logs().last().detail)); QCOMPARE(runner.variables()["message"].toObject()["text"].toString(), QString("ready")); QVERIFY(session.connected()); QVERIFY(session.takeDisplayRecords().empty()); QVERIFY(session.statistics().rxDatagrams >= 2);
        d.nodes[2].parameters["sourceFilter"] = QJsonObject{{"port", 1}}; d.nodes[2].parameters["timeout"] = "50"; QVERIFY(runner.start(d, &error)); QTRY_COMPARE(runner.state(), WorkflowRunState::Failed);
    }
    void tcpSplitCoalescedDelimiterFixedAndHeader() {
        for (const auto& mode : {"delimiter", "fixed", "lengthHeader"}) {
            QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost)); QPointer<QTcpSocket> socket;
            connect(&server, &QTcpServer::newConnection, this, [&] { socket = server.nextPendingConnection(); connect(socket, &QTcpSocket::readyRead, this, [&, mode] { socket->readAll(); QByteArray first, last; if (QString(mode) == "delimiter") { first = "no\nrea"; last = "dy\nextra\n"; } else if (QString(mode) == "fixed") { first = "nope!rea"; last = "dyextra"; } else { first = QByteArray::fromHex("00026e6f0005") + "rea"; last = "dy"; } socket->write(first); socket->flush(); QTimer::singleShot(15, this, [&, last] { if (socket) { socket->write(last); socket->flush(); } }); }); });
            SessionController session; auto config = udpConfig(server.serverPort()); config.kind = TransportKind::TcpClient; session.start(config); QTRY_VERIFY(session.connected()); session.setDisplayPaused(true);
            QJsonObject framing = QString(mode) == "delimiter" ? QJsonObject{{"mode", mode}, {"delimiter", "\n"}} : QString(mode) == "fixed" ? QJsonObject{{"mode", mode}, {"length", 5}} : QJsonObject{{"mode", mode}, {"headerBytes", 2}, {"byteOrder", "big"}};
            auto d = rawFlow({{"format", "text"}, {"payload", "ping"}, {"match", "equals"}, {"expected", "ready"}, {"timeout", "1000"}, {"framing", framing}}); WorkflowRunner runner; runner.setSessionProvider([&] { return &session; }); QString error; QVERIFY2(runner.start(d, &error), qPrintable(error)); QTRY_VERIFY_WITH_TIMEOUT(!runner.active(), 3000); QVERIFY2(runner.state() == WorkflowRunState::Completed, qPrintable(runner.logs().last().detail)); QCOMPARE(runner.variables()["message"].toObject()["text"].toString(), QString("ready")); QVERIFY(session.connected()); session.stop();
        }
    }
    void rawObserverCapacityEpochAndNoDisplayCoupling() {
        SessionController session; session.start(udpConfig(9)); QTRY_VERIFY(session.connected()); session.setDisplayPaused(true); const auto token = detailSessionTestAccess::generation(session);
        QString error; const auto observer = session.observeRaw(1024, &error); QVERIFY(observer); DataRecord r; r.direction = Direction::Receive; r.payload = shared("hello"); r.transport = TransportKind::Udp;
        QVERIFY(detailSessionTestAccess::data(session, r, token)); auto batch = session.takeRawObservation(observer); QCOMPARE(batch.records.size(), size_t(1)); QVERIFY(batch.records[0].payload == r.payload); QVERIFY(session.takeRawObservation(observer).records.empty());
        auto allocation = std::make_shared<Bytes>(1, 42); allocation->reserve(4096); r.payload = allocation; QVERIFY(detailSessionTestAccess::data(session, r, token)); batch = session.takeRawObservation(observer); QVERIFY(batch.error.contains("overflow")); QVERIFY(batch.records.empty()); QVERIFY(session.takeRawObservation(observer).error.contains("overflow")); session.removeRawObserver(observer);
        const auto second = session.observeRaw(1024, &error); QVERIFY(second); session.start(udpConfig(9)); batch = session.takeRawObservation(second); QVERIFY(batch.error.contains("replaced")); QVERIFY(batch.epoch != session.sessionEpoch()); session.removeRawObserver(second);
    }
    void sessionReplacementStopsRunAndOwnedCleanup() {
        SessionController session; session.start(udpConfig(9)); QTRY_VERIFY(session.connected());
        auto d = rawFlow({{"format", "text"}, {"payload", "ping"}, {"match", "any"}, {"timeout", "1000"}}); WorkflowRunner runner; runner.setSessionProvider([&] { return &session; }); QString error; QVERIFY(runner.start(d, &error)); QTRY_COMPARE(runner.activeNodeId(), QString("w")); session.start(udpConfig(9)); QTRY_COMPARE(runner.state(), WorkflowRunState::Failed); QTRY_VERIFY(session.connected());
        QVERIFY(runner.start(d, &error)); QTRY_COMPARE(runner.activeNodeId(), QString("w")); runner.stop(); QVERIFY(session.connected());
        session.stop(); auto owned = sequence({node("s", "start"), node("r", "raw", {{"ownership", "owned"}, {"config", workflowConnectionConfigToJson(udpConfig(9))}}), node("e", "end")}); QVERIFY2(runner.start(owned, &error), qPrintable(error)); QTRY_COMPARE(runner.state(), WorkflowRunState::Completed); QVERIFY(!session.connected());
    }
    void workflowObserverOverflowAndSampledDisplay() {
        QUdpSocket peer; QVERIFY(peer.bind(QHostAddress::LocalHost, 0));
        connect(&peer, &QUdpSocket::readyRead, this, [&] {
            while (peer.hasPendingDatagrams()) {
                QByteArray b(int(peer.pendingDatagramSize()), '\0'); QHostAddress from; quint16 port;
                peer.readDatagram(b.data(), b.size(), &from, &port);
                for (int i = 0; i < 64; ++i) peer.writeDatagram("other", from, port);
                peer.writeDatagram("ready", from, port);
            }
        });
        SessionController session; session.start(udpConfig(peer.localPort())); QTRY_VERIFY(session.connected()); session.setHighSpeed(true);
        auto d = rawFlow({{"format", "text"}, {"payload", "ping"}, {"match", "equals"}, {"expected", "ready"}, {"timeout", "1000"}});
        WorkflowRunner runner; runner.setSessionProvider([&] { return &session; }); QString error;
        QVERIFY(runner.start(d, &error)); QTRY_COMPARE(runner.state(), WorkflowRunState::Completed); QVERIFY(session.statistics().displayOmitted > 0);
        session.setDisplayPaused(true); d.limits.observerQueueBytes = 256;
        QVERIFY(runner.start(d, &error)); QTRY_COMPARE(runner.state(), WorkflowRunState::Failed);
        QVERIFY2(runner.result("w").detail.contains("溢出"), qPrintable(runner.result("w").detail)); QVERIFY(session.connected());
    }
    void ownedReplacementDoesNotStopReplacement() {
        SessionController session; auto d = sequence({node("s", "start"), node("r", "raw", {{"ownership", "owned"}, {"config", workflowConnectionConfigToJson(udpConfig(9))}}), node("d", "delay", {{"duration", "500"}}), node("e", "end")});
        WorkflowRunner runner; runner.setSessionProvider([&] { return &session; }); QString error;
        QVERIFY(runner.start(d, &error)); QTRY_COMPARE(runner.activeNodeId(), QString("d")); auto replacement = udpConfig(19); replacement.name = "manual"; session.start(replacement);
        QTRY_COMPARE(runner.state(), WorkflowRunState::Failed); QTRY_VERIFY(session.connected()); QCOMPARE(session.config().name, std::string("manual"));
    }
    void tcpDisconnectAndAssemblyOverflow() {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost)); QPointer<QTcpSocket> socket; bool overflow = true;
        connect(&server, &QTcpServer::newConnection, this, [&] { socket = server.nextPendingConnection(); connect(socket, &QTcpSocket::readyRead, this, [&] { socket->readAll(); if (overflow) { socket->write(QByteArray(1024, 'x')); socket->flush(); } else socket->disconnectFromHost(); }); });
        SessionController session; auto config = udpConfig(server.serverPort()); config.kind = TransportKind::TcpClient; session.start(config); QTRY_VERIFY(session.connected()); auto d = rawFlow({{"format", "text"}, {"payload", "ping"}, {"match", "any"}, {"timeout", "1000"}, {"framing", QJsonObject{{"mode", "delimiter"}, {"delimiter", "\n"}}}}); d.limits.maxMessageBytes = 256;
        WorkflowRunner runner; runner.setSessionProvider([&] { return &session; }); QString error; QVERIFY(runner.start(d, &error)); QTRY_COMPARE(runner.state(), WorkflowRunState::Failed); QVERIFY2(runner.result("w").detail.contains("上限"), qPrintable(runner.result("w").detail));
        overflow = false; QVERIFY(runner.start(d, &error)); QTRY_COMPARE(runner.state(), WorkflowRunState::Failed);
    }
};
QTEST_GUILESS_MAIN(WorkflowTest)
#include "test_workflow.moc"
