#pragma once
#include "portbridge/workflow.hpp"
#include <QWidget>
#include <memory>
class QCloseEvent;
class QResizeEvent;
namespace QtNodes {
class BasicGraphicsScene;
class GraphicsView;
} // namespace QtNodes
namespace portbridge {
class WorkflowGraphModel;
class WorkflowPage final : public QWidget {
    Q_OBJECT
  public:
    explicit WorkflowPage(QWidget *parent = nullptr);
    ~WorkflowPage() override;
    WorkflowRunner *runner() const;
    void setDarkTheme(bool);
    void setSessionProvider(std::function<SessionController *()>);
    void setProfilesProvider(std::function<QVector<ConnectionConfig>()>);
    void setRunPreparation(std::function<bool(const WorkflowDocument &, QString *)>);
    WorkflowDocument document() const;
    WorkflowGraphModel *graphModel() const;
    QtNodes::BasicGraphicsScene *graphScene() const;
    QtNodes::GraphicsView *graphView() const;
    QString selectedNodeId() const;
    bool isDirty() const;
    // Loading and all graph edits are side-effect free. Runtime owns its snapshot.
    bool setDocument(const WorkflowDocument &, QString *error = nullptr);
    bool loadFile(const QString &, QString *error = nullptr);
    bool saveFile(const QString &, QString *error = nullptr);
    bool applyTemplate(const QString &key);
    void selectNode(const QString &, bool results = false);
    bool confirmLeave();
  public slots:
    void showTemplates();
    void validateFlow();
    void startRun();

  protected:
    void closeEvent(QCloseEvent *) override;
    void resizeEvent(QResizeEvent *) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
} // namespace portbridge
