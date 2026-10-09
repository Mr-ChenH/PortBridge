#include <QtTest>
#include "ui/protocol_preview.hpp"
#include "ui/protocol_debug_page.hpp"
#include "ui/main_window.hpp"
#include "ui/workflow_page.hpp"
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QTemporaryDir>
#include <QSettings>
#include <QStandardPaths>
#include <QPointer>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QUrlQuery>
#include <QListWidget>
#include <QTableWidget>
#include <QTableView>
#include <QTabWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QSplitter>
#include <QLabel>
#include <QMessageBox>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QDialog>
#include <QTimer>
#include <QSaveFile>

using namespace portbridge;
namespace {
struct HttpFixture {
    QTcpServer server;
    QVector<QJsonObject> requests;
    HttpFixture() {
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                auto bytes = std::make_shared<QByteArray>();
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, bytes] {
                    *bytes += socket->readAll();
                    const int end = bytes->indexOf("\r\n\r\n");
                    if (end < 0)
                        return;
                    const auto lines = bytes->left(end).split('\n');
                    const auto start = lines.front().trimmed().split(' ');
                    if (start.size() != 3)
                        return;
                    QJsonObject headers;
                    int length = 0;
                    for (int i = 1; i < lines.size(); ++i) {
                        const auto line = lines[i].trimmed();
                        const auto at = line.indexOf(':');
                        if (at < 0)
                            continue;
                        auto k = line.left(at).toLower();
                        auto v = line.mid(at + 1).trimmed();
                        headers[QString::fromLatin1(k)] = QString::fromUtf8(v);
                        if (k == "content-length")
                            length = v.toInt();
                    }
                    if (bytes->size() < end + 4 + length)
                        return;
                    const auto payload = bytes->mid(end + 4, length);
                    const QJsonObject request{{"method", QString::fromLatin1(start[0])},
                                              {"uri", QString::fromLatin1(start[1])},
                                              {"headers", headers},
                                              {"body", QString::fromUtf8(payload)},
                                              {"bodyBase64", QString::fromLatin1(payload.toBase64())}};
                    requests.append(request);
                    socket->disconnect(socket, &QTcpSocket::readyRead, nullptr, nullptr);
                    const auto uri = start[1];
                    int code = uri.startsWith("/unauthorized") ? 401
                               : uri.startsWith("/redirect")   ? 302
                                                               : 200;
                    QByteArray result =
                        uri.startsWith("/large") ? QByteArray(4096, 'x')
                        : uri.startsWith("/plain")
                            ? QByteArray("UTF-8 中文🙂 ") + QByteArray(10000, 'x') + "payload-tail"
                            : QJsonDocument(
                                  QJsonObject{{"echo", request}, {"token", "response-private-secret"}})
                                  .toJson(QJsonDocument::Compact);
                    const auto reply =
                        QByteArray("HTTP/1.1 ") + QByteArray::number(code) +
                        " Result\r\nConnection: close\r\nContent-Type: " +
                        (uri.startsWith("/plain") || uri.startsWith("/large") ? "text/plain"
                                                                              : "application/json") +
                        "\r\nSet-Cookie: private_cookie=cookie-private-value\r\n" +
                        (code == 302 ? "Location: /followed\r\n" : "") +
                        "Content-Length: " + QByteArray::number(result.size()) + "\r\n\r\n" + result;
                    auto write = [socket, reply] {
                        socket->write(reply);
                        socket->disconnectFromHost();
                    };
                    if (uri.startsWith("/late"))
                        QTimer::singleShot(500, socket, write);
                    else
                        write();
                });
            }
        });
        server.listen(QHostAddress::LocalHost, 0);
    }
    QString url(const QString &path = "/echo") const {
        return QString("http://127.0.0.1:%1%2").arg(server.serverPort()).arg(path);
    }
};
struct WsFixture {
    QWebSocketServer server{QStringLiteral("real-manual-test"), QWebSocketServer::NonSecureMode};
    QPointer<QWebSocket> peer;
    int accepts = 0;
    QList<QByteArray> binary;
    QStringList texts;
    WsFixture() {
        QObject::connect(&server, &QWebSocketServer::newConnection, &server, [this] {
            ++accepts;
            peer = server.nextPendingConnection();
            auto *p = peer.data();
            QObject::connect(p, &QWebSocket::textMessageReceived, p, [this, p](const QString &value) {
                texts.append(value);
                p->sendTextMessage(value);
            });
            QObject::connect(p, &QWebSocket::binaryMessageReceived, p, [this, p](const QByteArray &value) {
                binary.append(value);
                p->sendBinaryMessage(value);
            });
            QObject::connect(p, &QWebSocket::disconnected, p, &QObject::deleteLater);
            p->sendTextMessage(QStringLiteral("主动推送"));
        });
        server.listen(QHostAddress::LocalHost, 0);
    }
    QString url() const { return QString("ws://127.0.0.1:%1/").arg(server.serverPort()); }
};
template <class T> T *widget(QWidget &root, const char *name) {
    auto *found = root.findChild<T *>(name);
    if (!found)
        qFatal("Missing widget: %s", name);
    return found;
}
void url(QWidget &root, const QString &value) {
    widget<QLineEdit>(root, "protocolUrl")->setText(value);
}
void row(QTableWidget *table, const QString &key, const QString &value, bool enabled = true) {
    const int at = table->rowCount();
    table->insertRow(at);
    auto *check = new QTableWidgetItem;
    check->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    check->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
    table->setItem(at, 0, check);
    table->setItem(at, 1, new QTableWidgetItem(key));
    table->setItem(at, 2, new QTableWidgetItem(value));
}
std::unique_ptr<QTimer> answer(const QString &name, QMessageBox::ButtonRole role,
                               std::function<void()> before = {}) {
    auto timer = std::make_unique<QTimer>();
    timer->setInterval(10);
    auto *raw = timer.get();
    QObject::connect(raw, &QTimer::timeout, raw, [raw, name, role, before] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box || box->objectName() != name)
            return;
        raw->stop();
        if (before)
            before();
        for (auto *button : box->buttons())
            if (box->buttonRole(button) == role) {
                button->click();
                break;
            }
    });
    raw->start();
    return timer;
}
QJsonObject parameters(const QString &value) {
    return {{"url", value},
            {"method", "GET"},
            {"timeoutMs", 1500},
            {"connectTimeoutMs", 1000},
            {"maxResponseBytes", 1024 * 1024},
            {"maxMessageBytes", 1024 * 1024}};
}
int count(const ProtocolDebugSession &session, const QString &direction) {
    int n = 0;
    for (const auto &e : session.entries())
        if (e.direction == direction)
            ++n;
    return n;
}
} // namespace
class ProtocolDebugTest : public QObject {
    Q_OBJECT
    QTemporaryDir directory;
  private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("PortBridgeManualTests");
        QCoreApplication::setApplicationName("ManualProtocols");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        QStandardPaths::setTestModeEnabled(true);
    }
    void init() {
        QSettings settings;
        settings.clear();
        settings.setValue("storage/directory",
                          directory.filePath(QString::fromLatin1(QTest::currentTestFunction())));
    }
    void noFloatingControlsCoverTheEditor() {
        for (const auto mode : {ProtocolDebugSession::Mode::Http, ProtocolDebugSession::Mode::WebSocket}) {
            ProtocolDebugPage page(mode, nullptr);
            page.resize(1180, 900);
            page.show();
            QTest::qWait(60);
            int visible = 0;
            for (auto *child : page.findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly))
                if (child->isVisible())
                    ++visible;
            QCOMPARE(visible, 2);
            auto *primary = widget<QPushButton>(page, "protocolPrimary");
            QCOMPARE(page.childAt(primary->mapTo(&page, primary->rect().center())),
                     static_cast<QWidget *>(primary));
            if (mode == ProtocolDebugSession::Mode::Http) {
                auto *method = widget<QComboBox>(page, "protocolMethod");
                QCOMPARE(page.childAt(method->mapTo(&page, method->rect().center())),
                         static_cast<QWidget *>(method));
            }
        }
    }
    void previewBoundaryMasksPartialKnownSecret() {
        workflowUi::SecretSamples secrets;
        workflowUi::appendSecret(QString(512, 's'), secrets);
        const QString source = QString(32760, 'x') + QString(512, 's') + "tail";
        const auto preview = workflowUi::previewValue(QJsonObject{{"echo", source}}, secrets);
        QVERIFY(preview.contains(QStringLiteral("已遮蔽")));
        QVERIFY(!preview.contains(QString(4, 's')));
        QVERIFY(preview.size() < 33024);
    }
    void unifiedCreationDialog_data() {
        QTest::addColumn<int>("kind");
        QTest::addColumn<bool>("dark");
        QTest::newRow("http-dark") << 4 << true;
        QTest::newRow("http-light") << 4 << false;
        QTest::newRow("websocket-dark") << 5 << true;
        QTest::newRow("websocket-light") << 5 << false;
    }
    void unifiedCreationDialog() {
        QFETCH(int, kind);
        QFETCH(bool, dark);
        HttpFixture http;
        WsFixture ws;
        SessionController raw;
        MainWindow w(&raw);
        w.show();
        if (w.property("darkTheme").toBool() != dark)
            widget<QPushButton>(w, "themeButton")->click();
        const int rawCount = widget<QListWidget>(w, "profileList")->count();
        auto *page = w.findChild<ProtocolDebugPage *>(kind == 4 ? "httpDebugPage" : "webSocketDebugPage");
        QVERIFY(page);
        const int saved = widget<QListWidget>(*page, "protocolLibrary")->count();
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            auto *choice = widget<QComboBox>(*dialog, "profileKind");
            QCOMPARE(choice->count(), 6);
            auto *card = widget<QPushButton>(*dialog, kind == 4 ? "profileType4" : "profileType5");
            card->click();
            QVERIFY(card->isChecked());
            card->click();
            QVERIFY(card->isChecked());
            auto *name = widget<QLineEdit>(*dialog, "profileName");
            name->setText(kind == 4 ? "HTTP health" : "WS realtime");
            auto *url = widget<QLineEdit>(*dialog, "profileProtocolUrl");
            QVERIFY(url->isVisible());
            url->setText("invalid-url");
            auto *save = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save);
            save->click();
            QVERIFY(dialog->isVisible());
            QVERIFY(widget<QLabel>(*dialog, "profileDialogError")->isVisible());
            url->setText(kind == 4 ? http.url() : ws.url());
            QTest::qWait(60);
            for (int i = 0; i < 6; ++i) {
                auto *type =
                    widget<QPushButton>(*dialog, QString("profileType%1").arg(i).toUtf8().constData());
                QVERIFY(type->height()>=82);
                QVERIFY(dialog->rect().contains(QRect(type->mapTo(dialog, QPoint()), type->size())));
            }
            QVERIFY(dialog->rect().contains(QRect(save->mapTo(dialog, QPoint()), save->size())));
            const auto folder = qEnvironmentVariable("PORTBRIDGE_MANUAL_SCREENSHOT_DIR");
            if (!folder.isEmpty()) {
                QDir().mkpath(folder);
                dialog->grab().save(folder + QString("/create-%1-%2.png")
                                                 .arg(kind)
                                                 .arg(w.property("darkTheme").toBool() ? "dark" : "light"));
            }
            save->click();
        });
        widget<QPushButton>(w, "createConnection")->click();
        QCOMPARE(widget<QListWidget>(w, "profileList")->count(), rawCount);
        QCOMPARE(widget<QListWidget>(*page, "protocolLibrary")->count(), saved + 1);
        QVERIFY(page->isVisible());
        QVERIFY(!page->dirty());
        QVERIFY(!page->session()->active());
        QVERIFY(!raw.connected());
        QCOMPARE(http.requests.size(), 0);
        QCOMPARE(ws.accepts, 0);
    }
    void creationPreservesDirtyDraftAndActiveSession() {
        HttpFixture http;
        WsFixture ws;
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, nullptr);
        url(page, http.url());
        widget<QLineEdit>(page, "protocolName")->setText("draft");
        QVERIFY(page.dirty());
        QString error;
        QVERIFY(!page.createSavedRequest("new", "bad-url", &error));
        QCOMPARE(widget<QLineEdit>(page, "protocolName")->text(), QString("draft"));
        QTimer::singleShot(0, [] {
            auto *confirmation = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY(confirmation);
            confirmation->done(QMessageBox::No);
        });
        QVERIFY(!page.createSavedRequest("new", http.url(), &error));
        QCOMPARE(widget<QLineEdit>(page, "protocolName")->text(), QString("draft"));
        QCOMPARE(http.requests.size(), 0);
        ProtocolDebugPage socket(ProtocolDebugSession::Mode::WebSocket, nullptr);
        url(socket, ws.url());
        socket.triggerSend();
        QTRY_VERIFY(socket.session()->connected());
        const auto epoch = socket.session()->epoch();
        QVERIFY(!socket.createSavedRequest("new", ws.url(), &error));
        QVERIFY(socket.session()->connected());
        QCOMPARE(socket.session()->epoch(), epoch);
        QCOMPARE(ws.accepts, 1);
    }
    void silentModesAndDraftLoading() {
        HttpFixture http;
        WsFixture ws;
        SessionController c;
        MainWindow w(&c);
        w.show();
        auto *h = widget<ProtocolDebugPage>(w, "httpDebugPage");
        auto *s = widget<ProtocolDebugPage>(w, "webSocketDebugPage");
        widget<QPushButton>(w, "httpWorkspaceMode")->click();
        url(*h, http.url());
        widget<QPushButton>(w, "webSocketWorkspaceMode")->click();
        url(*s, ws.url());
        auto *prepared = widget<QPlainTextEdit>(*s, "protocolMessage");
        QVERIFY(prepared->isEnabled());
        prepared->setFocus();
        QTest::keyClicks(prepared, "prepared-offline");
        QCOMPARE(prepared->toPlainText(), QString("prepared-offline"));
        widget<QPushButton>(w, "workflowNavigation")->click();
        widget<QPushButton>(w, "workspaceNavigation")->click();
        QTest::qWait(200);
        QCOMPARE(http.requests.size(), 0);
        QCOMPARE(ws.accepts, 0);
        QVERIFY(!h->session()->active());
        QVERIFY(!s->session()->active());
        QVERIFY(!c.connected());
        QString error;
        QVERIFY(h->loadDraft({{"schemaVersion", 1}, {"kind", "http"}, {"params", parameters(http.url())}},
                             &error));
        QTest::qWait(100);
        QCOMPARE(http.requests.size(), 0);
    }
    void controllerHttp401And302() {
        HttpFixture server;
        ProtocolDebugSession s(ProtocolDebugSession::Mode::Http);
        QString error;
        QVERIFY(s.start(parameters(server.url("/unauthorized")), &error));
        QTRY_VERIFY_WITH_TIMEOUT(!s.active(), 3000);
        QCOMPARE(s.latestResponse().value("status").toInt(), 401);
        QVERIFY(s.lastError().isEmpty());
        QVERIFY(s.latestResponse().value("headers").toObject().contains("set-cookie"));
        QVERIFY(s.start(parameters(server.url("/redirect")), &error));
        QTRY_VERIFY_WITH_TIMEOUT(!s.active(), 3000);
        QCOMPARE(s.latestResponse().value("status").toInt(), 302);
        QCOMPARE(server.requests.size(), 2);
    }
    void validationPrecedesGuard() {
        ProtocolDebugSession s(ProtocolDebugSession::Mode::Http);
        int called = 0;
        s.setStartGuard([&](QString *) {
            ++called;
            return true;
        });
        QString error;
        auto p = parameters("ws://127.0.0.1/");
        QVERIFY(!s.start(p, &error));
        p = parameters("http://127.0.0.1/");
        p["headers"] = QJsonObject{{"x-test", "value\r\nx-injected: yes"}};
        QVERIFY(!s.start(p, &error));
        p["headers"] = QJsonObject{{"Host", "alternate"}};
        QVERIFY(!s.start(p, &error));
        p["headers"] = QJsonObject{};
        p["timeoutMs"] = 60001;
        QVERIFY(!s.start(p, &error));
        p["headers"] = QJsonObject{{"Transfer-Encoding", "chunked"}};
        QVERIFY(!s.start(p, &error));
        p["headers"] = QJsonObject{{"Sec-WebSocket-Extensions", "unknown"}};
        QVERIFY(!s.start(p, &error));
        p["headers"] = QJsonObject{};
        p["url"] = "http://127.0.0.1/#fragment";
        QVERIFY(!s.start(p, &error));
        p["url"] = "http://127.0.0.1/";
        p["maxResponseBytes"] = 1024.5;
        QVERIFY(!s.start(p, &error));
        QCOMPARE(called, 0);
        QVERIFY(!s.active());
    }
    void cancelIsolatesLateHttpCallbacks() {
        HttpFixture server;
        ProtocolDebugSession s(ProtocolDebugSession::Mode::Http);
        QVERIFY(s.start(parameters(server.url("/late"))));
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 1, 3000);
        const auto old = s.epoch();
        s.cancel();
        QVERIFY(s.epoch() > old);
        QVERIFY(s.start(parameters(server.url("/echo"))));
        QTRY_VERIFY_WITH_TIMEOUT(!s.active(), 3000);
        const auto id = s.latestOperation();
        const auto result = s.latestResponse();
        QTest::qWait(700);
        QCOMPARE(s.latestOperation(), id);
        QCOMPARE(s.latestResponse(), result);
        QCOMPARE(count(s, "HTTP"), 1);
    }
    void allHttpMethodsReachTheServer() {
        HttpFixture server;
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, nullptr);
        url(page, server.url());
        QStringList methods{"GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS"};
        for (const auto &method : methods) {
            widget<QComboBox>(page, "protocolMethod")->setCurrentText(method);
            widget<QComboBox>(page, "protocolBodyKind")->setCurrentIndex(method == "HEAD" ? 0 : 1);
            widget<QPlainTextEdit>(page, "protocolRequestBody")->setPlainText("method-payload");
            page.triggerSend();
            QTRY_VERIFY_WITH_TIMEOUT(!page.session()->active(), 3000);
            QCOMPARE(server.requests.last().value("method").toString(), method);
            QCOMPARE(server.requests.last().value("body").toString(),
                     method == "HEAD" ? QString() : QString("method-payload"));
            QCOMPARE(page.session()->latestResponse().value("status").toInt(), 200);
            if (method == "HEAD")
                QCOMPARE(page.session()->latestResponse().value("bodyBytes").toInt(), 0);
        }
        QCOMPARE(server.requests.size(), 7);
    }
    void importedBoundsRejectBeforeMutation() {
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, nullptr);
        url(page, "http://127.0.0.1:8080/original");
        QString error;
        auto doc = QJsonObject{{"schemaVersion", 1},
                               {"kind", "http"},
                               {"params", parameters("http://127.0.0.1:8081/replacement")},
                               {"editor", QJsonObject{{"body", QString(1024 * 1024 + 1, 'x')}}}};
        QVERIFY(!page.loadDraft(doc, &error));
        QCOMPARE(widget<QLineEdit>(page, "protocolUrl")->text(), "http://127.0.0.1:8080/original");
        doc.remove("editor");
        auto p = doc["params"].toObject();
        p["method"] = "INVALID";
        doc["params"] = p;
        QVERIFY(!page.loadDraft(doc, &error));
        p["method"] = "GET";
        p["maxResponseBytes"] = 1025;
        doc["params"] = p;
        QVERIFY(!page.loadDraft(doc, &error));
        QCOMPARE(widget<QLineEdit>(page, "protocolUrl")->text(), "http://127.0.0.1:8080/original");
    }
    void connectedWidgetDestructionReleasesPeer() {
        WsFixture server;
        auto page = std::make_unique<ProtocolDebugPage>(ProtocolDebugSession::Mode::WebSocket, nullptr);
        page->show();
        url(*page, server.url());
        page->triggerSend();
        QTRY_VERIFY_WITH_TIMEOUT(page->session()->connected(), 3000);
        QPointer<QWebSocket> peer = server.peer;
        QSignalSpy disconnected(peer, &QWebSocket::disconnected);
        page.reset();
        QTRY_VERIFY_WITH_TIMEOUT(disconnected.size() > 0 || peer.isNull(), 3000);
    }
    void capacityFailureAndRecovery() {
        HttpFixture server;
        ProtocolDebugSession s(ProtocolDebugSession::Mode::Http);
        auto p = parameters(server.url("/large"));
        p["maxResponseBytes"] = 1024;
        QVERIFY(s.start(p));
        QTRY_VERIFY_WITH_TIMEOUT(!s.active(), 3000);
        QVERIFY(!s.lastError().isEmpty());
        QVERIFY(s.latestResponse().isEmpty());
        QVERIFY(s.start(parameters(server.url())));
        QTRY_VERIFY_WITH_TIMEOUT(!s.active(), 3000);
        QCOMPARE(s.latestResponse().value("status").toInt(), 200);
        QVERIFY(s.lastError().isEmpty());
    }
    void actualHttpEditorSemantics() {
        HttpFixture server;
        QSettings settings(directory.filePath("editor.ini"), QSettings::IniFormat);
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, &settings);
        page.resize(1180, 800);
        page.show();
        url(page, server.url("/echo?existing=1"));
        widget<QComboBox>(page, "protocolMethod")->setCurrentText("POST");
        row(widget<QTableWidget>(page, "protocolQuery"), "query", QStringLiteral("中文 &+🙂"));
        row(widget<QTableWidget>(page, "protocolQuery"), "disabled", "unused", false);
        row(widget<QTableWidget>(page, "protocolHeaders"), "x-manual", "value");
        widget<QComboBox>(page, "protocolAuthKind")->setCurrentIndex(2);
        widget<QLineEdit>(page, "protocolUsername")->setText("user");
        widget<QLineEdit>(page, "protocolPassword")->setText(QStringLiteral("密码"));
        widget<QComboBox>(page, "protocolBodyKind")->setCurrentIndex(2);
        const auto body = QStringLiteral("{\"message\":\"中文🙂\"}");
        widget<QPlainTextEdit>(page, "protocolRequestBody")->setPlainText(body);
        page.triggerSend();
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 1, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!page.session()->active(), 3000);
        const auto r = server.requests.front();
        QCOMPARE(r.value("method").toString(), "POST");
        QCOMPARE(r.value("body").toString(), body);
        QVERIFY(r.value("uri").toString().contains("%2B"));
        QUrlQuery query(QUrl("http://localhost" + r.value("uri").toString()));
        QCOMPARE(query.queryItemValue("query", QUrl::FullyDecoded), QStringLiteral("中文 &+🙂"));
        QCOMPARE(query.queryItemValue("existing"), "1");
        QVERIFY(!query.hasQueryItem("disabled"));
        QCOMPARE(r.value("headers").toObject().value("authorization").toString(),
                 "Basic " + QString::fromLatin1(QStringLiteral("user:密码").toUtf8().toBase64()));
        QCOMPARE(r.value("headers").toObject().value("x-manual").toString(), "value");
        QTRY_VERIFY(widget<QPlainTextEdit>(page, "protocolResponseHeaders")
                        ->toPlainText()
                        .contains(QStringLiteral("已遮蔽")));
        QVERIFY(!widget<QPlainTextEdit>(page, "protocolResponseHeaders")
                     ->toPlainText()
                     .contains("cookie-private-value"));
    }
    void urlEncodedBodyAndDuplicateHeaders() {
        HttpFixture server;
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, nullptr);
        url(page, server.url());
        widget<QComboBox>(page, "protocolMethod")->setCurrentText("POST");
        widget<QComboBox>(page, "protocolBodyKind")->setCurrentIndex(3);
        row(widget<QTableWidget>(page, "protocolFormBody"), "a b", "x+y&中文");
        page.triggerSend();
        QTRY_VERIFY_WITH_TIMEOUT(!page.session()->active(), 3000);
        QCOMPARE(server.requests.size(), 1);
        QCOMPARE(server.requests.front().value("body").toString(), "a+b=x%2By%26%E4%B8%AD%E6%96%87");
        row(widget<QTableWidget>(page, "protocolHeaders"), "X-Duplicate", "a");
        row(widget<QTableWidget>(page, "protocolHeaders"), "x-duplicate", "b");
        page.triggerSend();
        QTest::qWait(150);
        QCOMPARE(server.requests.size(), 1);
        QVERIFY(widget<QLabel>(page, "protocolWarning")->text().contains(QStringLiteral("重复")));
    }
    void schemeRoundTripPreservesQueryEncoding() {
        HttpFixture server;
        ProtocolDebugPage original(ProtocolDebugSession::Mode::Http, nullptr);
        url(original, server.url("/echo?a=A%2BB&literalPlus=A+B&encoded=%252F"));
        QString error;
        const auto doc = original.exportDraft(&error);
        QVERIFY(error.isEmpty());
        ProtocolDebugPage restored(ProtocolDebugSession::Mode::Http, nullptr);
        QVERIFY(restored.loadDraft(doc, &error));
        restored.triggerSend();
        QTRY_VERIFY_WITH_TIMEOUT(!restored.session()->active(), 3000);
        QCOMPARE(server.requests.first().value("uri").toString(),
                 "/echo?a=A%2BB&literalPlus=A+B&encoded=%252F");
    }
    void savedDraftMasksCredentialsAndRestores() {
        HttpFixture server;
        QSettings settings(directory.filePath("saved.ini"), QSettings::IniFormat);
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, &settings);
        url(page, server.url("/echo?token=url-private"));
        widget<QComboBox>(page, "protocolAuthKind")->setCurrentIndex(1);
        widget<QLineEdit>(page, "protocolBearer")->setText("bearer-private");
        widget<QLineEdit>(page, "protocolName")->setText("示例方案");
        row(widget<QTableWidget>(page, "protocolHeaders"), "x-api-key", "header-private");
        row(widget<QTableWidget>(page, "protocolQuery"), "password", "query-private");
        widget<QComboBox>(page, "protocolBodyKind")->setCurrentIndex(2);
        widget<QPlainTextEdit>(page, "protocolRequestBody")
            ->setPlainText("{\"token\":12345,\"message\":\"hello\"}");
        QString error;
        QVERIFY2(page.saveDraft(&error), qPrintable(error));
        const auto doc = page.exportDraft(&error);
        const auto bytes = QJsonDocument(doc).toJson();
        for (const auto &secret :
             {"url-private", "bearer-private", "header-private", "query-private", "12345"}) {
            QVERIFY(!bytes.contains(secret));
            QVERIFY(!settings.value("manual/httpProjectsV2").toByteArray().isEmpty());
            QVERIFY(!settings.value("manual/httpProjectsV2").toByteArray().contains(secret));
        }
        QCOMPARE(widget<QLineEdit>(page, "protocolBearer")->text(), "bearer-private");
        ProtocolDebugPage restored(ProtocolDebugSession::Mode::Http, &settings);
        QCOMPARE(widget<QListWidget>(restored, "protocolLibrary")->count(), 1);
        QVERIFY(restored.loadDraft(doc, &error));
        restored.triggerSend();
        QTest::qWait(100);
        QCOMPARE(server.requests.size(), 0);
        QVERIFY(widget<QLabel>(restored, "protocolWarning")->text().contains(QStringLiteral("遮蔽")));
        QVERIFY(!restored.loadDraft(
            {{"schemaVersion", 99}, {"kind", "http"}, {"params", parameters(server.url())}}, &error));
    }
    void credentialViewsAndClipboard_data() {
        QTest::addColumn<QString>("requestBody");
        QTest::addColumn<QString>("secret");
        QTest::newRow("string") << QString("{\"token\":\"typed-private-value\"}")
                                << QString("typed-private-value");
        QTest::newRow("number") << QString("{\"token\":12671}") << QString("12671");
        QTest::newRow("boolean") << QString("{\"token\":true}") << QString("\"token\":true");
        QTest::newRow("null") << QString("{\"token\":null}") << QString("\"token\":null");
        QTest::newRow("array") << QString("{\"token\":[\"array-private-value\",42]}")
                               << QString("array-private-value");
        QTest::newRow("object") << QString("{\"token\":{\"inner\":\"object-private-value\"}}")
                                << QString("object-private-value");
        QTest::newRow("escaped-key") << QString("{\"to\\u006ben\":\"escaped-private-value\"}")
                                     << QString("escaped-private-value");
        QTest::newRow("long-value") << (QString("{\"token\":\"") + QString(5000, 's') + "\"}")
                                    << QString(128, 's');
        QTest::newRow("large-body") << (QString("{\"padding\":\"") + QString(300000, 'x') +
                                        "\",\"token\":\"late-private-value\"}")
                                    << QString("late-private-value");
    }
    void credentialViewsAndClipboard() {
        QFETCH(QString, requestBody);
        QFETCH(QString, secret);
        HttpFixture server;
        QSettings settings(
            directory.filePath(
                QString("credentials-%1.ini").arg(QString::fromLatin1(QTest::currentDataTag()))),
            QSettings::IniFormat);
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, &settings);
        page.resize(1200, 850);
        page.show();
        url(page, server.url());
        widget<QComboBox>(page, "protocolMethod")->setCurrentText("POST");
        widget<QComboBox>(page, "protocolBodyKind")->setCurrentIndex(2);
        widget<QPlainTextEdit>(page, "protocolRequestBody")->setPlainText(requestBody);
        page.triggerSend();
        QTRY_VERIFY_WITH_TIMEOUT(!page.session()->active(), 3000);
        QTRY_VERIFY(widget<QPlainTextEdit>(page, "protocolResponseBody")
                        ->toPlainText()
                        .contains(QStringLiteral("已遮蔽")));
        QCOMPARE(server.requests.front().value("body").toString(), requestBody);
        const auto raw = page.session()->latestResponse();
        QVERIFY(raw.value("bodyText").toString().contains("response-private-secret"));
        auto *tabs = widget<QTabWidget>(page, "protocolResponseTabs");
        for (int at = 0; at < tabs->count(); ++at) {
            tabs->setCurrentIndex(at);
            auto *view = qobject_cast<QPlainTextEdit *>(tabs->currentWidget());
            if(!view)view=tabs->currentWidget()->findChild<QPlainTextEdit*>();
            QVERIFY(view);
            QVERIFY(!view->toPlainText().contains(secret));
            QVERIFY(!view->toPlainText().contains("response-private-secret"));
            QVERIFY(!view->toPlainText().contains("cookie-private-value"));
            QVERIFY(view->toPlainText().size() <= 33024);
            QString clipboard;
            for (int attempt = 0; attempt < 3; ++attempt) {
                QTest::qWait(60);
                widget<QPushButton>(page, "protocolCopy")->click();
                QTest::qWait(100);
                clipboard = QApplication::clipboard()->text();
                if (clipboard == view->toPlainText())
                    break;
            }
            QCOMPARE(clipboard, view->toPlainText());
        }
        QString error;
        QVERIFY(page.saveDraft(&error));
        QVERIFY(!settings.value("manual/httpProjectsV2").toByteArray().isEmpty());
        QVERIFY(!settings.value("manual/httpProjectsV2").toByteArray().contains(secret.toUtf8()));
        QCOMPARE(page.session()->latestResponse(), raw);
    }
    void rawBodyBytePagesAreComplete() {
        HttpFixture server;
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, nullptr);
        page.resize(1200, 800);
        page.show();
        url(page, server.url("/plain"));
        page.triggerSend();
        QTRY_VERIFY_WITH_TIMEOUT(!page.session()->active(), 3000);
        QTRY_VERIFY(widget<QPlainTextEdit>(page, "protocolResponseBody")
                        ->toPlainText()
                        .contains(QStringLiteral("中文🙂")));
        QByteArray actual;
        auto *next = widget<QPushButton>(page, "protocolNext");
        do {
            const auto hex = widget<QPlainTextEdit>(page, "protocolResponseHex")->toPlainText();
            actual += QByteArray::fromHex(hex.toLatin1());
            if (!next->isEnabled())
                break;
            next->click();
        } while (true);
        QCOMPARE(actual, QByteArray::fromBase64(
                             page.session()->latestResponse().value("bodyBase64").toString().toLatin1()));
        QVERIFY(widget<QPlainTextEdit>(page, "protocolResponseBody")->toPlainText().endsWith("payload-tail"));
    }
    void actualWebSocketTextBinaryEmptyAndClose() {
        WsFixture server;
        ProtocolDebugSession s(ProtocolDebugSession::Mode::WebSocket);
        QVERIFY(s.start(parameters(server.url())));
        QTRY_VERIFY_WITH_TIMEOUT(s.connected(), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(count(s, "RX"), 1, 3000);
        const auto text = QStringLiteral("文本🙂").toUtf8();
        QVERIFY(s.sendMessage(text, false));
        QTRY_VERIFY_WITH_TIMEOUT(!s.writing(), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(server.texts.size(), 1, 3000);
        QCOMPARE(server.texts.front().toUtf8(), text);
        QByteArray binary = QByteArray::fromHex("000102ff") + QByteArray(18000, 'x');
        QVERIFY(s.sendMessage(binary, true));
        QTRY_COMPARE_WITH_TIMEOUT(server.binary.size(), 1, 3000);
        QCOMPARE(server.binary.front(), binary);
        QTRY_VERIFY_WITH_TIMEOUT(!s.writing(), 3000);
        QVERIFY(s.sendMessage({}, false));
        QTRY_COMPARE_WITH_TIMEOUT(server.texts.size(), 2, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!s.writing(), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(count(s, "RX"), 4, 3000);
        QCOMPARE(s.receivedBytes(),
                 quint64(QStringLiteral("主动推送").toUtf8().size() + text.size() + binary.size()));
        bool found = false;
        for (const auto &e : s.entries())
            if (e.direction == "RX" && e.binary && e.payload == binary)
                found = true;
        QVERIFY(found);
        s.close(1000, QStringLiteral("正常完成"));
        QTRY_VERIFY_WITH_TIMEOUT(!s.active(), 3000);
        QVERIFY(count(s, "CLOSE") >= 1);
    }
    void webSocketPeerCloseCodeAndReason() {
        WsFixture server;
        ProtocolDebugSession s(ProtocolDebugSession::Mode::WebSocket);
        QVERIFY(s.start(parameters(server.url())));
        QTRY_VERIFY_WITH_TIMEOUT(s.connected(), 3000);
        QVERIFY(server.peer);
        server.peer->close(QWebSocketProtocol::CloseCode(4001), QStringLiteral("令牌过期"));
        QTRY_VERIFY_WITH_TIMEOUT(!s.active(), 3000);
        bool found = false;
        for (const auto &e : s.entries())
            found |= e.direction == "CLOSE" && e.detail.contains("4001") &&
                     e.detail.contains(QStringLiteral("令牌过期"));
        QVERIFY(found);
    }
    void webSocketHistoryIsByteBounded() {
        WsFixture server;
        ProtocolDebugSession s(ProtocolDebugSession::Mode::WebSocket);
        QVERIFY(s.start(parameters(server.url())));
        QTRY_VERIFY_WITH_TIMEOUT(s.connected(), 3000);
        QTRY_VERIFY(server.peer);
        const QByteArray payload(20000, 'x');
        for (int i = 0; i < 240; ++i)
            server.peer->sendBinaryMessage(payload);
        QTRY_VERIFY_WITH_TIMEOUT(s.receivedBytes() >= quint64(240 * payload.size()), 6000);
        QVERIFY(s.retainedBytes() <= 4 * 1024 * 1024);
        QVERIFY(s.entries().size() <= 500);
        QVERIFY(s.omitted() > 0);
        s.clearHistory();
        QCOMPARE(s.retainedBytes(), qsizetype(0));
        QVERIFY(s.connected());
        s.close();
        QTRY_VERIFY_WITH_TIMEOUT(!s.active(), 3000);
    }
    void webSocketPagePausesOnlyDisplay() {
        WsFixture server;
        ProtocolDebugPage page(ProtocolDebugSession::Mode::WebSocket, nullptr);
        page.resize(1180, 900);
        page.show();
        url(page, server.url());
        page.triggerSend();
        QTRY_VERIFY_WITH_TIMEOUT(page.session()->connected(), 3000);
        QTRY_VERIFY(widget<QTableView>(page, "protocolHistory")->model()->rowCount() > 0);
        auto *pause = widget<QCheckBox>(page, "protocolPauseDisplay");
        pause->setChecked(true);
        const int displayed = widget<QTableView>(page, "protocolHistory")->model()->rowCount();
        server.peer->sendTextMessage("during-pause");
        QTRY_VERIFY(count(*page.session(), "RX") >= 2);
        QCOMPARE(widget<QTableView>(page, "protocolHistory")->model()->rowCount(), displayed);
        pause->setChecked(false);
        QTRY_VERIFY(
            widget<QPlainTextEdit>(page, "protocolResponseBody")->toPlainText().contains("during-pause"));
        widget<QPlainTextEdit>(page, "protocolMessage")->setPlainText("0G");
        widget<QComboBox>(page, "protocolMessageKind")->setCurrentIndex(1);
        page.triggerSend();
        QCOMPARE(server.binary.size(), 0);
        widget<QPlainTextEdit>(page, "protocolMessage")->setPlainText("00 01 FF");
        page.triggerSend();
        QTRY_COMPARE_WITH_TIMEOUT(server.binary.size(), 1, 3000);
        QCOMPARE(server.binary.front(), QByteArray::fromHex("0001ff"));
    }
    void mainWindowResourceConfirmPreservesAndReplaces() {
        HttpFixture server;
        SessionController c;
        MainWindow w(&c);
        w.show();
        ConnectionConfig config;
        config.kind = TransportKind::Udp;
        config.localAddress = "127.0.0.1";
        config.localPort = 0;
        c.start(config);
        QTRY_VERIFY_WITH_TIMEOUT(c.connected(), 3000);
        widget<QPushButton>(w, "httpWorkspaceMode")->click();
        auto *h = widget<ProtocolDebugPage>(w, "httpDebugPage");
        url(*h, server.url());
        {
            auto decision = answer("manualProtocolResourceConfirmation", QMessageBox::RejectRole);
            h->triggerSend();
        }
        QVERIFY(c.connected());
        QCOMPARE(server.requests.size(), 0);
        {
            auto decision = answer("manualProtocolResourceConfirmation", QMessageBox::AcceptRole);
            h->triggerSend();
        }
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 1, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!h->session()->active(), 3000);
        QVERIFY(!c.connected());
    }
    void confirmationFencesChangedEpoch() {
        HttpFixture server;
        WsFixture ws;
        SessionController c;
        MainWindow w(&c);
        w.show();
        auto *s = widget<ProtocolDebugPage>(w, "webSocketDebugPage");
        widget<QPushButton>(w, "webSocketWorkspaceMode")->click();
        url(*s, ws.url());
        s->triggerSend();
        QTRY_VERIFY_WITH_TIMEOUT(s->session()->connected(), 3000);
        widget<QPushButton>(w, "httpWorkspaceMode")->click();
        auto *h = widget<ProtocolDebugPage>(w, "httpDebugPage");
        url(*h, server.url());
        {
            auto decision = answer("manualProtocolResourceConfirmation", QMessageBox::AcceptRole,
                                   [&] { s->session()->cancel(); });
            h->triggerSend();
        }
        QTest::qWait(100);
        QCOMPARE(server.requests.size(), 0);
        QVERIFY(!h->session()->active());
        QVERIFY(widget<QLabel>(*h, "protocolWarning")->text().contains(QStringLiteral("变更")));
    }
    void browsingKeepsWsAndHiddenShortcutsSilent() {
        WsFixture server;
        SessionController c;
        MainWindow w(&c);
        w.show();
        auto *s = widget<ProtocolDebugPage>(w, "webSocketDebugPage");
        widget<QPushButton>(w, "webSocketWorkspaceMode")->click();
        url(*s, server.url());
        s->triggerSend();
        QTRY_VERIFY_WITH_TIMEOUT(s->session()->connected(), 3000);
        widget<QPushButton>(w, "capturesNavigation")->click();
        QTest::keyClick(&w, Qt::Key_Return, Qt::ControlModifier);
        widget<QPushButton>(w, "workflowNavigation")->click();
        QTest::keyClick(&w, Qt::Key_Return, Qt::ControlModifier);
        QTest::qWait(100);
        QVERIFY(s->session()->connected());
        QCOMPARE(server.texts.size(), 0);
        widget<QPushButton>(w, "workspaceNavigation")->click();
        widget<QPushButton>(w, "rawWorkspaceMode")->click();
        QVERIFY(s->session()->connected());
        {
            auto decision = answer("manualProtocolResourceConfirmation", QMessageBox::RejectRole);
            widget<QPushButton>(w, "connectButton")->click();
        }
        QVERIFY(s->session()->connected());
        QVERIFY(!c.connected());
    }
    void workflowRequiresExplicitReleaseOfManualWs() {
        WsFixture server;
        SessionController c;
        MainWindow w(&c);
        w.show();
        auto *s = widget<ProtocolDebugPage>(w, "webSocketDebugPage");
        widget<QPushButton>(w, "webSocketWorkspaceMode")->click();
        url(*s, server.url());
        s->triggerSend();
        QTRY_VERIFY_WITH_TIMEOUT(s->session()->connected(), 3000);
        auto *flow = widget<WorkflowPage>(w, "workflowPage");
        WorkflowDocument doc;
        doc.id = "manual-conflict";
        doc.name = "manual-conflict";
        doc.nodes = {{"start", "start", "Start", QPointF(), {}},
                     {"delay", "delay", "Delay", QPointF(), {{"duration", "1000"}}},
                     {"end", "end", "End", QPointF(), {}}};
        doc.edges = {{"edge1", "start", "delay", "success"}, {"edge2", "delay", "end", "success"}};
        flow->setDocument(doc);
        widget<QPushButton>(w, "workflowNavigation")->click();
        {
            auto decision = answer("manualProtocolResourceConfirmation", QMessageBox::RejectRole);
            flow->startRun();
        }
        QVERIFY(!flow->runner()->active());
        QVERIFY(s->session()->connected());
        {
            auto decision = answer("manualProtocolResourceConfirmation", QMessageBox::AcceptRole);
            flow->startRun();
        }
        QVERIFY(flow->runner()->active());
        QVERIFY(!s->session()->active());
        flow->runner()->stop();
    }
    void mainWindowExitCancellationPreservesWs() {
        WsFixture server;
        SessionController c;
        MainWindow w(&c);
        w.show();
        auto *s = widget<ProtocolDebugPage>(w, "webSocketDebugPage");
        widget<QPushButton>(w, "webSocketWorkspaceMode")->click();
        url(*s, server.url());
        s->triggerSend();
        QTRY_VERIFY_WITH_TIMEOUT(s->session()->connected(), 3000);
        {
            auto decision = answer("manualProtocolExitConfirmation", QMessageBox::RejectRole);
            QVERIFY(!w.close());
        }
        QVERIFY(w.isVisible());
        QVERIFY(s->session()->connected());
        {
            auto decision = answer("manualProtocolExitConfirmation", QMessageBox::AcceptRole);
            QVERIFY(w.close());
        }
        QVERIFY(!s->session()->active());
    }
    void wsHandshakeHeadersAndOutgoingCapacity() {
        WsFixture server;
        ProtocolDebugPage page(ProtocolDebugSession::Mode::WebSocket, nullptr);
        url(page, server.url());
        widget<QComboBox>(page, "protocolAuthKind")->setCurrentIndex(1);
        widget<QLineEdit>(page, "protocolBearer")->setText("handshake-value");
        row(widget<QTableWidget>(page, "protocolHeaders"), "X-Manual", "present");
        widget<QSpinBox>(page, "protocolCapacity")->setValue(1);
        page.triggerSend();
        QTRY_VERIFY_WITH_TIMEOUT(page.session()->connected(), 3000);
        QCOMPARE(server.peer->request().rawHeader("Authorization"), QByteArray("Bearer handshake-value"));
        QCOMPARE(server.peer->request().rawHeader("X-Manual"), QByteArray("present"));
        QString error;
        QVERIFY(!page.session()->sendMessage(QByteArray(1025, 'x'), true, &error));
        QCOMPARE(server.binary.size(), 0);
        QVERIFY(page.session()->connected());
    }
    void nativeWsMessagesAndCompactGeometry() {
        WsFixture server;
        SessionController c;
        MainWindow w(&c);
        w.show();
        widget<QPushButton>(w, "webSocketWorkspaceMode")->click();
        auto *page = widget<ProtocolDebugPage>(w, "webSocketDebugPage");
        url(*page, server.url());
        page->triggerSend();
        QTRY_VERIFY_WITH_TIMEOUT(page->session()->connected(), 3000);
        widget<QPlainTextEdit>(*page, "protocolMessage")
            ->setPlainText(QStringLiteral("{\"event\":\"ping\",\"payload\":\"中文🙂\"}"));
        page->triggerSend();
        QTRY_COMPARE_WITH_TIMEOUT(server.texts.size(), 1, 3000);
        QTRY_VERIFY(widget<QPlainTextEdit>(*page, "protocolResponseBody")
                        ->toPlainText()
                        .contains(QStringLiteral("中文🙂")));
        const auto out = qEnvironmentVariable("PORTBRIDGE_MANUAL_SCREENSHOT_DIR");
        if (!out.isEmpty()) {
            QDir().mkpath(out);
            for (bool light : {false, true}) {
                if (w.property("darkTheme").toBool() == light)
                    widget<QPushButton>(w, "themeButton")->click();
                w.resize(1440, 1000);
                QTest::qWait(100);
                QVERIFY(w.grab().save(
                    QDir(out).filePath(light ? "ws-messages-light.png" : "ws-messages-dark.png")));
            }
            w.resize(1024, 768);
            QTest::qWait(100);
            QVERIFY(w.grab().save(QDir(out).filePath("ws-compact.png")));
            QSaveFile file(QDir(out).filePath("geometry.json"));
            QVERIFY(file.open(QIODevice::WriteOnly));
            const auto result = QJsonObject{
                {"width", w.width()},
                {"height", w.height()},
                {"dpr", w.devicePixelRatioF()},
                {"responseHeight", widget<QPlainTextEdit>(*page, "protocolResponseBody")->height()},
                {"messageHeight", widget<QPlainTextEdit>(*page, "protocolMessage")->height()}};
            const auto bytes = QJsonDocument(result).toJson();
            QCOMPARE(file.write(bytes), qint64(bytes.size()));
            QVERIFY(file.commit());
        }
        w.resize(1024, 768);
        QTest::qWait(100);
        auto *modes = widget<QWidget>(w, "workspaceProtocolBar");
        for (const auto *id : {"rawWorkspaceMode", "httpWorkspaceMode", "webSocketWorkspaceMode"}) {
            auto *button = widget<QPushButton>(w, id);
            QVERIFY(modes->rect().contains(QRect(button->mapTo(modes, QPoint()), button->size())));
        }
        QCOMPARE(w.size(), QSize(1024, 768));
        QVERIFY(widget<QPlainTextEdit>(*page, "protocolResponseBody")->height() >= 40);
        QVERIFY(widget<QPlainTextEdit>(*page, "protocolMessage")->height() >= 68);
    }
    void nativeHttpClipboardMaskAndGeometry() {
        HttpFixture server;
        SessionController c;
        MainWindow w(&c);
        w.show();
        widget<QPushButton>(w, "httpWorkspaceMode")->click();
        auto *h = widget<ProtocolDebugPage>(w, "httpDebugPage");
        url(*h, server.url("/unauthorized"));
        widget<QComboBox>(*h, "protocolAuthKind")->setCurrentIndex(1);
        widget<QLineEdit>(*h, "protocolBearer")->setText("request-private-secret");
        h->triggerSend();
        QTRY_VERIFY_WITH_TIMEOUT(!h->session()->active(), 3000);
        auto *response = widget<QPlainTextEdit>(*h, "protocolResponseBody");
        QTRY_VERIFY(response->toPlainText().contains(QStringLiteral("已遮蔽")));
        for (const auto &secret :
             {"request-private-secret", "response-private-secret", "cookie-private-value"})
            QVERIFY(!response->toPlainText().contains(secret));
        widget<QPushButton>(*h, "protocolCopy")->click();
        QTest::qWait(100);
        QCOMPARE(QApplication::clipboard()->text(), response->toPlainText());
        response->setFocus();
        response->selectAll();
        QTest::keyClick(response, Qt::Key_C, Qt::ControlModifier);
        QTest::qWait(100);
        QCOMPARE(QApplication::clipboard()->text(), response->toPlainText());
        const auto shotDir = qEnvironmentVariable("PORTBRIDGE_MANUAL_SCREENSHOT_DIR");
        if (!shotDir.isEmpty()) {
            QDir().mkpath(shotDir);
            for (bool light : {false, true}) {
                if (w.property("darkTheme").toBool() == light)
                    widget<QPushButton>(w, "themeButton")->click();
                w.resize(1440, 1000);
                QTest::qWait(100);
                QVERIFY(w.grab().save(
                    QDir(shotDir).filePath(light ? "http-401-light.png" : "http-401-dark.png")));
            }
            w.resize(1024, 768);
            QTest::qWait(100);
            QVERIFY(w.grab().save(QDir(shotDir).filePath("http-compact.png")));
        }
        QVERIFY(response->height() >= 40);
        QVERIFY(widget<QTabWidget>(*h, "protocolResponseTabs")->width() >= 200);
    }
};
QTEST_MAIN(ProtocolDebugTest)
#include "test_protocol_debug.moc"
