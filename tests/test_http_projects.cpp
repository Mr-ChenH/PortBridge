#include "portbridge/session_controller.hpp"
#include "ui/http_assertions.hpp"
#include "ui/http_browser_fingerprint.hpp"
#include "ui/http_configuration_dialog.hpp"
#include "ui/http_project_panel.hpp"
#include "ui/http_project_store.hpp"
#include "ui/http_request_resolver.hpp"
#include "ui/http_sequence_runner.hpp"
#include "ui/main_window.hpp"
#include "ui/protocol_debug_page.hpp"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslSocket>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
using namespace portbridge;
namespace {
template <class T> T *widget(QWidget &root, const char *id) {
    auto *w = root.findChild<T *>(id);
    if (!w)
        qFatal("Missing widget %s", id);
    return w;
}
void row(QTableWidget *table, const QString &name, const QString &value) {
    int at = table->rowCount();
    table->insertRow(at);
    auto *check = new QTableWidgetItem;
    check->setCheckState(Qt::Checked);
    table->setItem(at, 0, check);
    table->setItem(at, 1, new QTableWidgetItem(name));
    table->setItem(at, 2, new QTableWidgetItem(value));
}
QJsonObject variable(const QString &name, QJsonValue value, bool secret = false) {
    return {{"name", name}, {"value", value}, {"secret", secret}};
}
QJsonObject rule(const QString &name, const QString &path) {
    return {{"variable", name}, {"source", "json"}, {"path", path}, {"secret", true}};
}
QJsonObject request(const QString &url, const QString &name = "request") {
    return {{"schemaVersion", 1},
            {"kind", "http"},
            {"id", "original-request"},
            {"name", name},
            {"params", QJsonObject{{"url", url}, {"method", "GET"}}},
            {"editor", QJsonObject{{"baseUrl", url}, {"authKind", 0}}}};
}
QJsonObject success(const QString &value) {
    return {{"status", 200}, {"body", QJsonObject{{"data", QJsonObject{{"access_token", value}}}}}};
}
struct Server {
    QTcpServer server;
    QVector<QJsonObject> received;
    QVector<QJsonObject> &requests =
        received; // Both test generations inspect the same real wire receipts.
    QString token = "synthetic-token+/=:%";
    Server() {
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                auto input = std::make_shared<QByteArray>();
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, input] {
                    *input += socket->readAll();
                    const int end = input->indexOf("\r\n\r\n");
                    if (end < 0)
                        return;
                    auto lines = input->left(end).split('\n');
                    const auto start = lines.first().trimmed().split(' ');
                    if (start.size() != 3)
                        return;
                    QJsonObject headers;
                    int length = 0;
                    for (int i = 1; i < lines.size(); ++i) {
                        const auto field = lines[i].trimmed();
                        int pos = field.indexOf(':');
                        if (pos < 0)
                            continue;
                        const auto key = field.left(pos).toLower();
                        const auto value = field.mid(pos + 1).trimmed();
                        headers[QString::fromLatin1(key)] = QString::fromUtf8(value);
                        if (key == "content-length")
                            length = value.toInt();
                    }
                    if (input->size() < end + 4 + length)
                        return;
                    const auto body = input->mid(end + 4, length);
                    received.append({{"method", QString::fromLatin1(start[0])},
                                     {"uri", QString::fromLatin1(start[1])},
                                     {"headers", headers},
                                     {"body", QString::fromUtf8(body)}});
                    socket->disconnect(socket, &QTcpSocket::readyRead, nullptr, nullptr);
                    const bool login = start[1].startsWith("/login") || start[1].startsWith("/late");
                    const auto result =
                        (start[1].startsWith("/array")
                             ? QJsonDocument(QJsonArray{QJsonObject{
                                   {"id", 7},
                                   {"items", QJsonArray{QJsonArray{QJsonObject{{"id", 8}}}}}}})
                             : QJsonDocument(login ? success(token).value("body").toObject()
                                                   : QJsonObject{{"echo", received.last()}}))
                            .toJson(QJsonDocument::Compact);
                    const int status = start[1].startsWith("/failure") ? 401 : 200;
                    const auto bytes = QByteArray("HTTP/1.1 ") + QByteArray::number(status) +
                                       " Result\r\nContent-Type: application/json\r\nConnection: "
                                       "close\r\nX-Session: header-secret\r\nContent-Length: " +
                                       QByteArray::number(result.size()) + "\r\n\r\n" + result;
                    auto write = [socket, bytes] {
                        socket->write(bytes);
                        socket->disconnectFromHost();
                    };
                    if (start[1].startsWith("/late"))
                        QTimer::singleShot(350, socket, write);
                    else
                        write();
                });
            }
        });
        server.listen(QHostAddress::LocalHost, 0);
    }
    QString base() const { return QString("http://127.0.0.1:%1").arg(server.serverPort()); }
};
struct HttpsServer : QTcpServer {
    QSslCertificate certificate;
    QSslKey key;
    QVector<QJsonObject> requests;
    void incomingConnection(qintptr descriptor) override {
        auto *socket = new QSslSocket(this);
        if (!socket->setSocketDescriptor(descriptor)) {
            delete socket;
            return;
        }
        socket->setLocalCertificate(certificate);
        socket->setPrivateKey(key);
        socket->setPeerVerifyMode(QSslSocket::VerifyNone);
        auto input = std::make_shared<QByteArray>();
        QObject::connect(socket, &QSslSocket::disconnected, socket, &QObject::deleteLater);
        QObject::connect(socket, &QSslSocket::readyRead, socket, [this, socket, input] {
            *input += socket->readAll();
            const int end = input->indexOf("\r\n\r\n");
            if (end < 0)
                return;
            QJsonObject headers;
            for (const auto &line : input->left(end).split('\n')) {
                const auto colon = line.indexOf(':');
                if (colon > 0)
                    headers[QString::fromLatin1(line.left(colon).trimmed().toLower())] =
                        QString::fromUtf8(line.mid(colon + 1).trimmed());
            }
            requests.append(headers);
            socket->disconnect(socket, &QSslSocket::readyRead, nullptr, nullptr);
            const auto body =
                QJsonDocument(QJsonObject{{"headers", headers}}).toJson(QJsonDocument::Compact);
            socket->write("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: "
                          "application/json\r\nContent-Length: " +
                          QByteArray::number(body.size()) + "\r\n\r\n" + body);
            socket->disconnectFromHost();
        });
        socket->startServerEncryption();
    }
    QString base() const { return QString("https://localhost:%1").arg(serverPort()); }
};
} // namespace
class HttpProjectsTest : public QObject {
    Q_OBJECT
    QTemporaryDir directory;
  private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("PortBridgeHttpProjectsTests");
        QCoreApplication::setApplicationName("HttpProjects");
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
    void legacyMigrationAndStableIdentity() {
        QTemporaryDir dir;
        QSettings settings(dir.filePath("projects.ini"), QSettings::IniFormat);
        const auto old = request("http://127.0.0.1:1/legacy", "旧方案");
        const auto legacy =
            QJsonDocument(QJsonObject{{"schemaVersion", 1}, {"entries", QJsonArray{old}}}).toJson();
        settings.setValue("manual/httpLibrary", legacy);
        settings.sync();
        QString id;
        {
            ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, &settings);
            id = page.projectStore()->projectId();
            QCOMPARE(page.projectStore()->requests().size(), 1);
            QCOMPARE(page.projectStore()->requests().first().toObject().value("id").toString(),
                     "original-request");
            QCOMPARE(widget<QListWidget>(page, "protocolLibrary")->count(), 1);
            QCOMPARE(widget<QComboBox>(page, "httpFolderFilter")->currentData().toString(), "*");
            QVERIFY(!page.session()->active());
            QCOMPARE(settings.value("manual/httpLibrary").toByteArray(), legacy);
            QCOMPARE(QJsonDocument::fromJson(settings.value("manual/httpProjectsV2").toByteArray())
                         .object()
                         .value("schemaVersion")
                         .toInt(),
                     2);
        }
        HttpProjectStore again(&settings);
        QCOMPARE(again.projectId(), id);
        QCOMPARE(again.requests().size(), 1);
        QVERIFY(again.loadError().isEmpty());
    }
    void variablesTypesIsolationAndBounds() {
        HttpProjectStore store(nullptr);
        QString error;
        QVERIFY(store.setEnvironmentVariables(
            {variable("text", QStringLiteral("引号\" 反斜杠\\\n中文🙂")), variable("number", 42),
             variable("flag", true), variable("data", QJsonObject{{"value", 1}})},
            &error));
        const auto resolved = store.expand(
            "{\"text\":\"{{text}}\",\"number\":{{number}},\"flag\":{{flag}},\"object\":{{data}}}",
            &error, true);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QJsonParseError parse;
        const auto doc = QJsonDocument::fromJson(resolved.toUtf8(), &parse);
        QCOMPARE(parse.error, QJsonParseError::NoError);
        QCOMPARE(doc.object().value("number").toInt(), 42);
        QCOMPARE(doc.object().value("text").toString(), QStringLiteral("引号\" 反斜杠\\\n中文🙂"));
        QVERIFY(doc.object().value("flag").toBool());
        QVERIFY(doc.object().value("object").isObject());
        const auto original = store.environmentId();
        QVERIFY(store.createEnvironment("生产", &error));
        store.expand("{{text}}", &error);
        QVERIFY(!error.isEmpty());
        QVERIFY(store.selectEnvironment(original));
        QCOMPARE(store.expand("{{number}}"), "42");
        QJsonArray many;
        for (int i = 0; i < 257; ++i)
            many.append(variable("v" + QString::number(i), "x"));
        QVERIFY(!store.setEnvironmentVariables(many, &error));
        QCOMPARE(store.expand("{{number}}"), "42");
    }
    void atomicExtractionAndEnvironmentIsolation() {
        HttpProjectStore store(nullptr);
        QString error;
        const auto pid = store.projectId(), eid = store.environmentId();
        QVERIFY(store.extract(success("first-secret"), {rule("access_token", "$.data.access_token")},
                              pid, eid, &error));
        QCOMPARE(store.expand("{{access_token}}"), "first-secret");
        QVERIFY(!store.extract(
            success("new-secret"),
            {rule("access_token", "$.data.access_token"), rule("missing", "$.data.missing")}, pid, eid,
            &error));
        QCOMPARE(store.expand("{{access_token}}"), "first-secret");
        auto failure = success("bad-token");
        failure["status"] = 401;
        QVERIFY(
            !store.extract(failure, {rule("access_token", "$.data.access_token")}, pid, eid, &error));
        QCOMPARE(store.expand("{{access_token}}"), "first-secret");
        QVERIFY(store.createEnvironment("other"));
        store.expand("{{access_token}}", &error);
        QVERIFY(!error.isEmpty());
        QVERIFY(store.selectEnvironment(eid));
        QCOMPARE(store.expand("{{access_token}}"), "first-secret");
        QVERIFY(store.createProject("other-project"));
        store.expand("{{access_token}}", &error);
        QVERIFY(!error.isEmpty());
        QVERIFY(store.selectProject(pid));
        QVERIFY(store.selectEnvironment(eid));
        store.clearRuntime();
        store.expand("{{access_token}}", &error);
        QVERIFY(!error.isEmpty());
    }
    void secretsExcludedFromPersistenceAndExport() {
        QTemporaryDir dir;
        QSettings settings(dir.filePath("projects.ini"), QSettings::IniFormat);
        HttpProjectStore store(&settings);
        QString error;
        QVERIFY(
            store.setEnvironmentVariables({variable("base_url", "https://example.com"),
                                           variable("access_token", "synthetic-private-value", false)},
                                          &error));
        QCOMPARE(store.expand("{{access_token}}"), "synthetic-private-value");
        QVERIFY(
            !settings.value("manual/httpProjectsV2").toByteArray().contains("synthetic-private-value"));
        QVERIFY(!QJsonDocument(store.exportProject({})).toJson().contains("synthetic-private-value"));
        HttpProjectStore restarted(&settings);
        restarted.expand("{{access_token}}", &error);
        QVERIFY(!error.isEmpty());
        QCOMPARE(restarted.expand("{{base_url}}"), "https://example.com");
        const auto malformedSecret = QStringLiteral("literal-private-token{{not-a-template");
        QVERIFY(store.setProjectAuth({{"kind", "bearer"}, {"token", malformedSecret}}, &error));
        QVERIFY(
            !settings.value("manual/httpProjectsV2").toByteArray().contains(malformedSecret.toUtf8()));
        QVERIFY(!QJsonDocument(store.exportProject({})).toJson().contains(malformedSecret.toUtf8()));
    }
    void realLoginReuseAndTemplateSaving() {
        Server server;
        QTemporaryDir dir;
        QSettings settings(dir.filePath("projects.ini"), QSettings::IniFormat);
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, &settings);
        QString error;
        auto *store = page.projectStore();
        QVERIFY(store->setEnvironmentVariables({variable("base_url", server.base())}, &error));
        QVERIFY(store->setProjectAuth({{"kind", "bearer"}, {"token", "{{access_token}}"}}, &error));
        QVERIFY(page.loadDraft(request("{{base_url}}/login", "登录"), &error));
        row(widget<QTableWidget>(page, "httpExtractionRules"), "access_token", "$.data.access_token");
        QVERIFY2(page.saveDraft(&error), qPrintable(error));
        QVERIFY(
            page.exportDraft(&error).value("editor").toObject().value("baseUrl").toString().contains(
                "{{base_url}}"));
        QVERIFY(!page.session()->active());
        QCOMPARE(server.received.size(), 0);
        page.triggerSend();
        QTRY_VERIFY_WITH_TIMEOUT(!page.session()->active() && server.received.size() == 1, 5000);
        QCOMPARE(store->expand("{{access_token}}"), server.token);
        QVERIFY(!server.received[0].value("headers").toObject().contains("authorization"));
        auto next = request("{{base_url}}/business", "查询用户");
        next["id"] = "business-request";
        auto edit = next.value("editor").toObject();
        edit["authKind"] = 3;
        next["editor"] = edit;
        QVERIFY(page.loadDraft(next, &error));
        QVERIFY2(page.saveDraft(&error), qPrintable(error));
        page.triggerSend();
        QTRY_VERIFY_WITH_TIMEOUT(!page.session()->active() && server.received.size() == 2, 5000);
        QCOMPARE(server.received[1].value("headers").toObject().value("authorization").toString(),
                 "Bearer " + server.token);
        QVERIFY(!settings.value("manual/httpProjectsV2").toByteArray().contains(server.token.toUtf8()));
        QVERIFY(!widget<QPlainTextEdit>(page, "protocolResponseBody")
                     ->toPlainText()
                     .contains(server.token));
        QVERIFY(!QJsonDocument(store->exportProject(store->requests()))
                     .toJson()
                     .contains(server.token.toUtf8()));
    }
    void realEncodingJsonAndHeaderInjection() {
        Server server;
        QTemporaryDir dir;
        QSettings settings(dir.filePath("projects.ini"), QSettings::IniFormat);
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, &settings);
        QString error;
        const auto special = QStringLiteral("+% /&=引号\"\\\n中文🙂");
        QVERIFY(page.projectStore()->setEnvironmentVariables(
            {variable("base_url", server.base()), variable("special", special), variable("count", 9),
             variable("unsafe", "ok\r\nX-Injected:yes")},
            &error));
        QVERIFY(page.loadDraft(request("{{base_url}}/echo"), &error));
        widget<QComboBox>(page, "protocolMethod")->setCurrentText("POST");
        row(widget<QTableWidget>(page, "protocolQuery"), "q", "{{special}}");
        widget<QComboBox>(page, "protocolBodyKind")->setCurrentIndex(2);
        widget<QPlainTextEdit>(page, "protocolRequestBody")
            ->setPlainText("{\"text\":\"{{special}}\",\"count\":{{count}}}");
        page.triggerSend();
        QTRY_VERIFY_WITH_TIMEOUT(!page.session()->active() && server.received.size() == 1, 5000);
        QCOMPARE(QUrlQuery(QUrl(server.received[0].value("uri").toString()))
                     .queryItemValue("q", QUrl::FullyDecoded),
                 special);
        const auto body =
            QJsonDocument::fromJson(server.received[0].value("body").toString().toUtf8()).object();
        QCOMPARE(body.value("text").toString(), special);
        QCOMPARE(body.value("count").toInt(), 9);
        row(widget<QTableWidget>(page, "protocolHeaders"), "X-Test", "{{unsafe}}");
        page.triggerSend();
        QTest::qWait(120);
        QCOMPARE(server.received.size(), 1);
        QVERIFY(!page.session()->active());
        QVERIFY(!widget<QLabel>(page, "protocolWarning")->text().isEmpty());
    }
    void cancelAndChangedScopeNeverExtract() {
        Server server;
        QTemporaryDir dir;
        QSettings settings(dir.filePath("projects.ini"), QSettings::IniFormat);
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, &settings);
        QString error;
        auto *store = page.projectStore();
        const auto old = store->environmentId();
        QVERIFY(store->createEnvironment("second"));
        const auto other = store->environmentId();
        QVERIFY(store->selectEnvironment(old));
        QVERIFY(page.loadDraft(request(server.base() + "/late"), &error));
        row(widget<QTableWidget>(page, "httpExtractionRules"), "access_token", "$.data.access_token");
        page.triggerSend();
        QTRY_COMPARE_WITH_TIMEOUT(server.received.size(), 1, 5000);
        QVERIFY(!widget<QComboBox>(page, "httpEnvironmentChoice")->isEnabled());
        page.session()->cancel();
        QTest::qWait(450);
        QVERIFY(store->runtimeVariables().isEmpty());
        page.triggerSend();
        QTRY_COMPARE_WITH_TIMEOUT(server.received.size(), 2, 5000);
        QVERIFY(store->selectEnvironment(other));
        QTRY_VERIFY_WITH_TIMEOUT(!page.session()->active(), 5000);
        QVERIFY(store->runtimeVariables().isEmpty());
        QVERIFY(store->selectEnvironment(old));
        QVERIFY(store->runtimeVariables().isEmpty());
    }
    void contextRevalidationBeforeNetwork() {
        Server server;
        QTemporaryDir dir;
        QSettings settings(dir.filePath("projects.ini"), QSettings::IniFormat);
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, &settings);
        QString error;
        QVERIFY(page.loadDraft(request(server.base() + "/login"), &error));
        page.session()->setStartGuard([&](QString *) {
            page.projectStore()->createEnvironment("changed-during-confirmation");
            return true;
        });
        page.triggerSend();
        QTest::qWait(120);
        QCOMPARE(server.received.size(), 0);
        QVERIFY(!page.session()->active());
        QVERIFY(widget<QLabel>(page, "protocolWarning")->text().contains(QStringLiteral("变更")));
    }
    void corruptConfigurationPreserved() {
        QTemporaryDir dir;
        QSettings settings(dir.filePath("projects.ini"), QSettings::IniFormat);
        const QByteArray bad = "not a project document";
        settings.setValue("manual/httpProjectsV2", bad);
        settings.sync();
        HttpProjectStore store(&settings);
        QString error;
        QVERIFY(!store.loadError().isEmpty());
        QVERIFY(!store.createProject("replacement", &error));
        QCOMPARE(settings.value("manual/httpProjectsV2").toByteArray(), bad);
    }
    void projectAndFolderFiltersSilent() {
        Server server;
        QTemporaryDir dir;
        QSettings settings(dir.filePath("projects.ini"), QSettings::IniFormat);
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, &settings);
        QString error;
        QVERIFY(page.projectStore()->addFolder("认证"));
        QVERIFY(page.createSavedRequest("登录", server.base() + "/login", &error));
        widget<QComboBox>(page, "httpRequestFolder")->setCurrentText("认证");
        QVERIFY(page.saveDraft(&error));
        auto *panel = widget<HttpProjectPanel>(page, "httpProjectPanel");
        panel->refresh();
        auto *folders = widget<QComboBox>(page, "httpFolderFilter");
        folders->setCurrentIndex(folders->findData("认证"));
        QCOMPARE(widget<QListWidget>(page, "protocolLibrary")->count(), 1);
        folders->setCurrentIndex(folders->findData(""));
        QCOMPARE(widget<QListWidget>(page, "protocolLibrary")->count(), 0);
        const auto pid = page.projectStore()->projectId();
        QVERIFY(page.projectStore()->createProject("第二项目"));
        panel->refresh();
        folders->setCurrentIndex(folders->findData("*"));
        QCOMPARE(widget<QListWidget>(page, "protocolLibrary")->count(), 0);
        QVERIFY(page.projectStore()->selectProject(pid));
        panel->refresh();
        folders->setCurrentIndex(folders->findData("认证"));
        QCOMPARE(widget<QListWidget>(page, "protocolLibrary")->count(), 1);
        QCOMPARE(server.received.size(), 0);
    }
    void extractionEditorBoundIsRoundTripSafe() {
        QTemporaryDir dir;
        QSettings settings(dir.filePath("bounds.ini"), QSettings::IniFormat);
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, &settings);
        QString error;
        QVERIFY(page.loadDraft(request("http://127.0.0.1:1/never-sent"), &error));
        auto *table = widget<QTableWidget>(page, "httpExtractionRules");
        for (int i = 0; i < 32; ++i)
            row(table, "value" + QString::number(i), "$.data.access_token");
        QVERIFY(!widget<QPushButton>(page, "httpExtractionRulesAdd")->isEnabled());
        auto doc = page.exportDraft(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(page.loadDraft(doc, &error));
        row(table, "overflow", "$.data.access_token");
        table->item(32, 0)->setCheckState(Qt::Unchecked);
        QVERIFY(page.exportDraft(&error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(!page.session()->active());
    }
    void copiedEnvironmentOmitsRuntimeAndSecrets() {
        HttpProjectStore store(nullptr);
        QString error;
        QVERIFY(store.setEnvironmentVariables({variable("base_url", "http://example.com"),
                                               variable("access_token", "copy-private-secret")},
                                              &error));
        const auto original = store.environmentId();
        QVERIFY(store.copyEnvironment("副本", &error));
        QVERIFY(store.environmentId() != original);
        QCOMPARE(store.expand("{{base_url}}"), "http://example.com");
        store.expand("{{access_token}}", &error);
        QVERIFY(!error.isEmpty());
        QVERIFY(store.runtimeVariables().isEmpty());
        QVERIFY(store.selectEnvironment(original));
        QCOMPARE(store.expand("{{access_token}}"), "copy-private-secret");
    }
    void nestedVariablesAndCycleRejection() {
        HttpProjectStore store(nullptr);
        QString error;
        QVERIFY(store.setEnvironmentVariables({variable("host", "example.com"),
                                               variable("base_url", "https://{{host}}/api"),
                                               variable("a", "{{b}}"), variable("b", "{{a}}")},
                                              &error));
        QCOMPARE(store.expand("{{base_url}}", &error), "https://example.com/api");
        QVERIFY(error.isEmpty());
        QVERIFY(store.expand("{{a}}", &error).isEmpty());
        QVERIFY(error.contains(QStringLiteral("循环")));
    }
    void projectDefaultsAndSecretIsolation() {
        HttpProjectStore store(nullptr);
        QString error;
        QVERIFY(store.setProjectVariables(
            {variable("name", "project"), variable("access_token", "project-runtime-secret")}, &error));
        QCOMPARE(store.expand("{{name}}"), "project");
        const auto old = store.environmentId();
        QVERIFY(store.setEnvironmentVariables({variable("name", "environment")}, &error));
        QCOMPARE(store.expand("{{name}}"), "environment");
        QVERIFY(store.createEnvironment("second"));
        QCOMPARE(store.expand("{{name}}"), "project");
        store.expand("{{access_token}}", &error);
        QVERIFY(!error.isEmpty());
        QVERIFY(store.selectEnvironment(old));
        QCOMPARE(store.expand("{{access_token}}"), "project-runtime-secret");
    }
    void urlTemplatesAndFormEncodingUseRealBytes() {
        Server server;
        QTemporaryDir dir;
        QSettings settings(dir.filePath("projects.ini"), QSettings::IniFormat);
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, &settings);
        QString error;
        const auto special = QStringLiteral("中文🙂/+ %= &?");
        QVERIFY(page.projectStore()->setEnvironmentVariables({variable("base_url", server.base()),
                                                              variable("part", special),
                                                              variable("missing_value", "fine")},
                                                             &error));
        QVERIFY(page.loadDraft(request("{{base_url}}/echo/{{part}}?q={{part}}"), &error));
        widget<QComboBox>(page, "protocolMethod")->setCurrentText("POST");
        widget<QComboBox>(page, "protocolBodyKind")->setCurrentIndex(3);
        row(widget<QTableWidget>(page, "protocolFormBody"), "form", "{{part}}");
        page.triggerSend();
        QTRY_VERIFY_WITH_TIMEOUT(!page.session()->active() && server.received.size() == 1, 5000);
        const auto encoded = QString::fromLatin1(QUrl::toPercentEncoding(special));
        QCOMPARE(server.received[0].value("uri").toString(), "/echo/" + encoded + "?q=" + encoded);
        QCOMPARE(QUrl::fromPercentEncoding(
                     server.received[0].value("body").toString().mid(5).replace('+', ' ').toLatin1()),
                 special);
        row(widget<QTableWidget>(page, "protocolQuery"), "{{undefined}}", "{{missing_value}}");
        page.triggerSend();
        QTest::qWait(100);
        QCOMPARE(server.received.size(), 1);
        QVERIFY(page.requestParameters().isEmpty());
    }
    void exportImportAndMalformedRequestAreAtomic() {
        Server server;
        QTemporaryDir dir;
        QSettings settings(dir.filePath("projects.ini"), QSettings::IniFormat);
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, &settings);
        QString error;
        auto *store = page.projectStore();
        QVERIFY(store->setEnvironmentVariables(
            {variable("base_url", server.base()), variable("access_token", "export-private-token")},
            &error));
        QVERIFY(store->addFolder("认证"));
        QVERIFY(page.loadDraft(request("{{base_url}}/echo?token={{access_token}}"), &error));
        widget<QComboBox>(page, "protocolAuthKind")->setCurrentIndex(1);
        widget<QLineEdit>(page, "protocolBearer")->setText("{{access_token}}");
        widget<QComboBox>(page, "protocolBodyKind")->setCurrentIndex(2);
        widget<QComboBox>(page, "protocolMethod")->setCurrentText("POST");
        widget<QPlainTextEdit>(page, "protocolRequestBody")
            ->setPlainText("{\"token\":\"{{access_token}}\",\"password\":\"literal-password\"}");
        QVERIFY2(page.saveDraft(&error), qPrintable(error));
        const auto doc = page.exportHttpProject(&error);
        const auto bytes = QJsonDocument(doc).toJson();
        QVERIFY(bytes.contains("{{access_token}}"));
        QVERIFY(!bytes.contains("export-private-token"));
        QVERIFY(!bytes.contains("literal-password"));
        const auto oldId = store->projectId();
        QVERIFY2(page.importHttpProject(doc, &error), qPrintable(error));
        QVERIFY(oldId != store->projectId());
        QCOMPARE(store->projects().size(), 2);
        QCOMPARE(store->requests().size(), 2);
        QCOMPARE(widget<QListWidget>(page, "protocolLibrary")->count(), 1);
        QVERIFY(!page.session()->active());
        QCOMPARE(server.received.size(), 0);
        store->expand("{{access_token}}", &error);
        QVERIFY(!error.isEmpty());
        const auto previous = settings.value("manual/httpProjectsV2").toByteArray();
        auto bad = doc;
        auto requests = bad.value("requests").toArray();
        auto item = requests[0].toObject();
        auto edit = item.value("editor").toObject();
        edit["body"] = QString(1024 * 1024 + 1, 'x');
        item["editor"] = edit;
        requests[0] = item;
        bad["requests"] = requests;
        QVERIFY(!page.importHttpProject(bad, &error));
        QCOMPARE(settings.value("manual/httpProjectsV2").toByteArray(), previous);
        QCOMPARE(store->projects().size(), 2);
    }
    void folderMutationPreservesRequestIdentity() {
        HttpProjectStore store(nullptr);
        QString error;
        QVERIFY(store.addFolder("old", &error));
        auto r = request("http://example.com/request");
        r["projectId"] = store.projectId();
        r["folder"] = "old";
        QVERIFY(store.setRequests({r}, &error));
        QVERIFY(store.renameFolder("old", "new", &error));
        QCOMPARE(store.requests()[0].toObject().value("id").toString(), "original-request");
        QCOMPARE(store.requests()[0].toObject().value("folder").toString(), "new");
        QVERIFY(store.removeFolder("new", &error));
        QCOMPARE(store.requests()[0].toObject().value("folder").toString(), QString());
        QCOMPARE(store.requests().size(), 1);
    }
    void environmentSwitchKeepsTemplateDraft() {
        QTemporaryDir dir;
        QSettings settings(dir.filePath("projects.ini"), QSettings::IniFormat);
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, &settings);
        QString error;
        const auto original = page.projectStore()->environmentId();
        QVERIFY(page.projectStore()->createEnvironment("second"));
        widget<HttpProjectPanel>(page, "httpProjectPanel")->refresh();
        QVERIFY(page.loadDraft(request("{{base_url}}/users"), &error));
        widget<QLineEdit>(page, "protocolName")->setText("edited-name");
        auto *choices = widget<QComboBox>(page, "httpEnvironmentChoice");
        choices->setCurrentIndex(choices->findData(original));
        QCOMPARE(widget<QLineEdit>(page, "protocolUrl")->text(), "{{base_url}}/users");
        QCOMPARE(widget<QLineEdit>(page, "protocolName")->text(), "edited-name");
        QVERIFY(page.dirty());
    }
    void confirmationPreservesRawWhenProjectContextChanges() {
        Server server;
        SessionController raw;
        MainWindow window(&raw);
        window.show();
        ConnectionConfig config;
        config.kind = TransportKind::Udp;
        config.localAddress = "127.0.0.1";
        config.localPort = 0;
        raw.start(config);
        QTRY_VERIFY_WITH_TIMEOUT(raw.connected(), 3000);
        widget<QPushButton>(window, "httpWorkspaceMode")->click();
        auto *page = widget<ProtocolDebugPage>(window, "httpDebugPage");
        widget<QLineEdit>(*page, "protocolUrl")->setText(server.base() + "/login");
        QTimer answer;
        answer.setInterval(10);
        connect(&answer, &QTimer::timeout, &answer, [&] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box || box->objectName() != "manualProtocolResourceConfirmation")
                return;
            answer.stop();
            page->projectStore()->createEnvironment("changed");
            for (auto *b : box->buttons())
                if (box->buttonRole(b) == QMessageBox::AcceptRole) {
                    b->click();
                    break;
                }
        });
        answer.start();
        page->triggerSend();
        QVERIFY(raw.connected());
        QVERIFY(!page->session()->active());
        QCOMPARE(server.received.size(), 0);
        QVERIFY(widget<QLabel>(*page, "protocolWarning")->text().contains(QStringLiteral("变更")));
    }
    void assertionsUseAuthoritativeTypesAndMaskResults() {
        QJsonArray rules{
            QJsonObject{
                {"enabled", true}, {"source", "status"}, {"relation", "equals"}, {"expected", 200}},
            QJsonObject{{"enabled", true},
                        {"source", "json"},
                        {"path", "$.data.access_token"},
                        {"relation", "equals"},
                        {"expected", "assert-private-token"}},
            QJsonObject{{"enabled", true},
                        {"source", "header"},
                        {"path", "x-session"},
                        {"relation", "contains"},
                        {"expected", "secret"}}};
        auto response = success("assert-private-token");
        response["headers"] = QJsonObject{{"X-Session", "header-secret"}};
        auto results = HttpAssertions::evaluate(response, rules);
        QVERIFY(HttpAssertions::passed(results));
        QVERIFY(!QJsonDocument(results).toJson().contains("assert-private-token"));
        auto status = rules[0].toObject();
        status["expected"] = "200";
        rules[0] = status;
        QVERIFY(!HttpAssertions::passed(HttpAssertions::evaluate(response, rules)));
        auto bad = rules[1].toObject();
        bad["path"] = "$.data[999].token";
        rules[1] = bad;
        QVERIFY(!HttpAssertions::passed(HttpAssertions::evaluate(response, rules)));
        bad["relation"] = "unknown";
        QVERIFY(!HttpAssertions::validate({bad}).isEmpty());
    }
    void assertionRootArrayPathsAndInvalidIndices() {
        const QJsonArray nested{QJsonArray{QJsonObject{{"id", 8}}}};
        const QJsonObject item{{"id", 7}, {"items", nested}};
        const QJsonObject root{{"body", QJsonArray{item}}};
        auto assertion = [](const QString &path, QJsonValue expected) {
            return QJsonObject{{"enabled", true},
                               {"source", "json"},
                               {"path", path},
                               {"relation", "equals"},
                               {"expected", expected}};
        };
        for (const auto &path : {QString("$[0].id"), QString("[0].id")}) {
            const QJsonArray rules{assertion(path, 7)};
            QVERIFY2(HttpAssertions::validate(rules).isEmpty(), qPrintable(path));
            QVERIFY(HttpAssertions::passed(HttpAssertions::evaluate(root, rules)));
        }
        QVERIFY(HttpAssertions::passed(
            HttpAssertions::evaluate(root, {assertion("$[0].items[0][0].id", 8)})));
        QVERIFY(HttpAssertions::passed(HttpAssertions::evaluate(
            {{"body", QJsonObject{{"data", QJsonArray{item}}}}}, {assertion("$.data[0].id", 7)})));
        QVERIFY(HttpAssertions::passed(HttpAssertions::evaluate(
            {{"body", QJsonObject{{"data", QJsonArray{item}}}}}, {assertion("data.0.id", 7)})));
        QVERIFY(
            HttpAssertions::passed(HttpAssertions::evaluate(root, {assertion("$", QJsonArray{item})})));
        for (const auto &path :
             {QString("$[1].id"), QString("$[999999999999999999999].id"), QString("$[0].missing")}) {
            const QJsonArray rules{assertion(path, 7)};
            QVERIFY(HttpAssertions::validate(rules).isEmpty());
            QVERIFY(!HttpAssertions::passed(HttpAssertions::evaluate(root, rules)));
        }
        for (const auto &path :
             {QString("$[-1].id"), QString("$[1.5].id"), QString("$[].id"), QString("$[*].id"),
              QString("$[0]id"), QString("$[0].id\n"), QString("$[0].")})
            QVERIFY2(!HttpAssertions::validate({assertion(path, 7)}).isEmpty(), qPrintable(path));
        const auto longest = QString("$[0].") + QString(507, 'a');
        const QJsonObject longBody{{"body", QJsonArray{QJsonObject{{QString(507, 'a'), 7}}}}};
        QVERIFY(HttpAssertions::passed(HttpAssertions::evaluate(longBody, {assertion(longest, 7)})));
        QVERIFY(!HttpAssertions::validate({assertion(longest + 'a', 7)}).isEmpty());
    }
    void realRootArrayAssertionsPersistAndRun() {
        Server server;
        QTemporaryDir dir;
        QSettings settings(dir.filePath("root-array.ini"), QSettings::IniFormat);
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, &settings);
        QString error;
        auto doc = request(server.base() + "/array");
        doc["assertions"] = QJsonArray{QJsonObject{{"enabled", true},
                                                   {"source", "json"},
                                                   {"path", "$[0].id"},
                                                   {"relation", "equals"},
                                                   {"expected", 7}},
                                       QJsonObject{{"enabled", true},
                                                   {"source", "json"},
                                                   {"path", "$[0].items[0][0].id"},
                                                   {"relation", "equals"},
                                                   {"expected", 8}}};
        QVERIFY2(page.loadDraft(doc, &error), qPrintable(error));
        QVERIFY(page.saveDraft(&error));
        QCOMPARE(server.requests.size(), 0);
        const auto id = page.exportDraft(&error).value("id").toString();
        page.triggerSend();
        QTRY_VERIFY_WITH_TIMEOUT(!page.session()->active(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(page.assertionResult().value("passed").toBool(), 1000);
        QVERIFY(page.session()->latestResponse().value("body").isArray());
        QVERIFY(page.startHttpSequence({id, id}, false, &error));
        QTRY_VERIFY_WITH_TIMEOUT(!page.sequenceRunner()->running(), 5000);
        QCOMPARE(server.requests.size(), 3);
        QCOMPARE(page.sequenceRunner()->report().value("counts").toObject().value("passed").toInt(), 2);
    }
    void expandedAssertionsRevalidateScalarAndNestedBudget() {
        HttpProjectStore store(nullptr);
        QString error;
        QVERIFY(store.setEnvironmentVariables(
            {variable("large", QString(20000, 'x')), variable("small", "ordinary-value")}, &error));
        auto assertion = [](QJsonValue expected) {
            return QJsonObject{{"enabled", true},
                               {"source", "json"},
                               {"path", "$.text"},
                               {"relation", "equals"},
                               {"expected", expected}};
        };
        for (const QJsonValue &expected :
             {QJsonValue("{{large}}"), QJsonValue(QJsonObject{{"value", "{{large}}"}}),
              QJsonValue(QJsonArray{QJsonObject{{"value", "{{large}}"}}})}) {
            QVERIFY(HttpAssertions::validate({assertion(expected)}).isEmpty());
            QVERIFY(HttpAssertions::resolve({assertion(expected)}, store, &error).isEmpty());
            QVERIFY(error.contains("64KiB"));
        }
        for (const QJsonValue &expected :
             {QJsonValue("{{small}}"), QJsonValue(QJsonObject{{"value", "{{small}}"}})}) {
            const auto resolved = HttpAssertions::resolve({assertion(expected)}, store, &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(resolved.size(), 1);
            QVERIFY(HttpAssertions::validate(resolved).isEmpty());
        }
    }
    void expandedAssertionFailurePreservesRawBeforeConfirmation() {
        Server server;
        SessionController raw;
        MainWindow window(&raw);
        window.show();
        ConnectionConfig config;
        config.kind = TransportKind::Udp;
        config.localAddress = "127.0.0.1";
        config.localPort = 0;
        raw.start(config);
        QTRY_VERIFY_WITH_TIMEOUT(raw.connected(), 3000);
        const auto epoch = raw.sessionEpoch();
        widget<QPushButton>(window, "httpWorkspaceMode")->click();
        auto *page = widget<ProtocolDebugPage>(window, "httpDebugPage");
        QString error;
        QVERIFY(page->projectStore()->setEnvironmentVariables({variable("large", QString(20000, 'x'))},
                                                              &error));
        auto doc = request(server.base() + "/never");
        doc["assertions"] = QJsonArray{QJsonObject{{"enabled", true},
                                                   {"source", "json"},
                                                   {"path", "$.text"},
                                                   {"relation", "equals"},
                                                   {"expected", "{{large}}"}}};
        QVERIFY(page->loadDraft(doc, &error));
        QVERIFY(page->saveDraft(&error));
        const auto id = page->exportDraft(&error).value("id").toString();
        int confirmations = 0;
        QTimer dismiss;
        dismiss.setInterval(10);
        connect(&dismiss, &QTimer::timeout, &dismiss, [&] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box || box->objectName() != "manualProtocolResourceConfirmation")
                return;
            ++confirmations;
            for (auto *button : box->buttons())
                if (box->buttonRole(button) == QMessageBox::RejectRole) {
                    button->click();
                    break;
                }
        });
        dismiss.start();
        QVERIFY(!page->startHttpSequence({id}, false, &error));
        QVERIFY(error.contains("64KiB"));
        page->triggerSend();
        QTest::qWait(80);
        dismiss.stop();
        QCOMPARE(confirmations, 0);
        QCOMPARE(server.requests.size(), 0);
        QVERIFY(raw.connected());
        QCOMPARE(raw.sessionEpoch(), epoch);
        QVERIFY(!page->session()->active());
        QVERIFY(widget<QLabel>(*page, "protocolWarning")->text().contains("64KiB"));
    }
    void expandedAssertionFailureInLaterStepHonorsPolicies() {
        Server server;
        QTemporaryDir dir;
        QSettings settings(dir.filePath("later-budget.ini"), QSettings::IniFormat);
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, &settings);
        QString error;
        QVERIFY(page.projectStore()->setEnvironmentVariables({variable("large", QString(20000, 'x'))},
                                                             &error));
        auto invalid = request(server.base() + "/never");
        invalid["assertions"] = QJsonArray{QJsonObject{{"enabled", true},
                                                       {"source", "json"},
                                                       {"path", "$.text"},
                                                       {"relation", "equals"},
                                                       {"expected", "{{large}}"}}};
        QStringList ids;
        for (auto doc :
             {request(server.base() + "/login"), invalid, request(server.base() + "/business")}) {
            doc.remove("id");
            QVERIFY(page.loadDraft(doc, &error));
            QVERIFY(page.saveDraft(&error));
            ids << page.exportDraft(&error).value("id").toString();
        }
        QVERIFY(page.startHttpSequence(ids, false, &error));
        QTRY_VERIFY_WITH_TIMEOUT(!page.sequenceRunner()->running(), 5000);
        QCOMPARE(server.requests.size(), 1);
        auto counts = page.sequenceRunner()->report().value("counts").toObject();
        QCOMPARE(counts.value("passed").toInt(), 1);
        QCOMPARE(counts.value("failed").toInt(), 1);
        QCOMPARE(counts.value("skipped").toInt(), 1);
        QVERIFY(page.startHttpSequence(ids, true, &error));
        QTRY_VERIFY_WITH_TIMEOUT(!page.sequenceRunner()->running(), 5000);
        QCOMPARE(server.requests.size(), 3);
        QCOMPARE(server.requests.last().value("uri").toString(), QString("/business"));
        counts = page.sequenceRunner()->report().value("counts").toObject();
        QCOMPARE(counts.value("passed").toInt(), 2);
        QCOMPARE(counts.value("failed").toInt(), 1);
        QCOMPARE(counts.value("skipped").toInt(), 0);
        for (const auto &wire : server.requests)
            QVERIFY(wire.value("uri").toString() != "/never");
        QVERIFY(
            !QJsonDocument(page.sequenceRunner()->report()).toJson().contains(QByteArray(20000, 'x')));
    }
    void manualAssertionsPersistAndAreSilent() {
        Server server;
        QTemporaryDir dir;
        QSettings settings(dir.filePath("assertions.ini"), QSettings::IniFormat);
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, &settings);
        QString error;
        auto doc = request(server.base() + "/login");
        doc["assertions"] = QJsonArray{
            QJsonObject{
                {"enabled", true}, {"source", "status"}, {"relation", "equals"}, {"expected", 200}},
            QJsonObject{{"enabled", true},
                        {"source", "json"},
                        {"path", "$.data.access_token"},
                        {"relation", "exists"}}};
        QVERIFY(page.loadDraft(doc, &error));
        QVERIFY(page.saveDraft(&error));
        QCOMPARE(server.requests.size(), 0);
        QCOMPARE(page.exportDraft(&error).value("assertions").toArray().size(), 2);
        page.triggerSend();
        QTRY_VERIFY_WITH_TIMEOUT(!page.session()->active(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(page.assertionResult().value("passed").toBool(), 1000);
        QVERIFY(!QJsonDocument(page.assertionResult()).toJson().contains(server.token.toUtf8()));
        auto bad = doc;
        bad["assertions"] = "invalid";
        QVERIFY(!page.loadDraft(bad, &error));
    }
    void sequenceRealLoginAndFailurePolicies() {
        Server server;
        QTemporaryDir dir;
        QSettings settings(dir.filePath("sequence.ini"), QSettings::IniFormat);
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, &settings);
        auto *store = page.projectStore();
        QString error;
        QVERIFY(store->setEnvironmentVariables({variable("base_url", server.base())}, &error));
        QVERIFY(store->setProjectAuth({{"kind", "bearer"}, {"token", "{{access_token}}"}}, &error));
        auto login = request("{{base_url}}/login", "登录");
        login["extractionRows"] = QJsonArray{
            QJsonObject{{"enabled", true}, {"key", "access_token"}, {"value", "$.data.access_token"}}};
        login["assertions"] = QJsonArray{QJsonObject{
            {"enabled", true}, {"source", "status"}, {"relation", "equals"}, {"expected", 200}}};
        auto business = request("{{base_url}}/business", "查询");
        auto edit = business.value("editor").toObject();
        edit["authKind"] = 3;
        business["editor"] = edit;
        auto failure = request("{{base_url}}/failure", "失败");
        QJsonArray saved;
        for (auto r : {login, business, failure}) {
            r.remove("id");
            QVERIFY(page.loadDraft(r, &error));
            QVERIFY(page.saveDraft(&error));
            saved.append(page.exportDraft(&error));
        }
        QStringList ids;
        for (const auto &r : saved)
            ids << r.toObject().value("id").toString();
        QVERIFY(page.startHttpSequence(ids, false, &error));
        QTRY_VERIFY_WITH_TIMEOUT(!page.sequenceRunner()->running(), 5000);
        QCOMPARE(server.requests.size(), 3);
        QCOMPARE(server.requests[1].value("headers").toObject().value("authorization").toString(),
                 "Bearer " + server.token);
        auto report = page.sequenceRunner()->report();
        QCOMPARE(report.value("counts").toObject().value("passed").toInt(), 2);
        QCOMPARE(report.value("counts").toObject().value("failed").toInt(), 1);
        QVERIFY(!QJsonDocument(report).toJson().contains(server.token.toUtf8()));
        QVERIFY(!page.session()->active());
        QVERIFY(page.startHttpSequence({ids[2], ids[1]}, false, &error));
        QTRY_VERIFY_WITH_TIMEOUT(!page.sequenceRunner()->running(), 5000);
        QCOMPARE(page.sequenceRunner()->report().value("counts").toObject().value("skipped").toInt(),
                 1);
        const int count = server.requests.size();
        QVERIFY(page.startHttpSequence({ids[2], ids[1]}, true, &error));
        QTRY_VERIFY_WITH_TIMEOUT(!page.sequenceRunner()->running(), 5000);
        QCOMPARE(server.requests.size(), count + 2);
        QCOMPARE(page.sequenceRunner()->report().value("counts").toObject().value("passed").toInt(), 1);
    }
    void sequenceCancellationLocksAndLateResponse() {
        Server server;
        QTemporaryDir dir;
        QSettings settings(dir.filePath("cancel-sequence.ini"), QSettings::IniFormat);
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, &settings);
        QString error;
        auto late = request(server.base() + "/late");
        late["extractionRows"] = QJsonArray{
            QJsonObject{{"enabled", true}, {"key", "access_token"}, {"value", "$.data.access_token"}}};
        QVERIFY(page.loadDraft(late, &error));
        QVERIFY(page.saveDraft(&error));
        const auto id = page.exportDraft(&error).value("id").toString();
        QVERIFY(page.startHttpSequence({id, id}, false, &error));
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 1, 3000);
        QVERIFY(!widget<QComboBox>(page, "httpEnvironmentChoice")->isEnabled());
        QVERIFY(!page.loadDraft(request(server.base() + "/other"), &error));
        page.session()->cancel();
        QTRY_VERIFY_WITH_TIMEOUT(!page.sequenceRunner()->running(), 1000);
        QTest::qWait(450);
        QCOMPARE(server.requests.size(), 1);
        QVERIFY(page.projectStore()->runtimeVariables().isEmpty());
        QCOMPARE(page.sequenceRunner()->report().value("counts").toObject().value("cancelled").toInt(),
                 1);
        QCOMPARE(page.sequenceRunner()->report().value("counts").toObject().value("skipped").toInt(),
                 1);
        QVERIFY(widget<QComboBox>(page, "httpEnvironmentChoice")->isEnabled());
    }
    void sequenceContextMutationAndFirstValidationPreserveActivity() {
        Server server;
        QTemporaryDir dir;
        QSettings settings(dir.filePath("context-sequence.ini"), QSettings::IniFormat);
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, &settings);
        QString error;
        QVERIFY(page.loadDraft(request(server.base() + "/late"), &error));
        QVERIFY(page.saveDraft(&error));
        const auto id = page.exportDraft(&error).value("id").toString();
        QVERIFY(page.startHttpSequence({id, id}, false, &error));
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 1, 3000);
        QVERIFY(page.projectStore()->createEnvironment("外部变更", &error));
        QTRY_VERIFY_WITH_TIMEOUT(!page.sequenceRunner()->running(), 2000);
        QCOMPARE(server.requests.size(), 1);
        QCOMPARE(page.sequenceRunner()->report().value("counts").toObject().value("failed").toInt(), 1);
        QVERIFY(!page.session()->active());
        QVERIFY(!page.startHttpSequence({"missing-request"}, false, &error));
    }
    void sequenceResourceConfirmationRevalidatesBeforeStoppingRaw() {
        Server server;
        SessionController raw;
        MainWindow window(&raw);
        window.show();
        ConnectionConfig config;
        config.kind = TransportKind::Udp;
        config.localAddress = "127.0.0.1";
        config.localPort = 0;
        raw.start(config);
        QTRY_VERIFY_WITH_TIMEOUT(raw.connected(), 3000);
        widget<QPushButton>(window, "httpWorkspaceMode")->click();
        auto *page = widget<ProtocolDebugPage>(window, "httpDebugPage");
        QString error;
        QVERIFY(page->loadDraft(request(server.base() + "/login"), &error));
        QVERIFY(page->saveDraft(&error));
        const auto id = page->exportDraft(&error).value("id").toString();
        QTimer answer;
        answer.setInterval(10);
        connect(&answer, &QTimer::timeout, &answer, [&] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box || box->objectName() != "manualProtocolResourceConfirmation")
                return;
            answer.stop();
            page->projectStore()->createEnvironment("确认期间变更");
            for (auto *b : box->buttons())
                if (box->buttonRole(b) == QMessageBox::AcceptRole) {
                    b->click();
                    break;
                }
        });
        answer.start();
        QVERIFY(page->startHttpSequence({id, id}, false, &error));
        QVERIFY(raw.connected());
        QVERIFY(!page->session()->active());
        QCOMPARE(server.requests.size(), 0);
        QCOMPARE(page->sequenceRunner()->report().value("counts").toObject().value("failed").toInt(),
                 1);
        QCOMPARE(page->sequenceRunner()->report().value("counts").toObject().value("skipped").toInt(),
                 1);
    }
    void sequenceStepBoundaryKeepsResourceAndFrozenRequests() {
        Server server;
        QTemporaryDir dir;
        QSettings settings(dir.filePath("boundary.ini"), QSettings::IniFormat);
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, &settings);
        QString error;
        QVERIFY(page.loadDraft(request(server.base() + "/login"), &error));
        QVERIFY(page.saveDraft(&error));
        const auto id = page.exportDraft(&error).value("id").toString();
        bool observed = false;
        connect(page.sequenceRunner(), &HttpSequenceRunner::changed, &page, [&] {
            if (!page.sequenceRunner()->running() || page.sequenceRunner()->results().isEmpty() ||
                observed)
                return;
            observed = true;
            QVERIFY(page.session()->active());
            QVERIFY(page.session()->phase() == ProtocolDebugSession::Phase::Idle);
            QVERIFY(
                !page.session()->start({{"url", server.base() + "/unauthorized-interleave"}}, &error));
            page.sequenceRunner()->stop();
        });
        QVERIFY(page.startHttpSequence({id, id}, false, &error));
        QTRY_VERIFY_WITH_TIMEOUT(!page.sequenceRunner()->running(), 3000);
        QVERIFY(observed);
        QCOMPARE(server.requests.size(), 1);
        QCOMPARE(page.sequenceRunner()->report().value("counts").toObject().value("skipped").toInt(),
                 1);
    }
    void sequenceAcceptedResourceConfirmationDoesNotCompleteDuringDialog() {
        Server server;
        SessionController raw;
        MainWindow window(&raw);
        window.show();
        ConnectionConfig config;
        config.kind = TransportKind::Udp;
        config.localAddress = "127.0.0.1";
        config.localPort = 0;
        raw.start(config);
        QTRY_VERIFY_WITH_TIMEOUT(raw.connected(), 3000);
        widget<QPushButton>(window, "httpWorkspaceMode")->click();
        auto *page = widget<ProtocolDebugPage>(window, "httpDebugPage");
        QString error;
        QVERIFY(page->loadDraft(request(server.base() + "/login"), &error));
        QVERIFY(page->saveDraft(&error));
        const auto id = page->exportDraft(&error).value("id").toString();
        QTimer answer;
        answer.setInterval(50);
        connect(&answer, &QTimer::timeout, &answer, [&] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box || box->objectName() != "manualProtocolResourceConfirmation")
                return;
            answer.stop();
            QVERIFY(page->sequenceRunner()->running());
            QVERIFY(page->sequenceRunner()->results().isEmpty());
            QCOMPARE(server.requests.size(), 0);
            for (auto *b : box->buttons())
                if (box->buttonRole(b) == QMessageBox::AcceptRole) {
                    b->click();
                    break;
                }
        });
        answer.start();
        QVERIFY(page->startHttpSequence({id, id}, false, &error));
        QTRY_VERIFY_WITH_TIMEOUT(!page->sequenceRunner()->running(), 5000);
        QCOMPARE(server.requests.size(), 2);
        QCOMPARE(page->sequenceRunner()->report().value("counts").toObject().value("passed").toInt(),
                 2);
        QVERIFY(!raw.connected());
    }
    void queuedNextStepCannotSendAfterEscapeCancellation() {
        Server server;
        QTemporaryDir dir;
        QSettings settings(dir.filePath("queued-stop.ini"), QSettings::IniFormat);
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, &settings);
        QString error;
        QVERIFY(page.loadDraft(request(server.base() + "/login"), &error));
        QVERIFY(page.saveDraft(&error));
        const auto id = page.exportDraft(&error).value("id").toString();
        bool scheduled = false;
        connect(page.sequenceRunner(), &HttpSequenceRunner::changed, &page, [&] {
            if (!page.sequenceRunner()->running() || page.sequenceRunner()->results().isEmpty() ||
                scheduled)
                return;
            scheduled = true;
            QTimer::singleShot(0, &page, [&] { page.session()->cancel(); });
        });
        QVERIFY(page.startHttpSequence({id, id}, false, &error));
        QTRY_VERIFY_WITH_TIMEOUT(!page.sequenceRunner()->running(), 3000);
        QTest::qWait(100);
        QVERIFY(scheduled);
        QCOMPARE(server.requests.size(), 1);
        QCOMPARE(page.sequenceRunner()->report().value("counts").toObject().value("passed").toInt(), 1);
        QCOMPARE(page.sequenceRunner()->report().value("counts").toObject().value("skipped").toInt(),
                 1);
        QVERIFY(!page.session()->start(
            {{"url", server.base() + "/orphan"}, {"httpSequenceId", "expired-run"}}, &error));
        QTest::qWait(80);
        QCOMPARE(server.requests.size(), 1);
    }
    void nativeSequenceScreenshots() {
        const auto target = qEnvironmentVariable("PORTBRIDGE_HTTP_PROJECT_SCREENSHOT_DIR");
        if (target.isEmpty())
            QSKIP("Native sequence screenshots run separately.");
        QDir().mkpath(target);
        Server server;
        QTemporaryDir dir;
        QSettings settings(dir.filePath("screens-sequence.ini"), QSettings::IniFormat);
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, &settings);
        page.setAttribute(Qt::WA_StyledBackground, true);
        page.resize(1040, 660);
        SessionController raw;
        MainWindow theme(&raw);
        QString error;
        auto login = request(server.base() + "/login", "登录并提取token");
        login.remove("id");
        login["assertions"] = QJsonArray{
            QJsonObject{
                {"enabled", true}, {"source", "status"}, {"relation", "equals"}, {"expected", 200}},
            QJsonObject{{"enabled", true},
                        {"source", "json"},
                        {"path", "$.data.access_token"},
                        {"relation", "exists"}}};
        login["extractionRows"] = QJsonArray{
            QJsonObject{{"enabled", true}, {"key", "access_token"}, {"value", "$.data.access_token"}}};
        QVERIFY(page.loadDraft(login, &error));
        QVERIFY(page.saveDraft(&error));
        const auto id = page.exportDraft(&error).value("id").toString();
        page.setDarkTheme(true);
        page.setStyleSheet(theme.styleSheet() + page.styleSheet() +
                           "#httpDebugPage {background:#101618;}");
        page.show();
        QTest::qWait(150);
        widget<QTabWidget>(page, "protocolRequestTabs")->setCurrentIndex(6);
        QTest::qWait(80);
        QVERIFY(page.grab().save(target + "/assertions-dark.png"));
        QTimer capture;
        capture.setInterval(10);
        connect(&capture, &QTimer::timeout, &capture, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != "httpSequenceDialog")
                return;
            capture.stop();
            widget<QTableWidget>(*dialog, "httpSequenceSelection")
                ->item(0, 0)
                ->setCheckState(Qt::Checked);
            QTest::qWait(80);
            QVERIFY(dialog->grab().save(target + "/sequence-selection-dark.png"));
            dialog->reject();
        });
        capture.start();
        widget<QPushButton>(page, "httpSequenceRun")->click();
        QCOMPARE(server.requests.size(), 0);
        QVERIFY(page.startHttpSequence({id, id}, false, &error));
        QTRY_VERIFY_WITH_TIMEOUT(!page.sequenceRunner()->running(), 5000);
        QTest::qWait(120);
        QVERIFY(page.grab().save(target + "/sequence-results-dark.png"));
        QVERIFY(
            !widget<QPlainTextEdit>(page, "httpSequenceResults")->toPlainText().contains(server.token));
    }
    void browserFingerprintGeneratesStableScopedHeaders() {
        QTemporaryDir dir;
        QSettings settings(dir.filePath("fingerprint.ini"), QSettings::IniFormat);
        HttpProjectStore store(&settings);
        QString error;
        const auto first = httpFingerprint::generate("chrome", "Windows", "en-US");
        const auto second = httpFingerprint::generate("chrome", "Windows", "en-US", first);
        QVERIFY2(!first.isEmpty(), qPrintable(httpFingerprint::validate(first)));
        QVERIFY2(httpFingerprint::validate(first).isEmpty(),
                 qPrintable(httpFingerprint::validate(first)));
        QVERIFY(first.value("id").toString() != second.value("id").toString());
        QVERIFY(first.value("major").toInt() != second.value("major").toInt());
        const auto values = httpFingerprint::variables(first);
        QCOMPARE(values.value("browser_platform").toString(), "Windows");
        QVERIFY(values.value("browser_user_agent").toString().contains("Chrome/"));
        QVERIFY(values.value("browser_fingerprint")
                    .toObject()
                    .value("headers")
                    .toObject()
                    .contains("User-Agent"));
        QVERIFY(httpFingerprint::headerTemplates(first, true).contains("Sec-CH-UA"));
        QVERIFY(!httpFingerprint::headerTemplates(first, false).contains("Sec-CH-UA"));
        const auto firefox = httpFingerprint::generate("firefox", "Linux", "de-DE");
        QVERIFY(!httpFingerprint::headerTemplates(firefox, true).contains("Sec-CH-UA"));
        QVERIFY(store.setEnvironmentVariables({variable("base_url", "http://127.0.0.1:1")}, &error));
        QVERIFY(store.setBrowserFingerprint(first, &error));
        QVERIFY(store.effectiveVariables().contains("browser_user_agent"));
        QVERIFY(store.copyEnvironment("Fingerprint copy", &error));
        QCOMPARE(store.environment().value("browserFingerprint").toObject().value("browser").toString(),
                 "chrome");
        QVERIFY(store.setBrowserFingerprint({}, &error));
        QVERIFY(!store.effectiveVariables().contains("browser_user_agent"));
        QVERIFY2(store.selectEnvironment(store.project()
                                             .value("environments")
                                             .toArray()
                                             .first()
                                             .toObject()
                                             .value("id")
                                             .toString(),
                                         &error),
                 qPrintable(error));
        QVERIFY(store.effectiveVariables().contains("browser_user_agent"));
    }
    void browserFingerprintAddsHeadersAndManualHeaderWins() {
        Server server;
        QTemporaryDir dir;
        QSettings settings(dir.filePath("fingerprint-wire.ini"), QSettings::IniFormat);
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, &settings);
        auto *store = page.projectStore();
        QString error;
        QVERIFY(store->setEnvironmentVariables({variable("base_url", server.base())}, &error));
        const auto chrome = httpFingerprint::generate("chrome", "Windows", "en-US");
        QVERIFY(store->setBrowserFingerprint(chrome, &error));
        auto draft = request("{{base_url}}/business");
        QVERIFY(page.loadDraft(draft, &error));
        page.triggerSend();
        QTRY_VERIFY_WITH_TIMEOUT(!page.session()->active() && server.received.size() == 1, 5000);
        const auto first = server.received.last().value("headers").toObject();
        QVERIFY(first.value("user-agent").toString().contains("Chrome/"));
        QCOMPARE(first.value("accept-language").toString(), "en-US,en;q=0.9");
        QVERIFY(!first.contains("sec-ch-ua"));
        row(widget<QTableWidget>(page, "protocolHeaders"), "User-Agent", "PortBridge-Test/1.0");
        page.triggerSend();
        QTRY_VERIFY_WITH_TIMEOUT(!page.session()->active() && server.received.size() == 2, 5000);
        QCOMPARE(server.received.last().value("headers").toObject().value("user-agent").toString(),
                 "PortBridge-Test/1.0");
        QVERIFY(page.exportDraft(&error).value("editor").toObject().value("headers").toArray().size() ==
                1);
        QVERIFY(!settings.value("manual/httpProjectsV2").toByteArray().contains("PortBridge-Test"));
        QVERIFY(store->createEnvironment("Firefox env", &error));
        const auto firefox = httpFingerprint::generate("firefox", "Linux", "de-DE");
        QVERIFY(store->setBrowserFingerprint(firefox, &error));
        QCOMPARE(store->effectiveVariables().value("browser_name").toString(), "firefox");
        QVERIFY(!store->effectiveVariables().contains("browser_sec_ch_ua"));
    }
    void browserFingerprintDialogEditsAndCancelsAtomically() {
        QTemporaryDir dir;
        QSettings settings(dir.filePath("fingerprint-dialog.ini"), QSettings::IniFormat);
        HttpProjectStore store(&settings);
        QString error;
        const auto original = httpFingerprint::generate("edge", "Windows", "zh-CN");
        QVERIFY(store.setBrowserFingerprint(original, &error));
        const auto before = store.project();
        const auto revision = store.revision();
        {
            HttpConfigurationDialog dialog(&store, 4);
            dialog.show();
            QCOMPARE(widget<QCheckBox>(dialog, "httpBrowserFingerprintEnabled")->isChecked(), true);
            QVERIFY(widget<QLabel>(dialog, "httpBrowserFingerprintSummary")->text().contains("Edge"));
            widget<QPushButton>(dialog, "httpBrowserFingerprintGenerate")->click();
            QVERIFY(
                widget<QLabel>(dialog, "httpBrowserFingerprintSummary")->text().contains("自动请求头"));
            dialog.reject();
        }
        QCOMPARE(store.project(), before);
        QCOMPARE(store.revision(), revision);
        {
            HttpConfigurationDialog dialog(&store, 4);
            dialog.show();
            widget<QPushButton>(dialog, "httpBrowserFingerprintClear")->click();
            QVERIFY(!widget<QCheckBox>(dialog, "httpBrowserFingerprintEnabled")->isChecked());
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
            QCOMPARE(dialog.result(), int(QDialog::Accepted));
        }
        QVERIFY(store.environment().value("browserFingerprint").toObject().isEmpty());
        QVERIFY(!store.effectiveVariables().contains("browser_user_agent"));
        {
            HttpConfigurationDialog dialog(&store, 4);
            dialog.show();
            widget<QPushButton>(dialog, "httpBrowserFingerprintGenerate")->click();
            QVERIFY(widget<QCheckBox>(dialog, "httpBrowserFingerprintEnabled")->isChecked());
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
            QCOMPARE(dialog.result(), int(QDialog::Accepted));
        }
        const auto saved = store.environment().value("browserFingerprint").toObject();
        QVERIFY(!saved.isEmpty());
        QVERIFY(httpFingerprint::validate(saved).isEmpty());
        HttpProjectStore again(&settings);
        QCOMPARE(again.environment().value("browserFingerprint").toObject(), saved);
    }
    void browserFingerprintHttpsClientHintsUseActualWire() {
        QTemporaryDir dir;
        const auto ca = dir.filePath("ca.pem"), keyPath = dir.filePath("key.pem");
        QString openssl = qEnvironmentVariable("PORTBRIDGE_TEST_OPENSSL");
        if (openssl.isEmpty())
            openssl = QStandardPaths::findExecutable("openssl");
#ifdef Q_OS_WIN
        if (openssl.isEmpty())
            openssl = qEnvironmentVariable("ProgramFiles") + "/Git/mingw64/bin/openssl.exe";
#endif
        QVERIFY2(QFile::exists(openssl), "OpenSSL CLI is required for the isolated HTTPS fixture");
        QProcess generator;
        generator.start(openssl, {"req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", keyPath,
                                  "-out", ca, "-days", "2", "-subj", "/CN=localhost", "-addext",
                                  "subjectAltName=DNS:localhost", "-addext",
                                  "basicConstraints=critical,CA:TRUE"});
        QVERIFY(generator.waitForFinished(10000));
        QCOMPARE(generator.exitCode(), 0);
        QFile certFile(ca), keyFile(keyPath);
        QVERIFY(certFile.open(QIODevice::ReadOnly));
        QVERIFY(keyFile.open(QIODevice::ReadOnly));
        HttpsServer server;
        server.certificate = QSslCertificate(certFile.readAll(), QSsl::Pem);
        server.key = QSslKey(keyFile.readAll(), QSsl::Rsa, QSsl::Pem);
        QVERIFY(QSslSocket::supportsSsl());
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QSettings settings(dir.filePath("tls.ini"), QSettings::IniFormat);
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, &settings);
        auto *store = page.projectStore();
        QString error;
        QVERIFY(store->setEnvironmentVariables({variable("base_url", server.base())}));
        auto draft = request("{{base_url}}/headers");
        auto params = draft.value("params").toObject();
        params["caFile"] = ca;
        draft["params"] = params;
        for (const auto &browser : {"chrome", "edge", "firefox"}) {
            const auto profile = httpFingerprint::generate(browser, "Windows", "zh-CN");
            QVERIFY(store->setBrowserFingerprint(profile, &error));
            QVERIFY(page.loadDraft(draft, &error));
            const auto count = server.requests.size();
            page.triggerSend();
            QTRY_VERIFY_WITH_TIMEOUT(!page.session()->active() && server.requests.size() == count + 1,
                                     5000);
            QVERIFY2(page.session()->lastError().isEmpty(), qPrintable(page.session()->lastError()));
            const auto headers = server.requests.last();
            const auto values = httpFingerprint::variables(profile);
            QCOMPARE(headers.value("user-agent"), values.value("browser_user_agent"));
            QCOMPARE(headers.value("accept-language"), values.value("browser_accept_language"));
            const auto major = QString::number(profile.value("major").toInt());
            if (QString(browser) == "firefox") {
                QVERIFY(headers.value("user-agent").toString().contains("rv:" + major + ".0"));
                QVERIFY(headers.value("user-agent").toString().endsWith("Firefox/" + major + ".0"));
                QVERIFY(!headers.contains("sec-ch-ua"));
            } else {
                QVERIFY(headers.value("user-agent").toString().contains("Chrome/" + major + ".0.0.0"));
                QVERIFY(
                    headers.value("sec-ch-ua").toString().contains("\"Chromium\";v=\"" + major + "\""));
                QCOMPARE(headers.value("sec-ch-ua"), values.value("browser_sec_ch_ua"));
                QCOMPARE(headers.value("sec-ch-ua-platform").toString(), "\"Windows\"");
                QCOMPARE(headers.value("sec-ch-ua-mobile").toString(), "?0");
            }
        }
        const auto last = store->environment().value("browserFingerprint").toObject();
        auto disabled = last;
        disabled["enabled"] = false;
        QVERIFY(store->setBrowserFingerprint(disabled));
        QVERIFY(store->effectiveVariables().contains("browser_user_agent"));
        QVERIFY(page.loadDraft(draft, &error));
        page.triggerSend();
        QTRY_VERIFY_WITH_TIMEOUT(!page.session()->active() && server.requests.size() == 4, 5000);
        QVERIFY(!server.requests.last().contains("accept-language"));
        QVERIFY(!server.requests.last().contains("sec-ch-ua"));
    }
    void browserFingerprintValidatesPersistsAndResolvesPrecedence() {
        QTemporaryDir dir;
        QSettings settings(dir.filePath("persist.ini"), QSettings::IniFormat);
        HttpProjectStore store(&settings);
        QString error;
        const auto profile = httpFingerprint::generate("edge", "macOS", "ja-JP");
        QVERIFY(store.setBrowserFingerprint(profile, &error));
        const auto snapshot = store.project(), runtime = store.runtimeVariables();
        const auto revision = store.revision();
        for (const auto &field :
             {"browser", "platform", "language", "version", "major", "enabled", "headers"}) {
            auto invalid = profile;
            invalid[field] = QString("invalid\r\nvalue");
            QVERIFY(!store.setBrowserFingerprint(invalid, &error));
            QCOMPARE(store.project(), snapshot);
            QCOMPARE(store.runtimeVariables(), runtime);
            QCOMPARE(store.revision(), revision);
        }
        auto fractional = profile;
        fractional["major"] = 143.5;
        QVERIFY(!store.setBrowserFingerprint(fractional, &error));
        HttpProjectStore restored(&settings);
        QCOMPARE(restored.environment().value("browserFingerprint").toObject(), profile);
        const auto exported = store.exportProject({});
        HttpProjectStore imported(nullptr);
        QJsonArray requests;
        QVERIFY2(imported.importProject(exported, &requests, &error), qPrintable(error));
        QCOMPARE(imported.environment().value("browserFingerprint").toObject(), profile);
        auto malformed = exported;
        auto p = malformed.value("project").toObject();
        auto envs = p.value("environments").toArray();
        auto env = envs.first().toObject();
        auto bad = profile;
        bad["headers"] = QJsonObject{{"Cookie", "secret"}};
        env["browserFingerprint"] = bad;
        envs[0] = env;
        p["environments"] = envs;
        malformed["project"] = p;
        const auto before = imported.projects();
        QVERIFY(!imported.importProject(malformed, &requests, &error));
        QCOMPARE(imported.projects(), before);
        QVERIFY(store.setProjectVariables({variable("browser_user_agent", "project-agent")}));
        QVERIFY(store.expand("{{browser_user_agent}}") != "project-agent");
        QVERIFY(store.setEnvironmentVariables({variable("browser_user_agent", "environment-agent")}));
        QCOMPARE(store.expand("{{browser_user_agent}}"), "environment-agent");
        QVERIFY(store.extract({{"status", 200}, {"body", QJsonObject{{"ua", "runtime-agent"}}}},
                              {rule("browser_user_agent", "$.ua")}, store.projectId(),
                              store.environmentId(), &error));
        QCOMPARE(store.expand("{{browser_user_agent}}"), "runtime-agent");
        store.clearRuntime();
        QCOMPARE(store.expand("{{browser_user_agent}}"), "environment-agent");
        QVERIFY(
            store.setEnvironmentVariables({variable("browser_user_agent", "unsafe\r\nInjected: yes")}));
        QVERIFY(resolveHttpRequest(request("https://localhost/"), store, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }
    void browserFingerprintSequenceKeepsProfileFixed() {
        Server server;
        QTemporaryDir dir;
        QSettings settings(dir.filePath("sequence.ini"), QSettings::IniFormat);
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, &settings);
        auto *store = page.projectStore();
        QString error;
        const auto profile = httpFingerprint::generate("chrome", "Linux", "en-US");
        QVERIFY(store->setEnvironmentVariables({variable("base_url", server.base())}));
        QVERIFY(store->setBrowserFingerprint(profile));
        auto first = request("{{base_url}}/one");
        first["id"] = "fp-step-one";
        first["projectId"] = store->projectId();
        auto second = request("{{base_url}}/two");
        second["id"] = "fp-step-two";
        second["projectId"] = store->projectId();
        QVERIFY(store->setRequests({first, second}, &error));
        QVERIFY2(page.startHttpSequence({"fp-step-one", "fp-step-two"}, false, &error),
                 qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(!page.sequenceRunner()->running() && server.received.size() == 2,
                                 5000);
        QCOMPARE(server.received[0].value("headers").toObject().value("user-agent"),
                 server.received[1].value("headers").toObject().value("user-agent"));
        QCOMPARE(store->environment().value("browserFingerprint").toObject(), profile);
    }
    void nativeBrowserFingerprintScreenshots() {
        const auto target = qEnvironmentVariable("PORTBRIDGE_BROWSER_FINGERPRINT_SCREENSHOT_DIR");
        if (target.isEmpty())
            QSKIP("Native browser fingerprint screenshots run separately.");
        QDir().mkpath(target);
        HttpProjectStore store(nullptr);
        QWidget parent;
        parent.resize(1180, 820);
        for (bool dark : {true, false}) {
            parent.setProperty("darkTheme", dark);
            for (const auto &browser : {"chrome", "edge", "firefox"}) {
                QVERIFY(store.setBrowserFingerprint(
                    httpFingerprint::generate(browser, "Windows", "zh-CN")));
                HttpConfigurationDialog dialog(&store, 4, &parent);
                dialog.show();
                QTest::qWait(80);
                QVERIFY(widget<QPushButton>(dialog, "httpBrowserFingerprintGenerate")->isVisible());
                QVERIFY(dialog.rect().contains(dialog.findChild<QDialogButtonBox *>()->geometry()));
                QVERIFY(dialog.grab().save(
                    target + QString("/%1-%2.png").arg(browser, dark ? "dark" : "light")));
                dialog.reject();
            }
        }
    }
    void configurationSaveIsAtomicScopedAndKeepsRuntimeSeparate() {
        QTemporaryDir dir;
        QSettings settings(dir.filePath("configuration.ini"), QSettings::IniFormat);
        HttpProjectStore store(&settings);
        QString error;
        QVERIFY(store.setProjectVariables({variable("region", "project-default")}));
        QVERIFY(store.setEnvironmentVariables({variable("region", "environment-default")}));
        QVERIFY(store.extract({{"status", 200}, {"body", QJsonObject{{"region", "response-private"}}}},
                              {rule("region", "$.region")}, store.projectId(), store.environmentId(),
                              &error));
        auto configuration = QJsonObject{
            {"projectName", "Shared API"},
            {"environmentName", "Local"},
            {"projectVariables", QJsonArray{variable("region", "new-project-default")}},
            {"environmentVariables", QJsonArray{variable("region", "new-environment-default"),
                                                variable("base_url", "http://localhost:9000")}},
            {"auth", QJsonObject{{"kind", "bearer"}, {"token", "{{access_token}}"}}}};
        const auto beforeProject = store.project(), beforeRuntime = store.runtimeVariables();
        const auto beforeDisk = settings.value("manual/httpProjectsV2").toByteArray();
        const auto revision = store.revision();
        auto invalid = configuration;
        invalid["environmentVariables"] = QJsonArray{variable("same", 1), variable("same", 2)};
        QVERIFY(!store.saveConfiguration(store.projectId(), store.environmentId(), revision, invalid,
                                         {"region"}, &error));
        QCOMPARE(store.project(), beforeProject);
        QCOMPARE(store.runtimeVariables(), beforeRuntime);
        QCOMPARE(store.revision(), revision);
        QCOMPARE(settings.value("manual/httpProjectsV2").toByteArray(), beforeDisk);
        QVERIFY2(store.saveConfiguration(store.projectId(), store.environmentId(), revision,
                                         configuration, {}, &error),
                 qPrintable(error));
        QCOMPARE(store.expand("{{region}}"), "response-private");
        QCOMPARE(
            store.environment().value("variables").toArray()[0].toObject().value("value").toString(),
            "new-environment-default");
        QVERIFY(!settings.value("manual/httpProjectsV2").toByteArray().contains("response-private"));
        QVERIFY(!store.saveConfiguration(store.projectId(), store.environmentId(), revision,
                                         configuration, {}, &error));
        QVERIFY(store.saveConfiguration(store.projectId(), store.environmentId(), store.revision(),
                                        configuration, {"region"}, &error));
        QCOMPARE(store.expand("{{region}}"), "new-environment-default");
        QVERIFY(store.createEnvironment("Other"));
        QVERIFY(!store.effectiveVariables().contains("base_url"));
        QCOMPARE(store.expand("{{region}}"), "new-project-default");
        HttpProjectStore restored(&settings);
        QCOMPARE(restored.project().value("name").toString(), "Shared API");
        QVERIFY(restored.runtimeVariables().isEmpty());
    }
    void configurationDialogCancelSaveAndSensitiveDefinitions() {
        HttpProjectStore store(nullptr);
        QString error;
        QVERIFY(store.setEnvironmentVariables({variable("base_url", "http://localhost:8000"),
                                               variable("access_token", "synthetic-private", true)}));
        QVERIFY(store.setProjectAuth({{"kind", "bearer"}, {"token", "{{access_token}}"}}));
        const auto p = store.project(), runtime = store.runtimeVariables();
        const auto revision = store.revision();
        {
            HttpConfigurationDialog dialog(&store, 0);
            dialog.show();
            auto *table = widget<QTableWidget>(dialog, "httpVariableTable");
            QCOMPARE(table->rowCount(), 1);
            QCOMPARE(table->item(0, 0)->text(), "access_token");
            auto *value = qobject_cast<QLineEdit *>(table->cellWidget(0, 2));
            QVERIFY(value);
            QVERIFY(value->text().isEmpty());
            QCOMPARE(value->echoMode(), QLineEdit::Password);
            widget<QLineEdit>(dialog, "httpEnvironmentBaseUrl")->setText("http://localhost:9000");
            widget<QLineEdit>(dialog, "httpSettingsProjectName")->setText("Changed");
            widget<QTableWidget>(dialog, "httpEffectiveVariables")->setCurrentCell(0, 0);
            widget<QPushButton>(dialog, "httpRuntimeDelete")->click();
            dialog.reject();
        }
        QCOMPARE(store.project(), p);
        QCOMPARE(store.runtimeVariables(), runtime);
        QCOMPARE(store.revision(), revision);
        HttpConfigurationDialog dialog(&store, 2);
        dialog.show();
        auto *kind = widget<QComboBox>(dialog, "httpProjectAuthKind");
        auto *token = widget<QLineEdit>(dialog, "httpProjectAuthToken");
        auto *user = widget<QLineEdit>(dialog, "httpProjectAuthUsername");
        QVERIFY(token->isVisible());
        QVERIFY(!user->isVisible());
        kind->setCurrentIndex(2);
        QVERIFY(user->isVisible());
        QVERIFY(!token->isVisible());
        kind->setCurrentIndex(0);
        QVERIFY(!user->isVisible());
        QVERIFY(!token->isVisible());
        kind->setCurrentIndex(1);
        token->setText("Bearer mistaken-prefix");
        auto *save = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save);
        save->click();
        QCOMPARE(dialog.result(), 0);
        QCOMPARE(store.revision(), revision);
        QVERIFY(!widget<QLabel>(dialog, "httpSettingsError")->text().isEmpty());
        token->setText("{{access_token}}");
        widget<QLineEdit>(dialog, "httpEnvironmentBaseUrl")->setText("http://localhost:9000");
        save->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(store.expand("{{base_url}}"), "http://localhost:9000");
        QCOMPARE(store.expand("{{access_token}}"), "synthetic-private");
        QVERIFY(!store.environment().value("variables").toArray().first().toObject().contains("value"));
    }
    void configurationWriteFailureKeepsDefinitionsAndRuntime() {
        QTemporaryDir dir;
        const auto path = dir.filePath("blocked.ini");
        QSettings settings(path, QSettings::IniFormat);
        HttpProjectStore store(&settings);
        QVERIFY(store.setEnvironmentVariables({variable("access_token", "old-private", true)}));
        const auto project = store.project(), runtime = store.runtimeVariables();
        const auto revision = store.revision();
        const auto diskIntent = settings.value("manual/httpProjectsV2").toByteArray();
        // Replace only this test's file with a directory to force a real QSettings write failure.
        QVERIFY(QFile::remove(path));
        QVERIFY(QDir().mkdir(path));
        QString error;
        const QJsonObject configuration{
            {"projectName", "Changed"},
            {"environmentName", "Changed"},
            {"projectVariables", QJsonArray{variable("shared", 1)}},
            {"environmentVariables", QJsonArray{variable("access_token", "new-private", true)}},
            {"auth",
             QJsonObject{{"kind", "basic"}, {"username", "new-user"}, {"password", "new-password"}}}};
        QVERIFY(!store.saveConfiguration(store.projectId(), store.environmentId(), revision,
                                         configuration, {"access_token"}, &error));
        QCOMPARE(store.project(), project);
        QCOMPARE(store.runtimeVariables(), runtime);
        QCOMPARE(store.revision(), revision);
        QCOMPARE(settings.value("manual/httpProjectsV2").toByteArray(), diskIntent);
        QVERIFY(settings.status() != QSettings::NoError);
        QVERIFY(error.contains(QStringLiteral("写入失败")));
    }
    void auditRequestScopeEmptyStatesAndAddressStatus() {
        Server server;
        QTemporaryDir dir;
        QSettings settings(dir.filePath("audit-scope.ini"), QSettings::IniFormat);
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, &settings);
        page.show();
        auto *store = page.projectStore();
        auto *panel = widget<HttpProjectPanel>(page, "httpProjectPanel");
        QString error;
        for (const auto &value :
             {QJsonValue(""), QJsonValue(3), QJsonValue("https://{{missing_host}}")}) {
            QVERIFY(store->setEnvironmentVariables({variable("base_url", value)}, &error));
            panel->refresh();
            QVERIFY(!widget<QLabel>(page, "httpRuntimeStatus")->text().contains("已配置"));
        }
        QVERIFY(store->setEnvironmentVariables({variable("base_url", server.base())}, &error));
        panel->refresh();
        QVERIFY(widget<QLabel>(page, "httpRuntimeStatus")->text().contains("已配置"));
        QCOMPARE(server.received.size(), 0);
        QVERIFY(store->addFolder("认证", &error));
        QVERIFY(store->addFolder("业务", &error));
        panel->refresh();
        auto *filter = widget<QComboBox>(page, "httpFolderFilter");
        filter->setCurrentIndex(filter->findData("业务"));
        QVERIFY2(page.createSavedRequest("查询订单", "", &error), qPrintable(error));
        QCOMPARE(store->requests().last().toObject().value("folder").toString(), "业务");
        QCOMPARE(widget<QListWidget>(page, "protocolLibrary")->count(), 1);
        filter->setCurrentIndex(filter->findData("认证"));
        QCOMPARE(widget<QListWidget>(page, "protocolLibrary")->count(), 0);
        QVERIFY(widget<QLabel>(page, "protocolLibraryEmpty")->text().contains("当前分类"));
        filter->setCurrentIndex(filter->findData("*"));
        QCOMPARE(widget<QListWidget>(page, "protocolLibrary")->count(), 1);
        widget<QLineEdit>(page, "protocolLibrarySearch")->setText("missing-request");
        QVERIFY(widget<QLabel>(page, "protocolLibraryEmpty")->text().contains("没有匹配"));
        widget<QLineEdit>(page, "protocolLibrarySearch")->clear();
        widget<QPushButton>(page, "protocolNew")->click();
        QCOMPARE(store->requests().size(), 1);
        widget<QLineEdit>(page, "protocolName")->setText("未保存的草稿");
        QVERIFY(page.dirty());
        QCOMPARE(store->requests().size(), 1);
        QVERIFY(page.saveDraft(&error));
        QCOMPARE(store->requests().size(), 2);
        QVERIFY(store->setEnvironmentVariables({variable("base_url", "")}, &error));
        page.triggerSend();
        QVERIFY(!page.session()->active());
        QVERIFY(widget<QLabel>(page, "protocolWarning")->text().contains("配置环境"));
        QCOMPARE(server.received.size(), 0);
    }
    void newProjectConfiguresEnvironmentAndRequestUsesAddressTemplate() {
        Server first, second;
        QTemporaryDir dir;
        QSettings settings(dir.filePath("project-flow.ini"), QSettings::IniFormat);
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, &settings);
        page.show();
        auto *store = page.projectStore();
        auto *panel = widget<HttpProjectPanel>(page, "httpProjectPanel");
        const auto previousCount = store->projects().size();
        int phase = 0;
        QTimer setup;
        setup.setInterval(10);
        connect(&setup, &QTimer::timeout, &page, [&] {
            auto *active = QApplication::activeModalWidget();
            if (phase == 0) {
                auto *prompt = qobject_cast<QInputDialog *>(active);
                if (!prompt)
                    return;
                prompt->setTextValue("订单服务");
                ++phase;
                prompt->accept();
            } else if (phase == 1) {
                auto *dialog = dynamic_cast<HttpConfigurationDialog *>(active);
                if (!dialog)
                    return;
                QVERIFY(widget<QLabel>(*dialog, "httpSettingsCreationNotice")->isVisible());
                QVERIFY(widget<QLabel>(*dialog, "httpSettingsCreationNotice")
                            ->text()
                            .contains("不删除新项目"));
                QCOMPARE(widget<QTabWidget>(*dialog, "httpSettingsTabs")->currentIndex(), 0);
                widget<QLineEdit>(*dialog, "httpSettingsEnvironmentName")->setText("本地开发");
                widget<QLineEdit>(*dialog, "httpEnvironmentBaseUrl")->setText(first.base());
                ++phase;
                setup.stop();
                dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
            }
        });
        setup.start();
        panel->findChild<QAction *>("httpProjectNew")->trigger();
        QCOMPARE(phase, 2);
        QCOMPARE(store->projects().size(), previousCount + 1);
        QCOMPARE(store->project().value("name").toString(), "订单服务");
        QCOMPARE(store->environment().value("name").toString(), "本地开发");
        QCOMPARE(first.received.size(), 0);
        QCOMPARE(second.received.size(), 0);
        QString error;
        QVERIFY2(page.createSavedRequest("查询订单", "", &error), qPrintable(error));
        QCOMPARE(widget<QLineEdit>(page, "protocolUrl")->text(), "{{base_url}}/");
        widget<QLineEdit>(page, "protocolUrl")->setText("{{base_url}}/health");
        QVERIFY(page.saveDraft(&error));
        page.triggerSend();
        QTRY_VERIFY_WITH_TIMEOUT(!page.session()->active() && first.received.size() == 1, 5000);
        QVERIFY(store->createEnvironment("测试服务", &error));
        QVERIFY(store->setEnvironmentVariables({variable("base_url", second.base())}, &error));
        page.triggerSend();
        QTRY_VERIFY_WITH_TIMEOUT(!page.session()->active() && second.received.size() == 1, 5000);
        QCOMPARE(widget<QLineEdit>(page, "protocolUrl")->text(), "{{base_url}}/health");
        QVERIFY(store->createEnvironment("待配置", &error));
        page.triggerSend();
        QTest::qWait(80);
        QVERIFY(!page.session()->active());
        QCOMPARE(first.received.size(), 1);
        QCOMPARE(second.received.size(), 1);
        QVERIFY(widget<QLabel>(page, "protocolWarning")->isVisible());
    }
    void newEnvironmentOpensConfigurationAndStaysSilent() {
        Server server;
        QTemporaryDir dir;
        QSettings settings(dir.filePath("new-env.ini"), QSettings::IniFormat);
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, &settings);
        const auto previous = page.projectStore()->environmentId();
        int phase = 0;
        QTimer interact;
        interact.setInterval(10);
        connect(&interact, &QTimer::timeout, &page, [&] {
            auto *modal = QApplication::activeModalWidget();
            if (auto *prompt = qobject_cast<QInputDialog *>(modal); prompt && phase == 0) {
                prompt->setTextValue("QA environment");
                phase = 1;
                prompt->accept();
                return;
            }
            if (auto *dialog = qobject_cast<QDialog *>(modal);
                dialog && phase == 1 && dialog->objectName() == "httpProjectSettingsDialog") {
                phase = 2;
                interact.stop();
                widget<QLineEdit>(*dialog, "httpEnvironmentBaseUrl")->setText(server.base());
                dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
                if (dialog->isVisible())
                    dialog->reject();
            }
        });
        QTimer::singleShot(5000, &page, [] {
            if (auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                d->reject();
        });
        interact.start();
        auto *action = page.findChild<QAction *>("httpEnvironmentNew");
        QVERIFY(action);
        action->trigger();
        interact.stop();
        QCOMPARE(phase, 2);
        QVERIFY(page.projectStore()->environmentId() != previous);
        QCOMPARE(page.projectStore()->environment().value("name").toString(), "QA environment");
        QCOMPARE(page.projectStore()->expand("{{base_url}}"), server.base());
        QCOMPARE(server.received.size(), 0);
        QVERIFY(!page.session()->active());
        HttpProjectStore restored(&settings);
        QCOMPARE(restored.environmentId(), page.projectStore()->environmentId());
        QCOMPARE(restored.expand("{{base_url}}"), server.base());
    }
    void configurationDialogRefusesChangedContext() {
        HttpProjectStore store(nullptr);
        HttpConfigurationDialog dialog(&store, 0);
        const auto oldEnvironment = store.environmentId();
        QVERIFY(store.createEnvironment("new context"));
        const auto current = store.project();
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        QCOMPARE(dialog.result(), 0);
        QCOMPARE(store.project(), current);
        QVERIFY(store.environmentId() != oldEnvironment);
        QVERIFY(widget<QLabel>(dialog, "httpSettingsError")->text().contains(QStringLiteral("已变化")));
    }
    void configurationEnvironmentAndAuthenticationReachRealWire() {
        Server first, second;
        QTemporaryDir dir;
        QSettings settings(dir.filePath("wire.ini"), QSettings::IniFormat);
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, &settings);
        auto *store = page.projectStore();
        QString error;
        auto configure = [&](const QString &url, const QString &token, const QJsonObject &auth) {
            return store->saveConfiguration(
                store->projectId(), store->environmentId(), store->revision(),
                {{"projectName", "API"},
                 {"environmentName", store->environment().value("name")},
                 {"projectVariables", QJsonArray{variable("username", "shared-user")}},
                 {"environmentVariables",
                  QJsonArray{variable("base_url", url), variable("access_token", token, true),
                             variable("password", "scoped-password", true)}},
                 {"auth", auth}},
                {}, &error);
        };
        QVERIFY(configure(first.base(), "first-private",
                          {{"kind", "bearer"}, {"token", "{{access_token}}"}}));
        const auto firstId = store->environmentId();
        auto draft = request("{{base_url}}/business");
        draft["editor"] = QJsonObject{{"baseUrl", "{{base_url}}/business"}, {"authKind", 3}};
        QVERIFY(page.loadDraft(draft, &error));
        QCOMPARE(first.received.size(), 0);
        QCOMPARE(second.received.size(), 0);
        page.triggerSend();
        QTRY_VERIFY_WITH_TIMEOUT(!page.session()->active() && first.received.size() == 1, 5000);
        QCOMPARE(first.received.last().value("headers").toObject().value("authorization").toString(),
                 "Bearer first-private");
        QVERIFY(store->createEnvironment("Other"));
        QVERIFY(configure(second.base(), "second-private",
                          {{"kind", "bearer"}, {"token", "{{access_token}}"}}));
        QVERIFY(page.loadDraft(draft, &error));
        page.triggerSend();
        QTRY_VERIFY_WITH_TIMEOUT(!page.session()->active() && second.received.size() == 1, 5000);
        QCOMPARE(first.received.size(), 1);
        QCOMPARE(second.received.last().value("headers").toObject().value("authorization").toString(),
                 "Bearer second-private");
        QVERIFY(store->selectEnvironment(firstId));
        QVERIFY(
            configure(first.base(), "first-private",
                      {{"kind", "basic"}, {"username", "{{username}}"}, {"password", "{{password}}"}}));
        QVERIFY(page.loadDraft(draft, &error));
        page.triggerSend();
        QTRY_VERIFY_WITH_TIMEOUT(!page.session()->active() && first.received.size() == 2, 5000);
        QCOMPARE(first.received.last().value("headers").toObject().value("authorization").toString(),
                 "Basic " + QString::fromLatin1(QByteArray("shared-user:scoped-password").toBase64()));
        widget<QComboBox>(page, "protocolAuthKind")->setCurrentIndex(0);
        page.triggerSend();
        QTRY_VERIFY_WITH_TIMEOUT(!page.session()->active() && first.received.size() == 3, 5000);
        QVERIFY(!first.received.last().value("headers").toObject().contains("authorization"));
        const auto disk = settings.value("manual/httpProjectsV2").toByteArray();
        QVERIFY(!disk.contains("first-private"));
        QVERIFY(!disk.contains("second-private"));
        QVERIFY(!disk.contains("scoped-password"));
        QCOMPARE(widget<QComboBox>(page, "protocolAuthKind")->count(), 4);
    }
    void nativeConfigurationScreenshots() {
        const auto target = qEnvironmentVariable("PORTBRIDGE_HTTP_CONFIGURATION_SCREENSHOT_DIR");
        if (target.isEmpty())
            QSKIP("Native configuration screenshots run separately.");
        QDir().mkpath(target);
        HttpProjectStore store(nullptr);
        QWidget parent;
        parent.resize(1000, 700);
        QVERIFY(store.renameProject(QStringLiteral("会员服务联调")));
        QVERIFY(store.renameEnvironment(QStringLiteral("本地开发")));
        QVERIFY(store.setProjectVariables({variable("api_version", "v1"), variable("page_size", 20)}));
        QVERIFY(store.setEnvironmentVariables({variable("base_url", "http://localhost:8080"),
                                               variable("username", "demo"),
                                               variable("access_token", "synthetic-secret", true)}));
        QVERIFY(store.setProjectAuth({{"kind", "bearer"}, {"token", "{{access_token}}"}}));
        QVERIFY(store.setBrowserFingerprint(httpFingerprint::generate("edge", "Windows", "zh-CN")));
        for (bool dark : {true, false}) {
            parent.setProperty("darkTheme", dark);
            HttpConfigurationDialog dialog(&store, 0, &parent);
            dialog.show();
            QTest::qWait(60);
            auto *tabs = widget<QTabWidget>(dialog, "httpSettingsTabs");
            auto *navigation = widget<QListWidget>(dialog, "httpSettingsNavigation");
            const auto verifyFooter = [&] {
                auto *buttons = dialog.findChild<QDialogButtonBox *>();
                QVERIFY(dialog.rect().contains(buttons->geometry()));
                for (auto role : {QDialogButtonBox::Save, QDialogButtonBox::Cancel}) {
                    auto *button = buttons->button(role);
                    QVERIFY(button->isVisible());
                    QVERIFY(dialog.rect().contains(
                        QRect(button->mapTo(&dialog, QPoint()), button->size())));
                }
            };
            for (int tab = 0; tab < 5; ++tab) {
                QTest::mouseClick(navigation->viewport(), Qt::LeftButton, Qt::NoModifier,
                                  navigation->visualItemRect(navigation->item(tab)).center());
                QCOMPARE(tabs->currentIndex(), tab);
                QTest::qWait(40);
                QVERIFY(dialog.grab().save(
                    target + QString("/settings-%1-%2.png").arg(dark ? "dark" : "light").arg(tab)));
                verifyFooter();
            }
            tabs->setCurrentIndex(2);
            widget<QComboBox>(dialog, "httpProjectAuthKind")->setCurrentIndex(2);
            QTest::qWait(40);
            QVERIFY(dialog.grab().save(target + QString("/basic-%1.png").arg(dark ? "dark" : "light")));
            dialog.resize(740, 500);
            for (int tab : {0, 2, 4}) {
                tabs->setCurrentIndex(tab);
                QTest::qWait(40);
                QCOMPARE(dialog.size(), QSize(740, 500));
                verifyFooter();
                auto *scroll = qobject_cast<QScrollArea *>(tabs->currentWidget());
                QVERIFY(scroll);
                QVERIFY(scroll->widgetResizable());
                QVERIFY(dialog.grab().save(
                    target + QString("/compact-%1-%2.png").arg(dark ? "dark" : "light").arg(tab)));
            }
            tabs->setCurrentIndex(0);
            navigation->setFocus();
            QTest::keyClick(navigation, Qt::Key_Down);
            QCOMPARE(tabs->currentIndex(), 1);
            dialog.reject();
        }
    }
    void nativeProjectScreenshots() {
        const auto target = qEnvironmentVariable("PORTBRIDGE_HTTP_PROJECT_SCREENSHOT_DIR");
        if (target.isEmpty())
            QSKIP("Native screenshot capture is a separate controlled run.");
        QDir().mkpath(target);
        QTemporaryDir dir;
        QSettings settings(dir.filePath("screens.ini"), QSettings::IniFormat);
        SessionController raw;
        MainWindow theme(&raw);
        ProtocolDebugPage page(ProtocolDebugSession::Mode::Http, &settings);
        QString error;
        auto *store = page.projectStore();
        QVERIFY(store->renameProject("会员服务联调", &error));
        QVERIFY(store->addFolder("认证"));
        QVERIFY(store->addFolder("会员"));
        QVERIFY(store->setEnvironmentVariables(
            {variable("base_url", "http://127.0.0.1:8080"), variable("username", "demo")}, &error));
        QVERIFY(store->setProjectAuth({{"kind", "bearer"}, {"token", "{{access_token}}"}}, &error));
        QVERIFY(page.createSavedRequest("用户登录", "http://127.0.0.1:8080/login", &error));
        QVERIFY(page.loadDraft(request("{{base_url}}/users/me", "查询当前会员"), &error));
        widget<QComboBox>(page, "protocolAuthKind")->setCurrentIndex(3);
        widget<QComboBox>(page, "httpRequestFolder")->setCurrentText("会员");
        QVERIFY(page.saveDraft(&error));
        page.setAttribute(Qt::WA_StyledBackground, true);
        page.resize(1040, 660);
        page.show();
        QTest::qWait(80);
        QVERIFY(widget<QPushButton>(page, "protocolPrimary")->isVisible());
        QVERIFY(widget<QComboBox>(page, "httpProjectChoice")->currentText().contains("会员"));
        for (bool dark : {true, false}) {
            if (!dark)
                widget<QPushButton>(theme, "themeButton")->click();
            page.setDarkTheme(dark);
            page.setStyleSheet(
                theme.styleSheet() + page.styleSheet() +
                QString("#httpDebugPage {background:%1;}").arg(dark ? "#101618" : "#f5f7f7"));
            QTest::qWait(70);
            QVERIFY(page.grab().save(target + "/project-" + (dark ? "dark" : "light") + ".png"));
        }
        widget<QPushButton>(theme, "themeButton")->click();
        page.setDarkTheme(true);
        page.setStyleSheet(theme.styleSheet() + page.styleSheet() +
                           "#httpDebugPage {background:#101618;}");
        widget<QTabWidget>(page, "protocolRequestTabs")->setCurrentIndex(5);
        row(widget<QTableWidget>(page, "httpExtractionRules"), "access_token", "$.data.access_token");
        QTest::qWait(70);
        QVERIFY(page.grab().save(target + "/extraction-dark.png"));
        QTimer capture;
        capture.setInterval(10);
        connect(&capture, &QTimer::timeout, &capture, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != "httpVariablesDialog")
                return;
            capture.stop();
            QTest::qWait(80);
            QVERIFY(dialog->grab().save(target + "/variables-dark.png"));
            dialog->reject();
        });
        capture.start();
        widget<HttpProjectPanel>(page, "httpProjectPanel")->editVariables();
        QTimer captureAuth;
        captureAuth.setInterval(10);
        connect(&captureAuth, &QTimer::timeout, &captureAuth, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != "httpProjectAuthDialog")
                return;
            captureAuth.stop();
            QVERIFY(widget<QLineEdit>(*dialog, "httpProjectAuthToken")->echoMode() ==
                    QLineEdit::Password);
            QTest::qWait(80);
            QVERIFY(dialog->grab().save(target + "/project-auth-dark.png"));
            dialog->reject();
        });
        captureAuth.start();
        widget<HttpProjectPanel>(page, "httpProjectPanel")->editAuthentication();
        QVERIFY(!page.session()->active());
    }
};
QTEST_MAIN(HttpProjectsTest)
#include "test_http_projects.moc"
