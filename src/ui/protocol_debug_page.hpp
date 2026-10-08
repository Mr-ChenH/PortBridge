#pragma once
#include "protocol_debug_session.hpp"
#include <QWidget>
#include <memory>
class QSettings;
namespace portbridge {
class ProtocolDebugPage final : public QWidget {
    Q_OBJECT
  public:
    explicit ProtocolDebugPage(ProtocolDebugSession::Mode mode, QSettings *settings,
                               QWidget *parent = nullptr);
    ~ProtocolDebugPage() override;
    ProtocolDebugSession *session() const;
    void setDarkTheme(bool dark);
    void triggerSend();
    void focusUrl();
    bool dirty() const;
    bool createSavedRequest(const QString &name, const QString &url, QString *error = nullptr);
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
