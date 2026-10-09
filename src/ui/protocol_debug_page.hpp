#pragma once
#include "protocol_debug_session.hpp"
#include <QWidget>
#include <memory>
class QSettings;
namespace portbridge {
class HttpProjectStore;
class HttpSequenceRunner;
class ProtocolDebugPage final : public QWidget {
    Q_OBJECT
  public:
    explicit ProtocolDebugPage(ProtocolDebugSession::Mode mode, QSettings *settings,
                               QWidget *parent = nullptr);
    ~ProtocolDebugPage() override;
    HttpSequenceRunner *sequenceRunner() const;
    bool startHttpSequence(const QStringList &requestIds, bool continueOnFailure = false,
                           QString *error = nullptr);
    QJsonObject assertionResult() const;
    HttpProjectStore *projectStore() const;
    ProtocolDebugSession *session() const;
    void setDarkTheme(bool dark);
    void triggerSend();
    void focusUrl();
    bool dirty() const;
    bool createSavedRequest(const QString &name, const QString &url, QString *error = nullptr);
    QJsonObject exportHttpProject(QString *error = nullptr) const;
    bool importHttpProject(const QJsonObject &document, QString *error = nullptr);
    bool saveDraft(QString *error = nullptr);
    bool loadDraft(const QJsonObject &draft, QString *error = nullptr);
    QJsonObject exportDraft(QString *error = nullptr) const;
    QJsonObject requestParameters(QString *error = nullptr) const;
    QString summary() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
} // namespace portbridge
