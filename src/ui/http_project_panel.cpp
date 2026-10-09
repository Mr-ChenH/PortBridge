#include "http_project_panel.hpp"
#include "design_widgets.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
namespace portbridge {
namespace {
QPushButton *button(const QString &text, const char *name) {
    auto *b = new QPushButton(text);
    b->setObjectName(name);
    b->setProperty("textAction", true);
    return b;
}
QString valueText(const QJsonValue &v) {
    if (v.isString())
        return v.toString();
    if (v.isBool())
        return v.toBool() ? "true" : "false";
    if (v.isDouble())
        return QString::number(v.toDouble(), 'g', 16);
    if (v.isObject())
        return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    if (v.isArray())
        return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    return {};
}
} // namespace
HttpProjectPanel::HttpProjectPanel(HttpProjectStore *store, QWidget *parent)
    : QWidget(parent), store_(store) {
    setObjectName("httpProjectPanel");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(7);
    auto *heading = new QHBoxLayout;
    heading->addWidget(new QLabel(QStringLiteral("项目 / 请求")), 1);
    auto *management = new QToolButton;
    management->setText(QStringLiteral("管理"));
    management->setObjectName("httpProjectManage");
    management->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(management);
    management->setMenu(menu);
    heading->addWidget(management);
    layout->addLayout(heading);
    projects_ = new QComboBox;
    projects_->setObjectName("httpProjectChoice");
    layout->addWidget(projects_);
    auto *folderRow = new QHBoxLayout;
    folders_ = new QComboBox;
    folders_->setObjectName("httpFolderFilter");
    folderRow->addWidget(folders_, 1);
    auto *folderAdd = button("+", "httpFolderAdd");
    folderAdd->setToolTip(QStringLiteral("新建文件夹"));
    folderAdd->setFixedWidth(28);
    folderRow->addWidget(folderAdd);
    layout->addLayout(folderRow);
    layout->addWidget(new QLabel(QStringLiteral("当前环境")));
    environments_ = new QComboBox;
    environments_->setObjectName("httpEnvironmentChoice");
    layout->addWidget(environments_);
    auto *actions = new QHBoxLayout;
    auto *variables = button(QStringLiteral("变量"), "httpVariables");
    auto *auth = button(QStringLiteral("公共认证"), "httpProjectAuth");
    actions->addWidget(variables);
    actions->addWidget(auth);
    layout->addLayout(actions);
    tokenStatus_ = new QLabel;
    tokenStatus_->setObjectName("httpRuntimeStatus");
    tokenStatus_->setWordWrap(true);
    tokenStatus_->setProperty("muted", true);
    layout->addWidget(tokenStatus_);
    auto gate = [this](bool discard = false) {
        return !beforeContextChange || beforeContextChange(discard);
    };
    connect(projects_, &QComboBox::currentIndexChanged, this, [this, gate](int index) {
        if (updating_ || index < 0)
            return;
        if (!gate(true)) {
            refresh();
            return;
        }
        QString error;
        if (store_->selectProject(projects_->itemData(index).toString(), &error)) {
            refresh();
            if (contextChanged)
                contextChanged();
        } else
            result(false, error);
    });
    connect(environments_, &QComboBox::currentIndexChanged, this, [this, gate](int index) {
        if (updating_ || index < 0)
            return;
        if (!gate()) {
            refresh();
            return;
        }
        QString error;
        if (store_->selectEnvironment(environments_->itemData(index).toString(), &error)) {
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
    connect(folderAdd, &QPushButton::clicked, this, [this, gate] {
        if (!gate())
            return;
        bool ok = false;
        const auto name =
            QInputDialog::getText(this, QStringLiteral("新建文件夹"), QStringLiteral("文件夹名称"),
                                  QLineEdit::Normal, {}, &ok);
        if (!ok)
            return;
        QString error;
        result(store_->addFolder(name, &error), error);
    });
    connect(variables, &QPushButton::clicked, this, [this] { editVariables(); });
    connect(auth, &QPushButton::clicked, this, [this] { editAuthentication(); });
    auto action = [menu, this](const QString &title, const char *id, std::function<void()> fn) {
        auto *a = menu->addAction(title);
        a->setObjectName(id);
        connect(a, &QAction::triggered, this, std::move(fn));
    };
    action(QStringLiteral("新建项目"), "httpProjectNew", [this, gate] {
        if (!gate(true))
            return;
        bool ok = false;
        const auto name = QInputDialog::getText(this, QStringLiteral("新建项目"),
                                                QStringLiteral("项目名称"), QLineEdit::Normal, {}, &ok);
        if (!ok)
            return;
        QString error;
        const bool success = store_->createProject(name, &error);
        result(success, error);
        if (success && contextChanged)
            contextChanged();
    });
    action(QStringLiteral("重命名项目"), "httpProjectRename", [this] {
        bool ok = false;
        const auto name =
            QInputDialog::getText(this, QStringLiteral("重命名项目"), QStringLiteral("项目名称"),
                                  QLineEdit::Normal, store_->project().value("name").toString(), &ok);
        if (!ok)
            return;
        QString error;
        result(store_->renameProject(name, &error), error);
    });
    action(QStringLiteral("删除项目"), "httpProjectRemove", [this, gate] {
        if (!gate(true))
            return;
        if (QMessageBox::question(this, QStringLiteral("删除项目"),
                                  QStringLiteral("删除项目及其请求？当前活动不会被替换。"),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes)
            return;
        const auto id = store_->projectId();
        QString error;
        const bool success = store_->removeProject(&error);
        result(success, error);
        if (success) {
            if (projectRemoved)
                projectRemoved(id);
            if (contextChanged)
                contextChanged();
        }
    });
    action(QStringLiteral("项目变量"), "httpProjectVariables", [this] { editVariables(true); });
    action(QStringLiteral("重命名当前环境"), "httpEnvironmentRename", [this] {
        bool ok = false;
        const auto name = QInputDialog::getText(this, QStringLiteral("重命名环境"),
                                                QStringLiteral("环境名称"), QLineEdit::Normal,
                                                store_->environment().value("name").toString(), &ok);
        if (!ok)
            return;
        QString error;
        result(store_->renameEnvironment(name, &error), error);
    });
    action(QStringLiteral("重命名选中文件夹"), "httpFolderRename", [this] {
        const auto folder = folderFilter();
        if (folder.isEmpty() || folder == "*") {
            QMessageBox::information(this, QStringLiteral("文件夹"),
                                     QStringLiteral("请先在分类下拉框选中一个文件夹。"));
            return;
        }
        bool ok = false;
        const auto name =
            QInputDialog::getText(this, QStringLiteral("重命名文件夹"), QStringLiteral("文件夹名称"),
                                  QLineEdit::Normal, folder, &ok);
        if (!ok)
            return;
        QString error;
        result(store_->renameFolder(folder, name, &error), error);
    });
    action(QStringLiteral("删除选中文件夹"), "httpFolderRemove", [this] {
        const auto folder = folderFilter();
        if (folder.isEmpty() || folder == "*")
            return;
        if (QMessageBox::question(this, QStringLiteral("删除文件夹"),
                                  QStringLiteral("删除分类？请求会移动到“未分类”，请求内容保留。"),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes)
            return;
        QString error;
        result(store_->removeFolder(folder, &error), error);
    });
    menu->addSeparator();
    action(QStringLiteral("新建环境"), "httpEnvironmentNew", [this, gate] {
        if (!gate())
            return;
        bool ok = false;
        const auto name = QInputDialog::getText(this, QStringLiteral("新建环境"),
                                                QStringLiteral("环境名称"), QLineEdit::Normal, {}, &ok);
        if (!ok)
            return;
        QString error;
        const bool success = store_->createEnvironment(name, &error);
        result(success, error);
        if (success && contextChanged)
            contextChanged();
    });
    action(QStringLiteral("复制当前环境（不复制敏感值）"), "httpEnvironmentCopy", [this, gate] {
        if (!gate())
            return;
        bool ok = false;
        const auto name = QInputDialog::getText(
            this, QStringLiteral("复制环境"), QStringLiteral("新环境名称"), QLineEdit::Normal,
            store_->environment().value("name").toString() + QStringLiteral(" 副本"), &ok);
        if (!ok)
            return;
        QString error;
        const bool success = store_->copyEnvironment(name, &error);
        result(success, error);
        if (success && contextChanged)
            contextChanged();
    });
    action(QStringLiteral("删除当前环境"), "httpEnvironmentRemove", [this, gate] {
        if (!gate())
            return;
        if (QMessageBox::question(
                this, QStringLiteral("删除环境"), QStringLiteral("删除当前环境定义和运行值？"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        QString error;
        const bool success = store_->removeEnvironment(&error);
        result(success, error);
        if (success && contextChanged)
            contextChanged();
    });
    action(QStringLiteral("清除当前环境运行值"), "httpRuntimeClear", [this] {
        store_->clearRuntime();
        refresh();
        if (definitionsChanged)
            definitionsChanged();
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
    const auto selectedFolder = folderFilter();
    projects_->clear();
    for (const auto &v : store_->projects()) {
        const auto p = v.toObject();
        projects_->addItem(p.value("name").toString(), p.value("id").toString());
    }
    projects_->setCurrentIndex(projects_->findData(store_->projectId()));
    environments_->clear();
    for (const auto &v : store_->project().value("environments").toArray()) {
        const auto e = v.toObject();
        environments_->addItem(e.value("name").toString(), e.value("id").toString());
    }
    environments_->setCurrentIndex(environments_->findData(store_->environmentId()));
    folders_->clear();
    folders_->addItem(QStringLiteral("全部请求"), "*");
    folders_->addItem(QStringLiteral("未分类"), "");
    for (const auto &v : store_->project().value("folders").toArray())
        folders_->addItem(v.toString(), v.toString());
    const int at = folders_->findData(selectedFolder);
    folders_->setCurrentIndex(at < 0 ? 0 : at);
    const auto runtime = store_->runtimeVariables();
    int secrets = 0;
    for (auto it = runtime.begin(); it != runtime.end(); ++it)
        secrets += it.value().toObject().value("secret").toBool();
    tokenStatus_->setText(runtime.isEmpty()
                              ? QStringLiteral("运行变量为空 · 登录后可提取token")
                              : QStringLiteral("运行变量 %1 · 敏感 %2\n值仅保留在本次运行")
                                    .arg(runtime.size())
                                    .arg(secrets));
    updating_ = false;
}
void HttpProjectPanel::setActive(bool active) { setEnabled(!active); }
QString HttpProjectPanel::folderFilter() const {
    return folders_ && folders_->count() ? folders_->currentData().toString() : "*";
}
bool HttpProjectPanel::matches(const QJsonObject &r, const QString &fallback) const {
    if (r.value("projectId").toString(fallback) != store_->projectId())
        return false;
    return folderFilter() == "*" || r.value("folder").toString() == folderFilter();
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
void HttpProjectPanel::editVariables(bool projectScope) {
    QDialog dialog(this);
    dialog.setObjectName("httpVariablesDialog");
    dialog.setWindowTitle(
        projectScope ? QStringLiteral("项目变量 · ") + store_->project().value("name").toString()
                     : QStringLiteral("环境变量 · ") + store_->environment().value("name").toString());
    dialog.resize(720, 520);
    auto *layout = new QVBoxLayout(&dialog);
    auto *help = new QLabel(
        projectScope
            ? QStringLiteral(
                  "普通项目变量可供所有环境使用；敏感值只用于当前环境运行期。引用：{{变量名}}。")
            : QStringLiteral("普通环境变量保存到项目；敏感值和响应提取值只在本次运行保留。引用：{{base_"
                             "url}}、{{access_token}}。"));
    help->setWordWrap(true);
    layout->addWidget(help);
    auto *table = new QTableWidget(0, 5);
    table->setObjectName("httpVariableTable");
    table->setHorizontalHeaderLabels({QStringLiteral("变量名"), QStringLiteral("类型"),
                                      QStringLiteral("值"), QStringLiteral("敏感"),
                                      QStringLiteral("来源 / 更新时间")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    table->setColumnWidth(0, 128);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Interactive);
    table->setColumnWidth(4, 156);
    layout->addWidget(table, 1);
    auto append = [table](const QString &name, QJsonValue value, bool secret,
                          QString origin = QString()) {
        const int row = table->rowCount();
        table->insertRow(row);
        table->setItem(row, 0, new QTableWidgetItem(name));
        auto *type = new QComboBox;
        type->addItems({"文本", "数字", "布尔", "JSON"});
        type->setCurrentIndex(value.isDouble()                        ? 1
                              : value.isBool()                        ? 2
                              : (value.isObject() || value.isArray()) ? 3
                                                                      : 0);
        table->setCellWidget(row, 1, type);
        auto *edit = new QLineEdit(valueText(value));
        edit->setMaxLength(128 * 1024);
        edit->setEchoMode(secret ? QLineEdit::Password : QLineEdit::Normal);
        table->setCellWidget(row, 2, edit);
        auto *check = new QCheckBox;
        check->setChecked(secret);
        table->setCellWidget(row, 3, check);
        auto *source = new QTableWidgetItem(origin);
        source->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        if (origin.contains('T')) {
            const auto pos = origin.indexOf('T');
            source->setText(origin.left(origin.indexOf(' ')) + " · " + origin.mid(pos + 1, 8));
            source->setToolTip(origin);
        }
        table->setItem(row, 4, source);
        QObject::connect(check, &QCheckBox::toggled, edit, [edit](bool hidden) {
            edit->setEchoMode(hidden ? QLineEdit::Password : QLineEdit::Normal);
        });
    };
    const auto runtime = store_->runtimeVariables();
    QSet<QString> names;
    for (const auto &v :
         (projectScope ? store_->project() : store_->environment()).value("variables").toArray()) {
        const auto row = v.toObject();
        const auto name = row.value("name").toString();
        const auto running = runtime.value(name).toObject();
        append(name,
               running.contains("value") && (!projectScope || row.value("secret").toBool())
                   ? running.value("value")
                   : row.value("value"),
               row.value("secret").toBool() || running.value("secret").toBool(),
               running.contains("value")
                   ? QStringLiteral("运行值 ") + running.value("updated").toString()
               : projectScope ? QStringLiteral("项目定义")
                              : QStringLiteral("环境定义"));
        names.insert(name);
    }
    for (auto it = runtime.begin(); it != runtime.end(); ++it)
        if (!projectScope && !names.contains(it.key()))
            append(it.key(), it.value().toObject().value("value"),
                   it.value().toObject().value("secret").toBool(),
                   QStringLiteral("响应 / 运行期 ") +
                       it.value().toObject().value("updated").toString());
    auto *tools = new QHBoxLayout;
    auto *add = button(QStringLiteral("添加变量"), "httpVariableAdd");
    auto *remove = button(QStringLiteral("删除选中"), "httpVariableDelete");
    tools->addWidget(add);
    tools->addWidget(remove);
    tools->addStretch();
    layout->addLayout(tools);
    connect(add, &QPushButton::clicked, &dialog, [append, table] {
        if (table->rowCount() < 256)
            append({}, QString(), false);
    });
    connect(remove, &QPushButton::clicked, &dialog, [table] {
        if (table->currentRow() >= 0)
            table->removeRow(table->currentRow());
    });
    auto *error = new QLabel;
    error->setWordWrap(true);
    layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存变量"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        QJsonArray rows;
        for (int i = 0; i < table->rowCount(); ++i) {
            const auto name = table->item(i, 0) ? table->item(i, 0)->text().trimmed() : QString();
            const auto raw = qobject_cast<QLineEdit *>(table->cellWidget(i, 2))->text();
            QJsonValue value = raw;
            const int type = qobject_cast<QComboBox *>(table->cellWidget(i, 1))->currentIndex();
            if (type == 1) {
                bool ok = false;
                const double number = raw.toDouble(&ok);
                if (!ok) {
                    error->setText(QStringLiteral("数字格式无效：") + name);
                    return;
                }
                value = number;
            } else if (type == 2) {
                if (raw != "true" && raw != "false") {
                    error->setText(QStringLiteral("布尔值须为true/false：") + name);
                    return;
                }
                value = raw == "true";
            } else if (type == 3) {
                QJsonParseError parse;
                const auto doc = QJsonDocument::fromJson(raw.toUtf8(), &parse);
                if (parse.error != QJsonParseError::NoError) {
                    error->setText(parse.errorString());
                    return;
                }
                value = doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array());
            }
            rows.append(QJsonObject{
                {"name", name},
                {"value", value},
                {"secret", qobject_cast<QCheckBox *>(table->cellWidget(i, 3))->isChecked()}});
        }
        QString why;
        if (!(projectScope ? store_->setProjectVariables(rows, &why)
                           : store_->setEnvironmentVariables(rows, &why))) {
            error->setText(why);
            return;
        }
        QSet<QString> remaining;
        for (const auto &value : rows)
            remaining.insert(value.toObject().value("name").toString());
        for (auto it = runtime.begin(); it != runtime.end(); ++it)
            if (!remaining.contains(it.key()) && (!projectScope || names.contains(it.key())))
                store_->clearRuntimeVariable(it.key());
        dialog.accept();
    });
    if (dialog.exec() == QDialog::Accepted) {
        refresh();
        if (definitionsChanged)
            definitionsChanged();
    }
}
void HttpProjectPanel::editAuthentication() {
    QDialog dialog(this);
    dialog.setObjectName("httpProjectAuthDialog");
    dialog.setWindowTitle(QStringLiteral("项目公共认证"));
    dialog.resize(500, 310);
    auto *layout = new QVBoxLayout(&dialog);
    auto *hint = new QLabel(QStringLiteral("业务请求选择“继承项目”；登录请求选择“无认证”。Token建议引"
                                           "用敏感环境变量 {{access_token}}。"));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *form = new QFormLayout;
    const auto current = store_->project().value("auth").toObject();
    auto *kind = new QComboBox;
    kind->setObjectName("httpProjectAuthKind");
    kind->addItems({"none", "bearer", "basic"});
    kind->setCurrentText(current.value("kind").toString("none"));
    auto *token = new QLineEdit(current.value("token").toString());
    token->setObjectName("httpProjectAuthToken");
    token->setEchoMode(QLineEdit::Password);
    token->setPlaceholderText("{{access_token}}");
    auto *username = new QLineEdit(current.value("username").toString());
    auto *password = new QLineEdit(current.value("password").toString());
    password->setEchoMode(QLineEdit::Password);
    for (auto *edit : {token, username, password})
        edit->setMaxLength(16384);
    form->addRow(QStringLiteral("认证方式"), kind);
    form->addRow("Token", token);
    form->addRow(QStringLiteral("用户名"), username);
    form->addRow(QStringLiteral("密码 / 变量"), password);
    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        QString error;
        if (store_->setProjectAuth({{"kind", kind->currentText()},
                                    {"token", token->text()},
                                    {"username", username->text()},
                                    {"password", password->text()}},
                                   &error))
            dialog.accept();
        else
            QMessageBox::warning(&dialog, QStringLiteral("认证设置"), error);
    });
    if (dialog.exec() == QDialog::Accepted) {
        refresh();
        if (definitionsChanged)
            definitionsChanged();
    }
}
} // namespace portbridge
