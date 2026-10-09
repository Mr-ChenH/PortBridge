#include "http_request_resolver.hpp"
#include "http_browser_fingerprint.hpp"
#include "protocol_preview.hpp"
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrlQuery>
namespace portbridge {
QJsonObject resolveHttpRequest(const QJsonObject &request, const HttpProjectStore &store,
                               QString *error) {
    QString local;
    if (!error)
        error = &local;
    error->clear();
    auto fail = [&](const QString &why) {
        *error = why;
        return QJsonObject{};
    };
    const auto base = request.value("params").toObject(), edit = request.value("editor").toObject();
    if (request.value("schemaVersion").toInt() != 1 || request.value("kind").toString() != "http" ||
        !request.value("params").isObject() || request.value("prototypeOnly").toBool())
        return fail(QStringLiteral("HTTP请求模板格式无效。"));
    auto expand = [&](const QString &text, bool json = false) {
        return error->isEmpty() ? store.expand(text, error, json) : QString();
    };
    const auto sourceUrl = edit.value("baseUrl").toString(base.value("url").toString());
    QUrl target(store.expandUrl(sourceUrl.trimmed(), error), QUrl::StrictMode);
    if (!error->isEmpty())
        return {};
    QByteArray query = target.query(QUrl::FullyEncoded).toUtf8();
    QJsonObject headers;
    for (const auto &value : edit.value("query").toArray()) {
        const auto row = value.toObject();
        if (!row.value("enabled").toBool() || row.value("key").toString().isEmpty())
            continue;
        if (!query.isEmpty())
            query += '&';
        query += QUrl::toPercentEncoding(expand(row.value("key").toString())) + '=' +
                 QUrl::toPercentEncoding(expand(row.value("value").toString()));
    }
    if (!query.isEmpty() || target.hasQuery())
        target.setQuery(QString::fromLatin1(query), QUrl::StrictMode);
    QJsonArray headerRows = edit.value("headers").toArray();
    if (!edit.contains("headers")) {
        const auto raw = base.value("headers").toObject();
        for (auto it = raw.begin(); it != raw.end(); ++it)
            headerRows.append(QJsonObject{{"enabled", true}, {"key", it.key()}, {"value", it.value()}});
    }
    auto contains = [&](const QString &key) {
        for (auto it = headers.begin(); it != headers.end(); ++it)
            if (it.key().compare(key, Qt::CaseInsensitive) == 0)
                return true;
        return false;
    };
    for (const auto &value : headerRows) {
        const auto row = value.toObject();
        if (!row.value("enabled").toBool() || row.value("key").toString().isEmpty())
            continue;
        const auto key = expand(row.value("key").toString()).trimmed();
        if (contains(key))
            return fail(QStringLiteral("重复请求头：") + key);
        headers[key] = expand(row.value("value").toString());
    }
    const auto fingerprintHeaders = httpFingerprint::headerTemplates(
        store.environment().value("browserFingerprint").toObject(), target.scheme() == "https");
    for (auto it = fingerprintHeaders.begin(); it != fingerprintHeaders.end(); ++it)
        if (!contains(it.key()))
            headers[it.key()] = expand(it.value().toString());
    int auth = edit.value("authKind").toInt();
    QString token = edit.value("token").toString(), username = edit.value("username").toString(),
            password = edit.value("password").toString();
    if (auth == 3) {
        const auto inherited = store.project().value("auth").toObject();
        const auto kind = inherited.value("kind").toString("none");
        auth = kind == "bearer" ? 1 : kind == "basic" ? 2 : 0;
        token = inherited.value("token").toString();
        username = inherited.value("username").toString();
        password = inherited.value("password").toString();
    }
    QJsonObject samples;
    if (auth) {
        if (contains("Authorization"))
            return fail(QStringLiteral("认证页签与Authorization请求头重复，请保留一种。"));
        if (auth == 1) {
            token = expand(token);
            if (token.isEmpty() && error->isEmpty())
                return fail(QStringLiteral("请填写Bearer Token或配置项目Token变量。"));
            headers["Authorization"] = "Bearer " + token;
            samples["token"] = token;
        } else {
            username = expand(username);
            password = expand(password);
            if (username.contains(':'))
                return fail(QStringLiteral("Basic用户名不能含冒号。"));
            const auto credential =
                QString::fromLatin1((username + ':' + password).toUtf8().toBase64());
            headers["Authorization"] = "Basic " + credential;
            samples["password"] = password;
            samples["credential"] = credential;
        }
    }
    const int timeout = base.value("timeoutMs").toInt(10000);
    QJsonObject result{{"url", target.toString(QUrl::FullyEncoded)},
                       {"method", base.value("method").toString("GET")},
                       {"timeoutMs", timeout},
                       {"connectTimeoutMs", std::min(5000, timeout)},
                       {"maxResponseBytes", base.value("maxResponseBytes").toInt(1024 * 1024)},
                       {"caFile", base.value("caFile").toString()}};
    const int kind = edit.value("bodyKind")
                         .toInt(base.value("body").isObject() || base.value("body").isArray() ? 2
                                : base.contains("body")                                       ? 1
                                                                                              : 0);
    const auto sourceBody = edit.contains("body") ? edit.value("body").toString()
                                                  : workflowUi::jsonText(base.value("body"));
    if (kind == 1)
        result["body"] = expand(sourceBody);
    else if (kind == 2) {
        const auto body = expand(sourceBody, true);
        if (!error->isEmpty())
            return {};
        QJsonParseError parse;
        const auto document = QJsonDocument::fromJson(body.toUtf8(), &parse);
        if (parse.error != QJsonParseError::NoError || (!document.isObject() && !document.isArray()))
            return fail(QStringLiteral("JSON Body须为有效对象或数组。"));
        result["body"] = body;
        if (!contains("Content-Type"))
            headers["Content-Type"] = "application/json; charset=utf-8";
    } else if (kind == 3) {
        QByteArray form;
        auto escape = [](const QString &text) {
            auto b = QUrl::toPercentEncoding(text);
            b.replace("%20", "+");
            return b;
        };
        for (const auto &value : edit.value("formBody").toArray()) {
            const auto row = value.toObject();
            if (!row.value("enabled").toBool() || row.value("key").toString().isEmpty())
                continue;
            const auto key = expand(row.value("key").toString()),
                       v = expand(row.value("value").toString());
            if (!form.isEmpty())
                form += '&';
            form += escape(key) + '=' + escape(v);
            if (workflowUi::sensitive(key))
                samples[key] = v;
        }
        result["body"] = QString::fromLatin1(form);
        if (!contains("Content-Type"))
            headers["Content-Type"] = "application/x-www-form-urlencoded; charset=utf-8";
    }
    result["headers"] = headers;
    for (const auto &item : QUrlQuery(target).queryItems(QUrl::FullyDecoded))
        if (workflowUi::sensitive(item.first))
            samples[item.first] = item.second;
    const auto runtime = store.runtimeVariables();
    for (auto it = runtime.begin(); it != runtime.end(); ++it)
        if (it.value().toObject().value("secret").toBool())
            samples[it.key()] = it.value().toObject().value("value");
    result["credentialSamples"] = samples;
    result["httpProjectContext"] = QJsonObject{{"projectId", store.projectId()},
                                               {"environmentId", store.environmentId()},
                                               {"revision", double(store.revision())}};
    if (!error->isEmpty())
        return {};
    const auto why = ProtocolDebugSession::validate(result, ProtocolDebugSession::Mode::Http);
    if (!why.isEmpty())
        return fail(why);
    const QRegularExpression masked(QStringLiteral("\\[[^\\]\\r\\n]{0,96}已遮蔽\\]"));
    if (QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)).contains(masked) ||
        QUrl::fromPercentEncoding(result.value("url").toString().toUtf8()).contains(masked))
        return fail(QStringLiteral("方案中的凭据已被遮蔽，请补填有效值后再发送。"));
    return result;
}
} // namespace portbridge
