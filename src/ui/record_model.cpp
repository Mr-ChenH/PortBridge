#include "ui/record_model.hpp"
#include "portbridge/session_controller.hpp"
#include <QColor>
#include <QDateTime>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonParseError>
#include <algorithm>

namespace portbridge {
QString endpointText(const Endpoint& e) { return QString::fromUtf8(e.address.data(), qsizetype(e.address.size())) + ':' + QString::number(e.port); }
QByteArray recordBytes(const DataRecord& r) { return r.payload ? QByteArray(reinterpret_cast<const char*>(r.payload->data()), qsizetype(r.payload->size())) : QByteArray(); }
QString hexBytes(const QByteArray& bytes) { return QString::fromLatin1(bytes.toHex(' ').toUpper()); }
QString recordDirection(Direction d) { return d == Direction::Receive ? QStringLiteral("RX") : d == Direction::Transmit ? QStringLiteral("TX") : QStringLiteral("SYSTEM"); }
RecordModel::RecordModel(QObject* p, std::size_t limit, int rows) : QAbstractTableModel(p), byteLimit_(limit), rowLimit_(std::max(1, rows)) {}
int RecordModel::rowCount(const QModelIndex& p) const { return p.isValid() ? 0 : int(rows_.size()); }
int RecordModel::columnCount(const QModelIndex& p) const { return p.isValid() ? 0 : 6; }
QVariant RecordModel::headerData(int s, Qt::Orientation o, int role) const {
    if (role != Qt::DisplayRole || o != Qt::Horizontal) return {};
    static const QStringList names = {QStringLiteral("#"), QStringLiteral("软件时间"), QStringLiteral("方向"), QStringLiteral("来源 / 端点"), QStringLiteral("字节"), QStringLiteral("数据预览")};
    return s >= 0 && s < names.size() ? names[s] : QVariant();
}
QVariant RecordModel::data(const QModelIndex& i, int role) const {
    if (!i.isValid() || i.row() >= rowCount()) return {};
    const auto& row = rows_[std::size_t(i.row())]; const auto& r = row.record;
    if (role == Qt::FontRole) { QFont f(QStringLiteral("Consolas"),9); f.setFamilies({QStringLiteral("Consolas"),QStringLiteral("Microsoft YaHei UI")}); return f; }
    if (role == Qt::TextAlignmentRole && (i.column() == 0 || i.column() == 4)) return int(Qt::AlignRight | Qt::AlignVCenter);
    if (role == Qt::ForegroundRole && i.column() == 2) return QColor(r.direction == Direction::Receive ? "#116B35" : r.direction == Direction::Transmit ? "#9A4300" : "#c19758");
    if (role == Qt::ToolTipRole) return QStringLiteral("%1 · ID %2 · %3\n%4\n%5 B；%6。预览有限，复制和导出使用保留的完整字节。")
        .arg(recordDirection(r.direction)).arg(r.connectionId).arg(endpointText(r.peer)).arg(QDateTime::fromMSecsSinceEpoch(qint64(r.timestampUs / 1000)).toString(Qt::ISODateWithMs)).arg(r.payload ? r.payload->size() : 0).arg(r.transport==TransportKind::Udp?QStringLiteral("UDP 数据报"):QStringLiteral("流式读取块"));
    if (role != Qt::DisplayRole) return {};
    switch (i.column()) {
    case 0: return qulonglong(r.sequence);
    case 1: return QDateTime::fromMSecsSinceEpoch(qint64(r.timestampUs / 1000)).toString("HH:mm:ss.zzz");
    case 2: return recordDirection(r.direction);
    case 3: return r.transport==TransportKind::Udp?endpointText(r.peer):endpointText(r.peer) + QStringLiteral(" / #%1").arg(r.connectionId);
    case 4: return qulonglong(r.payload ? r.payload->size() : 0);
    case 5: {
        const auto len = r.payload ? r.payload->size() : 0;
        if (text_) return row.text + (len > 256 ? QStringLiteral(" …") : QString());
        const auto n = std::min<std::size_t>(64, len);
        return hexBytes(r.payload ? QByteArray(reinterpret_cast<const char*>(r.payload->data()), qsizetype(n)) : QByteArray()) + (len > n ? QStringLiteral(" …") : QString());
    }
    default: return {};
    }
}
void RecordModel::addSegment(Direction direction,const QString& text) {
    if(text.isEmpty())return;
    QString bounded=text;if(bounded.size()>1024*1024)bounded=QStringLiteral("[文本显示已裁剪]\n")+bounded.right(1024*1024-32);
    while(!stream_.empty()&&(streamCharacters_+bounded.size()>1024*1024||stream_.size()>=10000)){streamClipped_=true;streamCharacters_-=stream_.front().text.size();stream_.pop_front();}
    streamCharacters_+=bounded.size();stream_.push_back({direction,std::move(bounded)});
}
QString RecordModel::decodeRecord(const DataRecord& r,bool sampled) {
    const auto raw=recordBytes(r);QString full;
    if(sampled||r.direction==Direction::System)return QString::fromUtf8(raw.left(256));
    if(r.transport==TransportKind::Udp)full=QString::fromUtf8(raw);
    else {
        DecoderKey key{r.connectionId,int(r.direction),r.peer.address,r.peer.port};
        if(decoders_.size()>=128&&decoders_.find(key)==decoders_.end()){resetDecoders();addSegment(Direction::System,QStringLiteral("\n[解码器数量达到上限；状态已重置]\n"));}
        auto& decoder=decoders_[key];if(!decoder)decoder=std::make_unique<QStringDecoder>(QStringDecoder::Utf8);full=(*decoder)(raw);
    }
    const auto preview=full.left(r.transport==TransportKind::Udp?256:512);
    if(!sampled&&r.direction!=Direction::System){
        QString content=full;
        if(streamFormat_==1){content.clear();for(qsizetype i=0;i<raw.size();i+=16)content+=hexBytes(raw.mid(i,16))+'\n';}
        else if(streamFormat_==2){QJsonParseError error;const auto json=QJsonDocument::fromJson(raw,&error);if(error.error==QJsonParseError::NoError&&!json.isNull())content=QString::fromUtf8(json.toJson(QJsonDocument::Indented));}
        if(!content.isEmpty()||raw.isEmpty())addSegment(r.direction,QStringLiteral("\n[%1  %2 · %3 · %4 B]\n").arg(QDateTime::fromMSecsSinceEpoch(qint64(r.timestampUs/1000)).toString("hh:mm:ss.zzz"),r.direction==Direction::Receive?QStringLiteral("▼ RX 接收"):QStringLiteral("▲ TX 发送"),endpointText(r.peer)).arg(raw.size())+content+'\n');
    }
    return preview;
}
void RecordModel::append(const std::vector<DataRecord>& records, bool sampled) {
    if (records.empty()) return;
    std::deque<Row> incoming;
    std::size_t incomingBytes = 0;
    for (const auto& r : records) {
        // Account retained allocation and preview storage, not only payload length.
        const auto cost = (r.payload ? r.payload->capacity() : 0) + 2048 + r.peer.address.capacity();
        if (cost > byteLimit_) { ++omitted_; resetDecoders(); continue; }
        const QString text = decodeRecord(r,sampled);
        incoming.push_back({r, text, cost}); incomingBytes += cost;
        while (incomingBytes > byteLimit_ || incoming.size() > std::size_t(rowLimit_)) {
            incomingBytes -= incoming.front().cost; incoming.pop_front(); ++omitted_;
        }
    }
    std::size_t removed = 0;
    while (!rows_.empty() && (bytes_ + incomingBytes > byteLimit_ || rows_.size() + incoming.size() > std::size_t(rowLimit_))) {
        bytes_ -= rows_[removed].cost; ++removed;
        // rows are removed together below, so compare the remaining size.
        if (removed == rows_.size() || (bytes_ + incomingBytes <= byteLimit_ && rows_.size() - removed + incoming.size() <= std::size_t(rowLimit_))) break;
    }
    if (removed) {
        beginRemoveRows({}, 0, int(removed) - 1);
        for (std::size_t n = 0; n < removed; ++n) rows_.pop_front();
        endRemoveRows(); omitted_ += removed;
    }
    if (!incoming.empty()) {
        beginInsertRows({}, rowCount(), rowCount() + int(incoming.size()) - 1);
        for (auto& row : incoming) rows_.push_back(std::move(row));
        bytes_ += incomingBytes; endInsertRows();
    }
}
void RecordModel::clear() { beginResetModel(); rows_.clear(); bytes_ = 0; omitted_ = 0; stream_.clear(); streamCharacters_=0; streamClipped_=false; resetDecoders(); endResetModel(); }
void RecordModel::setText(bool e) { if (text_ == e) return; text_ = e; if (rowCount()) emit dataChanged(index(0,5), index(rowCount()-1,5)); }
const DataRecord* RecordModel::record(int row) const { return row >= 0 && row < rowCount() ? &rows_[std::size_t(row)].record : nullptr; }
QString RecordModel::textPreview(int row) const { return row >= 0 && row < rowCount() ? rows_[std::size_t(row)].text : QString(); }
QString RecordModel::takeStreamText() {QString out;for(const auto& segment:takeStreamSegments())out+=segment.text;return out;}
std::deque<RecordModel::TextSegment> RecordModel::takeStreamSegments(){std::deque<TextSegment> out;out.swap(stream_);streamCharacters_=0;if(streamClipped_)out.push_front({Direction::System,QStringLiteral("[文本显示已裁剪；完整保留样本可另行导出]\n")});streamClipped_=false;return out;}
void RecordModel::setStreamFormat(int format){streamFormat_=std::clamp(format,0,2);stream_.clear();streamCharacters_=0;streamClipped_=false;resetDecoders();for(const auto& row:rows_)decodeRecord(row.record,false);}
void RecordModel::resetDecoders() { decoders_.clear(); lastStreamKey_.reset(); }
RecordFilter::RecordFilter(QObject* p) : QSortFilterProxyModel(p) { setDynamicSortFilter(true); }
void RecordFilter::setQuery(const QString& q, int mode) {
    query_ = q; mode_ = mode; valid_ = true; hexQuery_.clear();
    if (mode == 1 && !q.isEmpty()) { QString error; hexQuery_ = encodePayload(q, true, QStringLiteral("UTF-8"), QStringLiteral("none"), &error); valid_ = error.isEmpty(); }
    invalidateFilter();
}
bool RecordFilter::filterAcceptsRow(int row, const QModelIndex&) const {
    if (query_.isEmpty()) return true;
    auto* model = static_cast<RecordModel*>(sourceModel()); const auto* r = model->record(row);
    if (!r || !valid_) return false;
    if (mode_ == 0) return (endpointText(r->peer) + QStringLiteral(" / #%1").arg(r->connectionId)).contains(query_, Qt::CaseInsensitive);
    if (mode_ == 1) return recordBytes(*r).contains(hexQuery_);
    // Search the entire retained raw block, plus decoded crossing-boundary preview.
    return QString::fromUtf8(recordBytes(*r)).contains(query_, Qt::CaseInsensitive) || model->textPreview(row).contains(query_, Qt::CaseInsensitive);
}
}
