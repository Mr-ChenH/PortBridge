#pragma once
#include <portbridge/workflow_protocol.hpp>
#include <QJsonDocument>
#include <QPointer>
#include <QUrl>
#include <boost/asio.hpp>
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <functional>

namespace portbridge::protocol {
namespace net = boost::asio;
constexpr qsizetype PayloadLimit = 8 * 1024 * 1024;
constexpr qsizetype HeaderLimit = 16 * 1024;
// Fits a maximum HTTP result (text/base64/JSON accounting) plus one maximum
// WS handoff and control events; simultaneous completion must never be dropped.
constexpr qsizetype EventLimit = 80 * 1024 * 1024;
struct Event {
    enum Kind { Finished, Message, State, Error, Closed } kind = State;
    uint64_t generation = 0;
    QString id, error;
    QJsonObject result;
    QByteArray bytes;
    bool binary = false;
    qsizetype cost = 256;
    std::function<void()> acknowledge{};
};
// I/O never accesses QObject. Its owning thread drains this finite mailbox.
struct Mailbox {
    std::mutex mutex;
    std::deque<Event> events;
    qsizetype bytes = 0;
    std::atomic<uint64_t> generation{1};
    std::atomic<bool> connected{false};
    bool push(Event event) {
        std::lock_guard<std::mutex> lock(mutex);
        if (event.generation != generation.load()) return false;
        if (events.size() >= 32 || event.cost > EventLimit - bytes) return false;
        bytes += event.cost;
        events.push_back(std::move(event));
        return true;
    }
    bool live(uint64_t token) const { return token == generation.load(); }
    void reset() {
        std::lock_guard<std::mutex> lock(mutex);
        ++generation; connected = false; events.clear(); bytes = 0;
    }
};
struct Request {
    QString id;
    uint64_t token = 0;
    std::string url, host, authority, port, target, caFile, method;
    std::vector<std::pair<std::string, std::string>> headers;
    QByteArray body;
    int timeout = 10000, connectTimeout = 5000;
    qsizetype limit = PayloadLimit;
    bool tls = false;
};
// A single bounded runtime for protocol clients, independent of raw standalone Asio.
// Client destruction posts cleanup; it never joins DNS, TLS or socket I/O on Qt.
class Runtime {
    net::io_context io_;
    net::executor_work_guard<net::io_context::executor_type> work_{net::make_work_guard(io_)};
    std::thread thread_;
public:
    Runtime() : thread_([this] { io_.run(); }) {}
    ~Runtime() { work_.reset(); io_.stop(); if (thread_.joinable()) thread_.join(); }
    net::io_context& io() { return io_; }
    static Runtime& instance() { static Runtime runtime; return runtime; }
};
struct HttpOperation {
    virtual ~HttpOperation() = default;
    virtual void cancel() = 0;
};
struct WebSocket {
    virtual ~WebSocket() = default;
    virtual void start() = 0;
    virtual void send(QString id, QByteArray bytes, bool binary) = 0;
    virtual void close(QString id, int code, QString reason) = 0;
    virtual void cancel() = 0;
};
std::shared_ptr<HttpOperation> startHttp(net::io_context&, std::shared_ptr<Mailbox>, Request);
std::shared_ptr<WebSocket> makePlainWebSocket(net::io_context&, std::shared_ptr<Mailbox>, Request);
std::shared_ptr<WebSocket> makeTlsWebSocket(net::io_context&, std::shared_ptr<Mailbox>, Request);
inline void finished(const std::shared_ptr<Mailbox>& box, const Request& r, QString id, QJsonObject result = {}, QString error = {}, qsizetype cost = 256) {
    box->push({Event::Finished, r.token, std::move(id), std::move(error), std::move(result), {}, false, cost});
}
inline void state(const std::shared_ptr<Mailbox>& box, const Request& r, bool connected) {
    if (!box->live(r.token)) return;
    box->push({Event::State, r.token, {}, {}, {}, {}, connected, 256});
}
inline void closed(const std::shared_ptr<Mailbox>& box, const Request& r, int code, const QString& reason, bool peerInitiated) {
    box->push({Event::Closed, r.token, {}, {}, {{"code", code}, {"reason", reason}, {"peerInitiated", peerInitiated}}, {}, false, 1024});
}
}
