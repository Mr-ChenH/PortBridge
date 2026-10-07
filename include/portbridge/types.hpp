#pragma once
#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace portbridge {
enum class TransportKind { Serial, TcpClient, TcpServer, Udp };
enum class Direction { Receive, Transmit, System };
using Bytes = std::vector<std::uint8_t>;
using SharedBytes = std::shared_ptr<const Bytes>;
struct Endpoint { std::string address; std::uint16_t port = 0; };
struct ConnectionConfig {
    std::string name = "UDP";
    TransportKind kind = TransportKind::Udp;
    std::string localAddress = "0.0.0.0";
    std::uint16_t localPort = 9000;
    std::string remoteAddress = "127.0.0.1";
    std::uint16_t remotePort = 9001;
    std::string serialPort;
    std::int32_t baudRate = 115200;
    int dataBits = 8;
    std::string parity = "None";
    std::string stopBits = "1";
    std::string flowControl = "None";
    std::size_t receiveBufferBytes = 64 * 1024 * 1024;
    std::size_t sendBufferBytes = 8 * 1024 * 1024;
    std::size_t sendQueueBytes = 8 * 1024 * 1024;
    int connectTimeoutMs = 5000;
    bool sequenceAnalysis = false;
    std::size_t sequenceOffset = 0;
    bool sequenceBigEndian = true;
    std::size_t sequenceWindow = 1024;
};
struct DataRecord {
    std::uint64_t sequence = 0;
    std::uint64_t timestampUs = 0;
    std::uint64_t connectionId = 0;
    Endpoint peer;
    TransportKind transport = TransportKind::Udp;
    Direction direction = Direction::Receive;
    SharedBytes payload;
};
enum class EventKind { Connecting, Connected, Bound, Listening, Disconnected, ClientAdded, ClientRemoved, Error, SendRejected, ReceiveTruncated, UdpTargetReady };
struct TransportEvent {
    EventKind kind = EventKind::Disconnected;
    std::string message;
    std::uint64_t connectionId = 0;
    Endpoint peer;
    Endpoint local;
};
struct ClientInfo { std::uint64_t id = 0; Endpoint peer; };
struct RecordingOptions {
    std::string directory;
    std::uint64_t rotateBytes = 1024ull * 1024 * 1024;
    std::uint32_t durationSeconds = 0;
    std::size_t queueBytes = 128 * 1024 * 1024;
};
struct CaptureInfo {
    std::string path;
    std::uint64_t startedUs = 0;
    std::uint64_t durationUs = 0;
    std::uint64_t bytes = 0;
    std::uint64_t records = 0;
    bool complete = true;
    std::string error;
    std::string transportSummary; // SERIAL / TCP CLIENT / TCP SERVER / UDP / MIXED; empty means unknown.
    bool metadataAvailable = false; // A valid bounded metadata sidecar is available.
};
struct Statistics {
    std::uint64_t rxBytes = 0, txBytes = 0, rxDatagrams = 0;
    std::uint64_t applicationDroppedRecords = 0, applicationDroppedBytes = 0;
    std::uint64_t displayOmitted = 0;
    std::uint64_t recordedBytes = 0, recordedRecords = 0, recordingFailures = 0;
    std::uint64_t sequenceMissing = 0, sequenceDuplicates = 0, sequenceReordered = 0;
    bool sequenceEnabled = false;
    std::size_t sampleQueueBytes = 0, recordingQueueBytes = 0, recordingQueueHighWater = 0, pendingSendBytes = 0;
    std::size_t actualReceiveBufferBytes = 0, actualSendBufferBytes = 0;
    double rxBytesPerSecond = 0, txBytesPerSecond = 0, datagramsPerSecond = 0;
    std::uint64_t sequenceExpected = 0; // Finalized present + missing positions; pending/duplicate observations excluded.
    std::uint64_t receiveTruncatedDatagrams = 0;
};
}
