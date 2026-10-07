#include "portbridge/network_engine.hpp"
#include "../src/network/buffer_pool.hpp"
#include "../src/network/udp_receive.hpp"
#include <asio.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace portbridge;
using namespace std::chrono_literals;
using asio::ip::tcp;
using asio::ip::udp;
namespace {
void require(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }
template<class Predicate> void until(Predicate predicate, const std::string& message, std::chrono::milliseconds timeout = 3000ms) {
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        require(std::chrono::steady_clock::now() < end, "Timed out: " + message);
        std::this_thread::sleep_for(1ms);
    }
}
struct Probe {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<TransportEvent> events;
    std::vector<DataRecord> records;
    std::function<bool(const DataRecord&)> consume;
    std::function<void(const TransportEvent&)> eventHook;
    std::thread::id callbackThread;
    NetworkEngine::Callbacks callbacks() {
        return {[this](const DataRecord& r) {
            bool accepted = !consume || consume(r);
            { std::lock_guard<std::mutex> lock(mutex); callbackThread = std::this_thread::get_id(); if (accepted) records.push_back(r); }
            changed.notify_all(); return accepted;
        }, [this](const TransportEvent& e) {
            { std::lock_guard<std::mutex> lock(mutex); callbackThread = std::this_thread::get_id(); events.push_back(e); }
            changed.notify_all(); if (eventHook) eventHook(e);
        }};
    }
    template<class Predicate> void wait(Predicate predicate, const std::string& message) {
        std::unique_lock<std::mutex> lock(mutex);
        require(changed.wait_for(lock, 3s, predicate), "Timed out: " + message);
    }
    bool has(EventKind kind) const { return std::any_of(events.begin(), events.end(), [kind](const auto& e) { return e.kind == kind; }); }
    TransportEvent event(EventKind kind) {
        wait([&] { return has(kind); }, "transport event");
        std::lock_guard<std::mutex> lock(mutex);
        return *std::find_if(events.begin(), events.end(), [kind](const auto& e) { return e.kind == kind; });
    }
    std::size_t bytes(Direction direction) const {
        std::size_t total = 0; for (const auto& r : records) if (r.direction == direction) total += r.payload->size(); return total;
    }
    Bytes joined(Direction direction) {
        std::lock_guard<std::mutex> lock(mutex); Bytes result;
        for (const auto& r : records) if (r.direction == direction) result.insert(result.end(), r.payload->begin(), r.payload->end());
        return result;
    }
};
ConnectionConfig config(TransportKind kind) {
    ConnectionConfig c; c.kind = kind; c.localAddress = "127.0.0.1"; c.localPort = 0;
    c.receiveBufferBytes = 1 << 20; c.sendBufferBytes = 1 << 16; c.sendQueueBytes = 1 << 20; return c;
}
SharedBytes data(std::size_t size, std::uint8_t value = 0x5a) { return std::make_shared<const Bytes>(size, value); }
tcp::socket acceptOne(tcp::acceptor& acceptor, asio::io_context& io) {
    acceptor.non_blocking(true); tcp::socket socket(io);
    until([&] { asio::error_code ec; acceptor.accept(socket, ec); if (!ec) return true;
          require(ec == asio::error::would_block || ec == asio::error::try_again, ec.message()); return false; }, "raw TCP accept");
    return socket;
}
Bytes readExact(tcp::socket& socket, std::size_t wanted) {
    Bytes bytes(wanted); std::size_t n = 0; socket.non_blocking(true);
    until([&] {
        asio::error_code ec; n += socket.read_some(asio::buffer(bytes.data() + n, wanted - n), ec);
        require(!ec || ec == asio::error::would_block || ec == asio::error::try_again, "TCP read: " + ec.message()); return n == wanted;
    }, "raw TCP bytes");
    return bytes;
}
void poolOwnershipAndReuse() {
    SharedBytes retained;
    std::weak_ptr<network_detail::BufferPool> retired;
    {
        auto pool = std::make_shared<network_detail::BufferPool>();
        auto first = pool->acquire(1);
        (*first)[0] = 0x5a;
        auto otherConsumer = first;
        first.reset();
        auto second = pool->acquire(1);
        require(second.get() != otherConsumer.get(), "Buffer reused while another consumer still owns it");
        require((*otherConsumer)[0] == 0x5a, "Live consumer bytes changed");
        auto reusable = second.get(); second.reset();
        auto reused = pool->acquire(1);
        require(reused.get() == reusable, "Released buffer was not reused");
        std::vector<std::shared_ptr<Bytes>> large;
        for (int i = 0; i < 32; ++i) large.push_back(pool->acquire());
        large.clear();
        std::size_t cacheBytes = 0;
        for (const auto& bucket : pool->free) for (const auto& bytes : bucket) cacheBytes += bytes->capacity();
        require(cacheBytes >= 2 * 1024 * 1024, "Pool lifetime regression did not populate the retired cache");
        retained = otherConsumer;
        retired = pool;
    }
    require(retired.expired(), "Tiny retained payload pinned a retired engine's entire free cache");
    require(retained && retained->size() == 1 && retained->front() == 0x5a,
            "Payload no longer valid after its originating pool was destroyed");
    retained.reset();
}
void tcpSplitBackpressure() {
    asio::io_context io; tcp::acceptor acceptor(io, {tcp::v4(), 0});
    Probe probe; std::atomic<bool> allow{false}; std::atomic<unsigned> retries{0};
    SharedBytes held; std::uint64_t heldSequence = 0, heldTime = 0; std::atomic<bool> mismatch{false};
    probe.consume = [&](const DataRecord& r) {
        if (r.direction == Direction::Receive && !allow) {
            if (!held) { held = r.payload; heldSequence = r.sequence; heldTime = r.timestampUs; }
            else if (held != r.payload || heldSequence != r.sequence || heldTime != r.timestampUs) mismatch = true;
            ++retries; return false;
        }
        return true;
    };
    NetworkEngine engine(probe.callbacks()); auto c = config(TransportKind::TcpClient);
    c.remoteAddress = "localhost"; c.remotePort = acceptor.local_endpoint().port(); engine.start(c);
    auto raw = acceptOne(acceptor, io); auto connected = probe.event(EventKind::Connected);
    require(connected.local.address == "127.0.0.1" && connected.local.port != 0, "Client binding not reported");
    require(engine.actualReceiveBufferBytes() > 0 && engine.actualSendBufferBytes() > 0, "Actual OS buffers unavailable");
    Bytes expected(180123); for (std::size_t i = 0; i < expected.size(); ++i) expected[i] = static_cast<std::uint8_t>((i * 37) ^ (i >> 7));
    const std::size_t splits[] = {1, 7, 1024, 3, 8191, 6553, 2};
    std::size_t offset = 0, split = 0;
    while (offset < expected.size()) {
        auto n = std::min(splits[split++ % 7], expected.size() - offset);
        asio::write(raw, asio::buffer(expected.data() + offset, n)); offset += n;
    }
    until([&] { return retries >= 4; }, "TCP retained block retries");
    require(!mismatch, "Backpressure changed retained block metadata or payload");
    { std::lock_guard<std::mutex> lock(probe.mutex); require(probe.bytes(Direction::Receive) == 0, "Rejected block delivered"); }
    allow = true;
    probe.wait([&] { return probe.bytes(Direction::Receive) == expected.size(); }, "TCP complete stream");
    require(probe.joined(Direction::Receive) == expected, "TCP split/merge lost or reordered bytes");
    require(engine.send(data(12345)), "TCP send admission");
    require(readExact(raw, 12345) == Bytes(12345, 0x5a), "TCP TX bytes differ");
    probe.wait([&] { return probe.bytes(Direction::Transmit) == 12345; }, "TCP TX completion");
    { std::lock_guard<std::mutex> lock(probe.mutex); require(probe.callbackThread != std::this_thread::get_id(), "Callbacks on calling thread"); }
    engine.stop(); probe.event(EventKind::Disconnected);
}
void multipleClients() {
    Probe probe; NetworkEngine server(probe.callbacks()); server.start(config(TransportKind::TcpServer));
    auto listening = probe.event(EventKind::Listening); asio::io_context io;
    std::vector<tcp::socket> sockets;
    for (unsigned i = 0; i < 4; ++i) { sockets.emplace_back(io); sockets.back().connect({asio::ip::make_address_v4("127.0.0.1"), listening.local.port}); }
    probe.wait([&] { return std::count_if(probe.events.begin(), probe.events.end(), [](const auto& e) { return e.kind == EventKind::ClientAdded; }) == 4; }, "Four clients");
    std::vector<std::uint64_t> ids;
    { std::lock_guard<std::mutex> lock(probe.mutex);
      for (auto& socket : sockets) {
          auto port = socket.local_endpoint().port();
          auto it = std::find_if(probe.events.begin(), probe.events.end(), [port](const auto& e) { return e.kind == EventKind::ClientAdded && e.peer.port == port; });
          require(it != probe.events.end(), "Client source identity missing"); ids.push_back(it->connectionId);
      } }
    require(std::adjacent_find(ids.begin(), ids.end()) == ids.end(), "Duplicate client IDs");
    require(server.send(data(7, 0x11), ids[2]), "Directed send");
    require(readExact(sockets[2], 7) == Bytes(7, 0x11), "Directed payload mismatch");
    for (unsigned i : {0u, 1u, 3u}) { sockets[i].non_blocking(true); asio::error_code ec; std::uint8_t b;
        sockets[i].read_some(asio::buffer(&b, 1), ec); require(ec == asio::error::would_block, "Directed bytes reached wrong client"); }
    require(server.send(data(13, 0x22), 0, true), "Broadcast send");
    for (auto& socket : sockets) require(readExact(socket, 13) == Bytes(13, 0x22), "Broadcast missing client");
    for (unsigned i = 0; i < 4; ++i) { sockets[i].non_blocking(false); asio::write(sockets[i], asio::buffer(Bytes(11, static_cast<std::uint8_t>(i)))); }
    probe.wait([&] { return probe.bytes(Direction::Receive) == 44; }, "Multi-client receive");
    { std::lock_guard<std::mutex> lock(probe.mutex);
      for (auto id : ids) { std::size_t n = 0; for (const auto& r : probe.records) if (r.direction == Direction::Receive && r.connectionId == id) n += r.payload->size(); require(n == 11, "Receive client identity wrong"); } }
    server.disconnectClient(ids[1]); auto removed = probe.event(EventKind::ClientRemoved);
    require(removed.connectionId == ids[1], "Selective disconnect wrong ID");
    require(!server.send(data(1), ids[1]), "Disconnected target accepted");
    require(server.send(data(5), ids[3]), "Disconnect affected other clients"); readExact(sockets[3], 5);
    server.stop(); probe.event(EventKind::Disconnected);
}
void udpDatagrams() {
    Probe probe; NetworkEngine engine(probe.callbacks()); asio::io_context io;
    udp::socket a(io, {udp::v4(), 0}), b(io, {udp::v4(), 0});
    auto c = config(TransportKind::Udp); c.remoteAddress = "localhost"; c.remotePort = a.local_endpoint().port(); engine.start(c);
    auto bound = probe.event(EventKind::Bound); udp::endpoint destination(asio::ip::make_address_v4("127.0.0.1"), bound.local.port);
    a.send_to(asio::buffer(Bytes{}), destination); a.send_to(asio::buffer(Bytes{1, 2, 3}), destination);
    b.send_to(asio::buffer(Bytes{4, 5}), destination); Bytes large(65507, 0xb7); b.send_to(asio::buffer(large), destination);
    probe.wait([&] { return std::count_if(probe.records.begin(), probe.records.end(), [](const auto& r) { return r.direction == Direction::Receive; }) == 4; }, "UDP four datagrams");
    { std::lock_guard<std::mutex> lock(probe.mutex); std::vector<DataRecord> rx;
      for (auto r : probe.records) if (r.direction == Direction::Receive) rx.push_back(std::move(r));
      require(rx[0].payload->empty() && rx[0].peer.port == a.local_endpoint().port(), "Empty UDP/source lost");
      require(*rx[1].payload == Bytes({1, 2, 3}) && rx[1].peer.port == a.local_endpoint().port(), "UDP source A");
      require(*rx[2].payload == Bytes({4, 5}) && rx[2].peer.port == b.local_endpoint().port(), "UDP source B");
      require(rx[0].payload->capacity() == 0 && rx[1].payload->capacity() <= 256 && rx[2].payload->capacity() <= 256, "Small UDP retained oversized receive capacity");
      require(*rx[3].payload == large, "Large legal UDP truncated"); }
    // Resolution may still be finishing at Bound: wait by retrying admission.
    until([&] { return engine.send(data(0)); }, "Resolved empty UDP TX");
    a.non_blocking(true); udp::endpoint source; Bytes buf(8); std::size_t received = 1;
    until([&] { asio::error_code ec; received = a.receive_from(asio::buffer(buf), source, 0, ec); return !ec; }, "Empty UDP TX arrived");
    require(received == 0 && source.port() == bound.local.port, "Empty UDP transmit source/boundary wrong");
    probe.wait([&] { return std::any_of(probe.records.begin(), probe.records.end(), [](const auto& r) { return r.direction == Direction::Transmit && r.payload->empty(); }); }, "Empty UDP completion");
    require(!engine.send(data(65508)), "Oversized IPv4 UDP admitted");
    engine.stop(); probe.event(EventKind::Disconnected);

    Probe dropped; std::atomic<unsigned> droppedCalls{0};
    dropped.consume = [&](const DataRecord& r) { if (r.direction == Direction::Receive) { ++droppedCalls; return false; } return true; };
    NetworkEngine dropping(dropped.callbacks()); c.remotePort = a.local_endpoint().port(); dropping.start(c);
    auto droppingBound = dropped.event(EventKind::Bound); destination.port(droppingBound.local.port);
    for (int i = 0; i < 8; ++i) a.send_to(asio::buffer(Bytes{9}), destination);
    until([&] { return droppedCalls == 8; }, "UDP false consumer continues receiving"); dropping.stop();
}
void udpDestinationChangePreservesBindingAndQueuedTargets() {
    asio::io_context io;udp::socket first(io,{udp::v4(),0}),second(io,{udp::v4(),0});
    first.non_blocking(true);second.non_blocking(true);
    Probe probe;std::atomic<bool> hold{true},entered{false};
    probe.eventHook=[&](const TransportEvent& event){if(event.kind==EventKind::UdpTargetReady&&hold){entered=true;const auto deadline=std::chrono::steady_clock::now()+3s;while(hold&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(1ms);}};
    NetworkEngine engine(probe.callbacks());auto cfg=config(TransportKind::Udp);cfg.remoteAddress="localhost";cfg.remotePort=first.local_endpoint().port();engine.start(cfg);
    const auto bound=probe.event(EventKind::Bound);until([&]{return entered.load();},"destination readiness callback");
    require(engine.udpTargetReady(),"initial target ready");require(engine.send(data(0)),"empty datagram queued");
    require(!engine.setUdpTarget({"127.0.0.1",second.local_endpoint().port()}),"even a queued empty datagram prevents target mutation");hold=false;
    std::array<char,16> buffer{};udp::endpoint sender;std::size_t received=0;
    auto receive=[&](udp::socket& socket){asio::error_code ec;received=socket.receive_from(asio::buffer(buffer),sender,0,ec);require(!ec||ec==asio::error::would_block||ec==asio::error::try_again,"UDP read");return !ec;};
    until([&]{return receive(first);},"old queued target");require(received==0&&sender.port()==bound.local.port,"queued empty payload uses original local socket");
    bool changed=false;until([&]{return changed||(changed=engine.setUdpTarget({"127.0.0.1",second.local_endpoint().port()}));},"change destination after queue drain");
    until([&]{return engine.udpTargetReady();},"new destination ready");require(engine.send(data(3,0x42)),"send new target");until([&]{return receive(second);},"new peer receives");require(received==3&&buffer[0]==0x42&&sender.port()==bound.local.port,"new destination retains binding and payload");
    probe.wait([&]{return probe.bytes(Direction::Transmit)==3;},"TX completion");
    {std::lock_guard<std::mutex> lock(probe.mutex);std::vector<std::uint16_t> ports;for(const auto& r:probe.records)if(r.direction==Direction::Transmit)ports.push_back(r.peer.port);require(ports==std::vector<std::uint16_t>{first.local_endpoint().port(),second.local_endpoint().port()},"TX metadata follows actual destination");}
    changed=false;until([&]{return changed||(changed=engine.setUdpTarget({"::1",second.local_endpoint().port()}));},"IPv6 target submitted to IPv4 resolver");probe.event(EventKind::Error);require(!engine.udpTargetReady(),"resolution failure never falls back to old target");
    second.send_to(asio::buffer("RX",2),{asio::ip::address_v4::loopback(),bound.local.port});probe.wait([&]{return probe.bytes(Direction::Receive)==2;},"failed destination keeps receiving");
    require(engine.setUdpTarget({"localhost",first.local_endpoint().port()}),"recover target");until([&]{return engine.udpTargetReady();},"recovered");engine.stop();require(!engine.udpTargetReady(),"stop clears readiness");require(!engine.setUdpTarget({"localhost",first.local_endpoint().port()}),"stopped target rejected");
}
void udpExplicitTargetsSendOnlyOnRequest() {
    asio::io_context io;udp::socket first(io,{udp::v4(),0}),second(io,{udp::v4(),0});first.non_blocking(true);second.non_blocking(true);
    Probe probe;NetworkEngine engine(probe.callbacks());auto cfg=config(TransportKind::Udp);cfg.remoteAddress="localhost";cfg.remotePort=first.local_endpoint().port();engine.start(cfg);const auto bound=probe.event(EventKind::Bound);
    until([&]{return engine.udpTargetReady();},"default target ready");
    std::array<char,16> buffer{};udp::endpoint sender;std::size_t size=0;
    auto receive=[&](udp::socket& socket){asio::error_code ec;size=socket.receive_from(asio::buffer(buffer),sender,0,ec);require(!ec||ec==asio::error::would_block||ec==asio::error::try_again,"UDP read");return !ec;};
    require(engine.setUdpTarget({"localhost",second.local_endpoint().port()}),"configure target without send");until([&]{return engine.udpTargetReady();},"changed default target ready");std::this_thread::sleep_for(50ms);
    require(!receive(first)&&!receive(second),"binding and target configuration send no datagram, including zero-length probes");
    require(engine.send(data(0),0,false,Endpoint{"localhost",first.local_endpoint().port()}),"direct empty request admitted");
    require(engine.send(data(3,0x42),0,false,Endpoint{"127.0.0.1",second.local_endpoint().port()}),"second queued request has its own target");
    until([&]{return receive(first);},"first explicit target");require(size==0&&sender.port()==bound.local.port,"empty datagram has correct source");
    until([&]{return receive(second);},"second explicit target");require(size==3&&buffer[0]==0x42&&sender.port()==bound.local.port,"queued target snapshot is preserved");
    probe.wait([&]{return probe.bytes(Direction::Transmit)==3;},"completed explicit TX");
    {std::lock_guard<std::mutex> lock(probe.mutex);std::vector<std::uint16_t> ports;for(const auto& r:probe.records)if(r.direction==Direction::Transmit)ports.push_back(r.peer.port);require(ports==std::vector<std::uint16_t>{first.local_endpoint().port(),second.local_endpoint().port()},"explicit TX endpoints match actual peers");}
    require(engine.send(data(1),0,false,Endpoint{"::1",first.local_endpoint().port()}),"invalid IPv4 target resolves asynchronously");require(engine.send(data(4),0,false,Endpoint{"localhost",second.local_endpoint().port()}),"queue continues after resolution failure");probe.event(EventKind::Error);
    until([&]{return receive(second);},"valid request following failure");require(size==4,"failed request never falls back to a previous destination");require(!receive(first),"failed target emits no datagram");
    until([&]{return engine.pendingSendBytes()==0;},"queue budget released");
    second.send_to(asio::buffer("RX",2),{asio::ip::address_v4::loopback(),bound.local.port});probe.wait([&]{return probe.bytes(Direction::Receive)==2;},"independent RX continues");
}
void udpTruncationCompletionContinues() {
    asio::io_context io;
    udp::socket receiver(io, {asio::ip::make_address_v4("127.0.0.1"), 0});
    udp::socket sender(io, {asio::ip::make_address_v4("127.0.0.1"), 0});
    const auto destination = receiver.local_endpoint();
    const Endpoint local{destination.address().to_string(), destination.port()};
    const Endpoint peer{sender.local_endpoint().address().to_string(), sender.local_endpoint().port()};
    const Endpoint unrelatedDestination{"127.0.0.1", static_cast<std::uint16_t>(peer.port == 1 ? 2 : 1)};
    std::array<std::uint8_t, 65536> scratch{}; // Same maximum-sized buffer as production.
    udp::endpoint source;
    std::vector<TransportEvent> events;
    std::vector<Bytes> delivered;
    std::vector<Endpoint> origins;
    unsigned completions = 0, resumes = 0, failures = 0;
    bool timeout = false;
    const asio::error_code truncation = asio::error::message_size;
    std::function<void()> receive;
    asio::steady_timer deadline(io, 3s);
    deadline.async_wait([&](asio::error_code ec) { if (!ec) { timeout = true; receiver.cancel(); } });
    receive = [&] {
        receiver.async_receive_from(asio::buffer(scratch), source, [&](asio::error_code ec, std::size_t n) {
            if (timeout) return;
            require(!ec && n != 0, "Regression setup did not receive a real nonempty datagram");
            ++completions;
            // A legal IPv4 payload cannot overflow 65536 bytes. Only the first
            // completion's OS error result is simulated, with a positive byte
            // count and real scratch bytes/source to catch accidental admission.
            if (completions == 1) ec = truncation;
            network_detail::completeUdpReceive(ec, n, Endpoint{source.address().to_string(), source.port()},
                local, unrelatedDestination,
                [](const std::string& operation, const asio::error_code& error) {
                    return operation + ": " + error.message() + " (" + error.category().name() + ":" + std::to_string(error.value()) + ")";
                },
                [&](TransportEvent e) { events.push_back(std::move(e)); },
                [&](std::size_t size, const Endpoint& origin) {
                    delivered.emplace_back(scratch.begin(), scratch.begin() + size); origins.push_back(origin);
                },
                [&] {
                    ++resumes;
                    if (completions == 1) {
                        require(delivered.empty(), "Truncated prefix admitted as a complete datagram");
                        receive();
                        sender.send_to(asio::buffer(Bytes{0x73, 0x39, 0xa4}), destination);
                    } else deadline.cancel();
                },
                [&](const std::string&) { ++failures; deadline.cancel(); });
        });
    };
    receive();
    sender.send_to(asio::buffer(Bytes{0xde, 0xad, 0xbe, 0xef}), destination);
    io.run();
    require(!timeout && completions == 2 && resumes == 2 && failures == 0, "Truncation did not continue real socket receive");
    require(events.size() == 1 && events[0].kind == EventKind::ReceiveTruncated, "Truncation not a distinct nonterminal event");
    const auto& event = events[0];
    require(event.connectionId == 0 && event.peer.address == peer.address && event.peer.port == peer.port &&
            event.local.address == local.address && event.local.port == local.port, "Truncation source/local binding lost or replaced with TX destination");
    require(event.message.find("UDP receive truncated datagram") != std::string::npos &&
            event.message.find(truncation.message()) != std::string::npos &&
            event.message.find(std::string(truncation.category().name()) + ":" + std::to_string(truncation.value())) != std::string::npos,
            "Truncation lost operation or OS error detail");
    require(delivered.size() == 1 && delivered[0] == Bytes({0x73, 0x39, 0xa4}) &&
            origins[0].address == peer.address && origins[0].port == peer.port,
            "Next real datagram missing, corrupted, or attributed to the wrong source");
    require(receiver.is_open() && receiver.local_endpoint() == destination, "Truncation retired the local binding");
}
void udpAbsentPeerKeepsBinding() {
    asio::io_context io;
    // Keep the future absent port reserved until the engine is bound, so its
    // ephemeral local port cannot accidentally equal the destination port.
    udp::socket absent(io, {udp::v4(), 0}), source(io, {udp::v4(), 0});
    const auto absentPort = absent.local_endpoint().port();
    Probe probe; NetworkEngine engine(probe.callbacks()); auto c = config(TransportKind::Udp);
    c.remotePort = absentPort; engine.start(c);
    const auto bound = probe.event(EventKind::Bound); absent.close();
    until([&] { return engine.send(data(64)); }, "UDP destination readiness");
#ifdef _WIN32
    const auto notification = probe.event(EventKind::Error);
    require(notification.message.find("UDP peer notification") != std::string::npos, "Absent UDP peer not reported as a peer notification");
    require(notification.local.address == bound.local.address && notification.local.port == bound.local.port &&
            notification.peer.address == "127.0.0.1" && notification.peer.port == absentPort, "UDP ICMP notification endpoints wrong");
#endif
    probe.wait([&] { return probe.bytes(Direction::Transmit) == 64; }, "Completed UDP TX survived absent-peer notification");
    // This source was reserved while the destination port was still occupied,
    // which guarantees it is a distinct endpoint even if ephemeral ports recycle.
    source.send_to(asio::buffer(Bytes{0x73, 0x39}), {asio::ip::make_address_v4("127.0.0.1"), bound.local.port});
    probe.wait([&] { return probe.bytes(Direction::Receive) == 2; }, "Independent UDP source after absent peer");
    {
        std::lock_guard<std::mutex> lock(probe.mutex);
        require(!probe.has(EventKind::Disconnected), "Absent UDP peer closed the local binding");
        auto rx = std::find_if(probe.records.begin(), probe.records.end(), [](const auto& r) { return r.direction == Direction::Receive; });
        require(rx != probe.records.end() && *rx->payload == Bytes({0x73, 0x39}) && rx->peer.port == source.local_endpoint().port(), "UDP receive after ICMP lost payload or source");
    }
    require(engine.send(data(13)), "UDP send state was retired by absent-peer error");
    probe.wait([&] { return probe.bytes(Direction::Transmit) == 77; }, "Second completed UDP TX");
    require(engine.pendingSendBytes() == 0, "Absent UDP peer leaked send reservations");
    engine.stop(); probe.event(EventKind::Disconnected);
}
void boundedAdmissionAndTx() {
    asio::io_context io; tcp::acceptor acceptor(io, {tcp::v4(), 0}); Probe probe;
    std::mutex latchMutex; std::condition_variable latch; bool release = false;
    probe.eventHook = [&](const TransportEvent& e) { if (e.kind == EventKind::Connected) {
        std::unique_lock<std::mutex> lock(latchMutex); latch.wait_for(lock, 2s, [&] { return release; }); } };
    std::atomic<unsigned> txCallbacks{0};
    probe.consume = [&](const DataRecord& r) { if (r.direction == Direction::Transmit) { ++txCallbacks; return false; } return true; };
    NetworkEngine engine(probe.callbacks()); auto c = config(TransportKind::TcpClient);
    c.remotePort = acceptor.local_endpoint().port(); c.sendQueueBytes = 64; engine.start(c);
    auto raw = acceptOne(acceptor, io); probe.event(EventKind::Connected);
    auto oversizedCapacity = std::make_shared<Bytes>(); oversizedCapacity->reserve(1024); oversizedCapacity->push_back(1);
    require(!engine.send(oversizedCapacity), "Send admission ignored retained vector capacity");
    require(engine.send(data(32)) && engine.send(data(32)), "Bounded queue initial admission");
    require(engine.pendingSendBytes() == 64, "Queue reservation not visible");
    for (int i = 0; i < 10000; ++i) require(!engine.send(data(1)), "Overflow admitted");
    require(engine.pendingSendBytes() == 64, "Queue exceeded limit");
    { std::lock_guard<std::mutex> lock(latchMutex); release = true; } latch.notify_all();
    require(readExact(raw, 64) == Bytes(64, 0x5a), "Admitted TX changed");
    until([&] { return txCallbacks == 2 && engine.pendingSendBytes() == 0; }, "TX consumer rejection must not retry writes");
    probe.event(EventKind::SendRejected); raw.non_blocking(true); asio::error_code ec; std::uint8_t byte;
    raw.read_some(asio::buffer(&byte, 1), ec); require(ec == asio::error::would_block, "Completed TX was resent");
    engine.stop();
}
void generationIsolation() {
    asio::io_context io; tcp::acceptor listener(io, {tcp::v4(), 0});
    Probe probe; std::atomic<unsigned> oldAttempts{0};
    probe.consume = [&](const DataRecord& r) {
        if (r.transport == TransportKind::TcpClient) { ++oldAttempts; return false; }
        return true;
    };
    NetworkEngine engine(probe.callbacks()); auto c = config(TransportKind::TcpClient);
    c.remotePort = listener.local_endpoint().port(); engine.start(c);
    auto raw = acceptOne(listener, io); probe.event(EventKind::Connected);
    asio::write(raw, asio::buffer(Bytes{1, 2, 3}));
    until([&] { return oldAttempts > 1; }, "Old generation retained TCP block");
    auto u = config(TransportKind::Udp); engine.start(u);
    const auto bound = probe.event(EventKind::Bound); const auto retiredAttempts = oldAttempts.load();
    udp::socket socket(io, {udp::v4(), 0});
    socket.send_to(asio::buffer(Bytes{4, 5, 6}), {asio::ip::make_address_v4("127.0.0.1"), bound.local.port});
    probe.wait([&] { return probe.bytes(Direction::Receive) == 3; }, "Replacement generation UDP data");
    std::this_thread::sleep_for(20ms);
    require(oldAttempts == retiredAttempts, "Retired TCP backpressure callback leaked into new UDP generation");
    { std::lock_guard<std::mutex> lock(probe.mutex);
      for (const auto& r : probe.records) require(r.transport == TransportKind::Udp && *r.payload == Bytes({4, 5, 6}), "Old data reached replacement session"); }
    engine.stop(); probe.event(EventKind::Disconnected);
}
void cancelTimeoutAndLifecycle() {
    Probe cancel; NetworkEngine* enginePtr = nullptr;
    cancel.eventHook = [&](const TransportEvent& e) { if (e.kind == EventKind::Connecting) enginePtr->stop(); };
    NetworkEngine engine(cancel.callbacks()); enginePtr = &engine;
    auto c = config(TransportKind::TcpClient); c.remoteAddress = "localhost"; c.remotePort = 9;
    auto begin = std::chrono::steady_clock::now(); engine.start(c);
    require(std::chrono::steady_clock::now() - begin < 100ms, "start blocks caller");
    cancel.event(EventKind::Disconnected);
    { std::lock_guard<std::mutex> lock(cancel.mutex); require(!cancel.has(EventKind::Connected) && !cancel.has(EventKind::Error), "Canceled generation produced stale connect/error callback"); }
    Probe timeout;
    // Hold the I/O callback past an already-armed deadline. This makes timeout
    // deterministic without relying on a public address silently dropping SYNs.
    timeout.eventHook = [](const TransportEvent& e) { if (e.kind == EventKind::Connecting) std::this_thread::sleep_for(30ms); };
    NetworkEngine timed(timeout.callbacks()); c.connectTimeoutMs = 1; timed.start(c);
    auto error = timeout.event(EventKind::Error); require(error.message.find("timeout") != std::string::npos, "Deadline did not cancel resolve/connect");
    timeout.event(EventKind::Disconnected);
    Probe refused; NetworkEngine refusing(refused.callbacks());
    asio::io_context io; tcp::acceptor port(io, {tcp::v4(), 0}); c.remotePort = port.local_endpoint().port(); port.close(); c.connectTimeoutMs = 1000;
    refusing.start(c); require(refused.event(EventKind::Error).message.find("TCP connect") != std::string::npos, "Connect refusal lacks precise error");
    Probe repeat; NetworkEngine repeating(repeat.callbacks()); auto u = config(TransportKind::Udp);
    for (unsigned i = 0; i < 25; ++i) {
        { std::lock_guard<std::mutex> lock(repeat.mutex); repeat.events.clear(); }
        repeating.start(u); repeat.event(EventKind::Bound); repeating.stop(); repeat.event(EventKind::Disconnected);
        require(repeating.pendingSendBytes() == 0, "Stop retained send bytes");
    }
    for (unsigned i = 0; i < 50; ++i) { repeating.start(c); repeating.stop(); repeating.start(u); }
    repeating.stop();
    // Destructor is the synchronization barrier even with outstanding reads,
    // accepts, name resolution, writes, and repeated generation changes.
    std::atomic<unsigned> callbacks{0};
    { NetworkEngine temporary({[&](const DataRecord&) { ++callbacks; return true; }, [&](const TransportEvent&) { ++callbacks; }});
      for (unsigned i = 0; i < 20; ++i) { temporary.start(u); temporary.stop(); } temporary.start(config(TransportKind::TcpServer)); }
    const auto finished = callbacks.load(); std::this_thread::sleep_for(20ms);
    require(callbacks == finished, "Callbacks survived engine destruction");
}
}
int main() {
    try {
        const std::pair<const char*, void(*)()> tests[] = {
            {"Retained payload outlives pool without pinning free cache; safe buffer reuse", poolOwnershipAndReuse},
            {"TCP arbitrary splits and retained-block backpressure", tcpSplitBackpressure},
            {"Four TCP clients directed/broadcast/selective disconnect", multipleClients},
            {"UDP empty/large/multiple origins and consumer drops", udpDatagrams},
            {"UDP live target switch preserves binding, queued targets and receive after resolution failure", udpDestinationChangePreservesBindingAndQueuedTargets},
            {"UDP explicit targets require no apply step; configuration emits no datagrams", udpExplicitTargetsSendOnlyOnRequest},
            {"UDP simulated message_size via production helper discards prefix and receives next real datagram", udpTruncationCompletionContinues},
            {"UDP absent peer keeps binding, independent receive, and completed TX", udpAbsentPeerKeepsBinding},
            {"Bounded admission and completed TX not retried", boundedAdmissionAndTx},
            {"Retired TCP block cannot enter replacement UDP generation", generationIsolation},
            {"Cancel/deadline/refusal/repeated stop/destruction", cancelTimeoutAndLifecycle}
        };
        for (const auto& test : tests) { test.second(); std::cout << "PASS " << test.first << '\n'; }
        std::cout << "PASS all network loopback tests (localhost; no hardware throughput claim)\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
