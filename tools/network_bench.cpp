#include "portbridge/network_engine.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace portbridge;
using Clock = std::chrono::steady_clock;
namespace {
constexpr std::size_t headerSize = 24;
constexpr std::size_t sequenceWindow = 65536;
struct Options {
    std::string mode = "self-loop", protocol = "udp", host = "127.0.0.1", bind = "127.0.0.1";
    std::uint16_t port = 19090, localPort = 0;
    std::size_t payload = 1472, queueBytes = 8 * 1024 * 1024, receiveBuffer = 16 * 1024 * 1024, sendBuffer = 1024 * 1024;
    double rate = 10000, duration = 5;
    unsigned drainMs = 1000;
    std::uint64_t expected = 0;
    std::uint32_t stream = 1;
};
std::string jsonString(const std::string& value) {
    std::ostringstream result; result << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') result << '\\' << c;
        else if (c >= 32) result << c;
        else result << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(c) << std::dec;
    }
    result << '"'; return result.str();
}
std::uint64_t number(const std::string& value) {
    if (value.empty() || value.front() == '-') throw std::runtime_error("Expected non-negative integer: " + value);
    std::size_t used = 0; auto result = std::stoull(value, &used);
    if (used != value.size()) throw std::runtime_error("Invalid integer: " + value);
    return result;
}
Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string key = argv[i];
        if (key == "--help") {
            std::cout << "PortBridge headless TCP/UDP benchmark\n"
                "  --mode self-loop|send|receive   --protocol tcp|udp\n"
                "  --host IPv4-or-hostname       --bind local-IPv4\n"
                "  --port 1..65535               --local-port 0..65535\n"
                "  --payload bytes (24..65507 UDP, 24..16777216 TCP)\n"
                "  --rate frames-per-second (0=unlimited) --duration seconds\n"
                "  --drain-ms milliseconds       --queue-bytes bytes\n"
                "  --receive-buffer bytes        --send-buffer bytes\n"
                "  --expected frames (receive mode, one sender; 0=unknown trailing loss)\n"
                "  --stream integer              (sender identity in test frame)\n"
                "Self-loop uses localhost and an ephemeral receiving port; it does not measure physical 2.5G hardware.\n"
                "Rate counts complete test frames/datagrams, payload includes the 24-byte header.\n"
                "Output is one JSON object; receivers announce their bound endpoint on stderr.\n";
            std::exit(0);
        }
        if (i + 1 == argc) throw std::runtime_error("Missing value for " + key);
        std::string value = argv[++i];
        if (key == "--mode") o.mode = value;
        else if (key == "--protocol") o.protocol = value;
        else if (key == "--host") o.host = value;
        else if (key == "--bind") o.bind = value;
        else if (key == "--port" || key == "--local-port") {
            auto n = number(value); if (n > 65535 || (key == "--port" && n == 0)) throw std::runtime_error("Port out of range");
            if (key == "--port") o.port = static_cast<std::uint16_t>(n); else o.localPort = static_cast<std::uint16_t>(n);
        }
        else if (key == "--payload") o.payload = static_cast<std::size_t>(number(value));
        else if (key == "--queue-bytes") o.queueBytes = static_cast<std::size_t>(number(value));
        else if (key == "--receive-buffer") o.receiveBuffer = static_cast<std::size_t>(number(value));
        else if (key == "--send-buffer") o.sendBuffer = static_cast<std::size_t>(number(value));
        else if (key == "--expected") o.expected = number(value);
        else if (key == "--stream" || key == "--drain-ms") {
            auto n = number(value); if (n > std::numeric_limits<std::uint32_t>::max()) throw std::runtime_error("Integer out of range");
            if (key == "--stream") o.stream = static_cast<std::uint32_t>(n); else o.drainMs = static_cast<unsigned>(n);
        }
        else if (key == "--rate" || key == "--duration") {
            std::size_t used = 0; double n = std::stod(value, &used);
            if (used != value.size() || !std::isfinite(n) || n < 0) throw std::runtime_error("Invalid numeric value");
            if (key == "--rate") o.rate = n; else o.duration = n;
        } else throw std::runtime_error("Unknown option: " + key);
    }
    if (o.mode != "self-loop" && o.mode != "send" && o.mode != "receive") throw std::runtime_error("Invalid mode");
    if (o.protocol != "tcp" && o.protocol != "udp") throw std::runtime_error("Invalid protocol");
    if (o.payload < headerSize || o.payload > (o.protocol == "udp" ? 65507u : 16777216u)) throw std::runtime_error("Payload out of range");
    if (o.queueBytes < o.payload) throw std::runtime_error("Queue must admit at least one payload");
    if (o.duration <= 0 || o.duration > 86400 || o.drainMs > 60000) throw std::runtime_error("Duration/drain out of range");
    if (o.rate > 100000000) throw std::runtime_error("Requested frame rate exceeds tool limit");
    return o;
}
void put32(Bytes& b, std::size_t p, std::uint32_t n) { for (unsigned i = 0; i < 4; ++i) b[p + i] = static_cast<std::uint8_t>(n >> (24 - 8 * i)); }
void put64(Bytes& b, std::size_t p, std::uint64_t n) { for (unsigned i = 0; i < 8; ++i) b[p + i] = static_cast<std::uint8_t>(n >> (56 - 8 * i)); }
std::uint32_t get32(const std::uint8_t* b) { std::uint32_t n = 0; for (unsigned i = 0; i < 4; ++i) n = (n << 8) | b[i]; return n; }
std::uint64_t get64(const std::uint8_t* b) { std::uint64_t n = 0; for (unsigned i = 0; i < 8; ++i) n = (n << 8) | b[i]; return n; }
std::uint32_t checksum(const std::uint8_t* bytes, std::size_t size) {
    std::uint32_t n = 2166136261u;
    for (std::size_t i = 0; i < size; ++i) { n ^= (i >= 16 && i < 20) ? 0 : bytes[i]; n *= 16777619u; }
    return n;
}
SharedBytes frame(const Options& o, std::uint64_t sequence) {
    auto bytes = std::make_shared<Bytes>(o.payload);
    put32(*bytes, 0, 0x50424231u); put32(*bytes, 4, static_cast<std::uint32_t>(o.payload));
    put64(*bytes, 8, sequence); put32(*bytes, 20, o.stream);
    for (std::size_t i = headerSize; i < bytes->size(); ++i) (*bytes)[i] = static_cast<std::uint8_t>((sequence * 31) ^ (i * 17) ^ o.stream);
    put32(*bytes, 16, checksum(bytes->data(), bytes->size())); return bytes;
}
struct Sequence {
    std::vector<std::uint64_t> slots = std::vector<std::uint64_t>(sequenceWindow, std::numeric_limits<std::uint64_t>::max());
    std::uint64_t unique = 0, high = 0, duplicates = 0, reordered = 0, lateUnknown = 0;
    bool seen = false;
    void add(std::uint64_t n) {
        if (seen && high >= sequenceWindow && n <= high - sequenceWindow) { ++lateUnknown; return; }
        auto& slot = slots[n % sequenceWindow];
        if (slot == n) { ++duplicates; return; }
        if (seen && n < high) ++reordered;
        slot = n; ++unique; high = seen ? std::max(high, n) : n; seen = true;
    }
};
struct Metrics {
    std::mutex mutex;
    std::uint64_t wireBytes = 0, datagrams = 0, valid = 0, invalid = 0, invalidBytes = 0, partialTcpBytes = 0;
    std::uint64_t txBytes = 0, txFrames = 0, errors = 0, teardownErrors = 0;
    std::uint64_t receiveTruncated = 0, teardownReceiveTruncated = 0;
    std::size_t peakPending = 0;
    std::map<std::string, Sequence> sequences;
    std::map<std::uint64_t, Bytes> tcpFrames;
    std::string firstError;
    std::atomic<bool> fatal{false};
    void transportEvent(const TransportEvent& event, bool teardown) {
        if (event.kind != EventKind::Error && event.kind != EventKind::ReceiveTruncated) return;
        std::lock_guard<std::mutex> lock(mutex);
        const bool truncated = event.kind == EventKind::ReceiveTruncated;
        if (truncated) ++receiveTruncated;
        // Keep original transport/teardown error totals compatible: truncation
        // was previously a generic Error. The new counters identify that subset.
        if (teardown) {
            ++teardownErrors;
            if (truncated) ++teardownReceiveTruncated;
            return;
        }
        ++errors;
        if (firstError.empty()) firstError = event.message;
        fatal = true;
    }
    std::uint64_t unique() const { std::uint64_t n = 0; for (auto& s : sequences) n += s.second.unique; return n; }
    void inspect(const std::uint8_t* bytes, std::size_t size, const std::string& peer) {
        if (size < headerSize || get32(bytes) != 0x50424231u || get32(bytes + 4) != size ||
            checksum(bytes, size) != get32(bytes + 16)) { ++invalid; invalidBytes += size; return; }
        auto sequence = get64(bytes + 8); auto stream = get32(bytes + 20);
        for (std::size_t i = headerSize; i < size; ++i) if (bytes[i] != static_cast<std::uint8_t>((sequence * 31) ^ (i * 17) ^ stream)) {
            ++invalid; invalidBytes += size; return;
        }
        // Avoid attacker-controlled enormous gap counts or unlimited sender identities.
        if (sequence > (1ull << 48)) { ++invalid; invalidBytes += size; return; }
        auto key = peer + "/" + std::to_string(stream);
        auto it = sequences.find(key);
        if (it == sequences.end()) {
            if (sequences.size() >= 256) { ++invalid; invalidBytes += size; return; }
            it = sequences.emplace(key, Sequence{}).first;
        }
        it->second.add(sequence); ++valid;
    }
    bool receive(const DataRecord& record, const Options& o) {
        if (record.direction != Direction::Receive) return true;
        std::lock_guard<std::mutex> lock(mutex);
        const auto& bytes = *record.payload; wireBytes += bytes.size();
        std::string peer = record.peer.address + ":" + std::to_string(record.peer.port);
        if (o.protocol == "udp") { ++datagrams; inspect(bytes.data(), bytes.size(), peer); return true; }
        auto& assembled = tcpFrames[record.connectionId];
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            auto n = std::min(o.payload - assembled.size(), bytes.size() - offset);
            assembled.insert(assembled.end(), bytes.begin() + offset, bytes.begin() + offset + n); offset += n;
            if (assembled.size() == o.payload) { inspect(assembled.data(), assembled.size(), peer); assembled.clear(); }
        }
        return true;
    }
};
struct Ready {
    std::mutex mutex;
    std::condition_variable cv;
    bool ready = false, failed = false;
    Endpoint local;
    std::string error;
    void signal(const TransportEvent& event, bool receiver) {
        std::lock_guard<std::mutex> lock(mutex);
        if (event.kind == EventKind::Connected || event.kind == EventKind::Bound || event.kind == EventKind::Listening) {
            ready = true; local = event.local;
            if (receiver) std::cerr << "ready " << local.address << ':' << local.port << '\n';
        }
        if (event.kind == EventKind::Error && !ready) { failed = true; error = event.message; }
        cv.notify_all();
    }
    void wait() {
        std::unique_lock<std::mutex> lock(mutex);
        if (!cv.wait_for(lock, std::chrono::seconds(7), [&] { return ready || failed; })) throw std::runtime_error("Transport startup timeout");
        if (failed) throw std::runtime_error(error);
    }
};
ConnectionConfig connection(const Options& o, bool receiver) {
    ConnectionConfig c;
    c.kind = o.protocol == "udp" ? TransportKind::Udp : (receiver ? TransportKind::TcpServer : TransportKind::TcpClient);
    c.localAddress = o.bind; c.localPort = receiver ? o.port : o.localPort;
    c.remoteAddress = o.host; c.remotePort = o.port;
    c.sendQueueBytes = o.queueBytes; c.receiveBufferBytes = o.receiveBuffer; c.sendBufferBytes = o.sendBuffer;
    return c;
}
int run(const Options& o) {
    Metrics metrics; Ready rxReady, txReady;
    std::unique_ptr<NetworkEngine> receiver, sender;
    std::atomic<bool> sendingDone{false};
    auto onError = [&](const TransportEvent& e) { metrics.transportEvent(e, sendingDone.load()); };
    if (o.mode != "send") {
        receiver = std::make_unique<NetworkEngine>(NetworkEngine::Callbacks{
            [&](const DataRecord& r) { return metrics.receive(r, o); },
            [&](const TransportEvent& e) {
                rxReady.signal(e, true); onError(e);
                if (e.kind == EventKind::ClientRemoved) {
                    std::lock_guard<std::mutex> lock(metrics.mutex);
                    auto it = metrics.tcpFrames.find(e.connectionId);
                    if (it != metrics.tcpFrames.end()) { metrics.partialTcpBytes += it->second.size(); metrics.tcpFrames.erase(it); }
                }
            }});
        auto c = connection(o, true);
        if (o.mode == "self-loop") { c.localAddress = "127.0.0.1"; c.localPort = 0; c.remoteAddress = "127.0.0.1"; }
        receiver->start(c); rxReady.wait();
    }
    if (o.mode != "receive") {
        sender = std::make_unique<NetworkEngine>(NetworkEngine::Callbacks{
            [&](const DataRecord& r) {
                if (r.direction == Direction::Transmit) {
                    std::lock_guard<std::mutex> lock(metrics.mutex); metrics.txBytes += r.payload->size();
                    if (r.payload->size() == o.payload) ++metrics.txFrames;
                }
                return true;
            },
            [&](const TransportEvent& e) {
                txReady.signal(e, false); onError(e);
                if (e.kind == EventKind::Disconnected && !sendingDone && txReady.ready) metrics.fatal = true;
            }});
        auto c = connection(o, false);
        if (o.mode == "self-loop") { c.localAddress = "127.0.0.1"; c.remoteAddress = "127.0.0.1"; c.remotePort = rxReady.local.port; }
        sender->start(c); txReady.wait();
    }
    auto started = Clock::now();
    const auto activeEnd = started + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(o.duration));
    std::uint64_t attempts = 0, admitted = 0, rejected = 0, admissionRetry = 0;
    double sendSeconds = 0;
    SharedBytes next;
    while (Clock::now() < activeEnd && !metrics.fatal) {
        if (!sender) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); continue; }
        if (o.rate > 0) {
            auto scheduled = started + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(static_cast<double>(admitted) / o.rate));
            auto now = Clock::now();
            if (now < scheduled) {
                if (scheduled >= activeEnd) break;
                // Sleep only the bulk of the interval, then yield: no per-frame
                // millisecond sleep that would impose a false 1000-PPS ceiling.
                if (scheduled - now > std::chrono::milliseconds(1)) std::this_thread::sleep_until(scheduled - std::chrono::milliseconds(1));
                else std::this_thread::yield();
                continue;
            }
        }
        if (!next) next = frame(o, admitted);
        ++attempts;
        if (sender->send(next)) { ++admitted; next.reset(); }
        else { ++rejected; ++admissionRetry; std::this_thread::sleep_for(std::chrono::microseconds(100)); }
        { std::lock_guard<std::mutex> lock(metrics.mutex); metrics.peakPending = std::max(metrics.peakPending, sender->pendingSendBytes()); }
    }
    sendSeconds = std::chrono::duration<double>(Clock::now() - started).count();
    const auto drainEnd = Clock::now() + std::chrono::milliseconds(o.drainMs);
    bool sendDrained = !sender;
    while (sender && Clock::now() < drainEnd && !metrics.fatal) {
        bool receivedAll;
        { std::lock_guard<std::mutex> lock(metrics.mutex);
          sendDrained = sender->pendingSendBytes() == 0 && metrics.txFrames == admitted;
          receivedAll = !receiver || metrics.unique() >= metrics.txFrames; }
        if (sendDrained && receivedAll) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!sender) std::this_thread::sleep_for(std::chrono::milliseconds(o.drainMs));
    const auto measuredSeconds = std::chrono::duration<double>(Clock::now() - started).count();
    const auto rxBuffer = receiver ? receiver->actualReceiveBufferBytes() : 0;
    const auto txBuffer = sender ? sender->actualSendBufferBytes() : 0;
    const auto pendingAtEnd = sender ? sender->pendingSendBytes() : 0;
    sendingDone = true;
    sender.reset(); receiver.reset(); // joins both I/O threads before reading results
    std::lock_guard<std::mutex> lock(metrics.mutex);
    for (const auto& partial : metrics.tcpFrames) metrics.partialTcpBytes += partial.second.size();
    std::uint64_t duplicate = 0, reordered = 0, late = 0, observedMissing = 0;
    for (const auto& stream : metrics.sequences) {
        duplicate += stream.second.duplicates; reordered += stream.second.reordered; late += stream.second.lateUnknown;
        observedMissing += stream.second.high + 1 - stream.second.unique;
    }
    const auto unique = metrics.unique();
    bool expectedKnown = o.mode == "self-loop" || (o.mode == "receive" && o.expected > 0);
    const auto expected = o.mode == "self-loop" ? metrics.txFrames : o.expected;
    const auto missing = expectedKnown ? (expected > unique ? expected - unique : 0) : observedMissing;
    const bool localhost = o.mode == "self-loop" || (o.mode == "receive" ? o.bind == "127.0.0.1" :
        (o.host == "127.0.0.1" || o.host == "localhost"));
    const auto effectiveHost = o.mode == "self-loop" ? "127.0.0.1" : o.host;
    const auto effectiveBind = o.mode == "self-loop" ? "127.0.0.1" : o.bind;
    // TCP is a reliability test; UDP loss is an observed result, not a transport failure.
    const bool consistent = !metrics.fatal && !metrics.errors && !metrics.invalid && !metrics.partialTcpBytes &&
        sendDrained && (o.protocol != "tcp" || o.mode != "self-loop" || missing == 0);
    std::cout << std::fixed << std::setprecision(6)
        << "{\"tool\":\"portbridge_bench\",\"protocol\":" << jsonString(o.protocol)
        << ",\"mode\":" << jsonString(o.mode) << ",\"environment\":" << jsonString(localhost ? "localhost" : "configured endpoints; hardware unverified")
        << ",\"physical_2_5g_validated\":false,\"host\":" << jsonString(effectiveHost) << ",\"bind\":" << jsonString(effectiveBind)
        << ",\"receiver_local_address\":" << jsonString(rxReady.local.address) << ",\"receiver_local_port\":" << rxReady.local.port
        << ",\"sender_local_address\":" << jsonString(txReady.local.address) << ",\"sender_local_port\":" << txReady.local.port
        << ",\"payload_bytes\":" << o.payload << ",\"requested_frames_per_second\":" << o.rate
        << ",\"requested_seconds\":" << o.duration << ",\"sending_seconds\":" << sendSeconds << ",\"elapsed_seconds_with_drain\":" << measuredSeconds
        << ",\"send_attempts\":" << attempts << ",\"admitted_frames\":" << admitted << ",\"send_rejections\":" << rejected
        << ",\"admission_retries\":" << admissionRetry << ",\"tx_completed_frames\":" << metrics.txFrames << ",\"tx_completed_bytes\":" << metrics.txBytes
        << ",\"tx_bytes_per_second\":" << metrics.txBytes / std::max(measuredSeconds, 0.000001)
        << ",\"tx_frames_per_second\":" << metrics.txFrames / std::max(measuredSeconds, 0.000001)
        << ",\"rx_bytes\":" << metrics.wireBytes << ",\"rx_datagrams\":" << metrics.datagrams << ",\"valid_frames\":" << metrics.valid
        << ",\"rx_unique_frames\":" << unique << ",\"checksum_or_format_failures\":" << metrics.invalid
        << ",\"invalid_bytes\":" << metrics.invalidBytes << ",\"tcp_incomplete_frame_bytes\":" << metrics.partialTcpBytes
        << ",\"rx_bytes_per_second\":" << metrics.wireBytes / std::max(measuredSeconds, 0.000001)
        << ",\"rx_frames_per_second\":" << metrics.valid / std::max(measuredSeconds, 0.000001)
        << ",\"expected_frames_known\":" << (expectedKnown ? "true" : "false") << ",\"expected_frames\":" << expected
        << ",\"missing_frames\":" << missing << ",\"missing_in_observed_sequence_range\":" << observedMissing
        << ",\"loss_fraction\":";
    if (expectedKnown && expected && !late) std::cout << static_cast<double>(missing) / expected; else std::cout << "null";
    std::cout << ",\"duplicate_frames\":" << duplicate << ",\"reordered_frames\":" << reordered
        << ",\"late_outside_observation_window\":" << late << ",\"sequence_window\":" << sequenceWindow
        << ",\"loss_count_exact\":" << ((expectedKnown && !late) ? "true" : "false")
        << ",\"application_dropped_frames\":0,\"kernel_or_nic_drops\":null,\"recording_enabled\":false,\"ui_enabled\":false"
        << ",\"peak_pending_send_bytes\":" << metrics.peakPending << ",\"pending_send_bytes_at_stop\":" << pendingAtEnd
        << ",\"actual_receive_buffer_bytes\":" << rxBuffer << ",\"actual_send_buffer_bytes\":" << txBuffer
        << ",\"transport_errors\":" << metrics.errors << ",\"teardown_errors\":" << metrics.teardownErrors << ",\"first_error\":" << jsonString(metrics.firstError)
        << ",\"receive_truncated_datagrams\":" << metrics.receiveTruncated
        << ",\"teardown_receive_truncated_datagrams\":" << metrics.teardownReceiveTruncated
        << ",\"drained\":" << (sendDrained ? "true" : "false") << ",\"success\":" << (consistent ? "true" : "false") << "}\n";
    return consistent ? 0 : 2;
}
}
int main(int argc, char** argv) {
    try { return run(parse(argc, argv)); }
    catch (const std::exception& e) { std::cout << "{\"tool\":\"portbridge_bench\",\"success\":false,\"error\":" << jsonString(e.what()) << "}\n"; return 1; }
}
