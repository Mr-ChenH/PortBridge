#pragma once
#include <QObject>
#include <QJsonObject>
#include <QByteArray>
#include <QString>
#include <memory>

namespace portbridge {
// One bounded HTTP operation / one persistent WebSocket resource. All public
// calls/signals belong to the QObject thread; library I/O runs independently.
class WorkflowProtocolClient : public QObject {
    Q_OBJECT
public:
    explicit WorkflowProtocolClient(QObject* parent = nullptr);
    ~WorkflowProtocolClient() override;
    // Parameters: url, method, headers (object or JSON text), body, timeoutMs /
    // timeout, connectTimeoutMs / connectTimeout, maxResponseBytes, and caFile.
    // Both timeout values are milliseconds (1..60000), payload caps 1..8 MiB.
    // HTTP output: status, headers, body (JSON when <=64 KiB and valid),
    // bodyText, bodyBase64 (lossless), bodyBytes, elapsedMs. Non-2xx completes.
    void httpRequest(const QString& operationId, const QJsonObject& parameters);
    // WS accepts url, headers, subprotocol, timeoutMs, maxMessageBytes, caFile.
    // TLS always verifies the certificate chain and hostname; no insecure mode.
    void connectWebSocket(const QString& operationId, const QJsonObject& parameters);
    void sendWebSocket(const QString& operationId, const QByteArray& payload, bool binary);
    void closeWebSocket(const QString& operationId, int code = 1000, const QString& reason = {});
    // Cancels/fences all operations, clears payload handoffs, and starts worker
    // cleanup without waiting on the QObject thread. Retired callbacks are silent.
    void cancelAll();
    bool webSocketConnected() const;
    QString resourceSummary() const;
signals:
    // Non-2xx is a completed HTTP response; response assertions belong to runner.
    void operationFinished(const QString& operationId, const QJsonObject& result, const QString& error);
    void webSocketMessage(const QByteArray& payload, bool binary);
    // Peer metadata is bounded by RFC6455 (123 UTF-8 bytes) and precedes the
    // disconnected state notification. peerInitiated is false for our close.
    void webSocketClosed(int code, const QString& reason, bool peerInitiated);
    void stateChanged();
    void protocolError(const QString& message);
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
}
