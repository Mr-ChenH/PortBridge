#include "workflow_private.hpp"
#include "workflow_messages.hpp"
#include <algorithm>
#include "portbridge/workflow_protocol.hpp"
#include <QDateTime>
#include <QElapsedTimer>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QUuid>
#include <QUrl>
#include <deque>

namespace portbridge {
using namespace workflowDetail;
struct WorkflowRunner::Impl {
    WorkflowRunner* q;
    std::function<SessionController*()> sessionProvider;
    std::function<QVector<ConnectionConfig>()> profilesProvider;
    WorkflowDocument plan;
    QMap<QString, WorkflowNode> nodes;
    QMap<QString, QString> edges;
    QMap<QString, WorkflowNodeResult> results;
    QMap<QString, int> loops;
    QJsonObject vars;
    QVector<WorkflowLogEntry> entries;
    WorkflowRunState state = WorkflowRunState::Idle;
    QString id, current, next, operation, resource, waitingKind;
    QPointer<SessionController> provided, raw;
    QPointer<WorkflowProtocolClient> protocol;
    bool ownsRaw = false, scheduled = false, inOperation = false;
    std::uint64_t generation = 0, rawEpoch = 0, providedEpoch = 0, observer = 0, operationSequence = 0;
    int steps = 0;
    qint64 deadline = 0, delayEnd = 0;
    QElapsedTimer clock, nodeClock;
    QTimer timer;
    struct RuntimeParameters : Parameters {
        using Parameters::operator=;
        QJsonValue operator[](const QString& key) const { return value(key); }
    };
    RuntimeParameters params;
    QByteArray assembly;
    std::uint64_t assemblyClient = 0;
    explicit Impl(WorkflowRunner* owner) : q(owner) {
        timer.setInterval(5); timer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&timer, &QTimer::timeout, q, [this] { tick(); });
    }
    bool active() const { return state == WorkflowRunState::Running || state == WorkflowRunState::Paused; }
    bool live(std::uint64_t token) const { return active() && generation == token; }
    QString freshOperation() { return id + "/op/" + QString::number(++operationSequence); }
    void disarm() { if (observer && raw) raw->removeRawObserver(observer); observer = 0; assembly.clear(); assemblyClient = 0; }
    void releaseRaw() {
        disarm(); auto old = raw; const bool owned = ownsRaw; const auto epoch = rawEpoch;
        raw = nullptr; rawEpoch = 0; ownsRaw = false;
        // A manual replacement belongs to the user, even if this runner owned
        // the previous epoch. Never close the replacement during run cleanup.
        if (old && owned && old->sessionEpoch() == epoch) old->stop();
    }
    void releaseProtocol() {
        auto old = protocol; protocol = nullptr;
        if (old) { QObject::disconnect(old, nullptr, q, nullptr); old->cancelAll(); old->deleteLater(); }
    }
    void finish(WorkflowRunState outcome, const QString& why) {
        if (!active()) return;
        const auto token = ++generation;
        state = outcome; timer.stop(); scheduled = false; inOperation = false; operation.clear(); waitingKind.clear(); next.clear();
        // Finish retained node state before cleanup can emit reentrant signals.
        if (results.contains(current) && results[current].state == WorkflowNodeState::Running) {
            auto& result = results[current]; result.state = WorkflowNodeState::Failed;
            result.detail = userMessage(why).left(2048); result.elapsedMs = nodeClock.elapsed();
            result.output = {{"cancelled", outcome == WorkflowRunState::Stopped}};
            if (entries.size() >= plan.limits.maxLogEntries) entries.removeFirst();
            entries.append({current, nodes.value(current).title, outcome == WorkflowRunState::Stopped ? "已停止" : "失败", result.detail, QDateTime::currentMSecsSinceEpoch(), result.elapsedMs});
        }
        for (auto it = results.begin(); it != results.end(); ++it) if (it->state == WorkflowNodeState::Pending) it->state = WorkflowNodeState::Skipped;
        resource.clear(); releaseProtocol(); releaseRaw();
        if (generation != token) return;
        emit q->nodeChanged(current); if (generation != token) return;
        emit q->logsChanged(); if (generation != token) return;
        emit q->resourceChanged(); if (generation != token) return;
        emit q->stateChanged(); if (generation != token) return;
        emit q->finished(outcome == WorkflowRunState::Completed, userMessage(why));
    }
    bool storeVariable(const QString& key, const QJsonValue& value, QString* why) {
        static const QRegularExpression name("^[A-Za-z_][A-Za-z0-9_]*$");
        if (key.size() > 128 || !name.match(key).hasMatch()) { *why = "Output variable must be a simple bounded identifier"; return false; }
        auto candidate = vars; candidate[key] = value;
        if (!boundedJson(candidate, plan.limits.maxVariableBytes)) { *why = "Runtime variables exceed memory/depth/entry bounds"; return false; }
        vars = std::move(candidate); return true;
    }
    QJsonValue substitute(const QJsonValue& value, QString* why, int depth = 0) {
        if (depth > 16) { *why = "Substitution exceeds JSON depth limit"; return {}; }
        if (value.isObject()) { QJsonObject o; const auto source = value.toObject(); for (auto it = source.begin(); it != source.end(); ++it) { o[it.key()] = substitute(it.value(), why, depth + 1); if (!why->isEmpty()) return {}; } return o; }
        if (value.isArray()) { QJsonArray a; for (const auto& item : value.toArray()) { a.append(substitute(item, why, depth + 1)); if (!why->isEmpty()) return {}; } return a; }
        if (!value.isString()) return value;
        const auto input = value.toString(); static const QRegularExpression pattern("\\$\\{([^{}]+)\\}");
        auto matches = pattern.globalMatch(input); QString output; qsizetype position = 0;
        while (matches.hasNext()) {
            const auto match = matches.next(); bool ok; const auto found = lookup(vars, match.captured(1), &ok);
            if (!ok) { *why = "Missing substitution variable: " + match.captured(1); return {}; }
            if (match.capturedStart() == 0 && match.capturedLength() == input.size()) return found;
            output += input.mid(position, match.capturedStart() - position); output += text(found); position = match.capturedEnd();
            if (output.size() > plan.limits.maxMessageBytes) { *why = "Substituted text exceeds message limit"; return {}; }
        }
        output += input.mid(position); if (output.size() > plan.limits.maxMessageBytes) { *why = "Substituted text exceeds message limit"; return {}; }
        return output;
    }
    bool compare(const QJsonValue& actual, const QJsonValue& expected, const QString& relation, bool* valid) {
        *valid = true;
        if (relation == "exists") return !actual.isUndefined();
        if (relation == "equals") return actual == expected || text(actual) == text(expected);
        if (relation == "notEquals") return !(actual == expected || text(actual) == text(expected));
        if (relation == "contains") return text(actual).contains(text(expected));
        bool aOk, bOk; const auto a = text(actual).toDouble(&aOk), b = text(expected).toDouble(&bOk);
        if (!aOk || !bOk || !std::isfinite(a) || !std::isfinite(b)) { *valid = false; return false; }
        if (relation == "greater") return a > b;
        if (relation == "less") return a < b;
        *valid = false; return false;
    }
    bool payload(QByteArray* out, QString* why) {
        const auto format = text(params["format"]);
        if (format == "binary" || format == "二进制变量") {
            bool ok; const auto v = lookup(vars, text(params["payload"]), &ok); if (!ok || !v.isObject() || !v.toObject()["bytesBase64"].isString()) { *why = "Binary payload variable must contain bytesBase64"; return false; }
            const auto decoded = QByteArray::fromBase64Encoding(v.toObject()["bytesBase64"].toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors); if (!decoded) { *why = "Invalid binary variable base64"; return false; } *out = decoded.decoded;
        } else *out = encodePayload(text(params["payload"]), format == "HEX" || format == "hex", text(params.value("encoding", "UTF-8")), text(params.value("eol", "none")), why);
        if (out->size() > plan.limits.maxMessageBytes) *why = "Send payload exceeds message limit";
        return why->isEmpty();
    }
    void queueStep() {
        if (!active() || state == WorkflowRunState::Paused || inOperation || scheduled) return;
        scheduled = true; const auto token = generation;
        QTimer::singleShot(0, q, [this, token] { if (generation != token) return; scheduled = false; if (live(token) && state == WorkflowRunState::Running && !inOperation) step(); });
    }
    void complete(bool success, QJsonObject output = {}, QString detail = {}, QString port = {}) {
        if (!active() || !inOperation) return;
        const auto token = generation; const auto node = nodes.value(current);
        disarm(); inOperation = false; waitingKind.clear(); operation.clear();
        if (!boundedJson(output, plan.limits.maxVariableBytes)) { success = false; output = {}; detail = "Node output exceeds memory/depth/entry bounds"; }
        // All retained node results share the same finite aggregate budget.
        QJsonObject retained; for (auto it = results.begin(); it != results.end(); ++it) if (it.key() != current) retained[it.key()] = it->output; retained[current] = output;
        if (!boundedJson(retained, plan.limits.maxVariableBytes)) { success = false; output = {}; detail = "Aggregate node results exceed memory bounds"; }
        if (node.type != "log") detail = userMessage(detail);
        results[current] = {success ? WorkflowNodeState::Succeeded : WorkflowNodeState::Failed, output, detail.left(2048), nodeClock.elapsed()};
        WorkflowLogEntry entry{current, node.title, success ? "成功" : "失败", detail.left(2048), QDateTime::currentMSecsSinceEpoch(), nodeClock.elapsed()};
        if (entries.size() >= plan.limits.maxLogEntries) entries.removeFirst();
        entries.append(entry);
        if (port.isEmpty()) port = success ? "success" : "error";
        const auto following = edges.value(current + "/" + port);
        next = following;
        emit q->nodeChanged(current); if (!live(token)) return;
        emit q->logsChanged(); if (!live(token)) return;
        emit q->variablesChanged(); if (!live(token)) return;
        if (!success && following.isEmpty()) { finish(WorkflowRunState::Failed, detail); return; }
        if (node.type == "end") { finish(success ? WorkflowRunState::Completed : WorkflowRunState::Failed, success ? QString{} : detail); return; }
        if (following.isEmpty()) { finish(WorkflowRunState::Failed, "Missing execution output: " + port); return; }
        queueStep();
    }
    void ensureProtocol() {
        if (protocol) return;
        auto client = new WorkflowProtocolClient(q); protocol = client; const auto token = generation;
        QObject::connect(client, &WorkflowProtocolClient::operationFinished, q, [this, token](const QString& op, const QJsonObject& result, const QString& error) {
            if (!live(token) || !inOperation || op != operation) return;
            if (!error.isEmpty()) { complete(false, result, error); return; }
            const auto kind = waitingKind;
            if (kind == "http") {
                QString why; const auto outputName = text(params.value("output", "httpResponse"));
                if (!storeVariable(outputName, result, &why)) { complete(false, result, why); return; }
                const auto expected = text(params.value("expectedStatus", "any")); const auto status = number(result["status"], 0);
                const bool ok = expected.isEmpty() || expected == "any" || expected == "任意状态" || (expected == "2xx" ? status >= 200 && status < 300 : status == number(expected, -1));
                complete(ok, result, ok ? QString("HTTP %1 response received").arg(status) : QString("HTTP %1 does not match expected %2").arg(status).arg(expected));
            } else if (kind == "wsConnect") { resource = "ws"; emit q->resourceChanged(); if (live(token)) complete(true, result, "WebSocket handshake completed"); }
            else if (kind == "wsSendWait") { /* wait remains armed until business response or deadline */ }
            else if (kind == "wsClose") { resource.clear(); emit q->resourceChanged(); if (live(token)) complete(true, result, "WebSocket closed"); }
            else complete(true, result, "WebSocket send completed locally");
        });
        QObject::connect(client, &WorkflowProtocolClient::webSocketClosed, q, [this, token](int code, const QString& reason, bool peerInitiated) {
            if (!live(token) || !peerInitiated) return;
            const auto detail = QStringLiteral("WebSocket 对端关闭（代码 %1）：%2").arg(code).arg(reason.left(256));
            const QJsonObject close{{"closed", true}, {"peerInitiated", true}, {"peerCode", code}, {"peerReason", reason}};
            resource.clear(); emit q->resourceChanged(); if (!live(token)) return;
            if (inOperation) complete(false, close, detail);
            else finish(WorkflowRunState::Failed, detail);
        });
        QObject::connect(client, &WorkflowProtocolClient::webSocketMessage, q, [this, token](const QByteArray& data, bool binary) { if (live(token) && inOperation && (waitingKind == "wsWait" || waitingKind == "wsSendWait")) message(data, binary); });
        QObject::connect(client, &WorkflowProtocolClient::protocolError, q, [this, token](const QString& error) { if (!live(token)) return; if (inOperation) complete(false, {}, error); else finish(WorkflowRunState::Failed, error); });
        QObject::connect(client, &WorkflowProtocolClient::stateChanged, q, [this, token] { if (!live(token)) return; emit q->resourceChanged(); if (!live(token)) return; if (resource == "ws" && !protocol->webSocketConnected() && waitingKind != "wsClose") { if (inOperation) complete(false, {}, "WebSocket disconnected"); else finish(WorkflowRunState::Failed, "WebSocket disconnected"); } });
    }
    bool armWait(QString* why) {
        const auto timeout = number(params.value("timeout", 5000), 5000); deadline = clock.elapsed() + timeout;
        if (resource == "ws") { waitingKind = "wsWait"; return true; }
        if (resource != "raw" || !raw || !raw->connected()) { *why = "A connected raw or WebSocket resource is required"; return false; }
        if (raw->config().kind != TransportKind::Udp && !params["framing"].isObject()) { *why = "TCP/serial waiting requires explicit stream framing"; return false; }
        // A TCP server wait without client filtering mixes independent streams.
        if (raw->config().kind == TransportKind::TcpServer && text(params["sourceFilter"].toObject()["clientId"]).isEmpty()) { *why = "TCP server wait requires a sourceFilter.clientId"; return false; }
        disarm(); observer = raw->observeRaw(size_t(plan.limits.observerQueueBytes), why);
        if (!observer) return false;
        waitingKind = "rawWait"; return true;
    }
    bool sendRaw(const QByteArray& bytes, QString* why) {
        if (!raw || resource != "raw" || !raw->connected() || raw->sessionEpoch() != rawEpoch) { *why = "Raw resource is disconnected or replaced"; return false; }
        bool idOk = true; std::uint64_t client = 0; if (params.contains("clientId")) client = text(params["clientId"]).toULongLong(&idOk);
        if (!idOk) { *why = "Invalid TCP client ID"; return false; }
        std::optional<Endpoint> target;
        if (!text(params["target"]).isEmpty()) { const auto u = QUrl("udp://" + text(params["target"])); if (!u.isValid() || u.host().isEmpty() || u.port() < 1 || u.port() > 65535) { *why = "Invalid request target"; return false; } target = Endpoint{u.host().toStdString(), std::uint16_t(u.port())}; }
        if (!raw->send(bytes, client, params["broadcast"].toBool(), target)) { *why = "Send was rejected before admission"; return false; }
        return true;
    }
    void step() {
        if (!active() || state != WorkflowRunState::Running || inOperation) return;
        if (clock.elapsed() >= plan.limits.maxDurationMs || ++steps > plan.limits.maxSteps) { finish(WorkflowRunState::Failed, "Workflow deadline or maximum steps exceeded"); return; }
        const auto token = generation; current = next; const auto node = nodes.value(current);
        if (node.id.isEmpty()) { finish(WorkflowRunState::Failed, "Execution node does not exist"); return; }
        inOperation = true; nodeClock.restart(); results[current].state = WorkflowNodeState::Running;
        emit q->nodeChanged(current); if (!live(token)) return;
        if (resource == "raw" && (!raw || raw->sessionEpoch() != rawEpoch)) { finish(WorkflowRunState::Failed, "Raw session was destroyed or manually replaced"); return; }
        QJsonObject input = node.parameters;
        // Parse header structure before expansion. A newline inside a retrieved
        // token must remain inside one value for protocol validation, never
        // become an additional Name: value line. JSON quotes follow the same rule.
        if ((node.type == "http" || node.type == "ws") && input["headers"].isString()) {
            const auto headers = input["headers"].toString();
            if (headers.size() > 16 * 1024) { complete(false, {}, "Request headers exceed limit"); return; }
            const auto trimmed = headers.trimmed();
            static const QRegularExpression wholeVariable("^\\$\\{[^{}]+\\}$");
            if (trimmed.startsWith('{')) {
                QJsonParseError error; const auto parsed = QJsonDocument::fromJson(trimmed.toUtf8(), &error);
                if (error.error != QJsonParseError::NoError || !parsed.isObject()) { complete(false, {}, "Headers must be a JSON object or Name: value lines"); return; }
                input["headers"] = parsed.object();
            } else if (!wholeVariable.match(trimmed).hasMatch()) {
                QJsonObject parsed; QSet<QString> names; const auto lines = headers.split('\n');
                if (lines.size() > 64) { complete(false, {}, "Too many request headers"); return; }
                for (const auto& rawLine : lines) {
                    const auto line = rawLine.trimmed(); if (line.isEmpty()) continue;
                    const auto colon = line.indexOf(':'); if (colon <= 0) { complete(false, {}, "Headers must contain Name: value lines"); return; }
                    const auto name = line.left(colon).trimmed();
                    if (names.contains(name.toLower())) { complete(false, {}, "Duplicate request header"); return; }
                    names.insert(name.toLower()); parsed[name] = line.mid(colon + 1).trimmed();
                }
                input["headers"] = parsed;
            } else input["headers"] = trimmed;
        }
        // JSON request bodies are parsed before variable substitution so quotes,
        // backslashes and object-valued tokens are encoded by the JSON serializer.
        if (node.type == "http" && input["body"].isString()) {
            const auto body = input["body"].toString(); QJsonParseError e; const auto doc = QJsonDocument::fromJson(body.toUtf8(), &e);
            const bool jsonMode = text(input["bodyMode"]) == "json" || (text(input["bodyMode"]).isEmpty() && (body.trimmed().startsWith('{') || body.trimmed().startsWith('[')));
            if (jsonMode) { if (e.error != QJsonParseError::NoError) { complete(false, {}, "JSON request body is invalid"); return; } input["body"] = doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array()); }
        }
        QString why; params = substitute(input, &why).toObject();
        if (!why.isEmpty() || !boundedJson(params, 16 * 1024 * 1024)) { complete(false, {}, why.isEmpty() ? "Resolved parameters exceed bounds" : why); return; }
        if ((node.type == "http" || node.type == "ws") && !params["headers"].isUndefined() && !params["headers"].isNull() && !params["headers"].isObject()) { complete(false, {}, "Headers variable must be an object"); return; }
        operation = freshOperation();
        const auto type = node.type;
        if (type == "start") { complete(true, {{"runId", id}}, "Run started with immutable configuration"); return; }
        if (type == "end") { const bool ok = params.value("success", true).toBool(true); complete(ok, {}, ok ? "Workflow completed" : text(params.value("error", "Failure end reached"))); return; }
        if (type == "delay") { waitingKind = "delay"; delayEnd = clock.elapsed() + number(params.value("duration", 1000), 1000); return; }
        if (type == "loop") { const int count = number(params.value("count", 3), 3); auto& iteration = loops[current]; const bool body = iteration < count; if (body) ++iteration; else iteration = 0; complete(true, {{"iteration", iteration}, {"count", count}}, body ? "Loop body" : "Loop completed", body ? "body" : "done"); return; }
        if (type == "variable") { const auto key = text(params["variable"]); auto value = params["value"]; const auto valueType = text(params["valueType"]); if (valueType == "number") { bool ok; double n = text(value).toDouble(&ok); if (!ok || !std::isfinite(n)) { complete(false, {}, "Variable is not a finite number"); return; } value = n; } else if (valueType == "bool") { const auto t = text(value); if (t != "true" && t != "false") { complete(false, {}, "Variable must be true or false"); return; } value = t == "true"; } if (!storeVariable(key, value, &why)) { complete(false, {}, why); return; } complete(true, {{"variable", key}, {"value", value}}, "Variable updated"); return; }
        if (type == "extract") { bool ok; auto inputValue = lookup(vars, text(params["source"]), &ok); if (!ok) { complete(false, {}, "Missing input field: " + text(params["source"])); return; } auto value = lookup(inputValue, text(params["path"]), &ok); if (!ok) { complete(false, {}, "Missing JSON field: " + text(params["path"])); return; } const auto key = text(params["variable"]); if (!storeVariable(key, value, &why)) { complete(false, {}, why); return; } complete(true, {{"variable", key}, {"value", value}}, "JSON field extracted"); return; }
        if (type == "assert" || type == "branch") { const auto source = text(params.value("source", params.value("variable"))); bool found; const auto actual = lookup(vars, source, &found); const auto relation = text(params.value("relation", params.value("operator", "equals"))); if (!found && relation != "exists") { complete(false, {}, "Missing input field: " + source); return; } bool valid; const bool matched = compare(actual, params["expected"], relation, &valid); if (!valid) { complete(false, {}, "Comparison requires valid relation and operand types"); return; } QJsonObject output{{"actual", actual}, {"expected", params["expected"]}, {"matched", matched}}; complete(type == "branch" || matched, output, matched ? "Condition passed" : "Condition did not match", type == "branch" ? (matched ? "true" : "false") : QString{}); return; }
        if (type == "log") { complete(true, {}, text(params["text"]).left(2048)); return; }
        if (type == "raw") {
            if (!resource.isEmpty()) { complete(false, {}, "Close the current persistent resource before binding raw"); return; }
            if (!provided) { complete(false, {}, "No stable raw session was provided"); return; }
            const auto ownership = text(params.value("ownership", "borrow")); ownsRaw = ownership == "owned" || ownership == "流程建立并释放"; raw = provided;
            if (ownsRaw) { if (raw->connected() || raw->connecting()) { raw = nullptr; ownsRaw = false; complete(false, {}, "Existing session must be explicitly stopped before establishing an owned resource"); return; } ConnectionConfig config; if (!workflowConnectionConfigFromJson(params["config"].toObject(), &config, &why)) { raw = nullptr; ownsRaw = false; complete(false, {}, why); return; } const auto expectedEpoch = raw->sessionEpoch() + 1; rawEpoch = expectedEpoch; raw->start(config); if (!live(token)) return; if (raw->sessionEpoch() != expectedEpoch) { raw = nullptr; ownsRaw = false; complete(false, {}, "Raw session was destroyed or manually replaced"); return; } }
            else if (raw->sessionEpoch() != providedEpoch) { raw = nullptr; complete(false, {}, "Raw session was destroyed or manually replaced"); return; }
            else if (!raw->connected()) { raw = nullptr; complete(false, {}, "Borrowing requires the provided session to be connected"); return; }
            if (params["config"].isObject() && !ownsRaw) { ConnectionConfig expected; if (!workflowConnectionConfigFromJson(params["config"].toObject(), &expected, &why) || workflowConnectionConfigToJson(expected) != workflowConnectionConfigToJson(raw->config())) { raw = nullptr; complete(false, {}, "Borrowed session differs from selected configuration snapshot"); return; } }
            rawEpoch = raw->sessionEpoch(); resource = "raw"; emit q->resourceChanged(); if (!live(token)) return;
            if (raw->connected()) complete(true, {{"ownership", ownsRaw ? "owned" : "borrow"}}, "Raw session bound"); else { waitingKind = "rawConnect"; deadline = clock.elapsed() + number(params.value("timeout", raw->config().connectTimeoutMs), 5000); } return;
        }
        if (type == "http" || type == "ws") {
            if (!resource.isEmpty()) { complete(false, {}, type == "http" ? "Close the current persistent resource before HTTP request" : "Close the current persistent resource before WebSocket connection"); return; }
            if (provided && (provided->connected() || provided->connecting())) { complete(false, {}, "Stop the provided raw session before HTTP or WebSocket operation"); return; }
            ensureProtocol(); waitingKind = type == "http" ? "http" : "wsConnect";
            params.insert("maxResponseBytes", std::min({plan.limits.maxMessageBytes, 8 * 1024 * 1024, number(params.value("maxResponseBytes", number(params.value("limit", 8), 8) * 1024 * 1024), plan.limits.maxMessageBytes)}));
            params.insert("maxMessageBytes", std::min({plan.limits.maxMessageBytes, 8 * 1024 * 1024, number(params.value("maxMessageBytes", number(params.value("limit", 8), 8) * 1024 * 1024), plan.limits.maxMessageBytes)}));
            const auto totalTimeout = number(params.value("timeoutMs", params.value("timeout", 10000)), 10000);
            params.insert("timeoutMs", totalTimeout);
            params.insert("connectTimeoutMs", number(params.value("connectTimeoutMs", params.value("connectTimeout", std::min(5000, totalTimeout))), std::min(5000, totalTimeout)));
            deadline = clock.elapsed() + totalTimeout;
            if (type == "http") protocol->httpRequest(operation, params); else protocol->connectWebSocket(operation, params); return;
        }
        if (type == "close") {
            if (resource == "raw") { releaseRaw(); resource.clear(); emit q->resourceChanged(); if (live(token)) complete(true, {}, "Raw resource released; borrowed connection retained"); }
            else if (resource == "ws" && protocol) { waitingKind = "wsClose"; deadline = clock.elapsed() + 5000; protocol->closeWebSocket(operation, number(params.value("code", 1000), 1000), text(params["reason"])); }
            else complete(false, {}, "No persistent resource to close");
            return;
        }
        if (type == "send" || type == "sendWait" || type == "wait") {
            const auto requested = text(params.value("resource", resource)); if (requested != resource) { complete(false, {}, "Requested resource is not bound"); return; }
            QByteArray bytes; if (type != "wait" && !payload(&bytes, &why)) { complete(false, {}, why); return; }
            if (type != "send" && !armWait(&why)) { complete(false, {}, why); return; }
            if (type == "wait") return;
            if (resource == "raw") { if (!sendRaw(bytes, &why)) { if (live(token)) complete(false, {}, why); return; } if (!live(token)) return; if (type == "send") complete(true, {{"admitted", true}, {"bytes", bytes.size()}, {"localWriteCompleted", false}, {"peerAcknowledged", false}}, "Send admitted to bounded local queue"); }
            else if (resource == "ws" && protocol) { waitingKind = type == "sendWait" ? "wsSendWait" : "wsSend"; deadline = clock.elapsed() + number(params.value("timeout", 5000), 5000); protocol->sendWebSocket(operation, bytes, text(params.value("messageType", "text")) == "binary"); }
            else complete(false, {}, "No communication resource is bound");
            return;
        }
        complete(false, {}, "Unsupported execution node type");
    }
    void message(const QByteArray& data, bool binary = false) {
        if (!active() || !inOperation) return;
        if (data.size() > plan.limits.maxMessageBytes) { complete(false, {}, "Received message exceeds limit"); return; }
        if (resource == "ws" && params.contains("messageType") && ((text(params["messageType"]) == "binary") != binary)) return;
        const auto mode = text(params.value("match", "any")); bool matched = mode == "any"; QJsonValue parsed(QJsonValue::Undefined);
        QJsonParseError error; const auto json = QJsonDocument::fromJson(data, &error);
        if (error.error == QJsonParseError::NoError) { parsed = json.isObject() ? QJsonValue(json.object()) : QJsonValue(json.array()); if (!boundedJson(parsed, plan.limits.maxVariableBytes)) { complete(false, {}, "Received JSON exceeds depth/entry/variable limits"); return; } }
        if (mode == "json" || mode == "JSON 字段") {
            if (parsed.isUndefined()) return;
            bool ok; const auto actual = lookup(parsed, text(params.value("path", "$")), &ok); if (!ok) return; bool valid; matched = compare(actual, params["expected"], text(params.value("operator", "equals")), &valid); if (!valid) { complete(false, {}, "Invalid message comparison"); return; }
        } else if (mode != "any") {
            QString why; const bool hex = text(params["expectedFormat"]) == "HEX" || (params["expectedFormat"].isUndefined() && text(params["format"]) == "HEX");
            const auto expected = encodePayload(text(params["expected"]), hex, "UTF-8", "none", &why); if (!why.isEmpty()) { complete(false, {}, "Invalid expected bytes: " + why); return; }
            if (mode == "prefix" || mode == "字节前缀") matched = data.startsWith(expected); else if (mode == "contains" || mode == "字节包含") matched = data.contains(expected); else matched = data == expected;
        }
        if (!matched) return;
        QJsonObject output = parsed.isObject() ? parsed.toObject() : QJsonObject{};
        output["length"] = data.size(); output["bytesBase64"] = QString::fromLatin1(data.toBase64()); output["text"] = QString::fromUtf8(data); output["binary"] = binary;
        if (parsed.isArray()) output["json"] = parsed;
        QString why; if (!storeVariable(text(params.value("output", "message")), output, &why)) { complete(false, {}, why); return; }
        complete(true, output, "Business response matched");
    }
    void consume(const DataRecord& record) {
        if (!inOperation || waitingKind != "rawWait") return;
        const auto filter = params["sourceFilter"].toObject();
        if (filter.contains("address") && text(filter["address"]).toStdString() != record.peer.address) return;
        if (filter.contains("port") && number(filter["port"], -1) != record.peer.port) return;
        if (filter.contains("clientId") && text(filter["clientId"]) != QString::number(record.connectionId)) return;
        if (!record.payload) return;
        if (record.payload->size() > size_t(plan.limits.maxMessageBytes)) { complete(false, {}, "Raw read exceeds message assembly limit"); return; }
        const QByteArray bytes(reinterpret_cast<const char*>(record.payload->data()), qsizetype(record.payload->size()));
        if (record.transport == TransportKind::Udp) { message(bytes); return; }
        if (assemblyClient && assemblyClient != record.connectionId) { complete(false, {}, "Stream source changed while assembling frame"); return; } assemblyClient = record.connectionId;
        if (assembly.size() + bytes.size() > plan.limits.maxMessageBytes) { complete(false, {}, "Stream assembly exceeds message limit"); return; }
        assembly += bytes;
        const Parameters f = params["framing"].toObject(); const auto mode = text(f["mode"]);
        for (int frames = 0; frames < 4096 && inOperation && waitingKind == "rawWait"; ++frames) {
            int size = -1, strip = 0; QByteArray frame;
            if (mode == "fixed") size = number(f["length"], -1);
            else if (mode == "delimiter") { QString why; const auto delimiter = encodePayload(text(f["delimiter"]), text(f["delimiterFormat"]) == "HEX", "UTF-8", "none", &why); if (!why.isEmpty() || delimiter.isEmpty()) { complete(false, {}, "Invalid stream delimiter"); return; } const auto offset = assembly.indexOf(delimiter); if (offset < 0) return; size = int(offset + delimiter.size()); strip = f["includeDelimiter"].toBool(false) ? 0 : int(delimiter.size()); }
            else if (mode == "lengthHeader") { const int offset = number(f.value("offset", 0), 0), header = number(f["headerBytes"], 0), headerEnd = offset + header; if (header < 1 || header > 4 || offset < 0 || offset > 64) { complete(false, {}, "Invalid length header"); return; } if (assembly.size() < headerEnd) return; std::uint64_t length = 0; for (int i = 0; i < header; ++i) { const int pos = text(f.value("byteOrder", "big")) == "little" ? header - i - 1 : i; length = (length << 8) | static_cast<unsigned char>(assembly[offset + pos]); } const auto total = f["includesHeader"].toBool(false) ? length : length + std::uint64_t(headerEnd); if (total < std::uint64_t(headerEnd) || total > std::uint64_t(plan.limits.maxMessageBytes)) { complete(false, {}, "Length header exceeds message limit"); return; } size = int(total); strip = -headerEnd; }
            else { complete(false, {}, "Unsupported stream framing"); return; }
            if (size < 1 || size > plan.limits.maxMessageBytes) { complete(false, {}, "Invalid frame length"); return; }
            if (assembly.size() < size) return;
            frame = assembly.left(size); assembly.remove(0, size); if (strip > 0) frame.chop(strip); else if (strip < 0) frame.remove(0, -strip); message(frame);
        }
    }
    void tick() {
        if (!active()) return;
        const auto token = generation;
        if (clock.elapsed() >= plan.limits.maxDurationMs) { finish(WorkflowRunState::Failed, "Workflow deadline exceeded"); return; }
        if (provided && (provided->connected() || provided->connecting()) && (resource == "ws" || waitingKind == "http" || waitingKind == "wsConnect")) { finish(WorkflowRunState::Failed, "Stop the provided raw session before HTTP or WebSocket operation"); return; }
        if (resource == "raw") {
            if (!raw || raw->sessionEpoch() != rawEpoch) { finish(WorkflowRunState::Failed, "Raw session was destroyed or manually replaced"); return; }
            if (!raw->connected() && waitingKind != "rawConnect") { if (inOperation) complete(false, {}, "Raw session disconnected"); else finish(WorkflowRunState::Failed, "Raw session disconnected"); return; }
        }
        if (!inOperation) { queueStep(); return; }
        if (waitingKind == "delay") { if (clock.elapsed() >= delayEnd) complete(true, {{"duration", number(params.value("duration", 1000), 1000)}}, "Delay completed"); return; }
        if (waitingKind == "rawConnect" && raw && raw->connected()) { complete(true, {{"ownership", "owned"}}, "Raw connection established"); return; }
        if (observer && raw) {
            auto batch = raw->takeRawObservation(observer);
            if (!batch.error.isEmpty() || batch.epoch != rawEpoch) { complete(false, {}, batch.error.isEmpty() ? "Raw observer epoch changed" : batch.error); return; }
            for (const auto& e : batch.events) {
                const auto filter = params["sourceFilter"].toObject();
                const bool selectedClient = !filter.contains("clientId") || text(filter["clientId"]) == QString::number(e.connectionId);
                if (e.kind == EventKind::SendRejected || e.kind == EventKind::ReceiveTruncated || e.kind == EventKind::Disconnected || (e.kind == EventKind::ClientRemoved && selectedClient) || e.kind == EventKind::Error) { complete(false, {}, QString::fromStdString(e.message)); return; }
            }
            for (const auto& r : batch.records) { consume(r); if (!live(token) || !inOperation || waitingKind != "rawWait") return; }
        }
        if (live(token) && inOperation && !waitingKind.isEmpty() && clock.elapsed() >= deadline) { if (waitingKind.startsWith("ws") || waitingKind == "http") { if (protocol) protocol->cancelAll(); } complete(false, {}, "Operation timed out"); }
    }
};
WorkflowRunner::WorkflowRunner(QObject* parent) : QObject(parent), d(std::make_unique<Impl>(this)) {}
WorkflowRunner::~WorkflowRunner() { blockSignals(true); if (d->active()) d->finish(WorkflowRunState::Stopped, "Runner destroyed"); }
void WorkflowRunner::setSessionProvider(std::function<SessionController*()> provider) { d->sessionProvider = std::move(provider); }
void WorkflowRunner::setProfilesProvider(std::function<QVector<ConnectionConfig>()> provider) { d->profilesProvider = std::move(provider); }
bool WorkflowRunner::start(const WorkflowDocument& document, QString* error) {
    if (error) error->clear();
    auto reject = [&](const QString& why) { if (error) *error = userMessage(why); return false; };
    if (d->active()) return reject("A workflow is already active");
    const auto issues = document.validate(); if (!issues.isEmpty()) return reject(issues.first().message);
    // Snapshot provided controller once; browsing cannot redirect execution.
    auto provided = d->sessionProvider ? d->sessionProvider() : nullptr;
    for (const auto& node : document.nodes) if (node.type == "raw") {
        if (!provided) return reject("Workflow requires a provided stable raw session");
        if (provided->periodicActive()) return reject("Stop periodic sending before running this workflow");
    }
    d->plan = document; d->nodes.clear(); d->edges.clear(); d->results.clear(); d->loops.clear(); d->entries.clear(); d->vars = document.initialVariables;
    for (const auto& node : document.nodes) { auto snapshot = node; for (const auto& def : workflowNodeDefinitions()) if (def.type == node.type) { auto merged = def.defaults; for (auto it = node.parameters.begin(); it != node.parameters.end(); ++it) merged[it.key()] = it.value(); snapshot.parameters = merged; break; } d->nodes[node.id] = snapshot; d->results[node.id] = {}; if (node.type == "start") d->next = node.id; }
    for (const auto& edge : document.edges) d->edges[edge.from + "/" + edge.port] = edge.to;
    d->provided = provided; d->providedEpoch = provided ? provided->sessionEpoch() : 0; d->current.clear(); d->id = QUuid::createUuid().toString(QUuid::WithoutBraces); d->steps = 0; d->operationSequence = 0; d->scheduled = d->inOperation = false; d->clock.restart(); d->state = WorkflowRunState::Running;
    const auto token = ++d->generation; d->timer.start(); emit logsChanged(); if (!d->live(token)) return true; emit variablesChanged(); if (!d->live(token)) return true; emit stateChanged(); if (d->live(token)) d->queueStep(); return true;
}
void WorkflowRunner::stop() { d->finish(WorkflowRunState::Stopped, "Stopped by user; admitted writes cannot be recalled"); }
void WorkflowRunner::pause() { if (d->state == WorkflowRunState::Running) { d->state = WorkflowRunState::Paused; emit stateChanged(); } }
void WorkflowRunner::resume() { if (d->state == WorkflowRunState::Paused) { d->state = WorkflowRunState::Running; const auto token = d->generation; emit stateChanged(); if (d->live(token)) d->queueStep(); } }
bool WorkflowRunner::active() const { return d->active(); }
WorkflowRunState WorkflowRunner::state() const { return d->state; }
QString WorkflowRunner::runId() const { return d->id; }
QString WorkflowRunner::activeNodeId() const { return d->current; }
QString WorkflowRunner::resourceSummary() const { if (d->resource == "raw" && d->raw) return QString("%1 · %2").arg(QString::fromStdString(d->raw->config().name), d->ownsRaw ? "流程拥有" : "借用"); if (d->protocol) return d->protocol->resourceSummary(); return "未绑定资源"; }
QJsonObject WorkflowRunner::variables() const { return d->vars; }
QVector<WorkflowLogEntry> WorkflowRunner::logs() const { return d->entries; }
WorkflowNodeResult WorkflowRunner::result(const QString& id) const { return d->results.value(id); }
bool WorkflowRunner::usesSession(const SessionController* session) const { return d->active() && session && (d->raw == session || (d->provided == session && std::any_of(d->nodes.begin(), d->nodes.end(), [](const WorkflowNode& node) { return node.type == "raw"; }))); }
}
