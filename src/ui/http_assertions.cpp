#include "http_assertions.hpp"
#include "../workflow/workflow_private.hpp"
#include "http_project_store.hpp"
#include <QRegularExpression>
namespace portbridge {
namespace {
QString pathOf(QString path) {
    if (path == "$")
        return {};
    if (path.startsWith("$."))
        path.remove(0, 2);
    else if (path.startsWith("$["))
        path.remove(0, 1);
    return path;
}
bool validPath(const QString &path) {
    if (path.size() > 512)
        return false;
    if (path.isEmpty() || path == "$")
        return true;
    static const QRegularExpression grammar(
        "^(?:[A-Za-z_][A-Za-z0-9_]*|\\[[0-9]+\\])"
        "(?:\\.(?:[A-Za-z_][A-Za-z0-9_]*|[0-9]+)|\\[[0-9]+\\])*\\z");
    return grammar.match(pathOf(path)).hasMatch();
}
QJsonValue lookup(QJsonValue value, const QString &path, bool *found) {
    *found = validPath(path);
    if (!*found)
        return QJsonValue(QJsonValue::Undefined);
    auto normalized = pathOf(path);
    if (normalized.isEmpty())
        return value;
    normalized.replace(QRegularExpression("\\[([0-9]+)\\]"), ".\\1");
    if (normalized.startsWith('.'))
        normalized.remove(0, 1);
    for (const auto &segment : normalized.split('.')) {
        if (value.isString()) {
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(value.toString().toUtf8(), &error);
            if (error.error != QJsonParseError::NoError ||
                (!document.isObject() && !document.isArray())) {
                *found = false;
                return QJsonValue(QJsonValue::Undefined);
            }
            value = document.isObject() ? QJsonValue(document.object()) : QJsonValue(document.array());
        }
        if (value.isObject() && value.toObject().contains(segment))
            value = value.toObject().value(segment);
        else if (value.isArray()) {
            bool indexValid = false;
            const int index = segment.toInt(&indexValid);
            const auto array = value.toArray();
            if (!indexValid || index < 0 || index >= array.size()) {
                *found = false;
                return QJsonValue(QJsonValue::Undefined);
            }
            value = array[index];
        } else {
            *found = false;
            return QJsonValue(QJsonValue::Undefined);
        }
    }
    return value;
}
} // namespace
QString HttpAssertions::validate(const QJsonArray &rules) {
    if (rules.size() > 32)
        return QStringLiteral("最多32行响应断言。");
    for (const auto &value : rules) {
        const auto r = value.toObject();
        const auto source = r.value("source").toString(), relation = r.value("relation").toString();
        if (!value.isObject() || !r.value("enabled").isBool() ||
            !QStringList{"status", "header", "json"}.contains(source) ||
            !QStringList{"equals", "exists", "contains"}.contains(relation))
            return QStringLiteral("断言来源、关系或启用标记无效。");
        const auto path = r.value("path").toString();
        if (path.size() > 512 || (source == "json" && !validPath(path)) ||
            (source == "header" &&
             (path.isEmpty() ||
              !QRegularExpression("^[!#$%&'*+.^_`|~0-9A-Za-z-]+$").match(path).hasMatch())))
            return QStringLiteral("断言字段路径或响应头名称无效。");
        if (relation != "exists" &&
            (!r.contains("expected") || !workflowDetail::boundedJson(r.value("expected"), 64 * 1024)))
            return QStringLiteral("断言预期值缺失、超过64KiB或深度限制。");
        if (relation == "contains" && !r.value("expected").isString())
            return QStringLiteral("包含断言的预期值须为字符串。");
    }
    return {};
}
QJsonArray HttpAssertions::resolve(const QJsonArray &rules, const HttpProjectStore &store,
                                   QString *error) {
    QString local;
    if (!error)
        error = &local;
    *error = validate(rules);
    if (!error->isEmpty())
        return {};
    auto result = rules;
    for (int i = 0; i < result.size(); ++i) {
        auto r = result[i].toObject();
        if (!r.value("enabled").toBool() || r.value("relation").toString() == "exists")
            continue;
        const auto expected = r.value("expected");
        if (expected.isString() && expected.toString().contains("{{"))
            r["expected"] = store.expand(expected.toString(), error);
        else if (expected.isObject() || expected.isArray()) {
            const auto text = store.expand(workflowDetail::text(expected), error, true);
            const auto doc = QJsonDocument::fromJson(text.toUtf8());
            if (doc.isNull() && error->isEmpty())
                *error = QStringLiteral("断言预期JSON展开无效。");
            r["expected"] = doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array());
        }
        if (!error->isEmpty())
            return {};
        result[i] = r;
    }
    *error = validate(result);
    return error->isEmpty() ? result : QJsonArray{};
}
QJsonArray HttpAssertions::evaluate(const QJsonObject &response, const QJsonArray &rules) {
    QJsonArray results;
    const auto why = validate(rules);
    if (!why.isEmpty())
        return {QJsonObject{{"index", 0}, {"passed", false}, {"message", why}}};
    for (int i = 0; i < rules.size(); ++i) {
        const auto rule = rules[i].toObject();
        if (!rule.value("enabled").toBool())
            continue;
        const auto source = rule.value("source").toString(),
                   relation = rule.value("relation").toString();
        bool found = false;
        QJsonValue actual(QJsonValue::Undefined);
        if (source == "status") {
            found = response.value("status").isDouble();
            actual = response.value("status");
        } else if (source == "header") {
            const auto headers = response.value("headers").toObject();
            for (auto it = headers.begin(); it != headers.end(); ++it)
                if (it.key().compare(rule.value("path").toString(), Qt::CaseInsensitive) == 0) {
                    actual = it.value();
                    found = true;
                    break;
                }
        } else if (response.value("body").isObject() || response.value("body").isArray())
            actual = lookup(response.value("body"), rule.value("path").toString(), &found);
        bool matched = relation == "exists" ? found
                       : relation == "equals"
                           ? found && actual == rule.value("expected")
                           : found && actual.isString() &&
                                 actual.toString().contains(rule.value("expected").toString());
        results.append(QJsonObject{{"index", i + 1},
                                   {"passed", matched},
                                   {"message", matched  ? QStringLiteral("断言通过")
                                               : !found ? QStringLiteral("字段不存在")
                                                        : QStringLiteral("预期条件未满足（值省略）")}});
    }
    return results;
}
bool HttpAssertions::passed(const QJsonArray &results) {
    for (const auto &value : results)
        if (!value.toObject().value("passed").toBool())
            return false;
    return true;
}
} // namespace portbridge
