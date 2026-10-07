#pragma once
#include "session_private.hpp"
#include <memory>
namespace portbridge::detail {
struct RecorderSnapshot {
    bool active = false;
    std::uint64_t bytes = 0, records = 0, failures = 0;
    size_t queued = 0, highWater = 0;
    QString error;
};
class Recorder {
public:
    enum class Admission { Inactive, Accepted, Full, Invalid };
    Recorder();
    ~Recorder();
    bool start(const RecordingOptions&, QString* error);
    void stop();
    Admission enqueue(const DataRecord&);
    void markIncomplete(const QString&);
    RecorderSnapshot snapshot() const;
    std::vector<CaptureInfo> captures() const;
    void resetStatistics(bool isolate = false);
    void refresh(const QString& directory);
private:
    struct State;
    std::shared_ptr<State> state_;
};
}
