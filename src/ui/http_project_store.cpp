#include "http_project_store.hpp"
#include "http_assertions.hpp"
#include "http_template_preview.hpp"
#include "workflow/workflow_private.hpp"
#include <QDateTime>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QUuid>
#include <functional>
namespace portbridge {
namespace {
QString uid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool secretName(const QString &name) {
    static QRegularExpression re("token|password|secret|authorization|api[_-]?key|cookie",
                                 QRegularExpression::CaseInsensitiveOption);
    return re.match(name).hasMatch();
}
bool fail(QString *error, const QString &message) {
    if (error)
        *error = message;
    return false;
}
QJsonObject newEnvironment(const QString &name) {
    return {{"id", uid()}, {"name", name}, {"variables", QJsonArray{}}};
}
QJsonObject newProject(const QString &name) {
    return {{"id", uid()},
            {"name", name},
            {"folders", QJsonArray{}},
            {"auth", QJsonObject{{"kind", "none"}}},
            {"variables", QJsonArray{}},
            {"environments", QJsonArray{newEnvironment(QStringLiteral("默认环境"))}}};
}
QString scalar(const QJsonValue &value) { return workflowDetail::text(value); }
QString jsonEscape(const QString &value) {
    const auto bytes = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(bytes.mid(2, bytes.size() - 4));
}
QString runtimeError(const QJsonObject &values) {
    QJsonArray rows;
    for (auto it = values.begin(); it != values.end(); ++it)
        rows.append(QJsonObject{{"name", it.key()}, {"value", it.value().toObject().value("value")}});
    return HttpProjectStore::validateVariables(rows);
}
QString checkProject(const QJsonObject &p) {
    static QRegularExpression identity("^[A-Za-z0-9_-]{1,128}$");
    if (!p.value("folders").isArray() || !p.value("variables").isArray() ||
        !p.value("environments").isArray() || !p.value("auth").isObject())
        return QStringLiteral("项目定义字段类型无效。");
    if (!identity.match(p.value("id").toString()).hasMatch() ||
        p.value("name").toString().trimmed().isEmpty() || p.value("name").toString().size() > 128)
        return QStringLiteral("项目名称或ID无效。");
    if (p.value("folders").toArray().size() > 128)
        return QStringLiteral("最多128个文件夹。");
    QSet<QString> folders;
    for (const auto &f : p.value("folders").toArray()) {
        if (!f.isString() || f.toString().isEmpty() || f.toString().size() > 128 ||
            folders.contains(f.toString()))
            return QStringLiteral("文件夹名称无效或重复。");
        folders.insert(f.toString());
    }
    const auto environments = p.value("environments").toArray();
    if (environments.isEmpty() || environments.size() > 32)
        return QStringLiteral("每个项目须有1–32个环境。");
    QSet<QString> ids;
    for (const auto &v : environments) {
        const auto e = v.toObject();
        if (!v.isObject() || !identity.match(e.value("id").toString()).hasMatch() ||
            !e.value("variables").isArray() || ids.contains(e.value("id").toString()) ||
            e.value("name").toString().trimmed().isEmpty() || e.value("name").toString().size() > 128)
            return QStringLiteral("环境名称或ID无效。");
        ids.insert(e.value("id").toString());
        const auto error = HttpProjectStore::validateVariables(e.value("variables").toArray());
        if (!error.isEmpty())
            return error;
    }
    const auto variableError = HttpProjectStore::validateVariables(p.value("variables").toArray());
    if (!variableError.isEmpty())
        return variableError;
    const auto auth = p.value("auth").toObject();
    if (!QStringList{"none", "bearer", "basic"}.contains(auth.value("kind").toString("none")))
        return QStringLiteral("项目认证方式无效。");
    for (const auto *k : {"token", "username", "password"})
        if (auth.value(k).toString().size() > 16384)
            return QStringLiteral("认证字段超过限制。");
    return {};
}
} // namespace
HttpProjectStore::HttpProjectStore(QSettings *settings) : settings_(settings) {
    QByteArray bytes;
    if (settings_)
        bytes = settings_->value("manual/httpProjectsV2").toByteArray();
    if (!bytes.isEmpty()) {
        const auto document =
            bytes.size() <= 8 * 1024 * 1024 ? QJsonDocument::fromJson(bytes).object() : QJsonObject{};
        auto candidate = document.value("projects").toArray();
        bool valid = bytes.size() <= 8 * 1024 * 1024 && document.value("schemaVersion").toInt() == 2 &&
                     !candidate.isEmpty() && candidate.size() <= 64;
        QSet<QString> ids;
        for (const auto &value : candidate) {
            const auto p = value.toObject();
            valid &= value.isObject() && checkProject(p).isEmpty() &&
                     !ids.contains(p.value("id").toString());
            ids.insert(p.value("id").toString());
        }
        if (valid) {
            projects_ = candidate;
            requests_ = document.value("requests").toArray();
            bool requestsValid =
                (!document.contains("requests") || document.value("requests").isArray()) &&
                requests_.size() <= 512;
            QSet<QString> requestIds;
            for (const auto &value : requests_) {
                const auto r = value.toObject();
                requestsValid &= value.isObject() && r.value("schemaVersion").toInt() == 1 &&
                                 r.value("kind").toString() == "http" && r.value("params").isObject() &&
                                 !r.value("id").toString().isEmpty() &&
                                 !requestIds.contains(r.value("id").toString()) &&
                                 ids.contains(r.value("projectId").toString()) &&
                                 (!r.contains("assertions") || r.value("assertions").isArray()) &&
                                 HttpAssertions::validate(r.value("assertions").toArray()).isEmpty();
                requestIds.insert(r.value("id").toString());
            }
            if (!requestsValid) {
                readOnly_ = true;
                loadError_ = QStringLiteral("项目请求库损坏；原配置已保留，未覆盖。");
                requests_ = {};
            }
        } else {
            readOnly_ = true;
            loadError_ = QStringLiteral("项目配置损坏或版本不支持；原配置已保留，未覆盖。");
        }
    }
    if (projects_.isEmpty())
        projects_.append(newProject(QStringLiteral("默认项目")));
    project_ = projects_.first().toObject().value("id").toString();
    environment_ = project().value("environments").toArray().first().toObject().value("id").toString();
    if (bytes.isEmpty() && settings_) {
        const auto legacy = settings_->value("manual/httpLibrary").toByteArray();
        const auto doc = QJsonDocument::fromJson(legacy).object();
        if (!legacy.isEmpty() &&
            (legacy.size() > 8 * 1024 * 1024 || doc.value("schemaVersion").toInt() != 1 ||
             doc.value("entries").toArray().size() > 64)) {
            readOnly_ = true;
            loadError_ = QStringLiteral("旧请求库格式无效；原数据已保留，未迁移。");
        } else {
            for (const auto &value : doc.value("entries").toArray()) {
                auto r = value.toObject();
                if (r.value("kind").toString() != "http" || r.value("schemaVersion").toInt() != 1) {
                    readOnly_ = true;
                    loadError_ = QStringLiteral("旧请求库含无效请求；原数据已保留。");
                    break;
                }
                r["projectId"] = project_;
                requests_.append(r);
            }
            if (!readOnly_)
                commit(projects_, &loadError_);
        }
    }
    if (settings_) {
        const auto wanted = settings_->value("manual/httpSelectedProject").toString();
        if (projectIndex(wanted) >= 0) {
            project_ = wanted;
            environment_ =
                project().value("environments").toArray().first().toObject().value("id").toString();
        }
        const auto wantedEnvironment = settings_->value("manual/httpSelectedEnvironment").toString();
        for (const auto &e : project().value("environments").toArray())
            if (e.toObject().value("id").toString() == wantedEnvironment)
                environment_ = wantedEnvironment;
    }
}
int HttpProjectStore::projectIndex(const QString &id) const {
    for (int i = 0; i < projects_.size(); ++i)
        if (projects_[i].toObject().value("id").toString() == id)
            return i;
    return -1;
}
QJsonObject HttpProjectStore::project() const {
    const int i = projectIndex(project_);
    return i >= 0 ? projects_[i].toObject() : QJsonObject{};
}
QJsonObject HttpProjectStore::environment() const {
    for (const auto &e : project().value("environments").toArray())
        if (e.toObject().value("id").toString() == environment_)
            return e.toObject();
    return {};
}
QString HttpProjectStore::runtimeKey(const QString &p, const QString &e) const { return p + "/" + e; }
bool HttpProjectStore::commit(QJsonArray candidate, QString *error, const QJsonArray *requests) {
    if (error)
        error->clear();
    if (readOnly_)
        return fail(error, loadError_);
    auto entries = requests ? *requests : requests_;
    if (entries.size() > 512)
        return fail(error, QStringLiteral("HTTP请求库最多512个请求。"));
    QSet<QString> requestIds;
    for (const auto &v : entries) {
        const auto r = v.toObject();
        if (!v.isObject() || r.value("schemaVersion").toInt() != 1 ||
            r.value("kind").toString() != "http" || !r.value("params").isObject() ||
            r.value("id").toString().isEmpty() || r.value("id").toString().size() > 128 ||
            requestIds.contains(r.value("id").toString()))
            return fail(error, QStringLiteral("HTTP请求类型、版本或ID无效。"));
        if ((r.contains("assertions") && !r.value("assertions").isArray()) ||
            !HttpAssertions::validate(r.value("assertions").toArray()).isEmpty())
            return fail(error, QStringLiteral("HTTP请求断言格式无效。"));
        requestIds.insert(r.value("id").toString());
    }
    if (candidate.isEmpty() || candidate.size() > 64)
        return fail(error, QStringLiteral("须保留1–64个项目。"));
    QSet<QString> ids;
    for (const auto &v : candidate) {
        const auto p = v.toObject();
        const auto why = checkProject(p);
        if (!why.isEmpty())
            return fail(error, why);
        if (ids.contains(p.value("id").toString()))
            return fail(error, QStringLiteral("项目ID重复。"));
        ids.insert(p.value("id").toString());
    }
    for (int i = 0; i < entries.size(); ++i) {
        auto r = entries[i].toObject();
        if (!r.contains("projectId"))
            r["projectId"] = project_;
        if (!ids.contains(r.value("projectId").toString()) || r.value("prototypeOnly").toBool())
            return fail(error, QStringLiteral("请求项目不存在或属于模拟配置。"));
        entries[i] = httpTemplate::safeRequest(r);
    }
    auto safe = candidate;
    for (int i = 0; i < safe.size(); ++i) {
        auto p = safe[i].toObject();
        auto sanitize = [](QJsonArray rows) {
            for (int k = 0; k < rows.size(); ++k) {
                auto row = rows[k].toObject();
                if (row.value("secret").toBool() || secretName(row.value("name").toString())) {
                    row["secret"] = true;
                    row.remove("value");
                }
                rows[k] = row;
            }
            return rows;
        };
        p["variables"] = sanitize(p.value("variables").toArray());
        auto envs = p.value("environments").toArray();
        for (int k = 0; k < envs.size(); ++k) {
            auto e = envs[k].toObject();
            e["variables"] = sanitize(e.value("variables").toArray());
            envs[k] = e;
        }
        p["environments"] = envs;
        auto auth = p.value("auth").toObject();
        for (const auto *k : {"token", "password"}) {
            auto text = auth.value(k).toString();
            if (!text.isEmpty() && !httpTemplate::referenceOnly(text))
                auth[k] = QStringLiteral("[已遮蔽]");
        }
        p["auth"] = auth;
        safe[i] = p;
    }
    const auto bytes =
        QJsonDocument(QJsonObject{{"schemaVersion", 2}, {"projects", safe}, {"requests", entries}})
            .toJson(QJsonDocument::Compact);
    if (bytes.size() > 8 * 1024 * 1024)
        return fail(error, QStringLiteral("项目和请求定义合计超过8MiB。"));
    if (settings_) {
        const auto previous = settings_->value("manual/httpProjectsV2");
        settings_->setValue("manual/httpProjectsV2", bytes);
        settings_->sync();
        if (settings_->status() != QSettings::NoError) {
            settings_->setValue("manual/httpProjectsV2", previous);
            settings_->sync();
            return fail(error, QStringLiteral("项目写入失败，请检查配置目录。"));
        }
    }
    projects_ = candidate;
    requests_ = entries;
    ++revision_;
    return true;
}
bool HttpProjectStore::setRequests(const QJsonArray &requests, QString *error) {
    return commit(projects_, error, &requests);
}
bool HttpProjectStore::replaceProject(QJsonObject value, QString *error) {
    auto candidate = projects_;
    const int index = projectIndex(value.value("id").toString());
    if (index < 0)
        return fail(error, QStringLiteral("项目已不存在。"));
    candidate[index] = value;
    return commit(candidate, error);
}
bool HttpProjectStore::selectProject(const QString &id, QString *error) {
    const int index = projectIndex(id);
    if (index < 0)
        return fail(error, QStringLiteral("项目不存在。"));
    if (projectIndex(project_) >= 0)
        selectedEnvironments_[project_] = environment_;
    project_ = id;
    environment_ = project().value("environments").toArray().first().toObject().value("id").toString();
    const auto wanted = selectedEnvironments_.value(
        id, settings_ ? settings_->value("manual/httpEnvironment/" + id).toString() : QString());
    for (const auto &e : project().value("environments").toArray())
        if (e.toObject().value("id").toString() == wanted)
            environment_ = wanted;
    ++revision_;
    if (settings_)
        settings_->setValue("manual/httpSelectedProject", id);
    return true;
}
bool HttpProjectStore::selectEnvironment(const QString &id, QString *error) {
    for (const auto &e : project().value("environments").toArray())
        if (e.toObject().value("id").toString() == id) {
            environment_ = id;
            selectedEnvironments_[project_] = id;
            ++revision_;
            if (settings_) {
                settings_->setValue("manual/httpSelectedEnvironment", id);
                settings_->setValue("manual/httpEnvironment/" + project_, id);
            }
            return true;
        }
    return fail(error, QStringLiteral("环境不存在。"));
}
bool HttpProjectStore::createProject(const QString &name, QString *error) {
    auto p = newProject(name.trimmed());
    auto candidate = projects_;
    candidate.append(p);
    if (!commit(candidate, error))
        return false;
    return selectProject(p.value("id").toString(), error);
}
bool HttpProjectStore::renameProject(const QString &name, QString *error) {
    auto p = project();
    p["name"] = name.trimmed();
    return replaceProject(p, error);
}
bool HttpProjectStore::removeProject(QString *error) {
    if (projects_.size() <= 1)
        return fail(error, QStringLiteral("至少保留一个项目。"));
    auto candidate = projects_;
    candidate.removeAt(projectIndex(project_));
    const auto old = project_;
    auto entries = requests_;
    for (int i = entries.size() - 1; i >= 0; --i)
        if (entries[i].toObject().value("projectId").toString() == old)
            entries.removeAt(i);
    if (!commit(candidate, error, &entries))
        return false;
    selectedEnvironments_.remove(old);
    for (auto it = runtime_.begin(); it != runtime_.end();)
        if (it.key().startsWith(old + "/"))
            it = runtime_.erase(it);
        else
            ++it;
    return selectProject(projects_.first().toObject().value("id").toString(), error);
}
bool HttpProjectStore::copyEnvironment(const QString &name, QString *error) {
    auto e = environment();
    e["id"] = uid();
    e["name"] = name.trimmed();
    auto variables = e.value("variables").toArray();
    for (int i = 0; i < variables.size(); ++i) {
        auto r = variables[i].toObject();
        if (r.value("secret").toBool() || secretName(r.value("name").toString()))
            r.remove("value");
        variables[i] = r;
    }
    e["variables"] = variables;
    auto p = project();
    auto environments = p.value("environments").toArray();
    environments.append(e);
    p["environments"] = environments;
    if (!replaceProject(p, error))
        return false;
    return selectEnvironment(e.value("id").toString(), error);
}
bool HttpProjectStore::createEnvironment(const QString &name, QString *error) {
    auto p = project();
    auto envs = p.value("environments").toArray();
    auto e = newEnvironment(name.trimmed());
    envs.append(e);
    p["environments"] = envs;
    if (!replaceProject(p, error))
        return false;
    return selectEnvironment(e.value("id").toString(), error);
}
bool HttpProjectStore::removeEnvironment(QString *error) {
    auto p = project();
    auto envs = p.value("environments").toArray();
    if (envs.size() <= 1)
        return fail(error, QStringLiteral("至少保留一个环境。"));
    for (int i = 0; i < envs.size(); ++i)
        if (envs[i].toObject().value("id").toString() == environment_)
            envs.removeAt(i);
    p["environments"] = envs;
    const auto old = runtimeKey(project_, environment_);
    if (!replaceProject(p, error))
        return false;
    runtime_.remove(old);
    return selectEnvironment(envs.first().toObject().value("id").toString(), error);
}
QString HttpProjectStore::validateVariables(const QJsonArray &rows) {
    if (rows.size() > 256)
        return QStringLiteral("最多256个变量。");
    QSet<QString> names;
    qint64 bytes = 0;
    static QRegularExpression identifier("^[A-Za-z_][A-Za-z0-9_]{0,127}$");
    for (const auto &v : rows) {
        const auto r = v.toObject();
        const auto name = r.value("name").toString();
        if (!v.isObject() || !identifier.match(name).hasMatch() || names.contains(name))
            return QStringLiteral("变量名称须为唯一的字母/数字/下划线标识。");
        names.insert(name);
        const auto value = r.value("value");
        if (!value.isUndefined() && !workflowDetail::boundedJson(value, 256 * 1024))
            return QStringLiteral("单个变量超过256KiB或深度限制。");
        bytes += scalar(value).size() * 2LL;
        if (bytes > 1024 * 1024)
            return QStringLiteral("变量合计超过1MiB。");
    }
    return {};
}
bool HttpProjectStore::renameEnvironment(const QString &name, QString *error) {
    auto p = project();
    auto envs = p.value("environments").toArray();
    for (int i = 0; i < envs.size(); ++i) {
        auto e = envs[i].toObject();
        if (e.value("id").toString() == environment_) {
            e["name"] = name.trimmed();
            envs[i] = e;
        }
    }
    p["environments"] = envs;
    return replaceProject(p, error);
}
bool HttpProjectStore::setProjectVariables(const QJsonArray &variables, QString *error) {
    const auto why = validateVariables(variables);
    if (!why.isEmpty())
        return fail(error, why);
    auto p = project();
    auto definitions = variables;
    auto runtime = runtimeVariables();
    for (int i = 0; i < definitions.size(); ++i) {
        auto r = definitions[i].toObject();
        const auto name = r.value("name").toString();
        if (r.value("secret").toBool() || secretName(name) ||
            runtime.value(name).toObject().value("responseDerived").toBool()) {
            r["secret"] = true;
            if (r.contains("value"))
                runtime[name] =
                    QJsonObject{{"value", r.value("value")},
                                {"secret", true},
                                {"responseDerived",
                                 runtime.value(name).toObject().value("responseDerived").toBool()},
                                {"updated", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
            r.remove("value");
        }
        definitions[i] = r;
    }
    const auto runtimeLimit = runtimeError(runtime);
    if (!runtimeLimit.isEmpty())
        return fail(error, runtimeLimit);
    QJsonObject total;
    for (auto it = runtime_.begin(); it != runtime_.end(); ++it)
        total[it.key()] = it.value();
    total[runtimeKey(project_, environment_)] = runtime;
    if (!workflowDetail::boundedJson(total, 16 * 1024 * 1024))
        return fail(error, QStringLiteral("所有运行变量超过容量限制。"));
    p["variables"] = definitions;
    if (!replaceProject(p, error))
        return false;
    runtime_[runtimeKey(project_, environment_)] = runtime;
    return true;
}
bool HttpProjectStore::setEnvironmentVariables(const QJsonArray &variables, QString *error) {
    QSet<QString> previousNames;
    for (const auto &r : environment().value("variables").toArray())
        previousNames.insert(r.toObject().value("name").toString());
    const auto why = validateVariables(variables);
    if (!why.isEmpty())
        return fail(error, why);
    auto p = project();
    auto envs = p.value("environments").toArray();
    QJsonArray definitions = variables;
    auto runtime = runtime_.value(runtimeKey(project_, environment_));
    for (int i = 0; i < definitions.size(); ++i) {
        auto r = definitions[i].toObject();
        const auto name = r.value("name").toString();
        if (r.value("secret").toBool() || secretName(name) ||
            runtime.value(name).toObject().value("responseDerived").toBool()) {
            r["secret"] = true;
            if (r.contains("value"))
                runtime[name] =
                    QJsonObject{{"value", r.value("value")},
                                {"secret", true},
                                {"responseDerived",
                                 runtime.value(name).toObject().value("responseDerived").toBool()},
                                {"updated", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
            r.remove("value");
        } else
            runtime.remove(name);
        definitions[i] = r;
    }
    for (int i = 0; i < envs.size(); ++i) {
        auto e = envs[i].toObject();
        if (e.value("id").toString() == environment_) {
            e["variables"] = definitions;
            envs[i] = e;
        }
    }
    p["environments"] = envs;
    QSet<QString> keep;
    for (const auto &r : variables)
        keep.insert(r.toObject().value("name").toString());
    for (auto it = runtime.begin(); it != runtime.end();)
        if (previousNames.contains(it.key()) && !keep.contains(it.key()))
            it = runtime.erase(it);
        else
            ++it;
    const auto runtimeLimit = runtimeError(runtime);
    if (!runtimeLimit.isEmpty())
        return fail(error, runtimeLimit);
    auto global = runtime_;
    global[runtimeKey(project_, environment_)] = runtime;
    QJsonObject total;
    for (auto it = global.begin(); it != global.end(); ++it)
        total[it.key()] = it.value();
    if (!workflowDetail::boundedJson(total, 16 * 1024 * 1024))
        return fail(error, QStringLiteral("所有环境运行值合计超过16MiB或条目限制。"));
    if (!replaceProject(p, error))
        return false;
    runtime_[runtimeKey(project_, environment_)] = runtime;
    return true;
}
bool HttpProjectStore::setProjectAuth(const QJsonObject &auth, QString *error) {
    auto p = project();
    p["auth"] = auth;
    return replaceProject(p, error);
}
bool HttpProjectStore::renameFolder(const QString &from, const QString &to, QString *error) {
    auto p = project();
    auto folders = p.value("folders").toArray();
    bool found = false;
    for (int i = 0; i < folders.size(); ++i)
        if (folders[i].toString() == from) {
            folders[i] = to.trimmed();
            found = true;
        }
    if (!found)
        return fail(error, QStringLiteral("文件夹已不存在。"));
    p["folders"] = folders;
    auto candidate = projects_;
    candidate[projectIndex(project_)] = p;
    auto requests = requests_;
    for (int i = 0; i < requests.size(); ++i) {
        auto r = requests[i].toObject();
        if (r.value("projectId").toString() == project_ && r.value("folder").toString() == from) {
            r["folder"] = to.trimmed();
            requests[i] = r;
        }
    }
    return commit(candidate, error, &requests);
}
bool HttpProjectStore::removeFolder(const QString &name, QString *error) {
    auto p = project();
    auto folders = p.value("folders").toArray();
    bool found = false;
    for (int i = folders.size() - 1; i >= 0; --i)
        if (folders[i].toString() == name) {
            folders.removeAt(i);
            found = true;
        }
    if (!found)
        return fail(error, QStringLiteral("文件夹已不存在。"));
    p["folders"] = folders;
    auto candidate = projects_;
    candidate[projectIndex(project_)] = p;
    auto requests = requests_;
    for (int i = 0; i < requests.size(); ++i) {
        auto r = requests[i].toObject();
        if (r.value("projectId").toString() == project_ && r.value("folder").toString() == name) {
            r["folder"] = "";
            requests[i] = r;
        }
    }
    return commit(candidate, error, &requests);
}
bool HttpProjectStore::addFolder(const QString &name, QString *error) {
    auto p = project();
    auto folders = p.value("folders").toArray();
    folders.append(name.trimmed());
    p["folders"] = folders;
    return replaceProject(p, error);
}
QJsonObject HttpProjectStore::runtimeVariables() const {
    return runtime_.value(runtimeKey(project_, environment_));
}
QJsonObject HttpProjectStore::effectiveVariables() const {
    QJsonObject values;
    for (const auto &r : project().value("variables").toArray()) {
        const auto row = r.toObject();
        if (row.contains("value"))
            values[row.value("name").toString()] = row.value("value");
    }
    for (const auto &r : environment().value("variables").toArray()) {
        const auto row = r.toObject();
        if (row.contains("value"))
            values[row.value("name").toString()] = row.value("value");
    }
    const auto current = runtimeVariables();
    for (auto it = current.begin(); it != current.end(); ++it)
        values[it.key()] = it.value().toObject().value("value");
    return values;
}
void HttpProjectStore::clearRuntimeVariable(const QString &name) {
    auto &values = runtime_[runtimeKey(project_, environment_)];
    if (values.contains(name)) {
        values.remove(name);
        ++revision_;
    }
}
void HttpProjectStore::clearRuntime() {
    runtime_.remove(runtimeKey(project_, environment_));
    ++revision_;
}
QString HttpProjectStore::expandUrl(const QString &source, QString *error) const {
    QString localError;
    if (!error)
        error = &localError;
    if (error)
        error->clear();
    if (source.size() > 4096) {
        fail(error, QStringLiteral("URL模板超过4096字符。"));
        return {};
    }
    static QRegularExpression placeholder("\\{\\{[^{}]+\\}\\}");
    auto matches = placeholder.globalMatch(source);
    QString result;
    int offset = 0;
    while (matches.hasNext()) {
        const auto match = matches.next();
        result += source.mid(offset, match.capturedStart() - offset);
        const auto value = expand(match.captured(), error);
        if (error && !error->isEmpty())
            return {};
        const int scheme = result.indexOf("://"),
                  path = scheme < 0 ? -1 : result.indexOf('/', scheme + 3), query = result.indexOf('?');
        if (scheme < 0)
            result += value;
        else if (path < 0 && query < 0) {
            if (value.contains(QRegularExpression("[/@?#\\\\\\s\\x00-\\x1f]"))) {
                fail(error, QStringLiteral("主机/端口变量不能包含路径、认证或控制字符。"));
                return {};
            }
            result += value;
        } else
            result += QString::fromLatin1(QUrl::toPercentEncoding(value));
        offset = match.capturedEnd();
        if (result.size() > 4096) {
            fail(error, QStringLiteral("URL变量展开超过4096字符。"));
            return {};
        }
    }
    result += source.mid(offset);
    if (result.contains("{{")) {
        fail(error, QStringLiteral("URL变量占位符无效。"));
        return {};
    }
    return result;
}
QString HttpProjectStore::expand(const QString &input, QString *error, bool json) const {
    if (error)
        error->clear();
    if (input.size() > 1024 * 1024) {
        fail(error, QStringLiteral("模板超过1Mi字符。"));
        return {};
    }
    const auto variables = effectiveVariables();
    bool failed = false;
    int steps = 0;
    auto reject = [&](const QString &why) {
        failed = true;
        fail(error, why);
    };
    static QRegularExpression nameRe("^[A-Za-z_][A-Za-z0-9_]{0,127}$");
    std::function<QString(const QString &, bool, QSet<QString>)> render;
    render = [&](const QString &source, bool asJson, QSet<QString> chain) {
        QString result;
        bool quoted = false, escaped = false;
        for (int i = 0; i < source.size() && !failed;) {
            if (source.mid(i, 2) == "{{") {
                const int end = source.indexOf("}}", i + 2);
                if (end < 0) {
                    reject(QStringLiteral("变量占位符未闭合。"));
                    break;
                }
                const auto name = source.mid(i + 2, end - i - 2).trimmed();
                if (!nameRe.match(name).hasMatch() || !variables.contains(name)) {
                    reject(QStringLiteral("未定义变量：") + name);
                    break;
                }
                if (chain.contains(name) || chain.size() >= 16 || ++steps > 4096) {
                    reject(QStringLiteral("变量循环引用或展开次数超过限制：") + name);
                    break;
                }
                if (asJson && quoted && escaped) {
                    reject(QStringLiteral("JSON占位符不能紧接单个反斜杠。"));
                    break;
                }
                auto value = variables.value(name);
                auto nested = chain;
                nested.insert(name);
                if (value.isString() && value.toString().contains("{{"))
                    value = render(value.toString(), false, nested);
                else if ((value.isObject() || value.isArray()) && scalar(value).contains("{{")) {
                    const auto expanded = render(scalar(value), true, nested);
                    const auto doc = QJsonDocument::fromJson(expanded.toUtf8());
                    if (doc.isObject())
                        value = doc.object();
                    else if (doc.isArray())
                        value = doc.array();
                    else
                        reject(QStringLiteral("变量内部JSON展开无效：") + name);
                }
                if (failed)
                    break;
                result += asJson ? (quoted ? jsonEscape(scalar(value))
                                           : (value.isString()
                                                  ? QString('"') + jsonEscape(value.toString()) + '"'
                                                  : scalar(value)))
                                 : scalar(value);
                i = end + 2;
            } else {
                const auto c = source[i++];
                result += c;
                if (asJson) {
                    if (escaped)
                        escaped = false;
                    else if (c == '\\' && quoted)
                        escaped = true;
                    else if (c == '"')
                        quoted = !quoted;
                }
            }
            if (result.size() > 4 * 1024 * 1024)
                reject(QStringLiteral("变量展开超过4Mi字符。"));
        }
        return failed ? QString() : result;
    };
    return render(input, json, {});
}
QString HttpProjectStore::validateRules(const QJsonArray &rules) {
    if (rules.size() > 32)
        return QStringLiteral("最多32条提取规则。");
    QJsonArray names;
    for (const auto &v : rules) {
        const auto r = v.toObject();
        if (!v.isObject() ||
            !QStringList{"json", "header"}.contains(r.value("source").toString("json")) ||
            r.value("path").toString().isEmpty() || r.value("path").toString().size() > 512)
            return QStringLiteral("提取来源/路径无效。");
        names.append(QJsonObject{{"name", r.value("variable")}});
        if (r.value("source").toString("json") == "json") {
            QString path = r.value("path").toString();
            path.replace(QRegularExpression("\\[([0-9]+)\\]"), ".\\1");
            if (!workflowDetail::safePath(path))
                return QStringLiteral("字段路径只支持点字段与非负数组索引。");
        }
    }
    return validateVariables(names);
}
bool HttpProjectStore::extract(const QJsonObject &response, const QJsonArray &rules, const QString &pid,
                               const QString &eid, QString *error) {
    if (error)
        error->clear();
    if (rules.isEmpty())
        return true;
    const auto why = validateRules(rules);
    if (!why.isEmpty())
        return fail(error, why);
    if (response.value("status").toInt() < 200 || response.value("status").toInt() >= 300)
        return fail(error, QStringLiteral("HTTP非2xx，未更新变量。"));
    const int pi = projectIndex(pid);
    if (pi < 0)
        return fail(error, QStringLiteral("原请求项目已删除，未更新变量。"));
    bool exists = false;
    for (const auto &e : projects_[pi].toObject().value("environments").toArray())
        exists |= e.toObject().value("id").toString() == eid;
    if (!exists)
        return fail(error, QStringLiteral("原请求环境已删除，未更新变量。"));
    auto candidate = runtime_.value(runtimeKey(pid, eid));
    for (const auto &v : rules) {
        const auto r = v.toObject();
        bool ok = false;
        QJsonValue value;
        auto path = r.value("path").toString();
        if (r.value("source").toString("json") == "header") {
            const auto headers = response.value("headers").toObject();
            for (auto it = headers.begin(); it != headers.end(); ++it)
                if (it.key().compare(path, Qt::CaseInsensitive) == 0) {
                    value = it.value();
                    ok = true;
                }
        } else {
            path.replace(QRegularExpression("\\[([0-9]+)\\]"), ".\\1");
            value = workflowDetail::lookup(response.value("body"), path, &ok);
        }
        if (!ok || value.isUndefined() || value.isNull())
            return fail(error, QStringLiteral("提取失败，保留原值：") + r.value("variable").toString());
        candidate[r.value("variable").toString()] = QJsonObject{
            {"value", value},
            {"responseDerived", true},
            {"secret", r.value("secret").toBool(true) || secretName(r.value("variable").toString())},
            {"updated", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    }
    QJsonArray rows;
    for (auto it = candidate.begin(); it != candidate.end(); ++it)
        rows.append(QJsonObject{{"name", it.key()}, {"value", it.value().toObject().value("value")}});
    const auto limit = validateVariables(rows);
    if (!limit.isEmpty())
        return fail(error, limit);
    auto global = runtime_;
    global[runtimeKey(pid, eid)] = candidate;
    QJsonObject total;
    for (auto it = global.begin(); it != global.end(); ++it)
        total[it.key()] = it.value();
    if (!workflowDetail::boundedJson(total, 16 * 1024 * 1024))
        return fail(error, QStringLiteral("所有环境运行值合计超过16MiB或条目限制。"));
    runtime_[runtimeKey(pid, eid)] = candidate;
    ++revision_;
    return true;
}
QJsonObject HttpProjectStore::exportProject(const QJsonArray &requests) const {
    auto p = project();
    auto sanitize = [](QJsonArray rows) {
        for (int i = 0; i < rows.size(); ++i) {
            auto r = rows[i].toObject();
            if (r.value("secret").toBool() || secretName(r.value("name").toString()))
                r.remove("value");
            rows[i] = r;
        }
        return rows;
    };
    p["variables"] = sanitize(p.value("variables").toArray());
    auto envs = p.value("environments").toArray();
    for (int i = 0; i < envs.size(); ++i) {
        auto e = envs[i].toObject();
        e["variables"] = sanitize(e.value("variables").toArray());
        envs[i] = e;
    }
    p["environments"] = envs;
    auto auth = p.value("auth").toObject();
    for (const auto *k : {"token", "password"})
        if (!httpTemplate::referenceOnly(auth.value(k).toString()) &&
            !auth.value(k).toString().isEmpty())
            auth[k] = QStringLiteral("[已遮蔽]");
    p["auth"] = auth;
    return {{"schemaVersion", 2},
            {"type", "portbridge-http-project"},
            {"project", p},
            {"requests",
             [&] {
                 QJsonArray safe;
                 for (const auto &r : requests)
                     safe.append(httpTemplate::safeRequest(r.toObject()));
                 return safe;
             }()},
            {"secretsExcluded", true}};
}
bool HttpProjectStore::importProject(const QJsonObject &document, QJsonArray *requests,
                                     QString *error) {
    if (document.value("prototypeOnly").toBool() || !document.value("requests").isArray() ||
        document.value("schemaVersion").toInt() != 2 ||
        document.value("type").toString() != "portbridge-http-project" ||
        QJsonDocument(document).toJson(QJsonDocument::Compact).size() > 8 * 1024 * 1024)
        return fail(error, QStringLiteral("项目文件版本、类型或大小无效。"));
    auto p = document.value("project").toObject();
    auto variables = p.value("variables").toArray();
    for (int i = 0; i < variables.size(); ++i) {
        auto r = variables[i].toObject();
        if (r.value("secret").toBool() || secretName(r.value("name").toString()))
            r.remove("value");
        variables[i] = r;
    }
    p["variables"] = variables;
    const auto why = checkProject(p);
    if (!why.isEmpty())
        return fail(error, why);
    if (document.value("requests").toArray().size() > 512)
        return fail(error, QStringLiteral("项目请求超过512条。"));
    p["id"] = uid();
    auto envs = p.value("environments").toArray();
    for (int i = 0; i < envs.size(); ++i) {
        auto e = envs[i].toObject();
        e["id"] = uid();
        auto vars = e.value("variables").toArray();
        for (int k = 0; k < vars.size(); ++k) {
            auto row = vars[k].toObject();
            if (row.value("secret").toBool() || secretName(row.value("name").toString()))
                row.remove("value");
            vars[k] = row;
        }
        e["variables"] = vars;
        envs[i] = e;
    }
    p["environments"] = envs;
    auto candidate = projects_;
    candidate.append(p);
    auto incoming = document.value("requests").toArray();
    auto allRequests = requests_;
    for (int i = 0; i < incoming.size(); ++i) {
        auto r = incoming[i].toObject();
        r["id"] = uid();
        r["projectId"] = p.value("id");
        incoming[i] = r;
        allRequests.append(r);
    }
    if (!commit(candidate, error, &allRequests))
        return false;
    selectProject(p.value("id").toString());
    if (requests)
        *requests = incoming;
    return true;
}
} // namespace portbridge
