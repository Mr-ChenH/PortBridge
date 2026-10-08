#include "ws_client.hpp"
namespace portbridge::protocol {
std::shared_ptr<WebSocket> makePlainWebSocket(net::io_context& io, std::shared_ptr<Mailbox> box, Request r) {
    return std::make_shared<Ws<PlainLayer>>(io, std::move(box), std::move(r));
}
}
