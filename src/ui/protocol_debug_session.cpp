#include "protocol_debug_session.hpp"
#include "workflow/workflow_messages.hpp"
#include <QDateTime>
#include <QJsonArray>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QSet>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace portbridge {
namespace {
bool token(const QString &s) {
    return !s.isEmpty() && s.size() <= 256 &&
           s.contains(QRegularExpression("^[!#$%&'*+.^_`|~0-9A-Za-z-]+$"));
}
QString normalizedError(const QString &error) {
    const auto translated = workflowDetail::userMessage(error);
    if (translated != error)
        return translated;
    if (error.contains("timed out", Qt::CaseInsensitive))
        return QStringLiteral("操作超时；请检查服务、地址及超时设置。");
    if (error.contains("limit", Qt::CaseInsensitive))
        return QStringLiteral("响应或消息超过设置的容量限制。");
    if (error.contains("TLS", Qt::CaseInsensitive))
        return QStringLiteral("网络或TLS验证失败；请检查服务、证书与主机名。") + " " + error;
    return error;
}
} // namespace
QString ProtocolDebugSession::validate(const QJsonObject &p, Mode mode) {
    const auto text = p.value("url").toString();
    const QUrl url(text, QUrl::StrictMode);
    const bool http = mode == Mode::Http;
    if (text.size() > 4096 || url.hasFragment() ||
        text.contains(QRegularExpression("[\\s\\x00-\\x1f]")) || !url.isValid() ||
        url.host().isEmpty() ||
        !(http ? QStringList{"http", "https"} : QStringList{"ws", "wss"}).contains(url.scheme()))
        return QStringLiteral("请填写完整的%1 URL，含协议与主机；不能包含空白或控制字符。")
            .arg(http ? "http/https" : "ws/wss");
    if (!url.userInfo().isEmpty())
        return QStringLiteral("请通过认证页签设置用户名/密码，URL不能包含认证信息。");
    if (http && !p.value("body").isUndefined() && !p.value("body").isNull() &&
        !p.value("body").isString() && !p.value("body").isObject() && !p.value("body").isArray())
        return QStringLiteral("请求体须为文本、JSON对象或数组。");
    const auto host = url.host(QUrl::FullyDecoded);
    if (QUrl::toAce(host).isEmpty() && !host.contains(':'))
        return QStringLiteral("网络主机名无效。");
    const int port = url.port(url.scheme() == "https" || url.scheme() == "wss" ? 443 : 80);
    if (port < 1 || port > 65535)
        return QStringLiteral("网络端口须为1–65535。");
    auto duration = [&](const char *key, int fallback) {
        auto value = p.value(key);
        if (value.isUndefined())
            value = p.value(QString(key) == "timeoutMs" ? "timeout" : "connectTimeout");
        if (value.isUndefined())
            return fallback;
        if (value.isDouble()) {
            const double n = value.toDouble();
            return std::isfinite(n) && n >= 1 && n <= 60000 && n == std::floor(n) ? int(n) : -1;
        }
        bool ok = false;
        const int n = value.toString().toInt(&ok);
        return ok ? n : -1;
    };
    const int timeout = duration("timeoutMs", 10000),
              connect = duration("connectTimeoutMs", std::min(timeout, 5000));
    if (timeout < 1 || timeout > 60000 || connect < 1 || connect > timeout)
        return QStringLiteral("超时须为1–60000ms，连接超时不能超过总超时。");
    const auto requested = p.value(http ? "maxResponseBytes" : "maxMessageBytes");
    bool capOk = true;
    const double amount = requested.isUndefined() ? 1024 * 1024
                          : requested.isDouble()  ? requested.toDouble()
                                                  : requested.toString().toDouble(&capOk);
    if (!capOk || !std::isfinite(amount) || amount < 1 || amount > 8 * 1024 * 1024 ||
        amount != std::floor(amount))
        return QStringLiteral("容量上限须为整数，1字节至8MiB。");
    const int cap = int(amount);
    if (cap < 1 || cap > 8 * 1024 * 1024)
        return QStringLiteral("容量上限须为1字节至8MiB。");
    if (!p.value("headers").isUndefined() && !p.value("headers").isObject())
        return QStringLiteral("请求头须为键值对象。");
    const auto headers = p.value("headers").toObject();
    QSet<QString> names;
    qsizetype bytes = 0;
    if (headers.size() > 64)
        return QStringLiteral("最多64个请求头。");
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
        auto name = it.key().toLower();
        auto value = it.value().toString();
        if (!token(it.key()) || !it.value().isString() || names.contains(name) ||
            value.contains(QRegularExpression("[\\r\\n\\x00]")))
            return QStringLiteral("请求头名称/值无效或重复；禁止换行注入。");
        if (QStringList{"host", "content-length", "transfer-encoding", "connection", "upgrade"}
                .contains(name) ||
            name.startsWith("sec-websocket-"))
            return QStringLiteral("%1由协议客户端管理，请删除该请求头。").arg(it.key());
        names.insert(name);
        bytes += it.key().toUtf8().size() + value.toUtf8().size() + 4;
        if (bytes > 65536)
            return QStringLiteral("请求头超过64KiB。");
    }
    if (http && !QStringList{"GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS"}.contains(
                    p.value("method").toString("GET")))
        return QStringLiteral("HTTP方法无效。");
    if (http && p.value("method").toString() == "HEAD" &&
        (p.value("body").isObject() || p.value("body").isArray() ||
         !p.value("body").toString().isEmpty()))
        return QStringLiteral("HEAD只读取响应头，本工作台不发送Body；请清空Body或改用其他方法。");
    if (p.value("body").isString() && p.value("body").toString().toUtf8().size() > 8 * 1024 * 1024)
        return QStringLiteral("请求体超过8MiB。");
    if (!http && !p.value("subprotocol").toString().isEmpty() &&
        !token(p.value("subprotocol").toString()))
        return QStringLiteral("子协议须为单个有效协议名称。");
    if (p.value("caFile").toString().size() > 4096 || p.value("caFile").toString().contains(QChar(0)))
        return QStringLiteral("CA文件路径无效或过长。");
    return {};
}
ProtocolDebugSession::ProtocolDebugSession(Mode mode, QObject *parent)
    : QObject(parent), client_(new WorkflowProtocolClient(this)), mode_(mode) {
    connect(client_, &WorkflowProtocolClient::operationFinished, this,
            [this](const QString &id, const QJsonObject &result, const QString &error) {
                if (id == activeId_ && !activeId_.isEmpty()) {
                    activeId_.clear();
                    error_ = normalizedError(error);
                    if (mode_ == Mode::Http) {
                        phase_ = Phase::Idle;
                        latest_ = result;
                        latestId_ = id;
                        if (error.isEmpty())
                            add("HTTP",
                                QStringLiteral("%1 · HTTP %2 · %3ms · %4B")
                                    .arg(snapshot_.value("method").toString("GET"))
                                    .arg(result.value("status").toInt())
                                    .arg(result.value("elapsedMs").toDouble())
                                    .arg(result.value("bodyBytes").toDouble()),
                                {}, false, result, id);
                        else
                            add("ERROR", error_, {}, false, {}, id);
                    } else {
                        phase_ = error.isEmpty() && client_->webSocketConnected() ? Phase::Connected
                                                                                  : Phase::Idle;
                        add(error.isEmpty() ? "OPEN" : "ERROR",
                            error.isEmpty() ? QStringLiteral("WebSocket握手完成；可收发完整消息。")
                                            : error_);
                    }
                } else if (id == sendId_ && !sendId_.isEmpty()) {
                    sendId_.clear();
                    error_ = normalizedError(error);
                    if (error.isEmpty()) {
                        transmitted_ += sending_.size();
                        add("TX", QStringLiteral("本地写出 %1B；不表示对端确认。").arg(sending_.size()),
                            sending_, sendingBinary_);
                    } else
                        add("ERROR", error_);
                    sending_.clear();
                } else if (id == closeId_ && !closeId_.isEmpty()) {
                    closeId_.clear();
                    phase_ = Phase::Idle;
                    error_ = normalizedError(error);
                    if (!error.isEmpty()) {
                        awaitLocalClose_ = false;
                        add("ERROR", error_);
                    }
                } else
                    return;
                emit changed();
            });
    connect(client_, &WorkflowProtocolClient::webSocketMessage, this,
            [this](const QByteArray &payload, bool binary) {
                if (mode_ != Mode::WebSocket || phase_ != Phase::Connected)
                    return;
                received_ += payload.size();
                add("RX",
                    QStringLiteral("%1 · %2B")
                        .arg(binary ? QStringLiteral("二进制") : QStringLiteral("UTF-8文本"))
                        .arg(payload.size()),
                    payload, binary);
                emit changed();
            });
    connect(client_, &WorkflowProtocolClient::webSocketClosed, this,
            [this](int code, const QString &reason, bool peer) {
                if (!active() && !awaitLocalClose_)
                    return;
                awaitLocalClose_ = false;
                add("CLOSE", QStringLiteral("%1关闭 · %2 · %3")
                                 .arg(peer ? QStringLiteral("对端") : QStringLiteral("本地"))
                                 .arg(code)
                                 .arg(reason.left(123)));
                emit changed();
            });
    connect(client_, &WorkflowProtocolClient::stateChanged, this, [this] {
        if (phase_ == Phase::Connected && !client_->webSocketConnected()) {
            phase_ = Phase::Idle;
            sendId_.clear();
            sending_.clear();
            emit changed();
        }
    });
    connect(client_, &WorkflowProtocolClient::protocolError, this, [this](const QString &error) {
        if (!active())
            return;
        error_ = normalizedError(error);
        if (mode_ != Mode::Http || !activeId_.isEmpty())
            add("ERROR", error_, {}, false, {}, mode_ == Mode::Http ? activeId_ : QString());
        emit changed();
    });
}
ProtocolDebugSession::~ProtocolDebugSession() { client_->cancelAll(); }
QString ProtocolDebugSession::nextId() {
    return "manual-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
}
bool ProtocolDebugSession::acquireSequence(const QString &id, QString *error) {
    if (mode_ != Mode::Http || id.isEmpty() || active() || starting_) {
        if (error)
            *error = QStringLiteral("HTTP会话已有活动，不能开始顺序联调。");
        return false;
    }
    sequenceId_ = id;
    ++epoch_;
    emit changed();
    return true;
}
void ProtocolDebugSession::releaseSequence(const QString &id) {
    if (sequenceId_ == id && phase_ == Phase::Idle) {
        sequenceId_.clear();
        ++epoch_;
        emit changed();
    }
}
bool ProtocolDebugSession::start(const QJsonObject &parameters, QString *error) {
    auto occupied = [&] {
        return phase_ != Phase::Idle ||
               ((!sequenceId_.isEmpty() || !parameters.value("httpSequenceId").toString().isEmpty()) &&
                parameters.value("httpSequenceId").toString() != sequenceId_);
    };
    auto why = (occupied() || starting_) ? QStringLiteral("已有操作或连接；请先取消或断开。")
                                         : validate(parameters, mode_);
    if (!why.isEmpty()) {
        if (error)
            *error = why;
        return false;
    }
    QScopedValueRollback<bool> starting(starting_, true);
    QScopedValueRollback<QJsonObject> pending(pendingParameters_, parameters);
    why = pendingContextError();
    if (!why.isEmpty()) {
        if (error)
            *error = why;
        return false;
    }
    const auto before = epoch_;
    if (guard_ && !guard_(&why)) {
        if (why.isEmpty())
            why = QStringLiteral("保留了当前活动，未发起通信。");
    }
    if (why.isEmpty())
        why = pendingContextError();
    if (why.isEmpty() && (epoch_ != before || occupied()))
        why = QStringLiteral("确认期间当前操作已变更，请重新发起通信。");
    if (!why.isEmpty()) {
        if (error)
            *error = why;
        return false;
    }
    auto normalized = parameters;
    const auto capacity = mode_ == Mode::Http ? "maxResponseBytes" : "maxMessageBytes";
    if (!normalized.contains(capacity))
        normalized[capacity] = 1024 * 1024;
    ++epoch_;
    received_ = 0;
    transmitted_ = 0;
    awaitLocalClose_ = false;
    client_->cancelAll();
    activeId_ = nextId();
    snapshot_ = normalized;
    endpoint_ = parameters.value("url").toString();
    error_.clear();
    phase_ = mode_ == Mode::Http ? Phase::Requesting : Phase::Connecting;
    if (mode_ == Mode::Http)
        add("PENDING", QStringLiteral("%1 · 请求中…").arg(normalized.value("method").toString("GET")),
            {}, false, {}, activeId_);
    else
        add("START", QStringLiteral("正在建立WebSocket连接。"));
    emit changed();
    if (mode_ == Mode::Http)
        client_->httpRequest(activeId_, normalized);
    else
        client_->connectWebSocket(activeId_, normalized);
    return true;
}
bool ProtocolDebugSession::sendMessage(const QByteArray &bytes, bool binary, QString *error) {
    if (!connected() || writing()) {
        if (error)
            *error = QStringLiteral("请等待连接或上次写入完成。");
        return false;
    }
    const auto configured = snapshot_.value("maxMessageBytes");
    const int limit =
        int(configured.isDouble() ? configured.toDouble() : configured.toString().toDouble());
    if (bytes.size() > std::min(limit, 8 * 1024 * 1024)) {
        if (error)
            *error =
                QStringLiteral("消息%1B超过此连接设置的%2B上限；未发送。").arg(bytes.size()).arg(limit);
        return false;
    }
    if (!binary && QString::fromUtf8(bytes).toUtf8() != bytes) {
        if (error)
            *error = QStringLiteral("文本消息须为有效UTF-8；未发送。");
        return false;
    }
    sendId_ = nextId();
    sending_ = bytes;
    sendingBinary_ = binary;
    client_->sendWebSocket(sendId_, bytes, binary);
    emit changed();
    return true;
}
void ProtocolDebugSession::close(int code, const QString &reason) {
    if (!connected()) {
        cancel();
        return;
    }
    if ((code != 1000 && (code < 3000 || code > 4999)) || reason.toUtf8().size() > 123) {
        error_ = QStringLiteral("关闭码须为1000或3000–4999，原因最多123个UTF-8字节。");
        emit changed();
        return;
    }
    closeId_ = nextId();
    awaitLocalClose_ = true;
    phase_ = Phase::Closing;
    client_->closeWebSocket(closeId_, code, reason);
    emit changed();
}
void ProtocolDebugSession::cancel() {
    if (!active())
        return;
    const auto operation = activeId_;
    ++epoch_;
    awaitLocalClose_ = false;
    client_->cancelAll();
    activeId_.clear();
    sendId_.clear();
    closeId_.clear();
    sending_.clear();
    sequenceId_.clear();
    phase_ = Phase::Idle;
    if (mode_ != Mode::Http || !operation.isEmpty())
        add("CANCEL", QStringLiteral("已取消；不能撤回已经发送的数据。"), {}, false, {}, operation);
    emit changed();
}
void ProtocolDebugSession::add(QString direction, QString detail, QByteArray bytes, bool binary,
                               QJsonObject response, QString operation) {
    ProtocolDebugEntry e{++nextEntry_,
                         QDateTime::currentMSecsSinceEpoch(),
                         std::move(direction),
                         detail.left(2048),
                         std::move(operation),
                         std::move(bytes),
                         binary,
                         std::move(response),
                         0};
    if (mode_ == Mode::Http && !e.operationId.isEmpty()) {
        const auto existing =
            std::find_if(entries_.begin(), entries_.end(), [&](const ProtocolDebugEntry &entry) {
                return entry.operationId == e.operationId;
            });
        if (existing != entries_.end()) {
            // A request keeps its identity and start time when the result replaces the pending row.
            e.id = existing->id;
            e.timestampMs = existing->timestampMs;
            retainedBytes_ -= existing->cost;
            entries_.erase(existing);
        }
    }
    e.payloadBytes = e.payload.size();
    if (!e.response.isEmpty())
        e.payloadBytes = qsizetype(e.response.value("bodyBytes").toDouble());
    e.cost = e.payload.capacity() + e.detail.size() * 2 + 512;
    if (!e.response.isEmpty())
        e.cost += qsizetype(e.response.value("bodyBytes").toDouble()) * 10 + 256 * 1024;
    const qsizetype budget = mode_ == Mode::Http ? 16 * 1024 * 1024 : 4 * 1024 * 1024;
    const int rows = mode_ == Mode::Http ? 64 : 500;
    if (e.cost > budget) {
        e.payload.clear();
        e.response = {};
        e.detail += QStringLiteral(" · 该内容超过历史预算，未保留在时间线；HTTP最新响应单独可查。");
        e.cost = 512 + e.detail.size() * 2;
        ++omitted_;
    }
    while (!entries_.isEmpty() && (entries_.size() >= rows || retainedBytes_ + e.cost > budget)) {
        retainedBytes_ -= entries_.front().cost;
        entries_.removeFirst();
        ++omitted_;
    }
    retainedBytes_ += e.cost;
    entries_.push_back(std::move(e));
}
void ProtocolDebugSession::clearHistory() {
    entries_.clear();
    retainedBytes_ = 0;
    omitted_ = 0;
    latest_ = {};
    latestId_.clear();
    emit changed();
}
} // namespace portbridge
