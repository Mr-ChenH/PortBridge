#include "http_configuration_dialog.hpp"
#include "http_auth_presentation.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <memory>
namespace portbridge {
namespace {
QString textOf(const QJsonValue &value) {
    if (value.isString())
        return value.toString();
    if (value.isDouble())
        return QString::number(value.toDouble(), 'g', 16);
    if (value.isBool())
        return value.toBool() ? "true" : "false";
    if (value.isObject())
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    if (value.isArray())
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    return {};
}
bool sensitive(const QString &name) {
    static const QRegularExpression pattern("token|password|secret|authorization|api[_-]?key|cookie",
                                            QRegularExpression::CaseInsensitiveOption);
    return pattern.match(name).hasMatch();
}
QLabel *caption(const QString &text, const char *name = nullptr) {
    auto *label = new QLabel(text);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    if (name)
        label->setObjectName(name);
    return label;
}
QLineEdit *input(const QString &value, const char *name, int limit = 16384) {
    auto *line = new QLineEdit(value);
    line->setObjectName(name);
    line->setMaxLength(limit);
    return line;
}
class Variables : public QWidget {
  public:
    QTableWidget *table;
    Variables(const QJsonArray &rows, const char *name, QWidget *parent = nullptr) : QWidget(parent) {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        table = new QTableWidget(0, 4);
        table->setObjectName(name);
        table->setHorizontalHeaderLabels({QStringLiteral("变量名"), QStringLiteral("类型"),
                                          QStringLiteral("定义值"), QStringLiteral("敏感")});
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
        table->setColumnWidth(0, 170);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        table->verticalHeader()->hide();
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        layout->addWidget(table, 1);
        for (const auto &value : rows)
            append(value.toObject());
        auto *tools = new QHBoxLayout;
        auto *add = new QPushButton(QStringLiteral("添加变量"));
        add->setObjectName(QString(name) + "Add");
        auto *remove = new QPushButton(QStringLiteral("删除定义"));
        remove->setObjectName(QString(name) + "Delete");
        tools->addWidget(add);
        tools->addWidget(remove);
        tools->addStretch();
        layout->addLayout(tools);
        QObject::connect(add, &QPushButton::clicked, this, [this] {
            if (table->rowCount() < 256) {
                append({});
                table->setCurrentCell(table->rowCount() - 1, 0);
                table->editItem(table->item(table->rowCount() - 1, 0));
            }
        });
        QObject::connect(remove, &QPushButton::clicked, this, [this] {
            if (table->currentRow() >= 0)
                table->removeRow(table->currentRow());
        });
        QObject::connect(table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
            if (item->column() == 0)
                mask(item->row());
        });
    }
    void mask(int row) {
        auto *edit = qobject_cast<QLineEdit *>(table->cellWidget(row, 2));
        auto *secret = qobject_cast<QCheckBox *>(table->cellWidget(row, 3));
        if (edit && secret)
            edit->setEchoMode(secret->isChecked() || sensitive(table->item(row, 0)->text())
                                  ? QLineEdit::Password
                                  : QLineEdit::Normal);
    }
    void append(QJsonObject row) {
        const int index = table->rowCount();
        table->insertRow(index);
        table->setItem(index, 0, new QTableWidgetItem(row.value("name").toString()));
        auto *type = new QComboBox;
        type->addItems(
            {QStringLiteral("文本"), QStringLiteral("数字"), QStringLiteral("布尔"), "JSON"});
        const auto value = row.value("value");
        type->setCurrentIndex(value.isDouble()                      ? 1
                              : value.isBool()                      ? 2
                              : value.isArray() || value.isObject() ? 3
                                                                    : 0);
        table->setCellWidget(index, 1, type);
        auto *edit = new QLineEdit(textOf(value));
        edit->setMaxLength(128 * 1024);
        edit->setProperty("originalDefined", row.contains("value"));
        edit->setPlaceholderText(row.contains("value")
                                     ? QStringLiteral("填写定义值")
                                     : QStringLiteral("未设置；敏感值在运行期单独保留"));
        table->setCellWidget(index, 2, edit);
        auto *secret = new QCheckBox;
        secret->setChecked(row.value("secret").toBool() || sensitive(row.value("name").toString()));
        table->setCellWidget(index, 3, secret);
        mask(index);
        QObject::connect(secret, &QCheckBox::toggled, this, [this, edit] {
            for (int i = 0; i < table->rowCount(); ++i)
                if (table->cellWidget(i, 2) == edit) {
                    mask(i);
                    break;
                }
        });
    }
    QJsonArray rows(QString *error) const {
        QJsonArray result;
        for (int i = 0; i < table->rowCount(); ++i) {
            const auto name = table->item(i, 0)->text().trimmed();
            auto *edit = qobject_cast<QLineEdit *>(table->cellWidget(i, 2));
            const auto raw = edit->text();
            const auto type = qobject_cast<QComboBox *>(table->cellWidget(i, 1))->currentIndex();
            QJsonObject row{
                {"name", name},
                {"secret",
                 qobject_cast<QCheckBox *>(table->cellWidget(i, 3))->isChecked() || sensitive(name)}};
            if (!raw.isEmpty() || edit->property("originalDefined").toBool()) {
                QJsonValue value = raw;
                if (type == 1) {
                    bool ok = false;
                    const auto number = raw.toDouble(&ok);
                    if (!ok || !std::isfinite(number)) {
                        *error = QStringLiteral("数字格式无效：") + name;
                        return {};
                    }
                    value = number;
                }
                if (type == 2) {
                    if (raw != "true" && raw != "false") {
                        *error = QStringLiteral("布尔值须为true或false：") + name;
                        return {};
                    }
                    value = raw == "true";
                }
                if (type == 3) {
                    QJsonParseError why;
                    auto doc = QJsonDocument::fromJson(raw.toUtf8(), &why);
                    if (why.error != QJsonParseError::NoError || (!doc.isObject() && !doc.isArray())) {
                        *error = QStringLiteral("JSON须为对象或数组：") + name;
                        return {};
                    }
                    value = doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array());
                }
                row["value"] = value;
            }
            result.append(row);
        }
        *error = HttpProjectStore::validateVariables(result);
        return result;
    }
};
} // namespace
HttpConfigurationDialog::HttpConfigurationDialog(HttpProjectStore *store, int tab, QWidget *parent)
    : QDialog(parent) {
    setObjectName("httpProjectSettingsDialog");
    setProperty("httpConfiguration", true);
    setWindowTitle(QStringLiteral("HTTP 项目设置"));
    resize(780, 610);
    bool dark = false;
    for (auto *p = parent; p; p = p->parentWidget())
        if (p->property("darkTheme").isValid()) {
            dark = p->property("darkTheme").toBool();
            break;
        }
    setStyleSheet(
        QStringLiteral(
            "#httpProjectSettingsDialog,#httpProjectSettingsDialog QWidget {color:%1;background:%2;} "
            "#httpProjectSettingsDialog {background:%2;} #httpSettingsTitle "
            "{font-size:19px;font-weight:600;} #httpSettingsContext,#httpSettingsHelp {color:%3;} "
            "#httpProjectSettingsDialog QLineEdit,#httpProjectSettingsDialog QComboBox "
            "{min-height:28px;padding:3px 7px;border:1px solid %4;border-radius:4px;background:%5;} "
            "#httpProjectSettingsDialog QTableWidget "
            "{background:%5;alternate-background-color:%2;gridline-color:%4;border:1px solid %4;} "
            "#httpProjectSettingsDialog QHeaderView::section "
            "{background:%2;padding:7px;border:0;border-bottom:1px solid %4;} "
            "#httpProjectSettingsDialog QPushButton {min-height:30px;padding:3px 12px;border:1px solid "
            "%4;border-radius:4px;background:%5;} #httpProjectSettingsDialog QPushButton:hover "
            "{border-color:%6;} #httpProjectSettingsDialog QPushButton[primary=true] "
            "{background:%6;color:%2;font-weight:600;} #httpSettingsError {color:%7;} "
            "#httpProjectAuthPreview {padding:12px;border:1px solid %4;background:%5;color:%3;} "
            "#httpProjectSettingsDialog QTabWidget::pane {border:1px solid %4;background:%2;} "
            "#httpProjectSettingsDialog QTabBar::tab {padding:9px 16px;background:%5;color:%3;"
            "border:1px solid %4;} #httpProjectSettingsDialog QTabBar::tab:selected "
            "{background:%2;color:%1;border-bottom:2px solid %6;}")
            .arg(dark ? "#e5edee" : "#213336", dark ? "#171c1f" : "#f6f8f8",
                 dark ? "#9baeb5" : "#5e7379", dark ? "#354249" : "#ccd9de",
                 dark ? "#202a2f" : "#ffffff", dark ? "#85dec4" : "#176e58",
                 dark ? "#ffb4a6" : "#9d382c")
            .replace("#httpProjectSettingsDialog", "QDialog[httpConfiguration=true]"));
    const auto project = store->project(), environment = store->environment();
    const auto projectId = store->projectId(), environmentId = store->environmentId();
    const auto revision = store->revision();
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(10);
    layout->addWidget(caption(QStringLiteral("项目配置"), "httpSettingsTitle"));
    layout->addWidget(caption(
        QStringLiteral("当前环境：%1 · 运行值 > 环境定义 > 项目定义。敏感值仅在当前环境运行期保留。")
            .arg(environment.value("name").toString()),
        "httpSettingsContext"));
    auto *projectRow = new QFormLayout;
    auto *projectName = input(project.value("name").toString(), "httpSettingsProjectName", 128);
    projectRow->addRow(QStringLiteral("项目名称"), projectName);
    layout->addLayout(projectRow);
    auto *tabs = new QTabWidget;
    tabs->setObjectName("httpSettingsTabs");
    layout->addWidget(tabs, 1);
    auto *envPage = new QWidget;
    auto *envLayout = new QVBoxLayout(envPage);
    auto *envForm = new QFormLayout;
    auto *envName = input(environment.value("name").toString(), "httpSettingsEnvironmentName", 128);
    envForm->addRow(QStringLiteral("环境名称"), envName);
    QJsonArray envRows;
    QJsonObject baseRow;
    bool baseInTable = false;
    for (const auto &value : environment.value("variables").toArray()) {
        const auto row = value.toObject();
        if (row.value("name").toString() == "base_url" && row.value("value").isString() &&
            !row.value("secret").toBool())
            baseRow = row;
        else {
            envRows.append(row);
            if (row.value("name").toString() == "base_url")
                baseInTable = true;
        }
    }
    auto *baseUrl = input(baseRow.value("value").toString(), "httpEnvironmentBaseUrl", 128 * 1024);
    baseUrl->setPlaceholderText("http://localhost:8080");
    baseUrl->setEnabled(!baseInTable);
    envForm->addRow(QStringLiteral("服务地址 · base_url"), baseUrl);
    envLayout->addLayout(envForm);
    envLayout->addWidget(caption(
        baseInTable
            ? QStringLiteral("已有base_url为非文本或敏感定义，请在下表编辑。")
            : QStringLiteral(
                  "请求URL填写 {{base_url}}/api/... 才会使用此地址。切换环境不会改写固定URL。")));
    auto *environmentVariables = new Variables(envRows, "httpVariableTable");
    envLayout->addWidget(environmentVariables, 1);
    tabs->addTab(envPage, QStringLiteral("当前环境"));
    auto *projectPage = new QWidget;
    auto *projectLayout = new QVBoxLayout(projectPage);
    projectLayout->addWidget(caption(QStringLiteral(
        "所有环境共享的默认定义。同名环境变量覆盖项目变量；敏感值只进入当前环境运行期。")));
    auto *projectVariables =
        new Variables(project.value("variables").toArray(), "httpProjectVariableTable");
    projectLayout->addWidget(projectVariables, 1);
    tabs->addTab(projectPage, QStringLiteral("项目变量"));
    auto *authPage = new QWidget;
    auto *authLayout = new QVBoxLayout(authPage);
    authLayout->addWidget(caption(
        QStringLiteral("只有选择“继承项目认证”的请求使用这里的配置。登录请求可单独选择“无认证”。")));
    auto current = project.value("auth").toObject();
    auto *kind = new QComboBox;
    kind->setObjectName("httpProjectAuthKind");
    for (const auto &key : {"none", "bearer", "basic"})
        kind->addItem(httpAuthName(key), key);
    kind->setCurrentIndex(kind->findData(current.value("kind").toString("none")));
    authLayout->addWidget(kind);
    auto *description = caption({}, "httpProjectAuthDescription");
    authLayout->addWidget(description);
    auto *credentialPages = new QStackedWidget;
    credentialPages->setObjectName("httpProjectAuthFields");
    authLayout->addWidget(credentialPages);
    credentialPages->addWidget(
        caption(QStringLiteral("继承此设置的请求不生成认证头。您仍可在请求里自行设置请求头。")));
    auto *bearerPage = new QWidget;
    auto *bearerLayout = new QFormLayout(bearerPage);
    auto *token = input(current.value("token").toString(), "httpProjectAuthToken");
    token->setEchoMode(QLineEdit::Password);
    token->setPlaceholderText("{{access_token}}");
    auto *variables = new QComboBox;
    variables->setObjectName("httpProjectAuthVariable");
    variables->addItem(QStringLiteral("选择变量引用…"), QString());
    QSet<QString> variableNames;
    variableNames.insert("access_token");
    for (const auto &rows :
         {project.value("variables").toArray(), environment.value("variables").toArray()})
        for (const auto &value : rows)
            variableNames.insert(value.toObject().value("name").toString());
    const auto runtime = store->runtimeVariables();
    for (auto it = runtime.begin(); it != runtime.end(); ++it)
        variableNames.insert(it.key());
    auto sorted = variableNames.values();
    sorted.sort();
    for (const auto &name : sorted)
        variables->addItem("{{" + name + "}}", name);
    static const QRegularExpression reference("\\A\\{\\{([A-Za-z_][A-Za-z0-9_]*)\\}\\}\\z");
    const auto match = reference.match(token->text());
    if (match.hasMatch())
        variables->setCurrentIndex(variables->findData(match.captured(1)));
    bearerLayout->addRow(QStringLiteral("Token变量"), variables);
    bearerLayout->addRow(QStringLiteral("Token值 / 模板"), token);
    QObject::connect(variables, &QComboBox::currentIndexChanged, this, [variables, token] {
        if (!variables->currentData().toString().isEmpty())
            token->setText("{{" + variables->currentData().toString() + "}}");
    });
    credentialPages->addWidget(bearerPage);
    auto *basicPage = new QWidget;
    auto *basicLayout = new QFormLayout(basicPage);
    auto *username = input(current.value("username").toString(), "httpProjectAuthUsername");
    username->setPlaceholderText("{{username}}");
    auto *password = input(current.value("password").toString(), "httpProjectAuthPassword");
    password->setPlaceholderText("{{password}}");
    password->setEchoMode(QLineEdit::Password);
    basicLayout->addRow(QStringLiteral("用户名 / 变量"), username);
    basicLayout->addRow(QStringLiteral("密码 / 变量"), password);
    credentialPages->addWidget(basicPage);
    auto *preview = caption({}, "httpProjectAuthPreview");
    authLayout->addWidget(preview);
    auto updateAuth = [kind, description, credentialPages, preview] {
        const auto key = kind->currentData().toString();
        description->setText(httpAuthDescription(key));
        credentialPages->setCurrentIndex(kind->currentIndex());
        preview->setText(httpAuthHeaderPreview(key));
    };
    updateAuth();
    QObject::connect(kind, &QComboBox::currentIndexChanged, this, updateAuth);
    authLayout->addWidget(caption(QStringLiteral(
        "变量引用可保存；直接填写的Token/密码不会明文持久化。敏感运行值不随项目导出。")));
    authLayout->addStretch();
    tabs->addTab(authPage, QStringLiteral("公共认证"));
    auto *effectivePage = new QWidget;
    auto *effectiveLayout = new QVBoxLayout(effectivePage);
    effectiveLayout->addWidget(caption(QStringLiteral(
        "当前已保存配置：运行值 > 环境定义 > 项目定义。保存草稿后应用新定义。敏感值始终遮蔽。")));
    auto *effective = new QTableWidget(0, 4);
    effective->setObjectName("httpEffectiveVariables");
    effective->setHorizontalHeaderLabels({QStringLiteral("变量名"), QStringLiteral("生效来源"),
                                          QStringLiteral("值 / 状态"), QStringLiteral("运行更新时间")});
    effective->setEditTriggers(QAbstractItemView::NoEditTriggers);
    effective->setSelectionBehavior(QAbstractItemView::SelectRows);
    effective->setSelectionMode(QAbstractItemView::SingleSelection);
    effective->verticalHeader()->hide();
    effective->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    effectiveLayout->addWidget(effective, 1);
    auto clear = std::make_shared<QStringList>();
    auto refreshEffective = [effective, project, environment, runtime, clear] {
        QJsonObject values, sources, secrets, times;
        QSet<QString> names;
        for (const auto &pair :
             {qMakePair(project.value("variables").toArray(), QStringLiteral("项目定义")),
              qMakePair(environment.value("variables").toArray(), QStringLiteral("环境定义"))})
            for (const auto &value : pair.first) {
                auto row = value.toObject();
                auto name = row.value("name").toString();
                names.insert(name);
                if (row.contains("value")) {
                    values[name] = row.value("value");
                    sources[name] = pair.second;
                    secrets[name] = row.value("secret").toBool() || sensitive(name);
                }
            }
        for (auto it = runtime.begin(); it != runtime.end(); ++it) {
            names.insert(it.key());
            if (clear->contains(it.key()))
                continue;
            auto row = it.value().toObject();
            values[it.key()] = row.value("value");
            sources[it.key()] = QStringLiteral("当前环境运行值");
            secrets[it.key()] = row.value("secret").toBool() || sensitive(it.key());
            times[it.key()] = row.value("updated");
        }
        auto sorted = names.values();
        sorted.sort();
        effective->setRowCount(0);
        for (const auto &name : sorted) {
            int row = effective->rowCount();
            effective->insertRow(row);
            effective->setItem(row, 0, new QTableWidgetItem(name));
            auto source = sources.value(name).toString(QStringLiteral("无值"));
            if (clear->contains(name))
                source += QStringLiteral(" · 待清除运行值");
            effective->setItem(row, 1, new QTableWidgetItem(source));
            effective->setItem(row, 2,
                               new QTableWidgetItem(!values.contains(name) ? QStringLiteral("未设置")
                                                    : secrets.value(name).toBool()
                                                        ? QStringLiteral("••••••（敏感）")
                                                        : textOf(values.value(name)).left(160)));
            effective->setItem(row, 3, new QTableWidgetItem(times.value(name).toString()));
        }
    };
    refreshEffective();
    auto *clearOne = new QPushButton(QStringLiteral("清除选中运行值（保存后生效）"));
    clearOne->setObjectName("httpRuntimeDelete");
    auto *clearAll = new QPushButton(QStringLiteral("清除全部运行值（保存后生效）"));
    clearAll->setObjectName("httpRuntimeDeleteAll");
    auto *runtimeActions = new QHBoxLayout;
    runtimeActions->addWidget(clearOne);
    runtimeActions->addWidget(clearAll);
    effectiveLayout->addLayout(runtimeActions);
    QObject::connect(clearAll, &QPushButton::clicked, this, [runtime, clear, refreshEffective] {
        *clear = runtime.keys();
        refreshEffective();
    });
    QObject::connect(clearOne, &QPushButton::clicked, this,
                     [effective, runtime, clear, refreshEffective] {
                         auto *item = effective->item(effective->currentRow(), 0);
                         if (item && runtime.contains(item->text()) && !clear->contains(item->text())) {
                             clear->append(item->text());
                             refreshEffective();
                         }
                     });
    tabs->addTab(effectivePage, QStringLiteral("生效变量"));
    tabs->setCurrentIndex(std::clamp(tab, 0, 3));
    auto *error = caption({}, "httpSettingsError");
    layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存配置"));
    buttons->button(QDialogButtonBox::Save)->setProperty("primary", true);
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, this, [=] {
        QString why;
        auto pRows = projectVariables->rows(&why);
        if (!why.isEmpty()) {
            error->setText(why);
            tabs->setCurrentIndex(1);
            return;
        }
        auto eRows = environmentVariables->rows(&why);
        if (!why.isEmpty()) {
            error->setText(why);
            tabs->setCurrentIndex(0);
            return;
        }
        if (!baseInTable && (!baseUrl->text().isEmpty() || !baseRow.isEmpty())) {
            auto row = baseRow;
            row["name"] = "base_url";
            row["value"] = baseUrl->text().trimmed();
            eRows.append(row);
        }
        QJsonObject auth{{"kind", kind->currentData().toString()}};
        if (kind->currentIndex() == 1) {
            if (token->text().trimmed().startsWith("Bearer ", Qt::CaseInsensitive)) {
                error->setText(QStringLiteral("Token不要填写Bearer前缀，工具会自动添加。"));
                tabs->setCurrentIndex(2);
                return;
            }
            auth["token"] = token->text();
        }
        if (kind->currentIndex() == 2) {
            auth["username"] = username->text();
            auth["password"] = password->text();
        }
        if (beforeSave && !beforeSave()) {
            error->setText(QStringLiteral("请先完成当前活动，再保存配置。旧配置已保留。"));
            return;
        }
        const QJsonObject configuration{{"projectName", projectName->text()},
                                        {"environmentName", envName->text()},
                                        {"projectVariables", pRows},
                                        {"environmentVariables", eRows},
                                        {"auth", auth}};
        if (!store->saveConfiguration(projectId, environmentId, revision, configuration, *clear,
                                      &why)) {
            error->setText(why);
            return;
        }
        accept();
    });
}
} // namespace portbridge
