#include "protocol_internal.hpp"
#include <cpr/cpr.h>
#include <curl/curl.h>
#include <QJsonArray>
#include <chrono>

namespace portbridge::protocol {
namespace {
class Http final : public HttpOperation, public std::enable_shared_from_this<Http> {
    std::shared_ptr<Mailbox> box_;
    Request r_;
    net::steady_timer timer_;
    std::unique_ptr<cpr::Session> session_;
    CURLM* multi_ = nullptr;
    CURL* easy_ = nullptr;
    QByteArray body_;
    QJsonObject headers_;
    qsizetype headerBytes_ = 0;
    int headerCount_ = 0;
    bool capped_ = false, done_ = false;
    std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now();
public:
    Http(net::io_context& io, std::shared_ptr<Mailbox> box, Request r) : box_(std::move(box)), r_(std::move(r)), timer_(io) {}
    ~Http() override { cleanup(); }
    void start() {
        try {
            session_ = std::make_unique<cpr::Session>();
            session_->SetUrl(cpr::Url{r_.url});
            session_->SetTimeout(cpr::Timeout{r_.timeout});
            session_->SetConnectTimeout(cpr::ConnectTimeout{r_.connectTimeout});
            session_->SetRedirect(cpr::Redirect{false});
            session_->SetVerifySsl(cpr::VerifySsl{true});
            cpr::Header headers;
            for (const auto& pair : r_.headers) headers[pair.first] = pair.second;
            session_->SetHeader(headers);
            if (!r_.body.isEmpty()) session_->SetBody(cpr::Body{r_.body.toStdString()});
            session_->SetWriteCallback(cpr::WriteCallback{[this](std::string_view data, intptr_t) {
                if (!box_->live(r_.token)) return false;
                if (qsizetype(data.size()) > r_.limit - body_.size()) { capped_ = true; return false; }
                body_.append(data.data(), qsizetype(data.size())); return true;
            }});
            session_->SetHeaderCallback(cpr::HeaderCallback{[this](std::string_view data, intptr_t) {
                if (!box_->live(r_.token)) return false;
                if (qsizetype(data.size()) > HeaderLimit - headerBytes_ || ++headerCount_ > 128) { capped_ = true; return false; }
                headerBytes_ += qsizetype(data.size());
                QByteArray line(data.data(), qsizetype(data.size()));
                if (line.startsWith("HTTP/")) headers_ = {};
                const auto colon = line.indexOf(':');
                if (colon > 0) {
                    const auto key = QString::fromLatin1(line.left(colon).trimmed()).toLower();
                    const auto value = QString::fromLatin1(line.mid(colon + 1).trimmed());
                    if (headers_.contains(key)) headers_[key] = headers_[key].toString() + ", " + value;
                    else headers_[key] = value;
                }
                return true;
            }});
            if (r_.method == "POST") session_->PreparePost();
            else if (r_.method == "PUT") session_->PreparePut();
            else if (r_.method == "PATCH") session_->PreparePatch();
            else if (r_.method == "DELETE") session_->PrepareDelete();
            else if (r_.method == "HEAD") session_->PrepareHead();
            else if (r_.method == "OPTIONS") session_->PrepareOptions();
            else session_->PrepareGet();
            easy_ = session_->GetCurlHolder()->handle;
            curl_easy_setopt(easy_, CURLOPT_PROTOCOLS_STR, "http,https");
            curl_easy_setopt(easy_, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
            curl_easy_setopt(easy_, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(easy_, CURLOPT_PROXY, "");
            curl_easy_setopt(easy_, CURLOPT_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(easy_, CURLOPT_SSL_VERIFYHOST, 2L);
            curl_easy_setopt(easy_, CURLOPT_SSLVERSION, long(CURL_SSLVERSION_TLSv1_2));
            if (!r_.caFile.empty()) curl_easy_setopt(easy_, CURLOPT_CAINFO, r_.caFile.c_str());
#ifdef _WIN32
            else curl_easy_setopt(easy_, CURLOPT_SSL_OPTIONS, long(CURLSSLOPT_NATIVE_CA));
#endif
            multi_ = curl_multi_init();
            if (!multi_ || curl_multi_add_handle(multi_, easy_) != CURLM_OK) { complete(CURLE_FAILED_INIT); return; }
            poll();
        } catch (...) { complete(CURLE_FAILED_INIT); }
    }
    void cancel() override {
        if (done_) return;
        done_ = true; timer_.cancel(); cleanup(); release();
    }
private:
    void cleanup() {
        if (multi_) { if (easy_) curl_multi_remove_handle(multi_, easy_); curl_multi_cleanup(multi_); multi_ = nullptr; }
        easy_ = nullptr; session_.reset();
    }
    void release() { body_ = {}; headers_ = {}; r_.body = {}; r_.headers.clear(); r_.headers.shrink_to_fit(); r_.url.clear(); r_.caFile.clear(); }
    void poll() {
        if (done_) return;
        if (!box_->live(r_.token)) { cancel(); return; }
        if (std::chrono::steady_clock::now() - started_ >= std::chrono::milliseconds(r_.timeout)) { complete(CURLE_OPERATION_TIMEDOUT); return; }
        int running = 0;
        if (curl_multi_perform(multi_, &running) != CURLM_OK) { complete(CURLE_RECV_ERROR); return; }
        int pending = 0;
        while (auto* message = curl_multi_info_read(multi_, &pending)) {
            if (message->msg == CURLMSG_DONE) { complete(message->data.result); return; }
        }
        timer_.expires_after(std::chrono::milliseconds(5));
        timer_.async_wait([self = shared_from_this()](boost::system::error_code ec) { if (!ec) self->poll(); });
    }
    void complete(CURLcode code) {
        if (done_) return;
        done_ = true; timer_.cancel();
        long status = 0;
        if (easy_) curl_easy_getinfo(easy_, CURLINFO_RESPONSE_CODE, &status);
        // cpr owns the configured easy handle; multi supplies the real completion.
        // Call Complete for its documented finalization, without using its empty
        // default response buffers (our capped callbacks own accumulation).
        if (session_) session_->Complete(code);
        cleanup();
        QJsonObject result;
        QString error;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_).count();
        if (capped_) error = "HTTP response exceeded body or header limit";
        else if (code == CURLE_OPERATION_TIMEDOUT) error = "HTTP request timed out";
        else if (code != CURLE_OK) error = QString("HTTP network/TLS error (%1)").arg(int(code));
        else {
            const auto text = QString::fromUtf8(body_);
            QJsonValue body = text;
            if (body_.size() <= 64 * 1024) {
                QJsonParseError parseError;
                const auto doc = QJsonDocument::fromJson(body_, &parseError);
                if (parseError.error == QJsonParseError::NoError) body = doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array());
            }
            result = {{"status", int(status)}, {"headers", headers_}, {"body", body}, {"bodyText", text},
                      {"bodyBase64", QString::fromLatin1(body_.toBase64())}, {"bodyBytes", double(body_.size())}, {"elapsedMs", double(elapsed)}};
        }
        finished(box_, r_, r_.id, result, error, body_.size() * 7 + headerBytes_ * 4 + 1024 * 1024);
        release();
    }
};
}
std::shared_ptr<HttpOperation> startHttp(net::io_context& io, std::shared_ptr<Mailbox> box, Request r) {
    auto op = std::make_shared<Http>(io, std::move(box), std::move(r)); op->start(); return op;
}
}
