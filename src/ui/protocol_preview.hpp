#pragma once
#include "workflow_canvas.hpp"
namespace portbridge::workflowUi {
inline QString previewValue(const QJsonValue &value, const SecretSamples &secrets = {},
                            const QString &key = {}) {
    // Budget before string conversion and JSON serialization, including nested containers.
    int remaining = 32768, entries = 0;
    std::function<QJsonValue(QJsonValue, int)> trim = [&](QJsonValue v, int depth) -> QJsonValue {
        if (depth > 24 || entries++ > 512 || remaining <= 0)
            return QStringLiteral("… 预览已截断；可导出结果文件。");
        if (v.isString()) {
            auto text = v.toString();
            const int length = std::min(remaining, int(text.size()));
            remaining -= length;
            if (length < text.size()) {
                // Inspect a bounded overlap so a known credential crossing the
                // preview boundary cannot leak its visible prefix.
                const auto guard = text.left(length + 4096);
                if (redactText(guard, secrets) != guard)
                    return QStringLiteral("[截断边界含凭据，文本预览已遮蔽]");
            }
            return text.left(length) + (length < text.size() ? QStringLiteral("\n… 预览已截断") : QString());
        }
        if (v.isObject()) {
            const auto object = v.toObject();
            QJsonObject out;
            for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
                if (remaining <= 0 || entries > 512) {
                    out["…"] = QStringLiteral("其余字段预览已截断");
                    break;
                }
                auto name = it.key().left(128);
                remaining -= name.size();
                out[name] = sensitive(it.key()) ? QJsonValue(QStringLiteral("[已遮蔽]"))
                                                : trim(it.value(), depth + 1);
            }
            return out;
        }
        if (v.isArray()) {
            QJsonArray out;
            for (const auto &item : v.toArray()) {
                if (remaining <= 0 || entries > 512 || out.size() >= 100) {
                    out.append(QStringLiteral("… 数组预览已截断"));
                    break;
                }
                out.append(trim(item, depth + 1));
            }
            return out;
        }
        remaining -= 24;
        return v;
    };
    auto s = jsonText(redact(trim(value, 0), key, secrets));
    if (s.size() > 32768)
        s = s.left(32768) + QStringLiteral("\n… 预览已截断；可导出结果文件。");
    return s;
}
} // namespace portbridge::workflowUi
