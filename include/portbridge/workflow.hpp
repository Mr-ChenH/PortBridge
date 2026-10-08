#pragma once
#include "portbridge/session_controller.hpp"
#include <QObject>
#include <QJsonObject>
#include <QPointF>
#include <QVector>
#include <QStringList>
#include <functional>
#include <memory>

namespace portbridge {
struct WorkflowNode { QString id, type, title; QPointF position; QJsonObject parameters; };
struct WorkflowEdge { QString id, from, to, port = QStringLiteral("success"); };
struct WorkflowIssue { QString nodeId, field, message; };
struct WorkflowLimits { int maxSteps = 10000; int maxDurationMs = 600000; int maxMessageBytes = 8*1024*1024; int observerQueueBytes = 4*1024*1024; int maxVariableBytes = 16*1024*1024; int maxLogEntries = 2000; };
struct WorkflowDocument {
    QString id, name, description;
    QVector<WorkflowNode> nodes;
    QVector<WorkflowEdge> edges;
    QJsonObject initialVariables;
    WorkflowLimits limits;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject&, WorkflowDocument*, QString* error = nullptr);
    static WorkflowDocument templateDocument(const QString& key);
    QVector<WorkflowIssue> validate() const;
};
struct WorkflowNodeDefinition { QString type, name, group, protocol, description; QStringList outputs; QJsonObject defaults; };
QVector<WorkflowNodeDefinition> workflowNodeDefinitions();
// Explicit snapshots for workflow-owned resources; existing profile v1 is untouched.
QJsonObject workflowConnectionConfigToJson(const ConnectionConfig&);
bool workflowConnectionConfigFromJson(const QJsonObject&, ConnectionConfig*, QString* error = nullptr);
struct WorkflowLogEntry { QString nodeId, nodeTitle, status, detail; qint64 timestampMs = 0, elapsedMs = 0; };
enum class WorkflowRunState { Idle, Running, Paused, Completed, Failed, Stopped };
enum class WorkflowNodeState { Pending, Running, Succeeded, Failed, Skipped };
struct WorkflowNodeResult { WorkflowNodeState state = WorkflowNodeState::Pending; QJsonObject output; QString detail; qint64 elapsedMs = 0; };

class WorkflowRunner : public QObject {
    Q_OBJECT
public:
    explicit WorkflowRunner(QObject* parent = nullptr);
    ~WorkflowRunner() override;
    void setSessionProvider(std::function<SessionController*()> provider);
    void setProfilesProvider(std::function<QVector<ConnectionConfig>()> provider);
    bool start(const WorkflowDocument&, QString* error = nullptr);
    void stop();
    void pause();
    void resume();
    bool active() const;
    WorkflowRunState state() const;
    QString runId() const;
    QString activeNodeId() const;
    QString resourceSummary() const;
    QJsonObject variables() const;
    QVector<WorkflowLogEntry> logs() const;
    WorkflowNodeResult result(const QString& nodeId) const;
    // True while this runner owns/borrows the given raw session for communication.
    bool usesSession(const SessionController*) const;
signals:
    void stateChanged();
    void nodeChanged(const QString& nodeId);
    void logsChanged();
    void variablesChanged();
    void resourceChanged();
    void finished(bool succeeded, const QString& error);
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
}
