#pragma once
#include "portbridge/types.hpp"
#include <asio/error.hpp>
#include <asio/error_code.hpp>
#include <cstddef>

namespace portbridge::network_detail {
// Shared by the real async receive completion and its regression test. The
// scratch buffer remains full-sized; an error completion never admits its
// partial bytes as a complete datagram. Callers apply generation/liveness gates.
template<class FormatError, class EmitEvent, class Deliver, class Resume, class Fail>
void completeUdpReceive(const asio::error_code& ec, std::size_t bytes,
                        const Endpoint& source, const Endpoint& local,
                        const Endpoint& notificationPeer, FormatError formatError,
                        EmitEvent emitEvent, Deliver deliver, Resume resume, Fail fail) {
    if (ec) {
        if (ec == asio::error::message_size) {
            emitEvent(TransportEvent{EventKind::ReceiveTruncated,
                formatError("UDP receive truncated datagram", ec), 0, source, local});
        } else if (ec == asio::error::connection_refused || ec == asio::error::connection_reset ||
                   ec == asio::error::host_unreachable || ec == asio::error::network_unreachable ||
                   ec == asio::error::network_reset) {
            // Winsock can deliver asynchronous ICMP notifications through a
            // pending receive while the local binding remains usable.
            emitEvent(TransportEvent{EventKind::Error,
                formatError("UDP peer notification (socket remains bound)", ec), 0,
                notificationPeer, local});
        } else {
            fail(formatError("UDP receive", ec));
            return;
        }
    } else {
        deliver(bytes, source);
    }
    resume();
}
}
