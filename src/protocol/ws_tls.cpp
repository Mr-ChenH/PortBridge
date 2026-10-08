#include "ws_client.hpp"
namespace portbridge::protocol {
std::shared_ptr<WebSocket> makeTlsWebSocket(net::io_context& io, std::shared_ptr<Mailbox> box, Request r) {
    return std::make_shared<Ws<TlsLayer>>(io, std::move(box), std::move(r));
}
}
