#include "http_project_panel.hpp"
#include "http_auth_presentation.hpp"
#include "http_browser_fingerprint.hpp"
#include "http_configuration_dialog.hpp"
#include <QComboBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
namespace portbridge {
namespace {
QPushButton *button(const QString &text, const char *name) {
    auto *b = new QPushButton(text);
    b->setObjectName(name);
    b->setProperty("textAction", true);
    return b;
}
QLabel *label(const QString &text, const char *name = nullptr) {
    auto *l = new QLabel(text);
    l->setTextFormat(Qt::PlainText);
    l->setWordWrap(true);
    if (name)
        l->setObjectName(name);
    return l;
}
} // namespace
HttpProjectPanel::HttpProjectPanel(HttpProjectStore *store, QWidget *parent)
    : QWidget(parent), store_(store) {
    setObjectName("httpProjectPanel");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(7);
    auto *heading = new QHBoxLayout;
    heading->addWidget(label(QStringLiteral("HTTP 项目")), 1);
    auto *management = new QToolButton;
    management->setText(QStringLiteral("管理"));
    management->setObjectName("httpProjectManage");
    management->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(management);
    management->setMenu(menu);
    heading->addWidget(management);
    layout->addLayout(heading);
    auto *purpose = label(QStringLiteral("项目收纳接口请求，共享环境与认证。"), "httpProjectPurpose");
    purpose->setProperty("muted", true);
    layout->addWidget(purpose);
    projects_ = new QComboBox;
    projects_->setObjectName("httpProjectChoice");
    layout->addWidget(projects_);
    auto *settings = button(QStringLiteral("项目设置"), "httpProjectSettings");
    layout->addWidget(settings);
    auto *folderRow = new QHBoxLayout;
    folders_ = new QComboBox;
    folders_->setObjectName("httpFolderFilter");
    folderRow->addWidget(folders_, 1);
    auto *folderAdd = button("+", "httpFolderAdd");
    folderAdd->setToolTip(QStringLiteral("新建请求分类"));
    folderAdd->setFixedWidth(28);
    folderRow->addWidget(folderAdd);
    layout->addLayout(folderRow);
    auto *envHeading = new QHBoxLayout;
    envHeading->addWidget(label(QStringLiteral("当前环境")), 1);
    auto *configure = button(QStringLiteral("配置环境"), "httpEnvironmentConfigure");
    envHeading->addWidget(configure);
    layout->addLayout(envHeading);
    environments_ = new QComboBox;
    environments_->setObjectName("httpEnvironmentChoice");
    environments_->setToolTip(
        QStringLiteral("选择变量上下文。请求用{{base_url}}引用服务地址；切换不会发送或自动登录。"));
    layout->addWidget(environments_);
    tokenStatus_ = label({}, "httpRuntimeStatus");
    tokenStatus_->setProperty("muted", true);
    layout->addWidget(tokenStatus_);
    auto gate = [this](bool discard = false) {
        return !beforeContextChange || beforeContextChange(discard);
    };
    connect(settings, &QPushButton::clicked, this, [this] { editConfiguration(0); });
    connect(configure, &QPushButton::clicked, this, [this] { editConfiguration(0); });
    connect(projects_, &QComboBox::currentIndexChanged, this, [this, gate](int at) {
        if (updating_ || at < 0)
            return;
        if (!gate(true)) {
            refresh();
            return;
        }
        QString error;
        if (store_->selectProject(projects_->itemData(at).toString(), &error)) {
            refresh();
            if (contextChanged)
                contextChanged();
        } else
            result(false, error);
    });
    connect(environments_, &QComboBox::currentIndexChanged, this, [this, gate](int at) {
        if (updating_ || at < 0)
            return;
        if (!gate()) {
            refresh();
            return;
        }
        QString error;
        if (store_->selectEnvironment(environments_->itemData(at).toString(), &error)) {
            refresh();
            if (contextChanged)
                contextChanged();
        } else
            result(false, error);
    });
    connect(folders_, &QComboBox::currentIndexChanged, this, [this] {
        if (!updating_ && definitionsChanged)
            definitionsChanged();
    });
    auto prompt = [this](const QString &title, const QString &initial = QString()) {
        bool ok = false;
        const auto value =
            QInputDialog::getText(this, title, QStringLiteral("名称"), QLineEdit::Normal, initial, &ok);
        return ok ? value.trimmed() : QString();
    };
    connect(folderAdd, &QPushButton::clicked, this, [this, gate, prompt] {
        if (!gate())
            return;
        const auto name = prompt(QStringLiteral("新建请求分类"));
        if (name.isEmpty())
            return;
        QString e;
        const bool ok = store_->addFolder(name, &e);
        result(ok, e);
    });
    auto action = [menu, this](const QString &text, const char *id, std::function<void()> fn) {
        auto *a = menu->addAction(text);
        a->setObjectName(id);
        connect(a, &QAction::triggered, this, std::move(fn));
    };
    action(QStringLiteral("新建项目并配置环境"), "httpProjectNew", [this, gate] {
        if (!gate(true))
            return;
        bool confirmed = false;
        const auto name =
            QInputDialog::getText(
                this, QStringLiteral("新建HTTP项目"),
                QStringLiteral("项目收纳一组接口请求，并共享环境、变量和公共认证。\n例如：订单服务、设"
                               "备管理API。创建后配置环境地址。\n\n项目名称"),
                QLineEdit::Normal, {}, &confirmed)
                .trimmed();
        if (!confirmed || name.isEmpty())
            return;
        QString e;
        const bool ok = store_->createProject(name, &e);
        result(ok, e);
        if (ok) {
            if (contextChanged)
                contextChanged();
            editConfiguration(
                0, nullptr,
                QStringLiteral("项目已创建。保存会应用环境配置；取消只放弃本次配置，不删除新项目。"));
        }
    });
    action(QStringLiteral("重命名项目"), "httpProjectRename", [this] { editConfiguration(1); });
    action(QStringLiteral("删除项目"), "httpProjectRemove", [this, gate] {
        if (!gate(true) || QMessageBox::question(
                               this, QStringLiteral("删除项目"), QStringLiteral("删除项目及其请求？"),
                               QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        const auto id = store_->projectId();
        QString e;
        const bool ok = store_->removeProject(&e);
        result(ok, e);
        if (ok) {
            if (projectRemoved)
                projectRemoved(id);
            if (contextChanged)
                contextChanged();
        }
    });
    action(QStringLiteral("项目名称与共享变量"), "httpProjectVariables",
           [this] { editVariables(true); });
    action(QStringLiteral("公共认证"), "httpProjectAuth", [this] { editAuthentication(); });
    menu->addSeparator();
    action(QStringLiteral("新建环境并配置"), "httpEnvironmentNew", [this, gate, prompt] {
        if (!gate())
            return;
        const auto name = prompt(QStringLiteral("新建环境"));
        if (name.isEmpty())
            return;
        QString e;
        const bool ok = store_->createEnvironment(name, &e);
        result(ok, e);
        if (ok) {
            if (contextChanged)
                contextChanged();
            editConfiguration(0, nullptr,
                              QStringLiteral("环境已创建。取消只放弃本次配置，不删除新环境。"));
        }
    });
    action(QStringLiteral("复制当前环境并配置（不复制敏感值）"), "httpEnvironmentCopy",
           [this, gate, prompt] {
               if (!gate())
                   return;
               const auto name =
                   prompt(QStringLiteral("复制环境"),
                          store_->environment().value("name").toString() + QStringLiteral(" 副本"));
               if (name.isEmpty())
                   return;
               QString e;
               const bool ok = store_->copyEnvironment(name, &e);
               result(ok, e);
               if (ok) {
                   if (contextChanged)
                       contextChanged();
                   editConfiguration(
                       0, nullptr, QStringLiteral("环境副本已创建。取消只放弃本次修改，不删除副本。"));
               }
           });
    action(QStringLiteral("配置 / 重命名当前环境"), "httpEnvironmentRename",
           [this] { editConfiguration(0); });
    action(QStringLiteral("删除当前环境"), "httpEnvironmentRemove", [this, gate] {
        if (!gate() || QMessageBox::question(this, QStringLiteral("删除环境"),
                                             QStringLiteral("删除当前环境定义和运行值？"),
                                             QMessageBox::Yes | QMessageBox::No,
                                             QMessageBox::No) != QMessageBox::Yes)
            return;
        QString e;
        const bool ok = store_->removeEnvironment(&e);
        result(ok, e);
        if (ok && contextChanged)
            contextChanged();
    });
    action(QStringLiteral("浏览器指纹（当前环境）"), "httpEnvironmentFingerprint",
           [this] { editConfiguration(4); });
    action(QStringLiteral("查看生效变量 / 清除运行值"), "httpRuntimeClear",
           [this] { editConfiguration(3); });
    menu->addSeparator();
    action(QStringLiteral("重命名选中分类"), "httpFolderRename", [this, gate, prompt] {
        if (!gate())
            return;
        const auto folder = folderFilter();
        if (folder.isEmpty() || folder == "*")
            return;
        const auto name = prompt(QStringLiteral("重命名分类"), folder);
        if (name.isEmpty())
            return;
        QString e;
        const bool ok = store_->renameFolder(folder, name, &e);
        result(ok, e);
    });
    action(QStringLiteral("删除选中分类"), "httpFolderRemove", [this, gate] {
        const auto folder = folderFilter();
        if (!gate() || folder.isEmpty() || folder == "*")
            return;
        if (QMessageBox::question(
                this, QStringLiteral("删除分类"), QStringLiteral("请求会移至未分类，内容保留。"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        QString e;
        const bool ok = store_->removeFolder(folder, &e);
        result(ok, e);
    });
    menu->addSeparator();
    action(QStringLiteral("导入项目"), "httpProjectImport", [this, gate] {
        if (gate(true) && importRequested)
            importRequested();
    });
    action(QStringLiteral("导出项目（不含凭据）"), "httpProjectExport", [this] {
        if (exportRequested)
            exportRequested();
    });
    refresh();
}
void HttpProjectPanel::refresh() {
    updating_ = true;
    const auto folder = folderFilter();
    projects_->clear();
    for (const auto &v : store_->projects()) {
        auto p = v.toObject();
        projects_->addItem(p.value("name").toString(), p.value("id").toString());
    }
    projects_->setCurrentIndex(projects_->findData(store_->projectId()));
    environments_->clear();
    for (const auto &v : store_->project().value("environments").toArray()) {
        auto e = v.toObject();
        environments_->addItem(e.value("name").toString(), e.value("id").toString());
    }
    environments_->setCurrentIndex(environments_->findData(store_->environmentId()));
    folders_->clear();
    folders_->addItem(QStringLiteral("全部请求"), "*");
    folders_->addItem(QStringLiteral("未分类"), "");
    for (const auto &v : store_->project().value("folders").toArray())
        folders_->addItem(v.toString(), v.toString());
    const auto at = folders_->findData(folder);
    folders_->setCurrentIndex(at < 0 ? 0 : at);
    const auto values = store_->effectiveVariables();
    const auto runtime = store_->runtimeVariables();
    QString addressStatus = QStringLiteral("未配置，点击配置环境");
    const auto base = values.value("base_url");
    if (!base.isUndefined() && !base.isNull() && !base.isString())
        addressStatus = QStringLiteral("类型无效，须为文本地址");
    else if (!base.toString().trimmed().isEmpty()) {
        QString error;
        const auto resolved = store_->expandUrl("{{base_url}}", &error);
        const QUrl target(resolved);
        addressStatus = error.isEmpty() && target.isValid() && !target.host().isEmpty() &&
                                (target.scheme() == "http" || target.scheme() == "https")
                            ? QStringLiteral("已配置 {{base_url}}")
                            : QStringLiteral("待补齐或修正，点击配置环境");
    }
    tokenStatus_->setText(
        QStringLiteral("服务地址：%1\n环境定义 %2 · 运行值 %3\n公共认证：%4")
            .arg(addressStatus)
            .arg(store_->environment().value("variables").toArray().size())
            .arg(runtime.size())
            .arg(httpAuthName(store_->project().value("auth").toObject().value("kind").toString())
                     .section(QStringLiteral("（"), 0, 0)));
    const auto fingerprint = store_->environment().value("browserFingerprint").toObject();
    if (!fingerprint.isEmpty())
        tokenStatus_->setText(tokenStatus_->text() + QStringLiteral("\n浏览器：%1 %2 · %3")
                                                         .arg(fingerprint.value("browser").toString())
                                                         .arg(fingerprint.value("major").toInt())
                                                         .arg(fingerprint.value("enabled").toBool()
                                                                  ? QStringLiteral("自动请求头")
                                                                  : QStringLiteral("仅变量")));
    updating_ = false;
}
void HttpProjectPanel::setActive(bool active) { setEnabled(!active); }
QString HttpProjectPanel::folderFilter() const {
    return folders_ && folders_->count() ? folders_->currentData().toString() : "*";
}
bool HttpProjectPanel::matches(const QJsonObject &request, const QString &fallback) const {
    return request.value("projectId").toString(fallback) == store_->projectId() &&
           (folderFilter() == "*" || request.value("folder").toString() == folderFilter());
}
void HttpProjectPanel::result(bool ok, const QString &error) {
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("项目设置"), error);
        return;
    }
    refresh();
    if (definitionsChanged)
        definitionsChanged();
}
void HttpProjectPanel::editConfiguration(int tab, const char *name, const QString &notice) {
    if (!isEnabled() || (beforeContextChange && !beforeContextChange(false)))
        return;
    HttpConfigurationDialog dialog(store_, tab, this);
    if (name)
        dialog.setObjectName(name);
    if (!notice.isEmpty()) {
        auto *label = dialog.findChild<QLabel *>("httpSettingsCreationNotice");
        label->setText(notice);
        label->show();
    }
    dialog.beforeSave = [this] {
        return isEnabled() && (!beforeContextChange || beforeContextChange(false));
    };
    if (dialog.exec() == QDialog::Accepted) {
        refresh();
        if (definitionsChanged)
            definitionsChanged();
    }
}
void HttpProjectPanel::editVariables(bool projectScope) {
    editConfiguration(projectScope ? 1 : 0, "httpVariablesDialog");
}
void HttpProjectPanel::editAuthentication() { editConfiguration(2, "httpProjectAuthDialog"); }
} // namespace portbridge
