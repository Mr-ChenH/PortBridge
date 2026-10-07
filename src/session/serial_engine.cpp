#include "serial_engine.hpp"
#include "session_private.hpp"
#include <QSerialPortInfo>
#include <algorithm>
namespace portbridge::detail {
SerialEngine::SerialEngine(NetworkEngine::Callbacks callbacks) : callbacks_(std::move(callbacks)) {
    context_ = new QObject; context_->moveToThread(&thread_);
    QObject::connect(&thread_, &QThread::finished, context_, &QObject::deleteLater);
    thread_.setObjectName("PortBridge serial I/O"); thread_.start();
    QMetaObject::invokeMethod(context_, [this] {
        port_ = new QSerialPort(context_); retry_ = new QTimer(context_); retry_->setInterval(10);
        QObject::connect(port_, &QSerialPort::readyRead, context_, [this] { receive(); });
        QObject::connect(port_, &QSerialPort::bytesWritten, context_, [this](qint64 n) { written(n); });
        QObject::connect(retry_, &QTimer::timeout, context_, [this] { receive(); });
        QObject::connect(port_, &QSerialPort::errorOccurred, context_, [this](QSerialPort::SerialPortError code) {
            if (code == QSerialPort::NoError || code == QSerialPort::NotOpenError) return;
            event(EventKind::Error, port_->errorString()); close();
        });
    }, Qt::QueuedConnection);
}
SerialEngine::~SerialEngine() {
    QMetaObject::invokeMethod(context_, [this] { close(); thread_.quit(); }, Qt::QueuedConnection);
    thread_.wait();
}
void SerialEngine::event(EventKind kind, const QString& message) {
    TransportEvent e; e.kind = kind; e.message = utf8(message); e.connectionId = connection_; e.peer.address = config_.serialPort; e.local.address = config_.serialPort;
    if (callbacks_.onEvent) callbacks_.onEvent(e);
}
void SerialEngine::close(bool report) {
    { std::lock_guard<std::mutex> lock(mutex_); accepting_ = false; pending_ = pendingOperations_ = 0; ++generation_; }
    retry_->stop(); held_.reset(); writes_.clear();
    const bool wasOpen = port_->isOpen();
    if (wasOpen) port_->close();
    if (report) event(EventKind::Disconnected, wasOpen ? "Serial port closed" : "Serial port is not open");
}
void SerialEngine::start(const ConnectionConfig& config) {
    { std::lock_guard<std::mutex> lock(mutex_); accepting_ = false; ++generation_; }
    QMetaObject::invokeMethod(context_, [this, config] {
        close(false); config_ = config; ++connection_; event(EventKind::Connecting, "Opening serial port");
        // Enumeration is advisory: a manually entered device name remains valid.
        for (const auto& info : QSerialPortInfo::availablePorts()) {
            if (info.portName() == text(config.serialPort) || info.systemLocation() == text(config.serialPort)) { port_->setPort(info); break; }
        }
        port_->setPortName(text(config.serialPort));
        const auto parity = config.parity == "Even" ? QSerialPort::EvenParity : config.parity == "Odd" ? QSerialPort::OddParity : config.parity == "Mark" ? QSerialPort::MarkParity : config.parity == "Space" ? QSerialPort::SpaceParity : QSerialPort::NoParity;
        const auto stops = config.stopBits == "2" ? QSerialPort::TwoStop : config.stopBits == "1.5" ? QSerialPort::OneAndHalfStop : QSerialPort::OneStop;
        const auto flow = config.flowControl == "Hardware" ? QSerialPort::HardwareControl : config.flowControl == "Software" ? QSerialPort::SoftwareControl : QSerialPort::NoFlowControl;
        if (!port_->setBaudRate(config.baudRate) || !port_->setDataBits(QSerialPort::DataBits(config.dataBits)) || !port_->setParity(parity) || !port_->setStopBits(stops) || !port_->setFlowControl(flow)) { event(EventKind::Error, "Serial configuration failed: " + port_->errorString()); close(); return; }
        port_->setReadBufferSize(qint64(config.receiveBufferBytes));
        if (!port_->open(QIODevice::ReadWrite)) { event(EventKind::Error, "Serial open failed: " + port_->errorString()); close(); return; }
        { std::lock_guard<std::mutex> lock(mutex_); accepting_ = true; pending_ = pendingOperations_ = 0; limit_ = config.sendQueueBytes; }
        retry_->start(); event(EventKind::Connected, "Serial port open");
    }, Qt::QueuedConnection);
}
void SerialEngine::stop() {
    { std::lock_guard<std::mutex> lock(mutex_); accepting_ = false; ++generation_; }
    QMetaObject::invokeMethod(context_, [this] { close(); }, Qt::QueuedConnection);
}
bool SerialEngine::send(SharedBytes bytes) {
    if (!bytes || bytes->empty()) return false;
    std::uint64_t generation;
    { std::lock_guard<std::mutex> lock(mutex_); if (!accepting_ || pendingOperations_ >= 4096 || bytes->size() > limit_ || pending_ > limit_ - bytes->size()) return false; pending_ += bytes->size(); ++pendingOperations_; generation = generation_; }
    QMetaObject::invokeMethod(context_, [this, bytes, generation] {
        { std::lock_guard<std::mutex> lock(mutex_); if (generation != generation_ || !accepting_) return; }
        const auto n = port_->write(reinterpret_cast<const char*>(bytes->data()), qint64(bytes->size()));
        if (n < 0) { event(EventKind::Error, "Serial write failed: " + port_->errorString()); close(); return; }
        if (n != qint64(bytes->size())) {
            { std::lock_guard<std::mutex> lock(mutex_); pending_ -= bytes->size() - size_t(n); }
            event(EventKind::SendRejected, "Serial write accepted only part of the payload");
        }
        if (n > 0) writes_.push_back({bytes, size_t(n), 0});
        else { std::lock_guard<std::mutex> lock(mutex_); --pendingOperations_; }
    }, Qt::QueuedConnection);
    return true;
}
size_t SerialEngine::pendingSendBytes() const { std::lock_guard<std::mutex> lock(mutex_); return pending_; }
void SerialEngine::written(qint64 count) {
    while (count > 0 && !writes_.empty()) {
        auto& w = writes_.front(); const auto n = std::min(size_t(count), w.submitted - w.completed);
        DataRecord r; r.transport = TransportKind::Serial; r.direction = Direction::Transmit; r.connectionId = connection_; r.peer.address = config_.serialPort; r.timestampUs = nowUs();
        if (n == w.bytes->size()) r.payload = w.bytes;
        else r.payload = std::make_shared<const Bytes>(w.bytes->begin() + ptrdiff_t(w.completed), w.bytes->begin() + ptrdiff_t(w.completed + n));
        w.completed += n; count -= qint64(n);
        { std::lock_guard<std::mutex> lock(mutex_); pending_ -= std::min(pending_, n); }
        if (callbacks_.onData) callbacks_.onData(r);
        if (w.completed == w.submitted) {
            writes_.pop_front(); std::lock_guard<std::mutex> lock(mutex_); if (pendingOperations_) --pendingOperations_;
        }
    }
}
void SerialEngine::receive() {
    if (!port_->isOpen()) return;
    if (held_) { if (callbacks_.onData && !callbacks_.onData(*held_)) return; held_.reset(); }
    // Bound each event-loop turn, and leave unread bytes in the configured serial buffer.
    for (int i = 0; i < 32 && port_->bytesAvailable() > 0; ++i) {
        const auto bytes = port_->read(std::min<qint64>(65536, port_->bytesAvailable()));
        if (bytes.isEmpty()) break;
        DataRecord r; r.transport = TransportKind::Serial; r.direction = Direction::Receive; r.connectionId = connection_; r.peer.address = config_.serialPort; r.timestampUs = nowUs();
        r.payload = std::make_shared<const Bytes>(reinterpret_cast<const std::uint8_t*>(bytes.constData()), reinterpret_cast<const std::uint8_t*>(bytes.constData()) + bytes.size());
        if (callbacks_.onData && !callbacks_.onData(r)) { held_ = std::move(r); break; }
    }
}
}
