#include "http_sequence_runner.hpp"
#include "http_request_resolver.hpp"
#include <QDateTime>
#include <QJsonDocument>
#include <QUuid>
namespace portbridge {
HttpSequenceRunner::HttpSequenceRunner(HttpProjectStore *store, ProtocolDebugSession *session,
                                       QObject *parent)
    : QObject(parent), store_(store), session_(session) {
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this] { stop(); });
    connect(session_, &ProtocolDebugSession::changed, this, [this] {
        const auto id = id_;
        QTimer::singleShot(0, this, [this, id] {
            if (running_ && id == id_)
                observe();
        });
    });
}
HttpSequenceRunner::~HttpSequenceRunner() {
    if (running_) {
        running_ = false;
        session_->cancel();
    }
}
QJsonArray HttpSequenceRunner::extractionRules(const QJsonObject &request, QString *error) {
    QJsonArray result;
    const auto rows = request.value("extractionRows");
    if (!rows.isUndefined() && !rows.isArray()) {
        if (error)
            *error = QStringLiteral("提取设置须为数组。");
        return {};
    }
    if (rows.toArray().size() > 32) {
        if (error)
            *error = QStringLiteral("最多32行提取规则。");
        return {};
    }
    for (const auto &v : rows.toArray()) {
        const auto r = v.toObject();
        if (!v.isObject() || !r.value("key").isString() || !r.value("value").isString()) {
            if (error)
                *error = QStringLiteral("提取规则格式无效。");
            return {};
        }
        if (!r.value("enabled").toBool(true))
            continue;
        const auto path = r.value("value").toString();
        result.append(QJsonObject{{"variable", r.value("key")},
                                  {"source", path.startsWith("header:") ? "header" : "json"},
                                  {"path", path.startsWith("header:") ? path.mid(7) : path},
                                  {"secret", true}});
    }
    const auto why = HttpProjectStore::validateRules(result);
    if (error)
        *error = why;
    return why.isEmpty() ? result : QJsonArray{};
}
bool HttpSequenceRunner::start(const QJsonArray &requests, bool continueOnFailure, QString *error) {
    QString local;
    if (!error)
        error = &local;
    error->clear();
    auto fail = [&](const QString &text) {
        *error = text;
        return false;
    };
    if (running_ || session_->active())
        return fail(QStringLiteral("请先结束当前HTTP活动。"));
    if (requests.isEmpty() || requests.size() > 128 ||
        QJsonDocument(requests).toJson(QJsonDocument::Compact).size() > 8 * 1024 * 1024)
        return fail(QStringLiteral("顺序联调要求1–128个请求，模板合计最多8MiB。"));
    for (const auto &value : requests) {
        const auto r = value.toObject();
        if (!value.isObject() || r.value("projectId").toString() != store_->projectId() ||
            r.value("id").toString().isEmpty() || r.value("kind").toString() != "http" ||
            r.value("schemaVersion").toInt() != 1)
            return fail(QStringLiteral("只能执行当前项目的已保存HTTP请求。"));
        if (r.contains("assertions") && !r.value("assertions").isArray())
            return fail(QStringLiteral("断言须为数组。"));
        const auto why = HttpAssertions::validate(r.value("assertions").toArray());
        if (!why.isEmpty())
            return fail(why);
        extractionRules(r, error);
        if (!error->isEmpty())
            return false;
    }
    // Validate the first request before reserving resources; later requests may need login output.
    const auto first = resolveHttpRequest(requests.first().toObject(), *store_, error);
    if (first.isEmpty() || !error->isEmpty())
        return false;
    HttpAssertions::resolve(requests.first().toObject().value("assertions").toArray(), *store_, error);
    if (!error->isEmpty())
        return false;
    id_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    project_ = store_->projectId();
    environment_ = store_->environmentId();
    revision_ = store_->revision();
    requests_ = requests;
    results_ = {};
    index_ = 0;
    waiting_ = false;
    continue_ = continueOnFailure;
    cancelled_ = false;
    elapsed_.start();
    running_ = true;
    if (!session_->acquireSequence(id_, error)) {
        running_ = false;
        return false;
    }
    deadline_.start(600000);
    emit changed();
    next();
    return true;
}
void HttpSequenceRunner::next() {
    if (!running_)
        return;
    if (session_->sequenceId() != id_) {
        finish(true);
        return;
    }
    if (index_ >= requests_.size()) {
        finish(false);
        return;
    }
    if (project_ != store_->projectId() || environment_ != store_->environmentId() ||
        revision_ != store_->revision()) {
        complete(false, QStringLiteral("项目/环境/变量已变更，运行已终止。"));
        finish(true);
        return;
    }
    QString error;
    const auto request = requests_[index_].toObject();
    auto parameters = resolveHttpRequest(request, *store_, &error);
    if (error.isEmpty())
        currentAssertions_ =
            HttpAssertions::resolve(request.value("assertions").toArray(), *store_, &error);
    if (error.isEmpty())
        currentExtraction_ = extractionRules(request, &error);
    if (!error.isEmpty() || parameters.isEmpty()) {
        complete(false, QStringLiteral("请求模板或规则验证失败；未发送。"));
        return;
    }
    parameters["httpSequenceId"] = id_;
    parameters["httpSequenceRequestId"] = request.value("id");
    previousOperation_ = session_->latestOperation();
    waiting_ = true;
    startingStep_ = true;
    const bool started = session_->start(parameters, &error);
    startingStep_ = false;
    if (!running_)
        return;
    if (!started) {
        waiting_ = false;
        complete(false, QStringLiteral("请求未开始：资源确认拒绝或上下文已变更。"));
        if (running_)
            finish(true);
        return;
    }
    epoch_ = session_->epoch();
    emit changed();
}
void HttpSequenceRunner::observe() {
    if (!running_ || startingStep_)
        return;
    if (session_->sequenceId() != id_) {
        finish(true);
        return;
    }
    if (!waiting_ || session_->phase() != ProtocolDebugSession::Phase::Idle)
        return;
    if (epoch_ != session_->epoch() || project_ != store_->projectId() ||
        environment_ != store_->environmentId() || revision_ != store_->revision()) {
        complete(false, QStringLiteral("请求上下文已过期，未更新运行变量。"));
        finish(true);
        return;
    }
    if (session_->latestOperation() == previousOperation_)
        return;
    waiting_ = false;
    const auto response = session_->latestResponse();
    if (!session_->lastError().isEmpty()) {
        complete(false, QStringLiteral("网络操作失败（原错误可在HTTP历史查看）。"));
        return;
    }
    const auto assertions = HttpAssertions::evaluate(response, currentAssertions_);
    QString extractionError;
    const bool extracted =
        store_->extract(response, currentExtraction_, project_, environment_, &extractionError);
    revision_ = store_->revision();
    const int status = response.value("status").toInt();
    const bool passed =
        status >= 200 && status < 300 && extracted && HttpAssertions::passed(assertions);
    complete(passed,
             passed                          ? QStringLiteral("请求与断言通过")
             : !extracted                    ? QStringLiteral("响应提取失败，原变量保留")
             : status < 200 || status >= 300 ? QStringLiteral("HTTP非2xx")
                                             : QStringLiteral("响应断言失败"),
             assertions, response);
}
void HttpSequenceRunner::complete(bool passed, const QString &message, const QJsonArray &assertions,
                                  const QJsonObject &response) {
    if (!running_ || index_ >= requests_.size())
        return;
    waiting_ = false;
    const auto request = requests_[index_].toObject();
    results_.append(QJsonObject{{"step", index_ + 1},
                                {"requestId", request.value("id")},
                                {"outcome", passed ? "passed" : "failed"},
                                {"status", response.value("status")},
                                {"elapsedMs", response.value("elapsedMs")},
                                {"bodyBytes", response.value("bodyBytes")},
                                {"message", message},
                                {"assertions", assertions}});
    ++index_;
    emit changed();
    if (!passed && !continue_) {
        finish(false);
        return;
    }
    const auto id = id_;
    QTimer::singleShot(0, this, [this, id] {
        if (running_ && id == id_)
            next();
    });
}
void HttpSequenceRunner::finish(bool cancelled) {
    if (!running_)
        return;
    running_ = false;
    deadline_.stop();
    cancelled_ = cancelled;
    if (waiting_ && index_ < requests_.size()) {
        const auto request = requests_[index_].toObject();
        results_.append(QJsonObject{{"step", index_ + 1},
                                    {"requestId", request.value("id")},
                                    {"outcome", "cancelled"},
                                    {"message", QStringLiteral("已停止；不能撤回已发送的数据")}});
        ++index_;
    }
    waiting_ = false;
    while (index_ < requests_.size()) {
        results_.append(QJsonObject{{"step", index_ + 1},
                                    {"requestId", requests_[index_].toObject().value("id")},
                                    {"outcome", "skipped"},
                                    {"message", QStringLiteral("未执行")}});
        ++index_;
    }
    if (session_->sequenceId() == id_) {
        if (session_->phase() != ProtocolDebugSession::Phase::Idle)
            session_->cancel();
        else
            session_->releaseSequence(id_);
    }
    emit changed();
}
void HttpSequenceRunner::stop() { finish(true); }
QJsonObject HttpSequenceRunner::report() const {
    QJsonObject counts{{"passed", 0}, {"failed", 0}, {"cancelled", 0}, {"skipped", 0}};
    for (const auto &result : results_) {
        const auto state = result.toObject().value("outcome").toString();
        counts[state] = counts.value(state).toInt() + 1;
    }
    return {{"schemaVersion", 1},
            {"type", "portbridge-http-sequence-result"},
            {"runId", id_},
            {"projectId", project_},
            {"environmentId", environment_},
            {"running", running_},
            {"cancelled", cancelled_},
            {"continueOnFailure", continue_},
            {"plannedSteps", requests_.size()},
            {"counts", counts},
            {"results", results_},
            {"valuesExcluded", true}};
}
} // namespace portbridge
