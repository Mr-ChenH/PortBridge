#pragma once
#include "portbridge/workflow_protocol.hpp"
#include <QJsonObject>
#include <QVector>
#include <functional>

namespace portbridge {
struct ProtocolDebugEntry {
    quint64 id{};
    qint64 timestampMs{};
    QString direction, detail, operationId;
    QByteArray payload;
    bool binary{};
    QJsonObject response;
    qsizetype cost{}, payloadBytes{};
};
class ProtocolDebugSession final : public QObject {
    Q_OBJECT
  public:
    enum class Mode { Http, WebSocket };
    enum class Phase { Idle, Requesting, Connecting, Connected, Closing };
    explicit ProtocolDebugSession(Mode mode, QObject *parent = nullptr);
    ~ProtocolDebugSession() override;
    bool start(const QJsonObject &parameters, QString *error = nullptr);
    bool sendMessage(const QByteArray &bytes, bool binary, QString *error = nullptr);
    void close(int code = 1000, const QString &reason = {});
    void cancel();
    bool active() const { return phase_ != Phase::Idle || !sequenceId_.isEmpty(); }
    bool acquireSequence(const QString &id, QString *error = nullptr);
    void releaseSequence(const QString &id);
    QString sequenceId() const { return sequenceId_; }
    bool connected() const { return phase_ == Phase::Connected && client_->webSocketConnected(); }
    bool writing() const { return !sendId_.isEmpty(); }
    Phase phase() const { return phase_; }
    Mode mode() const { return mode_; }
    quint64 epoch() const { return epoch_; }
    const QVector<ProtocolDebugEntry> &entries() const { return entries_; }
    qsizetype retainedBytes() const { return retainedBytes_; }
    quint64 omitted() const { return omitted_; }
    const QJsonObject &latestResponse() const { return latest_; }
    QString latestOperation() const { return latestId_; }
    QString lastError() const { return error_; }
    QString endpoint() const { return endpoint_; }
    const QJsonObject &requestSnapshot() const { return snapshot_; }
    quint64 receivedBytes() const { return received_; }
    quint64 transmittedBytes() const { return transmitted_; }
    void clearHistory();
    void setStartGuard(std::function<bool(QString *)> guard) { guard_ = std::move(guard); }
    void setContextValidator(std::function<QString(const QJsonObject &)> validator) {
        contextValidator_ = std::move(validator);
    }
    QString pendingContextError() const {
        return contextValidator_ ? contextValidator_(pendingParameters_) : QString();
    }
    static QString validate(const QJsonObject &parameters, Mode mode);
  signals:
    void changed();

  private:
    void add(QString direction, QString detail, QByteArray bytes = {}, bool binary = false,
             QJsonObject response = {}, QString operation = {});
    QString nextId();
    WorkflowProtocolClient *client_;
    Mode mode_;
    Phase phase_ = Phase::Idle;
    QString activeId_, sendId_, closeId_, latestId_, error_, endpoint_, sequenceId_;
    QJsonObject pendingParameters_;
    QJsonObject latest_, snapshot_;
    QVector<ProtocolDebugEntry> entries_;
    quint64 epoch_ = 0, nextEntry_ = 0, omitted_ = 0, received_ = 0, transmitted_ = 0;
    qsizetype retainedBytes_ = 0;
    bool starting_ = false, awaitLocalClose_ = false;
    QByteArray sending_;
    bool sendingBinary_ = false;
    std::function<QString(const QJsonObject &)> contextValidator_;
    std::function<bool(QString *)> guard_;
};
} // namespace portbridge
