#pragma once
#include "portbridge/types.hpp"
#include <QObject>
#include <QString>
#include <QByteArray>
#include <QVector>
#include <memory>
#include <optional>
#include <functional>

namespace portbridge {
struct Command { QString name; QString input; bool hex = true; QString encoding = QStringLiteral("UTF-8"); QString eol = QStringLiteral("none"); bool periodic = false; int intervalMs = 1000; int count = 10; };
QByteArray encodePayload(const QString& input, bool hex, const QString& encoding, const QString& eol, QString* error = nullptr);
QString formatHex(const SharedBytes& bytes);
QVector<ConnectionConfig> loadProfiles(QString* error = nullptr);
bool saveProfiles(const QVector<ConnectionConfig>& profiles, QString* error = nullptr);
QVector<Command> loadCommands(QString* error = nullptr);
bool saveCommands(const QVector<Command>& commands, QString* error = nullptr);
bool importProfiles(const QString& path, QVector<ConnectionConfig>* profiles, QString* error = nullptr);
bool exportProfiles(const QString& path, const QVector<ConnectionConfig>& profiles, QString* error = nullptr);
bool importCommands(const QString& path, QVector<Command>* commands, QString* error = nullptr);
bool exportCommands(const QString& path, const QVector<Command>& commands, QString* error = nullptr);
// Runs on the calling thread; progress returns false to cancel before commit.
// GUI callers must execute offline exports in their background export worker.
using CaptureExportProgress = std::function<bool(std::uint64_t processedBytes, std::uint64_t totalBytes)>;
bool exportCapture(const QString& capturePath, const QString& outputPath, QString* error = nullptr,
                   const CaptureExportProgress& progress = {});

// Independent receive observation: polled on the QObject thread, never coupled
// to display sampling. Payload storage is shared; queue charge includes capacity
// and metadata. A sticky error means evidence was lost and the observer is fenced.
struct RawObservation {
    std::uint64_t epoch = 0;
    std::vector<DataRecord> records;
    std::vector<TransportEvent> events;
    QString error;
};
class SessionController : public QObject {
    Q_OBJECT
public:
    explicit SessionController(QObject* parent = nullptr);
    ~SessionController() override;
    void start(const ConnectionConfig& config);
    // Stops activity and clears selected-configuration state without connecting.
    void selectConfiguration(const ConnectionConfig& config);
    void stop();
    // Explicit UDP targets belong to the request; the configured default is unchanged.
    bool send(const QByteArray& payload, std::uint64_t clientId = 0, bool broadcast = false, std::optional<Endpoint> udpTarget = std::nullopt);
    bool startPeriodic(const QByteArray& payload, int intervalMs, int count = 0, std::uint64_t clientId = 0, bool broadcast = false, std::optional<Endpoint> udpTarget = std::nullopt);
    bool setUdpTarget(const QString& address, int port, QString* error = nullptr);
    bool udpTargetReady() const;
    void stopPeriodic();
    bool periodicActive() const;
    int periodicSent() const;
    bool connected() const;
    bool connecting() const;
    QString statusText() const;
    QString lastError() const;
    ConnectionConfig config() const;
    Endpoint localEndpoint() const;
    std::vector<ClientInfo> clients() const;
    void disconnectClient(std::uint64_t id);
    std::uint64_t sessionEpoch() const;
    // At most four observers; 256 bytes..64 MiB each. Zero indicates rejection.
    std::uint64_t observeRaw(std::size_t queueBytes, QString* error = nullptr);
    RawObservation takeRawObservation(std::uint64_t observerId);
    void removeRawObserver(std::uint64_t observerId);
    void setHighSpeed(bool enabled);
    bool highSpeed() const;
    void setDisplayPaused(bool paused);
    bool displayPaused() const;
    void clearDisplay();
    void resetStatistics();
    Statistics statistics() const;
    std::vector<DataRecord> takeDisplayRecords();
    bool startRecording(const RecordingOptions& options, QString* error = nullptr);
    void stopRecording();
    bool recording() const;
    std::vector<CaptureInfo> captures() const;
    // Coalesced asynchronous rescan; callers can poll captures().
    void refreshCaptures(const QString& directory);
signals:
    void stateChanged();
    void statisticsChanged();
    void dataAvailable();
    void clientsChanged();
    void errorOccurred(const QString& message);
    void recordingChanged();
    void periodicChanged();
private:
    friend struct detailSessionTestAccess;
    struct Impl;
    std::unique_ptr<Impl> d;
};
}
