#pragma once
#include "portbridge/types.hpp"
#include <QAbstractTableModel>
#include <QSortFilterProxyModel>
#include <QStringDecoder>
#include <deque>
#include <map>
#include <optional>

namespace portbridge {
QString endpointText(const Endpoint& endpoint);
QByteArray recordBytes(const DataRecord& record);
QString hexBytes(const QByteArray& bytes);
QString recordDirection(Direction direction);

// Raw samples and their cached previews share a byte budget. A long record is
// omitted as a whole; no truncated payload is ever exported as complete.
class RecordModel final : public QAbstractTableModel {
public:
    explicit RecordModel(QObject* parent = nullptr, std::size_t byteLimit = 10 * 1024 * 1024, int rowLimit = 50000);
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    void append(const std::vector<DataRecord>& records, bool sampled);
    void clear();
    void setText(bool enabled);
    const DataRecord* record(int row) const;
    QString textPreview(int row) const;
    struct TextSegment { Direction direction; QString text; };
    QString takeStreamText();
    std::deque<TextSegment> takeStreamSegments();
    void setStreamFormat(int format);
    void resetDecoders();
    std::uint64_t omitted() const { return omitted_; }
    std::size_t retainedBytes() const { return bytes_; }
    std::size_t byteLimit() const { return byteLimit_; }
private:
    struct Row { DataRecord record; QString text; std::size_t cost = 0; };
    std::deque<Row> rows_;
    using DecoderKey = std::tuple<std::uint64_t, int, std::string, std::uint16_t>;
    std::map<DecoderKey, std::unique_ptr<QStringDecoder>> decoders_;
    std::optional<DecoderKey> lastStreamKey_;
    std::size_t byteLimit_, bytes_ = 0;
    int rowLimit_;
    std::uint64_t omitted_ = 0;
    bool text_ = false;
    int streamFormat_ = 0;
    std::deque<TextSegment> stream_;
    qsizetype streamCharacters_ = 0;
    bool streamClipped_ = false;
    QString decodeRecord(const DataRecord&, bool sampled);
    void addSegment(Direction direction, const QString& text);
};
class RecordFilter final : public QSortFilterProxyModel {
public:
    explicit RecordFilter(QObject* parent = nullptr);
    void setQuery(const QString& query, int mode);
protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;
private:
    QString query_;
    QByteArray hexQuery_;
    int mode_ = 0;
    bool valid_ = true;
};
}
