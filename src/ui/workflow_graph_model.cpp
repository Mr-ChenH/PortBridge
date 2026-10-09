#include "workflow_graph_model.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSize>
#include <QUuid>
#include <QWidget>
#include <QtNodes/NodeData>
#include <QtNodes/NodeStyle>
#include <QtNodes/StyleCollection>
#include <algorithm>

namespace portbridge {
using namespace QtNodes;
static QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
static QString connectionKey(ConnectionId c) {
    return QStringLiteral("%1:%2:%3").arg(c.outNodeId).arg(c.outPortIndex).arg(c.inNodeId);
}
WorkflowGraphModel::WorkflowGraphModel(QObject *parent) {
    setParent(parent);
    m_document.id = uuid();
    m_document.name = QStringLiteral("未命名流程");
}
NodeId WorkflowGraphModel::newNodeId() { return m_nextId++; }
WorkflowNode *WorkflowGraphModel::node(NodeId id) {
    auto sid = m_ids.value(id);
    for (auto &n : m_document.nodes)
        if (n.id == sid)
            return &n;
    return nullptr;
}
const WorkflowNode *WorkflowGraphModel::node(NodeId id) const {
    auto sid = m_ids.value(id);
    for (const auto &n : m_document.nodes)
        if (n.id == sid)
            return &n;
    return nullptr;
}
NodeId WorkflowGraphModel::graphId(const QString &sid) const {
    for (auto it = m_ids.cbegin(); it != m_ids.cend(); ++it)
        if (it.value() == sid)
            return it.key();
    return InvalidNodeId;
}
QStringList WorkflowGraphModel::outputs(NodeId id) const {
    const auto *n = node(id);
    if (n)
        for (const auto &d : workflowNodeDefinitions())
            if (d.type == n->type)
                return d.outputs;
    return {};
}
std::unordered_set<NodeId> WorkflowGraphModel::allNodeIds() const {
    std::unordered_set<NodeId> ids;
    for (auto it = m_ids.cbegin(); it != m_ids.cend(); ++it)
        ids.insert(it.key());
    return ids;
}
ConnectionId WorkflowGraphModel::connectionId(const WorkflowEdge &e) const {
    auto id = graphId(e.from);
    return {id, PortIndex(outputs(id).indexOf(e.port)), graphId(e.to), 0};
}
std::unordered_set<ConnectionId> WorkflowGraphModel::allConnectionIds(NodeId id) const {
    std::unordered_set<ConnectionId> ids;
    for (const auto &e : m_document.edges) {
        auto c = connectionId(e);
        if (c.outNodeId == id || c.inNodeId == id)
            ids.insert(c);
    }
    return ids;
}
std::unordered_set<ConnectionId> WorkflowGraphModel::connections(NodeId id, PortType type,
                                                                 PortIndex index) const {
    std::unordered_set<ConnectionId> ids;
    for (auto c : allConnectionIds(id))
        if (type == PortType::Out ? (c.outNodeId == id && c.outPortIndex == index)
                                  : (c.inNodeId == id && c.inPortIndex == index))
            ids.insert(c);
    return ids;
}
bool WorkflowGraphModel::connectionExists(ConnectionId c) const {
    for (const auto &e : m_document.edges)
        if (connectionId(e) == c)
            return true;
    return false;
}
bool WorkflowGraphModel::nodeExists(NodeId id) const { return node(id) != nullptr; }
NodeId WorkflowGraphModel::addNode(const QString type) {
    if (m_locked || m_document.nodes.size() >= 256)
        return InvalidNodeId;
    for (const auto &d : workflowNodeDefinitions())
        if (d.type == type) {
            auto id = newNodeId();
            WorkflowNode n{uuid(), type, d.name, {}, d.defaults};
            m_ids.insert(id, n.id);
            m_stableIds.insert(n.id, id);
            m_document.nodes.push_back(n);
            emit nodeCreated(id);
            emit documentChanged();
            return id;
        }
    emit editRejected(QStringLiteral("不支持的节点类型：%1").arg(type));
    return InvalidNodeId;
}
bool WorkflowGraphModel::connectionPossible(ConnectionId c) const {
    const auto *a = node(c.outNodeId);
    const auto *b = node(c.inNodeId);
    if (m_locked || !a || !b || a == b || b->type == "start" || c.inPortIndex != 0 ||
        c.outPortIndex >= PortIndex(outputs(c.outNodeId).size()) || m_document.edges.size() >= 512 ||
        !connections(c.outNodeId, PortType::Out, c.outPortIndex).empty())
        return false;
    // Every directed cycle must pass a finite-loop node; ordinary cycles are rejected.
    if (a->type == "loop" || b->type == "loop")
        return true;
    QSet<QString> seen;
    std::function<bool(QString)> reaches = [&](QString sid) {
        if (sid == a->id)
            return true;
        if (seen.contains(sid))
            return false;
        seen.insert(sid);
        for (const auto &n : m_document.nodes)
            if (n.id == sid && n.type == "loop")
                return false;
        for (const auto &e : m_document.edges)
            if (e.from == sid && reaches(e.to))
                return true;
        return false;
    };
    return !reaches(b->id);
}
void WorkflowGraphModel::addConnection(ConnectionId c) {
    if (!connectionPossible(c)) {
        emit editRejected(QStringLiteral("无法连接：出口仅允许一条线；请通过有限循环节点构建循环。"));
        return;
    }
    auto key = connectionKey(c);
    auto eid = m_edgeIds.value(key);
    if (eid.isEmpty()) {
        eid = uuid();
        m_edgeIds.insert(key, eid);
    }
    m_document.edges.push_back(
        {eid, node(c.outNodeId)->id, node(c.inNodeId)->id, outputs(c.outNodeId).value(c.outPortIndex)});
    emit connectionCreated(c);
    emit documentChanged();
}
QVariant WorkflowGraphModel::nodeData(NodeId id, NodeRole role) const {
    const auto *n = node(id);
    if (!n)
        return {};
    switch (role) {
    case NodeRole::Type:
        return n->type;
    case NodeRole::Position:
        return n->position;
    case NodeRole::Caption:
        return n->title;
    case NodeRole::CaptionVisible:
        return true;
    case NodeRole::InPortCount:
        return uint(n->type == "start" ? 0 : 1);
    case NodeRole::OutPortCount:
        return uint(outputs(id).size());
    case NodeRole::Size:
        return QSize(
            n->type == "start" || n->type == "end" ? 108 : 248,
            n->type == "start" || n->type == "end" ? 76 : std::max(146, 66 + int(outputs(id).size()) * 28));
    case NodeRole::Style: {
        auto style = StyleCollection::nodeStyle().toJson();
        auto values = style["NodeStyle"].toObject();
        values["ShadowEnabled"] = false;
        values["Opacity"] = 1.;
        style["NodeStyle"] = values;
        return style;
    }
    case NodeRole::Widget:
        return QVariant::fromValue<QWidget *>(nullptr);
    default:
        return {};
    }
}
NodeFlags WorkflowGraphModel::nodeFlags(NodeId) const {
    return m_locked ? NodeFlag::Locked : NodeFlag::NoFlags;
}
bool WorkflowGraphModel::setNodeData(NodeId id, NodeRole role, QVariant value) {
    auto *n = node(id);
    if (m_locked || !n)
        return false;
    if (role == NodeRole::Position) {
        if (n->position == value.toPointF())
            return true;
        n->position = value.toPointF();
        emit nodePositionUpdated(id);
        emit documentChanged();
        return true;
    }
    return false;
}
QVariant WorkflowGraphModel::portData(NodeId id, PortType type, PortIndex index, PortRole role) const {
    switch (role) {
    case PortRole::DataType:
        return QVariant::fromValue(NodeDataType{"execution", QStringLiteral("执行顺序")});
    case PortRole::ConnectionPolicyRole:
        return QVariant::fromValue(type == PortType::Out ? ConnectionPolicy::One : ConnectionPolicy::Many);
    case PortRole::CaptionVisible:
        return true;
    case PortRole::Caption:
        return type == PortType::In ? QStringLiteral("执行入口") : outputs(id).value(index);
    default:
        return {};
    }
}
bool WorkflowGraphModel::deleteConnection(ConnectionId c) {
    if (m_locked)
        return false;
    for (int i = 0; i < m_document.edges.size(); ++i)
        if (connectionId(m_document.edges[i]) == c) {
            m_document.edges.removeAt(i);
            emit connectionDeleted(c);
            emit documentChanged();
            return true;
        }
    return false;
}
bool WorkflowGraphModel::deleteNode(NodeId id) {
    if (m_locked || !nodeExists(id))
        return false;
    for (auto c : allConnectionIds(id))
        deleteConnection(c);
    auto sid = m_ids.take(id);
    for (int i = 0; i < m_document.nodes.size(); ++i)
        if (m_document.nodes[i].id == sid) {
            m_document.nodes.removeAt(i);
            break;
        }
    emit nodeDeleted(id);
    emit documentChanged();
    return true;
}
QJsonObject WorkflowGraphModel::saveNode(NodeId id) const {
    const auto *n = node(id);
    if (!n)
        return {};
    return {{"id", int(id)},       {"position", QJsonObject{{"x", n->position.x()}, {"y", n->position.y()}}},
            {"workflowId", n->id}, {"type", n->type},
            {"title", n->title},   {"parameters", n->parameters}};
}
void WorkflowGraphModel::loadNode(const QJsonObject &o) {
    if (m_locked || m_document.nodes.size() >= 256)
        return;
    auto id = NodeId(o["id"].toInt());
    if (nodeExists(id))
        return;
    auto pos = o["position"].toObject();
    auto sid = o["workflowId"].toString();
    if (sid.isEmpty() || graphId(sid) != InvalidNodeId)
        sid = uuid();
    m_document.nodes.push_back({sid,
                                o["type"].toString(),
                                o["title"].toString(),
                                {pos["x"].toDouble(), pos["y"].toDouble()},
                                o["parameters"].toObject()});
    m_ids.insert(id, sid);
    m_stableIds.insert(sid, id);
    m_nextId = std::max(m_nextId, id + 1);
    emit nodeCreated(id);
    emit documentChanged();
}
void WorkflowGraphModel::setDocument(const WorkflowDocument &d) {
    if (m_locked)
        return;
    m_document = d;
    m_ids.clear();
    m_edgeIds.clear();
    for (const auto &n : d.nodes) {
        auto id = m_stableIds.value(n.id, InvalidNodeId);
        if (id == InvalidNodeId) {
            id = newNodeId();
            m_stableIds.insert(n.id, id);
        }
        m_ids.insert(id, n.id);
    }
    for (const auto &e : d.edges)
        m_edgeIds.insert(connectionKey(connectionId(e)), e.id);
    emit modelReset();
    emit documentChanged();
}
void WorkflowGraphModel::forgetHistory() {
    m_stableIds.clear();
    m_edgeIds.clear();
    for (auto it = m_ids.constBegin(); it != m_ids.constEnd(); ++it)
        m_stableIds.insert(it.value(), it.key());
    for (const auto &edge : m_document.edges)
        m_edgeIds.insert(connectionKey(connectionId(edge)), edge.id);
}
void WorkflowGraphModel::setLocked(bool locked) {
    m_locked = locked;
    for (auto id : allNodeIds())
        emit nodeFlagsUpdated(id);
}
void WorkflowGraphModel::updateNode(const QString &sid, const QString &title, const QJsonObject &p) {
    if (m_locked)
        return;
    auto id = graphId(sid);
    if (auto *n = node(id)) {
        n->title = title;
        n->parameters = p;
        emit nodeUpdated(id);
        emit documentChanged();
    }
}
void WorkflowGraphModel::setName(const QString &name) {
    if (m_locked)
        return;
    m_document.name = name;
    emit documentChanged();
}
} // namespace portbridge
