#include "portbridge/network_engine.hpp"
#include "buffer_pool.hpp"
#include "udp_receive.hpp"
#include <asio.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace portbridge {
namespace {
using asio::ip::tcp;
using asio::ip::udp;
using Error = asio::error_code;
constexpr std::size_t blockSize = 65536;
constexpr std::size_t maxQueuedWrites = 4096;
constexpr std::size_t maxClients = 256;
std::uint64_t timestamp() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}
template<class E> Endpoint endpoint(const E& e) { return {e.address().to_string(), e.port()}; }
std::string detail(const std::string& operation, const Error& ec) {
    std::string message = ec.message();
#ifdef _WIN32
    // Asio's Win32 system category formats messages in CP_ACP. Shared contracts
    // use UTF-8, so Chinese Windows error text must not reach Qt as ANSI bytes.
    const int count = MultiByteToWideChar(CP_ACP, 0, message.data(), static_cast<int>(message.size()), nullptr, 0);
    if (count > 0) {
        std::wstring wide(static_cast<std::size_t>(count), L'\0');
        MultiByteToWideChar(CP_ACP, 0, message.data(), static_cast<int>(message.size()), wide.data(), count);
        const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), count, nullptr, 0, nullptr, nullptr);
        if (size > 0) {
            message.resize(static_cast<std::size_t>(size));
            WideCharToMultiByte(CP_UTF8, 0, wide.data(), count, message.data(), size, nullptr, nullptr);
        }
    }
#endif
    return operation + ": " + message + " (" + ec.category().name() + ":" + std::to_string(ec.value()) + ")";
}
using BufferPool = portbridge::network_detail::BufferPool;
}

struct NetworkEngine::Impl {
    struct Write {
        SharedBytes bytes;
        std::uint64_t generation;
        std::size_t charge;
        bool released = false; // accessed exclusively by I/O thread
        std::optional<Endpoint> udpTarget;
    };
    struct Connection {
        tcp::socket socket;
        asio::steady_timer retry;
        std::uint64_t id;
        Endpoint peer, local;
        bool closed = false;
        std::deque<std::shared_ptr<Write>> writes;
        std::optional<DataRecord> held;
        Connection(asio::io_context& io, std::uint64_t value) : socket(io), retry(io), id(value) {}
    };
    struct Run {
        std::uint64_t generation;
        ConnectionConfig config;
        tcp::resolver tcpResolver;
        udp::resolver udpResolver, sendResolver;
        std::optional<Endpoint> cachedSendTarget;
        udp::endpoint cachedSendEndpoint;
        tcp::acceptor acceptor;
        udp::socket datagram;
        asio::steady_timer connectTimer;
        std::map<std::uint64_t, std::shared_ptr<Connection>> connections;
        std::shared_ptr<Connection> connecting;
        std::deque<std::shared_ptr<Write>> udpWrites;
        udp::endpoint destination, source;
        std::array<std::uint8_t, blockSize> udpReceive;
        Endpoint local;
        bool stopped = false, destinationReady = false;
        Run(asio::io_context& io, std::uint64_t gen, ConnectionConfig c)
            : generation(gen), config(std::move(c)), tcpResolver(io), udpResolver(io), sendResolver(io),
              acceptor(io), datagram(io), connectTimer(io) {}
    };
    Callbacks callbacks;
    asio::io_context io;
    asio::executor_work_guard<asio::io_context::executor_type> guard{asio::make_work_guard(io)};
    std::thread worker;
    std::shared_ptr<BufferPool> pool = std::make_shared<BufferPool>();
    std::shared_ptr<Run> current;
    mutable std::mutex admission;
    std::atomic<std::uint64_t> desired{0};
    std::atomic<std::size_t> receiveSize{0}, sendSize{0};
    ConnectionConfig requested;
    std::map<std::uint64_t, Endpoint> targets;
    bool ready = false, rejectionPosted = false, udpBound = false, udpResolving = false;
    std::size_t pending = 0, reserved = 0, queuedWrites = 0;
    std::uint64_t nextClient = 1, nextRecord = 1;

