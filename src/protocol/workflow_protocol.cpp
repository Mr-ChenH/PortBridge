#include "protocol_internal.hpp"
#include <QJsonArray>
#include <QSet>
#include <QTimer>
#include <QThread>
#include <algorithm>
#include <cmath>

namespace portbridge {
using namespace protocol;
namespace {
int duration(const QJsonObject& p, const char* name, const char* alias, int fallback) {
    const auto value = p.contains(name) ? p.value(name) : p.value(alias);
    if (value.isUndefined()) return fallback;
    bool ok = false;
    if (value.isDouble()) {
        const auto numeric = value.toDouble();
        return std::isfinite(numeric) && numeric >= 1 && numeric <= 60000 && numeric == std::floor(numeric) ? int(numeric) : -1;
    }
    const int n = value.toString().toInt(&ok);
    return ok ? n : -1;
}
bool jsonBodyBudget(const QJsonValue& value, qsizetype& remaining, int& nodes, int depth = 0) {
    if (++nodes > 4096 || depth > 32 || remaining < 32) return false;
    remaining -= 32;
    auto stringFits = [&](const QString& text) {
        if (text.size() > remaining / 6) return false;
        remaining -= text.size() * 6; return true;
    };
    if (value.isString()) return stringFits(value.toString());
    if (value.isObject()) {
        const auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it)
            if (!stringFits(it.key()) || !jsonBodyBudget(it.value(), remaining, nodes, depth + 1)) return false;
    } else if (value.isArray()) {
        for (const auto& item : value.toArray()) if (!jsonBodyBudget(item, remaining, nodes, depth + 1)) return false;
    }
    return true;
}
bool parse(const QString& id, const QJsonObject& p, bool ws, Request& r, QString& error) {
    auto fail = [&](const QString& e) { error = e; return false; };
    if (id.isEmpty() || id.size() > 128) return fail("Invalid operation identifier");
    const QString urlText = p.value("url").toString();
    if (urlText.size() > 4096) return fail("URL exceeds metadata limit");
    QUrl url(urlText, QUrl::StrictMode);
    const auto scheme = url.scheme().toLower();
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasFragment() ||
        (ws ? scheme != "ws" && scheme != "wss" : scheme != "http" && scheme != "https"))
        return fail("Invalid protocol URL (embedded credentials are unsupported)");
    r.id = id; r.tls = scheme == "https" || scheme == "wss";
    r.url = url.toEncoded().toStdString();
    const auto hostname = url.host(QUrl::FullyDecoded);
    r.host = hostname.contains(':') ? hostname.toStdString() : QUrl::toAce(hostname).toStdString();
    if (r.host.empty()) return fail("Invalid network hostname");
    const int port = url.port(r.tls ? 443 : 80);
    if (port <= 0 || port > 65535) return fail("Invalid network port");
    r.port = std::to_string(port);
    r.authority = (r.host.find(':') != std::string::npos ? "[" + r.host + "]" : r.host) + ":" + r.port;
    r.target = url.path(QUrl::FullyEncoded).toStdString();
    if (r.target.empty()) r.target = "/";
    if (url.hasQuery()) r.target += "?" + url.query(QUrl::FullyEncoded).toStdString();
    r.timeout = duration(p, "timeoutMs", "timeout", 10000);
    r.connectTimeout = duration(p, "connectTimeoutMs", "connectTimeout", std::min(5000, r.timeout));
    if (r.timeout < 1 || r.timeout > 60000 || r.connectTimeout < 1 || r.connectTimeout > r.timeout)
        return fail("Timeout must be 1–60000 ms; connect timeout cannot exceed total timeout");
    const auto cap = p.value(ws ? "maxMessageBytes" : "maxResponseBytes");
    if (!cap.isUndefined()) {
        const double requested = cap.isDouble() ? cap.toDouble() : cap.toString().toDouble();
        if (!std::isfinite(requested) || requested < 1 || requested > PayloadLimit || requested != std::floor(requested)) return fail("Payload limit must be 1–8388608 bytes");
        r.limit = qsizetype(requested);
    }
    const auto ca = p.value("caFile").toString();
    if (ca.size() > 4096 || ca.contains(QChar(0))) return fail("Invalid CA file");
    r.caFile = ca.toUtf8().toStdString();
    QJsonObject headers;
    const auto inputHeaders = p.value("headers");
    if (inputHeaders.isObject()) headers = inputHeaders.toObject();
    else if (inputHeaders.isString() && !inputHeaders.toString().trimmed().isEmpty()) {
        if (inputHeaders.toString().size() > HeaderLimit) return fail("Request headers exceed limit");
        const auto text = inputHeaders.toString().trimmed();
        if (text.startsWith('{')) {
            QJsonParseError e;
            const auto doc = QJsonDocument::fromJson(text.toUtf8(), &e);
            if (e.error != QJsonParseError::NoError || !doc.isObject()) return fail("Headers must be a JSON object or Name: value lines");
            headers = doc.object();
        } else {
            const auto lines = text.split('\n');
            if (lines.size() > 64) return fail("Too many request headers");
            QSet<QString> names;
            for (const auto& raw : lines) {
                const auto line = raw.trimmed();
                if (line.isEmpty()) continue;
                const auto colon = line.indexOf(':');
                if (colon <= 0) return fail("Headers must contain Name: value lines");
                const auto name = line.left(colon).trimmed();
                if (names.contains(name.toLower())) return fail("Duplicate request header");
                names.insert(name.toLower()); headers[name] = line.mid(colon + 1).trimmed();
            }
        }
    } else if (!inputHeaders.isUndefined() && !inputHeaders.isNull() && !inputHeaders.isString()) return fail("Headers must be an object");
    if (headers.size() > 64) return fail("Too many request headers");
    qsizetype headerBytes = 0;
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        if (!it.value().isString()) return fail("Header values must be strings");
        const auto key = it.key().toLatin1();
        const auto value = it.value().toString();
        if (key.isEmpty() || key.size() > 256 || value.size() > HeaderLimit) return fail("Header exceeds metadata limit");
        for (char c : key) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || QByteArray("!#$%&'*+-.^_`|~").contains(c))) return fail("Invalid header name");
        if (value.contains('\r') || value.contains('\n') || value.contains(QChar(0))) return fail("Invalid header value");
        const auto bytes = value.toUtf8();
        headerBytes += key.size() + bytes.size() + 4;
        if (headerBytes > HeaderLimit) return fail("Request headers exceed limit");
        const auto lower = key.toLower();
        if (lower == "host" || lower == "content-length" || lower == "transfer-encoding" || lower.startsWith("sec-websocket-") || lower == "connection" || lower == "upgrade") return fail("Reserved protocol header");
        r.headers.emplace_back(key.toStdString(), bytes.toStdString());
    }
    if (ws) {
        const auto sub = p.value("subprotocol").toString();
        if (!sub.isEmpty()) {
            if (sub.size() > 256) return fail("Subprotocol exceeds metadata limit");
            for (const auto c : sub) if (c.unicode() > 127 || (!c.isLetterOrNumber() && !QString("!#$%&'*+-.^_`|~").contains(c))) return fail("Invalid subprotocol token");
            r.headers.emplace_back("Sec-WebSocket-Protocol", sub.toStdString());
        }
    } else {
        r.method = p.value("method").toString("GET").toUpper().toStdString();
        if (r.method != "GET" && r.method != "HEAD" && r.method != "POST" && r.method != "PUT" && r.method != "PATCH" && r.method != "DELETE" && r.method != "OPTIONS") return fail("Unsupported HTTP method");
        const auto body = p.value("body");
        if (body.isString()) {
            if (body.toString().size() > PayloadLimit) return fail("Request body exceeds limit");
            r.body = body.toString().toUtf8();
        } else if (body.isObject() || body.isArray()) {
            qsizetype remaining = PayloadLimit; int nodes = 0;
            if (!jsonBodyBudget(body, remaining, nodes)) return fail("JSON request exceeds byte, node, or nesting limit");
            r.body = body.isObject() ? QJsonDocument(body.toObject()).toJson(QJsonDocument::Compact) : QJsonDocument(body.toArray()).toJson(QJsonDocument::Compact);
            r.headers.emplace_back("Content-Type", "application/json");
        } else if (!body.isUndefined() && !body.isNull()) return fail("Unsupported HTTP body");
        if (r.body.size() > PayloadLimit) return fail("Request body exceeds limit");
    }
    return true;
}
}
struct WorkflowProtocolClient::Impl {
    WorkflowProtocolClient* q;
    std::shared_ptr<Mailbox> box = std::make_shared<Mailbox>();
    struct IoState : std::enable_shared_from_this<IoState> {
        std::shared_ptr<HttpOperation> http;
        std::shared_ptr<WebSocket> ws;
        std::mutex commandMutex;
        std::deque<std::function<void(IoState&)>> commands;
        bool scheduled = false;
        void schedule() {
            net::post(Runtime::instance().io(), [state = shared_from_this()] { state->drain(); });
        }
        void submit(std::function<void(IoState&)> command, bool replace = false) {
            bool start = false;
            {
                std::lock_guard<std::mutex> lock(commandMutex);
                if (replace) commands.clear();
                // Admission permits HTTP, connect, send and close only once.
                commands.push_back(std::move(command));
                Q_ASSERT(commands.size() <= 5);
                if (!scheduled) { scheduled = true; start = true; }
            }
            if (start) schedule();
        }
        void drain() {
            for (int i = 0; i < 4; ++i) {
                std::function<void(IoState&)> command;
                {
                    std::lock_guard<std::mutex> lock(commandMutex);
                    if (commands.empty()) { scheduled = false; return; }
                    command = std::move(commands.front()); commands.pop_front();
                }
                command(*this);
            }
            schedule();
        }
    };
    std::shared_ptr<IoState> io = std::make_shared<IoState>();
    QTimer timer;
    QString httpId, connectId, sendId, closeId;
    bool wsHeld = false;
    explicit Impl(WorkflowProtocolClient* owner) : q(owner) {
        Runtime::instance();
        timer.setInterval(10);
        QObject::connect(&timer, &QTimer::timeout, q, [this] { drain(); });
        timer.start();
    }
    bool available(const QString& id) const { return !id.isEmpty() && id.size() <= 128 && id != httpId && id != connectId && id != sendId && id != closeId; }
    void reject(const QString& id, const QString& error) { emit q->operationFinished(id.left(128), {}, error); }
    void drain() {
        // Take one event at a time: cancel/reentrant destruction during a signal
        // cannot deliver remaining old-generation data or retain large batches.
        for (int i = 0; i < 16; ++i) {
            Event event;
            {
                std::lock_guard<std::mutex> lock(box->mutex);
                if (box->events.empty()) break;
                event = std::move(box->events.front()); box->events.pop_front(); box->bytes -= event.cost;
            }
            if (!box->live(event.generation)) continue;
            QPointer<WorkflowProtocolClient> alive(q);
            if (event.kind == Event::Finished) {
                if (event.result.value("connected").toBool()) box->connected = true;
                if (event.result.value("closed").toBool()) box->connected = false;
                if (event.id == httpId) httpId.clear();
                if (event.id == connectId) { connectId.clear(); if (!event.error.isEmpty()) wsHeld = false; }
                if (event.id == sendId) sendId.clear();
                if (event.id == closeId) { closeId.clear(); wsHeld = false; }
                emit q->operationFinished(event.id, event.result, event.error);
            } else if (event.kind == Event::Message) emit q->webSocketMessage(event.bytes, event.binary);
            else if (event.kind == Event::Closed) {
                box->connected = false; wsHeld = false;
                emit q->webSocketClosed(event.result.value("code").toInt(), event.result.value("reason").toString(), event.result.value("peerInitiated").toBool());
            } else if (event.kind == Event::State) {
                box->connected = event.binary;
                if (!box->connected && connectId.isEmpty()) wsHeld = false;
                emit q->stateChanged();
            } else emit q->protocolError(event.error);
            if (!alive) return;
            if (event.acknowledge) event.acknowledge();
        }
    }
    void cancel() {
        box->reset(); httpId.clear(); connectId.clear(); sendId.clear(); closeId.clear(); wsHeld = false;
        io->submit([](IoState& state) {
            if (state.http) state.http->cancel();
            if (state.ws) state.ws->cancel();
            state.http.reset(); state.ws.reset();
        }, true);
    }
};
WorkflowProtocolClient::WorkflowProtocolClient(QObject* parent) : QObject(parent), d(std::make_unique<Impl>(this)) {}
WorkflowProtocolClient::~WorkflowProtocolClient() { d->timer.stop(); d->cancel(); }
void WorkflowProtocolClient::httpRequest(const QString& id, const QJsonObject& p) {
    Q_ASSERT(thread() == QThread::currentThread());
    if (!d->available(id) || !d->httpId.isEmpty()) { d->reject(id, "HTTP operation busy or invalid identifier"); return; }
    Request r; QString error;
    if (!parse(id, p, false, r, error)) { d->reject(id, error); return; }
    r.token = d->box->generation; d->httpId = id;
    auto box = d->box;
    d->io->submit([box, r = std::move(r)](Impl::IoState& state) mutable {
        if (!box->live(r.token)) return;
        state.http = startHttp(Runtime::instance().io(), box, std::move(r));
    });
}
void WorkflowProtocolClient::connectWebSocket(const QString& id, const QJsonObject& p) {
    Q_ASSERT(thread() == QThread::currentThread());
    if (!d->available(id) || d->wsHeld) { d->reject(id, "WebSocket resource busy or invalid identifier"); return; }
    Request r; QString error;
    if (!parse(id, p, true, r, error)) { d->reject(id, error); return; }
    r.token = d->box->generation; d->connectId = id; d->wsHeld = true;
    auto box = d->box;
    d->io->submit([box, r = std::move(r)](Impl::IoState& state) mutable {
        if (!box->live(r.token)) return;
        try { state.ws = r.tls ? makeTlsWebSocket(Runtime::instance().io(), box, r) : makePlainWebSocket(Runtime::instance().io(), box, r); state.ws->start(); }
        catch (...) { finished(box, r, r.id, {}, "WebSocket TLS configuration failed"); }
    });
}
void WorkflowProtocolClient::sendWebSocket(const QString& id, const QByteArray& bytes, bool binary) {
    Q_ASSERT(thread() == QThread::currentThread());
    if (!d->available(id) || !d->box->connected || !d->sendId.isEmpty() || !d->closeId.isEmpty() || bytes.size() > PayloadLimit) { d->reject(id, "WebSocket unavailable, busy, or payload exceeds limit"); return; }
    if (!binary && QString::fromUtf8(bytes).toUtf8() != bytes) { d->reject(id, "WebSocket text must be valid UTF-8"); return; }
    d->sendId = id; auto box = d->box; const auto token = box->generation.load();
    d->io->submit([box, token, id, bytes, binary](Impl::IoState& state) { if (box->live(token) && state.ws) state.ws->send(id, bytes, binary); });
}
void WorkflowProtocolClient::closeWebSocket(const QString& id, int code, const QString& reason) {
    Q_ASSERT(thread() == QThread::currentThread());
    const bool validCode = (code >= 1000 && code <= 1014 && code != 1004 && code != 1005 && code != 1006) || (code >= 3000 && code <= 4999);
    if (!d->available(id) || !d->box->connected || !d->closeId.isEmpty() || !validCode || reason.toUtf8().size() > 123) { d->reject(id, "Invalid WebSocket close or unavailable resource"); return; }
    d->closeId = id; auto box = d->box; const auto token = box->generation.load();
    d->io->submit([box, token, id, code, reason](Impl::IoState& state) { if (box->live(token) && state.ws) state.ws->close(id, code, reason); });
}
void WorkflowProtocolClient::cancelAll() { Q_ASSERT(thread() == QThread::currentThread()); d->cancel(); emit stateChanged(); }
bool WorkflowProtocolClient::webSocketConnected() const { return d->box->connected; }
QString WorkflowProtocolClient::resourceSummary() const {
    if (d->box->connected) return "WebSocket 已连接";
    if (!d->connectId.isEmpty()) return "WebSocket 连接中";
    if (!d->httpId.isEmpty()) return "HTTP 请求中";
    return "协议资源空闲";
}
}
