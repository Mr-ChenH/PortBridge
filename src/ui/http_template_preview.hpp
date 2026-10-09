#pragma once
#include "protocol_preview.hpp"
#include <QUrl>
namespace portbridge::httpTemplate {
using workflowUi::SecretSamples;
inline bool referenceOnly(const QString &source) {
    static QRegularExpression re("^(?:Bearer )?\\{\\{[A-Za-z_][A-Za-z0-9_]{0,127}\\}\\}$");
    return re.match(source).hasMatch();
}
inline QJsonValue redactTemplate(const QJsonValue &value, const SecretSamples &samples,
                                 const QString &key = {}, int depth = 0) {
    if (depth > 24)
        return QStringLiteral("[结构过深，已遮蔽]");
    if (value.isString() && referenceOnly(value.toString()))
        return value;
    if (workflowUi::sensitive(key))
        return QStringLiteral("[已遮蔽]");
    if (value.isObject()) {
        QJsonObject out;
        const auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it)
            out[it.key()] = redactTemplate(it.value(), samples, it.key(), depth + 1);
        return out;
    }
    if (value.isArray()) {
        QJsonArray out;
        for (const auto &child : value.toArray())
            out.append(redactTemplate(child, samples, {}, depth + 1));
        return out;
    }
    if (value.isString() && value.toString().contains("{{")) {
        const auto doc = QJsonDocument::fromJson(value.toString().toUtf8());
        if (doc.isObject() || doc.isArray()) {
            const auto masked =
                redactTemplate(doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array()),
                               samples, {}, depth + 1);
            return masked.isObject() ? QString::fromUtf8(QJsonDocument(masked.toObject()).toJson())
                                     : QString::fromUtf8(QJsonDocument(masked.toArray()).toJson());
        }
    }
    return workflowUi::redact(value, key, samples);
}
inline QString safeUrl(const QString &source, const SecretSamples &secrets) {
    QUrl url(source);
    url.setUserName({});
    url.setPassword({});
    auto parts = url.query(QUrl::FullyEncoded).split('&', Qt::KeepEmptyParts);
    for (auto &part : parts) {
        const int at = part.indexOf('=');
        const auto key = QUrl::fromPercentEncoding((at < 0 ? part : part.left(at)).toUtf8());
        const auto original = at < 0 ? QString() : QUrl::fromPercentEncoding(part.mid(at + 1).toUtf8());
        const auto value = referenceOnly(original)      ? original
                           : workflowUi::sensitive(key) ? QStringLiteral("[已遮蔽]")
                                                        : workflowUi::redactText(original, secrets);
        if (value != original)
            part = (at < 0 ? part : part.left(at)) + '=' +
                   QString::fromLatin1(QUrl::toPercentEncoding(value));
    }
    if (url.hasQuery())
        url.setQuery(parts.join('&'), QUrl::StrictMode);
    auto result = url.toString(QUrl::FullyEncoded);
    static QRegularExpression ref("%7B%7B([A-Za-z_][A-Za-z0-9_]{0,127})%7D%7D",
                                  QRegularExpression::CaseInsensitiveOption);
    result.replace(ref, "{{\\1}}");
    return result;
}
inline QJsonArray safeAssertions(QJsonArray rows, const SecretSamples &samples) {
    for (int i = 0; i < rows.size(); ++i) {
        auto r = rows[i].toObject();
        if (r.contains("expected"))
            r["expected"] = redactTemplate(r.value("expected"), samples, r.value("path").toString());
        rows[i] = r;
    }
    return rows;
}
inline QJsonObject safeRequest(const QJsonObject &request) {
    SecretSamples samples;
    workflowUi::collectSecrets(request, samples);
    auto result = redactTemplate(request, samples).toObject();
    auto params = result.value("params").toObject();
    const auto original = request.value("params").toObject();
    params["url"] = safeUrl(original.value("url").toString(), samples);
    params.remove("credentialSamples");
    params.remove("httpProjectContext");
    result["params"] = params;
    auto editor = result.value("editor").toObject();
    if (editor.contains("baseUrl"))
        editor["baseUrl"] =
            safeUrl(request.value("editor").toObject().value("baseUrl").toString(), samples);
    for (const auto *kind : {"query", "headers", "formBody"}) {
        auto rows = editor.value(kind).toArray();
        for (int i = 0; i < rows.size(); ++i) {
            auto r = rows[i].toObject();
            r["value"] = redactTemplate(r.value("value"), samples, r.value("key").toString());
            rows[i] = r;
        }
        if (editor.contains(kind))
            editor[kind] = rows;
    }
    result["editor"] = editor;
    for (const auto *key : {"id", "projectId", "folder", "extractionRows"})
        if (request.contains(key))
            result[key] = request.value(key);
    if (request.contains("assertions"))
        result["assertions"] = safeAssertions(request.value("assertions").toArray(), samples);
    result["credentialsMasked"] = true;
    return result;
}
} // namespace portbridge::httpTemplate