    explicit Impl(Callbacks c) : callbacks(std::move(c)), worker([this] { io.run(); }) {}
    ~Impl() {
        { std::lock_guard<std::mutex> lock(admission); ++desired; ready = false; targets.clear(); pending = reserved = queuedWrites = 0; }
        asio::post(io, [this] { closeRun(current); current.reset(); guard.reset(); });
        // Canceled completions drain before run() returns; no handler survives this join.
        worker.join();
    }
    bool live(const std::shared_ptr<Run>& run) const {
        return run && !run->stopped && run->generation == desired.load();
    }
    void event(const std::shared_ptr<Run>& run, EventKind kind, std::string text,
               std::uint64_t id = 0, Endpoint peer = {}, Endpoint local = {}) {
        if (!live(run) || !callbacks.onEvent) return;
        try { callbacks.onEvent({kind, std::move(text), id, std::move(peer), std::move(local)}); }
        catch (...) { /* User event callbacks cannot unwind through Asio. */ }
    }
    bool deliver(const std::shared_ptr<Run>& run, const DataRecord& record) {
        if (!live(run)) return false;
        try { return !callbacks.onData || callbacks.onData(record); }
        catch (...) {
            event(run, EventKind::Error, "Data consumer threw an exception", record.connectionId, record.peer, run->local);
            return false;
        }
    }
    DataRecord record(const std::shared_ptr<Run>& run, std::uint64_t id, Endpoint peer,
                      Direction direction, SharedBytes bytes) {
        return {nextRecord++, timestamp(), id, std::move(peer), run->config.kind, direction, std::move(bytes)};
    }
    void release(const std::shared_ptr<Write>& write) {
        if (write->released) return;
        write->released = true;
        std::lock_guard<std::mutex> lock(admission);
        if (write->generation == desired.load()) { reserved -= write->charge; pending -= write->bytes->size(); --queuedWrites; }
    }
    void clearWrites(std::deque<std::shared_ptr<Write>>& writes) {
        for (const auto& w : writes) release(w);
        writes.clear();
    }
    void closeRun(const std::shared_ptr<Run>& run) {
        if (!run || run->stopped) return;
        run->stopped = true;
        run->tcpResolver.cancel(); run->udpResolver.cancel(); run->sendResolver.cancel(); run->connectTimer.cancel();
        Error ec;
        run->acceptor.close(ec); run->datagram.close(ec);
        clearWrites(run->udpWrites);
        if (run->connecting) { run->connecting->closed = true; run->connecting->socket.close(ec); }
        for (auto& entry : run->connections) {
            auto& c = entry.second;
            c->closed = true; c->retry.cancel(); c->socket.shutdown(tcp::socket::shutdown_both, ec); c->socket.close(ec); c->held.reset(); clearWrites(c->writes);
        }
        run->connections.clear();
    }
    void invalidate(std::uint64_t generation) {
        std::lock_guard<std::mutex> lock(admission);
        if (desired.load() == generation) { ready = false; udpBound = udpResolving = false; targets.clear(); }
    }
    void fail(const std::shared_ptr<Run>& run, const std::string& message, Endpoint peer = {}) {
        if (!live(run)) return;
        invalidate(run->generation);
        event(run, EventKind::Error, message, 0, peer, run->local);
        event(run, EventKind::Disconnected, message, 0, peer, run->local);
        closeRun(run);
    }
    template<class Socket> bool configure(const std::shared_ptr<Run>& run, Socket& socket) {
        Error ec;
        const auto cap = static_cast<std::size_t>(std::numeric_limits<int>::max());
        if (run->config.receiveBufferBytes) {
            socket.set_option(asio::socket_base::receive_buffer_size(static_cast<int>(std::min(cap, run->config.receiveBufferBytes))), ec);
            if (ec) { event(run, EventKind::Error, detail("Set receive buffer", ec)); return false; }
        }
        if (run->config.sendBufferBytes) {
            socket.set_option(asio::socket_base::send_buffer_size(static_cast<int>(std::min(cap, run->config.sendBufferBytes))), ec);
            if (ec) { event(run, EventKind::Error, detail("Set send buffer", ec)); return false; }
        }
        asio::socket_base::receive_buffer_size rx;
        asio::socket_base::send_buffer_size tx;
        socket.get_option(rx, ec);
        if (ec) { event(run, EventKind::Error, detail("Read receive buffer", ec)); return false; }
        socket.get_option(tx, ec);
        if (ec) { event(run, EventKind::Error, detail("Read send buffer", ec)); return false; }
        if (live(run)) { receiveSize = static_cast<std::size_t>(rx.value()); sendSize = static_cast<std::size_t>(tx.value()); }
        return true;
    }
    void publish(const std::shared_ptr<Run>& run, const std::shared_ptr<Connection>& c = {}) {
        std::lock_guard<std::mutex> lock(admission);
        if (!live(run)) return;
        ready = true;
        if (c) targets[c->id] = c->peer;
    }
    void remove(const std::shared_ptr<Run>& run, const std::shared_ptr<Connection>& c, const std::string& reason) {
        if (c->closed) return;
        c->closed = true;
        Error ec; c->socket.shutdown(tcp::socket::shutdown_both, ec); c->socket.close(ec); c->retry.cancel(); c->held.reset(); clearWrites(c->writes);
        run->connections.erase(c->id);
        { std::lock_guard<std::mutex> lock(admission);
          if (run->generation == desired.load()) { targets.erase(c->id); if (run->config.kind == TransportKind::TcpClient) ready = false; } }
        event(run, run->config.kind == TransportKind::TcpServer ? EventKind::ClientRemoved : EventKind::Disconnected,
              reason, c->id, c->peer, c->local);
    }
    void readTcp(const std::shared_ptr<Run>& run, const std::shared_ptr<Connection>& c) {
        if (!live(run) || c->closed) return;
        auto bytes = pool->acquire();
        c->socket.async_read_some(asio::buffer(*bytes), [this, run, c, bytes](Error ec, std::size_t n) {
            if (!live(run) || c->closed) return;
            // Deliver bytes even when the OS reports an error alongside a partial read.
            if (n) {
                SharedBytes payload;
                if (n <= 8192) {
                    auto small = pool->acquire(n); std::copy_n(bytes->begin(), n, small->begin()); payload = std::move(small);
                } else { bytes->resize(n); payload = bytes; }
                c->held = record(run, c->id, c->peer, Direction::Receive, std::move(payload));
                retryTcp(run, c, ec);
            } else if (ec) {
                if (ec != asio::error::eof) event(run, EventKind::Error, detail("TCP receive", ec), c->id, c->peer, c->local);
                remove(run, c, ec == asio::error::eof ? "Peer closed connection" : detail("TCP receive", ec));
            } else readTcp(run, c);
        });
    }
    void retryTcp(const std::shared_ptr<Run>& run, const std::shared_ptr<Connection>& c, Error readError) {
        if (!live(run) || c->closed || !c->held) return;
        if (!deliver(run, *c->held)) {
            c->retry.expires_after(std::chrono::milliseconds(5));
            c->retry.async_wait([this, run, c, readError](Error ec) { if (!ec) retryTcp(run, c, readError); });
            return;
        }
        c->held.reset();
        if (readError) {
            if (readError != asio::error::eof) event(run, EventKind::Error, detail("TCP receive", readError), c->id, c->peer, c->local);
            remove(run, c, detail("TCP receive", readError));
        } else readTcp(run, c);
    }
    void readUdp(const std::shared_ptr<Run>& run) {
        if (!live(run)) return;
        run->datagram.async_receive_from(asio::buffer(run->udpReceive), run->source, [this, run](Error ec, std::size_t n) {
            if (!live(run)) return;
            network_detail::completeUdpReceive(ec, n, endpoint(run->source), run->local,
                run->destinationReady ? endpoint(run->destination) : Endpoint{}, detail,
                [this, run](TransportEvent e) {
                    event(run, e.kind, std::move(e.message), e.connectionId, std::move(e.peer), std::move(e.local));
                },
                [this, run](std::size_t size, const Endpoint& source) {
                    auto bytes = pool->acquire(size); // empty and tiny records retain a matching size class
                    std::copy_n(run->udpReceive.begin(), size, bytes->begin());
                    deliver(run, record(run, 0, source, Direction::Receive, std::move(bytes)));
                },
                [this, run] { readUdp(run); },
                [this, run](const std::string& message) { fail(run, message); });
        });
    }
    void writeTcp(const std::shared_ptr<Run>& run, const std::shared_ptr<Connection>& c) {
        if (!live(run) || c->closed || c->writes.empty()) return;
        const auto w = c->writes.front();
        asio::async_write(c->socket, asio::buffer(*w->bytes), [this, run, c, w](Error ec, std::size_t n) {
            if (!live(run) || c->closed) { release(w); return; }
            c->writes.pop_front(); release(w);
            if (n) {
                SharedBytes completed = w->bytes;
                if (n != w->bytes->size()) completed = std::make_shared<const Bytes>(w->bytes->begin(), w->bytes->begin() + n);
                // A TX consumer rejection never causes completed bytes to be resent.
                deliver(run, record(run, c->id, c->peer, Direction::Transmit, std::move(completed)));
            }
            if (ec || n != w->bytes->size()) {
                const auto message = "TCP write completed " + std::to_string(n) + "/" + std::to_string(w->bytes->size()) +
                    " bytes; " + (ec ? detail("write", ec) : "incomplete write");
                event(run, EventKind::Error, message, c->id, c->peer, c->local); remove(run, c, message);
            } else writeTcp(run, c);
        });
    }
    void writeUdpTo(const std::shared_ptr<Run>& run,const std::shared_ptr<Write>& w,const udp::endpoint& destination) {
        run->datagram.async_send_to(asio::buffer(*w->bytes), destination, [this, run, w, destination](Error ec, std::size_t n) {
            if (!live(run)) { release(w); return; }
            run->udpWrites.pop_front(); release(w);
            if (!ec && n == w->bytes->size()) {
                deliver(run, record(run, 0, endpoint(destination), Direction::Transmit, w->bytes));
            } else {
                event(run, EventKind::Error, "UDP write completed " + std::to_string(n) + "/" + std::to_string(w->bytes->size()) +
                      " bytes; " + (ec ? detail("write", ec) : "incomplete datagram"), 0, endpoint(destination), run->local);
            }
            writeUdp(run);
        });
    }
    void writeUdp(const std::shared_ptr<Run>& run) {
        if (!live(run) || run->udpWrites.empty()) return;
        auto w=run->udpWrites.front();
        if(!w->udpTarget){if(run->destinationReady)writeUdpTo(run,w,run->destination);return;}
        const auto target=*w->udpTarget;
        if(run->cachedSendTarget && run->cachedSendTarget->address==target.address && run->cachedSendTarget->port==target.port){writeUdpTo(run,w,run->cachedSendEndpoint);return;}
        Error ec;const auto address=asio::ip::make_address_v4(target.address,ec);
        if(!ec){writeUdpTo(run,w,{address,target.port});return;}
        // One resolver operation for the front of the bounded queue. No socket
        // connect, handshake, empty probe or configuration-time datagram.
        run->sendResolver.async_resolve(udp::v4(),target.address,std::to_string(target.port),
            [this,run,w,target](Error error,udp::resolver::results_type results){
                if(!live(run)){release(w);return;}
                if(error||results.empty()){
                    run->udpWrites.pop_front();release(w);
                    event(run,EventKind::Error,error?detail("UDP send target resolution",error):"UDP send target has no IPv4 address",0,target,run->local);
                    writeUdp(run);return;
                }
                run->cachedSendTarget=target;run->cachedSendEndpoint=results.begin()->endpoint();writeUdpTo(run,w,run->cachedSendEndpoint);
            });
    }
    void accept(const std::shared_ptr<Run>& run) {
        if (!live(run)) return;
        auto c = std::make_shared<Connection>(io, nextClient++);
        run->acceptor.async_accept(c->socket, [this, run, c](Error ec) {
            if (!live(run)) return;
            if (ec) { fail(run, detail("TCP accept", ec)); return; }
            Error addressError;
            c->peer = endpoint(c->socket.remote_endpoint(addressError));
            if (!addressError) c->local = endpoint(c->socket.local_endpoint(addressError));
            if (addressError || run->connections.size() >= maxClients || !configure(run, c->socket)) {
                Error ignored; c->socket.close(ignored);
                event(run, EventKind::Error, addressError ? detail("Accepted endpoint", addressError) : "Accepted client rejected: connection limit or socket configuration failure", c->id, c->peer, c->local);
            } else {
                run->connections[c->id] = c; publish(run, c);
                event(run, EventKind::ClientAdded, "TCP client connected", c->id, c->peer, c->local); readTcp(run, c);
            }
            accept(run);
        });
    }
    void resolveUdpTarget(const std::shared_ptr<Run>& run) {
        run->destinationReady = false;
        run->udpResolver.async_resolve(udp::v4(), run->config.remoteAddress, std::to_string(run->config.remotePort),
            [this, run](Error error, udp::resolver::results_type results) {
                if (!live(run)) return;
                const bool success = !error && !results.empty();
                if (success) { run->destination = results.begin()->endpoint(); run->destinationReady = true; }
                { std::lock_guard<std::mutex> lock(admission);
                  if (!live(run)) return;
                  udpResolving = false; ready = success; }
                if (success) event(run, EventKind::UdpTargetReady, "UDP send destination ready (peer online status unknown)", 0, endpoint(run->destination), run->local);
                else event(run, EventKind::Error, error ? detail("UDP destination resolution", error) : "UDP destination resolution returned no IPv4 address", 0, {}, run->local);
            });
    }
    bool setUdpTarget(const Endpoint& target) {
        std::lock_guard<std::mutex> lock(admission);
        if (requested.kind != TransportKind::Udp || !udpBound || udpResolving || queuedWrites ||
            target.address.empty() || target.address.size() > 253 || target.address.find_first_of(" \t\r\n") != std::string::npos || !target.port) return false;
        const auto generation = desired.load();
        ready = false; udpResolving = true;
        requested.remoteAddress = target.address; requested.remotePort = target.port;
        asio::post(io, [this, generation, target] {
            const auto run = current;
            if (!live(run) || run->generation != generation) return;
            run->config.remoteAddress = target.address; run->config.remotePort = target.port;
            resolveUdpTarget(run);
        });
        return true;
    }
    void begin(std::uint64_t generation, ConnectionConfig config) {
        if (generation != desired.load()) return;
        closeRun(current); current.reset();
        if (generation != desired.load()) return;
        auto run = std::make_shared<Run>(io, generation, std::move(config)); current = run;
        Error ec;
        const auto address = asio::ip::make_address_v4(run->config.localAddress.empty() ? "0.0.0.0" : run->config.localAddress, ec);
        if (ec) { fail(run, detail("Local IPv4 address", ec)); return; }
        if (run->config.kind == TransportKind::TcpServer) {
            run->acceptor.open(tcp::v4(), ec);
            if (!ec) run->acceptor.bind({address, run->config.localPort}, ec);
            if (!ec) run->acceptor.listen(asio::socket_base::max_listen_connections, ec);
            if (ec) { fail(run, detail("TCP listen", ec)); return; }
            run->local = endpoint(run->acceptor.local_endpoint(ec));
            if (ec) { fail(run, detail("Listener endpoint", ec)); return; }
            // Report effective buffer values on the listener before any client arrives.
            if (!configure(run, run->acceptor)) { fail(run, "Listener socket buffer configuration failed"); return; }
            publish(run); event(run, EventKind::Listening, "TCP listening", 0, {}, run->local); accept(run);
        } else if (run->config.kind == TransportKind::Udp) {
            run->datagram.open(udp::v4(), ec);
            if (!ec) run->datagram.bind({address, run->config.localPort}, ec);
            if (ec) { fail(run, detail("UDP bind", ec)); return; }
            run->local = endpoint(run->datagram.local_endpoint(ec));
            if (ec) { fail(run, detail("UDP local endpoint", ec)); return; }
            if (!configure(run, run->datagram)) { fail(run, "UDP socket buffer configuration failed"); return; }
            { std::lock_guard<std::mutex> lock(admission);
              if (!live(run)) return;
              udpBound = true; udpResolving = true; }
            event(run, EventKind::Bound, "UDP bound (no peer connectivity implied)", 0, {}, run->local); readUdp(run);
            if (live(run)) resolveUdpTarget(run);
        } else if (run->config.kind == TransportKind::TcpClient) {
            auto c = std::make_shared<Connection>(io, nextClient++); run->connecting = c;
            c->socket.open(tcp::v4(), ec);
            if (!ec) c->socket.bind({address, run->config.localPort}, ec);
            if (ec) { fail(run, detail("TCP local bind", ec)); return; }
            c->local = endpoint(c->socket.local_endpoint(ec)); run->local = c->local;
            if (ec) { fail(run, detail("TCP local endpoint", ec)); return; }
            if (!configure(run, c->socket)) { fail(run, "TCP socket buffer configuration failed"); return; }
            run->connectTimer.expires_after(std::chrono::milliseconds(std::max(1, run->config.connectTimeoutMs)));
            run->connectTimer.async_wait([this, run](Error error) {
                if (!error && live(run) && run->connecting) fail(run, "TCP connect timeout (including name resolution)", {run->config.remoteAddress, run->config.remotePort});
            });
            event(run, EventKind::Connecting, "Resolving and connecting to " + run->config.remoteAddress + ":" + std::to_string(run->config.remotePort), c->id,
                  {run->config.remoteAddress, run->config.remotePort}, c->local);
            if (!live(run)) return;
            run->tcpResolver.async_resolve(tcp::v4(), run->config.remoteAddress, std::to_string(run->config.remotePort),
                [this, run, c](Error error, tcp::resolver::results_type results) {
                    if (!live(run)) return;
                    if (error || results.empty()) { fail(run, error ? detail("TCP resolution", error) : "TCP resolution returned no IPv4 address"); return; }
                    auto endpoints = std::make_shared<std::vector<tcp::endpoint>>();
                    for (const auto& result : results) endpoints->push_back(result.endpoint());
                    connectNext(run, c, endpoints, 0);
                });
        } else fail(run, "NetworkEngine does not support serial transport");
    }
    void connectNext(const std::shared_ptr<Run>& run, const std::shared_ptr<Connection>& c,
                     const std::shared_ptr<std::vector<tcp::endpoint>>& endpoints, std::size_t index) {
        if (!live(run)) return;
        c->socket.async_connect((*endpoints)[index], [this, run, c, endpoints, index](Error ec) {
            if (!live(run)) return;
            if (ec) {
                if (index + 1 < endpoints->size()) {
                    Error error; c->socket.close(error); c->socket.open(tcp::v4(), error);
                    auto address = asio::ip::make_address_v4(run->config.localAddress.empty() ? "0.0.0.0" : run->config.localAddress, error);
                    if (!error) c->socket.bind({address, run->config.localPort}, error);
                    if (error || !configure(run, c->socket)) { fail(run, error ? detail("TCP rebind", error) : "TCP socket configuration failed"); return; }
                    connectNext(run, c, endpoints, index + 1);
                } else fail(run, detail("TCP connect", ec), endpoint((*endpoints)[index]));
                return;
            }
            run->connectTimer.cancel(); run->connecting.reset();
            c->peer = endpoint(c->socket.remote_endpoint(ec));
            if (!ec) c->local = endpoint(c->socket.local_endpoint(ec));
            if (ec) { fail(run, detail("Connected endpoints", ec)); return; }
            run->local = c->local; run->connections[c->id] = c; publish(run, c);
            event(run, EventKind::Connected, "TCP connected", c->id, c->peer, c->local); readTcp(run, c);
        });
    }
    void start(const ConnectionConfig& config) {
        std::uint64_t generation;
        { std::lock_guard<std::mutex> lock(admission);
          generation = ++desired; requested = config; ready = false; udpBound = udpResolving = false; rejectionPosted = false; targets.clear(); pending = reserved = queuedWrites = 0; receiveSize = sendSize = 0; }
        asio::post(io, [this, generation, config] { begin(generation, config); });
    }
    void stop() {
        std::uint64_t generation;
        { std::lock_guard<std::mutex> lock(admission);
          generation = ++desired; ready = false; udpBound = udpResolving = false; rejectionPosted = false; targets.clear(); pending = reserved = queuedWrites = 0; receiveSize = sendSize = 0; }
        asio::post(io, [this, generation] {
            if (generation != desired.load()) return;
            Endpoint local, peer;
            if (current) {
                local = current->local;
                if (current->config.kind == TransportKind::TcpClient && !current->connections.empty()) peer = current->connections.begin()->second->peer;
            }
            closeRun(current); current.reset();
            if (generation != desired.load()) return;
            if (callbacks.onEvent) {
                try { callbacks.onEvent({EventKind::Disconnected, "Stopped", 0, peer, local}); } catch (...) {}
            }
        });
    }
    bool send(SharedBytes bytes, std::uint64_t clientId, bool broadcast, std::optional<Endpoint> udpTarget) {
        std::unique_lock<std::mutex> lock(admission);
        auto generation = desired.load();
        std::string rejection;
        std::vector<std::uint64_t> ids;
        // Budget retained vector capacity as well as payload length. Empty UDP
        // still reserves one unit and one descriptor; pending reports payload bytes.
        std::size_t cost = bytes ? std::max<std::size_t>(1, bytes->capacity()) : 0;
        if(udpTarget)cost+=sizeof(Endpoint)+udpTarget->address.capacity();
        if (!bytes) rejection = "Null send payload";
        else if(udpTarget&&(requested.kind!=TransportKind::Udp||udpTarget->address.empty()||udpTarget->address.size()>253||udpTarget->address.find_first_of(" \t\r\n")!=std::string::npos||!udpTarget->port))rejection="Invalid explicit UDP target";
        else if (udpTarget?!udpBound:!ready) rejection = "Transport is not ready to send";
        else if (requested.kind == TransportKind::Udp) {
            if (bytes->size() > 65507) rejection = "UDP payload exceeds IPv4 maximum 65507 bytes";
            else ids.push_back(0);
        } else if (bytes->empty()) rejection = "Empty TCP send payload";
        else if (broadcast && requested.kind == TransportKind::TcpServer) {
            for (const auto& target : targets) ids.push_back(target.first);
            if (ids.empty()) rejection = "Broadcast has no connected clients";
        } else if (requested.kind == TransportKind::TcpClient && clientId == 0 && !targets.empty()) ids.push_back(targets.begin()->first);
        else if (targets.count(clientId)) ids.push_back(clientId);
        else rejection = "Selected TCP client is not connected";
        if (rejection.empty() && (ids.size() > maxQueuedWrites - queuedWrites ||
            cost > requested.sendQueueBytes || ids.size() > (requested.sendQueueBytes - reserved) / cost))
            rejection = "Send queue capacity exceeded";
        if (!rejection.empty()) {
            // A producer can reject millions of requests: at most one notification is
            // outstanding so rejection itself cannot create an unbounded Asio queue.
            if (!rejectionPosted) {
                rejectionPosted = true;
                asio::post(io, [this, generation, rejection, clientId] {
                    { std::lock_guard<std::mutex> guardLock(admission);
                      if (generation == desired.load()) rejectionPosted = false; }
                    if (current && current->generation == generation) event(current, EventKind::SendRejected, rejection, clientId, {}, current->local);
                });
            }
            return false;
        }
        reserved += cost * ids.size(); pending += bytes->size() * ids.size(); queuedWrites += ids.size();
        // Posting while holding admission preserves ordering against concurrent start/stop calls.
        asio::post(io, [this, generation, bytes = std::move(bytes), ids = std::move(ids), cost, udpTarget=std::move(udpTarget)] {
            auto run = current;
            for (auto id : ids) {
                auto w = std::make_shared<Write>(Write{bytes, generation, cost, false, udpTarget});
                if (!live(run) || run->generation != generation) { release(w); continue; }
                if (run->config.kind == TransportKind::Udp) {
                    run->udpWrites.push_back(w); if (run->udpWrites.size() == 1) writeUdp(run);
                } else {
                    auto found = run->connections.find(id);
                    if (found == run->connections.end()) { release(w); event(run, EventKind::SendRejected, "Client disconnected before queued send", id); }
                    else { auto c = found->second; c->writes.push_back(w); if (c->writes.size() == 1) writeTcp(run, c); }
                }
            }
        });
        return true;
    }
};

