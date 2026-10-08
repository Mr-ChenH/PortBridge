#pragma once
#include "protocol_internal.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/ssl.hpp>
#include <openssl/ssl.h>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

namespace portbridge::protocol {
namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace ssl = net::ssl;
using PlainLayer = beast::tcp_stream;
using TlsLayer = beast::ssl_stream<beast::tcp_stream>;
inline void trustWindowsRoots(ssl::context& context) {
#ifdef _WIN32
    HCERTSTORE roots = CertOpenSystemStoreW(0, L"ROOT");
    if (!roots) throw std::runtime_error("CA store unavailable");
    PCCERT_CONTEXT certificate = nullptr;
    while ((certificate = CertEnumCertificatesInStore(roots, certificate))) {
        const unsigned char* der = certificate->pbCertEncoded;
        X509* x509 = d2i_X509(nullptr, &der, certificate->cbCertEncoded);
        if (x509) { X509_STORE_add_cert(SSL_CTX_get_cert_store(context.native_handle()), x509); X509_free(x509); }
    }
    CertCloseStore(roots, 0);
#else
    context.set_default_verify_paths();
#endif
}
// No compression, one active write, one close, one complete-message handoff.
// A read is rearmed only once Qt acknowledges delivery: GUI stalls cannot turn
// async_read into an unbounded queued signal stream.
template<class Layer>
class Ws final : public WebSocket, public std::enable_shared_from_this<Ws<Layer>> {
    std::shared_ptr<Mailbox> box_;
    Request r_;
    ssl::context tls_{ssl::context::tls_client};
    websocket::stream<Layer, false> stream_;
    net::ip::tcp::resolver resolver_;
    net::steady_timer deadline_;
    beast::flat_buffer buffer_;
    websocket::response_type handshake_;
    QByteArray writing_;
    QString sendId_, closeId_;
    websocket::close_reason closeReason_;
    bool closed_ = false, connecting_ = true, writingActive_ = false, readingActive_ = false, closingActive_ = false;
    bool peerCloseValidated_ = false;
    bool completedClose(boost::system::error_code ec) const {
        if (!ec || ec == websocket::error::closed) return true;
        // Qt peers can reset TCP / omit TLS close_notify after a complete WS
        // closing handshake. Beast's teardown error then masks the close frame.
        // Accept only these transport errors, backed by a valid received close;
        // a socket failure without a close frame remains an explicit failure.
        if (ec != net::error::connection_reset && ec != net::error::connection_aborted &&
            ec != ssl::error::stream_truncated) return false;
        if (peerCloseValidated_) return true;
        // async_close can parse the reply itself without calling control_callback.
        // reason() is filled there before validation, so validate both fields.
        const auto& peer = stream_.reason();
        const int code = int(peer.code);
        const bool validCode = (code >= 1000 && code <= 1014 && code != 1004 && code != 1005 && code != 1006) || (code >= 3000 && code <= 4999);
        const QByteArray reason(peer.reason.data(), qsizetype(peer.reason.size()));
        return validCode && QString::fromUtf8(reason).toUtf8() == reason;
    }
    static auto makeStream(net::io_context& io, ssl::context& tls) {
        if constexpr (std::is_same_v<Layer, TlsLayer>) return websocket::stream<Layer, false>(io, tls);
        else return websocket::stream<Layer, false>(io);
    }
    auto self() { return this->shared_from_this(); }
    bool live() const { return !closed_ && box_->live(r_.token); }
    void deadline(int ms, const QString& stage) {
        deadline_.expires_after(std::chrono::milliseconds(ms));
        deadline_.async_wait([s = self(), stage](boost::system::error_code ec) { if (!ec && s->live()) s->fail("WebSocket " + stage + " timed out"); });
    }
    void transportClose() {
        resolver_.cancel(); deadline_.cancel();
        boost::system::error_code ignored;
        auto& socket = beast::get_lowest_layer(stream_).socket();
        socket.cancel(ignored); socket.shutdown(net::ip::tcp::socket::shutdown_both, ignored); socket.close(ignored);
    }
    void release() { if (!readingActive_) buffer_.consume(buffer_.size()); r_.headers.clear(); r_.headers.shrink_to_fit(); r_.body = {}; r_.url.clear(); r_.caFile.clear(); handshake_ = {}; }
    void fail(QString error) {
        if (closed_) return;
        closed_ = true; transportClose();
        if (connecting_) { connecting_ = false; finished(box_, r_, r_.id, {}, error); }
        if (!sendId_.isEmpty()) { finished(box_, r_, sendId_, {}, error); sendId_.clear(); }
        if (!closeId_.isEmpty()) { finished(box_, r_, closeId_, {}, error); closeId_.clear(); }
        box_->push({Event::Error, r_.token, {}, error, {}, {}, false, 256});
        state(box_, r_, false); release();
        // An outstanding async_write still references writing_. Its cancellation
        // callback releases that buffer; never invalidate it early.
        if (!writingActive_) writing_ = {};
    }
    void upgrade() {
        if (!live()) return;
        beast::get_lowest_layer(stream_).expires_never();
        stream_.set_option(websocket::stream_base::timeout{
            std::chrono::milliseconds(r_.timeout), std::chrono::seconds(30), true});
        stream_.read_message_max(std::size_t(r_.limit));
        stream_.control_callback([this](websocket::frame_type type, beast::string_view) {
            // Beast invokes this only after validating code and UTF-8 payload.
            if (type == websocket::frame_type::close) peerCloseValidated_ = true;
        });
        stream_.write_buffer_bytes(16 * 1024);
        stream_.auto_fragment(true);
        stream_.set_option(websocket::stream_base::decorator([headers = r_.headers](websocket::request_type& request) {
            for (const auto& pair : headers) request.set(pair.first, pair.second);
        }));
        stream_.async_handshake(handshake_, r_.authority, r_.target, [s = self()](boost::system::error_code ec) {
            if (!s->live()) return;
            if (ec) { s->fail(QString("WebSocket handshake failed (%1)").arg(ec.value())); return; }
            std::string requested;
            for (const auto& pair : s->r_.headers) if (pair.first == "Sec-WebSocket-Protocol") requested = pair.second;
            const auto selected = s->handshake_[beast::http::field::sec_websocket_protocol];
            if ((!requested.empty() && selected != requested) || (requested.empty() && !selected.empty())) { s->fail("WebSocket subprotocol mismatch"); return; }
            s->deadline_.cancel(); s->connecting_ = false;
            finished(s->box_, s->r_, s->r_.id, {{"connected", true}, {"subprotocol", QString::fromUtf8(selected.data(), qsizetype(selected.size()))}});
            state(s->box_, s->r_, true);
            s->handshake_ = {}; s->r_.headers.clear();
            // Decorator retains request credentials unless explicitly replaced.
            s->stream_.set_option(websocket::stream_base::decorator([](websocket::request_type&) {}));
            s->read();
        });
    }
    void tlsHandshake() {
        if constexpr (std::is_same_v<Layer, TlsLayer>) {
            if (!SSL_set_tlsext_host_name(stream_.next_layer().native_handle(), r_.host.c_str())) { fail("WebSocket TLS hostname setup failed"); return; }
            stream_.next_layer().set_verify_mode(ssl::verify_peer);
            stream_.next_layer().set_verify_callback(ssl::host_name_verification(r_.host));
            stream_.next_layer().async_handshake(ssl::stream_base::client, [s = self()](boost::system::error_code ec) {
                if (!s->live()) return;
                if (ec) s->fail(QString("WebSocket TLS verification/handshake failed (%1)").arg(ec.value())); else s->upgrade();
            });
        } else upgrade();
    }
    void read() {
        if (!live() || closingActive_ || readingActive_) return;
        readingActive_ = true;
        stream_.async_read(buffer_, [s = self()](boost::system::error_code ec, std::size_t) {
            s->readingActive_ = false;
            if (!s->live()) { s->buffer_.consume(s->buffer_.size()); return; }
            if (ec) {
                if (!s->closeId_.isEmpty() && s->completedClose(ec)) return;
                if (s->completedClose(ec)) {
                    s->closed_ = true; s->transportClose();
                    const auto& peer = s->stream_.reason();
                    protocol::closed(s->box_, s->r_, int(peer.code), QString::fromUtf8(peer.reason.data(), qsizetype(peer.reason.size())), true);
                    if (!s->sendId_.isEmpty()) { finished(s->box_, s->r_, s->sendId_, {}, "WebSocket peer closed during send"); s->sendId_.clear(); }
                    state(s->box_, s->r_, false); s->release();
                } else s->fail(QString("WebSocket receive/protocol error (%1)").arg(ec.value()));
                return;
            }
            const auto data = beast::buffers_to_string(s->buffer_.data());
            Event event{Event::Message, s->r_.token, {}, {}, {}, QByteArray(data.data(), qsizetype(data.size())), s->stream_.got_binary(), qsizetype(data.size()) + 256};
            s->buffer_.consume(s->buffer_.size());
            std::weak_ptr<Ws> weak = s;
            event.acknowledge = [weak] { net::post(Runtime::instance().io(), [weak] { if (const auto next = weak.lock(); next && next->live()) next->read(); }); };
            if (!s->box_->push(std::move(event))) s->fail("WebSocket delivery budget exceeded");
        });
    }
    void beginClose() {
        if (!live() || closingActive_ || writingActive_) return;
        closingActive_ = true; deadline(5000, "close");
        stream_.async_close(closeReason_, [s = self()](boost::system::error_code ec) {
            if (!s->live()) return;
            if (!s->completedClose(ec)) { s->fail(QString("WebSocket close failed (%1)").arg(ec.value())); return; }
            s->closed_ = true; s->transportClose();
            // Complete the intended close before publishing disconnected state.
            finished(s->box_, s->r_, s->closeId_, {{"closed", true}, {"code", int(s->closeReason_.code)}, {"reason", QString::fromUtf8(s->closeReason_.reason.data(), qsizetype(s->closeReason_.reason.size()))},
                {"peerCode", int(s->stream_.reason().code)}, {"peerReason", QString::fromUtf8(s->stream_.reason().reason.data(), qsizetype(s->stream_.reason().reason.size()))}});
            s->closeId_.clear();
            const auto& peer = s->stream_.reason();
            protocol::closed(s->box_, s->r_, int(peer.code), QString::fromUtf8(peer.reason.data(), qsizetype(peer.reason.size())), false);
            state(s->box_, s->r_, false); s->release();
        });
    }
public:
    Ws(net::io_context& io, std::shared_ptr<Mailbox> box, Request r)
        : box_(std::move(box)), r_(std::move(r)), stream_(makeStream(io, tls_)), resolver_(io), deadline_(io), buffer_(std::size_t(r_.limit)) {
        if constexpr (std::is_same_v<Layer, TlsLayer>) {
            tls_.set_options(ssl::context::default_workarounds | ssl::context::no_sslv2 | ssl::context::no_sslv3);
            SSL_CTX_set_min_proto_version(tls_.native_handle(), TLS1_2_VERSION);
            if (!r_.caFile.empty()) tls_.load_verify_file(r_.caFile); else trustWindowsRoots(tls_);
        }
    }
    void start() override {
        deadline(r_.timeout, "connect/handshake");
        resolver_.async_resolve(r_.host, r_.port, [s = self()](boost::system::error_code ec, net::ip::tcp::resolver::results_type results) {
            if (!s->live()) return;
            if (ec) { s->fail(QString("WebSocket name resolution failed (%1)").arg(ec.value())); return; }
            std::vector<net::ip::tcp::endpoint> endpoints;
            for (const auto& result : results) { if (endpoints.size() >= 64) break; endpoints.push_back(result.endpoint()); }
            // Windows can silently drop a localhost IPv6 attempt when the peer
            // listens only on IPv4. Prefer IPv4 while retaining IPv6 fallback.
            std::stable_sort(endpoints.begin(), endpoints.end(), [](const auto& a, const auto& b) { return a.address().is_v4() && b.address().is_v6(); });
            beast::get_lowest_layer(s->stream_).expires_after(std::chrono::milliseconds(s->r_.connectTimeout));
            beast::get_lowest_layer(s->stream_).async_connect(endpoints, [s](boost::system::error_code error, net::ip::tcp::endpoint) {
                if (!s->live()) return;
                if (error) s->fail(QString("WebSocket connect failed (%1:%2)").arg(QString::fromLatin1(error.category().name())).arg(error.value())); else s->tlsHandshake();
            });
        });
    }
    void send(QString id, QByteArray bytes, bool binary) override {
        if (!live() || connecting_ || writingActive_ || !closeId_.isEmpty() || bytes.size() > r_.limit) { finished(box_, r_, id, {}, "WebSocket send unavailable or message exceeds limit"); return; }
        writingActive_ = true; sendId_ = std::move(id); writing_ = std::move(bytes); stream_.binary(binary); deadline(r_.timeout, "send");
        stream_.async_write(net::buffer(writing_.constData(), std::size_t(writing_.size())), [s = self()](boost::system::error_code ec, std::size_t size) {
            s->writingActive_ = false; s->writing_ = {};
            if (!s->live()) return;
            if (ec) { s->fail(QString("WebSocket send failed (%1)").arg(ec.value())); return; }
            s->deadline_.cancel(); finished(s->box_, s->r_, s->sendId_, {{"sentBytes", double(size)}, {"completed", true}}); s->sendId_.clear();
            if (!s->closeId_.isEmpty()) s->beginClose();
        });
    }
    void close(QString id, int code, QString reason) override {
        if (!live() || connecting_ || !closeId_.isEmpty()) { finished(box_, r_, id, {}, "WebSocket close unavailable"); return; }
        closeId_ = std::move(id); closeReason_.code = static_cast<websocket::close_code>(code); closeReason_.reason = reason.toUtf8().toStdString(); beginClose();
    }
    void cancel() override { if (closed_) return; closed_ = true; transportClose(); release(); if (!writingActive_) writing_ = {}; }
};
}
