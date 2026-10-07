#include "session_private.hpp"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QSet>
#include <cmath>
#include <chrono>

namespace portbridge {
namespace {
void fail(QString* error, const QString& message) { if (error) *error = message; }
bool keys(const QJsonObject& o, const QStringList& fields) {
    if (o.size() != fields.size()) return false;
    for (const auto& field : fields) if (!o.contains(field)) return false;
    return true;
}
bool integer(const QJsonValue& v, double low, double high) {
    return v.isDouble() && std::isfinite(v.toDouble()) && std::floor(v.toDouble()) == v.toDouble() && v.toDouble() >= low && v.toDouble() <= high;
}
QString kindName(TransportKind k) {
    switch (k) { case TransportKind::Serial: return "serial"; case TransportKind::TcpClient: return "tcpClient"; case TransportKind::TcpServer: return "tcpServer"; case TransportKind::Udp: return "udp"; }
    return {};
}
QJsonObject profileJson(const ConnectionConfig& c) {
    return {{"name", detail::text(c.name)}, {"kind", kindName(c.kind)}, {"localAddress", detail::text(c.localAddress)}, {"localPort", c.localPort}, {"remoteAddress", detail::text(c.remoteAddress)}, {"remotePort", c.remotePort}, {"serialPort", detail::text(c.serialPort)}, {"baudRate", c.baudRate}, {"dataBits", c.dataBits}, {"parity", detail::text(c.parity)}, {"stopBits", detail::text(c.stopBits)}, {"flowControl", detail::text(c.flowControl)}, {"receiveBufferBytes", double(c.receiveBufferBytes)}, {"sendBufferBytes", double(c.sendBufferBytes)}, {"sendQueueBytes", double(c.sendQueueBytes)}, {"connectTimeoutMs", c.connectTimeoutMs}, {"sequenceAnalysis", c.sequenceAnalysis}, {"sequenceOffset", double(c.sequenceOffset)}, {"sequenceBigEndian", c.sequenceBigEndian}, {"sequenceWindow", double(c.sequenceWindow)}};
}
bool decodeProfile(const QJsonObject& o, ConnectionConfig& c) {
    const auto reference = profileJson(c);
    if (!keys(o, reference.keys())) return false;
    for (const auto& field : {"name", "kind", "localAddress", "remoteAddress", "serialPort", "parity", "stopBits", "flowControl"})
        if (!o[field].isString() || o[field].toString().size() > 4096) return false;
    const auto kind = o["kind"].toString();
    if (kind == "serial") c.kind = TransportKind::Serial;
    else if (kind == "tcpClient") c.kind = TransportKind::TcpClient;
    else if (kind == "tcpServer") c.kind = TransportKind::TcpServer;
    else if (kind == "udp") c.kind = TransportKind::Udp;
    else return false;
    if (!integer(o["localPort"], 0, 65535) || !integer(o["remotePort"], 0, 65535) || !integer(o["baudRate"], 1, 4000000) || !integer(o["dataBits"], 5, 8) || !integer(o["connectTimeoutMs"], 1, 600000) || !integer(o["sequenceOffset"], 0, 67108856) || !integer(o["sequenceWindow"], 1, 1048576)) return false;
    for (const auto& field : {"receiveBufferBytes", "sendBufferBytes", "sendQueueBytes"}) if (!integer(o[field], 1, 1073741824)) return false;
    if (!o["sequenceAnalysis"].isBool() || !o["sequenceBigEndian"].isBool()) return false;
    c.name = detail::utf8(o["name"].toString()); c.localAddress = detail::utf8(o["localAddress"].toString()); c.remoteAddress = detail::utf8(o["remoteAddress"].toString()); c.serialPort = detail::utf8(o["serialPort"].toString());
    c.parity = detail::utf8(o["parity"].toString()); c.stopBits = detail::utf8(o["stopBits"].toString()); c.flowControl = detail::utf8(o["flowControl"].toString());
    c.localPort = std::uint16_t(o["localPort"].toInt()); c.remotePort = std::uint16_t(o["remotePort"].toInt()); c.baudRate = o["baudRate"].toInt(); c.dataBits = o["dataBits"].toInt(); c.connectTimeoutMs = o["connectTimeoutMs"].toInt();
    c.receiveBufferBytes = size_t(o["receiveBufferBytes"].toDouble()); c.sendBufferBytes = size_t(o["sendBufferBytes"].toDouble()); c.sendQueueBytes = size_t(o["sendQueueBytes"].toDouble());
    c.sequenceAnalysis = o["sequenceAnalysis"].toBool(); c.sequenceOffset = size_t(o["sequenceOffset"].toDouble()); c.sequenceBigEndian = o["sequenceBigEndian"].toBool(); c.sequenceWindow = size_t(o["sequenceWindow"].toDouble());
    return detail::validateConfig(c).isEmpty();
}
bool readRoot(const QString& path, const QString& member, QJsonArray& array, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { fail(error, f.errorString()); return false; }
    if (f.size() > 16 * 1024 * 1024) { fail(error, "Configuration exceeds 16 MiB"); return false; }
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(f.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) { fail(error, "Invalid JSON: " + parse.errorString()); return false; }
    const auto o = doc.object();
    if (!keys(o, {"schemaVersion", member}) || !integer(o["schemaVersion"], 1, 1) || !o[member].isArray() || o[member].toArray().size() > 10000) { fail(error, "Unsupported schema or invalid " + member); return false; }
    array = o[member].toArray(); return true;
}
bool writeRoot(const QString& path, const QString& member, const QJsonArray& array, QString* error) {
    const auto bytes = QJsonDocument(QJsonObject{{"schemaVersion", 1}, {member, array}}).toJson();
    if (bytes.size() > 16 * 1024 * 1024) { fail(error, "Configuration exceeds 16 MiB"); return false; }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { fail(error, f.errorString()); return false; }
    if (f.write(bytes) != bytes.size() || !f.commit()) { fail(error, f.errorString()); return false; }
    fail(error, {}); return true;
}
QVector<ConnectionConfig> defaults() {
    QVector<ConnectionConfig> out;
    for (auto kind : {TransportKind::Serial, TransportKind::TcpClient, TransportKind::TcpServer, TransportKind::Udp}) {
        ConnectionConfig c; c.kind = kind;
        c.name = kind == TransportKind::Serial ? "Serial" : kind == TransportKind::TcpClient ? "TCP Client" : kind == TransportKind::TcpServer ? "TCP Server" : "UDP";
        out.push_back(c);
    }
    return out;
}
}
namespace detail {
std::uint64_t nowUs() { return std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count()); }
QString storageDirectory() {
    QSettings settings;
    const auto path = settings.value("storage/directory", QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).toString();
    QDir().mkpath(path); return path;
}
QString validateConfig(const ConnectionConfig& c) {
    if (kindName(c.kind).isEmpty()) return "Unknown transport";
    if (c.name.empty() || c.name.size() > 4096) return "Profile name is required";
    if (c.localAddress.empty() || c.remoteAddress.empty() || c.localAddress.size() > 4096 || c.remoteAddress.size() > 4096 || c.serialPort.size() > 4096) return "Invalid address";
    if ((c.kind == TransportKind::TcpClient || c.kind == TransportKind::Udp) && !c.remotePort) return "Remote port must be 1..65535";
    if (c.sequenceAnalysis && c.kind != TransportKind::Udp) return "Sequence analysis requires UDP datagrams; stream framing is not configured";
    if (c.baudRate <= 0 || c.baudRate > 4000000 || c.dataBits < 5 || c.dataBits > 8) return "Invalid serial baud rate or data bits";
    if (c.parity != "None" && c.parity != "Even" && c.parity != "Odd" && c.parity != "Mark" && c.parity != "Space") return "Invalid serial parity";
    if (c.stopBits != "1" && c.stopBits != "1.5" && c.stopBits != "2") return "Invalid serial stop bits";
    if (c.flowControl != "None" && c.flowControl != "Hardware" && c.flowControl != "Software") return "Invalid serial flow control";
    if (!c.receiveBufferBytes || c.receiveBufferBytes > 1073741824 || !c.sendBufferBytes || c.sendBufferBytes > 1073741824 || !c.sendQueueBytes || c.sendQueueBytes > 1073741824 || c.connectTimeoutMs < 1 || c.connectTimeoutMs > 600000 || c.sequenceOffset > 67108856 || !c.sequenceWindow || c.sequenceWindow > 1048576) return "Buffer, timeout or sequence setting out of range";
    return {};
}
}
QByteArray encodePayload(const QString& input, bool hex, const QString& encoding, const QString& eol, QString* error) {
    fail(error, {}); QByteArray out;
    if (encoding != "ASCII" && encoding != "UTF-8") { fail(error, "Encoding must be ASCII or UTF-8"); return {}; }
    if (hex) {
        int first = -1;
        for (qsizetype i = 0; i < input.size(); ++i) {
            const auto c = input[i]; if (c.isSpace()) continue;
            const ushort u = c.unicode();
            int digit = u >= '0' && u <= '9' ? u - '0' : u >= 'A' && u <= 'F' ? u - 'A' + 10 : u >= 'a' && u <= 'f' ? u - 'a' + 10 : -1;
            if (digit < 0) { fail(error, QString("Invalid HEX character at position %1").arg(i + 1)); return {}; }
            if (first < 0) first = digit; else { out.append(char(first * 16 + digit)); first = -1; }
        }
        if (first >= 0) { fail(error, "HEX requires complete pairs of digits"); return {}; }
    } else {
        for (qsizetype i = 0; i < input.size(); ++i) {
            if (encoding == "ASCII" && input[i].unicode() > 127) { fail(error, QString("Non-ASCII character at position %1").arg(i + 1)); return {}; }
            if (input[i].isHighSurrogate()) { if (i + 1 >= input.size() || !input[i + 1].isLowSurrogate()) { fail(error, "Unpaired UTF-16 surrogate"); return {}; } ++i; }
            else if (input[i].isLowSurrogate()) { fail(error, "Unpaired UTF-16 surrogate"); return {}; }
        }
        out = input.toUtf8();
    }
    if (eol == "CR") out.append('\r'); else if (eol == "LF") out.append('\n'); else if (eol == "CRLF") out.append("\r\n"); else if (eol != "none") { fail(error, "EOL must be none, CR, LF or CRLF"); return {}; }
    return out;
}
QString formatHex(const SharedBytes& bytes) {
    if (!bytes || bytes->empty()) return {};
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(bytes->data()), qsizetype(bytes->size())).toHex(' ').toUpper());
}
bool importProfiles(const QString& path, QVector<ConnectionConfig>* profiles, QString* error) {
    fail(error, {}); if (!profiles) { fail(error, "Missing profiles destination"); return false; }
    QJsonArray array; if (!readRoot(path, "profiles", array, error)) return false;
    QVector<ConnectionConfig> candidate;
    for (const auto& value : array) { ConnectionConfig c; if (!value.isObject() || !decodeProfile(value.toObject(), c)) { fail(error, QString("Invalid profile at index %1").arg(candidate.size())); return false; } candidate.push_back(c); }
    *profiles = candidate; return true;
}
bool exportProfiles(const QString& path, const QVector<ConnectionConfig>& profiles, QString* error) {
    QJsonArray array;
    if (profiles.size() > 10000) { fail(error, "Too many profiles"); return false; }
    for (const auto& c : profiles) { const auto message = detail::validateConfig(c); if (!message.isEmpty()) { fail(error, message); return false; } array.append(profileJson(c)); }
    return writeRoot(path, "profiles", array, error);
}
QVector<ConnectionConfig> loadProfiles(QString* error) {
    const auto path = detail::storageDirectory() + "/profiles.json";
    fail(error, {}); if (!QFile::exists(path)) return defaults();
    QVector<ConnectionConfig> profiles; if (!importProfiles(path, &profiles, error)) return defaults(); return profiles;
}
bool saveProfiles(const QVector<ConnectionConfig>& profiles, QString* error) { return exportProfiles(detail::storageDirectory() + "/profiles.json", profiles, error); }
bool importCommands(const QString& path, QVector<Command>* commands, QString* error) {
    fail(error, {}); if (!commands) { fail(error, "Missing commands destination"); return false; }
    QJsonArray array; if (!readRoot(path, "commands", array, error)) return false;
    QVector<Command> candidate;
    for (const auto& value : array) {
        const auto o = value.toObject();
        auto required = o;
        for (const auto& field : {"periodic", "intervalMs", "count"}) required.remove(field);
        if (!value.isObject() || !keys(required, {"name", "input", "hex", "encoding", "eol"}) || !o["hex"].isBool() ||
            (o.contains("periodic") && !o["periodic"].isBool()) ||
            (o.contains("intervalMs") && !integer(o["intervalMs"], 1, 86400000)) ||
            (o.contains("count") && !integer(o["count"], 0, 1000000))) { fail(error, "Invalid command fields"); return false; }
        for (const auto& field : {"name", "input", "encoding", "eol"}) if (!o[field].isString()) { fail(error, "Invalid command field type"); return false; }
        Command c{o["name"].toString(), o["input"].toString(), o["hex"].toBool(), o["encoding"].toString(), o["eol"].toString()};
        if (o.contains("periodic")) c.periodic = o["periodic"].toBool();
        if (o.contains("intervalMs")) c.intervalMs = o["intervalMs"].toInt();
        if (o.contains("count")) c.count = o["count"].toInt();
        QString message; encodePayload(c.input, c.hex, c.encoding, c.eol, &message);
        if (c.name.trimmed().isEmpty() || c.name.size() > 4096 || c.input.size() > 1048576 || !message.isEmpty()) { fail(error, "Invalid command: " + message); return false; }
        candidate.push_back(c);
    }
    *commands = candidate; return true;
}
bool exportCommands(const QString& path, const QVector<Command>& commands, QString* error) {
    QJsonArray array;
    if (commands.size() > 10000) { fail(error, "Too many commands"); return false; }
    for (const auto& c : commands) {
        QString message; encodePayload(c.input, c.hex, c.encoding, c.eol, &message);
        if (c.name.trimmed().isEmpty() || c.name.size() > 4096 || c.input.size() > 1048576 || !message.isEmpty() || c.intervalMs < 1 || c.intervalMs > 86400000 || c.count < 0 || c.count > 1000000) { fail(error, "Invalid command: " + message); return false; }
        array.append(QJsonObject{{"name", c.name}, {"input", c.input}, {"hex", c.hex}, {"encoding", c.encoding}, {"eol", c.eol}, {"periodic", c.periodic}, {"intervalMs", c.intervalMs}, {"count", c.count}});
    }
    return writeRoot(path, "commands", array, error);
}
QVector<Command> loadCommands(QString* error) {
    const auto path = detail::storageDirectory() + "/commands.json";
    fail(error, {}); QVector<Command> commands; if (QFile::exists(path)) importCommands(path, &commands, error); return commands;
}
bool saveCommands(const QVector<Command>& commands, QString* error) { return exportCommands(detail::storageDirectory() + "/commands.json", commands, error); }
}
