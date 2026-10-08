#include "workflow_private.hpp"
#include "workflow_messages.hpp"
#include "../session/session_private.hpp"
#include <QSet>
#include <QUuid>
#include <QUrl>
#include <algorithm>

namespace portbridge {
using namespace workflowDetail;
QJsonObject workflowConnectionConfigToJson(const ConnectionConfig& c) {
    const QStringList kinds{"serial", "tcpClient", "tcpServer", "udp"};
    return {{"name", detail::text(c.name)}, {"kind", kinds.value(int(c.kind))}, {"localAddress", detail::text(c.localAddress)}, {"localPort", c.localPort}, {"remoteAddress", detail::text(c.remoteAddress)}, {"remotePort", c.remotePort}, {"serialPort", detail::text(c.serialPort)}, {"baudRate", c.baudRate}, {"dataBits", c.dataBits}, {"parity", detail::text(c.parity)}, {"stopBits", detail::text(c.stopBits)}, {"flowControl", detail::text(c.flowControl)}, {"receiveBufferBytes", double(c.receiveBufferBytes)}, {"sendBufferBytes", double(c.sendBufferBytes)}, {"sendQueueBytes", double(c.sendQueueBytes)}, {"connectTimeoutMs", c.connectTimeoutMs}, {"sequenceAnalysis", c.sequenceAnalysis}, {"sequenceOffset", double(c.sequenceOffset)}, {"sequenceBigEndian", c.sequenceBigEndian}, {"sequenceWindow", double(c.sequenceWindow)}};
}
bool workflowConnectionConfigFromJson(const QJsonObject& o, ConnectionConfig* out, QString* error) {
    if (error) error->clear();
    auto fail = [&](const QString& why) { if (error) *error = userMessage(why); return false; };
    if (!out || !boundedJson(o, 64 * 1024)) return fail("Invalid or oversized connection snapshot");
    ConnectionConfig c; const auto defaults = workflowConnectionConfigToJson(c);
    for (auto it = o.begin(); it != o.end(); ++it) if (!defaults.contains(it.key())) return fail("Unknown connection field: " + it.key());
    auto merged = defaults; for (auto it = o.begin(); it != o.end(); ++it) merged[it.key()] = it.value();
    const auto kind = merged["kind"].toString(); const QStringList kinds{"serial", "tcpClient", "tcpServer", "udp"};
    const auto index = kinds.indexOf(kind); if (index < 0) return fail("Unknown connection transport"); c.kind = TransportKind(index);
    for (const auto& key : {"name", "localAddress", "remoteAddress", "serialPort", "parity", "stopBits", "flowControl"}) if (!merged[key].isString() || merged[key].toString().size() > 4096) return fail("Invalid connection text field");
    for (const auto& key : {"localPort", "remotePort"}) if (!merged[key].isDouble() || !integer(merged[key], 0, 65535)) return fail("Invalid connection port");
    for (const auto& key : {"receiveBufferBytes", "sendBufferBytes", "sendQueueBytes"}) if (!merged[key].isDouble() || !integer(merged[key], 1, 1073741824)) return fail("Invalid connection capacity");
    for (const auto& key : {"baudRate", "dataBits", "connectTimeoutMs", "sequenceOffset", "sequenceWindow"}) if (!merged[key].isDouble()) return fail("Invalid connection serial/sequence settings");
    if (!integer(merged["baudRate"], 1, 4000000) || !integer(merged["dataBits"], 5, 8) || !integer(merged["connectTimeoutMs"], 1, 600000) || !integer(merged["sequenceOffset"], 0, 67108856) || !integer(merged["sequenceWindow"], 1, 1048576) || !merged["sequenceAnalysis"].isBool() || !merged["sequenceBigEndian"].isBool()) return fail("Invalid connection serial/sequence settings");
    c.name = detail::utf8(merged["name"].toString()); c.localAddress = detail::utf8(merged["localAddress"].toString()); c.remoteAddress = detail::utf8(merged["remoteAddress"].toString()); c.serialPort = detail::utf8(merged["serialPort"].toString()); c.parity = detail::utf8(merged["parity"].toString()); c.stopBits = detail::utf8(merged["stopBits"].toString()); c.flowControl = detail::utf8(merged["flowControl"].toString());
    c.localPort = std::uint16_t(number(merged["localPort"], 0)); c.remotePort = std::uint16_t(number(merged["remotePort"], 0)); c.baudRate = number(merged["baudRate"], 0); c.dataBits = number(merged["dataBits"], 0); c.connectTimeoutMs = number(merged["connectTimeoutMs"], 0);
    c.receiveBufferBytes = size_t(merged["receiveBufferBytes"].toDouble()); c.sendBufferBytes = size_t(merged["sendBufferBytes"].toDouble()); c.sendQueueBytes = size_t(merged["sendQueueBytes"].toDouble()); c.sequenceAnalysis = merged["sequenceAnalysis"].toBool(); c.sequenceOffset = size_t(merged["sequenceOffset"].toDouble()); c.sequenceBigEndian = merged["sequenceBigEndian"].toBool(); c.sequenceWindow = size_t(merged["sequenceWindow"].toDouble());
    const auto why = detail::validateConfig(c); if (!why.isEmpty()) return fail(why); if (c.kind == TransportKind::Serial && c.serialPort.empty()) return fail("Serial port is required"); *out = c; return true;
}
QVector<WorkflowNodeDefinition> workflowNodeDefinitions() {
    auto def = [](QString type, QString name, QString group, QString protocol, QJsonObject defaults, QStringList outputs = {"success", "error"}) { return WorkflowNodeDefinition{type, name, group, protocol, name, outputs, defaults}; };
    return {
        def("start", "开始", "流程控制", "START", {}, {"success"}), def("end", "结束", "流程控制", "END", {}, {}),
        def("delay", "延时", "流程控制", "DELAY", {{"duration", "1000"}}),
        def("branch", "条件分支", "流程控制", "IF", {{"variable", "status"}, {"expected", "ready"}, {"operator", "equals"}}, {"true", "false", "error"}),
        def("loop", "有限循环", "流程控制", "LOOP", {{"count", "3"}}, {"body", "done", "error"}),
        def("raw", "使用连接方案", "通信与消息", "SESSION", {{"ownership", "borrow"}}),
        def("send", "发送数据", "通信与消息", "SEND", {{"format", "HEX"}, {"payload", "AA 55 01 00"}, {"resource", "raw"}}),
        def("sendWait", "发送并等待", "通信与消息", "WAIT", {{"format", "文本 / UTF-8"}, {"payload", "{\"action\":\"subscribe\",\"topic\":\"device.status\"}"}, {"match", "JSON 字段"}, {"path", "$.type"}, {"expected", "status"}, {"timeout", "5000"}, {"output", "message"}}),
        def("wait", "等待消息", "通信与消息", "RECEIVE", {{"match", "字节包含"}, {"expected", "ready"}, {"timeout", "5000"}, {"output", "message"}}),
        def("http", "HTTP 请求", "HTTP / WebSocket", "HTTP", {{"method", "POST"}, {"url", "http://127.0.0.1:8080/api/token"}, {"headers", "Content-Type: application/json"}, {"body", "{\"device\":\"bench-01\"}"}, {"connectTimeout", "3000"}, {"timeout", "10000"}, {"limit", "8"}, {"expectedStatus", "200"}, {"output", "httpResponse"}}),
        def("ws", "WebSocket 连接", "HTTP / WebSocket", "WS", {{"url", "ws://127.0.0.1:8081/events"}, {"headers", "Authorization: Bearer ${token}"}, {"subprotocol", ""}, {"timeout", "5000"}, {"limit", "8"}}),
        def("close", "关闭连接", "HTTP / WebSocket", "CLOSE", {{"code", "1000"}, {"reason", "流程完成"}}),
        def("extract", "提取 JSON 字段", "变量与验证", "JSON", {{"source", "httpResponse.body"}, {"path", "$.token"}, {"variable", "token"}}),
        def("assert", "断言结果", "变量与验证", "ASSERT", {{"source", "message.device.state"}, {"expected", "ready"}, {"operator", "equals"}}),
        def("variable", "设置变量", "变量与验证", "VAR", {{"variable", "deviceId"}, {"value", "bench-01"}}),
        def("log", "步骤日志", "变量与验证", "LOG", {{"text", "设备响应已确认"}})
    };
}
QJsonObject WorkflowDocument::toJson() const {
    QJsonArray ns, es;
    for (const auto& n : nodes) ns.append(QJsonObject{{"id", n.id}, {"type", n.type}, {"title", n.title}, {"position", QJsonObject{{"x", n.position.x()}, {"y", n.position.y()}}}, {"parameters", n.parameters}});
    for (const auto& e : edges) es.append(QJsonObject{{"id", e.id}, {"from", e.from}, {"to", e.to}, {"port", e.port}});
    return {{"schemaVersion", 1}, {"workflowId", id}, {"name", name}, {"description", description}, {"nodes", ns}, {"edges", es}, {"variables", initialVariables}, {"limits", QJsonObject{{"maxSteps", limits.maxSteps}, {"maxDurationMs", limits.maxDurationMs}, {"maxMessageBytes", limits.maxMessageBytes}, {"observerQueueBytes", limits.observerQueueBytes}, {"maxVariableBytes", limits.maxVariableBytes}, {"maxLogEntries", limits.maxLogEntries}}}};
}
bool WorkflowDocument::fromJson(const QJsonObject& o, WorkflowDocument* out, QString* error) {
    if (error) error->clear();
    auto fail = [&](QString why) { if (error) *error = userMessage(why); return false; };
    if (!out || !boundedJson(o, 16 * 1024 * 1024)) return fail("Workflow exceeds JSON depth, entry or memory bounds");
    const QStringList root{"schemaVersion", "workflowId", "name", "description", "nodes", "edges", "variables", "limits"};
    for (auto it = o.begin(); it != o.end(); ++it) if (!root.contains(it.key())) return fail("Unsupported workflow field: " + it.key());
    if (!o["schemaVersion"].isDouble() || o["schemaVersion"].toDouble() != 1 || !o["nodes"].isArray() || !o["edges"].isArray() || !o["variables"].isObject() || !o["limits"].isObject()) return fail("Unsupported schema or invalid workflow fields");
    for (const auto& field : {"workflowId", "name", "description"}) if (!o[field].isString()) return fail("Invalid workflow metadata");
    if (o["nodes"].toArray().size() > 256 || o["edges"].toArray().size() > 512) return fail("Workflow exceeds 256 nodes or 512 edges");
    WorkflowDocument d; d.id = o["workflowId"].toString(); d.name = o["name"].toString(); d.description = o["description"].toString(); d.initialVariables = o["variables"].toObject();
    const auto l = o["limits"].toObject(); const auto defaults = d.toJson()["limits"].toObject();
    for (auto it = l.begin(); it != l.end(); ++it) if (!defaults.contains(it.key()) || !it.value().isDouble() || !integer(it.value(), 1, 67108864)) return fail("Invalid workflow limit: " + it.key());
    d.limits.maxSteps = number(l.value("maxSteps"), d.limits.maxSteps); d.limits.maxDurationMs = number(l.value("maxDurationMs"), d.limits.maxDurationMs); d.limits.maxMessageBytes = number(l.value("maxMessageBytes"), d.limits.maxMessageBytes); d.limits.observerQueueBytes = number(l.value("observerQueueBytes"), d.limits.observerQueueBytes); d.limits.maxVariableBytes = number(l.value("maxVariableBytes"), d.limits.maxVariableBytes); d.limits.maxLogEntries = number(l.value("maxLogEntries"), d.limits.maxLogEntries);
    for (const auto& item : o["nodes"].toArray()) {
        if (!item.isObject()) return fail("Node must be an object");
        const auto n = item.toObject();
        const QStringList fields{"id", "type", "title", "position", "parameters"}; for (auto it = n.begin(); it != n.end(); ++it) if (!fields.contains(it.key())) return fail("Unknown node field");
        if (!n["id"].isString() || !n["type"].isString() || !n["title"].isString() || !n["position"].isObject() || !n["parameters"].isObject()) return fail("Invalid node fields");
        const auto p = n["position"].toObject(); if (p.size() != 2 || !p["x"].isDouble() || !p["y"].isDouble() || !std::isfinite(p["x"].toDouble()) || !std::isfinite(p["y"].toDouble())) return fail("Invalid node position");
        d.nodes.append({n["id"].toString(), n["type"].toString(), n["title"].toString(), {p["x"].toDouble(), p["y"].toDouble()}, n["parameters"].toObject()});
    }
    for (const auto& item : o["edges"].toArray()) {
        if (!item.isObject()) return fail("Edge must be an object");
        const auto e = item.toObject();
        const QStringList fields{"id", "from", "to", "port"}; if (e.size() != 4) return fail("Invalid edge fields"); for (const auto& field : fields) if (!e[field].isString()) return fail("Invalid edge text");
        d.edges.append({e["id"].toString(), e["from"].toString(), e["to"].toString(), e["port"].toString()});
    }
    // Graph errors remain reviewable in the editor, but malformed/unknown schema
    // is rejected. Execution always requires validate() to pass.
    for (const auto& issue : d.validate()) if (issue.field == "type" || issue.field == "parameters" || issue.field == "limits") return fail(issue.message);
    *out = std::move(d); return true;
}
QVector<WorkflowIssue> WorkflowDocument::validate() const {
    QVector<WorkflowIssue> issues; auto issue = [&](QString id, QString field, QString message) { if (issues.size() < 1024) issues.append({id, field, userMessage(message)}); };
    if (nodes.size() > 256 || edges.size() > 512) { issue({}, "limits", "Workflow exceeds 256 nodes or 512 edges"); return issues; }
    if (limits.maxSteps < 1 || limits.maxSteps > 100000 || limits.maxDurationMs < 1 || limits.maxDurationMs > 3600000 || limits.maxMessageBytes < 1 || limits.maxMessageBytes > 16 * 1024 * 1024 || limits.observerQueueBytes < 256 || limits.observerQueueBytes > 64 * 1024 * 1024 || limits.maxVariableBytes < 256 || limits.maxVariableBytes > 64 * 1024 * 1024 || limits.maxLogEntries < 1 || limits.maxLogEntries > 10000) issue({}, "limits", "Workflow limits out of supported bounds");
    if (!boundedJson(initialVariables, std::max(256, limits.maxVariableBytes))) issue({}, "variables", "Initial variables exceed bounds");
    QMap<QString, WorkflowNodeDefinition> defs; for (const auto& d : workflowNodeDefinitions()) defs[d.type] = d;
    QMap<QString, const WorkflowNode*> index; int starts = 0, ends = 0; QString start;
    const QStringList common{"resource", "output"};
    const QMap<QString, QStringList> extras{
        {"raw", {"config", "profile", "ownership", "timeout"}},
        {"send", {"target", "clientId", "broadcast", "encoding", "eol", "messageType"}},
        {"sendWait", {"target", "clientId", "broadcast", "encoding", "eol", "messageType", "framing", "sourceFilter", "expectedFormat", "operator"}},
        {"wait", {"framing", "sourceFilter", "messageType", "expectedFormat", "path", "operator"}},
        {"http", {"bodyMode", "verifyTls", "caFile", "followRedirects", "maxResponseBytes", "connectTimeoutMs", "timeoutMs"}},
        {"ws", {"verifyTls", "caFile", "maxMessageBytes", "timeoutMs"}},
        {"extract", {"encoding"}}, {"assert", {"relation"}}, {"branch", {"source", "relation"}}, {"variable", {"valueType"}}, {"end", {"success", "error"}}
    };
    for (const auto& n : nodes) {
        if (n.id.isEmpty() || n.id.size() > 128 || index.contains(n.id)) issue(n.id, "id", "Node IDs must be unique and bounded"); else index[n.id] = &n;
        if (!defs.contains(n.type)) { issue(n.id, "type", "Unsupported node type: " + n.type); continue; }
        if (n.title.size() > 256 || !std::isfinite(n.position.x()) || !std::isfinite(n.position.y()) || std::abs(n.position.x()) > 1000000 || std::abs(n.position.y()) > 1000000) issue(n.id, "title", "Node title/position exceeds bounds");
        if (n.type == "start") { ++starts; start = n.id; } if (n.type == "end") ++ends;
        const auto& p = n.parameters; if (!boundedJson(p, 16 * 1024 * 1024)) issue(n.id, "parameters", "Node parameters exceed bounds");
        if (p.contains("verifyTls") && !p["verifyTls"].isBool()) issue(n.id, "verifyTls", "TLS verification must be a boolean");
        if (p.contains("caFile") && (!p["caFile"].isString() || p["caFile"].toString().size() > 4096)) issue(n.id, "caFile", "CA file path must be bounded text");
        for (const auto& capacity : {"maxResponseBytes", "maxMessageBytes"}) if (p.contains(capacity) && !integer(p[capacity], 1, limits.maxMessageBytes)) issue(n.id, capacity, "Response or message capacity must fit the workflow limit");
        if (p.contains("resource") && !QStringList{"raw", "ws"}.contains(text(p["resource"]))) issue(n.id, "resource", "Resource must be raw or ws");
        auto allowed = defs[n.type].defaults.keys(); allowed += common; allowed += extras.value(n.type);
        for (auto it = p.begin(); it != p.end(); ++it) if (!allowed.contains(it.key())) issue(n.id, "parameters", "Unknown parameter: " + it.key());
        Parameters merged = defs[n.type].defaults; for (auto it = p.begin(); it != p.end(); ++it) merged[it.key()] = it.value();
        for (const auto& k : {"duration", "timeout", "connectTimeout", "timeoutMs", "connectTimeoutMs", "count", "limit"}) if (merged.contains(k) && !integer(merged[k], 1, QString(k) == "limit" ? 16 : QString(k) == "count" ? 100000 : 3600000)) issue(n.id, k, "Time, count or capacity must be a bounded positive integer");
        for (const auto& k : {"source", "variable", "path", "output"}) if (merged.contains(k) && !safePath(merged[k].toString())) issue(n.id, k, "Only safe dotted field paths are supported");
        if (n.type == "http" || n.type == "ws") {
            const auto total = merged.value("timeoutMs", merged.value("timeout", 10000));
            const auto connect = merged.value("connectTimeoutMs", merged.value("connectTimeout", total));
            if (!integer(total, 1, 60000) || !integer(connect, 1, 60000) || number(connect, -1) > number(total, -1)) issue(n.id, "timeout", "Protocol timeout must be 1..60000 ms and connect timeout cannot exceed total timeout");
            if (!integer(merged.value("limit", 8), 1, 8)) issue(n.id, "limit", "HTTP/WebSocket message capacity must be 1..8 MiB");
            for (const auto& capacity : {"maxResponseBytes", "maxMessageBytes"}) if (merged.contains(capacity) && !integer(merged[capacity], 1, std::min(limits.maxMessageBytes, 8 * 1024 * 1024))) issue(n.id, capacity, "HTTP/WebSocket message capacity must be 1..8 MiB");
        }
        if (n.type == "http" || n.type == "ws") { const auto u = QUrl(merged["url"].toString()); const auto schemes = n.type == "http" ? QStringList{"http", "https"} : QStringList{"ws", "wss"}; if (!u.isValid() || !schemes.contains(u.scheme()) || u.host().isEmpty() || u.port(1) < 1 || !u.userInfo().isEmpty()) issue(n.id, "url", "Invalid protocol URL or embedded credentials"); }
        if (n.type == "http") { const auto status = text(merged["expectedStatus"]); if (status != "2xx" && status != "any" && status != "任意状态" && !status.isEmpty() && !integer(status, 100, 599)) issue(n.id, "expectedStatus", "Expected HTTP status must be any, 2xx, or 100..599"); if (!QStringList{"GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS"}.contains(text(merged["method"]))) issue(n.id, "method", "Unsupported HTTP method"); }
        if (n.type == "raw") { const auto ownership = text(merged["ownership"]); if (!QStringList{"borrow", "owned", "借用已有连接", "流程建立并释放"}.contains(ownership)) issue(n.id, "ownership", "Choose borrowed or owned resource"); if (ownership == "owned" || ownership == "流程建立并释放") { ConnectionConfig c; QString why; if (!p["config"].isObject() || !workflowConnectionConfigFromJson(p["config"].toObject(), &c, &why)) issue(n.id, "config", "Owned raw resource requires an explicit valid config snapshot: " + why); } }
        if (p.contains("target") && !text(p["target"]).isEmpty()) { const auto u = QUrl("udp://" + text(p["target"])); if (!u.isValid() || u.host().isEmpty() || u.port() < 1 || u.port() > 65535 || !u.userInfo().isEmpty() || !u.path().isEmpty()) issue(n.id, "target", "Target must be host:port (IPv6 in brackets)"); }
        if (p.contains("framing")) { const Parameters f = p["framing"].toObject(); const auto mode = f["mode"].toString(); const QStringList keys{"mode", "delimiter", "delimiterFormat", "includeDelimiter", "length", "headerBytes", "offset", "byteOrder", "includesHeader"}; for (auto it = f.begin(); it != f.end(); ++it) if (!keys.contains(it.key())) issue(n.id, "framing", "Unknown framing field"); if (!p["framing"].isObject() || !QStringList{"delimiter", "fixed", "lengthHeader"}.contains(mode)) issue(n.id, "framing", "Choose delimiter, fixed, or lengthHeader stream framing"); if (mode == "fixed" && !integer(f["length"], 1, limits.maxMessageBytes)) issue(n.id, "framing", "Fixed frame length out of bounds"); if (mode == "delimiter" && (text(f["delimiter"]).isEmpty() || text(f["delimiter"]).size() > 256)) issue(n.id, "framing", "Delimiter must contain 1..256 characters"); if (mode == "lengthHeader" && (!integer(f["headerBytes"], 1, 4) || !integer(f.value("offset", 0), 0, 64) || !QStringList{"big", "little"}.contains(text(f.value("byteOrder", "big"))))) issue(n.id, "framing", "Length header settings invalid"); }
        if (p.contains("sourceFilter")) { const auto f = p["sourceFilter"].toObject(); for (auto it = f.begin(); it != f.end(); ++it) if (!QStringList{"address", "port", "clientId"}.contains(it.key())) issue(n.id, "sourceFilter", "Unknown source filter field"); if (!p["sourceFilter"].isObject() || (f.contains("port") && !integer(f["port"], 1, 65535)) || (f.contains("address") && (text(f["address"]).isEmpty() || text(f["address"]).size() > 253))) issue(n.id, "sourceFilter", "Invalid receive source filter"); }
        if (n.type == "send" || n.type == "sendWait") { const auto format = text(merged["format"]); if (!QStringList{"HEX", "hex", "text", "UTF-8", "文本 / UTF-8", "binary", "二进制变量"}.contains(format)) issue(n.id, "format", "Unsupported payload format"); if ((format == "HEX" || format == "hex") && !text(merged["payload"]).contains("${")) { QString why; encodePayload(text(merged["payload"]), true, "UTF-8", "none", &why); if (!why.isEmpty()) issue(n.id, "payload", why); } }
        if (n.type == "wait" || n.type == "sendWait") { if (!QStringList{"json", "JSON 字段", "prefix", "字节前缀", "contains", "字节包含", "equals", "字节相等", "any"}.contains(text(merged["match"]))) issue(n.id, "match", "Unsupported message matcher"); }
        if (n.type == "assert" || n.type == "branch") { if (!QStringList{"equals", "notEquals", "contains", "greater", "less", "exists"}.contains(text(merged.value("relation", merged.value("operator", "equals"))))) issue(n.id, "operator", "Unsupported comparison relation"); }
    }
    if (starts != 1) issue({}, "nodes", "Exactly one start node is required");
    if (!ends) issue({}, "nodes", "An end node is required");
    QSet<QString> edgeIds, outlets; QMap<QString, QStringList> adjacency;
    for (const auto& e : edges) {
        if (e.id.isEmpty() || e.id.size() > 128 || edgeIds.contains(e.id)) issue(e.from, "edges", "Edge IDs must be unique and bounded");
        edgeIds.insert(e.id);
        if (!index.contains(e.from) || !index.contains(e.to)) { issue(e.from, "edges", "Edge endpoint does not exist"); continue; }
        if (index[e.to]->type == "start" || e.from == e.to || !defs.value(index[e.from]->type).outputs.contains(e.port)) issue(e.from, "edges", "Invalid edge endpoint or output port");
        const auto outlet = e.from + "/" + e.port; if (outlets.contains(outlet)) issue(e.from, "edges", "A control output can have only one edge"); outlets.insert(outlet); adjacency[e.from].append(e.to);
    }
    QSet<QString> reachable; std::function<void(QString)> walk = [&](QString id) { if (reachable.contains(id)) return; reachable.insert(id); for (const auto& to : adjacency.value(id)) walk(to); }; if (starts == 1) walk(start);
    for (const auto& n : nodes) {
        if (!reachable.contains(n.id)) issue(n.id, "edges", "Node is unreachable from start");
        const auto needed = n.type == "branch" ? QStringList{"true", "false"} : n.type == "loop" ? QStringList{"body", "done"} : n.type == "end" ? QStringList{} : QStringList{"success"};
        for (const auto& port : needed) if (!outlets.contains(n.id + "/" + port)) issue(n.id, "edges", "Missing control output: " + port);
    }
    // Removing loop nodes must make the graph acyclic: every return path must
    // pass a finite loop controller rather than an ordinary operation.
    QMap<QString, int> colors; std::function<void(QString)> dfs = [&](QString id) {
        if (!index.contains(id) || index[id]->type == "loop") return;
        if (colors[id] == 1) { issue(id, "edges", "Cycle must return through a bounded loop node"); return; }
        if (colors[id] == 2) return;
        colors[id] = 1; for (const auto& to : adjacency.value(id)) dfs(to); colors[id] = 2;
    }; for (const auto& n : nodes) dfs(n.id);
    // Every cycle must traverse a body edge, not only touch a loop node.
    // This rejects done-edge cycles while permitting nested finite loops.
    QMap<QString, QStringList> withoutBody;
    for (const auto& e : edges) if (index.contains(e.from) && !(index[e.from]->type == "loop" && e.port == "body")) withoutBody[e.from].append(e.to);
    colors.clear();
    std::function<void(QString)> finiteDfs = [&](QString id) {
        if (colors[id] == 1) { issue(id, "edges", "Loop done edge cannot form a cycle"); return; }
        if (colors[id] == 2) return;
        colors[id] = 1; for (const auto& to : withoutBody.value(id)) finiteDfs(to); colors[id] = 2;
    };
    for (const auto& n : nodes) finiteDfs(n.id);
    for (const auto& n : nodes) if (n.type == "loop") {
        QString body; for (const auto& e : edges) if (e.from == n.id && e.port == "body") body = e.to;
        QSet<QString> visited; std::function<bool(QString)> returns = [&](QString id) { if (id == n.id) return true; if (visited.contains(id)) return false; visited.insert(id); for (const auto& to : adjacency.value(id)) if (returns(to)) return true; return false; };
        if (!body.isEmpty() && !returns(body)) issue(n.id, "edges", "Loop body must return to its loop controller");
    }
    return issues;
}
WorkflowDocument WorkflowDocument::templateDocument(const QString& key) {
    WorkflowDocument d; d.id = QUuid::createUuid().toString(QUuid::WithoutBraces); d.name = key == "login" ? "设备登录与订阅" : key == "udp" ? "UDP 指令响应" : "未命名流程";
    QMap<QString, WorkflowNodeDefinition> defs; for (const auto& def : workflowNodeDefinitions()) defs[def.type] = def;
    const QStringList types = key == "login" ? QStringList{"start", "http", "extract", "ws", "sendWait", "assert", "close", "end"} : key == "udp" ? QStringList{"start", "raw", "sendWait", "assert", "end"} : QStringList{"start", "end"};
    for (int i = 0; i < types.size(); ++i) { const auto def = defs[types[i]]; d.nodes.append({QString("n%1").arg(i + 1), def.type, def.name, QPointF(40 + (i % 4) * 280, 80 + (i / 4) * 240), def.defaults}); if (i) d.edges.append({QString("e%1").arg(i), d.nodes[i - 1].id, d.nodes[i].id, "success"}); }
    if (key == "udp") { d.nodes[2].parameters = {{"resource", "raw"}, {"format", "HEX"}, {"payload", "AA 55 01 00"}, {"match", "字节前缀"}, {"expectedFormat", "HEX"}, {"expected", "AA 55"}, {"timeout", "3000"}}; d.nodes[3].parameters = {{"source", "message.length"}, {"expected", "8"}}; }
    if (key == "login") d.nodes[4].parameters["resource"] = "ws";
    return d;
}
}
