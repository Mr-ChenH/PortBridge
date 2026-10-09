#pragma once
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
class QSettings;
namespace portbridge {
// Project definitions are persistent; secret and response-derived values are
// runtime-only.
class HttpProjectStore {
  public:
    explicit HttpProjectStore(QSettings *settings);
    QJsonArray projects() const { return projects_; }
    QJsonArray requests() const { return requests_; }
    QString loadError() const { return loadError_; }
    bool setRequests(const QJsonArray &requests, QString *error = nullptr);
    QJsonObject project() const;
    QJsonObject environment() const;
    QString projectId() const { return project_; }
    QString environmentId() const { return environment_; }
    quint64 revision() const { return revision_; }
    bool selectProject(const QString &id, QString *error = nullptr);
    bool selectEnvironment(const QString &id, QString *error = nullptr);
    bool createProject(const QString &name, QString *error = nullptr);
    bool renameProject(const QString &name, QString *error = nullptr);
    bool removeProject(QString *error = nullptr);
    bool copyEnvironment(const QString &name, QString *error = nullptr);
    bool createEnvironment(const QString &name, QString *error = nullptr);
    bool removeEnvironment(QString *error = nullptr);
    bool renameEnvironment(const QString &name, QString *error = nullptr);
    bool setProjectVariables(const QJsonArray &variables, QString *error = nullptr);
    bool setEnvironmentVariables(const QJsonArray &variables, QString *error = nullptr);
    bool setProjectAuth(const QJsonObject &auth, QString *error = nullptr);
    bool setBrowserFingerprint(const QJsonObject &configuration, QString *error = nullptr);
    bool saveConfiguration(const QString &projectId, const QString &environmentId, quint64 revision,
                           const QJsonObject &configuration, const QStringList &clearRuntime,
                           QString *error = nullptr);
    bool renameFolder(const QString &from, const QString &to, QString *error = nullptr);
    bool removeFolder(const QString &name, QString *error = nullptr);
    bool addFolder(const QString &name, QString *error = nullptr);
    QJsonObject effectiveVariables() const;
    QJsonObject runtimeVariables() const;
    void clearRuntimeVariable(const QString &name);
    void clearRuntime();
    QString expandUrl(const QString &value, QString *error = nullptr) const;
    QString expand(const QString &value, QString *error = nullptr, bool json = false) const;
    bool extract(const QJsonObject &response, const QJsonArray &rules, const QString &projectId,
                 const QString &environmentId, QString *error = nullptr);
    QJsonObject exportProject(const QJsonArray &requests) const;
    bool importProject(const QJsonObject &document, QJsonArray *requests, QString *error = nullptr);
    static QString validateVariables(const QJsonArray &variables);
    static QString validateRules(const QJsonArray &rules);

  private:
    QSettings *settings_;
    QJsonArray projects_, requests_;
    QString loadError_;
    bool readOnly_ = false;
    QString project_, environment_;
    quint64 revision_ = 0;
    QHash<QString, QString> selectedEnvironments_;
    QHash<QString, QJsonObject> runtime_;
    QString runtimeKey(const QString &project, const QString &environment) const;
    bool commit(QJsonArray candidate, QString *error, const QJsonArray *requests = nullptr);
    int projectIndex(const QString &id) const;
    bool replaceProject(QJsonObject project, QString *error);
};
} // namespace portbridge
