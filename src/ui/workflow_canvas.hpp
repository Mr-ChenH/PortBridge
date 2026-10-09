#pragma once
#include "design_widgets.hpp"
#include "workflow_graph_model.hpp"
#include "workflow_presentation.hpp"
#include <QDrag>
#include <QDragEnterEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMimeData>
#include <QRegularExpression>
#include <QTreeWidget>
#include <QUndoStack>
#include <QtNodes/AbstractConnectionPainter>
#include <QtNodes/AbstractNodePainter>
#include <QtNodes/BasicGraphicsScene>
#include <QtNodes/GraphicsView>
#include <QtNodes/UndoCommands>
#include <QtNodes/internal/AbstractNodeGeometry.hpp>
#include <QtNodes/internal/ConnectionGraphicsObject.hpp>
#include <QtNodes/internal/NodeGraphicsObject.hpp>
#include <cmath>
#include <functional>

namespace portbridge::workflowUi {
inline QString portLabel(const QString &port) {
    if (port == "success")
        return QStringLiteral("下一步");
    if (port == "error")
        return QStringLiteral("错误");
    if (port == "true")
        return QStringLiteral("通过");
    if (port == "false")
        return QStringLiteral("不通过");
    if (port == "body")
        return QStringLiteral("循环体");
    if (port == "done")
        return QStringLiteral("退出");
    return port;
}
inline bool sensitive(const QString &key) {
    return key.contains(
        QRegularExpression("token|password|passwd|secret|authorization|credential|cookie|api.?key",
                           QRegularExpression::CaseInsensitiveOption));
}
struct SecretSample {
    QString text;
    bool partial{};
    bool operator==(const SecretSample& other) const { return text == other.text && partial == other.partial; }
};
using SecretSamples = QVector<SecretSample>;
inline void appendSecret(const QString& text, SecretSamples& secrets) {
    if (text.isEmpty() || text == "[已遮蔽]" || text.contains("${") || secrets.size() >= 1024)
        return;
    const SecretSample sample{text.size() > 4096 ? text.left(128) : text, text.size() > 4096};
    if (!secrets.contains(sample))
        secrets.append(sample);
}
inline QString redactText(QString text, const SecretSamples &secrets = {});
inline bool encodedNeedsMasking(const QString& encoded, const SecretSamples& secrets) {
    QByteArray tail;
    constexpr qsizetype chunk = 65536; // aligned Base64 groups; retain a bounded overlap for secret matches
    for (qsizetype offset = 0; offset < encoded.size(); offset += chunk) {
        auto decoded = QByteArray::fromBase64Encoding(encoded.mid(offset, chunk).toLatin1(),
                                                     QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded)
            return true;
        auto bytes = tail + decoded.decoded;
        auto text = QString::fromUtf8(bytes);
        // Encoded structured data can contain credentials of any JSON type,
        // escaped field names or keys split across chunks. Hide that representation
        // conservatively; the separately redacted structured result stays readable.
        if (bytes.contains('{') || bytes.contains('[') || redactText(text, secrets) != text)
            return true;
        tail = bytes.right(16384);
    }
    return false;
}
inline QJsonValue redact(const QJsonValue &value, const QString &key = {}, const SecretSamples &secrets = {}) {
    if (sensitive(key))
        return QStringLiteral("[已遮蔽]");
    if (value.isObject()) {
        auto o = value.toObject();
        for (auto it = o.begin(); it != o.end(); ++it)
            it.value() = redact(it.value(), it.key(), secrets);
        return o;
    }
    if (value.isArray()) {
        QJsonArray a;
        for (auto v : value.toArray())
            a.append(redact(v, {}, secrets));
        return a;
    }
    if (value.isString()) {
        auto s = value.toString();
        if (key.endsWith("Base64", Qt::CaseInsensitive) && encodedNeedsMasking(s, secrets))
            return QStringLiteral("[原始字节包含凭据或无法安全检查，已遮蔽]");
        if (s.size() > 262144 && (s.contains('{') || s.contains('[')))
            return QStringLiteral("[大型结构化文本无法安全预览，已遮蔽]");
        auto parsed = s.size() <= 262144 ? QJsonDocument::fromJson(s.toUtf8()) : QJsonDocument();
        if (parsed.isObject())
            return QString::fromUtf8(QJsonDocument(redact(parsed.object(), {}, secrets).toObject())
                                         .toJson(QJsonDocument::Compact));
        if (parsed.isArray())
            return QString::fromUtf8(QJsonDocument(redact(parsed.array(), {}, secrets).toArray())
                                         .toJson(QJsonDocument::Compact));
        return redactText(s, secrets);
    }
    return value;
}
inline QString redactText(QString text, const SecretSamples &secrets) {
    static const QRegularExpression credentialKey(
        R"pb("(?:[^"\\]*(?:token|password|passwd|secret|authorization|credential|cookie|api.?key)[^"\\]*|[^"\\]*\\u[^"\\]*)"\s*:)pb",
        QRegularExpression::CaseInsensitiveOption);
    // Free text may contain truncated JSON; never rely on the credential value's type.
    if (text.contains(credentialKey))
        return QStringLiteral("[文本包含凭据字段，已遮蔽]");
    for (const auto &secret : secrets) {
        if (secret.partial && text.contains(secret.text))
            return QStringLiteral("[文本包含长凭据，已遮蔽]");
        if (!secret.text.isEmpty())
            text.replace(secret.text, QStringLiteral("[已遮蔽]"));
    }
    const QRegularExpression auth("((?:authorization|cookie|set-cookie|x-api-key)\\s*:\\s*)([^\\r\\n]+)",
                                  QRegularExpression::CaseInsensitiveOption);
    auto matches = auth.globalMatch(text);
    QVector<QPair<int, QPair<int, QString>>> replacements;
    while (matches.hasNext()) {
        auto m = matches.next();
        if (!m.captured(2).contains("${"))
            replacements.push_back(
                {int(m.capturedStart(2)), {int(m.capturedLength(2)), QStringLiteral("[已遮蔽]")}});
    }
    for (auto it = replacements.crbegin(); it != replacements.crend(); ++it)
        text.replace(it->first, it->second.first, it->second.second);
    text.replace(QRegularExpression("Bearer\\s+(?!\\$\\{)[A-Za-z0-9._~+/-]+=*",
                                    QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("Bearer [已遮蔽]"));
    text.replace(QRegularExpression("((?:token|password|secret|api_key)=)[^&\\s]+",
                                    QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("\\1[已遮蔽]"));
    text.replace(
        QRegularExpression(
            R"pb(("(?:[^"\\]*token[^"\\]*|password|secret|authorization|cookie|api.?key)"\s*:\s*)"(?:\\.|[^"\\])*(?:"|$))pb",
            QRegularExpression::CaseInsensitiveOption),
        QStringLiteral("\\1\"[已遮蔽]\""));
    return text;
}
inline void collectSecrets(const QJsonValue &value, SecretSamples &secrets, const QString &key = {},
                           int* remaining = nullptr, int depth = 0) {
    int localBudget = 65536;
    auto& budget = remaining ? *remaining : localBudget;
    if (--budget < 0 || depth > 24 || secrets.size() >= 1024)
        return;
    if (sensitive(key)) {
        if (value.isString()) {
            appendSecret(value.toString(), secrets);
            return;
        }
        if (value.isDouble()) {
            appendSecret(QString::number(value.toDouble(), 'g', 16), secrets);
            return;
        }
        if (value.isObject()) {
            const auto object = value.toObject();
            for (auto it=object.constBegin(); it!=object.constEnd(); ++it)
                collectSecrets(it.value(), secrets, "credential", &budget, depth+1);
            return;
        }
        if (value.isArray()) {
            for (const auto& item : value.toArray())
                collectSecrets(item, secrets, "credential", &budget, depth+1);
            return;
        }
    }
    if (value.isObject()) {
        auto o = value.toObject();
        if (sensitive(o.value("variable").toString()) && o.value("value").isString())
            appendSecret(o.value("value").toString(), secrets);
        for (auto it = o.constBegin(); it != o.constEnd(); ++it)
            collectSecrets(it.value(), secrets, it.key(), &budget, depth+1);
    }
    if (value.isArray())
        for (auto v : value.toArray())
            collectSecrets(v, secrets, {}, &budget, depth+1);
    if (value.isString() && value.toString().size() > 262144) {
        const auto text=value.toString();
        static const QRegularExpression field(
            R"pb("[^"\\]*(?:token|password|passwd|secret|authorization|credential|cookie|api.?key)[^"\\]*"\s*:\s*"([^"\\]{1,128}))pb",
            QRegularExpression::CaseInsensitiveOption);
        for (qsizetype offset=0; offset<text.size() && budget>0 && secrets.size()<1024; offset+=65536) {
            --budget;
            auto matches=field.globalMatch(text.mid(std::max(qsizetype(0),offset-512),66048));
            while (matches.hasNext() && budget>0 && secrets.size()<1024) {
                --budget;
                const SecretSample sample{matches.next().captured(1),true};
                if (!secrets.contains(sample)) secrets.append(sample);
            }
        }
    }
    if (value.isString() && value.toString().size() <= 262144) {
        auto doc = QJsonDocument::fromJson(value.toString().toUtf8());
        if (doc.isObject())
            collectSecrets(doc.object(), secrets, {}, &budget, depth+1);
    }
}
inline QString jsonText(const QJsonValue &v) {
    if (v.isObject())
        return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Indented));
    if (v.isArray())
        return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Indented));
    if (v.isString())
        return v.toString();
    if (v.isBool())
        return v.toBool() ? "true" : "false";
    if (v.isDouble())
        return QString::number(v.toDouble(), 'g', 15);
    return "null";
}
inline QString nodeState(WorkflowNodeState state) {
    switch (state) {
    case WorkflowNodeState::Running:
        return QStringLiteral("执行中");
    case WorkflowNodeState::Succeeded:
        return QStringLiteral("完成");
    case WorkflowNodeState::Failed:
        return QStringLiteral("失败");
    case WorkflowNodeState::Skipped:
        return QStringLiteral("未执行");
    default:
        return QStringLiteral("未运行");
    }
}
inline QStringList summary(const WorkflowNode &n) {
    auto p = n.parameters;
    static const auto definitions=workflowNodeDefinitions();
    for (const auto& definition : definitions) {
        if (definition.type!=n.type) continue;
        auto defaults=definition.defaults;
        for (auto it=p.constBegin();it!=p.constEnd();++it) defaults[it.key()]=it.value();
        p=defaults;break;
    }
    auto s = [&](const char *k) {
        auto v = p.value(k);
        return v.isString() ? v.toString().left(256) : jsonText(v).left(256);
    };
    if (n.type == "http")
        return {s("method") + "  " + s("url").remove(QRegularExpression("^https?://")), QStringLiteral("响应 → ") + s("output")};
    if (n.type == "ws")
        return {s("url"), QStringLiteral("握手完成后进入下一步")};
    if (n.type == "extract")
        return {s("path") + " → " + s("variable"), QStringLiteral("输入 ") + s("source")};
    if (n.type == "assert" || n.type == "branch") {
        const auto relation=p.value("relation").toString(p.value("operator").toString("equals"));
        const QString symbol=QMap<QString,QString>{{"equals","="},{"notEquals","≠"},{"greater",">"},{"less","<"},{"contains",QStringLiteral("包含")},{"exists",QStringLiteral("存在")}}.value(relation,relation);
        return {s(n.type == "assert" ? "source" : "variable").section('.', -1) + " " + symbol +
                    (relation=="exists" ? QString() : " " + s("expected")),
                QStringLiteral("明确通过 / 不通过")};
    }
    if (n.type == "raw")
        return {p.value("profile").toString(QStringLiteral("当前明确活动资源")),
                s("ownership") == "owned" ? QStringLiteral("流程建立并释放")
                                          : QStringLiteral("借用已有连接")};
    if (n.type == "send" || n.type == "sendWait")
        return {s("payload"), n.type == "sendWait"
                                  ? s("match") + QStringLiteral(" · 超时 ") + s("timeout") + " ms"
                                  : s("format")};
    if (n.type == "wait")
        return {s("expected"), QStringLiteral("超时 ") + s("timeout") + " ms"};
    if (n.type == "loop")
        return {s("count") + QStringLiteral(" 次"), QStringLiteral("循环体 / 退出")};
    if (n.type == "delay")
        return {s("duration") + " ms", QStringLiteral("非阻塞等待")};
    if (n.type == "variable")
        return {s("variable") + " = " + s("value"), QStringLiteral("写入运行变量")};
    if (n.type == "close")
        return {QStringLiteral("关闭码 ") + s("code"), s("reason")};
    if (n.type == "log")
        return {s("text"), QStringLiteral("写入步骤日志")};
    return {};
}
class Geometry final : public QtNodes::AbstractNodeGeometry {
  public:
    explicit Geometry(WorkflowGraphModel &m) : AbstractNodeGeometry(m) {}
    QSize size(QtNodes::NodeId id) const override {
        return _graphModel.nodeData(id, QtNodes::NodeRole::Size).toSize();
    }
    QRectF boundingRect(QtNodes::NodeId id) const override {
        return QRectF(QPointF(), size(id)).adjusted(-10, -6, 10, 6);
    }
    void recomputeSize(QtNodes::NodeId) const override {}
    // Port side follows the saved serpentine layout, without adding business parameters.
    bool flipped(QtNodes::NodeId id) const {
        const auto &m = static_cast<const WorkflowGraphModel &>(_graphModel);
        auto *n = m.node(id);
        if (!n)
            return false;
        const auto doc = m.document();
        for (const auto &edge : doc.edges) {
            const WorkflowNode *other = nullptr;
            const bool outgoing = edge.from == n->id;
            if (outgoing)
                other = m.node(m.graphId(edge.to));
            else if (edge.to == n->id)
                other = m.node(m.graphId(edge.from));
            if (!other || std::abs(other->position.y() - n->position.y()) > 120)
                continue;
            if (outgoing && other->position.x() < n->position.x() - 80)
                return true;
            if (!outgoing && other->position.x() > n->position.x() + 80)
                return true;
        }
        return false;
    }
    QPointF portPosition(QtNodes::NodeId id, QtNodes::PortType type,
                         QtNodes::PortIndex index) const override {
        auto s = size(id);
        bool right = (type == QtNodes::PortType::Out) != flipped(id);
        return {right ? double(s.width()) : 0., s.width() == 108                ? 38.
                                                : type == QtNodes::PortType::In ? 66.
                                                                                : 66. + index * 28};
    }
    QPointF portTextPosition(QtNodes::NodeId id, QtNodes::PortType type,
                             QtNodes::PortIndex index) const override {
        auto p = portPosition(id, type, index);
        return {p.x() > 0 ? p.x() - 80 : 12, p.y() + 5};
    }
    QPointF captionPosition(QtNodes::NodeId) const override { return {43, 26}; }
    QRectF captionRect(QtNodes::NodeId id) const override {
        return {43, 8, double(size(id).width() - 52), 28};
    }
    QPointF widgetPosition(QtNodes::NodeId) const override { return {}; }
    QRect resizeHandleRect(QtNodes::NodeId) const override { return {}; }
};
class NodePainter final : public QtNodes::AbstractNodePainter {
  public:
    bool *dark;
    WorkflowRunner *runner;
    NodePainter(bool *theme, WorkflowRunner *r) : dark(theme), runner(r) {}
    void paint(QPainter *p, QtNodes::NodeGraphicsObject &object) const override {
        const auto &m = static_cast<const WorkflowGraphModel &>(object.graphModel());
        auto *n = m.node(object.nodeId());
        if (!n)
            return;
        const auto &g = object.nodeScene()->nodeGeometry();
        const auto s = g.size(object.nodeId());
        const bool mini = s.width() == 108;
        const QColor ink(*dark ? "#e5edee" : "#213336"), muted(*dark ? "#b5c4cb" : "#435960"),
            panel(*dark ? "#171c1f" : "#ffffff"), line(*dark ? "#2b3438" : "#d5dfe1"),
            accent(*dark ? "#85dec4" : "#176e58"), red(*dark ? "#ed9693" : "#b04040");
        const auto state = runner->result(n->id).state;
        auto border = state == WorkflowNodeState::Failed ? red
                      : state == WorkflowNodeState::Running || state == WorkflowNodeState::Succeeded ||
                              object.isSelected()
                          ? accent
                          : line;
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->setBrush(panel);
        p->setPen(QPen(border, object.isSelected() || state == WorkflowNodeState::Running ? 2 : 1));
        p->drawRoundedRect(QRectF(QPointF(), s), mini ? 38 : 8, mini ? 38 : 8);
        const auto category = categoryColor(n->type, *dark);
        if (!mini) {
            auto tint = category; tint.setAlpha(18);
            p->setPen(Qt::NoPen); p->setBrush(tint);
            p->drawRoundedRect(QRectF(12, 12, 22, 22), 4, 4);
        }
        design::drawIcon(*p, QRectF(mini ? 17 : 15, mini ? 29 : 15, 16, 16), nodeIcon(n->type), category);
        p->setFont(design::font(13, false, true));
        p->setPen(ink);
        const int titleWidth = s.width() - (mini ? 46 : 126);
        p->drawText(QRect(mini ? 39 : 41, mini ? 22 : 8, titleWidth, 30), Qt::AlignVCenter,
                    p->fontMetrics().elidedText(n->title, Qt::ElideRight, titleWidth));
        if (!mini) {
            p->setFont(design::font(8, true)); p->setPen(category);
            p->drawText(QRect(s.width()-82, 12, 70, 22), Qt::AlignRight|Qt::AlignVCenter, protocolTag(n->type));
            p->setPen(line);
            p->drawLine(1, 42, s.width() - 1, 42);
            auto rows = summary(*n);
            SecretSamples secrets;
            collectSecrets(runner->variables(), secrets);
            collectSecrets(n->parameters, secrets);
            for (int i = 0; i < rows.size() && i < 2; ++i) {
                p->setFont(design::font(i ? 11 : 12, true));
                p->setPen(i ? muted : ink);
                auto safe = redactText(rows[i], secrets);
                const bool reservePortLabel = i || m.outputs(object.nodeId()).size() > 2;
                const bool reverse = !m.outputs(object.nodeId()).isEmpty() &&
                    g.portPosition(object.nodeId(), QtNodes::PortType::Out, 0).x() == 0;
                const int textLeft = reservePortLabel && reverse ? 88 : 13;
                const int textWidth = s.width() - textLeft - (reservePortLabel && !reverse ? 85 : 25);
                p->drawText(QRect(textLeft, 49 + i * 25, textWidth, 22), Qt::AlignVCenter,
                            p->fontMetrics().elidedText(safe, Qt::ElideRight, textWidth));
            }
            p->setFont(design::font(10));
            const auto doc = m.document();
            int ordinal = 0;
            for (const auto& node : doc.nodes) { ++ordinal; if (node.id == n->id) break; }
            p->setPen(muted);
            p->drawText(QRect(13, s.height() - 28, 28, 24), Qt::AlignVCenter,
                        QStringLiteral("%1").arg(ordinal, 2, 10, QChar('0')));
            p->setPen(border == line ? muted : border);
            p->drawText(QRect(s.width()-105, s.height() - 28, 92, 24), Qt::AlignRight|Qt::AlignVCenter, nodeState(state));
        }
        for (auto type : {QtNodes::PortType::In, QtNodes::PortType::Out}) {
            int count = type == QtNodes::PortType::In ? (n->type == "start" ? 0 : 1)
                                                      : m.outputs(object.nodeId()).size();
            for (int i = 0; i < count; ++i) {
                auto pos = g.portPosition(object.nodeId(), type, i);
                auto port = type == QtNodes::PortType::Out ? m.outputs(object.nodeId()).value(i) : QString();
                auto color = port == "error" ? red : muted;
                p->setPen(QPen(color, 1.6));
                p->setBrush(panel);
                p->drawEllipse(pos, 6, 6);
                if (!mini && type == QtNodes::PortType::Out && port != "success") {
                    p->setFont(design::font(10));
                    p->setPen(color);
                    auto t = g.portTextPosition(object.nodeId(), type, i);
                    p->drawText(t, portLabel(port));
                }
            }
        }
        p->restore();
    }
};
class EdgePainter final : public QtNodes::AbstractConnectionPainter {
    bool *dark;
    WorkflowRunner *runner;
    QPainterPath path(const QtNodes::ConnectionGraphicsObject &c) const {
        auto a = c.out(), b = c.in();
        auto id = c.connectionId();
        auto &geometry = c.nodeScene()->nodeGeometry();
        const auto &model = static_cast<const WorkflowGraphModel &>(c.graphModel());
        double distance = std::clamp(std::abs(a.x() - b.x()) * .4, 32., 160.);
        double out = 1, in = -1;
        if (model.nodeExists(id.outNodeId))
            out =
                geometry.portPosition(id.outNodeId, QtNodes::PortType::Out, id.outPortIndex).x() > 0 ? 1 : -1;
        if (model.nodeExists(id.inNodeId))
            in = geometry.portPosition(id.inNodeId, QtNodes::PortType::In, id.inPortIndex).x() > 0 ? 1 : -1;
        auto c1 = a + QPointF(out * distance, 0), c2 = b + QPointF(in * distance, 0);
        // QtNodes clips painting to its own connection bounds. Keep reverse/vertical
        // control points inside those bounds so row transitions remain continuous.
        const auto bounds = c.boundingRect().adjusted(2, 2, -2, -2);
        c1.setX(std::clamp(c1.x(), bounds.left(), bounds.right()));
        c2.setX(std::clamp(c2.x(), bounds.left(), bounds.right()));
        QPainterPath line(a);
        line.cubicTo(c1, c2, b);
        return line;
    }

