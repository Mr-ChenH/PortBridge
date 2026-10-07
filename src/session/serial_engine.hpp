#pragma once
#include "portbridge/network_engine.hpp"
#include <QObject>
#include <QThread>
#include <QSerialPort>
#include <QTimer>
#include <atomic>
#include <deque>
#include <mutex>
#include <optional>
namespace portbridge::detail {
class SerialEngine {
public:
    explicit SerialEngine(NetworkEngine::Callbacks callbacks);
    ~SerialEngine();
    void start(const ConnectionConfig& config);
    void stop();
    bool send(SharedBytes payload);
    size_t pendingSendBytes() const;
private:
    void close(bool report = true);
    void receive();
    void written(qint64 count);
    void event(EventKind kind, const QString& message);
    NetworkEngine::Callbacks callbacks_;
    QThread thread_;
    QObject* context_ = nullptr;
    QSerialPort* port_ = nullptr;
    QTimer* retry_ = nullptr;
    ConnectionConfig config_;
    std::uint64_t connection_ = 0;
    std::optional<DataRecord> held_;
    struct Write { SharedBytes bytes; size_t submitted = 0, completed = 0; };
    std::deque<Write> writes_;
    mutable std::mutex mutex_;
    size_t pending_ = 0, limit_ = 0, pendingOperations_ = 0;
    std::uint64_t generation_ = 0;
    bool accepting_ = false;
};
}
