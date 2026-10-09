#include "http_browser_fingerprint.hpp"
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QUuid>
namespace portbridge::httpFingerprint {
namespace {
const QStringList browsers{"chrome", "edge", "firefox"};
const QStringList platforms{"Windows", "macOS", "Linux"};
const QStringList languages{"zh-CN", "zh-TW", "en-US", "ja-JP", "de-DE"};
QString userAgent(const QJsonObject &p) {
    const auto browser = p.value("browser").toString(), platform = p.value("platform").toString();
    const auto major = QString::number(p.value("major").toInt());
    QString os = platform == "Windows" ? "Windows NT 10.0; Win64; x64"
                 : platform == "macOS" ? "Macintosh; Intel Mac OS X 10_15_7"
                                       : "X11; Linux x86_64";
    if (browser == "firefox" && platform == "macOS")
        os = "Macintosh; Intel Mac OS X 10.15";
    if (browser == "firefox")
        return "Mozilla/5.0 (" + os + "; rv:" + major + ".0) Gecko/20100101 Firefox/" + major + ".0";
    return "Mozilla/5.0 (" + os + ") AppleWebKit/537.36 (KHTML, like Gecko) Chrome/" + major +
           ".0.0.0 Safari/537.36" + (browser == "edge" ? " Edg/" + major + ".0.0.0" : QString());
}
QString acceptLanguage(const QString &language) {
    if (language == "en-US")
        return "en-US,en;q=0.9";
    return language + "," + language.section('-', 0, 0) + ";q=0.9,en;q=0.8";
}
QString brand(const QJsonObject &p) {
    const auto version = QString::number(p.value("major").toInt());
    return "\"Not_A Brand\";v=\"8\", \"Chromium\";v=\"" + version + "\", \"" +
           (p.value("browser").toString() == "edge" ? "Microsoft Edge" : "Google Chrome") + "\";v=\"" +
           version + "\"";
}
} // namespace
QString validate(const QJsonValue &configuration) {
    if (configuration.isUndefined())
        return {};
    if (!configuration.isObject())
        return QStringLiteral("浏览器指纹配置须为对象。");
    const auto p = configuration.toObject();
    if (p.isEmpty())
        return {};
    const QSet<QString> allowed{"version", "id", "enabled", "browser", "platform", "major", "language"};
    for (auto it = p.begin(); it != p.end(); ++it)
        if (!allowed.contains(it.key()))
            return QStringLiteral("浏览器指纹配置包含未知字段。");
    static const QRegularExpression identity("\\A[A-Za-z0-9_-]{1,128}\\z");
    if (!p.value("version").isDouble() || p.value("version").toDouble() != 1 ||
        !p.value("enabled").isBool() || !identity.match(p.value("id").toString()).hasMatch() ||
        !browsers.contains(p.value("browser").toString()) ||
        !platforms.contains(p.value("platform").toString()) ||
        !languages.contains(p.value("language").toString()) || !p.value("major").isDouble() ||
        p.value("major").toDouble() != p.value("major").toInt() || p.value("major").toInt() < 100 ||
        p.value("major").toInt() > 199)
        return QStringLiteral("浏览器指纹版本、标识、浏览器、平台、语言或主版本无效。");
    return {};
}
QJsonObject generate(const QString &browser, const QString &platform, const QString &language,
                     const QJsonObject &previous) {
    if ((browser != "any" && !browsers.contains(browser)) ||
        (platform != "any" && !platforms.contains(platform)) || !languages.contains(language))
        return {};
    auto *random = QRandomGenerator::global();
    const auto b = browser == "any" ? browsers[random->bounded(browsers.size())] : browser;
    const auto os = platform == "any" ? platforms[random->bounded(platforms.size())] : platform;
    const QList<int> versions = b == "firefox" ? QList<int>{128, 135, 143} : QList<int>{131, 138, 143};
    int index = random->bounded(versions.size());
    if (previous.value("browser") == b && previous.value("platform") == os &&
        previous.value("language") == language && previous.value("major").toInt() == versions[index])
        index = (index + 1) % versions.size();
    return {{"version", 1},        {"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {"enabled", true},     {"browser", b},
            {"platform", os},      {"major", versions[index]},
            {"language", language}};
}
QJsonObject variables(const QJsonObject &p) {
    if (p.isEmpty() || !validate(p).isEmpty())
        return {};
    const auto ua = userAgent(p), language = acceptLanguage(p.value("language").toString());
    auto result = QJsonObject{{"browser_user_agent", ua},
                              {"browser_accept_language", language},
                              {"browser_platform", p.value("platform")},
                              {"browser_name", p.value("browser")},
                              {"browser_version", QString::number(p.value("major").toInt())}};
    QJsonObject headers{{"User-Agent", ua}, {"Accept-Language", language}};
    if (p.value("browser").toString() != "firefox") {
        result["browser_sec_ch_ua"] = brand(p);
        result["browser_sec_ch_ua_mobile"] = "?0";
        result["browser_sec_ch_ua_platform"] = "\"" + p.value("platform").toString() + "\"";
        headers["Sec-CH-UA"] = result.value("browser_sec_ch_ua");
        headers["Sec-CH-UA-Mobile"] = "?0";
        headers["Sec-CH-UA-Platform"] = result.value("browser_sec_ch_ua_platform");
    }
    auto fingerprint = p;
    fingerprint["headers"] = headers;
    fingerprint["scope"] = "http-request-headers";
    result["browser_fingerprint"] = fingerprint;
    return result;
}
QJsonObject headerTemplates(const QJsonObject &p, bool https) {
    if (p.isEmpty() || !validate(p).isEmpty() || !p.value("enabled").toBool())
        return {};
    QJsonObject headers{{"User-Agent", "{{browser_user_agent}}"},
                        {"Accept-Language", "{{browser_accept_language}}"}};
    if (https && p.value("browser").toString() != "firefox") {
        headers["Sec-CH-UA"] = "{{browser_sec_ch_ua}}";
        headers["Sec-CH-UA-Mobile"] = "{{browser_sec_ch_ua_mobile}}";
        headers["Sec-CH-UA-Platform"] = "{{browser_sec_ch_ua_platform}}";
    }
    return headers;
}
QString summary(const QJsonObject &p) {
    if (p.isEmpty())
        return QStringLiteral("尚未生成。点击随机生成，可先预览再保存。");
    const auto browser = p.value("browser").toString();
    return QString("%1 %2 · %3 · %4 · %5")
        .arg(browser == "chrome" ? "Chrome"
             : browser == "edge" ? "Edge"
                                 : "Firefox")
        .arg(p.value("major").toInt())
        .arg(p.value("platform").toString(), p.value("language").toString(),
             p.value("enabled").toBool() ? QStringLiteral("自动请求头已启用")
                                         : QStringLiteral("仅提供变量"));
}
} // namespace portbridge::httpFingerprint