NetworkEngine::NetworkEngine(Callbacks callbacks) : d(std::make_unique<Impl>(std::move(callbacks))) {}
NetworkEngine::~NetworkEngine() = default;
void NetworkEngine::start(const ConnectionConfig& config) { d->start(config); }
void NetworkEngine::stop() { d->stop(); }
bool NetworkEngine::send(SharedBytes bytes, std::uint64_t clientId, bool broadcast, std::optional<Endpoint> udpTarget) { return d->send(std::move(bytes), clientId, broadcast, std::move(udpTarget)); }
bool NetworkEngine::setUdpTarget(const Endpoint& target) { return d->setUdpTarget(target); }
bool NetworkEngine::udpTargetReady() const { std::lock_guard<std::mutex> lock(d->admission); return d->requested.kind == TransportKind::Udp && d->udpBound && d->ready; }
void NetworkEngine::disconnectClient(std::uint64_t clientId) {
    const auto generation = d->desired.load();
    asio::post(d->io, [impl = d.get(), generation, clientId] {
        auto run = impl->current;
        if (!impl->live(run) || run->generation != generation) return;
        auto it = run->connections.find(clientId);
        if (it != run->connections.end()) impl->remove(run, it->second, "Disconnected by user");
    });
}
std::size_t NetworkEngine::pendingSendBytes() const { std::lock_guard<std::mutex> lock(d->admission); return d->pending; }
std::size_t NetworkEngine::actualReceiveBufferBytes() const { return d->receiveSize.load(); }
std::size_t NetworkEngine::actualSendBufferBytes() const { return d->sendSize.load(); }
}
