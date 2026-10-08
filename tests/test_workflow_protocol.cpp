#include <portbridge/workflow_protocol.hpp>
#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSslSocket>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslConfiguration>
#include <QWebSocket>
#include <QWebSocketServer>
#include <QCryptographicHash>
#include <QProcess>
#include <QTemporaryDir>
#include <QFile>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QStandardPaths>
#include <QJsonArray>
#include <memory>
using portbridge::WorkflowProtocolClient;

namespace {
QByteArray frame(QByteArray payload, int opcode = 1, bool final = true) {
    QByteArray out; out += char((final ? 0x80 : 0) | opcode);
    if (payload.size() < 126) out += char(payload.size());
    else if (payload.size() <= 65535) { out += char(126); out += char(payload.size() >> 8); out += char(payload.size()); }
    else { out += char(127); for (int i = 7; i >= 0; --i) out += char(quint64(payload.size()) >> (i * 8)); }
    return out + payload;
}
class LocalServer : public QTcpServer {
public:
    bool tls = false;
    QSslCertificate certificate;
    QSslKey key;
    int requests = 0, pongs = 0;
    QJsonObject lastHeaders;
    QList<QPointer<QTcpSocket>> peers;
    QString url(const QString& path, const QString& protocol = "http", const QString& host = "localhost") const {
        return QString("%1%2://%3:%4%5").arg(protocol).arg(tls ? "s" : "").arg(host).arg(serverPort()).arg(path);
    }
    ~LocalServer() override { close(); for (auto peer : peers) if (peer) { peer->abort(); delete peer; } }
protected:
    void incomingConnection(qintptr descriptor) override {
        QTcpSocket* socket;
        if (tls) {
            auto* ssl = new QSslSocket(this); ssl->setSocketDescriptor(descriptor);
            ssl->setLocalCertificate(certificate); ssl->setPrivateKey(key); ssl->setPeerVerifyMode(QSslSocket::VerifyNone);
            socket = ssl; ssl->startServerEncryption();
        } else { socket = new QTcpSocket(this); socket->setSocketDescriptor(descriptor); }
        peers.append(socket);
        struct Peer { QByteArray input, message; bool ws = false, replied = false; int opcode = 1; };
        auto state = std::make_shared<Peer>();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, state] {
            state->input += socket->readAll();
            if (!state->ws) {
                const int end = state->input.indexOf("\r\n\r\n"); if (end < 0 || state->replied) return;
                const auto lines = state->input.left(end).split('\n');
                const auto path = lines.front().split(' ').value(1);
                lastHeaders = {};
                for (const auto& raw : lines) {
                    const auto line = raw.trimmed(); const auto colon = line.indexOf(':');
                    if (colon > 0) lastHeaders[QString::fromLatin1(line.left(colon)).toLower()] = QString::fromLatin1(line.mid(colon + 1).trimmed());
                }
                state->input.remove(0, end + 4); state->replied = true; ++requests;
                if (path.startsWith("/ws") || path == "/fragment" || path == "/oversize" || path == "/malformed" || path == "/flood" || path == "/maxmessage" || path == "/peerclose" || path == "/peerclose-info") {
                    QByteArray nonce, protocol;
                    for (const auto& raw : lines) {
                        const auto line = raw.trimmed(); const auto colon = line.indexOf(':');
                        if (line.left(colon).toLower() == "sec-websocket-key") nonce = line.mid(colon + 1).trimmed();
                        if (line.left(colon).toLower() == "sec-websocket-protocol") protocol = line.mid(colon + 1).trimmed();
                    }
                    auto accept = QCryptographicHash::hash(nonce + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", QCryptographicHash::Sha1).toBase64();
                    socket->write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept + "\r\n" + (protocol.isEmpty() ? QByteArray{} : "Sec-WebSocket-Protocol: " + protocol + "\r\n") + "\r\n");
                    state->ws = true;
                    if (path == "/fragment") socket->write(frame("frag", 1, false) + frame("ping", 9) + frame("mented", 0, true) + frame(QByteArray::fromHex("00ff41"), 2));
                    if (path == "/maxmessage") socket->write(frame(QByteArray(8 * 1024 * 1024, 'w'), 2));
                    if (path == "/oversize") socket->write(frame(QByteArray(4096, 'x')));
                    if (path == "/malformed") socket->write(QByteArray::fromHex("830178")); // reserved opcode 3
                    if (path == "/flood") { QByteArray batch; for (int i = 0; i < 1000; ++i) batch += frame("bounded"); socket->write(batch); }
                    if (path == "/peerclose") socket->write(frame(QByteArray::fromHex("03e8"), 8));
                    if (path == "/peerclose-info") socket->write(frame(QByteArray::fromHex("0fa1") + QString::fromUtf8("令牌已过期").toUtf8(), 8));
                } else {
                    if (path == "/slow") return;
                    int status = path == "/401" ? 401 : path == "/500" ? 500 : 200;
                    QByteArray body = path == "/maxbody" ? QByteArray(8 * 1024 * 1024, 'h') : path == "/large" ? QByteArray(65536, 'x') : QByteArray(status == 200 ? "{\"ok\":true,\"value\":42}" : "{\"error\":\"retained\"}");
                    QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + " Result\r\nContent-Type: application/json\r\nX-Test: present\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n";
                    if (path == "/headers") response += "X-Large: " + QByteArray(20000, 'x') + "\r\n";
                    socket->write(response + "\r\n" + body); socket->disconnectFromHost(); return;
                }
            }
            while (state->ws && state->input.size() >= 2) {
                const auto* bytes = reinterpret_cast<const unsigned char*>(state->input.constData());
                const int opcode = bytes[0] & 15; const bool final = bytes[0] & 128, masked = bytes[1] & 128;
                quint64 size = bytes[1] & 127; int offset = 2;
                if (size == 126) { if (state->input.size() < 4) return; size = (bytes[2] << 8) | bytes[3]; offset = 4; }
                else if (size == 127) { if (state->input.size() < 10) return; size = 0; for (int i = 2; i < 10; ++i) size = (size << 8) | bytes[i]; offset = 10; }
                if (size > 8 * 1024 * 1024 || state->input.size() < qsizetype(offset + (masked ? 4 : 0) + size)) return;
                QByteArray mask = masked ? state->input.mid(offset, 4) : QByteArray{}; offset += masked ? 4 : 0;
                QByteArray payload = state->input.mid(offset, qsizetype(size));
                if (masked) for (qsizetype i = 0; i < payload.size(); ++i) payload[i] = payload[i] ^ mask[i % 4];
                state->input.remove(0, offset + qsizetype(size));
                if (opcode == 8) { socket->write(frame(payload, 8)); socket->disconnectFromHost(); return; }
                if (opcode == 9) { socket->write(frame(payload, 10)); continue; }
                if (opcode == 10) { ++pongs; continue; }
                if (opcode == 1 || opcode == 2) state->opcode = opcode;
                state->message += payload;
                if (final) { socket->write(frame(state->message, state->opcode)); state->message.clear(); }
            }
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
};
QJsonObject params(const QString& url, int timeout = 2000) { return {{"url", url}, {"timeoutMs", timeout}, {"connectTimeoutMs", std::min(timeout, 1000)}}; }
QJsonObject response(const QSignalSpy& spy, int index = 0) { return spy[index][1].toJsonObject(); }
QString error(const QSignalSpy& spy, int index = 0) { return spy[index][2].toString(); }
}

class ProtocolTest : public QObject {
    Q_OBJECT
    QTemporaryDir certificateDir;
    QSslCertificate cert;
    QSslKey key;
    QString ca;
private slots:
    void initTestCase() {
        QString executable = qEnvironmentVariable("PORTBRIDGE_TEST_OPENSSL");
        if (executable.isEmpty()) executable = QStandardPaths::findExecutable("openssl");
#ifdef Q_OS_WIN
        if (executable.isEmpty()) executable = qEnvironmentVariable("ProgramFiles") + "/Git/mingw64/bin/openssl.exe";
#endif
        QVERIFY2(QFile::exists(executable), "OpenSSL CLI is required only to generate isolated test certificates");
        QProcess generator;
        ca = certificateDir.filePath("ca.pem"); const auto keyPath = certificateDir.filePath("key.pem");
        generator.start(executable, {"req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", keyPath, "-out", ca, "-days", "2", "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost", "-addext", "basicConstraints=critical,CA:TRUE"});
        QVERIFY(generator.waitForFinished(10000)); QCOMPARE(generator.exitCode(), 0);
        QFile certFile(ca); QVERIFY(certFile.open(QIODevice::ReadOnly)); cert = QSslCertificate(certFile.readAll(), QSsl::Pem);
        QFile keyFile(keyPath); QVERIFY(keyFile.open(QIODevice::ReadOnly)); key = QSslKey(keyFile.readAll(), QSsl::Rsa, QSsl::Pem);
        QVERIFY(!cert.isNull()); QVERIFY(!key.isNull());
        QVERIFY2(QSslSocket::supportsSsl(), qPrintable(QSslSocket::availableBackends().join(',')));
    }
    void httpStatusAndBody() {
        LocalServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        WorkflowProtocolClient client;
        connect(&client, &WorkflowProtocolClient::operationFinished, &client, [] { QCOMPARE(QThread::currentThread(), qApp->thread()); }, Qt::DirectConnection);
        QSignalSpy done(&client, &WorkflowProtocolClient::operationFinished);
        for (int status : {200, 401, 500}) {
            done.clear(); auto p = params(server.url("/" + QString::number(status)));
            p["headers"] = "Content-Type: application/json\nAuthorization: Bearer test-only";
            client.httpRequest(QString::number(status), p);
            QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 4000); QVERIFY2(error(done).isEmpty(), qPrintable(error(done)));
            QCOMPARE(response(done)["status"].toInt(), status); QVERIFY(response(done)["body"].isObject());
            QCOMPARE(response(done)["headers"].toObject()["x-test"].toString(), QString("present"));
            QVERIFY(!response(done)["bodyBase64"].toString().isEmpty());
            QCOMPARE(server.lastHeaders["authorization"].toString(), QString("Bearer test-only"));
        }
    }
    void httpCapsDeadlinesCancelAndReuse() {
        LocalServer server; QVERIFY(server.listen(QHostAddress::LocalHost)); WorkflowProtocolClient client;
        QSignalSpy done(&client, &WorkflowProtocolClient::operationFinished);
        auto p = params(server.url("/large")); p["maxResponseBytes"] = 1024; client.httpRequest("cap", p);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 4000); QVERIFY(error(done).contains("limit")); QVERIFY(response(done).isEmpty());
        done.clear(); client.httpRequest("headers", params(server.url("/headers")));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 4000); QVERIFY(error(done).contains("limit"));
        done.clear(); QElapsedTimer elapsed; elapsed.start(); client.httpRequest("deadline", params(server.url("/slow"), 100));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 1500); QVERIFY(error(done).contains("timed out")); QVERIFY(elapsed.elapsed() < 1000);
        done.clear(); client.httpRequest("retired", params(server.url("/slow"), 5000)); QTest::qWait(30);
        elapsed.restart(); client.cancelAll(); QVERIFY(elapsed.elapsed() < 100);
        client.httpRequest("reuse", params(server.url("/200"))); QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 4000);
        QCOMPARE(done[0][0].toString(), QString("reuse")); QVERIFY(error(done).isEmpty()); QTest::qWait(150); QCOMPARE(done.size(), 1);
        auto owned = std::make_unique<WorkflowProtocolClient>(); owned->httpRequest("destroy", params(server.url("/slow"), 5000)); QTest::qWait(30);
        elapsed.restart(); owned.reset(); QVERIFY(elapsed.elapsed() < 100);
    }
    void webSocketTextBinaryFragmentAndClose() {
        LocalServer server; QVERIFY(server.listen(QHostAddress::LocalHost)); WorkflowProtocolClient client;
        connect(&client, &WorkflowProtocolClient::webSocketMessage, &client, [] { QCOMPARE(QThread::currentThread(), qApp->thread()); }, Qt::DirectConnection);
        connect(&client, &WorkflowProtocolClient::stateChanged, &client, [] { QCOMPARE(QThread::currentThread(), qApp->thread()); }, Qt::DirectConnection);
        QSignalSpy done(&client, &WorkflowProtocolClient::operationFinished), messages(&client, &WorkflowProtocolClient::webSocketMessage);
        auto p = params(server.url("/fragment", "ws")); p["subprotocol"] = "test"; client.connectWebSocket("open", p);
        QTRY_VERIFY_WITH_TIMEOUT(!done.empty(), 4000); QVERIFY2(error(done).isEmpty(), qPrintable(error(done))); QVERIFY(client.webSocketConnected());
        QTRY_COMPARE_WITH_TIMEOUT(messages.size(), 2, 4000); QCOMPARE(messages[0][0].toByteArray(), QByteArray("fragmented")); QCOMPARE(messages[0][1].toBool(), false);
        QCOMPARE(messages[1][0].toByteArray(), QByteArray::fromHex("00ff41")); QCOMPARE(messages[1][1].toBool(), true); QTRY_COMPARE(server.pongs, 1);
        done.clear(); messages.clear(); client.sendWebSocket("text", "hello", false);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 4000); QVERIFY(error(done).isEmpty()); QCOMPARE(response(done)["sentBytes"].toInt(), 5);
        QTRY_COMPARE_WITH_TIMEOUT(messages.size(), 1, 4000); QCOMPARE(messages[0][0].toByteArray(), QByteArray("hello")); QVERIFY(!messages[0][1].toBool());
        done.clear(); messages.clear(); client.sendWebSocket("binary", QByteArray::fromHex("00abff"), true);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 4000); QVERIFY(error(done).isEmpty()); QTRY_COMPARE_WITH_TIMEOUT(messages.size(), 1, 4000); QVERIFY(messages[0][1].toBool());
        done.clear(); QStringList order;
        connect(&client, &WorkflowProtocolClient::operationFinished, &client, [&](QString id, QJsonObject, QString) { if (id == "close") order << "finished"; });
        connect(&client, &WorkflowProtocolClient::stateChanged, &client, [&] { if (!client.webSocketConnected()) order << "state"; });
        client.closeWebSocket("close", 1000, "done"); QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 4000); QVERIFY2(error(done).isEmpty(), qPrintable(error(done)));
        QCOMPARE(response(done)["code"].toInt(), 1000); QCOMPARE(response(done)["peerCode"].toInt(), 1000); QCOMPARE(response(done)["peerReason"].toString(), QString("done")); QVERIFY(!client.webSocketConnected()); QCOMPARE(order, QStringList({"finished", "state"}));
    }
    void webSocketFailuresAndLimits() {
        LocalServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        for (const auto& route : {QString("/reject"), QString("/slow"), QString("/oversize"), QString("/malformed"), QString("/peerclose")}) {
            WorkflowProtocolClient client; QSignalSpy done(&client, &WorkflowProtocolClient::operationFinished), errors(&client, &WorkflowProtocolClient::protocolError);
            auto p = params(server.url(route, "ws"), 300); p["maxMessageBytes"] = 1024; client.connectWebSocket("open", p);
            QTRY_VERIFY_WITH_TIMEOUT(!done.empty(), 2500);
            if (route == "/reject" || route == "/slow") QVERIFY(!error(done).isEmpty());
            else { QVERIFY(error(done).isEmpty()); QTRY_VERIFY_WITH_TIMEOUT(!client.webSocketConnected(), 3000); if (route != "/peerclose") QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 1500); }
        }
    }
    void unsolicitedPeerCloseMetadataBeforeState() {
        LocalServer server; QVERIFY(server.listen(QHostAddress::LocalHost)); WorkflowProtocolClient client;
        QSignalSpy done(&client, &WorkflowProtocolClient::operationFinished), closed(&client, &WorkflowProtocolClient::webSocketClosed);
        QStringList order;
        connect(&client, &WorkflowProtocolClient::webSocketClosed, &client, [&](int code, const QString& reason, bool initiated) {
            QCOMPARE(QThread::currentThread(), qApp->thread()); QCOMPARE(code, 4001); QCOMPARE(reason, QString::fromUtf8("令牌已过期")); QVERIFY(initiated);
            QVERIFY(!client.webSocketConnected()); order << "closed";
        }, Qt::DirectConnection);
        connect(&client, &WorkflowProtocolClient::stateChanged, &client, [&] { if (!client.webSocketConnected()) order << "state"; });
        client.connectWebSocket("peer-close", params(server.url("/peerclose-info", "ws")));
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 3000); QCOMPARE(done.size(), 1); QVERIFY(error(done).isEmpty());
        QCOMPARE(closed[0][0].toInt(), 4001); QCOMPARE(closed[0][1].toString(), QString::fromUtf8("令牌已过期")); QVERIFY(closed[0][2].toBool());
        QCOMPARE(order, QStringList({"closed", "state"}));
    }
    void qtWebSocketCloseInterop_data() {
        QTest::addColumn<bool>("secure"); QTest::addColumn<bool>("peerInitiated"); QTest::addColumn<bool>("abortOnly");
        for (bool secure : {false, true}) for (bool peer : {false, true})
            QTest::newRow(qPrintable(QString("%1-%2").arg(secure ? "wss" : "ws", peer ? "peer" : "requested"))) << secure << peer << false;
        QTest::newRow("ws-abrupt") << false << false << true;
        QTest::newRow("wss-abrupt") << true << false << true;
    }
    void qtWebSocketCloseInterop() {
        QFETCH(bool, secure); QFETCH(bool, peerInitiated); QFETCH(bool, abortOnly);
        QWebSocketServer server("Actual Qt peer", secure ? QWebSocketServer::SecureMode : QWebSocketServer::NonSecureMode);
        if (secure) {
            auto configuration = QSslConfiguration::defaultConfiguration(); configuration.setLocalCertificate(cert); configuration.setPrivateKey(key);
            configuration.setPeerVerifyMode(QSslSocket::VerifyNone); server.setSslConfiguration(configuration);
        }
        QVERIFY(server.listen(QHostAddress::LocalHost)); QPointer<QWebSocket> peer;
        connect(&server, &QWebSocketServer::newConnection, this, [&] { peer = server.nextPendingConnection(); peer->setParent(&server); });
        WorkflowProtocolClient client; QSignalSpy done(&client, &WorkflowProtocolClient::operationFinished), closed(&client, &WorkflowProtocolClient::webSocketClosed), errors(&client, &WorkflowProtocolClient::protocolError);
        auto p = params(QString("%1://localhost:%2/events").arg(secure ? "wss" : "ws").arg(server.serverPort())); if (secure) p["caFile"] = ca;
        client.connectWebSocket("qt-open", p); QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 4000); QVERIFY2(error(done).isEmpty(), qPrintable(error(done))); QTRY_VERIFY(peer); QVERIFY(client.webSocketConnected());
        QStringList order;
        connect(&client, &WorkflowProtocolClient::webSocketClosed, &client, [&](int, const QString&, bool) { order << "closed"; });
        connect(&client, &WorkflowProtocolClient::stateChanged, &client, [&] { if (!client.webSocketConnected()) order << "state"; });
        const auto reason = QString::fromUtf8("订阅令牌失效");
        if (abortOnly) {
            peer->abort(); QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 4000); QCOMPARE(closed.size(), 0); QVERIFY(!client.webSocketConnected()); return;
        }
        if (peerInitiated) peer->close(static_cast<QWebSocketProtocol::CloseCode>(4001), reason);
        else client.closeWebSocket("qt-close", 1000, reason);
        QTRY_VERIFY_WITH_TIMEOUT(!closed.isEmpty() || !errors.isEmpty(), 5000);
        QVERIFY2(closed.size() == 1, errors.isEmpty() ? "Missing close metadata" : qPrintable(errors[0][0].toString()));
        QCOMPARE(closed[0][0].toInt(), peerInitiated ? 4001 : 1000); QCOMPARE(closed[0][1].toString(), reason); QCOMPARE(closed[0][2].toBool(), peerInitiated);
        QCOMPARE(errors.size(), 0); QCOMPARE(order, QStringList({"closed", "state"}));
        if (!peerInitiated) { QCOMPARE(done.size(), 2); QVERIFY2(error(done, 1).isEmpty(), qPrintable(error(done, 1))); QCOMPARE(response(done, 1)["peerCode"].toInt(), 1000); }
    }
    void boundedHandoffCancelAndReuse() {
        LocalServer server; QVERIFY(server.listen(QHostAddress::LocalHost)); WorkflowProtocolClient client;
        QSignalSpy done(&client, &WorkflowProtocolClient::operationFinished), messages(&client, &WorkflowProtocolClient::webSocketMessage);
        client.connectWebSocket("flood", params(server.url("/flood", "ws"))); QTRY_VERIFY_WITH_TIMEOUT(!done.empty(), 3000);
        // Deliberately stop pumping Qt while actual worker I/O remains active.
        QThread::msleep(150); QVERIFY(messages.size() < 32);
        client.cancelAll(); const auto retired = messages.size(); QTest::qWait(50); QCOMPARE(messages.size(), retired);
        done.clear(); client.connectWebSocket("reuse", params(server.url("/ws", "ws"))); QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 3000); QVERIFY(error(done).isEmpty());
        client.closeWebSocket("close"); QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, 3000);
    }
    void maximumHttpAndWebSocketPayloads() {
        LocalServer server; QVERIFY(server.listen(QHostAddress::LocalHost)); WorkflowProtocolClient client;
        QSignalSpy done(&client, &WorkflowProtocolClient::operationFinished), messages(&client, &WorkflowProtocolClient::webSocketMessage);
        client.connectWebSocket("maximum-ws", params(server.url("/maxmessage", "ws"), 5000));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 5000); QVERIFY(error(done).isEmpty());
        client.httpRequest("maximum-http", params(server.url("/maxbody"), 5000));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, 7000); QVERIFY2(error(done, 1).isEmpty(), qPrintable(error(done, 1)));
        QCOMPARE(response(done, 1)["bodyBytes"].toInt(), 8 * 1024 * 1024);
        QCOMPARE(QByteArray::fromBase64(response(done, 1)["bodyBase64"].toString().toLatin1()), QByteArray(8 * 1024 * 1024, 'h'));
        QTRY_COMPARE_WITH_TIMEOUT(messages.size(), 1, 5000); QCOMPARE(messages[0][0].toByteArray(), QByteArray(8 * 1024 * 1024, 'w')); QVERIFY(messages[0][1].toBool());
        client.cancelAll();
    }
    void cancellationAdmissionAndConfigurationBudgets() {
        LocalServer server; QVERIFY(server.listen(QHostAddress::LocalHost)); WorkflowProtocolClient client;
        QSignalSpy done(&client, &WorkflowProtocolClient::operationFinished);
        const auto pending = params(server.url("/slow"), 5000);
        for (int i = 0; i < 200; ++i) { client.httpRequest("retired" + QString::number(i), pending); client.cancelAll(); }
        client.httpRequest("reuse", params(server.url("/200")));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 3000); QCOMPARE(done[0][0].toString(), QString("reuse")); QVERIFY(error(done).isEmpty());
        done.clear(); client.connectWebSocket("retired-ws", params(server.url("/slow", "ws"), 5000)); QTest::qWait(50);
        QElapsedTimer elapsed; elapsed.start(); client.cancelAll(); QVERIFY(elapsed.elapsed() < 100);
        client.connectWebSocket("reuse-ws", params(server.url("/ws", "ws")));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 3000); QCOMPARE(done[0][0].toString(), QString("reuse-ws")); QVERIFY(error(done).isEmpty());
        done.clear(); client.sendWebSocket("retired-write", QByteArray(1024 * 1024, 'x'), true); client.cancelAll(); QTest::qWait(100); QVERIFY(done.isEmpty());
        auto owned = std::make_unique<WorkflowProtocolClient>(); owned->connectWebSocket("destroy-ws", params(server.url("/slow", "ws"), 5000)); QTest::qWait(30);
        elapsed.restart(); owned.reset(); QVERIFY(elapsed.elapsed() < 100);
        auto invalid = params(server.url("/200")); invalid["headers"] = "Bad header"; client.httpRequest("invalid-header", invalid);
        QCOMPARE(done.size(), 1); QVERIFY(!error(done).isEmpty());
        done.clear(); invalid = params(server.url("/200")); invalid["maxResponseBytes"] = 1e100; client.httpRequest("invalid-cap", invalid);
        QCOMPARE(done.size(), 1); QVERIFY(!error(done).isEmpty());
        done.clear(); QJsonArray body; for (int i = 0; i < 4097; ++i) body.append(i);
        invalid = params(server.url("/200")); invalid["body"] = body; client.httpRequest("invalid-json-budget", invalid);
        QCOMPARE(done.size(), 1); QVERIFY(error(done).contains("limit"));
    }
    void httpsAndWssTrustAndHostname() {
        LocalServer server; server.tls = true; server.certificate = cert; server.key = key; QVERIFY(server.listen(QHostAddress::LocalHost));
        for (bool ws : {false, true}) for (int mode : {0, 1, 2}) {
            WorkflowProtocolClient client; QSignalSpy done(&client, &WorkflowProtocolClient::operationFinished), messages(&client, &WorkflowProtocolClient::webSocketMessage);
            auto p = params(server.url(ws ? "/ws" : "/200", ws ? "ws" : "http", mode == 2 ? "127.0.0.1" : "localhost"));
            if (mode != 1) p["caFile"] = ca;
            if (ws) client.connectWebSocket("tls", p); else client.httpRequest("tls", p);
            QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 5000);
            if (mode == 0) {
                QVERIFY2(error(done).isEmpty(), qPrintable(error(done)));
                if (ws) { client.sendWebSocket("echo", "secure", false); QTRY_COMPARE_WITH_TIMEOUT(messages.size(), 1, 4000); QCOMPARE(messages[0][0].toByteArray(), QByteArray("secure")); client.closeWebSocket("close"); }
                else QCOMPARE(response(done)["status"].toInt(), 200);
            } else { QVERIFY2(!error(done).isEmpty(), "TLS accepted untrusted CA or wrong hostname"); QVERIFY(!client.webSocketConnected()); }
        }
    }
};
QTEST_GUILESS_MAIN(ProtocolTest)
#include "test_workflow_protocol.moc"
