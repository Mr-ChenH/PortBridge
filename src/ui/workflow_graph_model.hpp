#pragma once
#include "portbridge/workflow.hpp"
#include <QHash>
#include <QtNodes/AbstractGraphModel>
#include <functional>

namespace portbridge {
class WorkflowGraphModel final : public QtNodes::AbstractGraphModel {
    Q_OBJECT
  public:
    explicit WorkflowGraphModel(QObject *parent = nullptr);
    WorkflowDocument document() const { return m_document; }
    void setDocument(const WorkflowDocument &);
    void forgetHistory();
    WorkflowNode *node(QtNodes::NodeId);
    const WorkflowNode *node(QtNodes::NodeId) const;
    QtNodes::NodeId graphId(const QString &) const;
    QStringList outputs(QtNodes::NodeId) const;
    void setLocked(bool);
    bool locked() const { return m_locked; }
    void updateNode(const QString &, const QString &, const QJsonObject &);
    void setName(const QString &);
    QtNodes::NodeId newNodeId() override;
    std::unordered_set<QtNodes::NodeId> allNodeIds() const override;
    std::unordered_set<QtNodes::ConnectionId> allConnectionIds(QtNodes::NodeId) const override;
    std::unordered_set<QtNodes::ConnectionId> connections(QtNodes::NodeId, QtNodes::PortType,
                                                          QtNodes::PortIndex) const override;
    bool connectionExists(QtNodes::ConnectionId) const override;
    QtNodes::NodeId addNode(QString const type = QString()) override;
    bool connectionPossible(QtNodes::ConnectionId) const override;
    bool detachPossible(QtNodes::ConnectionId const) const override { return !m_locked; }
    void addConnection(QtNodes::ConnectionId) override;
    bool nodeExists(QtNodes::NodeId) const override;
    QVariant nodeData(QtNodes::NodeId, QtNodes::NodeRole) const override;
    QtNodes::NodeFlags nodeFlags(QtNodes::NodeId) const override;
    bool setNodeData(QtNodes::NodeId, QtNodes::NodeRole, QVariant) override;
    QVariant portData(QtNodes::NodeId, QtNodes::PortType, QtNodes::PortIndex,
                      QtNodes::PortRole) const override;
    bool setPortData(QtNodes::NodeId, QtNodes::PortType, QtNodes::PortIndex, QVariant const &,
                     QtNodes::PortRole) override {
        return false;
    }
    bool deleteConnection(QtNodes::ConnectionId) override;
    bool deleteNode(QtNodes::NodeId) override;
    QJsonObject saveNode(QtNodes::NodeId) const override;
    void loadNode(QJsonObject const &) override;
    bool loopsEnabled() const override { return true; }
  signals:
    void documentChanged();
    void editRejected(const QString &);

  private:
    QtNodes::ConnectionId connectionId(const WorkflowEdge &) const;
    WorkflowDocument m_document;
    QHash<QtNodes::NodeId, QString> m_ids;
    QHash<QString, QtNodes::NodeId> m_stableIds;
    QHash<QString, QString> m_edgeIds;
    QtNodes::NodeId m_nextId = 1;
    bool m_locked = false;
};
} // namespace portbridge
