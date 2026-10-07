#pragma once
#include "portbridge/types.hpp"
#include <functional>
#include <memory>
#include <optional>

namespace portbridge {
// All callbacks run on the dedicated I/O thread. Never access QWidget from them.
// onData false: TCP retains/retries the same block without reading further;
// UDP discards that datagram and continues. The consumer owns drop accounting.
class NetworkEngine {
public:
    struct Callbacks {
        std::function<bool(const DataRecord&)> onData;
        std::function<void(const TransportEvent&)> onEvent;
    };
    explicit NetworkEngine(Callbacks callbacks);
    ~NetworkEngine();
    NetworkEngine(const NetworkEngine&) = delete;
    NetworkEngine& operator=(const NetworkEngine&) = delete;
    void start(const ConnectionConfig& config);
    void stop();
    // Admission to a bounded queue, not proof of peer receipt. TX callback after write.
    // An explicit UDP target belongs to this datagram, including while queued.
    bool send(SharedBytes bytes, std::uint64_t clientId = 0, bool broadcast = false,
              std::optional<Endpoint> udpTarget = std::nullopt);
    // Changes only the UDP destination; rejects while resolving or writes are pending.
    bool setUdpTarget(const Endpoint& target);
    bool udpTargetReady() const;
    void disconnectClient(std::uint64_t clientId);
    std::size_t pendingSendBytes() const;
    std::size_t actualReceiveBufferBytes() const;
    std::size_t actualSendBufferBytes() const;
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
}
