#pragma once
#include "portbridge/session_controller.hpp"
#include <QString>
// Internal callback seam: tests exercise the production generation gate without changing network buffers.
namespace portbridge {
struct detailSessionTestAccess {
    static std::uint64_t generation(const SessionController&);
    static void event(SessionController&, const TransportEvent&, std::uint64_t token);
    static bool data(SessionController&, const DataRecord&, std::uint64_t token);
};
}
namespace portbridge::detail {
QString validateConfig(const ConnectionConfig& config);
QString storageDirectory();
inline QString text(const std::string& value) { return QString::fromUtf8(value.data(), qsizetype(value.size())); }
inline std::string utf8(const QString& value) { const auto b = value.toUtf8(); return std::string(b.constData(), size_t(b.size())); }
std::uint64_t nowUs();
inline size_t retainedCost(const DataRecord& r) {
    return sizeof(DataRecord) + 128 + r.peer.address.capacity() + (r.payload ? r.payload->capacity() : 0);
}
}
