#pragma once
#include <QJsonArray>
#include <QJsonObject>
namespace portbridge {
class HttpProjectStore;
class HttpAssertions {
  public:
    static QString validate(const QJsonArray &rules);
    static QJsonArray resolve(const QJsonArray &rules, const HttpProjectStore &store, QString *error);
    // Output deliberately excludes actual/expected values and response data.
    static QJsonArray evaluate(const QJsonObject &response, const QJsonArray &rules);
    static bool passed(const QJsonArray &results);
};
} // namespace portbridge
