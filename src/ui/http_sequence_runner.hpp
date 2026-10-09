#pragma once
#include "http_assertions.hpp"
#include "http_project_store.hpp"
#include "protocol_debug_session.hpp"
#include <QElapsedTimer>
#include <QTimer>
namespace portbridge {
class HttpSequenceRunner final : public QObject {
    Q_OBJECT
  public:
    HttpSequenceRunner(HttpProjectStore *store, ProtocolDebugSession *session,
                       QObject *parent = nullptr);
    ~HttpSequenceRunner() override;
    bool start(const QJsonArray &requests, bool continueOnFailure, QString *error = nullptr);
    void stop();
    bool running() const { return running_; }
    QJsonObject report() const;
    QJsonArray results() const { return results_; }
    static QJsonArray extractionRules(const QJsonObject &request, QString *error);
  signals:
    void changed();

  private:
    void next();
    void observe();
    void complete(bool passed, const QString &message, const QJsonArray &assertions = {},
                  const QJsonObject &response = {});
    void finish(bool cancelled);
    HttpProjectStore *store_;
    ProtocolDebugSession *session_;
    QTimer deadline_;
    QElapsedTimer elapsed_;
    QJsonArray requests_, results_, currentAssertions_, currentExtraction_;
    QString id_, project_, environment_, previousOperation_;
    quint64 revision_ = 0, epoch_ = 0;
    int index_ = 0;
    bool running_ = false, waiting_ = false, continue_ = false, cancelled_ = false,
         startingStep_ = false;
};
} // namespace portbridge