  public:
    EdgePainter(bool *theme, WorkflowRunner *r) : dark(theme), runner(r) {}
    QPainterPath getPainterStroke(const QtNodes::ConnectionGraphicsObject &c) const override {
        QPainterPathStroker stroker;
        stroker.setWidth(14);
        return stroker.createStroke(path(c));
    }
    void paint(QPainter *p, const QtNodes::ConnectionGraphicsObject &c) const override {
        auto id = c.connectionId();
        const auto &model = static_cast<const WorkflowGraphModel &>(c.graphModel());
        auto *from = model.node(id.outNodeId);
        auto *to = model.node(id.inNodeId);
        bool visited = from && runner->result(from->id).state == WorkflowNodeState::Succeeded;
        bool current = visited && to && runner->result(to->id).state == WorkflowNodeState::Running;
        QColor color(*dark ? "#96a7ad" : "#70868c");
        if (visited || c.isSelected())
            color = QColor(*dark ? "#85dec4" : "#176e58");
        auto port = model.outputs(id.outNodeId).value(id.outPortIndex);
        if (port == "error")
            color = QColor(*dark ? "#ed9693" : "#b04040");
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(color, c.isSelected() ? 2.8 : 1.7, current ? Qt::DashLine : Qt::SolidLine));
        auto line = path(c);
        p->drawPath(line);
        if (from && to) {
            auto point = line.pointAtPercent(.97), end = line.pointAtPercent(1);
            auto angle = std::atan2(end.y() - point.y(), end.x() - point.x());
            QPolygonF arrow;
            arrow << end << end - QPointF(std::cos(angle - .45) * 8, std::sin(angle - .45) * 8)
                  << end - QPointF(std::cos(angle + .45) * 8, std::sin(angle + .45) * 8);
            p->setBrush(color);
            p->setPen(Qt::NoPen);
            p->drawPolygon(arrow);
        }
        p->restore();
    }
};
class Palette final : public QTreeWidget {
  public:
    using QTreeWidget::QTreeWidget;

