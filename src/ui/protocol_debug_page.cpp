#include "protocol_debug_page.hpp"
#include "byte_text_preview.hpp"
#include "design_widgets.hpp"
#include "http_assertions.hpp"
#include "http_assertions_editor.hpp"
#include "http_auth_presentation.hpp"
#include "http_project_panel.hpp"
#include "http_project_store.hpp"
#include "http_request_resolver.hpp"
#include "http_sequence_runner.hpp"
#include "http_template_preview.hpp"
#include "protocol_preview.hpp"
#include <QAbstractTableModel>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>
#include <QVBoxLayout>

namespace portbridge {
using namespace workflowUi;
namespace {
constexpr int DraftLimit = 8 * 1024 * 1024, InputLimit = 1024 * 1024, PageBytes = 4095;
QLabel *text(const QString &value, const char *id = nullptr) {
    auto *w = new QLabel(value);
    if (id)
        w->setObjectName(id);
    w->setTextFormat(Qt::PlainText);
    w->setWordWrap(true);
    return w;
}
QPushButton *btn(const QString &value, const char *id) {
    auto *w = new QPushButton(value);
    w->setObjectName(id);
    w->setMinimumHeight(28);
    return w;
}
QPlainTextEdit *editor(const char *id, bool readonly = false) {
    auto *w = new QPlainTextEdit;
    w->setObjectName(id);
    w->setReadOnly(readonly);
    auto font = design::font(12, true);
    font.setFamilies(
        {"Cascadia Code", "Consolas", "Microsoft YaHei UI", "Segoe UI Emoji", "Segoe UI Symbol"});
    w->setFont(font);
    w->setTabStopDistance(28);
    return w;
}
QPlainTextEdit *previewEditor(QWidget *tab) {
    if (auto *editor = qobject_cast<QPlainTextEdit *>(tab))
        return editor;
    return tab ? tab->findChild<QPlainTextEdit *>() : nullptr;
}
QLineEdit *line(const char *id, int limit = 8192) {
    auto *w = new QLineEdit;
    w->setObjectName(id);
    w->setMaxLength(limit);
    w->setMinimumHeight(28);
    return w;
}
using httpTemplate::redactTemplate;
using httpTemplate::referenceOnly;
using httpTemplate::safeUrl;
class KeyValues final : public QWidget {
  public:
    QTableWidget *table;
    QPushButton *add;
    std::function<void()> changed;
    int rowLimit() const { return table->objectName() == "httpExtractionRules" ? 32 : 64; }
    explicit KeyValues(const char *name, const QString &hint) {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(10, 8, 10, 8);
        layout->setSpacing(6);
        layout->addWidget(text(hint));
        table = new QTableWidget(0, 3);
        table->setObjectName(name);
        table->setHorizontalHeaderLabels(
            {QStringLiteral("启用"), QStringLiteral("名称"), QStringLiteral("值")});
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
        table->setColumnWidth(0, 44);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
        table->setColumnWidth(1, 180);
        table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        table->verticalHeader()->hide();
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setMinimumHeight(70);
        layout->addWidget(table, 1);
        auto *tools = new QHBoxLayout;
        add = btn(QStringLiteral("＋ 添加一行"), (QByteArray(name) + "Add").constData());
        auto *remove = btn(QStringLiteral("删除选中"), (QByteArray(name) + "Remove").constData());
        tools->addWidget(add);
        tools->addWidget(remove);
        tools->addStretch();
        layout->addLayout(tools);
        connect(add, &QPushButton::clicked, this, [this] {
            if (table->rowCount() < rowLimit()) {
                append({{"enabled", true}, {"key", ""}, {"value", ""}});
                table->setCurrentCell(table->rowCount() - 1, 1);
                table->editItem(table->currentItem());
            }
        });
        connect(remove, &QPushButton::clicked, this, [this] {
            const int row = table->currentRow();
            if (row >= 0) {
                table->removeRow(row);
                if (changed)
                    changed();
            }
        });
        connect(table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
            const int limit = item->column() == 1 ? 256 : 8192;
            if (item->text().size() > limit) {
                QSignalBlocker block(table);
                item->setText(item->text().left(limit));
            }
            add->setEnabled(table->rowCount() < rowLimit());
            if (changed)
                changed();
        });
    }
    void append(const QJsonObject &row) {
        QSignalBlocker block(table);
        const int at = table->rowCount();
        table->insertRow(at);
        auto *check = new QTableWidgetItem;
        check->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        check->setCheckState(row.value("enabled").toBool(true) ? Qt::Checked : Qt::Unchecked);
        table->setItem(at, 0, check);
        table->setItem(at, 1, new QTableWidgetItem(row.value("key").toString().left(256)));
        table->setItem(at, 2, new QTableWidgetItem(row.value("value").toString().left(8192)));
        table->setRowHeight(at, 28);
        add->setEnabled(table->rowCount() < rowLimit());
        if (changed)
            changed();
    }
    QJsonArray rows() const {
        QJsonArray out;
        for (int r = 0; r < table->rowCount(); ++r)
            out.append(QJsonObject{{"enabled", table->item(r, 0)->checkState() == Qt::Checked},
                                   {"key", table->item(r, 1)->text()},
                                   {"value", table->item(r, 2)->text()}});
        return out;
    }
    void setRows(const QJsonArray &rows) {
        QSignalBlocker block(table);
        table->setRowCount(0);
        for (const auto &row : rows)
            append(row.toObject());
    }
};
class Timeline final : public QAbstractTableModel {
  public:
    struct Row {
        quint64 id;
        QString time, direction, detail;
        int status = 0;
    };
    QVector<Row> rows;
    bool http = false;
    bool dark = true;
    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : rows.size();
    }
    int columnCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : 3; }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
            return QStringList{QStringLiteral("时间"),
                               http ? QStringLiteral("状态") : QStringLiteral("方向"),
                               http ? QStringLiteral("请求 / 响应") : QStringLiteral("事件 / 消息")}
                .value(section);
        return {};
    }
    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || index.row() >= rows.size())
            return {};
        const auto &row = rows[index.row()];
        if (role == Qt::DisplayRole) {
            QString state = row.direction;
            if (http) {
                if (state == "PENDING")
                    state = QStringLiteral("请求中");
                else if (state == "HTTP")
                    state = QStringLiteral("已响应");
                else if (state == "ERROR")
                    state = QStringLiteral("失败");
                else if (state == "CANCEL")
                    state = QStringLiteral("已取消");
            }
            return index.column() == 0 ? row.time : index.column() == 1 ? state : row.detail;
        }
        if (role == Qt::ForegroundRole)
            return QColor(row.direction == "HTTP" && row.status >= 400 ? (dark ? "#ffbd70" : "#925400")
                          : row.direction == "HTTP"                    ? (dark ? "#68ed9d" : "#116b35")
                          : row.direction == "PENDING"                 ? (dark ? "#85dec4" : "#176e58")
                          : row.direction == "TX"                      ? (dark ? "#ffad5c" : "#9a4300")
                          : row.direction == "RX"                      ? (dark ? "#68ed9d" : "#116b35")
                          : row.direction == "ERROR" ? (dark ? "#ef7e7e" : "#a93731")
                                                     : (dark ? "#8b9a9f" : "#5e7379"));
        return {};
    }
    void refresh(const ProtocolDebugSession &session, const SecretSamples &secrets) {
        beginResetModel();
        http = session.mode() == ProtocolDebugSession::Mode::Http;
        rows.clear();
        for (const auto &e : session.entries()) {
            const auto response =
                e.operationId == session.latestOperation() ? session.latestResponse() : e.response;
            rows.append({e.id, QDateTime::fromMSecsSinceEpoch(e.timestampMs).toString("HH:mm:ss.zzz"),
                         e.direction, redactText(e.detail, secrets), response.value("status").toInt()});
        }
        endResetModel();
    }
};
} // namespace
struct ProtocolDebugPage::Impl : QObject {
    ProtocolDebugPage *q;
    ProtocolDebugSession::Mode mode;
    QSettings *settings;
    ProtocolDebugSession *session;
    std::unique_ptr<HttpProjectStore> projects;
    std::unique_ptr<HttpSequenceRunner> sequence;
    HttpAssertionsEditor *assertions = nullptr;
    QPlainTextEdit *assertionView = nullptr, *sequenceView = nullptr;
    QPushButton *sequenceButton = nullptr;
    QJsonArray pendingAssertions;
    QJsonObject assertionReport;
    void openSequence();
    bool startSequence(const QStringList &ids, bool continueOnFailure, QString *error);
    void showSequence();
    HttpProjectPanel *projectPanel = nullptr;
    QComboBox *requestFolder = nullptr;
    KeyValues *extraction = nullptr;
    QString defaultProject, browsingProject, handledResponse;
    quint64 extractionRevision = 0;
    QString extractionProject, extractionEnvironment;
    QJsonArray pendingExtraction;
    bool http, dark = true, dirty = false, loading = false, allMasked = false, warningError = true;
    quint64 lastEpoch = 0, selected = 0;
    SecretSamples secrets;
    QLineEdit *url, *name, *token, *username, *password, *subprotocol, *caFile, *closeReason;
    QComboBox *method, *auth, *bodyKind, *messageKind;
    QSpinBox *timeout, *cap, *closeCode;
    KeyValues *query, *headers, *formBody;
    QPlainTextEdit *body, *message, *responseBody, *responseJson, *responseHeaders, *responseHex;
    QWidget *authentication, *bodyEditor, *handshake, *composer, *libraryPanel;
    QTabWidget *requestTabs, *responseTabs;
    QSplitter *vertical;
    QPushButton *primary, *messageSend, *save, *previous, *next;
    QCheckBox *paused;
    QLabel *status, *warning, *historyHint, *pageHint, *empty;
    QLabel *libraryEmpty = nullptr;
    QLabel *authHint = nullptr;
    QTableView *log;
    Timeline *timeline;
    QListWidget *library;
    QLineEdit *search;
    QJsonArray saved;
    QString draftId;
    int page = 0;
    QTimer refreshTimer;
    ProtocolDebugSession::Phase shownPhase = ProtocolDebugSession::Phase::Idle;
    bool shownWrite = false;
    Impl(ProtocolDebugPage *owner, ProtocolDebugSession::Mode m, QSettings *store)
        : q(owner), mode(m), settings(store), session(new ProtocolDebugSession(m, owner)),
          http(m == ProtocolDebugSession::Mode::Http) {
        if (http) {
            projects = std::make_unique<HttpProjectStore>(settings);
            defaultProject = projects->projects().first().toObject().value("id").toString();
            browsingProject = projects->projectId();
        }
        build();
        if (projects) {
            sequence = std::make_unique<HttpSequenceRunner>(projects.get(), session);
            connect(sequence.get(), &HttpSequenceRunner::changed, q, [this] {
                showSequence();
                projectPanel->refresh();
                controls();
            });
        }
        if (projects)
            session->setContextValidator([this](const QJsonObject &parameters) {
                const auto context = parameters.value("httpProjectContext").toObject();
                if (context.isEmpty())
                    return QString();
                if (context.value("projectId").toString() != projects->projectId() ||
                    context.value("environmentId").toString() != projects->environmentId() ||
                    context.value("revision").toDouble() != double(projects->revision()))
                    return QStringLiteral("确认期间项目/环境/变量已变更，请重新发送。旧活动已保留。");
                return QString();
            });
        loadLibrary();
        refresh();
    }
    QString key() const { return http ? "manual/httpLibrary" : "manual/webSocketLibrary"; }
    void mark() {
        if (!loading)
            dirty = true;
    }
    void warn(const QString &value, bool failure = true) {
        warningError = failure;
        warning->setStyleSheet(
            QString("color:%1;padding:4px 0")
                .arg(failure ? (dark ? "#ef7e7e" : "#a93731") : (dark ? "#85dec4" : "#176e58")));
        warning->setText(redactText(value.left(4096), secrets));
        warning->setVisible(!value.isEmpty());
    }
    void bounded(QPlainTextEdit *input) {
        if (input->toPlainText().size() > InputLimit) {
            QSignalBlocker block(input);
            auto cursor = input->textCursor();
            cursor.setPosition(InputLimit);
            cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
            warn(QStringLiteral("编辑内容超过1Mi字符；超出部分未保留，请检查内容。"));
        }
        mark();
    }
    void build();
    void refresh();
    void showEntry();
    void controls();
    void captureSecrets();
    QJsonObject parameters(QString *error, bool templateMode = false) const;
    void projectDefinitionsChanged();
    void updateAuthPresentation();
    void processExtraction();
    QJsonArray extractionRules() const;
    void exportProject();
    bool importProjectDocument(const QJsonObject &document, QString *error);
    void importProject();
    QJsonObject draft(QString *error) const;
    bool agreeToReplace() {
        if (!dirty)
            return true;
        QMessageBox dialog(QMessageBox::Question, QStringLiteral("载入请求配置"),
                           QStringLiteral("载入会放弃当前未保存编辑；不会发送请求或建立连接。"),
                           QMessageBox::Yes | QMessageBox::No, q);
        dialog.setObjectName("protocolDraftReplaceConfirmation");
        dialog.setDefaultButton(QMessageBox::No);
        return dialog.exec() == QMessageBox::Yes;
    }
    bool applyDraft(const QJsonObject &doc, QString *error);
    bool saveDraft(QString *error);
    void loadLibrary();
    void refreshLibrary();
    bool writeLibrary(QString *error = nullptr);
    void primaryAction();
    void sendMessage();
};
void ProtocolDebugPage::Impl::build() {
    q->setObjectName(http ? "httpDebugPage" : "webSocketDebugPage");
    auto *outer = new QHBoxLayout(q);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    libraryPanel = new QWidget;
    libraryPanel->setObjectName("protocolLibraryPanel");
    libraryPanel->setFixedWidth(200);
    auto *side = new QVBoxLayout(libraryPanel);
    side->setContentsMargins(12, 16, 12, 12);
    side->setSpacing(8);
    auto *title = text(http ? QStringLiteral("HTTP 请求") : QStringLiteral("WebSocket 连接"));
    title->setFont(design::font(13, false, true));
    if (!http)
        side->addWidget(title);
    else {
        title->setParent(libraryPanel);
        title->hide();
    }
    if (http) {
        projectPanel = new HttpProjectPanel(projects.get(), libraryPanel);
        side->addWidget(projectPanel);
        projectPanel->beforeContextChange = [this](bool discardDraft) {
            if (session->active()) {
                warn(QStringLiteral("请先完成或取消请求，再切换项目/环境。"));
                return false;
            }
            return !discardDraft || agreeToReplace();
        };
        projectPanel->contextChanged = [this] {
            if (browsingProject == projects->projectId()) {
                projectDefinitionsChanged();
                return;
            }
            browsingProject = projects->projectId();
            QString error;
            applyDraft({{"schemaVersion", 1},
                        {"kind", "http"},
                        {"name", ""},
                        {"params", QJsonObject{{"url", ""}, {"method", "GET"}}},
                        {"editor", QJsonObject{}}},
                       &error);
            projectDefinitionsChanged();
        };
        projectPanel->definitionsChanged = [this] { projectDefinitionsChanged(); };
        projectPanel->exportRequested = [this] { exportProject(); };
        projectPanel->importRequested = [this] { importProject(); };
        projectPanel->projectRemoved = [this](const QString &) {
            saved = projects->requests();
            refreshLibrary();
        };
    }
    search = line("protocolLibrarySearch", 256);
    search->setPlaceholderText(http ? QStringLiteral("搜索请求名称或路径…")
                                    : QStringLiteral("搜索连接…"));
    side->addWidget(search);
    library = new QListWidget;
    library->setObjectName("protocolLibrary");
    library->setWordWrap(false);
    libraryEmpty =
        text(http ? QStringLiteral("当前项目还没有请求。\n新建草稿并保存，或从顶部创建请求。")
                  : QStringLiteral("还没有保存连接。\n编辑后点击“保存连接”。"),
             "protocolLibraryEmpty");
    side->addWidget(libraryEmpty);
    side->addWidget(library, 1);
    side->addWidget(text(QStringLiteral("双击载入 · 不自动通信\n默认保存会遮蔽凭据")));
    outer->addWidget(libraryPanel);
    auto *work = new QWidget;
    auto *layout = new QVBoxLayout(work);
    layout->setContentsMargins(16, 12, 16, 10);
    layout->setSpacing(8);
    outer->addWidget(work, 1);
    auto *heading = new QHBoxLayout;
    auto *titleText =
        text(http ? QStringLiteral("HTTP 请求 / 响应") : QStringLiteral("WebSocket 双向消息"),
             "protocolTitle");
    titleText->setFont(design::font(16, false, true));
    heading->addWidget(titleText);
    heading->addStretch();
    auto *fresh = btn(QStringLiteral("新建草稿"), "protocolNew");
    fresh->setToolTip(QStringLiteral("开始未保存的编辑；保存后才加入当前请求或连接列表。"));
    save = btn(http ? QStringLiteral("保存请求") : QStringLiteral("保存连接"), "protocolSave");
    save->setToolTip(http ? QStringLiteral("保存当前请求；已载入的请求会原位更新，改名不会另建请求。")
                          : QStringLiteral("保存当前连接配置；改名会更新同一条连接。"));
    auto *import =
        btn(http ? QStringLiteral("导入请求") : QStringLiteral("导入连接"), "protocolImport");
    auto *exportButton =
        btn(http ? QStringLiteral("导出请求") : QStringLiteral("导出连接"), "protocolExport");
    import->setToolTip(
        http
            ? QStringLiteral("导入一个请求到编辑器；保存后加入当前项目。整项目导入请使用左侧管理菜单。")
            : QStringLiteral("导入单条WebSocket连接配置到编辑器。"));
    exportButton->setToolTip(
        http ? QStringLiteral("导出当前请求（含未保存编辑）；整项目导出请使用左侧管理菜单。")
             : QStringLiteral("导出当前连接配置（含未保存编辑）。"));
    auto *remove =
        btn(http ? QStringLiteral("删除请求") : QStringLiteral("删除连接"), "protocolDelete");
    remove->setToolTip(QStringLiteral("删除左侧列表选中的已保存项，不是清空当前编辑器。"));
    for (auto *b : {fresh, save, import, exportButton, remove}) {
        b->setProperty("textAction", true);
        heading->addWidget(b);
    }
    if (http) {
        sequenceButton = btn(QStringLiteral("顺序联调"), "httpSequenceRun");
        sequenceButton->setProperty("textAction", true);
        heading->addWidget(sequenceButton);
        connect(sequenceButton, &QPushButton::clicked, q, [this] { openSequence(); });
    }
    layout->addLayout(heading);
    name = line("protocolName", 128);
    name->setPlaceholderText(http ? QStringLiteral("请求名称，例如：用户登录、查询订单")
                                  : QStringLiteral("连接名称，例如：实时消息"));
    name->setToolTip(http ? QStringLiteral("名称用于在当前项目中识别接口请求，与服务地址无关。")
                          : QStringLiteral("名称用于识别保存的WebSocket连接。"));
    layout->addWidget(name);
    if (http) {
        auto *row = new QHBoxLayout;
        row->addWidget(text(QStringLiteral("保存到文件夹")));
        requestFolder = new QComboBox;
        requestFolder->setObjectName("httpRequestFolder");
        row->addWidget(requestFolder, 1);
        layout->addLayout(row);
        connect(requestFolder, &QComboBox::currentIndexChanged, q, [this] { mark(); });
    }
    auto *urlRow = new QHBoxLayout;
    urlRow->setSpacing(8);
    method = new QComboBox;
    method->setObjectName("protocolMethod");
    method->addItems({"GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS"});
    method->setFixedWidth(96);
    method->setVisible(http);
    urlRow->addWidget(method);
    url = line("protocolUrl", 4096);
    url->setPlaceholderText(http ? "{{base_url}}/api/...  或完整URL" : "wss://example.com/socket");
    urlRow->addWidget(url, 1);
    primary = btn(http ? QStringLiteral("发送请求") : QStringLiteral("连接"), "protocolPrimary");
    primary->setProperty("primary", true);
    primary->setMinimumWidth(96);
    primary->setToolTip(http ? QStringLiteral("Ctrl+Enter 发送 / 取消请求")
                             : QStringLiteral("明确建立或断开WebSocket连接"));
    urlRow->addWidget(primary);
    layout->addLayout(urlRow);
    auto *addressHelp = text(
        http ? QStringLiteral("环境配置服务地址；此处填写 {{base_url}}/接口路径，也可使用完整URL。")
             : QString(),
        "httpRequestAddressHelp");
    addressHelp->setProperty("muted", true);
    addressHelp->setVisible(http);
    layout->addWidget(addressHelp);
    warning = text({}, "protocolWarning");
    warning->setProperty("warning", true);
    warning->hide();
    layout->addWidget(warning);
    vertical = new QSplitter(Qt::Vertical);
    vertical->setObjectName("protocolVerticalSplitter");
    vertical->setChildrenCollapsible(false);
    vertical->setHandleWidth(5);
    layout->addWidget(vertical, 1);
    auto *request = new QWidget;
    auto *requestLayout = new QVBoxLayout(request);
    requestLayout->setContentsMargins(0, 0, 0, 0);
    requestLayout->setSpacing(6);
    requestTabs = new QTabWidget;
    requestTabs->setObjectName("protocolRequestTabs");
    query = new KeyValues("protocolQuery",
                          QStringLiteral("只发送启用的参数；保留URL自带query，并追加这些参数。"));
    headers = new KeyValues("protocolHeaders",
                            QStringLiteral("只发送启用的请求头；认证页签可生成Authorization。"));
    requestTabs->addTab(query, QStringLiteral("参数"));
    requestTabs->addTab(headers, QStringLiteral("请求头"));
    authentication = new QWidget;
    auto *authLayout = new QFormLayout(authentication);
    authLayout->setContentsMargins(12, 12, 12, 12);
    authLayout->setSpacing(8);
    auth = new QComboBox;
    auth->setObjectName("protocolAuthKind");
    auth->addItems({QStringLiteral("无认证（此请求不生成认证头）"), httpAuthName("bearer"),
                    httpAuthName("basic")});
    if (http) {
        auth->addItem(QStringLiteral("继承项目认证"));
        auth->setCurrentIndex(3);
    }
    token = line("protocolBearer", 16384);
    token->setEchoMode(QLineEdit::Password);
    username = line("protocolUsername", 4096);
    password = line("protocolPassword", 16384);
    password->setEchoMode(QLineEdit::Password);
    authLayout->addRow(QStringLiteral("认证方式"), auth);
    authLayout->addRow("Token", token);
    authLayout->addRow(QStringLiteral("用户名"), username);
    authLayout->addRow(QStringLiteral("密码"), password);
    authHint = text({}, "protocolAuthDescription");
    authLayout->addRow(authHint);
    requestTabs->addTab(authentication, QStringLiteral("认证"));
    bodyEditor = new QWidget(q);
    auto *bodyLayout = new QVBoxLayout(bodyEditor);
    bodyLayout->setContentsMargins(10, 8, 10, 8);
    bodyLayout->setSpacing(6);
    bodyKind = new QComboBox;
    bodyKind->setObjectName("protocolBodyKind");
    bodyKind->addItems(
        {QStringLiteral("无Body"), QStringLiteral("原文（UTF-8）"), "JSON", "x-www-form-urlencoded"});
    bodyLayout->addWidget(bodyKind);
    body = editor("protocolRequestBody");
    body->setPlaceholderText(QStringLiteral("请求体；编辑不发送。JSON模式会验证并设置Content-Type。"));
    bodyLayout->addWidget(body, 1);
    formBody =
        new KeyValues("protocolFormBody", QStringLiteral("按UTF-8对名称和值编码；空格编码为+。"));
    bodyLayout->addWidget(formBody, 1);
    if (http)
        requestTabs->addTab(bodyEditor, QStringLiteral("Body"));
    else
        bodyEditor->hide();
    handshake = new QWidget;
    auto *advanced = new QFormLayout(handshake);
    advanced->setContentsMargins(12, 10, 12, 10);
    advanced->setSpacing(8);
    timeout = new QSpinBox;
    timeout->setObjectName("protocolTimeout");
    timeout->setRange(1, 60000);
    timeout->setValue(10000);
    timeout->setSuffix(" ms");
    cap = new QSpinBox;
    cap->setObjectName("protocolCapacity");
    cap->setRange(1, 8192);
    cap->setValue(1024);
    cap->setSuffix(" KiB");
    subprotocol = line("protocolSubprotocol", 256);
    subprotocol->setParent(q);
    subprotocol->setVisible(!http);
    caFile = line("protocolCaFile", 4096);
    advanced->addRow(QStringLiteral("操作超时"), timeout);
    advanced->addRow(http ? QStringLiteral("响应Body上限") : QStringLiteral("单条消息上限"), cap);
    if (!http)
        advanced->addRow(QStringLiteral("子协议（可选）"), subprotocol);
    advanced->addRow(QStringLiteral("CA文件（可选）"), caFile);
    advanced->addRow(text(QStringLiteral("TLS校验证书链与主机名；HTTP重定向保留原响应，不自动跟随。")));
    requestTabs->addTab(handshake, QStringLiteral("设置"));
    if (http) {
        extraction = new KeyValues(
            "httpExtractionRules",
            QStringLiteral("启用规则后，成功响应保存为当前环境运行变量（默认敏感）。名称为变量"
                           "名，值为$.data.access_token等字段路径；响应头用header:名称。"));
        requestTabs->addTab(extraction, QStringLiteral("响应提取"));
        extraction->changed = [this] { mark(); };
        assertions = new HttpAssertionsEditor;
        assertions->changed = [this] { mark(); };
        requestTabs->addTab(assertions, QStringLiteral("响应断言"));
    }
    requestLayout->addWidget(requestTabs, 1);
    composer = new QWidget(q);
    auto *compose = new QVBoxLayout(composer);
    compose->setContentsMargins(0, 0, 0, 0);
    compose->setSpacing(5);
    auto *messageTools = new QHBoxLayout;
    messageKind = new QComboBox;
    messageKind->setObjectName("protocolMessageKind");
    messageKind->addItems({QStringLiteral("UTF-8 文本"), QStringLiteral("HEX 二进制")});
    messageTools->addWidget(messageKind);
    messageTools->addStretch();
    messageSend = btn(QStringLiteral("发送消息"), "protocolSendMessage");
    messageSend->setProperty("primary", true);
    messageTools->addWidget(messageSend);
    compose->addLayout(messageTools);
    message = editor("protocolMessage");
    message->setMinimumHeight(68);
    message->setMaximumHeight(140);
    message->setPlaceholderText(
        QStringLiteral("完整一条消息；不会自动添加换行，支持空消息。Ctrl+Enter发送。"));
    compose->addWidget(message, 1);
    auto *closing = new QHBoxLayout;
    closeCode = new QSpinBox;
    closeCode->setObjectName("protocolCloseCode");
    closeCode->setRange(1000, 4999);
    closeCode->setValue(1000);
    closeCode->setFixedWidth(80);
    closeReason = line("protocolCloseReason", 123);
    closeReason->setPlaceholderText(QStringLiteral("断开原因（可选，最多123 UTF-8字节）"));
    closing->addWidget(text(QStringLiteral("关闭码")));
    closing->addWidget(closeCode);
    closing->addWidget(closeReason, 1);
    compose->addLayout(closing);
    if (!http)
        requestLayout->addWidget(composer);
    else
        composer->hide();
    vertical->addWidget(request);
    auto *response = new QWidget;
    auto *responseLayout = new QVBoxLayout(response);
    responseLayout->setContentsMargins(0, 0, 0, 0);
    responseLayout->setSpacing(6);
    auto *statusRow = new QHBoxLayout;
    status = text({}, "protocolStatus");
    status->setFont(design::font(12, false, true));
    statusRow->addWidget(status, 1);
    paused = new QCheckBox(QStringLiteral("暂停显示"));
    paused->setObjectName("protocolPauseDisplay");
    paused->setVisible(!http);
    statusRow->addWidget(paused);
    auto *clear = btn(QStringLiteral("清空"), "protocolClear");
    auto *copy = btn(QStringLiteral("复制预览"), "protocolCopy");
    auto *exportPreview = btn(QStringLiteral("导出预览"), "protocolExportPreview");
    for (auto *b : {clear, copy, exportPreview}) {
        b->setProperty("textAction", true);
        statusRow->addWidget(b);
    }
    responseLayout->addLayout(statusRow);
    historyHint = text({}, "protocolHistoryHint");
    historyHint->setProperty("muted", true);
    responseLayout->addWidget(historyHint);
    auto *resultSplit = new QSplitter(Qt::Horizontal);
    resultSplit->setObjectName("protocolResultSplitter");
    timeline = new Timeline;
    timeline->setParent(q);
    log = new QTableView;
    log->setObjectName("protocolHistory");
    log->setModel(timeline);
    log->setSelectionBehavior(QAbstractItemView::SelectRows);
    log->setSelectionMode(QAbstractItemView::SingleSelection);
    log->setEditTriggers(QAbstractItemView::NoEditTriggers);
    log->verticalHeader()->hide();
    log->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    log->setColumnWidth(0, 94);
    log->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    log->setColumnWidth(1, 56);
    log->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    log->setMinimumWidth(160);
    resultSplit->addWidget(log);
    auto *details = new QWidget;
    auto *detailLayout = new QVBoxLayout(details);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(4);
    responseTabs = new QTabWidget;
    responseTabs->setObjectName("protocolResponseTabs");
    responseBody = editor("protocolResponseBody", true);
    responseJson = editor("protocolResponseJson", true);
    responseHeaders = editor("protocolResponseHeaders", true);
    responseHex = editor("protocolResponseHex", true);
    responseTabs->addTab(responseBody, http ? "Body" : QStringLiteral("消息"));
    if (http) {
        responseTabs->addTab(responseJson, "JSON");
        responseTabs->addTab(responseHeaders, "Headers");
    } else {
        responseJson->setParent(q);
        responseHeaders->setParent(q);
        responseJson->hide();
        responseHeaders->hide();
    }
    responseTabs->addTab(responseHex, "HEX");
    if (http) {
        assertionView = editor("httpAssertionResults", true);
        assertionView->setPlainText(QStringLiteral("发送请求后检查已配置的响应断言。"));
        responseTabs->addTab(assertionView, QStringLiteral("断言"));
        auto *pane = new QWidget;
        auto *box = new QVBoxLayout(pane);
        box->setContentsMargins(0, 0, 0, 0);
        sequenceView = editor("httpSequenceResults", true);
        sequenceView->setPlainText(QStringLiteral("选择已保存请求，点击“顺序联调”开始。"));
        box->addWidget(sequenceView, 1);
        auto *exportResult = btn(QStringLiteral("导出联调报告（值省略）"), "httpSequenceExport");
        box->addWidget(exportResult);
        connect(exportResult, &QPushButton::clicked, q, [this] {
            if (!sequence || sequence->report().value("runId").toString().isEmpty())
                return;
            const auto path =
                QFileDialog::getSaveFileName(q, QStringLiteral("导出联调结果"), {}, "JSON (*.json)");
            if (path.isEmpty())
                return;
            QSaveFile file(path);
            const auto bytes = QJsonDocument(sequence->report()).toJson();
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
                warn(QStringLiteral("联调报告导出失败。"));
        });
        responseTabs->addTab(pane, QStringLiteral("联调结果"));
    }
    detailLayout->addWidget(responseTabs, 1);
    auto *pageRow = new QHBoxLayout;
    previous = btn(QStringLiteral("上一页"), "protocolPrevious");
    next = btn(QStringLiteral("下一页"), "protocolNext");
    pageHint = text({}, "protocolPageHint");
    pageHint->setProperty("muted", true);
    pageRow->addWidget(previous);
    pageRow->addWidget(pageHint, 1);
    pageRow->addWidget(next);
    detailLayout->addLayout(pageRow);
    resultSplit->addWidget(details);
    resultSplit->setStretchFactor(0, 0);
    resultSplit->setStretchFactor(1, 1);
    resultSplit->setSizes({280, 600});
    responseLayout->addWidget(resultSplit, 1);
    empty = text(http ? QStringLiteral("尚未发送请求。选择方法、填写URL后点击发送；这里展示真实响应。")
                      : QStringLiteral("尚未建立连接。明确连接后收发完整消息；切换页面不会断开。"),
                 "protocolEmptyState");
    responseLayout->addWidget(empty);
    vertical->addWidget(response);
    vertical->setStretchFactor(0, 0);
    vertical->setStretchFactor(1, 1);
    vertical->setSizes(http ? QList<int>{200, 350} : QList<int>{300, 300});
    auto authVisibility = [this, authLayout] {
        const int kind = auth->currentIndex();
        authLayout->setRowVisible(token, kind == 1);
        authLayout->setRowVisible(username, kind == 2);
        authLayout->setRowVisible(password, kind == 2);
        updateAuthPresentation();
    };
    authVisibility();
    connect(auth, &QComboBox::currentIndexChanged, q, [this, authVisibility] {
        authVisibility();
        mark();
    });
    auto bodyVisibility = [this] {
        body->setVisible(bodyKind->currentIndex() == 1 || bodyKind->currentIndex() == 2);
        formBody->setVisible(bodyKind->currentIndex() == 3);
    };
    bodyVisibility();
    connect(bodyKind, &QComboBox::currentIndexChanged, q, [this, bodyVisibility] {
        bodyVisibility();
        mark();
    });
    for (auto *input : {url, name, token, username, password, subprotocol, caFile, closeReason})
        connect(input, &QLineEdit::textChanged, q, [this] { mark(); });
    for (auto *combo : {method, messageKind})
        connect(combo, &QComboBox::currentIndexChanged, q, [this] { mark(); });
    for (auto *spin : {timeout, cap, closeCode})
        connect(spin, &QSpinBox::valueChanged, q, [this] { mark(); });
    for (auto *table : {query, headers, formBody})
        table->changed = [this] { mark(); };
    connect(body, &QPlainTextEdit::textChanged, q, [this] { bounded(body); });
    connect(message, &QPlainTextEdit::textChanged, q, [this] { bounded(message); });
    connect(primary, &QPushButton::clicked, q, [this] { primaryAction(); });
    connect(messageSend, &QPushButton::clicked, q, [this] { sendMessage(); });
    connect(paused, &QCheckBox::toggled, q, [this](bool p) {
        if (!p)
            refresh();
    });
    connect(clear, &QPushButton::clicked, q, [this] {
        selected = 0;
        page = 0;
        secrets.clear();
        allMasked = false;
        lastEpoch = 0;
        session->clearHistory();
        captureSecrets();
        refresh();
    });
    connect(&refreshTimer, &QTimer::timeout, q, [this] {
        if (!paused->isChecked())
            refresh();
    });
    refreshTimer.setSingleShot(true);
    refreshTimer.setInterval(50);
    connect(session, &ProtocolDebugSession::changed, q, [this] {
        processExtraction();
        captureSecrets();
        if (session->phase() != shownPhase || session->writing() != shownWrite)
            controls();
        if (!refreshTimer.isActive())
            refreshTimer.start();
    });
    connect(log->selectionModel(), &QItemSelectionModel::currentRowChanged, q,
            [this](const QModelIndex &index) {
                if (index.isValid() && index.row() < timeline->rows.size()) {
                    selected = timeline->rows[index.row()].id;
                    page = 0;
                    showEntry();
                }
            });
    connect(previous, &QPushButton::clicked, q, [this] {
        if (page > 0)
            --page;
        showEntry();
    });
    connect(next, &QPushButton::clicked, q, [this] {
        ++page;
        showEntry();
    });
    connect(copy, &QPushButton::clicked, q, [this] {
        auto *current = previewEditor(responseTabs->currentWidget());
        if (current)
            QApplication::clipboard()->setText(current->toPlainText());
    });
    connect(exportPreview, &QPushButton::clicked, q, [this] {
        const auto path = QFileDialog::getSaveFileName(q, QStringLiteral("导出当前遮蔽预览"),
                                                       "protocol-preview.json", "JSON (*.json)");
        if (path.isEmpty())
            return;
        auto *current = previewEditor(responseTabs->currentWidget());
        QSaveFile file(path);
        const QJsonObject value{{"schemaVersion", 1},
                                {"scope", "masked-visible-preview"},
                                {"status", status->text()},
                                {"page", page + 1},
                                {"view", responseTabs->tabText(responseTabs->currentIndex())},
                                {"text", current ? current->toPlainText() : QString()}};
        if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(value).toJson()) < 0 ||
            !file.commit())
            warn(QStringLiteral("预览导出失败：") + file.errorString());
    });
    connect(search, &QLineEdit::textChanged, q, [this] { refreshLibrary(); });
    connect(library, &QListWidget::itemDoubleClicked, q, [this](QListWidgetItem *item) {
        if (session->active()) {
            warn(QStringLiteral("当前活动已保留；请先取消请求或断开连接后载入配置。"));
            return;
        }
        if (!agreeToReplace())
            return;
        for (const auto &value : saved) {
            if (value.toObject().value("id").toString() == item->data(Qt::UserRole).toString()) {
                QString error;
                if (!applyDraft(value.toObject(), &error))
                    warn(error);
                break;
            }
        }
    });
    connect(save, &QPushButton::clicked, q, [this] {
        QString error;
        if (!saveDraft(&error))
            warn(error);
    });
    connect(fresh, &QPushButton::clicked, q, [this] {
        if (session->active()) {
            warn(QStringLiteral("活动会话已保留；请先取消请求或断开连接，再创建请求或连接。"));
            return;
        }
        if (dirty && QMessageBox::question(q, QStringLiteral("新建草稿"),
                                           QStringLiteral("放弃当前未保存编辑？新草稿不会自动保存。"),
                                           QMessageBox::Yes | QMessageBox::No,
                                           QMessageBox::No) != QMessageBox::Yes)
            return;
        QString error;
        applyDraft({{"schemaVersion", 1},
                    {"kind", http ? "http" : "webSocket"},
                    {"name", ""},
                    {"params", QJsonObject{{"url", http ? "{{base_url}}/" : ""}, {"method", "GET"}}},
                    {"editor", QJsonObject{}},
                    {"folder", projectPanel && projectPanel->folderFilter() != "*"
                                   ? projectPanel->folderFilter()
                                   : QString()}},
                   &error);
        warn({});
    });
    connect(remove, &QPushButton::clicked, q, [this] {
        auto *item = library->currentItem();
        if (!item)
            return;
        if (QMessageBox::question(q, QStringLiteral("删除已保存配置"),
                                  QStringLiteral("删除选中的已保存请求或连接？活动连接不受影响。"),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes)
            return;
        const auto id = item->data(Qt::UserRole).toString();
        const auto previous = saved;
        for (int i = 0; i < saved.size(); ++i)
            if (saved[i].toObject().value("id").toString() == id) {
                saved.removeAt(i);
                break;
            }
        QString error;
        if (!writeLibrary(&error)) {
            saved = previous;
            warn(error);
        }
        refreshLibrary();
    });
    connect(exportButton, &QPushButton::clicked, q, [this] {
        QString error;
        const auto document = draft(&error);
        if (!error.isEmpty()) {
            warn(error);
            return;
        }
        const auto path = QFileDialog::getSaveFileName(
            q, QStringLiteral("导出遮蔽后的请求配置"),
            http ? "request.pbhttp.json" : "connection.pbws.json", "JSON (*.json)");
        if (path.isEmpty())
            return;
        QSaveFile file(path);
        const auto bytes = QJsonDocument(document).toJson();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            warn(QStringLiteral("配置导出失败：") + file.errorString());
    });
    connect(import, &QPushButton::clicked, q, [this] {
        if (session->active()) {
            warn(QStringLiteral("请先取消请求或断开连接，再导入配置；当前活动已保留。"));
            return;
        }
        const auto path =
            QFileDialog::getOpenFileName(q, QStringLiteral("载入请求配置"), {}, "JSON (*.json)");
        if (path.isEmpty())
            return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > DraftLimit) {
            warn(QStringLiteral("无法读取配置，或文件超过8MiB。"));
            return;
        }
        QJsonParseError parse;
        const auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
        QString error;
        if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
            warn(QStringLiteral("配置JSON格式无效。"));
            return;
        }
        if (!agreeToReplace())
            return;
        if (!applyDraft(doc.object(), &error))
            warn(error.isEmpty() ? QStringLiteral("配置JSON格式无效。") : error);
    });
}
QJsonArray ProtocolDebugPage::Impl::extractionRules() const {
    QJsonArray rules;
    if (!extraction)
        return rules;
    for (const auto &value : extraction->rows()) {
        const auto row = value.toObject();
        if (!row.value("enabled").toBool(true))
            continue;
        const auto path = row.value("value").toString().trimmed();
        rules.append(QJsonObject{{"variable", row.value("key").toString().trimmed()},
                                 {"source", path.startsWith("header:") ? "header" : "json"},
                                 {"path", path.startsWith("header:") ? path.mid(7) : path},
                                 {"secret", true}});
    }
    return rules;
}
void ProtocolDebugPage::Impl::projectDefinitionsChanged() {
    if (!projects || !requestFolder)
        return;
    saved = projects->requests();
    const bool wasLoading = loading;
    loading = true;
    auto chosen = requestFolder->currentData().toString();
    if (!chosen.isEmpty() && !projects->project().value("folders").toArray().contains(chosen)) {
        for (const auto &value : saved) {
            const auto r = value.toObject();
            if (r.value("id").toString() == draftId &&
                r.value("projectId").toString() == projects->projectId()) {
                chosen = r.value("folder").toString();
                break;
            }
        }
    }
    requestFolder->clear();
    requestFolder->addItem(QStringLiteral("未分类"), "");
    for (const auto &f : projects->project().value("folders").toArray())
        requestFolder->addItem(f.toString(), f.toString());
    const int at = requestFolder->findData(chosen);
    requestFolder->setCurrentIndex(at < 0 ? 0 : at);
    loading = wasLoading;
    if (projectPanel)
        projectPanel->refresh();
    if (library)
        refreshLibrary();
    const auto kind = projects->project().value("auth").toObject().value("kind").toString("none");
    auth->setItemText(3, QStringLiteral("继承项目 · ") + httpAuthName(kind));
    updateAuthPresentation();
}
void ProtocolDebugPage::Impl::updateAuthPresentation() {
    if (!authHint)
        return;
    QString kind = auth->currentIndex() == 1 ? "bearer" : auth->currentIndex() == 2 ? "basic" : "none";
    QString source = QStringLiteral("此请求自行配置认证；保存与默认导出会遮蔽实际凭据。");
    if (http && projects && auth->currentIndex() == 3) {
        kind = projects->project().value("auth").toObject().value("kind").toString("none");
        source = QStringLiteral("继承项目“%1”的%2。变量使用当前环境“%3”；在项目设置中修改。")
                     .arg(projects->project().value("name").toString(), httpAuthName(kind),
                          projects->environment().value("name").toString());
    }
    authHint->setText(source + '\n' + httpAuthDescription(kind) + '\n' + httpAuthHeaderPreview(kind));
}
void ProtocolDebugPage::Impl::processExtraction() {
    if (!projects || session->active() || (sequence && sequence->running()))
        return;
    const auto id = session->latestOperation();
    if (id.isEmpty() || id == handledResponse)
        return;
    handledResponse = id;
    if (!session->requestSnapshot().value("httpSequenceId").toString().isEmpty())
        return;
    if (session->lastError().isEmpty()) {
        const auto results = HttpAssertions::evaluate(session->latestResponse(), pendingAssertions);
        assertionReport = {{"operationId", id},
                           {"results", results},
                           {"passed", HttpAssertions::passed(results)},
                           {"valuesExcluded", true}};
        QStringList lines;
        lines << QStringLiteral("断言 %1 项 · %2")
                     .arg(results.size())
                     .arg(HttpAssertions::passed(results) ? QStringLiteral("通过")
                                                          : QStringLiteral("失败"));
        for (const auto &v : results) {
            const auto r = v.toObject();
            lines << QStringLiteral("#%1 · %2")
                         .arg(r.value("index").toInt())
                         .arg(r.value("message").toString());
        }
        assertionView->setPlainText(lines.join('\n'));
    } else {
        assertionReport = {{"operationId", id},
                           {"passed", false},
                           {"message", QStringLiteral("网络失败，未评估断言")},
                           {"valuesExcluded", true}};
        assertionView->setPlainText(QStringLiteral("网络失败，未评估断言。"));
    }
    if (pendingExtraction.isEmpty() || !session->lastError().isEmpty())
        return;
    if (extractionProject != projects->projectId() ||
        extractionEnvironment != projects->environmentId() ||
        extractionRevision != projects->revision()) {
        warn(QStringLiteral("响应属于原项目/环境，未更新当前环境变量。"));
        return;
    }
    QString error;
    if (!projects->extract(session->latestResponse(), pendingExtraction, extractionProject,
                           extractionEnvironment, &error))
        warn(error);
    else {
        projectPanel->refresh();
        warn(QStringLiteral("已提取%1个运行变量；可供本环境后续请求使用。")
                 .arg(pendingExtraction.size()),
             false);
    }
}
void ProtocolDebugPage::Impl::exportProject() {
    QJsonArray requests;
    for (const auto &v : saved)
        if (v.toObject().value("projectId").toString(defaultProject) == projects->projectId())
            requests.append(v);
    const auto file = QFileDialog::getSaveFileName(q, QStringLiteral("导出HTTP项目（敏感值省略）"), {},
                                                   QStringLiteral("HTTP项目 (*.pbhttp-project.json)"));
    if (file.isEmpty())
        return;
    QSaveFile output(file);
    const auto bytes = QJsonDocument(projects->exportProject(requests)).toJson(QJsonDocument::Indented);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
        warn(QStringLiteral("项目导出失败。"));
    else
        warn(QStringLiteral("已导出保存的请求和环境定义；敏感运行值未导出。"), false);
}
void ProtocolDebugPage::Impl::importProject() {
    const auto file = QFileDialog::getOpenFileName(q, QStringLiteral("导入HTTP项目"), {},
                                                   QStringLiteral("HTTP项目 (*.pbhttp-project.json)"));
    if (file.isEmpty())
        return;
    QFile input(file);
    if (!input.open(QIODevice::ReadOnly) || input.size() > DraftLimit) {
        warn(QStringLiteral("项目文件无法读取或超过8MiB。"));
        return;
    }
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(input.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        warn(QStringLiteral("项目JSON无效。"));
        return;
    }
    QString error;
    if (!importProjectDocument(doc.object(), &error))
        warn(error);
}
bool ProtocolDebugPage::Impl::importProjectDocument(const QJsonObject &document, QString *error) {
    auto fail = [&](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    if (!projects || session->active())
        return fail(QStringLiteral("HTTP项目导入要求空闲HTTP页；当前活动已保留。"));
    if (!document.value("requests").isArray() || document.value("prototypeOnly").toBool() ||
        QJsonDocument(document).toJson(QJsonDocument::Compact).size() > DraftLimit)
        return fail(QStringLiteral("项目请求格式、原型标记或大小无效。"));
    const auto incoming = document.value("requests").toArray();
    if (saved.size() + incoming.size() > 512)
        return fail(QStringLiteral("请求库最多512个请求。"));
    QJsonArray safeRequests;
    ProtocolDebugPage checker(ProtocolDebugSession::Mode::Http, nullptr);
    QString why;
    for (const auto &value : incoming) {
        if (!value.isObject() || !checker.loadDraft(value.toObject(), &why))
            return fail(why.isEmpty() ? QStringLiteral("项目请求格式无效。") : why);
        const auto safe = checker.exportDraft(&why);
        if (!why.isEmpty())
            return fail(why);
        safeRequests.append(safe);
    }
    auto candidate = document;
    candidate["requests"] = safeRequests;
    QJsonArray requests;
    if (!projects->importProject(candidate, &requests, &why))
        return fail(why);
    saved = projects->requests();
    browsingProject = projects->projectId();
    applyDraft({{"schemaVersion", 1},
                {"kind", "http"},
                {"name", ""},
                {"params", QJsonObject{{"url", ""}, {"method", "GET"}}},
                {"editor", QJsonObject{}}},
               &why);
    projectPanel->refresh();
    projectDefinitionsChanged();
    warn(QStringLiteral("项目已导入；未发送任何请求。"), false);
    if (error)
        error->clear();
    return true;
}
QJsonObject ProtocolDebugPage::Impl::parameters(QString *error, bool templateMode) const {
    QString localError;
    if (!error)
        error = &localError;
    if (error)
        error->clear();
    auto fail = [&](const QString &why) {
        if (error)
            *error = why;
        return QJsonObject{};
    };
    if (http && !templateMode) {
        const auto assertionError = HttpAssertions::validate(assertions->rules());
        if (!assertionError.isEmpty())
            return fail(assertionError);
        if (extraction->rows().size() > 32)
            return fail(QStringLiteral("最多32行响应提取设置。"));
        const auto rulesError = HttpProjectStore::validateRules(extractionRules());
        if (!rulesError.isEmpty())
            return fail(rulesError);
        QJsonObject raw{{"schemaVersion", 1},
                        {"kind", "http"},
                        {"params", QJsonObject{{"method", method->currentText()},
                                               {"timeoutMs", timeout->value()},
                                               {"maxResponseBytes", cap->value() * 1024},
                                               {"caFile", caFile->text()}}},
                        {"editor", QJsonObject{{"baseUrl", url->text()},
                                               {"query", query->rows()},
                                               {"headers", headers->rows()},
                                               {"authKind", auth->currentIndex()},
                                               {"token", token->text()},
                                               {"username", username->text()},
                                               {"password", password->text()},
                                               {"bodyKind", bodyKind->currentIndex()},
                                               {"body", body->toPlainText()},
                                               {"formBody", formBody->rows()}}}};
        return resolveHttpRequest(raw, *projects, error);
    }
    auto resolve = [&](const QString &source, bool json = false) {
        if (error && !error->isEmpty())
            return QString();
        return projects && !templateMode ? projects->expand(source, error, json) : source;
    };
    const auto resolvedUrl = projects && !templateMode
                                 ? projects->expandUrl(url->text().trimmed(), error)
                                 : resolve(url->text().trimmed());
    if (error && !error->isEmpty())
        return {};
    QUrl target(resolvedUrl, QUrl::StrictMode);
    QByteArray encodedQuery = target.query(QUrl::FullyEncoded).toUtf8();
    QJsonObject hs;
    for (const auto &row : query->rows()) {
        const auto r = row.toObject();
        if (!r.value("enabled").toBool() || r.value("key").toString().isEmpty())
            continue;
        if (!encodedQuery.isEmpty())
            encodedQuery += '&';
        encodedQuery += QUrl::toPercentEncoding(resolve(r.value("key").toString())) + '=' +
                        QUrl::toPercentEncoding(resolve(r.value("value").toString()));
        if (error && !error->isEmpty())
            return {};
    }
    if (!encodedQuery.isEmpty() || target.hasQuery())
        target.setQuery(QString::fromLatin1(encodedQuery), QUrl::StrictMode);
    const QUrlQuery qs(target);
    for (const auto &row : headers->rows()) {
        const auto r = row.toObject();
        if (!r.value("enabled").toBool() || r.value("key").toString().isEmpty())
            continue;
        const auto k = resolve(r.value("key").toString()).trimmed();
        for (auto it = hs.constBegin(); it != hs.constEnd(); ++it)
            if (it.key().compare(k, Qt::CaseInsensitive) == 0)
                return fail(QStringLiteral("重复请求头：") + k);
        hs[k] = resolve(r.value("value").toString());
        if (error && !error->isEmpty())
            return {};
    }
    auto contains = [&](const QString &key) {
        for (auto it = hs.constBegin(); it != hs.constEnd(); ++it)
            if (it.key().compare(key, Qt::CaseInsensitive) == 0)
                return true;
        return false;
    };
    int authKind = auth->currentIndex();
    QString authToken = token->text(), authUsername = username->text(), authPassword = password->text();
    if (projects && authKind == 3) {
        const auto inherited = projects->project().value("auth").toObject();
        const auto kind = inherited.value("kind").toString("none");
        authKind = kind == "bearer" ? 1 : kind == "basic" ? 2 : 0;
        authToken = inherited.value("token").toString();
        authUsername = inherited.value("username").toString();
        authPassword = inherited.value("password").toString();
    }
    if (authKind != 0) {
        if (contains("Authorization"))
            return fail(QStringLiteral("认证页签与Authorization请求头重复，请保留一种。"));
        if (authKind == 1) {
            authToken = resolve(authToken);
            if (error && !error->isEmpty())
                return {};
            if (authToken.isEmpty())
                return fail(QStringLiteral("请填写Bearer Token或配置项目Token变量。"));
            hs["Authorization"] = "Bearer " + authToken;
        } else {
            authUsername = resolve(authUsername);
            authPassword = resolve(authPassword);
            if (error && !error->isEmpty())
                return {};
            if (authUsername.contains(':'))
                return fail(QStringLiteral("Basic用户名不能含冒号。"));
            hs["Authorization"] =
                "Basic " + QString::fromLatin1((authUsername + ':' + authPassword).toUtf8().toBase64());
        }
    }
    QJsonObject p{{"url", templateMode && url->text().contains("{{")
                              ? url->text().trimmed()
                              : target.toString(QUrl::FullyEncoded)},
                  {"headers", hs},
                  {"timeoutMs", timeout->value()},
                  {"connectTimeoutMs", std::min(5000, timeout->value())},
                  {http ? "maxResponseBytes" : "maxMessageBytes", cap->value() * 1024},
                  {"caFile", caFile->text()}};
    if (http) {
        p["method"] = method->currentText();
        const int kind = bodyKind->currentIndex();
        if (kind == 1)
            p["body"] = resolve(body->toPlainText());
        else if (kind == 2) {
            QJsonParseError parse;
            const auto resolvedBody = resolve(body->toPlainText(), true);
            if (error && !error->isEmpty())
                return {};
            const auto doc = QJsonDocument::fromJson(resolvedBody.toUtf8(), &parse);
            if (!(templateMode && resolvedBody.contains("{{")) &&
                (parse.error != QJsonParseError::NoError || (!doc.isObject() && !doc.isArray())))
                return fail(QStringLiteral("JSON Body须为有效对象或数组：") + parse.errorString());
            p["body"] = resolvedBody;
            if (!contains("Content-Type"))
                hs["Content-Type"] = "application/json; charset=utf-8";
        } else if (kind == 3) {
            QByteArray encoded;
            auto escape = [](const QString &v) {
                auto b = QUrl::toPercentEncoding(v);
                b.replace("%20", "+");
                return b;
            };
            for (const auto &row : formBody->rows()) {
                const auto r = row.toObject();
                if (!r.value("enabled").toBool() || r.value("key").toString().isEmpty())
                    continue;
                if (!encoded.isEmpty())
                    encoded += '&';
                encoded += escape(resolve(r.value("key").toString())) + '=' +
                           escape(resolve(r.value("value").toString()));
                if (error && !error->isEmpty())
                    return {};
            }
            p["body"] = QString::fromLatin1(encoded);
            if (!contains("Content-Type"))
                hs["Content-Type"] = "application/x-www-form-urlencoded; charset=utf-8";
        }
        p["headers"] = hs;
    } else
        p["subprotocol"] = subprotocol->text().trimmed();
    // Keep only bounded credential samples in the immutable snapshot; these are
    // not wire fields and allow echoed Basic/Bearer values to be masked reliably.
    QJsonObject samples;
    if (authKind == 1)
        samples["token"] = authToken;
    if (authKind == 2) {
        samples["password"] = authPassword;
        samples["credential"] = hs.value("Authorization").toString().mid(6);
    }
    for (const auto &item : qs.queryItems(QUrl::FullyDecoded))
        if (sensitive(item.first))
            samples[item.first] = item.second;
    if (http && bodyKind->currentIndex() == 3)
        for (const auto &row : formBody->rows()) {
            const auto item = row.toObject();
            if (item.value("enabled").toBool() && sensitive(item.value("key").toString()))
                samples[item.value("key").toString()] = item.value("value");
        }
    if (projects && !templateMode) {
        const auto runtime = projects->runtimeVariables();
        for (auto it = runtime.begin(); it != runtime.end(); ++it)
            if (it.value().toObject().value("secret").toBool())
                samples[it.key()] = it.value().toObject().value("value");
        p["httpProjectContext"] = QJsonObject{{"projectId", projects->projectId()},
                                              {"environmentId", projects->environmentId()},
                                              {"revision", double(projects->revision())}};
    }
    if (error && !error->isEmpty())
        return {};
    if (assertions) {
        const auto assertionError = HttpAssertions::validate(assertions->rules());
        if (!assertionError.isEmpty())
            return fail(assertionError);
    }
    if (extraction && extraction->rows().size() > 32)
        return fail(QStringLiteral("最多32行响应提取设置，请先删除多余行。"));
    const auto ruleError = projects ? HttpProjectStore::validateRules(extractionRules()) : QString();
    if (!ruleError.isEmpty())
        return fail(ruleError);
    p["credentialSamples"] = samples;
    const bool unresolved =
        templateMode &&
        QString::fromUtf8(QJsonDocument(p).toJson(QJsonDocument::Compact)).contains("{{");
    const auto why = unresolved ? QString() : ProtocolDebugSession::validate(p, mode);
    if (!why.isEmpty())
        return fail(why);
    const QRegularExpression masked(QStringLiteral("\\[[^\\]\\r\\n]{0,96}已遮蔽\\]"));
    if (!templateMode &&
        (QString::fromUtf8(QJsonDocument(p).toJson(QJsonDocument::Compact)).contains(masked) ||
         QUrl::fromPercentEncoding(p.value("url").toString().toUtf8()).contains(masked)))
        return fail(QStringLiteral("配置中的凭据已被遮蔽，请补填有效值后再发送。"));
    return p;
}
void ProtocolDebugPage::Impl::captureSecrets() {
    if (projects) {
        const auto values = projects->runtimeVariables();
        for (auto it = values.begin(); it != values.end(); ++it)
            if (it.value().toObject().value("secret").toBool())
                appendSecret(jsonText(it.value().toObject().value("value")), secrets);
    }
    if (lastEpoch == session->epoch())
        return;
    lastEpoch = session->epoch();
    if (secrets.size() > 900) {
        session->clearHistory();
        secrets.clear();
        selected = 0;
    }
    collectSecrets(session->requestSnapshot(), secrets);
    const auto rawBody = session->requestSnapshot().value("body").toString();
    if (rawBody.size() <= InputLimit) {
        const auto doc = QJsonDocument::fromJson(rawBody.toUtf8());
        if (doc.isObject())
            collectSecrets(doc.object(), secrets);
        else if (doc.isArray())
            collectSecrets(doc.array(), secrets);
    }
    allMasked = secrets.size() >= 1024;
    for (const auto &sample : secrets)
        allMasked |= sample.partial;
}
void ProtocolDebugPage::Impl::primaryAction() {
    if (sequence && sequence->running()) {
        sequence->stop();
        controls();
        return;
    }
    if (session->active()) {
        if (session->connected())
            session->close(closeCode->value(), closeReason->text());
        else
            session->cancel();
        controls();
        return;
    }
    if (projects && url->text().contains("{{base_url}}")) {
        const auto base = projects->effectiveVariables().value("base_url");
        if (!base.isString() || base.toString().trimmed().isEmpty()) {
            warn(QStringLiteral(
                "当前环境的服务地址未配置或不是文本。请点击“配置环境”，或填写完整请求URL。"));
            return;
        }
    }
    QString error;
    const auto p = parameters(&error);
    if (!error.isEmpty()) {
        warn(error);
        return;
    }
    warn({});
    if (projects) {
        pendingAssertions = HttpAssertions::resolve(assertions->rules(), *projects, &error);
        if (!error.isEmpty()) {
            warn(error);
            return;
        }
        assertionReport = {};
        assertionView->setPlainText(QStringLiteral("等待本次响应…"));
        extractionProject = projects->projectId();
        extractionEnvironment = projects->environmentId();
        pendingExtraction = extractionRules();
        extractionRevision = projects->revision();
    }
    if (!session->start(p, &error))
        warn(error);
    controls();
}
void ProtocolDebugPage::Impl::sendMessage() {
    if (message->toPlainText().contains(
            QRegularExpression(QStringLiteral("\\[[^\\]\\r\\n]{0,96}已遮蔽\\]")))) {
        warn(QStringLiteral("消息中的凭据已被遮蔽，请补填后再发送。"));
        return;
    }
    QByteArray bytes;
    const bool binary = messageKind->currentIndex() == 1;
    const auto source = message->toPlainText();
    if (binary) {
        QString hex = source;
        hex.remove(QRegularExpression("\\s"));
        if (hex.size() % 2 || hex.contains(QRegularExpression("[^0-9a-fA-F]"))) {
            warn(QStringLiteral("HEX须为完整的十六进制字节，可用空白分隔；未发送。"));
            return;
        }
        bytes = QByteArray::fromHex(hex.toLatin1());
    } else
        bytes = source.toUtf8();
    QString error;
    if (!session->sendMessage(bytes, binary, &error))
        warn(error);
    else {
        if (secrets.size() > 900) {
            session->clearHistory();
            secrets.clear();
            collectSecrets(session->requestSnapshot(), secrets);
        }
        const auto doc = QJsonDocument::fromJson(bytes);
        if (doc.isObject())
            collectSecrets(doc.object(), secrets);
        else if (doc.isArray())
            collectSecrets(doc.array(), secrets);
        allMasked = secrets.size() >= 1024;
        for (const auto &sample : secrets)
            allMasked |= sample.partial;
        warn({});
    }
    controls();
}
void ProtocolDebugPage::Impl::controls() {
    shownPhase = session->phase();
    shownWrite = session->writing();
    const bool active = session->active();
    requestTabs->setEnabled(true);
    for (int i = 0; i < requestTabs->count(); ++i)
        requestTabs->widget(i)->setEnabled(!active);
    if (http && extraction)
        extraction->setEnabled(!active);
    if (projectPanel)
        projectPanel->setActive(active);
    if (requestFolder)
        requestFolder->setEnabled(!active);
    url->setEnabled(!active);
    method->setEnabled(!active);
    name->setEnabled(!active);
    save->setEnabled(!active);
    if (sequenceButton)
        sequenceButton->setEnabled(!active);
    primary->setText(sequence && sequence->running() ? QStringLiteral("停止联调")
                     : http ? (active ? QStringLiteral("取消请求") : QStringLiteral("发送请求"))
                            : (session->connected() ? QStringLiteral("断开")
                               : active             ? QStringLiteral("取消连接")
                                                    : QStringLiteral("连接")));
    messageSend->setEnabled(session->connected() && !session->writing());
    message->setEnabled(true); // Preparing a message is silent, including while disconnected.
    const auto phase = session->phase();
    QString value =
        phase == ProtocolDebugSession::Phase::Requesting   ? QStringLiteral("等待HTTP响应…")
        : phase == ProtocolDebugSession::Phase::Connecting ? QStringLiteral("WebSocket握手中…")
        : phase == ProtocolDebugSession::Phase::Connected
            ? QStringLiteral("已连接 · RX %1B / 本地写出 %2B")
                  .arg(session->receivedBytes())
                  .arg(session->transmittedBytes())
        : phase == ProtocolDebugSession::Phase::Closing ? QStringLiteral("正在关闭WebSocket…")
        : http                                          ? QStringLiteral("未在发送请求")
                                                        : QStringLiteral("未连接");
    if (http && !session->latestResponse().isEmpty() && !active) {
        const auto r = session->latestResponse();
        value = QStringLiteral("HTTP %1 · %2ms · Body %3B")
                    .arg(r.value("status").toInt())
                    .arg(r.value("elapsedMs").toDouble())
                    .arg(r.value("bodyBytes").toDouble());
    }
    if (!session->lastError().isEmpty())
        value = QStringLiteral("操作错误 · ") + redactText(session->lastError().left(2048), secrets);
    status->setText(value);
    status->setStyleSheet(QString("color:%1")
                              .arg(!session->lastError().isEmpty() ? (dark ? "#ef7e7e" : "#a93731")
                                   : http && session->latestResponse().value("status").toInt() >= 400
                                       ? (dark ? "#ffbd70" : "#925400")
                                       : (dark ? "#85dec4" : "#176e58")));
}
void ProtocolDebugPage::Impl::refresh() {
    controls();
    if (paused->isChecked())
        return;
    const auto old = selected;
    const bool follow =
        selected == 0 || (!timeline->rows.isEmpty() && selected == timeline->rows.back().id);
    QSignalBlocker block(log->selectionModel());
    timeline->refresh(*session, secrets);
    if (follow && !timeline->rows.isEmpty())
        selected = timeline->rows.back().id;
    int row = -1;
    for (int i = 0; i < timeline->rows.size(); ++i)
        if (timeline->rows[i].id == selected)
            row = i;
    if (row < 0 && !timeline->rows.isEmpty()) {
        row = timeline->rows.size() - 1;
        selected = timeline->rows[row].id;
    }
    if (row >= 0) {
        log->setCurrentIndex(timeline->index(row, 0));
        if (follow)
            log->scrollToBottom();
    }
    if (old != selected)
        page = 0;
    empty->setVisible(session->entries().isEmpty());
    historyHint->setText(QStringLiteral("保留 %1 项 · 历史预算 %2 / %3 KiB%4")
                             .arg(session->entries().size())
                             .arg(session->retainedBytes() / 1024)
                             .arg(http ? 16384 : 4096)
                             .arg(session->omitted()
                                      ? QStringLiteral(" · 显示历史未保留 %1 项（不代表网络丢包）")
                                            .arg(session->omitted())
                                      : QString()));
    showEntry();
}
void ProtocolDebugPage::Impl::showEntry() {
    const ProtocolDebugEntry *item = nullptr;
    for (const auto &e : session->entries())
        if (e.id == selected) {
            item = &e;
            break;
        }
    QJsonObject r;
    if (item)
        r = item->response;
    if (item && item->operationId == session->latestOperation() && !session->latestResponse().isEmpty())
        r = session->latestResponse();
    QByteArray bytes;
    int total = 0;
    bool structured = false, hidden = allMasked, unretained = false;
    QString bodyText, json, headerText, hex;
    if (http && !r.isEmpty() && !session->active()) {
        status->setText(QStringLiteral("选中响应 · HTTP %1 · %2ms · Body %3B")
                            .arg(r.value("status").toInt())
                            .arg(r.value("elapsedMs").toDouble())
                            .arg(r.value("bodyBytes").toDouble()));
        status->setStyleSheet(QString("color:%1")
                                  .arg(r.value("status").toInt() >= 400
                                           ? (dark ? "#ffbd70" : "#925400")
                                           : (dark ? "#85dec4" : "#176e58")));
    }
    if (http && !r.isEmpty()) {
        total = r.value("bodyBytes").toInt();
        const auto parsed = r.value("body");
        structured = parsed.isObject() || parsed.isArray();
        headerText = previewValue(r.value("headers"), secrets);
        json = structured ? previewValue(parsed, secrets)
                          : QStringLiteral("响应不是JSON对象/数组；请使用Body或HEX查看。");
        if (structured) {
            bodyText = json;
            hex = QStringLiteral("结构化JSON的原始HEX默认遮蔽；JSON页签保留遮蔽后的字段。");
        } else {
            const auto encoded = r.value("bodyBase64").toString();
            const int start = std::max(0, page * 5460 - 4);
            bytes = QByteArray::fromBase64(encoded.mid(start, 5460 + 8).toLatin1());
            const auto view = byteTextPage(QByteArrayView(bytes), page ? 3 : 0, PageBytes);
            bodyText = redactText(view.text, secrets);
            const auto guard = encoded.mid(std::max(0, page * 5460 - 5464), 16388);
            hidden |= encodedNeedsMasking(guard, secrets);
            if (view.invalidUtf8)
                bodyText = QStringLiteral("[部分字节不是有效UTF-8；原始字节仍保留]\n") + bodyText;
            bytes = bytes.mid(page ? 3 : 0, PageBytes);
        }
    } else if (!http && item && item->payloadBytes > item->payload.size()) {
        unretained = true;
        structured = true;
        total = int(item->payloadBytes);
        bodyText = redactText(item->detail, secrets);
        hex = QStringLiteral("该消息内容未保留；仅保留事件元数据。");
    } else if (!http && item && (item->direction == "TX" || item->direction == "RX")) {
        total = item->payload.size();
        const auto &raw = item->payload;
        structured = raw.contains('{') || raw.contains('[');
        if (structured) {
            if (raw.size() <= 262144) {
                const auto doc = QJsonDocument::fromJson(raw);
                if (doc.isObject())
                    bodyText = previewValue(doc.object(), secrets);
                else if (doc.isArray())
                    bodyText = previewValue(doc.array(), secrets);
                else
                    bodyText = QStringLiteral("结构化或无法安全检查的消息，默认正文已遮蔽。");
            } else
                bodyText =
                    QStringLiteral("大型结构化消息无法安全预览，默认正文已遮蔽；原始消息仍保留。");
            hex = QStringLiteral("结构化消息的原始HEX默认遮蔽。");
        } else {
            const auto view = byteTextPage(QByteArrayView(raw), qsizetype(page) * PageBytes, PageBytes);
            bodyText = redactText(view.text, secrets);
            if (view.invalidUtf8)
                bodyText = QStringLiteral("[二进制或无效UTF-8，使用HEX检查]\n") + bodyText;
            const auto guard = raw.mid(std::max(0, page * PageBytes - 4096), PageBytes + 8192);
            hidden |= redactText(QString::fromUtf8(guard), secrets) != QString::fromUtf8(guard);
            bytes = raw.mid(page * PageBytes, PageBytes);
        }
    } else
        bodyText = item   ? redactText(item->detail, secrets)
                   : http ? QStringLiteral("选择请求记录查看响应详情。")
                          : QStringLiteral("选择时间线中的响应或消息查看详情。");
    const int pages = structured ? 1 : std::max(1, (total + PageBytes - 1) / PageBytes);
    if (page >= pages) {
        page = pages - 1;
        showEntry();
        return;
    }
    if (hidden) {
        bodyText = QStringLiteral("[存在长凭据、凭据数量超限或无法安全分页的凭据，正文已遮蔽]");
        json = bodyText;
        hex = bodyText;
    } else if (!structured && !bytes.isEmpty()) {
        const int visible = std::min(4095, int(bytes.size()));
        hex = QString::fromLatin1(bytes.left(visible).toHex(' ').toUpper());
        if (visible < bytes.size())
            hex += QStringLiteral("\n… 本页HEX预览仅显示前4095B；切换字节页查看后续页。");
    }
    responseBody->setPlainText(bodyText.left(32768 + 256));
    responseJson->setPlainText(json.left(32768 + 256));
    responseHeaders->setPlainText(headerText.left(32768 + 256));
    responseHex->setPlainText(hex.left(32768));
    previous->setEnabled(page > 0);
    next->setEnabled(page + 1 < pages);
    pageHint->setText(unretained
                          ? QStringLiteral("原始 %1B · 超出历史预算，仅保留事件元数据").arg(total)
                      : structured ? QStringLiteral("结构化预览 · 默认遮蔽凭据 · 字段有界")
                                   : QStringLiteral("字节页 %1 / %2 · 原始 %3B · 默认遮蔽凭据")
                                         .arg(page + 1)
                                         .arg(pages)
                                         .arg(total));
}
QJsonObject ProtocolDebugPage::Impl::draft(QString *error) const {
    const auto p = parameters(error, http);
    if (p.isEmpty() || (error && !error->isEmpty()))
        return {};
    SecretSamples samples;
    collectSecrets(p, samples);
    if (projects) {
        const auto runtime = projects->runtimeVariables();
        for (auto it = runtime.begin(); it != runtime.end(); ++it)
            if (it.value().toObject().value("secret").toBool())
                appendSecret(jsonText(it.value().toObject().value("value")), samples);
    }
    appendSecret(token->text(), samples);
    appendSecret(password->text(), samples);
    auto maskRows = [&](QJsonArray rows) {
        for (int i = 0; i < rows.size(); ++i) {
            auto row = rows[i].toObject();
            row["value"] = redactTemplate(row.value("value"), samples, row.value("key").toString());
            rows[i] = row;
        }
        return rows;
    };
    auto savedMessage = redact(message->toPlainText(), {}, samples);
    if (!http && messageKind->currentIndex() == 1) {
        QString source = message->toPlainText();
        source.remove(QRegularExpression("\\s"));
        const auto bytes = QByteArray::fromHex(source.toLatin1());
        if (encodedNeedsMasking(QString::fromLatin1(bytes.toBase64()), samples))
            savedMessage = QStringLiteral("[二进制消息可能包含凭据，已遮蔽]");
    }
    QJsonObject edit{{"baseUrl", safeUrl(url->text(), samples)},
                     {"query", maskRows(query->rows())},
                     {"headers", maskRows(headers->rows())},
                     {"authKind", auth->currentIndex()},
                     {"token", token->text().isEmpty() || referenceOnly(token->text())
                                   ? token->text()
                                   : QStringLiteral("[已遮蔽]")},
                     {"username", username->text()},
                     {"password", password->text().isEmpty() || referenceOnly(password->text())
                                      ? password->text()
                                      : QStringLiteral("[已遮蔽]")},
                     {"bodyKind", bodyKind->currentIndex()},
                     {"body", redactTemplate(body->toPlainText(), samples)},
                     {"formBody", maskRows(formBody->rows())},
                     {"messageKind", messageKind->currentIndex()},
                     {"message", savedMessage}};
    auto safe = redactTemplate(p, samples).toObject();
    safe["url"] = safeUrl(p.value("url").toString(), samples);
    safe.remove("credentialSamples");
    return {{"schemaVersion", 1},
            {"kind", http ? "http" : "webSocket"},
            {"id", draftId},
            {"name", redactText(name->text().trimmed().isEmpty()
                                    ? (http ? QStringLiteral("未命名HTTP请求")
                                            : QStringLiteral("未命名WebSocket连接"))
                                    : name->text().trimmed(),
                                samples)},
            {"params", safe},
            {"editor", edit},
            {"projectId", projects ? projects->projectId() : QString()},
            {"folder", requestFolder ? requestFolder->currentData().toString() : QString()},
            {"assertions",
             assertions ? httpTemplate::safeAssertions(assertions->rules(), samples) : QJsonArray{}},
            {"extractionRows", extraction ? extraction->rows() : QJsonArray{}},
            {"credentialsMasked", true}};
}
bool ProtocolDebugPage::Impl::applyDraft(const QJsonObject &doc, QString *error) {
    auto fail = [&](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    if (error)
        error->clear();
    if (session->active())
        return fail(QStringLiteral("当前活动已保留，请先取消请求或断开连接后载入配置。"));
    if (doc.value("schemaVersion").toInt() != 1 ||
        doc.value("kind").toString() != (http ? "http" : "webSocket") ||
        !doc.value("params").isObject() ||
        QJsonDocument(doc).toJson(QJsonDocument::Compact).size() > DraftLimit)
        return fail(QStringLiteral("请求/连接配置的版本、类型或大小不匹配。"));
    const auto p = doc.value("params").toObject(), e = doc.value("editor").toObject();
    if (doc.value("prototypeOnly").toBool())
        return fail(QStringLiteral("模拟原型配置不能作为正式请求/连接载入。"));
    for (const auto &k : {"query", "headers", "formBody"}) {
        if (e.contains(k) && !e.value(k).isArray())
            return fail(QStringLiteral("配置键值表格式无效。"));
        const auto rows = e.value(k).toArray();
        if (rows.size() > 64)
            return fail(QStringLiteral("键值表超过64行。"));
        for (const auto &row : rows) {
            const auto r = row.toObject();
            if (!row.isObject() || !r.value("key").isString() || !r.value("value").isString() ||
                r.value("key").toString().size() > 256 || r.value("value").toString().size() > 8192)
                return fail(QStringLiteral("配置键值字段超过限制或格式无效；未载入。"));
        }
    }
    for (const auto &key : {"body", "message"})
        if (e.value(key).toString().size() > InputLimit)
            return fail(QStringLiteral("编辑内容超过1Mi字符；未载入。"));
    if (p.value("body").isString() && p.value("body").toString().size() > InputLimit)
        return fail(QStringLiteral("请求体超过编辑上限；未载入。"));
    const auto incomingUrl = e.value("baseUrl").toString(p.value("url").toString());
    if (incomingUrl.size() > 4096 || e.value("token").toString().size() > 16384 ||
        e.value("password").toString().size() > 16384 || e.value("username").toString().size() > 4096 ||
        p.value("caFile").toString().size() > 4096 || p.value("subprotocol").toString().size() > 256)
        return fail(QStringLiteral("配置字段超过编辑上限；未载入。"));
    const int incomingCap = p.value(http ? "maxResponseBytes" : "maxMessageBytes").toInt(1024 * 1024),
              incomingTimeout = p.value("timeoutMs").toInt(10000);
    if (incomingCap < 1024 || incomingCap > 8 * 1024 * 1024 || incomingCap % 1024 ||
        incomingTimeout < 1 || incomingTimeout > 60000)
        return fail(QStringLiteral("容量须为1–8192整KiB，超时须为1–60000ms；未载入。"));
    if (p.value("headers").toObject().size() > 64)
        return fail(QStringLiteral("请求头超过64条；未载入。"));
    if (http && !QStringList{"GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS"}.contains(
                    p.value("method").toString("GET")))
        return fail(QStringLiteral("配置中的HTTP方法无效；未载入。"));
    const auto incomingHeaders = p.value("headers").toObject();
    for (auto it = incomingHeaders.constBegin(); it != incomingHeaders.constEnd(); ++it)
        if (!it.value().isString() || it.key().size() > 256 || it.value().toString().size() > 8192)
            return fail(QStringLiteral("配置请求头格式无效或超过限制；未载入。"));
    if (e.contains("body") && !e.value("body").isString())
        return fail(QStringLiteral("编辑Body须为文本；未载入。"));
    const auto incomingBody = e.contains("body")   ? e.value("body").toString()
                              : p.contains("body") ? jsonText(p.value("body"))
                                                   : QString();
    if (incomingBody.size() > InputLimit)
        return fail(QStringLiteral("Body超过编辑上限；未载入。"));
    if (http) {
        if (doc.contains("assertions") && !doc.value("assertions").isArray())
            return fail(QStringLiteral("断言设置须为数组。"));
        const auto assertionError = HttpAssertions::validate(doc.value("assertions").toArray());
        if (!assertionError.isEmpty())
            return fail(assertionError);
        const auto rows = doc.value("extractionRows").toArray();
        if (rows.size() > 32)
            return fail(QStringLiteral("提取规则超过32行。"));
        QJsonArray rules;
        for (const auto &v : rows) {
            const auto r = v.toObject();
            if (!v.isObject() || !r.value("key").isString() || !r.value("value").isString())
                return fail(QStringLiteral("提取规则格式无效。"));
            if (r.value("enabled").toBool(true)) {
                const auto path = r.value("value").toString();
                rules.append(QJsonObject{{"variable", r.value("key")},
                                         {"source", path.startsWith("header:") ? "header" : "json"},
                                         {"path", path.startsWith("header:") ? path.mid(7) : path}});
            }
        }
        const auto why = HttpProjectStore::validateRules(rules);
        if (!why.isEmpty())
            return fail(why);
        if (doc.value("folder").toString().size() > 128)
            return fail(QStringLiteral("文件夹名称过长。"));
    }
    loading = true;
    draftId = doc.value("id").toString().left(128);
    name->setText(doc.value("name").toString().left(128));
    url->setText(e.value("baseUrl").toString(p.value("url").toString()));
    method->setCurrentText(p.value("method").toString("GET"));
    query->setRows(e.value("query").toArray());
    if (e.contains("headers"))
        headers->setRows(e.value("headers").toArray());
    else {
        QJsonArray rows;
        const auto hs = p.value("headers").toObject();
        for (auto it = hs.constBegin(); it != hs.constEnd(); ++it)
            rows.append(QJsonObject{{"enabled", true}, {"key", it.key()}, {"value", it.value()}});
        headers->setRows(rows);
    }
    auth->setCurrentIndex(
        std::clamp(e.value("authKind").toInt(http && !doc.contains("id") ? 3 : 0), 0, http ? 3 : 2));
    token->setText(e.value("token").toString());
    username->setText(e.value("username").toString());
    password->setText(e.value("password").toString());
    bodyKind->setCurrentIndex(
        std::clamp(e.value("bodyKind")
                       .toInt(p.value("body").isObject() || p.value("body").isArray() ? 2
                              : p.contains("body")                                    ? 1
                                                                                      : 0),
                   0, 3));
    body->setPlainText(incomingBody);
    formBody->setRows(e.value("formBody").toArray());
    messageKind->setCurrentIndex(std::clamp(e.value("messageKind").toInt(), 0, 1));
    message->setPlainText(e.value("message").toString().left(InputLimit));
    timeout->setValue(p.value("timeoutMs").toInt(10000));
    cap->setValue(p.value(http ? "maxResponseBytes" : "maxMessageBytes").toInt(1024 * 1024) / 1024);
    subprotocol->setText(p.value("subprotocol").toString());
    caFile->setText(p.value("caFile").toString());
    if (projects) {
        projectDefinitionsChanged();
        const int at = requestFolder->findData(doc.value("folder").toString());
        requestFolder->setCurrentIndex(at < 0 ? 0 : at);
        extraction->setRows(doc.value("extractionRows").toArray());
        assertions->setRules(doc.value("assertions").toArray());
    }
    dirty = false;
    loading = false;
    warn(doc.value("credentialsMasked").toBool()
             ? QStringLiteral("已载入请求配置，尚未发送；若含[已遮蔽]，请先补填凭据。")
             : QString(),
         false);
    return true;
}
bool ProtocolDebugPage::Impl::saveDraft(QString *error) {
    auto doc = draft(error);
    if (error && !error->isEmpty())
        return false;
    if (draftId.isEmpty())
        draftId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    doc["id"] = draftId;
    auto candidate = saved;
    int at = -1;
    for (int i = 0; i < candidate.size(); ++i)
        if (candidate[i].toObject().value("id").toString() == draftId)
            at = i;
    if (at >= 0)
        candidate[at] = doc;
    else {
        if (candidate.size() >= (http ? 512 : 64)) {
            if (error)
                *error = QStringLiteral("最多保存%1个请求或连接，请先删除旧项。").arg(http ? 512 : 64);
            return false;
        }
        candidate.append(doc);
    }
    if (QJsonDocument(candidate).toJson(QJsonDocument::Compact).size() > DraftLimit) {
        if (error)
            *error = QStringLiteral("保存列表超过8MiB；请减少请求数量或Body大小。");
        return false;
    }
    const auto previous = saved;
    saved = candidate;
    if (!writeLibrary(error)) {
        saved = previous;
        return false;
    }
    dirty = false;
    refreshLibrary();
    warn(http ? QStringLiteral("请求已保存到当前项目（凭据已遮蔽）；不会自动发送。")
              : QStringLiteral("连接已保存（凭据已遮蔽）；不会自动连接。"),
         false);
    return true;
}
bool ProtocolDebugPage::Impl::writeLibrary(QString *error) {
    if (!settings) {
        if (error)
            *error = QStringLiteral("未提供配置存储位置，不能宣称已保存。");
        return false;
    }
    if (projects)
        return projects->setRequests(saved, error);
    settings->setValue(key(), QJsonDocument(QJsonObject{{"schemaVersion", 1}, {"entries", saved}})
                                  .toJson(QJsonDocument::Compact));
    settings->sync();
    if (settings->status() != QSettings::NoError) {
        if (error)
            *error = QStringLiteral("请求配置写入失败；请检查配置目录权限与可用空间。");
        return false;
    }
    return true;
}
void ProtocolDebugPage::Impl::loadLibrary() {
    if (projects) {
        saved = projects->requests();
        projectDefinitionsChanged();
        if (!projects->loadError().isEmpty())
            warn(projects->loadError());
        return;
    }
    if (!settings)
        return;
    const auto bytes = settings->value(key()).toByteArray();
    if (bytes.size() > DraftLimit)
        return;
    const auto doc = QJsonDocument::fromJson(bytes).object();
    if (doc.value("schemaVersion").toInt() != 1)
        return;
    const auto entries = doc.value("entries").toArray();
    if (entries.size() > 64)
        return;
    for (const auto &row : entries) {
        const auto item = row.toObject();
        if (item.value("schemaVersion").toInt() == 1 &&
            item.value("kind").toString() == (http ? "http" : "webSocket"))
            saved.append(item);
    }
    if (projects)
        projectDefinitionsChanged();
    refreshLibrary();
}
void ProtocolDebugPage::Impl::refreshLibrary() {
    library->clear();
    int projectCount = 0;
    for (const auto &row : saved) {
        const auto r = row.toObject();
        if (http && r.value("projectId").toString(defaultProject) == projects->projectId())
            ++projectCount;
        if (projectPanel && !projectPanel->matches(r, defaultProject))
            continue;
        const auto label = r.value("name").toString();
        const auto endpoint = r.value("params").toObject().value("url").toString();
        if (!search->text().isEmpty() && !label.contains(search->text(), Qt::CaseInsensitive) &&
            !endpoint.contains(search->text(), Qt::CaseInsensitive))
            continue;
        auto *item = new QListWidgetItem(
            (http ? r.value("params").toObject().value("method").toString("GET") : QString("WS")) +
                "  " + label,
            library);
        item->setData(Qt::UserRole, r.value("id"));
        item->setToolTip(label + '\n' + endpoint);
        item->setSizeHint({160, 42});
    }
    libraryEmpty->setVisible(library->count() == 0);
    const auto emptyText =
        !search->text().isEmpty()  ? QStringLiteral("没有匹配请求或连接，试试其他名称或地址。")
        : http && projectCount > 0 ? QStringLiteral("当前分类没有请求。切换到“全部请求”查看其他接口。")
        : http ? QStringLiteral("当前项目还没有请求。\n新建草稿并保存，或从顶部创建请求。")
               : QStringLiteral("还没有保存连接。\n新建草稿后点击“保存连接”。");
    libraryEmpty->setText(emptyText);
}
void ProtocolDebugPage::Impl::openSequence() {
    if (!sequence || session->active())
        return;
    QDialog dialog(q);
    dialog.setObjectName("httpSequenceDialog");
    dialog.setWindowTitle(QStringLiteral("项目顺序联调"));
    dialog.resize(760, 520);
    auto *layout = new QVBoxLayout(&dialog);
    auto *hint = text(QStringLiteral("勾选已保存请求，用上移/"
                                     "下移调整执行顺序。点击运行才发送；使用当前环境，每步完成提取后再"
                                     "解析下一步。未保存的编辑不参与。"));
    layout->addWidget(hint);
    auto *table = new QTableWidget(0, 3);
    table->setObjectName("httpSequenceSelection");
    table->setHorizontalHeaderLabels(
        {QStringLiteral("执行"), QStringLiteral("请求"), QStringLiteral("文件夹")});
    table->setColumnWidth(0, 48);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->verticalHeader()->hide();
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (const auto &value : projects->requests()) {
        const auto r = value.toObject();
        if (r.value("projectId").toString() != projects->projectId())
            continue;
        const int at = table->rowCount();
        table->insertRow(at);
        auto *check = new QTableWidgetItem;
        check->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        check->setCheckState(Qt::Unchecked);
        check->setData(Qt::UserRole, r.value("id").toString());
        table->setItem(at, 0, check);
        table->setItem(at, 1, new QTableWidgetItem(redactText(r.value("name").toString(), secrets)));
        table->setItem(at, 2, new QTableWidgetItem(r.value("folder").toString()));
    }
    layout->addWidget(table, 1);
    auto *tools = new QHBoxLayout;
    auto *up = btn(QStringLiteral("上移"), "httpSequenceUp");
    auto *down = btn(QStringLiteral("下移"), "httpSequenceDown");
    tools->addWidget(up);
    tools->addWidget(down);
    auto *policy = new QCheckBox(QStringLiteral("失败后继续后续请求"));
    policy->setObjectName("httpSequenceContinue");
    tools->addWidget(policy);
    tools->addStretch();
    layout->addLayout(tools);
    auto move = [table](int delta) {
        const int row = table->currentRow(), target = row + delta;
        if (row < 0 || target < 0 || target >= table->rowCount())
            return;
        for (int c = 0; c < 3; ++c) {
            auto *a = table->takeItem(row, c);
            auto *b = table->takeItem(target, c);
            table->setItem(row, c, b);
            table->setItem(target, c, a);
        }
        table->setCurrentCell(target, 1);
    };
    connect(up, &QPushButton::clicked, &dialog, [move] { move(-1); });
    connect(down, &QPushButton::clicked, &dialog, [move] { move(1); });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("运行选定请求"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QStringList ids;
    for (int row = 0; row < table->rowCount(); ++row)
        if (table->item(row, 0)->checkState() == Qt::Checked)
            ids << table->item(row, 0)->data(Qt::UserRole).toString();
    QString error;
    if (!startSequence(ids, policy->isChecked(), &error))
        warn(error);
}
bool ProtocolDebugPage::Impl::startSequence(const QStringList &ids, bool continueOnFailure,
                                            QString *error) {
    QString local;
    if (!error)
        error = &local;
    error->clear();
    if (!sequence || session->active()) {
        *error = QStringLiteral("顺序联调要求空闲HTTP页。");
        return false;
    }
    if (ids.isEmpty() || ids.size() > 128) {
        *error = QStringLiteral("请选择1–128个已保存请求。");
        return false;
    }
    QJsonArray snapshots;
    ProtocolDebugPage checker(ProtocolDebugSession::Mode::Http, nullptr);
    for (const auto &id : ids) {
        QJsonObject found;
        for (const auto &value : projects->requests()) {
            const auto r = value.toObject();
            if (r.value("id").toString() == id &&
                r.value("projectId").toString() == projects->projectId()) {
                found = r;
                break;
            }
        }
        if (found.isEmpty() || !checker.loadDraft(found, error)) {
            if (error->isEmpty())
                *error = QStringLiteral("请求不存在或已移出当前项目。");
            return false;
        }
        snapshots.append(found);
    }
    if (!sequence->start(snapshots, continueOnFailure, error))
        return false;
    responseTabs->setCurrentIndex(responseTabs->count() - 1);
    return true;
}
void ProtocolDebugPage::Impl::showSequence() {
    if (!sequence || !sequenceView)
        return;
    const auto report = sequence->report();
    const auto counts = report.value("counts").toObject();
    QStringList lines;
    lines << QStringLiteral("%1 · 共%2步 · 通过%3 / 失败%4 / 取消%5 / 跳过%6")
                 .arg(sequence->running() ? QStringLiteral("顺序联调运行中")
                                          : QStringLiteral("顺序联调已结束"))
                 .arg(report.value("plannedSteps").toInt())
                 .arg(counts.value("passed").toInt())
                 .arg(counts.value("failed").toInt())
                 .arg(counts.value("cancelled").toInt())
                 .arg(counts.value("skipped").toInt());
    for (const auto &value : report.value("results").toArray()) {
        const auto r = value.toObject();
        lines << QStringLiteral("\n第%1步 · HTTP %2 · %3")
                     .arg(r.value("step").toInt())
                     .arg(r.value("status").toInt())
                     .arg(r.value("message").toString());
        for (const auto &v : r.value("assertions").toArray()) {
            const auto a = v.toObject();
            lines << QStringLiteral("  断言#%1：%2")
                         .arg(a.value("index").toInt())
                         .arg(a.value("message").toString());
        }
    }
    sequenceView->setPlainText(lines.join('\n'));
}
ProtocolDebugPage::ProtocolDebugPage(ProtocolDebugSession::Mode mode, QSettings *settings,
                                     QWidget *parent)
    : QWidget(parent), d(std::make_unique<Impl>(this, mode, settings)) {}
ProtocolDebugPage::~ProtocolDebugPage() {
    // QWidget deletes its child widgets before QObject disconnects receivers.
    // Cancel while the Impl and form are alive; then sever callbacks into Impl.
    d->refreshTimer.stop();
    if (d->sequence)
        d->sequence->stop();
    d->session->cancel();
    for (auto *sender : findChildren<QObject *>())
        QObject::disconnect(sender, nullptr, this, nullptr);
    QObject::disconnect(&d->refreshTimer, nullptr, this, nullptr);
}
HttpSequenceRunner *ProtocolDebugPage::sequenceRunner() const { return d->sequence.get(); }
bool ProtocolDebugPage::startHttpSequence(const QStringList &ids, bool continueOnFailure,
                                          QString *error) {
    return d->startSequence(ids, continueOnFailure, error);
}
QJsonObject ProtocolDebugPage::assertionResult() const { return d->assertionReport; }
HttpProjectStore *ProtocolDebugPage::projectStore() const { return d->projects.get(); }
QJsonObject ProtocolDebugPage::exportHttpProject(QString *error) const {
    if (error)
        error->clear();
    if (!d->projects) {
        if (error)
            *error = QStringLiteral("WebSocket页不使用HTTP项目。");
        return {};
    }
    QJsonArray requests;
    for (const auto &value : d->projects->requests())
        if (value.toObject().value("projectId").toString() == d->projects->projectId())
            requests.append(value);
    return d->projects->exportProject(requests);
}
bool ProtocolDebugPage::importHttpProject(const QJsonObject &document, QString *error) {
    if (d->session->active()) {
        if (error)
            *error = QStringLiteral("当前活动已保留，请先取消请求。");
        return false;
    }
    if (!d->agreeToReplace()) {
        if (error)
            *error = QStringLiteral("保留未保存编辑，未导入项目。");
        return false;
    }
    return d->importProjectDocument(document, error);
}
ProtocolDebugSession *ProtocolDebugPage::session() const { return d->session; }
void ProtocolDebugPage::setDarkTheme(bool dark) {
    d->dark = dark;
    setProperty("darkTheme", dark);
    d->warn(d->warning->text(), d->warningError);
    setStyleSheet(QString("#protocolLibraryPanel {background:%1;border-right:1px "
                          "solid %2;} #protocolTitle "
                          "{font-size:20px;font-weight:600;} "
                          "#protocolLibraryEmpty {color:%3;padding:10px 4px;}")
                      .arg(dark ? "#171c1f" : "#f6f8f8", dark ? "#2b3438" : "#d5dfe1",
                           dark ? "#8b9a9f" : "#5e7379"));
    d->timeline->dark = dark;
    d->refresh();
}
void ProtocolDebugPage::triggerSend() {
    if (d->http)
        d->primaryAction();
    else if (d->session->connected())
        d->sendMessage();
    else if (!d->session->active())
        d->primaryAction();
}
void ProtocolDebugPage::focusUrl() {
    d->url->setFocus();
    d->url->selectAll();
}
bool ProtocolDebugPage::dirty() const { return d->dirty; }
bool ProtocolDebugPage::createSavedRequest(const QString &name, const QString &url, QString *error) {
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (error)
        error->clear();
    if (name.trimmed().isEmpty() || name.trimmed().size() > 128)
        return fail(QStringLiteral("请求或连接名称须为1–128个字符。"));
    if (d->session->active())
        return fail(QStringLiteral("当前协议活动已保留，请先取消请求或断开连接，再创建请求或连接。"));
    const auto initialUrl =
        d->http && url.trimmed().isEmpty() ? QStringLiteral("{{base_url}}/") : url.trimmed();
    QJsonObject params{{"url", initialUrl},
                       {"method", "GET"},
                       {"timeoutMs", 10000},
                       {"connectTimeoutMs", 5000},
                       {d->http ? "maxResponseBytes" : "maxMessageBytes", 1048576}};
    const auto validation = d->http && initialUrl.contains("{{")
                                ? QString()
                                : ProtocolDebugSession::validate(params, d->session->mode());
    if (!validation.isEmpty())
        return fail(validation);
    if (!d->agreeToReplace())
        return fail(QStringLiteral("已保留当前未保存编辑。"));
    if (!d->applyDraft({{"schemaVersion", 1},
                        {"kind", d->http ? "http" : "webSocket"},
                        {"name", name.trimmed()},
                        {"params", params},
                        {"editor", QJsonObject{}},
                        {"folder", d->projectPanel && d->projectPanel->folderFilter() != "*"
                                       ? d->projectPanel->folderFilter()
                                       : QString()}},
                       error))
        return false;
    return d->saveDraft(error);
}
bool ProtocolDebugPage::saveDraft(QString *error) {
    QString local;
    return d->saveDraft(error ? error : &local);
}
bool ProtocolDebugPage::loadDraft(const QJsonObject &draft, QString *error) {
    return d->applyDraft(draft, error);
}
QJsonObject ProtocolDebugPage::exportDraft(QString *error) const {
    QString local;
    return d->draft(error ? error : &local);
}
QJsonObject ProtocolDebugPage::requestParameters(QString *error) const {
    QString local;
    return d->parameters(error ? error : &local);
}
QString ProtocolDebugPage::summary() const { return d->status->text(); }
} // namespace portbridge
