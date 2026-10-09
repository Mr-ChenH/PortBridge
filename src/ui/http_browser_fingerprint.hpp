#pragma once
#include <QJsonObject>
#include <QJsonValue>
namespace portbridge::httpFingerprint {
QString validate(const QJsonValue &configuration);
QJsonObject generate(const QString &browser = "any", const QString &platform = "any",
                     const QString &language = "zh-CN", const QJsonObject &previous = {});
QJsonObject variables(const QJsonObject &configuration);
QJsonObject headerTemplates(const QJsonObject &configuration, bool https);
QString summary(const QJsonObject &configuration);
} // namespace portbridge::httpFingerprint
