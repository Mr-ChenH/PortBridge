#pragma once
#include "portbridge/workflow.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <cmath>
namespace portbridge::workflowDetail {
// QJsonObject has no default-value overload; keep fallback semantics explicit
// in the internal parameter object without changing the persisted ABI.
struct Parameters : QJsonObject {
    Parameters() = default;
    Parameters(const QJsonObject& o) : QJsonObject(o) {}
    using QJsonObject::operator=;
    using QJsonObject::value;
    QJsonValue value(const QString& key, const QJsonValue& fallback) const {
        const auto found = QJsonObject::value(key);
        return found.isUndefined() ? fallback : found;
    }
};
inline QString text(const QJsonValue& v) {
    if (v.isString()) return v.toString();
    if (v.isBool()) return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (v.isDouble()) return QString::number(v.toDouble(), 'g', 16);
    if (v.isNull()) return QStringLiteral("null");
    if (v.isObject()) return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    if (v.isArray()) return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    return {};
}
inline int number(const QJsonValue& v, int fallback) { bool ok = false; const auto n = text(v).toInt(&ok); return ok ? n : fallback; }
inline bool integer(const QJsonValue& v, int low, int high) {
    bool ok = false; const auto n = text(v).toDouble(&ok);
    return ok && std::isfinite(n) && std::floor(n) == n && n >= low && n <= high;
}
inline bool boundedJson(const QJsonValue& v, qint64 limit, int depth = 0, qint64* total = nullptr, int* entries = nullptr) {
    qint64 local = 0; int count = 0; if (!total) total = &local; if (!entries) entries = &count;
    if (depth > 16 || ++*entries > 20000) return false;
    *total += 32;
    if (v.isString()) *total += qint64(v.toString().size()) * 6;
    else if (v.isObject()) {
        const auto o = v.toObject();
        for (auto it = o.begin(); it != o.end(); ++it) {
            *total += qint64(it.key().size()) * 6 + 32;
            if (!boundedJson(it.value(), limit, depth + 1, total, entries)) return false;
        }
    } else if (v.isArray()) for (const auto& item : v.toArray()) if (!boundedJson(item, limit, depth + 1, total, entries)) return false;
    return *total <= limit;
}
inline bool safePath(QString path) {
    if (path == "$" || path.isEmpty()) return true;
    if (path.startsWith("$.")) path.remove(0, 2);
    static const QRegularExpression expression(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*(\\.[A-Za-z_][A-Za-z0-9_]*|\\.[0-9]+)*$"));
    return path.size() <= 512 && expression.match(path).hasMatch();
}
inline QJsonValue lookup(QJsonValue v, QString path, bool* ok) {
    *ok = safePath(path);
    if (!*ok) return QJsonValue(QJsonValue::Undefined);
    if (path.startsWith("$.")) path.remove(0, 2);
    if (path == "$" || path.isEmpty()) return v;
    for (const auto& key : path.split('.')) {
        if (v.isString()) { QJsonParseError e; const auto doc = QJsonDocument::fromJson(v.toString().toUtf8(), &e); if (e.error != QJsonParseError::NoError) { *ok = false; return QJsonValue(QJsonValue::Undefined); } v = doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array()); }
        if (v.isObject() && v.toObject().contains(key)) v = v.toObject().value(key);
        else if (v.isArray()) { bool indexOk; const int index = key.toInt(&indexOk); const auto a = v.toArray(); if (!indexOk || index < 0 || index >= a.size()) { *ok = false; return QJsonValue(QJsonValue::Undefined); } v = a[index]; }
        else { *ok = false; return QJsonValue(QJsonValue::Undefined); }
    }
    return v;
}
}