  protected:
    void startDrag(Qt::DropActions) override {
        auto *item = currentItem();
        if (!item || item->data(0, Qt::UserRole).toString().isEmpty())
            return;
        auto *mime = new QMimeData;
        mime->setData("application/x-portbridge-workflow-node",
                      item->data(0, Qt::UserRole).toString().toUtf8());
        auto *drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->exec(Qt::CopyAction);
    }
};
class View final : public QtNodes::GraphicsView {
  public:
    bool *dark;
    WorkflowGraphModel *model;
    std::function<void(QString, QPointF)> add;
    View(QtNodes::BasicGraphicsScene *scene, WorkflowGraphModel *m, bool *theme)
        : GraphicsView(scene), dark(theme), model(m) {
        setAcceptDrops(true);
        setScaleRange(.25, 1.6);
        setAccessibleName(QStringLiteral("工作流画布：拖动节点和端口连接，Shift 框选，空白拖动平移"));
        setFrameShape(QFrame::NoFrame);
    }

  protected:
    void dragEnterEvent(QDragEnterEvent *e) override {
        if (!model->locked() && e->mimeData()->hasFormat("application/x-portbridge-workflow-node"))
            e->acceptProposedAction();
    }
    void dragMoveEvent(QDragMoveEvent *e) override {
        if (!model->locked() && e->mimeData()->hasFormat("application/x-portbridge-workflow-node"))
            e->acceptProposedAction();
    }
    void dropEvent(QDropEvent *e) override {
        if (model->locked())
            return;
        auto type = QString::fromUtf8(e->mimeData()->data("application/x-portbridge-workflow-node"));
        if (add) {
            add(type, mapToScene(e->position().toPoint()) - QPointF(104, 50));
            e->acceptProposedAction();
        }
    }
    void drawBackground(QPainter *p, const QRectF &r) override {
        p->fillRect(r, QColor(*dark ? "#111619" : "#f4f7f6"));
        p->setPen(QColor(*dark ? "#283034" : "#d7e1df"));
        for (int x = int(std::floor(r.left() / 20)) * 20; x < r.right(); x += 20)
            for (int y = int(std::floor(r.top() / 20)) * 20; y < r.bottom(); y += 20)
                p->drawPoint(QPointF(x, y));
    }
    void drawForeground(QPainter *p, const QRectF &) override {
        if (!model->document().nodes.isEmpty())
            return;
        p->save();
        p->resetTransform();
        p->setPen(QColor(*dark ? "#b5c4cb" : "#435960"));
        p->setFont(design::font(17));
        p->drawText(viewport()->rect().adjusted(20, 0, -20, -20), Qt::AlignCenter,
                    QStringLiteral("从一个步骤开始\n\n拖入节点，或选择模板搭建第一条流程。"));
        p->restore();
    }
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && !model->locked()) {
            auto *object = qgraphicsitem_cast<QtNodes::NodeGraphicsObject *>(itemAt(e->pos()));
            if (object) {
                auto index = nodeScene()->nodeGeometry().checkPortHit(
                    object->nodeId(), QtNodes::PortType::Out, object->mapFromScene(mapToScene(e->pos())));
                if (index != QtNodes::InvalidPortIndex &&
                    !model->connections(object->nodeId(), QtNodes::PortType::Out, index).empty()) {
                    emit model->editRejected(
                        QStringLiteral("该出口已有连线；选中连线删除，或从目标入口拖动重新连接。"));
                    e->accept();
                    return;
                }
            }
        }
        GraphicsView::mousePressEvent(e);
    }
    void keyPressEvent(QKeyEvent *e) override {
        if (model->locked() && (e->key() == Qt::Key_Delete || e->matches(QKeySequence::Paste) ||
                                e->matches(QKeySequence::Undo) || e->matches(QKeySequence::Redo))) {
            e->accept();
            return;
        }
        GraphicsView::keyPressEvent(e);
    }
};
class EditCommand final : public QUndoCommand {
    std::function<void()> before, after;

  public:
    EditCommand(QString title, std::function<void()> undo, std::function<void()> redo)
        : QUndoCommand(title), before(std::move(undo)), after(std::move(redo)) {}
    void undo() override { before(); }
    void redo() override { after(); }
};
} // namespace portbridge::workflowUi
