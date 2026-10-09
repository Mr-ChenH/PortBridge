#pragma once
#include "http_project_store.hpp"
#include "protocol_debug_session.hpp"
namespace portbridge {
// Pure request resolution for saved templates; no communication or UI mutation.
QJsonObject resolveHttpRequest(const QJsonObject &request, const HttpProjectStore &store,
                               QString *error);
} // namespace portbridge
