// Independent application e2e: Qt is used only for localhost HTTP/WS servers.
// The client under test is the actual cpr/curl/Beast/OpenSSL protocol library.
#include <portbridge/workflow.hpp>
#include <portbridge/workflow_protocol.hpp>
#include "ui/workflow_page.hpp"
#include <QtTest>
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslSocket>
#include <QStandardPaths>
#include <QTableView>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QWebSocket>
#include <QWebSocketServer>
#include <algorithm>
#include <memory>
using namespace portbridge;
namespace {
constexpr int TestDeadline = 6000;
class HttpServer : public QTcpServer {
public:
    bool secure = false, hold = false;
    int status = 200, requests = 0;
    QByteArray response = "{\"token\":\"e2e-token\",\"enabled\":true,\"count\":7}";
    QJsonObject requestHeaders;
    QByteArray requestBody, method, path, extraHeaders;
    QSslCertificate certificate;
    QSslKey key;
    QList<QPointer<QTcpSocket>> sockets;
    QString url(const QString& host = "127.0.0.1") const {
        return QString("%1://%2:%3/api/token").arg(secure ? "https" : "http", host).arg(serverPort());
    }
    ~HttpServer() override { close(); for (const auto& socket : sockets) if (socket) socket->abort(); }
protected:
    void incomingConnection(qintptr descriptor) override {
        QTcpSocket* socket = nullptr;
        if (secure) {
            auto* tls = new QSslSocket(this); tls->setSocketDescriptor(descriptor);
            tls->setLocalCertificate(certificate); tls->setPrivateKey(key);
            tls->setPeerVerifyMode(QSslSocket::VerifyNone); socket = tls; tls->startServerEncryption();
        } else { socket = new QTcpSocket(this); socket->setSocketDescriptor(descriptor); }
        sockets.append(socket);
        auto input = std::make_shared<QByteArray>(); auto replied = std::make_shared<bool>(false);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, input, replied] {
            *input += socket->readAll(); if (*replied) return;
            const auto end = input->indexOf("\r\n\r\n"); if (end < 0) return;
            QJsonObject headers; const auto lines = input->left(end).split('\n');
            for (const auto& raw : lines) { const auto line = raw.trimmed(); const auto colon = line.indexOf(':'); if (colon > 0) headers[QString::fromLatin1(line.left(colon)).toLower()] = QString::fromUtf8(line.mid(colon + 1).trimmed()); }
            const auto length = headers.value("content-length").toString().toInt(); if (input->size() < end + 4 + length) return;
            *replied = true; ++requests; requestHeaders = headers;
            method = lines.front().split(' ').value(0); path = lines.front().split(' ').value(1); requestBody = input->mid(end + 4, length);
            if (hold) return;
            socket->write("HTTP/1.1 " + QByteArray::number(status) + " Test\r\nContent-Type: application/json\r\nX-E2E: actual-server\r\nContent-Length: " + QByteArray::number(response.size()) + "\r\nConnection: close\r\n" + extraHeaders + "\r\n" + response);
            socket->disconnectFromHost();
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
};
class WsServer : public QObject {
public:
    QWebSocketServer server;
    QList<QPointer<QWebSocket>> peers;
    QPointer<QWebSocket> latest;
    QByteArray authorization, received;
    QJsonObject requestHeaders;
    bool receivedBinary = false, defer = false, echo = false;
    int connections = 0, subscriptions = 0, sequence = 42, padding = 0;
    explicit WsServer(bool secure = false) : server("Independent e2e server", secure ? QWebSocketServer::SecureMode : QWebSocketServer::NonSecureMode, this) {
        connect(&server, &QWebSocketServer::newConnection, this, [this] {
            auto* socket = server.nextPendingConnection(); socket->setParent(this); latest = socket; peers.append(socket); ++connections;
            authorization = socket->request().rawHeader("Authorization"); requestHeaders = {};
            for (const auto& name : socket->request().rawHeaderList()) requestHeaders[QString::fromLatin1(name).toLower()] = QString::fromUtf8(socket->request().rawHeader(name));
            socket->setOutgoingFrameSize(17); // Exercise complete-message assembly over many WS frames.
            connect(socket, &QWebSocket::textMessageReceived, this, [this, socket](const QString& message) { received = message.toUtf8(); receivedBinary = false; ++subscriptions; if (!defer) reply(socket); });
            connect(socket, &QWebSocket::binaryMessageReceived, this, [this, socket](const QByteArray& message) { received = message; receivedBinary = true; ++subscriptions; if (!defer) reply(socket); });
            connect(socket, &QWebSocket::disconnected, socket, &QObject::deleteLater);
        });
    }
    ~WsServer() override { server.close(); for (const auto& socket : peers) if (socket) socket->abort(); }
    QString url(const QString& host = "127.0.0.1") const { return QString("%1://%2:%3/events").arg(server.secureMode() == QWebSocketServer::SecureMode ? "wss" : "ws", host).arg(server.serverPort()); }
    QByteArray response() const { return QJsonDocument(QJsonObject{{"type", "status"}, {"device", QJsonObject{{"state", "ready"}}}, {"seq", sequence}, {"enabled", true}, {"values", QJsonArray{1, "typed"}}, {"padding", QString(padding, 'x')}}).toJson(QJsonDocument::Compact); }
    void reply(QWebSocket* socket = nullptr) { if (!socket) socket = latest; if (!socket) return; if (echo && receivedBinary) socket->sendBinaryMessage(received); else socket->sendTextMessage(QString::fromUtf8(echo ? received : response())); socket->flush(); }
    int activePeers() const { int active = 0; for (const auto& socket : peers) if (socket && socket->state() == QAbstractSocket::ConnectedState) ++active; return active; }
};
WorkflowDocument login(HttpServer& http, WsServer& ws) {
    auto document = WorkflowDocument::templateDocument("login");
    document.nodes[1].parameters["url"] = http.url(); document.nodes[3].parameters["url"] = ws.url();
    return document;
}
WorkflowDocument linear(const QVector<WorkflowNode>& nodes) {
    WorkflowDocument document; document.id = "independent-e2e"; document.name = "真实协议组合验收"; document.nodes = nodes;
    for (int i = 1; i < nodes.size(); ++i) document.edges.append({QString::number(i), nodes[i - 1].id, nodes[i].id, "success"});
    return document;
}
WorkflowNode node(const QString& id, const QString& type, const QJsonObject& parameters = {}) { return {id, type, id, {}, parameters}; }
QPushButton* control(WorkflowPage& page, const char* name) { auto* button = page.findChild<QPushButton*>(name); if (!button) qFatal("Missing workflow control %s", name); return button; }
QString runDetails(WorkflowRunner& runner) { QString result; for (const auto& log : runner.logs()) result += log.nodeId + ": " + log.detail + "\n"; return result; }
void evidence(const QString& name, const QJsonObject& result) {
    const auto directory = qEnvironmentVariable("PORTBRIDGE_E2E_EVIDENCE"); if (directory.isEmpty()) return;
    QFile file(directory + "/" + name + ".json"); if (!file.open(QIODevice::WriteOnly)) qFatal("Cannot write requested e2e evidence"); file.write(QJsonDocument(result).toJson(QJsonDocument::Indented));
}
void evidence(const QString& name, WorkflowRunner& runner) {
    QJsonObject results; for (const auto& id : {QString("n1"), QString("n2"), QString("n3"), QString("n4"), QString("n5"), QString("n6"), QString("n7"), QString("n8"), QString("recovery")}) { const auto item = runner.result(id); results[id] = QJsonObject{{"state", int(item.state)}, {"output", item.output}, {"detail", item.detail}, {"elapsedMs", double(item.elapsedMs)}}; }
    QJsonArray logs; for (const auto& item : runner.logs()) logs.append(QJsonObject{{"nodeId", item.nodeId}, {"status", item.status}, {"detail", item.detail}});
    evidence(name, QJsonObject{{"runId", runner.runId()}, {"state", int(runner.state())}, {"variables", runner.variables()}, {"results", results}, {"logs", logs}, {"fixtureOnly", true}});
}
}
class WorkflowE2eTest : public QObject {
    Q_OBJECT
    QTemporaryDir certs;
    QSslCertificate certificate;
    QSslKey key;
    QString ca;
private slots:
    void initTestCase() {
        QVERIFY(certs.isValid());
        const auto executable = qEnvironmentVariable("PORTBRIDGE_TEST_OPENSSL", qEnvironmentVariable("ProgramFiles") + "/Git/mingw64/bin/openssl.exe");
        QVERIFY2(QFile::exists(executable), "Existing OpenSSL CLI is required only for isolated server certificate generation");
        ca = certs.filePath("trusted-ca.pem"); const auto privateKey = certs.filePath("key.pem"); QProcess generator;
        generator.start(executable, {"req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", privateKey, "-out", ca, "-days", "2", "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost", "-addext", "basicConstraints=critical,CA:TRUE"});
        QVERIFY(generator.waitForFinished(10000)); QCOMPARE(generator.exitCode(), 0);
        QFile pem(ca); QVERIFY(pem.open(QIODevice::ReadOnly)); certificate = QSslCertificate(pem.readAll(), QSsl::Pem);
        QFile secret(privateKey); QVERIFY(secret.open(QIODevice::ReadOnly)); key = QSslKey(secret.readAll(), QSsl::Rsa, QSsl::Pem);
        QVERIFY(!certificate.isNull()); QVERIFY(!key.isNull()); QVERIFY2(QSslSocket::supportsSsl(), "Qt TLS is needed only by the test servers");
    }
    void canonicalLoginThroughPageButtonsPauseAndResults() {
        HttpServer http; WsServer ws; ws.defer = true; QVERIFY(http.listen(QHostAddress::LocalHost)); QVERIFY(ws.server.listen(QHostAddress::LocalHost));
        WorkflowPage page; page.resize(1200, 780); page.show(); auto document = login(http, ws); QVERIFY(document.validate().isEmpty()); QVERIFY(page.setDocument(document));
        auto* runner = page.runner(); QSignalSpy done(runner, &WorkflowRunner::finished);
        QCOMPARE(http.requests, 0); QCOMPARE(ws.connections, 0); control(page, "workflowRun")->click();
        QTRY_COMPARE_WITH_TIMEOUT(ws.subscriptions, 1, TestDeadline); QCOMPARE(runner->activeNodeId(), QString("n5"));
        QCOMPARE(http.method, QByteArray("POST")); QCOMPARE(http.path, QByteArray("/api/token")); QCOMPARE(http.requestHeaders["content-type"].toString(), QString("application/json"));
        QCOMPARE(QJsonDocument::fromJson(http.requestBody).object(), QJsonObject({{"device", "bench-01"}}));
        QCOMPARE(ws.authorization, QByteArray("Bearer e2e-token")); QCOMPARE(QJsonDocument::fromJson(ws.received).object(), QJsonObject({{"action", "subscribe"}, {"topic", "device.status"}})); QVERIFY(!ws.receivedBinary);
        control(page, "workflowPause")->click(); QCOMPARE(runner->state(), WorkflowRunState::Paused); ws.reply();
        QTRY_COMPARE_WITH_TIMEOUT(runner->result("n5").state, WorkflowNodeState::Succeeded, TestDeadline); QCOMPARE(runner->state(), WorkflowRunState::Paused); QCOMPARE(runner->result("n6").state, WorkflowNodeState::Pending); QVERIFY(done.isEmpty());
        const auto variables = runner->variables(); QCOMPARE(variables["token"].toString(), QString("e2e-token")); QVERIFY(variables["message"].toObject()["enabled"].isBool()); QVERIFY(variables["message"].toObject()["seq"].isDouble());
        const auto response = variables["httpResponse"].toObject(); QCOMPARE(response["status"].toInt(), 200); QVERIFY(response["body"].isObject()); QCOMPARE(response["body"].toObject()["count"].toInt(), 7); QVERIFY(response["body"].toObject()["enabled"].isBool());
        QCOMPARE(response["headers"].toObject()["x-e2e"].toString(), QString("actual-server")); QCOMPARE(QByteArray::fromBase64(response["bodyBase64"].toString().toLatin1()), http.response); QCOMPARE(response["bodyText"].toString().toUtf8(), http.response); QCOMPARE(response["bodyBytes"].toInt(), http.response.size()); QVERIFY(response["elapsedMs"].toDouble() >= 0);
        page.selectNode("n5", true); auto* result = page.findChild<QPlainTextEdit*>("workflowResult"); QVERIFY(result); QTRY_VERIFY(result->toPlainText().contains("ready"));
        auto* logs = page.findChild<QTableView*>("workflowLogs"); auto* values = page.findChild<QTableView*>("workflowVariables"); QVERIFY(logs && values); QCOMPARE(logs->model()->rowCount(), runner->logs().size()); QVERIFY(values->model()->rowCount() >= 3);
        control(page, "workflowPause")->click(); QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, TestDeadline); QVERIFY2(done[0][0].toBool(), qPrintable(runDetails(*runner))); QCOMPARE(runner->state(), WorkflowRunState::Completed);
        for (const auto& item : document.nodes) QCOMPARE(runner->result(item.id).state, WorkflowNodeState::Succeeded);
        evidence("canonical-page-login", *runner);
        QCOMPARE(runner->logs().size(), 8); QCOMPARE(runner->result("n7").output["closed"].toBool(), true); QCOMPARE(runner->result("n7").output["peerCode"].toInt(), 1000);
        QTRY_COMPARE_WITH_TIMEOUT(ws.activePeers(), 0, TestDeadline); QTRY_VERIFY(page.findChildren<WorkflowProtocolClient*>().isEmpty()); QCOMPARE(http.requests, 1); QCOMPARE(ws.connections, 1);
    }
    void jsonBodySubstitutionPreservesQuotesObjectsAndTypes() {
        HttpServer http; WsServer ws; QVERIFY(http.listen(QHostAddress::LocalHost)); QVERIFY(ws.server.listen(QHostAddress::LocalHost)); auto document = login(http, ws);
        const QString quoted = "bench\"\\line\n中文"; const QJsonObject nested{{"name", "nested"}, {"valid", true}};
        document.initialVariables = {{"device", quoted}, {"enabled", true}, {"count", 7}, {"nested", nested}};
        document.nodes[1].parameters["body"] = "{\"device\":\"${device}\",\"enabled\":\"${enabled}\",\"count\":\"${count}\",\"nested\":\"${nested}\",\"message\":\"prefix ${device}\"}";
        document.nodes[1].parameters["bodyMode"] = "json"; document.nodes[3].parameters["headers"] = QJsonObject{{"Authorization", "Bearer ${token}"}};
        WorkflowRunner runner; QString why; QVERIFY2(runner.start(document, &why), qPrintable(why)); QTRY_VERIFY_WITH_TIMEOUT(!runner.active(), TestDeadline); QCOMPARE(runner.state(), WorkflowRunState::Completed);
        QJsonParseError parse; evidence("typed-json-body", runner); evidence("http-request-body", QJsonObject{{"received", QJsonDocument::fromJson(http.requestBody).object()}}); const auto body = QJsonDocument::fromJson(http.requestBody, &parse); QCOMPARE(parse.error, QJsonParseError::NoError); QCOMPARE(body.object()["device"].toString(), quoted); QVERIFY(body.object()["enabled"].isBool()); QCOMPARE(body.object()["count"].toInt(), 7); QCOMPARE(body.object()["nested"].toObject(), nested); QCOMPARE(body.object()["message"].toString(), "prefix " + quoted); QCOMPARE(ws.authorization, QByteArray("Bearer e2e-token")); QTRY_COMPARE(ws.activePeers(), 0);
    }
    void jsonHeaderTextSubstitutionPreservesQuotes_data() { QTest::addColumn<bool>("wholeObject"); QTest::newRow("json-header-text") << false; QTest::newRow("whole-object-variable") << true; }
    void jsonHeaderTextSubstitutionPreservesQuotes() {
        QFETCH(bool, wholeObject); HttpServer http; WsServer ws; QVERIFY(http.listen(QHostAddress::LocalHost)); QVERIFY(ws.server.listen(QHostAddress::LocalHost)); auto document = login(http, ws); const QString label = "quoted\"\\label"; document.initialVariables["label"] = label;
        if (wholeObject) { document.initialVariables["requestHeaders"] = QJsonObject{{"Content-Type", "application/json"}, {"X-Label", "prefix " + label}}; document.nodes[1].parameters["headers"] = " ${requestHeaders} "; }
        else document.nodes[1].parameters["headers"] = QString::fromUtf8(QJsonDocument(QJsonObject{{"Content-Type", "application/json"}, {"X-Label", "prefix ${label}"}}).toJson(QJsonDocument::Compact));
        WorkflowRunner runner; QString why; QVERIFY2(runner.start(document, &why), qPrintable(why)); QTRY_VERIFY_WITH_TIMEOUT(!runner.active(), TestDeadline);
        evidence(wholeObject ? "whole-object-headers" : "json-header-quoted-value", runner); QVERIFY2(runner.state() == WorkflowRunState::Completed, qPrintable(runDetails(runner))); QCOMPARE(http.requestHeaders["x-label"].toString(), "prefix " + label); QTRY_COMPARE(ws.activePeers(), 0);
    }
    void tokenCannotInjectNewHeadersInCanonicalTemplate_data() { QTest::addColumn<QString>("newline"); QTest::newRow("crlf-token") << QString("\r\n"); QTest::newRow("lf-token") << QString("\n"); }
    void tokenCannotInjectNewHeadersInCanonicalTemplate() {
        QFETCH(QString, newline); HttpServer http; WsServer ws; http.response = QJsonDocument(QJsonObject{{"token", "good" + newline + "X-Injected: yes"}}).toJson(QJsonDocument::Compact); QVERIFY(http.listen(QHostAddress::LocalHost)); QVERIFY(ws.server.listen(QHostAddress::LocalHost)); WorkflowRunner runner; QString why; QVERIFY2(runner.start(login(http, ws), &why), qPrintable(why)); QTRY_VERIFY_WITH_TIMEOUT(!runner.active(), TestDeadline);
        evidence(newline.size() == 2 ? "token-crlf-rejection" : "token-lf-rejection", runner); evidence(newline.size() == 2 ? "token-crlf-server" : "token-lf-server", QJsonObject{{"wsConnections", ws.connections}, {"headers", ws.requestHeaders}});
        QVERIFY2(runner.state() == WorkflowRunState::Failed, "Canonical token substitution injected another request header and still completed"); QCOMPARE(ws.connections, 0); QCOMPARE(runner.result("n4").state, WorkflowNodeState::Failed);
    }
    void http401AnyRetainsResponseAndMissingTokenFailsBeforeWs() {
        HttpServer http; WsServer ws; http.status = 401; http.response = "{\"error\":\"unauthorized\",\"retryable\":false}"; QVERIFY(http.listen(QHostAddress::LocalHost)); QVERIFY(ws.server.listen(QHostAddress::LocalHost)); auto document = login(http, ws); document.nodes[1].parameters["expectedStatus"] = "any";
        WorkflowRunner runner; QString why; QVERIFY2(runner.start(document, &why), qPrintable(why)); QTRY_VERIFY_WITH_TIMEOUT(!runner.active(), TestDeadline); QCOMPARE(runner.state(), WorkflowRunState::Failed);
        evidence("http401-any-missing-token", runner);
        QCOMPARE(runner.result("n2").state, WorkflowNodeState::Succeeded); QCOMPARE(runner.result("n3").state, WorkflowNodeState::Failed); QVERIFY(runner.result("n3").detail.contains("token")); QCOMPARE(runner.result("n4").state, WorkflowNodeState::Skipped);
        const auto response = runner.variables()["httpResponse"].toObject(); QCOMPARE(response["status"].toInt(), 401); QCOMPARE(response["body"].toObject()["error"].toString(), QString("unauthorized")); QVERIFY(response["body"].toObject()["retryable"].isBool()); QCOMPARE(QByteArray::fromBase64(response["bodyBase64"].toString().toLatin1()), http.response); QCOMPARE(ws.connections, 0); QVERIFY(runner.findChildren<WorkflowProtocolClient*>().isEmpty() || !runner.findChild<WorkflowProtocolClient*>()->webSocketConnected());
    }
    void binarySendWaitRoundTripsLosslessly() {
        WsServer ws; ws.echo = true; QVERIFY(ws.server.listen(QHostAddress::LocalHost)); const auto bytes = QByteArray::fromHex("00ff414200");
        auto document = linear({node("s", "start"), node("w", "ws", {{"url", ws.url()}, {"headers", ""}}), node("m", "sendWait", {{"resource", "ws"}, {"format", "HEX"}, {"payload", "00 FF 41 42 00"}, {"match", "any"}, {"messageType", "binary"}}), node("c", "close"), node("e", "end")});
        WorkflowRunner runner; QString why; QVERIFY2(runner.start(document, &why), qPrintable(why)); QTRY_VERIFY_WITH_TIMEOUT(!runner.active(), TestDeadline); QVERIFY2(runner.state() == WorkflowRunState::Completed, qPrintable(runDetails(runner))); QCOMPARE(ws.received, bytes); QVERIFY(ws.receivedBinary);
        const auto result = runner.result("m").output; QVERIFY(result["binary"].toBool()); QCOMPARE(result["length"].toInt(), bytes.size()); QCOMPARE(QByteArray::fromBase64(result["bytesBase64"].toString().toLatin1()), bytes); QTRY_COMPARE(ws.activePeers(), 0);
    }
    void oversizedHttpHeadersBodiesAndWsFailAndRelease_data() { QTest::addColumn<QString>("kind"); QTest::newRow("http-body") << "body"; QTest::newRow("http-header") << "header"; QTest::newRow("ws-message") << "ws"; }
    void oversizedHttpHeadersBodiesAndWsFailAndRelease() {
        QFETCH(QString, kind); HttpServer http; WsServer ws; QVERIFY(http.listen(QHostAddress::LocalHost)); QVERIFY(ws.server.listen(QHostAddress::LocalHost)); auto document = login(http, ws);
        if (kind == "body") { http.response = QByteArray(4096, 'x'); document.nodes[1].parameters["maxResponseBytes"] = 128; }
        if (kind == "header") http.extraHeaders = "X-Oversized: " + QByteArray(20000, 'x') + "\r\n";
        if (kind == "ws") { ws.padding = 8192; document.nodes[3].parameters["maxMessageBytes"] = 1024; }
        WorkflowRunner runner; QString why; QVERIFY2(runner.start(document, &why), qPrintable(why)); QTRY_VERIFY_WITH_TIMEOUT(!runner.active(), TestDeadline); evidence("oversize-" + kind, runner); QCOMPARE(runner.state(), WorkflowRunState::Failed); QCOMPARE(runner.result(kind == "ws" ? "n5" : "n2").state, WorkflowNodeState::Failed); QVERIFY(runner.result(kind == "ws" ? "n5" : "n2").detail.contains(QRegularExpression("\\p{Han}"))); QVERIFY(!runner.variables().contains("message")); QTRY_COMPARE(ws.activePeers(), 0); QTRY_VERIFY(runner.findChildren<WorkflowProtocolClient*>().isEmpty());
    }
    void pageStopRetiresQueuedReplyAndNewRunRemainsIndependent() {
        HttpServer http; WsServer ws; ws.defer = true; ws.sequence = 101; QVERIFY(http.listen(QHostAddress::LocalHost)); QVERIFY(ws.server.listen(QHostAddress::LocalHost)); WorkflowPage page; page.show(); auto document = login(http, ws); QVERIFY(page.setDocument(document));
        auto* runner = page.runner(); QSignalSpy done(runner, &WorkflowRunner::finished); control(page, "workflowRun")->click(); QTRY_COMPARE_WITH_TIMEOUT(ws.subscriptions, 1, TestDeadline); const auto oldId = runner->runId();
        ws.reply(); QThread::msleep(40); // Real Beast reader queues a reply while Qt's consumer is deliberately stalled.
        QVERIFY(!runner->variables().contains("message")); control(page, "workflowStop")->click(); QCOMPARE(runner->state(), WorkflowRunState::Stopped); QCOMPARE(done.size(), 1); QVERIFY(!done[0][0].toBool());
        ws.sequence = 202; ws.defer = false; control(page, "workflowRun")->click(); QVERIFY(runner->runId() != oldId); QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, TestDeadline); QVERIFY2(done[1][0].toBool(), qPrintable(runDetails(*runner))); QCOMPARE(runner->variables()["message"].toObject()["seq"].toInt(), 202); QCOMPARE(runner->result("n1").output["runId"].toString(), runner->runId()); QCOMPARE(runner->logs().size(), 8); QTRY_COMPARE(ws.activePeers(), 0); QTRY_VERIFY(page.findChildren<WorkflowProtocolClient*>().isEmpty()); QCOMPARE(http.requests, 2); QCOMPARE(ws.connections, 2);
    }
    void unsolicitedPeerCloseRetainsUtf8ReasonAndCanRouteError_data() { QTest::addColumn<bool>("recover"); QTest::newRow("terminal-failure") << false; QTest::newRow("explicit-error-edge") << true; }
    void unsolicitedPeerCloseRetainsUtf8ReasonAndCanRouteError() {
        QFETCH(bool, recover); HttpServer http; WsServer ws; ws.defer = true; QVERIFY(http.listen(QHostAddress::LocalHost)); QVERIFY(ws.server.listen(QHostAddress::LocalHost)); auto document = login(http, ws);
        if (recover) { document.nodes.append(node("recovery", "log", {{"text", "已处理对端关闭"}})); document.edges.append({"peer-error", "n5", "recovery", "error"}); document.edges.append({"recover-end", "recovery", "n8", "success"}); }
        WorkflowRunner runner; QString why; QVERIFY2(runner.start(document, &why), qPrintable(why)); QTRY_COMPARE_WITH_TIMEOUT(ws.subscriptions, 1, TestDeadline); const auto reason = QStringLiteral("订阅令牌失效"); ws.latest->close(static_cast<QWebSocketProtocol::CloseCode>(4001), reason);
        QTRY_VERIFY_WITH_TIMEOUT(!runner.active(), TestDeadline); evidence(recover ? "peer-close-error-edge" : "peer-close-failure", runner); QCOMPARE(runner.state(), recover ? WorkflowRunState::Completed : WorkflowRunState::Failed); const auto result = runner.result("n5"); QCOMPARE(result.state, WorkflowNodeState::Failed); QCOMPARE(result.output["peerCode"].toInt(), 4001); QCOMPARE(result.output["peerReason"].toString(), reason); QVERIFY(result.output["peerInitiated"].toBool()); QVERIFY(result.detail.contains("4001")); QVERIFY(result.detail.contains(reason));
        if (recover) { QCOMPARE(runner.result("recovery").state, WorkflowNodeState::Succeeded); }
        const auto logs = runner.logs(); QVERIFY(std::any_of(logs.begin(), logs.end(), [&](const WorkflowLogEntry& log) { return log.nodeId == "n5" && log.detail.contains(reason); })); QTRY_COMPARE(ws.activePeers(), 0);
    }
    void unsolicitedPeerCloseSignalPrecedesStateAndReentrantDestructionIsSafe() {
        WsServer ws; QVERIFY(ws.server.listen(QHostAddress::LocalHost)); auto first = std::make_unique<WorkflowProtocolClient>(); auto* owned = first.get(); QPointer<WorkflowProtocolClient> client = owned; QSignalSpy opened(owned, &WorkflowProtocolClient::operationFinished), closed(owned, &WorkflowProtocolClient::webSocketClosed); QStringList order;
        connect(owned, &WorkflowProtocolClient::webSocketClosed, this, [&](int, const QString&, bool) { QCOMPARE(QThread::currentThread(), qApp->thread()); order << "close"; });
        connect(owned, &WorkflowProtocolClient::stateChanged, this, [&] { if (client && !client->webSocketConnected()) order << "state"; });
        QSignalSpy errors(owned, &WorkflowProtocolClient::protocolError);
        owned->connectWebSocket("peer-test", {{"url", ws.url()}, {"timeoutMs", 2000}}); QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, TestDeadline); QVERIFY(opened[0][2].toString().isEmpty()); ws.latest->close(static_cast<QWebSocketProtocol::CloseCode>(4001), QStringLiteral("失效")); QTRY_VERIFY_WITH_TIMEOUT(!closed.isEmpty() || !errors.isEmpty(), TestDeadline);
        evidence("peer-close-protocol", QJsonObject{{"closedSignals", int(closed.size())}, {"protocolError", errors.isEmpty() ? QString{} : errors[0][0].toString()}, {"connected", owned->webSocketConnected()}});
        QVERIFY2(closed.size() == 1, errors.isEmpty() ? "No close metadata" : qPrintable(errors[0][0].toString())); QCOMPARE(closed[0][0].toInt(), 4001); QCOMPARE(closed[0][1].toString(), QStringLiteral("失效")); QVERIFY(closed[0][2].toBool()); QTRY_COMPARE(order, QStringList({"close", "state"})); first.reset();
        ws.defer = true; owned = new WorkflowProtocolClient(this); client = owned; QSignalSpy next(owned, &WorkflowProtocolClient::operationFinished); owned->connectWebSocket("destroy-test", {{"url", ws.url()}}); QTRY_COMPARE_WITH_TIMEOUT(next.size(), 1, TestDeadline);
        connect(owned, &WorkflowProtocolClient::webSocketMessage, this, [owned](const QByteArray&, bool) { delete owned; }, Qt::DirectConnection); ws.reply(); QTRY_VERIFY_WITH_TIMEOUT(client.isNull(), TestDeadline); QTRY_COMPARE(ws.activePeers(), 0);
    }
    void abruptSocketLossDoesNotBecomeSuccessfulClose_data() { QTest::addColumn<bool>("secure"); QTest::newRow("ws-no-close-frame") << false; QTest::newRow("wss-no-close-frame") << true; }
    void abruptSocketLossDoesNotBecomeSuccessfulClose() {
        QFETCH(bool, secure); WsServer ws(secure); if (secure) { auto tls = QSslConfiguration::defaultConfiguration(); tls.setLocalCertificate(certificate); tls.setPrivateKey(key); tls.setPeerVerifyMode(QSslSocket::VerifyNone); ws.server.setSslConfiguration(tls); }
        QVERIFY(ws.server.listen(QHostAddress::LocalHost)); WorkflowProtocolClient client; QSignalSpy opened(&client, &WorkflowProtocolClient::operationFinished), errors(&client, &WorkflowProtocolClient::protocolError), closed(&client, &WorkflowProtocolClient::webSocketClosed);
        QJsonObject parameters{{"url", ws.url(secure ? "localhost" : "127.0.0.1")}, {"timeoutMs", 2000}}; if (secure) parameters["caFile"] = ca; client.connectWebSocket("abrupt-control", parameters); QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, TestDeadline); QVERIFY2(opened[0][2].toString().isEmpty(), qPrintable(opened[0][2].toString())); ws.latest->abort();
        QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), TestDeadline); QVERIFY(closed.isEmpty()); QTRY_VERIFY(!client.webSocketConnected()); evidence(secure ? "wss-abrupt-control" : "ws-abrupt-control", QJsonObject{{"closedSignals", int(closed.size())}, {"protocolError", errors[0][0].toString()}, {"connected", client.webSocketConnected()}});
    }
    void secureCombinedLoginTrustAndWrongHost_data() { QTest::addColumn<int>("mode"); QTest::newRow("trusted-ca") << 0; QTest::newRow("untrusted-http-ca") << 1; QTest::newRow("wrong-http-host") << 2; QTest::newRow("wrong-wss-host") << 3; QTest::newRow("untrusted-wss-ca") << 4; QTest::newRow("trusted-unicode-ca-path") << 5; }
    void secureCombinedLoginTrustAndWrongHost() {
        QFETCH(int, mode); HttpServer http; http.secure = true; http.certificate = certificate; http.key = key; WsServer ws(true); auto tls = QSslConfiguration::defaultConfiguration(); tls.setLocalCertificate(certificate); tls.setPrivateKey(key); tls.setPeerVerifyMode(QSslSocket::VerifyNone); ws.server.setSslConfiguration(tls);
        QVERIFY(http.listen(QHostAddress::LocalHost)); QVERIFY(ws.server.listen(QHostAddress::LocalHost)); auto document = login(http, ws); document.nodes[1].parameters["url"] = http.url(mode == 2 ? "127.0.0.1" : "localhost"); document.nodes[3].parameters["url"] = ws.url(mode == 3 ? "127.0.0.1" : "localhost");
        QString trusted = ca; if (mode == 5) { trusted = certs.filePath(QStringLiteral("可信证书.pem")); QVERIFY(QFile::copy(ca, trusted)); }
        if (mode != 1) document.nodes[1].parameters["caFile"] = trusted;
        if (mode != 1 && mode != 4) document.nodes[3].parameters["caFile"] = trusted;
        WorkflowRunner runner; QString why; QVERIFY2(runner.start(document, &why), qPrintable(why)); QTRY_VERIFY_WITH_TIMEOUT(!runner.active(), 10000);
        evidence("tls-" + QString::number(mode), runner);
        if (mode == 0 || mode == 5) { QVERIFY2(runner.state() == WorkflowRunState::Completed, qPrintable(runDetails(runner))); QCOMPARE(ws.authorization, QByteArray("Bearer e2e-token")); QCOMPARE(runner.variables()["message"].toObject()["device"].toObject()["state"].toString(), QString("ready")); QVERIFY(runner.result("n7").output["closed"].toBool()); QCOMPARE(runner.result("n7").output["peerCode"].toInt(), 1000); }
        else { QCOMPARE(runner.state(), WorkflowRunState::Failed); const auto result = runner.result(mode == 3 || mode == 4 ? "n4" : "n2"); QCOMPARE(result.state, WorkflowNodeState::Failed); QVERIFY(result.detail.contains(QRegularExpression("\\p{Han}"))); if (mode != 3 && mode != 4) QCOMPARE(ws.connections, 0); }
        QTRY_COMPARE(ws.activePeers(), 0); QTRY_VERIFY(runner.findChildren<WorkflowProtocolClient*>().isEmpty());
    }
};
QTEST_MAIN(WorkflowE2eTest)
#include "test_workflow_e2e.moc"
