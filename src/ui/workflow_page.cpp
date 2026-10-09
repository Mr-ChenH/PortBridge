#include "workflow_page.hpp"
#include "workflow_canvas.hpp"
#include "protocol_preview.hpp"
#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QTabBar>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

namespace portbridge {
using namespace workflowUi;
using namespace QtNodes;
namespace {
constexpr int PreviewCharacters = 32768;
constexpr int HistoryBytes = 16 * 1024 * 1024;
QString preview(const QJsonValue &value, const SecretSamples &secrets = {}, const QString &key = {}) {
    return previewValue(value,secrets,key);
}
QPushButton *button(const QString &text, const QString &name, QWidget *parent = nullptr) {
    auto *b = new QPushButton(text, parent);
    b->setObjectName(name);
    b->setAccessibleName(text);
    b->setMinimumHeight(30);
    return b;
}
} // namespace
struct WorkflowPage::Impl {
    WorkflowPage *q;
    WorkflowRunner *runner;
    WorkflowGraphModel *model;
    BasicGraphicsScene *scene;
    View *view;
    bool dark = true, restoring = false, compact = false;
    QString selected, path;
    QJsonObject saved;
    std::function<QVector<ConnectionConfig>()> profiles;
    std::function<bool(const WorkflowDocument &, QString *)> prepare;
    QLineEdit *title;
    QLabel *saveState, *description, *resource, *status, *issue, *graphInfo, *inspectorHead,
        *inspectorMeta, *inspectorIcon, *canvasTitle, *resultStatus, *resultDetail, *paletteEmpty;
    QTabWidget *responseTabs;
    QPlainTextEdit *responseBody, *responseHeaders;
    QPushButton *recordTab, *variableTab;
    QPushButton *run, *pause, *stop, *save, *templates, *open, *exportFile, *undo, *redo, *remove, *copy,
        *zoom, *collapse;
    QWidget *palettePanel, *inspector, *logPanel;
    Palette *palette;
    QLineEdit *search;
    QSplitter *editor, *vertical;
    QTabWidget *tabs, *logTabs;
    QScrollArea *parameters;
    QPlainTextEdit *result;
    QStandardItemModel *logs, *variables;
    QTableView *logView, *variableView;
    QString displayedRun;
    WorkflowLogEntry lastLog;
    bool hasLastLog = false;
    QVector<int> editorSizes{210, 640, 286}, verticalSizes{540, 200};
    qint64 historyEstimate = 0;
    int historyCount = 0;
    explicit Impl(WorkflowPage *owner)
        : q(owner), runner(new WorkflowRunner(owner)), model(new WorkflowGraphModel(owner)),
          scene(new BasicGraphicsScene(*model, owner)), view(new View(scene, model, &dark)) {}
    void layout();
    void wire();
    void form();
    void runtime();
    void records();
    void variableRows();
    void updateDocumentUi();
    void filter();
    void reveal(const QString &);
    void message(const QString &text) {
        issue->setText(text);
        issue->setVisible(!text.isEmpty());
    }
    SecretSamples secrets() const {
        SecretSamples values;
        int remaining=65536;
        collectSecrets(runner->variables(), values, {}, &remaining);
        collectSecrets(model->document().toJson(), values, {}, &remaining);
        for (const auto &n : model->document().nodes)
            collectSecrets(runner->result(n.id).output, values, {}, &remaining);
        return values;
    }
    void mutate(const QString &text, std::function<void()> before, std::function<void()> after,
                qint64 bytes = 0) {
        if (runner->active())
            return;
        if (!bytes)
            bytes = QJsonDocument(model->document().toJson()).toJson(QJsonDocument::Compact).size() * 2;
        if (bytes > HistoryBytes) {
            scene->undoStack().clear();
            historyEstimate = 0;
            after();
            message(QStringLiteral("本次修改超过撤销容量，已清理历史；可先保存文件保留版本。"));
            return;
        }
        if (historyEstimate + bytes > HistoryBytes) {
            scene->undoStack().clear();
            historyEstimate = 0;
        }
        historyEstimate += bytes;
        scene->undoStack().push(new EditCommand(text, std::move(before), std::move(after)));
    }
    void setParameter(QString key, QJsonValue value, QString id = {}) {
        if (id.isEmpty())
            id = selected;
        auto *n = model->node(model->graphId(id));
        if (!n || runner->active())
            return;
        auto before = *n, after = before;
        after.parameters[key] = value;
        if (before.parameters == after.parameters)
            return;
        mutate(
            QStringLiteral("修改参数"),
            [this, before] { model->updateNode(before.id, before.title, before.parameters); },
            [this, after] { model->updateNode(after.id, after.title, after.parameters); },
            QJsonDocument(before.parameters).toJson(QJsonDocument::Compact).size() +
                QJsonDocument(after.parameters).toJson(QJsonDocument::Compact).size());
    }
};
WorkflowPage::WorkflowPage(QWidget *parent) : QWidget(parent), d(std::make_unique<Impl>(this)) {
    setObjectName("workflowPage");
    setFont(design::font(12));
    setMinimumSize(620, 440);
    d->layout();
    d->wire();
    setDarkTheme(true);
    auto doc = WorkflowDocument::templateDocument("login");
    doc.description = QStringLiteral("获取凭据，建立消息订阅，验证设备响应。");
    for (int i = 0; i < doc.nodes.size(); ++i)
        doc.nodes[i].position = i < 4 ? QPointF(i ? 210 + (i - 1) * 280 : 36, i ? 96 : 124)
                                      : QPointF(i == 7 ? 36 : 210 + (6 - i) * 280, i == 7 ? 364 : 336);
    setDocument(doc);
    d->saved = {};
    d->updateDocumentUi();
}
WorkflowPage::~WorkflowPage() {
    QSettings settings;
    settings.setValue("workflow/editorSizes", d->editor->saveState());
    settings.setValue("workflow/verticalSizes", d->vertical->saveState());
    settings.setValue("workflow/logCollapsed", d->logTabs->isHidden());
    d->runner->stop();
    for (auto *child : findChildren<QObject *>())
        QObject::disconnect(child, nullptr, this, nullptr);
    delete d->view;
    delete d->scene;
    delete d->runner;
}
WorkflowRunner *WorkflowPage::runner() const { return d->runner; }
WorkflowDocument WorkflowPage::document() const { return d->model->document(); }
WorkflowGraphModel *WorkflowPage::graphModel() const { return d->model; }
BasicGraphicsScene *WorkflowPage::graphScene() const { return d->scene; }
GraphicsView *WorkflowPage::graphView() const { return d->view; }
QString WorkflowPage::selectedNodeId() const { return d->selected; }
bool WorkflowPage::isDirty() const { return document().toJson() != d->saved; }
void WorkflowPage::setSessionProvider(std::function<SessionController *()> p) {
    d->runner->setSessionProvider(std::move(p));
}
void WorkflowPage::setProfilesProvider(std::function<QVector<ConnectionConfig>()> p) {
    d->profiles = p;
    d->runner->setProfilesProvider(std::move(p));
    d->form();
}
void WorkflowPage::setRunPreparation(std::function<bool(const WorkflowDocument &, QString *)> p) {
    d->prepare = std::move(p);
}
void WorkflowPage::Impl::layout() {
    auto *root = new QVBoxLayout(q);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto *heading = new QWidget;
    heading->setObjectName("workflowHeading");
    auto *row = new QHBoxLayout(heading);
    row->setContentsMargins(22, 13, 22, 13);
    auto *words = new QVBoxLayout;
    auto *eyebrow = new QLabel("AUTOMATION / WORKFLOW");
    eyebrow->setObjectName("workflowEyebrow");
    eyebrow->setFont(design::font(10));
    words->addWidget(eyebrow);
    auto *titleRow = new QHBoxLayout;
    title = new QLineEdit;
    title->setObjectName("workflowTitle");
    title->setAccessibleName(QStringLiteral("流程名称"));
    title->setFont(design::font(21, false, true));
    title->setMaxLength(256);
    saveState = new QLabel(QStringLiteral("草稿"));
    saveState->setObjectName("workflowSaveState");
    titleRow->addWidget(title);
    titleRow->addWidget(saveState);
    titleRow->addStretch();
    words->addLayout(titleRow);
    description = new QLabel(QStringLiteral("组合步骤，验证设备响应；只有显式运行才执行通信。"));
    description->setObjectName("workflowDescription");
    description->setTextFormat(Qt::PlainText);
    description->setFont(design::font(11));
    description->setWordWrap(true);
    words->addWidget(description);
    row->addLayout(words, 1);
    templates = button(QStringLiteral("模板"), "workflowTemplates");
    save = button(QStringLiteral("保存"), "workflowSave");
    run = button(QStringLiteral("运行"), "workflowRun");
    run->setProperty("primary", true);
    for (auto *b : {templates, save, run})
        row->addWidget(b);
    root->addWidget(heading);
    auto *toolbar = new QWidget;
    toolbar->setObjectName("workflowToolbar");
    auto *tools = new QHBoxLayout(toolbar);
    tools->setContentsMargins(20, 4, 20, 4);
    tools->setSpacing(5);
    toolbar->setMinimumHeight(42);
    auto *paletteToggle = button(QStringLiteral("节点库"), "workflowPaletteToggle");
    auto *inspectToggle = button(QStringLiteral("参数"), "workflowInspectorToggle");
    undo = button(QStringLiteral("撤销"), "workflowUndo");
    redo = button(QStringLiteral("重做"), "workflowRedo");
    copy = button(QStringLiteral("复制"), "workflowDuplicate");
    remove = button(QStringLiteral("删除"), "workflowDelete");
    auto *validate = button(QStringLiteral("检查流程"), "workflowValidate");
    open = button(QStringLiteral("导入"), "workflowImport");
    exportFile = button(QStringLiteral("导出"), "workflowExport");
    for (auto *b : {paletteToggle, undo, redo, copy, remove, validate, open, exportFile}) {
        b->setProperty("quiet",true);
        b->setToolTip(b->text());
        if (b == undo || b == redo || b == copy || b == remove || b == open || b == exportFile) {
            b->setText({}); b->setFixedWidth(32);
        }
        tools->addWidget(b);
        if (b==paletteToggle || b==remove) {
            auto* divider=new QFrame;divider->setProperty("divider",true);divider->setFixedSize(1,18);
            tools->addWidget(divider);
        }
    }
    tools->addStretch();
    resource = new QLabel(QStringLiteral("未绑定资源"));
    resource->setObjectName("workflowResource");
    resource->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    resource->setMinimumWidth(70);
    tools->addWidget(resource, 1, Qt::AlignRight);
    inspectToggle->setProperty("quiet", true);
    tools->addWidget(inspectToggle);
    root->addWidget(toolbar);
    issue = new QLabel;
    issue->setObjectName("workflowIssue");
    issue->setWordWrap(true);
    issue->setContentsMargins(16, 7, 16, 7);
    issue->setAccessibleName(QStringLiteral("流程问题"));
    issue->hide();
    root->addWidget(issue);
    vertical = new QSplitter(Qt::Vertical);
    vertical->setObjectName("workflowVerticalSplitter");
    vertical->setChildrenCollapsible(false);
    vertical->setHandleWidth(3);
    editor = new QSplitter;
    editor->setObjectName("workflowEditorSplitter");
    editor->setHandleWidth(1);
    palettePanel = new QWidget;
    palettePanel->setObjectName("workflowPalettePanel");
    palettePanel->setMinimumWidth(155);
    auto *left = new QVBoxLayout(palettePanel);
    left->setContentsMargins(12, 12, 12, 9);
    left->setSpacing(10);
    auto* libraryTitle=new QLabel(QStringLiteral("节点库")); libraryTitle->setObjectName("workflowLibraryTitle");
    auto* libraryHint=new QLabel(QStringLiteral("拖入画布")); libraryHint->setProperty("muted",true);
    auto* libraryHeading=new QHBoxLayout;libraryHeading->addWidget(libraryTitle);libraryHeading->addStretch();libraryHeading->addWidget(libraryHint);
    left->addLayout(libraryHeading);
    search = new QLineEdit;
    search->setObjectName("workflowNodeSearch");
    search->setPlaceholderText(QStringLiteral("搜索节点 / 协议"));
    search->setAccessibleName(QStringLiteral("搜索节点"));
    left->addWidget(search);
    paletteEmpty=new QLabel(QStringLiteral("没有匹配的节点\n试试协议名或步骤名称"));
    paletteEmpty->setObjectName("workflowPaletteEmpty");paletteEmpty->setProperty("muted",true);
    paletteEmpty->setWordWrap(true);paletteEmpty->hide();left->addWidget(paletteEmpty);
    palette = new Palette;
    palette->setObjectName("workflowPalette");
    palette->setHeaderHidden(true);
    palette->setIndentation(0);
    palette->setRootIsDecorated(false);
    palette->setDragEnabled(true);
    palette->setSelectionMode(QAbstractItemView::SingleSelection);
    palette->setAccessibleName(QStringLiteral("节点库：双击或回车添加，拖入画布"));
    palette->setItemDelegate(new PaletteDelegate(&dark,palette));
    palette->setMouseTracking(true);
    palette->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    left->addWidget(palette, 1);
    auto *hint = new QLabel(QStringLiteral("从出口拖至入口连接\nShift 框选 · 双击打开参数"));
    hint->setFont(design::font(10));
    left->addWidget(hint);
    QMap<QString, QTreeWidgetItem *> groups;
    for (const auto &def : workflowNodeDefinitions()) {
        auto *group = groups.value(def.group);
        if (!group) {
            group = new QTreeWidgetItem(palette, {def.group});
            group->setFlags(Qt::ItemIsEnabled);
            group->setFont(0, design::font(10));
            groups.insert(def.group, group);
        }
        auto *item = new QTreeWidgetItem(group, {def.name});
        item->setData(0, Qt::UserRole, def.type);
        item->setData(0, Qt::UserRole + 1, def.name + " " + def.protocol);
        item->setToolTip(0, def.description + QStringLiteral("；双击或拖入画布"));
        item->setSizeHint(0, {160, 32});
    }
    palette->expandAll();
    editor->addWidget(palettePanel);
    auto *canvas = new QWidget;
    canvas->setMinimumWidth(260);
    auto *middle = new QVBoxLayout(canvas);
    middle->setContentsMargins(0, 0, 0, 0);
    middle->setSpacing(0);
    auto *canvasHeader = new QWidget;
    canvasHeader->setObjectName("workflowCanvasHeader");
    auto* canvasHeading=new QHBoxLayout(canvasHeader); canvasHeading->setContentsMargins(18,8,18,8);
    canvasTitle=new QLabel(QStringLiteral("主流程"));canvasTitle->setProperty("muted",true);
    auto* canvasHint=new QLabel(QStringLiteral("拖动节点 · 空白处拖动平移"));canvasHint->setProperty("muted",true);
    canvasHeading->addWidget(canvasTitle);canvasHeading->addStretch();canvasHeading->addWidget(canvasHint);
    middle->addWidget(canvasHeader);
    scene->setNodeGeometry(std::make_unique<Geometry>(*model));
    scene->setNodePainter(std::make_unique<NodePainter>(&dark, runner));
    scene->setConnectionPainter(std::make_unique<EdgePainter>(&dark, runner));
    view->setObjectName("workflowCanvas");
    middle->addWidget(view, 1);
    auto *camera = new QHBoxLayout;
    camera->setContentsMargins(12, 5, 12, 5);
    auto *note = new QLabel(QStringLiteral("编辑不会连接设备或发送数据"));
    note->setFont(design::font(10));
    camera->addWidget(note, 1);
    auto *minus = button("−", "workflowZoomOut");
    zoom = button("100%", "workflowZoomReset");
    auto *plus = button("+", "workflowZoomIn");
    auto *fit = button(QStringLiteral("概览"), "workflowFit");
    for (auto *b : {minus, zoom, plus, fit}) {
        b->setMinimumWidth(0);
        camera->addWidget(b);
    }
    middle->addLayout(camera);
    editor->addWidget(canvas);
    inspector = new QWidget;
    inspector->setObjectName("workflowInspector");
    inspector->setMinimumWidth(245);
    auto *right = new QVBoxLayout(inspector);
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(0);
    auto* inspectorCaption=new QWidget;
    auto* captionRow=new QHBoxLayout(inspectorCaption);captionRow->setContentsMargins(18,12,12,8);
    auto* inspectorLabel=new QLabel(QStringLiteral("节点详情"));inspectorLabel->setObjectName("workflowInspectorLabel");
    auto* inspectorClose=button(QStringLiteral("×"),"workflowInspectorClose");inspectorClose->setProperty("quiet",true);
    inspectorClose->setFixedWidth(28);inspectorClose->setAccessibleName(QStringLiteral("收起节点详情"));
    captionRow->addWidget(inspectorLabel);captionRow->addStretch();captionRow->addWidget(inspectorClose);
    right->addWidget(inspectorCaption);
    auto* identity=new QWidget;identity->setObjectName("workflowNodeIdentity");
    auto* identityRow=new QHBoxLayout(identity);identityRow->setContentsMargins(18,12,18,12);
    inspectorIcon=new QLabel;inspectorIcon->setFixedSize(34,34);inspectorIcon->setObjectName("workflowInspectorIcon");
    auto* identityWords=new QVBoxLayout;identityWords->setSpacing(5);
    inspectorHead = new QLabel(QStringLiteral("节点详情"));
    inspectorHead->setObjectName("workflowInspectorHeading");inspectorHead->setWordWrap(true);inspectorHead->setTextFormat(Qt::PlainText);
    inspectorMeta=new QLabel;inspectorMeta->setObjectName("workflowInspectorMeta");inspectorMeta->setProperty("muted",true);
    identityWords->addWidget(inspectorHead);identityWords->addWidget(inspectorMeta);
    identityRow->addWidget(inspectorIcon);identityRow->addLayout(identityWords,1);
    right->addWidget(identity);
    connect(inspectorClose,&QPushButton::clicked,q,[this]{inspector->hide();});
    tabs = new QTabWidget;
    tabs->setObjectName("workflowInspectorTabs");
    parameters = new QScrollArea;
    parameters->setWidgetResizable(true);
    parameters->setFrameShape(QFrame::NoFrame);
    result = new QPlainTextEdit;
    result->setObjectName("workflowResult");
    result->setReadOnly(true);
    result->setFont(design::font(11, true));
    result->setAccessibleName(QStringLiteral("上次节点结果，敏感值已遮蔽"));
    auto* resultPage=new QWidget;
    auto* responseLayout=new QVBoxLayout(resultPage);responseLayout->setContentsMargins(14,12,14,12);responseLayout->setSpacing(10);
    resultStatus=new QLabel(QStringLiteral("尚未执行"));resultStatus->setObjectName("workflowResultStatus");
    resultDetail=new QLabel;resultDetail->setObjectName("workflowResultDetail");resultDetail->setWordWrap(true);resultDetail->setTextFormat(Qt::PlainText);resultDetail->setProperty("muted",true);
    responseTabs=new QTabWidget;responseTabs->setObjectName("workflowResponseTabs");
    responseBody=new QPlainTextEdit;responseBody->setObjectName("workflowResponseBody");responseBody->setReadOnly(true);
    responseHeaders=new QPlainTextEdit;responseHeaders->setObjectName("workflowResponseHeaders");responseHeaders->setReadOnly(true);
    for (auto* editor : {result,responseBody,responseHeaders}) editor->setFont(design::font(12,true));
    responseTabs->addTab(responseBody,QStringLiteral("Body"));
    responseTabs->addTab(responseHeaders,QStringLiteral("Headers"));
    responseTabs->addTab(result,QStringLiteral("完整结果"));
    responseLayout->addWidget(resultStatus);responseLayout->addWidget(resultDetail);responseLayout->addWidget(responseTabs,1);
    auto *resultExport = button(QStringLiteral("导出已遮蔽结果…"), "workflowResultExport");
    resultExport->setProperty("quiet",true);responseLayout->addWidget(resultExport);
    tabs->addTab(parameters, QStringLiteral("参数"));
    tabs->addTab(resultPage, QStringLiteral("上次结果"));
    right->addWidget(tabs, 1);
    auto* inspectorFooter=new QLabel(QStringLiteral("更改仅影响下一次运行"));inspectorFooter->setObjectName("workflowInspectorFooter");
    inspectorFooter->setContentsMargins(18,10,12,10);right->addWidget(inspectorFooter);
    editor->addWidget(inspector);
    editor->setStretchFactor(1, 1);
    vertical->addWidget(editor);
    logPanel = new QWidget;
    logPanel->setObjectName("workflowExecution");
    logPanel->setMinimumHeight(44);
    auto *bottom = new QVBoxLayout(logPanel);
    bottom->setContentsMargins(12, 4, 12, 6);
    bottom->setSpacing(2);
    auto *controls = new QHBoxLayout;
    recordTab=button(QStringLiteral("运行记录  0"),"workflowRecordsTab");
    variableTab=button(QStringLiteral("运行变量  0"),"workflowVariablesTab");
    for(auto* b : {recordTab,variableTab}) {b->setCheckable(true);b->setProperty("viewTab",true);controls->addWidget(b);}
    recordTab->setChecked(true);controls->addStretch();
    status = new QLabel(QStringLiteral("尚未运行"));
    status->setObjectName("workflowRunStatus");
    controls->addWidget(status);
    pause = button(QStringLiteral("暂停后续步骤"), "workflowPause");
    stop = button(QStringLiteral("停止"), "workflowStop");
    collapse = button(QStringLiteral("折叠"), "workflowLogToggle");
    for (auto *b : {pause, stop, collapse}) {b->setProperty("quiet",true);controls->addWidget(b);}
    bottom->addLayout(controls);
    logTabs = new QTabWidget;
    logTabs->setObjectName("workflowLogTabs");
    logTabs->tabBar()->hide();
    connect(recordTab,&QPushButton::clicked,q,[this]{logTabs->setCurrentIndex(0);recordTab->setChecked(true);variableTab->setChecked(false);});
    connect(variableTab,&QPushButton::clicked,q,[this]{logTabs->setCurrentIndex(1);variableTab->setChecked(true);recordTab->setChecked(false);});
    connect(logTabs,&QTabWidget::currentChanged,q,[this](int index){recordTab->setChecked(index==0);variableTab->setChecked(index==1);});
    logs = new QStandardItemModel(0, 5, q);
    logs->setHorizontalHeaderLabels({QStringLiteral("时间"), QStringLiteral("节点"), QStringLiteral("状态"),
                                     QStringLiteral("详情"), QStringLiteral("耗时")});
    variables = new QStandardItemModel(0, 3, q);
    variables->setHorizontalHeaderLabels(
        {QStringLiteral("变量"), QStringLiteral("类型"), QStringLiteral("值（敏感字段已遮蔽）")});
    auto setupTable = [this](QStandardItemModel *model, const QString &name) {
        auto *v = new EmptyTable(&dark,
            name=="workflowLogs"?QStringLiteral("运行后，在这里查看每一步的结果与耗时。") : QStringLiteral("运行后，在这里查看变量的类型与值。"),
            QStringLiteral("当前流程尚未执行；编辑不会连接设备或发送数据。"));
        v->setObjectName(name);
        v->setModel(model);
        v->setEditTriggers(QAbstractItemView::NoEditTriggers);
        v->setSelectionBehavior(QAbstractItemView::SelectRows);
        v->setSelectionMode(QAbstractItemView::SingleSelection);
        v->verticalHeader()->hide();
        v->verticalHeader()->setDefaultSectionSize(32);
        v->horizontalHeader()->setStretchLastSection(false);
        v->setAlternatingRowColors(false);
        v->setFont(design::font(11, true));
        return v;
    };
    logView = setupTable(logs, "workflowLogs");
    variableView = setupTable(variables, "workflowVariables");
    logView->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    for (auto p : QList<QPair<int, int>>{{0, 85}, {1, 140}, {2, 90}, {4, 75}})
        logView->setColumnWidth(p.first, p.second);
    variableView->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    variableView->setColumnWidth(0, 160);
    variableView->setColumnWidth(1, 85);
    logTabs->addTab(logView, QStringLiteral("运行记录"));
    logTabs->addTab(variableView, QStringLiteral("运行变量"));
    bottom->addWidget(logTabs, 1);
    vertical->addWidget(logPanel);
    vertical->setStretchFactor(0, 1);
    root->addWidget(vertical, 1);
    graphInfo = new QLabel;
    graphInfo->setObjectName("workflowGraphInfo");
    graphInfo->setContentsMargins(14, 5, 14, 5);
    graphInfo->setFont(design::font(10));
    root->addWidget(graphInfo);
    scene->undoStack().setUndoLimit(80);
    editor->setSizes({210, 640, 286});
    vertical->setSizes({540, 200});
    QSettings settings;
    editor->restoreState(settings.value("workflow/editorSizes").toByteArray());
    vertical->restoreState(settings.value("workflow/verticalSizes").toByteArray());
    if (settings.value("workflow/logCollapsed", false).toBool()) {
        logTabs->hide();
        logPanel->setMaximumHeight(44);
        collapse->setText(QStringLiteral("展开"));
    } else {
        logPanel->setMinimumHeight(140);
    }
    connect(paletteToggle, &QPushButton::clicked, q,
            [this] { palettePanel->setVisible(!palettePanel->isVisible()); });
    connect(inspectToggle, &QPushButton::clicked, q, [this] {
        inspector->setVisible(!inspector->isVisible());
        if (inspector->isVisible())
            reveal(selected);
    });
    connect(minus, &QPushButton::clicked, view, &GraphicsView::scaleDown);
    connect(plus, &QPushButton::clicked, view, &GraphicsView::scaleUp);
    connect(zoom, &QPushButton::clicked, q, [this] {
        view->setupScale(1);
        reveal(selected);
    });
    connect(fit, &QPushButton::clicked, q, [this] {
        view->zoomFitAll();
        zoom->setText(QString::number(qRound(view->getScale() * 100)) + "%");
    });
    connect(validate, &QPushButton::clicked, q, &WorkflowPage::validateFlow);
    connect(resultExport, &QPushButton::clicked, q, [this] {
        if (selected.isEmpty())
            return;
        auto file = QFileDialog::getSaveFileName(q, QStringLiteral("导出已遮蔽节点结果"),
                                                 "workflow-result.json", "JSON (*.json)");
        if (file.isEmpty())
            return;
        QSaveFile f(file);
        if (!f.open(QIODevice::WriteOnly) ||
            f.write(
                QJsonDocument(redact(runner->result(selected).output, {}, secrets()).toObject()).toJson()) <
                0 ||
            !f.commit())
            message(QStringLiteral("无法写入结果文件。"));
    });
}

void WorkflowPage::Impl::wire() {
    view->add = [this](QString type, QPointF pos) {
        if (runner->active())
            return;
        scene->undoStack().push(new CreateCommand(scene, type, pos));
        auto ids = model->allNodeIds();
        for (auto id : ids)
            if (auto *n = model->node(id); n && n->position == pos) {
                q->selectNode(n->id);
                break;
            }
    };
    connect(palette, &QTreeWidget::itemActivated, q, [this](QTreeWidgetItem *i, int) {
        auto type = i->data(0, Qt::UserRole).toString();
        if (!type.isEmpty())
            view->add(type, view->mapToScene(view->viewport()->rect().center()) - QPointF(104, 66));
    });
    connect(search, &QLineEdit::textChanged, q, [this] { filter(); });
    connect(model, &WorkflowGraphModel::documentChanged, q, [this] { updateDocumentUi(); });
    connect(model, &WorkflowGraphModel::editRejected, q, [this](QString why) { message(why); });
    connect(scene, &BasicGraphicsScene::nodeClicked, q, [this](NodeId id) {
        if (auto *n = model->node(id)) {
            selected = n->id;
            form();
            runtime();
        }
    });
    connect(scene, &BasicGraphicsScene::nodeDoubleClicked, q, [this](NodeId id) {
        if (auto *n = model->node(id))
            q->selectNode(n->id);
    });
    connect(scene, &QGraphicsScene::selectionChanged, q, [this] {
        auto items = scene->selectedItems();
        remove->setEnabled(!runner->active() && !items.isEmpty());
        copy->setEnabled(!runner->active() && !items.isEmpty());
    });
    connect(undo, &QPushButton::clicked, q, [this] {
        if (!runner->active()) {
            scene->undoStack().undo();
            form();
            runtime();
        }
    });
    connect(redo, &QPushButton::clicked, q, [this] {
        if (!runner->active()) {
            scene->undoStack().redo();
            form();
            runtime();
        }
    });
    auto deleteSelection = [this] {
        if (runner->active())
            return;
        auto bytes = QJsonDocument(model->document().toJson()).toJson(QJsonDocument::Compact).size() * 2;
        if (historyEstimate + bytes > HistoryBytes) {
            scene->undoStack().clear();
            historyEstimate = 0;
        }
        historyEstimate += bytes;
        view->onDeleteSelectedObjects();
    };
    connect(remove, &QPushButton::clicked, q, deleteSelection);
    auto duplicate = [this] {
        if (runner->active())
            return;
        auto before = model->document(), after = before;
        QMap<QString, QString> ids;
        for (auto *item : scene->selectedItems())
            if (auto *object = qgraphicsitem_cast<NodeGraphicsObject *>(item)) {
                if (auto *n = model->node(object->nodeId())) {
                    auto c = *n;
                    c.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    ids[n->id] = c.id;
                    c.position += QPointF(36, 38);
                    c.title += QStringLiteral(" 副本");
                    after.nodes.append(c);
                }
            }
        for (const auto &edge : before.edges)
            if (ids.contains(edge.from) && ids.contains(edge.to))
                after.edges.append({QUuid::createUuid().toString(QUuid::WithoutBraces), ids[edge.from],
                                    ids[edge.to], edge.port});
        if (after.nodes.size() > 256 || after.edges.size() > 512) {
            message(QStringLiteral("复制超过图容量（256 节点 / 512 连线）。"));
            return;
        }
        if (ids.isEmpty())
            return;
        mutate(
            QStringLiteral("复制节点"),
            [this, before] {
                model->setDocument(before);
                form();
            },
            [this, after] {
                model->setDocument(after);
                form();
            });
    };
    connect(copy, &QPushButton::clicked, q, duplicate);
    // Keep native text undo/copy/delete in inputs. Graph shortcuts have canvas-only scope.
    for (auto *action : view->actions()) {
        action->setShortcutContext(Qt::WidgetShortcut);
        if (action->shortcuts().contains(QKeySequence::Delete)) {
            disconnect(action, nullptr, view, nullptr);
            connect(action, &QAction::triggered, q, deleteSelection);
        }
        if (action->shortcuts().contains(QKeySequence(Qt::CTRL | Qt::Key_D))) {
            disconnect(action, nullptr, view, nullptr);
            connect(action, &QAction::triggered, q, duplicate);
        }
    }
    auto *connectAction = new QAction(QStringLiteral("连接节点端口"), view);
    connectAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    connectAction->setShortcutContext(Qt::WidgetShortcut);
    view->addAction(connectAction);
    connect(connectAction, &QAction::triggered, q, [this] {
        if (runner->active())
            return;
        QDialog dialog(q);
        dialog.setObjectName("workflowConnectDialog");
        dialog.setWindowTitle(QStringLiteral("连接节点端口"));
        auto *layout = new QFormLayout(&dialog);
        auto *from = new QComboBox;
        auto *port = new QComboBox;
        auto *to = new QComboBox;
        from->setAccessibleName(QStringLiteral("起点节点"));
        port->setAccessibleName(QStringLiteral("执行出口"));
        to->setAccessibleName(QStringLiteral("目标入口"));
        for (const auto &n : model->document().nodes) {
            auto id = model->graphId(n.id);
            if (!model->outputs(id).isEmpty())
                from->addItem(n.title, id);
            if (n.type != "start")
                to->addItem(n.title, id);
        }
        auto refresh = [this, from, port] {
            port->clear();
            for (const auto &name : model->outputs(from->currentData().toUInt()))
                port->addItem(portLabel(name), name);
        };
        connect(from, &QComboBox::currentIndexChanged, &dialog, refresh);
        from->setCurrentIndex(std::max(0, from->findData(model->graphId(selected))));
        refresh();
        layout->addRow(QStringLiteral("起点节点"), from);
        layout->addRow(QStringLiteral("执行出口"), port);
        layout->addRow(QStringLiteral("目标入口"), to);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addRow(buttons);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, [this, &dialog, from, port, to] {
            ConnectionId c{from->currentData().toUInt(), PortIndex(port->currentIndex()),
                           to->currentData().toUInt(), 0};
            if (!model->connectionPossible(c)) {
                message(QStringLiteral("连接无效：请选择可用出口与其他节点入口。"));
                return;
            }
            scene->undoStack().push(new ConnectCommand(scene, c));
            dialog.accept();
        });
        dialog.exec();
        view->setFocus(Qt::OtherFocusReason);
    });
    auto *saveAction = new QAction(q);
    saveAction->setShortcut(QKeySequence::Save);
    q->addAction(saveAction);
    connect(saveAction, &QAction::triggered, save, &QPushButton::click);
    connect(view, &GraphicsView::scaleChanged, q,
            [this](double scale) { zoom->setText(QString::number(qRound(scale * 100)) + "%"); });
    connect(&scene->undoStack(), &QUndoStack::indexChanged, q, [this](int index) {
        if (scene->undoStack().count() == 0)
            model->forgetHistory();
        undo->setEnabled(!runner->active() && scene->undoStack().canUndo());
        redo->setEnabled(!runner->active() && scene->undoStack().canRedo());
        if (index > historyCount) {
            historyEstimate +=
                QJsonDocument(model->document().toJson()).toJson(QJsonDocument::Compact).size() * 2;
        }
        historyCount = index;
        if (historyEstimate > HistoryBytes)
            QTimer::singleShot(0, q, [this] {
                scene->undoStack().clear();
                historyEstimate = 0;
                historyCount = 0;
                message(QStringLiteral("撤销历史已达到 16 MiB 容量，已清理；当前流程保留。"));
            });
    });
    connect(title, &QLineEdit::editingFinished, q, [this] {
        if (runner->active())
            return;
        auto before = model->document().name, after = title->text();
        if (before != after)
            mutate(
                QStringLiteral("修改流程名称"), [this, before] { model->setName(before); },
                [this, after] { model->setName(after); }, (before.size() + after.size()) * 2);
    });
    connect(templates, &QPushButton::clicked, q, &WorkflowPage::showTemplates);
    connect(run, &QPushButton::clicked, q, &WorkflowPage::startRun);
    connect(pause, &QPushButton::clicked, q, [this] {
        if (runner->state() == WorkflowRunState::Paused)
            runner->resume();
        else
            runner->pause();
    });
    connect(stop, &QPushButton::clicked, runner, &WorkflowRunner::stop);
    connect(save, &QPushButton::clicked, q, [this] {
        if (runner->active())
            return;
        auto filename = path;
        if (filename.isEmpty())
            filename = QFileDialog::getSaveFileName(q, QStringLiteral("保存工作流"), "workflow.pbflow.json",
                                                    QStringLiteral("工作流 (*.pbflow.json)"));
        if (filename.isEmpty())
            return;
        QString why;
        if (!q->saveFile(filename, &why))
            message(why);
    });
    connect(open, &QPushButton::clicked, q, [this] {
        if (runner->active() || !q->confirmLeave())
            return;
        auto filename = QFileDialog::getOpenFileName(q, QStringLiteral("导入工作流"), {},
                                                     QStringLiteral("工作流 (*.pbflow.json);;JSON (*.json)"));
        if (filename.isEmpty())
            return;
        QString why;
        if (!q->loadFile(filename, &why))
            message(why);
    });
    connect(exportFile, &QPushButton::clicked, q, [this] {
        if (runner->active())
            return;
        auto filename =
            QFileDialog::getSaveFileName(q, QStringLiteral("导出工作流（凭据已遮蔽）"),
                                         "workflow.pbflow.json", QStringLiteral("工作流 (*.pbflow.json)"));
        if (filename.isEmpty())
            return;
        auto previous = path;
        auto previousSaved = saved;
        QString why;
        if (!q->saveFile(filename, &why))
            message(why);
        path = previous;
        saved = previousSaved;
        updateDocumentUi();
    });
    connect(collapse, &QPushButton::clicked, q, [this] {
        bool visible = logTabs->isVisible();
        logTabs->setVisible(!visible);
        collapse->setText(visible ? QStringLiteral("展开") : QStringLiteral("折叠"));
        if (visible) {
            verticalSizes = vertical->sizes().toVector();
            logPanel->setMinimumHeight(44);
            logPanel->setMaximumHeight(44);
            vertical->setSizes({q->height() - 160, 44});
        } else {
            logPanel->setMaximumHeight(QWIDGETSIZE_MAX);
            logPanel->setMinimumHeight(140);
            vertical->setSizes(verticalSizes.toList());
        }
    });
    connect(logView, &QTableView::clicked, q, [this](QModelIndex index) {
        auto id = logs->index(index.row(), 0).data(Qt::UserRole).toString();
        q->selectNode(id, true);
    });
    connect(runner, &WorkflowRunner::stateChanged, q, [this] {
        runtime();
        form();
    });
    connect(runner, &WorkflowRunner::nodeChanged, q, [this](QString id) {
        scene->update();
        if (runner->result(id).state == WorkflowNodeState::Failed)
            q->selectNode(id, true);
        else if (runner->result(id).state == WorkflowNodeState::Running)
            reveal(id);
        if (selected == id)
            runtime();
    });
    connect(runner, &WorkflowRunner::logsChanged, q, [this] { records(); });
    connect(runner, &WorkflowRunner::variablesChanged, q, [this] { variableRows(); });
    connect(runner, &WorkflowRunner::resourceChanged, q, [this] { runtime(); });
    connect(runner, &WorkflowRunner::finished, q, [this](bool succeeded, QString why) {
        if (!succeeded && !why.isEmpty())
            message(redactText(why, secrets()));
        runtime();
    });
    runtime();
}
void WorkflowPage::Impl::filter() {
    auto term = search->text().trimmed();
    bool matched=false;
    for (int g = 0; g < palette->topLevelItemCount(); ++g) {
        auto *group = palette->topLevelItem(g);
        bool any = false;
        for (int i = 0; i < group->childCount(); ++i) {
            auto *item = group->child(i);
            bool visible = term.isEmpty() ||
                           item->data(0, Qt::UserRole + 1).toString().contains(term, Qt::CaseInsensitive);
            item->setHidden(!visible);
            any |= visible;
        }
        group->setHidden(!any);
        matched |= any;
    }
    paletteEmpty->setVisible(!matched);
}
void WorkflowPage::Impl::updateDocumentUi() {
    auto doc = model->document();
    QSignalBlocker blocker(title);
    title->setText(doc.name);
    title->setFixedWidth(std::clamp(QFontMetrics(design::font(21,false,true)).horizontalAdvance(doc.name)+12, 90, 350));
    title->setToolTip(doc.name + QStringLiteral("；点击编辑名称"));
    description->setText(doc.description.isEmpty()
        ? QStringLiteral("组合步骤，验证设备响应；只有显式运行才执行通信。")
        : doc.description);
    saveState->setText(q->isDirty()
                           ? (path.isEmpty() ? QStringLiteral("草稿 / 未保存") : QStringLiteral("未保存"))
                           : QStringLiteral("已保存"));
    canvasTitle->setText(QStringLiteral("主流程  /  %1 个节点").arg(doc.nodes.size()));
    graphInfo->setText(QStringLiteral("%1 个节点 · %2 条连线 · 编辑不会发送数据")
                           .arg(doc.nodes.size())
                           .arg(doc.edges.size()));
    QStringList names;
    for (const auto &n : doc.nodes)
        names.append(n.title);
    view->setAccessibleDescription(QStringLiteral("流程 %1。%2。%3 条执行连线。")
                                       .arg(doc.name, names.join(QStringLiteral("，")))
                                       .arg(doc.edges.size()));
    view->viewport()->update();
}
void WorkflowPage::Impl::reveal(const QString &id) {
    auto gid = model->graphId(id);
    if (auto *object = scene->nodeGraphicsObject(gid)) {
        if (view->getScale() < .85)
            view->setupScale(.85);
        const auto rect=view->mapFromScene(object->sceneBoundingRect()).boundingRect();
        if (!view->viewport()->rect().adjusted(25,25,-25,-25).contains(rect))
            view->ensureVisible(object, 30, 30);
    }
}
void WorkflowPage::selectNode(const QString &id, bool results) {
    auto gid = d->model->graphId(id);
    if (gid == InvalidNodeId)
        return;
    d->selected = id;
    d->scene->clearSelection();
    if (auto *item = d->scene->nodeGraphicsObject(gid))
        item->setSelected(true);
    d->inspector->show();
    d->form();
    d->runtime();
    if (results)
        d->tabs->setCurrentIndex(1);
    d->reveal(id);
}

void WorkflowPage::Impl::form() {
    auto *body = new QWidget;
    body->setObjectName("workflowParameterBody");
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(14, 12, 14, 14);
    layout->setSpacing(7);
    auto *n = model->node(model->graphId(selected));
    const bool locked = runner->active();
    if (!n) {
        inspectorHead->setText(QStringLiteral("流程参数"));
        inspectorMeta->setText(QStringLiteral("选择节点查看设置"));
        inspectorIcon->setPixmap(design::icon(design::Icon::Workflow,categoryColor("variable",dark),20).pixmap(20,20));
        auto *help = new QLabel(QStringLiteral("从节点库拖入步骤，从出口拖至入口建立执行顺序。\n分支使用通过 "
                                               "/ 不通过，有限循环使用循环体 / 退出。"));
        help->setWordWrap(true);
        layout->addWidget(help);
        auto *label = new QLabel(QStringLiteral("初始变量（JSON 对象）"));
        layout->addWidget(label);
        auto *values = new QPlainTextEdit(jsonText(model->document().initialVariables));
        values->setObjectName("workflowInitialVariables");
        values->setReadOnly(locked);
        values->setMaximumHeight(180);
        layout->addWidget(values);
        connect(values, &QPlainTextEdit::textChanged, q, [this, values] {
            if (runner->active())
                return;
            auto parsed = QJsonDocument::fromJson(values->toPlainText().toUtf8());
            if (!parsed.isObject()) {
                message(QStringLiteral("初始变量必须是 JSON 对象。"));
                return;
            }
            auto before = model->document(), after = before;
            after.initialVariables = parsed.object();
            if (after.initialVariables == before.initialVariables)
                return;
            mutate(
                QStringLiteral("修改初始变量"), [this, before] { model->setDocument(before); },
                [this, after] { model->setDocument(after); });
        });
        layout->addWidget(new QLabel(QStringLiteral(
            "上限：256 节点 / 512 连线\n10,000 步 · 总截止时间 10 分钟\n默认单消息 8 MiB · 日志 2,000 条")));
        layout->addStretch();
        parameters->setWidget(body);
        return;
    }
    auto node = *n;
    auto p = node.parameters;
    for (const auto &def : workflowNodeDefinitions())
        if (def.type == node.type) {
            auto merged = def.defaults;
            for (auto it = p.begin(); it != p.end(); ++it)
                merged[it.key()] = it.value();
            p = merged;
            break;
        }
    inspectorHead->setText(node.title);
    inspectorMeta->setText(protocolTag(node.type) + " · " + node.id.left(12));
    inspectorMeta->setToolTip(node.id);
    inspectorIcon->setPixmap(design::icon(nodeIcon(node.type),categoryColor(node.type,dark),20).pixmap(20,20));
    inspectorHead->setAccessibleName(QStringLiteral("节点详情：%1，稳定标识 %2").arg(node.title, node.id));
    auto label = [&](QString text) {
        auto *l = new QLabel(text);
        l->setWordWrap(true);
        l->setFont(design::font(11));
        layout->addWidget(l);
    };
    auto line = [&](QString name, QString key, QString fallback = QString()) {
        label(name);
        const auto text = p.contains(key) ? jsonText(p[key]) : fallback;
        const int limit = key == "url" ? 4096 : 32768;
        const bool oversized = text.size() > limit;
        auto *e = new QLineEdit(oversized ? text.left(limit) : text);
        e->setObjectName("workflowParam_" + key);
        e->setAccessibleName(name);
        e->setMaxLength(limit);
        e->setEnabled(!locked);
        e->setReadOnly(oversized);
        if (oversized)
            e->setToolTip(QStringLiteral("内容较大，仅显示前部；原始参数保留，可通过变量或工作流文件修改。"));
        layout->addWidget(e);
        if (!oversized)
            connect(e, &QLineEdit::editingFinished, q,
                    [this, e, key, id = node.id] { setParameter(key, e->text(), id); });
        return e;
    };
    auto area = [&](QString name, QString key, QString fallback = QString()) {
        label(name);
        const auto text = p.contains(key) ? jsonText(p[key]) : fallback;
        const bool oversized = text.size() > 262144;
        auto *e =
            new QPlainTextEdit(oversized ? text.left(PreviewCharacters) +
                                               QStringLiteral("\n… 内容较大，编辑预览已截断；原始参数保留。")
                                         : text);
        e->setObjectName("workflowParam_" + key);
        e->setAccessibleName(name);
        e->setFont(design::font(11, true));
        e->setMinimumHeight(96);
        e->setMaximumHeight(180);
        e->setReadOnly(locked || oversized);
        layout->addWidget(e);
        connect(e, &QPlainTextEdit::textChanged, q, [this, e, key, id = node.id, oversized] {
            if (oversized)
                return;
            if (e->toPlainText().size() > 1024 * 1024) {
                message(QStringLiteral("编辑内容超过 1 MiB 表单限额，请通过变量或文件调整流程。"));
                return;
            }
            setParameter(key, e->toPlainText(), id);
        });
        return e;
    };
    auto combo = [&](QString name, QString key, QList<QPair<QString, QString>> options,
                     QString fallback = QString()) {
        label(name);
        auto *e = new QComboBox;
        e->setObjectName("workflowParam_" + key);
        e->setAccessibleName(name);
        for (const auto &pair : options)
            e->addItem(pair.first, pair.second);
        auto current = p.value(key).toString(fallback);
        auto index = e->findData(current);
        if (index < 0 && !current.isEmpty()) {
            e->addItem(current, current);
            index = e->count() - 1;
        }
        e->setCurrentIndex(std::max(0, index));
        e->setEnabled(!locked);
        layout->addWidget(e);
        connect(e, &QComboBox::currentIndexChanged, q,
                [this, e, key, id = node.id] {
                    setParameter(key, e->currentData().toString(), id);
                    if (key == "resource" || key == "match")
                        QTimer::singleShot(0,q,[this,id]{if (selected==id) form();});
                });
        return e;
    };
    auto check = [&](QString name, QString key, bool fallback) {
        auto *e = new QCheckBox(name);
        e->setObjectName("workflowParam_" + key);
        e->setAccessibleName(name);
        e->setChecked(p.value(key).toBool(fallback));
        e->setEnabled(!locked);
        layout->addWidget(e);
        connect(e, &QCheckBox::toggled, q, [this, e, key, id = node.id](bool v) {
            if (key == "verifyTls" && !v) {
                QSignalBlocker blocker(e);
                e->setChecked(true);
                message(QStringLiteral("TLS 连接始终验证证书链与服务器名称；可选择自定义 CA 文件。"));
                return;
            }
            setParameter(key, v, id);
        });
    };
    auto json = [&](QString name, QString key, QJsonObject fallback) {
        label(name);
        auto *e = new QPlainTextEdit(jsonText(p.value(key).isObject() ? p.value(key) : QJsonValue(fallback)));
        e->setObjectName("workflowParam_" + key);
        e->setAccessibleName(name);
        e->setMinimumHeight(100);
        e->setMaximumHeight(170);
        e->setReadOnly(locked);
        layout->addWidget(e);
        auto *apply = button(QStringLiteral("应用 ") + name, "workflowApply_" + key);
        apply->setEnabled(!locked);
        layout->addWidget(apply);
        connect(apply, &QPushButton::clicked, q, [this, e, key, id = node.id] {
            QJsonParseError error;
            auto doc = QJsonDocument::fromJson(e->toPlainText().toUtf8(), &error);
            if (!doc.isObject()) {
                message(QStringLiteral("%1 必须是 JSON 对象：%2").arg(key, error.errorString()));
                return;
            }
            setParameter(key, doc.object(), id);
            message(QStringLiteral("已应用配置；请检查流程。"));
        });
    };
    auto framing = [&] {
        label(QStringLiteral("TCP / 串口分帧模式（UDP / WS 保留完整边界）"));
        auto frame = p.value("framing").toObject();
        auto *mode = new QComboBox;
        mode->setObjectName("workflowFramingMode");
        mode->setAccessibleName(QStringLiteral("TCP 或串口分帧模式"));
        mode->addItem(QStringLiteral("分隔符"), "delimiter");
        mode->addItem(QStringLiteral("固定长度"), "fixed");
        mode->addItem(QStringLiteral("长度字段"), "lengthHeader");
        mode->setCurrentIndex(std::max(0, mode->findData(frame.value("mode").toString("delimiter"))));
        mode->setEnabled(!locked);
        layout->addWidget(mode);
        connect(mode, &QComboBox::currentIndexChanged, q, [this, mode, id = node.id] {
            auto *n = model->node(model->graphId(id));
            if (!n)
                return;
            auto f = n->parameters.value("framing").toObject();
            auto chosen = mode->currentData().toString();
            f["mode"] = chosen;
            if (chosen == "fixed" && !f.contains("length"))
                f["length"] = 8;
            if (chosen == "delimiter" && !f.contains("delimiter")) {
                f["delimiter"] = "0A";
                f["delimiterFormat"] = "HEX";
            }
            if (chosen == "lengthHeader") {
                if (!f.contains("headerBytes"))
                    f["headerBytes"] = 2;
                if (!f.contains("offset"))
                    f["offset"] = 0;
                if (!f.contains("byteOrder"))
                    f["byteOrder"] = "big";
            }
            setParameter("framing", f, id);
        });
        for (const auto &spec :
             QList<QPair<QString, QString>>{{"delimiter", QStringLiteral("分隔符（默认 HEX 0A）")},
                                            {"length", QStringLiteral("固定帧长度 / 字节")},
                                            {"headerBytes", QStringLiteral("长度字段宽度 / 1–4 字节")},
                                            {"offset", QStringLiteral("长度字段偏移 / 字节")}}) {
            label(spec.second);
            auto *e = new QLineEdit(jsonText(frame.contains(spec.first)
                                                 ? frame.value(spec.first)
                                                 : (spec.first == "delimiter"     ? QJsonValue("0A")
                                                    : spec.first == "length"      ? QJsonValue(8)
                                                    : spec.first == "headerBytes" ? QJsonValue(2)
                                                                                  : QJsonValue(0))));
            e->setObjectName("workflowFraming_" + spec.first);
            e->setAccessibleName(spec.second);
            e->setEnabled(!locked);
            layout->addWidget(e);
            connect(e, &QLineEdit::editingFinished, q, [this, e, mode, key = spec.first, id = node.id] {
                auto *n = model->node(model->graphId(id));
                if (!n)
                    return;
                auto f = n->parameters.value("framing").toObject();
                f["mode"] = mode->currentData().toString();
                if (key == "delimiter") {
                    f[key] = e->text();
                    f["delimiterFormat"] = "HEX";
                } else {
                    bool ok = false;
                    auto number = e->text().toInt(&ok);
                    if (!ok) {
                        message(QStringLiteral("分帧长度和偏移必须是整数。"));
                        return;
                    }
                    f[key] = number;
                }
                setParameter("framing", f, id);
            });
        }
    };
    label(QStringLiteral("节点名称"));
    auto *nodeTitle = new QLineEdit(node.title);
    nodeTitle->setObjectName("workflowNodeTitle");
    nodeTitle->setAccessibleName(QStringLiteral("节点名称"));
    nodeTitle->setMaxLength(256);
    nodeTitle->setEnabled(!locked);
    layout->addWidget(nodeTitle);
    connect(nodeTitle, &QLineEdit::editingFinished, q, [this, nodeTitle, id = node.id] {
        if (runner->active())
            return;
        auto *old = model->node(model->graphId(id));
        if (!old || old->title == nodeTitle->text())
            return;
        auto before = *old, after = before;
        after.title = nodeTitle->text();
        mutate(
            QStringLiteral("修改节点名称"),
            [this, before] { model->updateNode(before.id, before.title, before.parameters); },
            [this, after] { model->updateNode(after.id, after.title, after.parameters); });
    });
    const auto type = node.type;
    auto* mainForm = layout;
    auto section = [&](const QString& caption, const QString& key) {
        auto* group=new FoldSection(caption,key);
        mainForm->addWidget(group);
        layout=new QVBoxLayout(group->contents());
        layout->setContentsMargins(0,4,0,4);layout->setSpacing(8);
    };
    auto endSection = [&] { layout=mainForm; };
    if (type == "http") {
        combo(QStringLiteral("请求方法"), "method",
              {{"GET", "GET"},
               {"POST", "POST"},
               {"PUT", "PUT"},
               {"PATCH", "PATCH"},
               {"DELETE", "DELETE"},
               {"HEAD", "HEAD"},
               {"OPTIONS", "OPTIONS"}});
        line(QStringLiteral("请求 URL"), "url");
        combo(QStringLiteral("请求体模式"), "bodyMode",
              {{QStringLiteral("JSON / 原文"), "text"}, {QStringLiteral("JSON"), "json"}}, "text");
        area(QStringLiteral("Body / 请求体"), "body")->setMaximumHeight(88);
        section(QStringLiteral("请求头"),"headers");
        area(QStringLiteral("每行一个；支持 ${变量}"), "headers");
        endSection();
        line(QStringLiteral("期望状态：200、2xx 或 any"), "expectedStatus");
        line(QStringLiteral("响应输出变量"), "output");
        section(QStringLiteral("超时、容量与 TLS"),"advanced");
        line(QStringLiteral("连接超时 / ms"), "connectTimeout");
        line(QStringLiteral("总超时 / ms"), "timeout");
        line(QStringLiteral("最大响应 / MiB"), "limit");
        check(QStringLiteral("TLS 证书链与服务器名称验证（始终开启）"), "verifyTls", true);
        line(QStringLiteral("自定义 CA 文件 / 可选"), "caFile");
        endSection();
        label(QStringLiteral("非 2xx 响应仍保留 status、headers 和 body；按期望状态决定结果。"));
    } else if (type == "ws") {
        line(QStringLiteral("WebSocket URL（ws / wss）"), "url");
        line(QStringLiteral("子协议 / 可选"), "subprotocol");
        line(QStringLiteral("连接超时 / ms"), "timeout");
        section(QStringLiteral("握手请求头"),"headers");
        area(QStringLiteral("每行一个；支持 ${变量}"), "headers");
        endSection();
        section(QStringLiteral("消息容量与 TLS"),"advanced");
        line(QStringLiteral("完整消息上限 / MiB"), "limit");
        check(QStringLiteral("TLS 证书链与服务器名称验证（始终开启）"), "verifyTls", true);
        line(QStringLiteral("自定义 CA 文件 / 可选"), "caFile");
        endSection();
        label(QStringLiteral("握手成功后进入下一步；文本和二进制消息保持完整边界，自动重连关闭。"));
    } else if (type == "raw") {
        combo(QStringLiteral("资源归属"), "ownership",
              {{QStringLiteral("借用已有活动连接"), "borrow"}, {QStringLiteral("流程建立并释放"), "owned"}},
              "borrow");
        label(QStringLiteral("选择方案并保存配置副本（不会打开连接）"));
        auto *list = new QComboBox;
        list->setObjectName("workflowRawProfile");
        list->setAccessibleName(QStringLiteral("选择连接方案配置副本"));
        list->addItem(QStringLiteral("选择一个方案…"));
        auto choices = profiles ? profiles() : QVector<ConnectionConfig>();
        for (const auto &c : choices)
            list->addItem(QString::fromStdString(c.name));
        list->setEnabled(!locked);
        layout->addWidget(list);
        connect(list, &QComboBox::currentIndexChanged, q, [this, list, choices, id = node.id](int index) {
            if (index <= 0 || index > choices.size())
                return;
            setParameter("config", workflowConnectionConfigToJson(choices[index - 1]), id);
            setParameter("profile", list->currentText(), id);
        });
        json(QStringLiteral("连接配置快照"), "config", workflowConnectionConfigToJson(ConnectionConfig{}));
        line(QStringLiteral("连接等待超时 / ms"), "timeout", "5000");
        label(QStringLiteral("借用结束时保留连接；流程建立的连接在结束后释放。借用绑定明确活动会话，不按名称"
                             "或列表位置查找。"));
    } else if (type == "send" || type == "sendWait" || type == "wait") {
        combo(QStringLiteral("绑定资源"), "resource",
              {{QStringLiteral("串口 / TCP / UDP"), "raw"}, {QStringLiteral("WebSocket"), "ws"}}, "raw");
        const bool webSocket=p.value("resource").toString("raw")=="ws";
        if (webSocket)
            combo(QStringLiteral("完整消息类型"), "messageType",
                  {{QStringLiteral("文本"), "text"}, {QStringLiteral("二进制"), "binary"}}, "text");
        if (type != "wait") {
            combo(QStringLiteral("发送格式"), "format",
                  {{"HEX", "HEX"},
                   {QStringLiteral("文本 / UTF-8"), QStringLiteral("文本 / UTF-8")},
                   {QStringLiteral("二进制变量"), QStringLiteral("二进制变量")}},
                  "HEX");
            area(QStringLiteral("发送内容 / 变量引用 ${name}"), "payload");
            if (!webSocket) {
                section(QStringLiteral("发送目标"),"target");
                line(QStringLiteral("UDP 目标 host:port / 可选"), "target");
                line(QStringLiteral("TCP 服务端客户端标识 / 可选"), "clientId");
                check(QStringLiteral("TCP 服务端广播（显式选择）"), "broadcast", false);
                endSection();
            }
        }
        if (type != "send") {
            combo(QStringLiteral("响应匹配"), "match",
                  {{QStringLiteral("JSON 字段"), QStringLiteral("JSON 字段")},
                   {QStringLiteral("字节相等"), QStringLiteral("字节相等")},
                   {QStringLiteral("字节前缀"), QStringLiteral("字节前缀")},
                   {QStringLiteral("字节包含"), QStringLiteral("字节包含")},
                   {QStringLiteral("任意完整消息"), "any"}},
                  "any");
            const auto match=p.value("match").toString("any");
            if (match=="JSON 字段" || match=="json")
                line(QStringLiteral("JSON 字段路径"), "path", "$.type");
            if (match!="any") {
                line(QStringLiteral("预期值"), "expected");
                if (match!="JSON 字段" && match!="json")
                    combo(QStringLiteral("预期字节格式"), "expectedFormat",
                          {{QStringLiteral("UTF-8"), "UTF-8"}, {"HEX", "HEX"}}, "UTF-8");
            }
            line(QStringLiteral("等待超时 / ms"), "timeout", "5000");
            line(QStringLiteral("响应输出变量"), "output", "message");
            if (!webSocket) {
                section(QStringLiteral("来源限定与流分帧"),"framing");
                json(QStringLiteral("来源限定：address / port / clientId"), "sourceFilter", {});
                framing();
                json(QStringLiteral("TCP / 串口分帧（高级 JSON）"), "framing",
                     {{"mode", "delimiter"},
                      {"delimiter", "0A"},
                      {"delimiterFormat", "HEX"},
                      {"includeDelimiter", false}});
                endSection();
            }
            label(QStringLiteral("分帧模式：delimiter（分隔符）、fixed（length）、lengthHeader（headerBytes、"
                                 "offset、byteOrder、includesHeader）。UDP / WebSocket "
                                 "直接使用完整边界。先登记匹配再发送；暂停仅影响后续步骤。"));
        }
    } else if (type == "assert" || type == "branch") {
        line(type == "assert" ? QStringLiteral("输入字段") : QStringLiteral("运行变量"),
             type == "assert" ? "source" : "variable");
        combo(QStringLiteral("比较关系"), "operator",
              {{QStringLiteral("相等"), "equals"},
               {QStringLiteral("不等"), "notEquals"},
               {QStringLiteral("包含"), "contains"},
               {QStringLiteral("大于"), "greater"},
               {QStringLiteral("小于"), "less"},
               {QStringLiteral("存在"), "exists"}},
              "equals");
        line(QStringLiteral("期望值"), "expected");
        label(type == "branch"
                  ? QStringLiteral("通过 / 不通过是正常分支；错误出口用于参数或执行错误。")
                  : QStringLiteral("显示实际值与期望值；不通过沿错误出口处理，未连接则失败结束。"));
    } else if (type == "extract") {
        line(QStringLiteral("输入变量"), "source");
        line(QStringLiteral("JSON 字段路径"), "path");
        line(QStringLiteral("输出变量"), "variable");
        label(QStringLiteral("字段缺失时明确失败；凭据在结果、日志、变量和导出中遮蔽。"));
    } else if (type == "loop") {
        line(QStringLiteral("最大循环次数"), "count");
        label(
            QStringLiteral("循环体必须返回此节点，退出端口连接后续步骤。执行还受最大步数与总截止时间限制。"));
    } else if (type == "delay")
        line(QStringLiteral("等待时间 / ms"), "duration");
    else if (type == "variable") {
        line(QStringLiteral("变量名称"), "variable");
        combo(QStringLiteral("变量类型"), "valueType",
              {{QStringLiteral("字符串"), "string"},
               {QStringLiteral("数字"), "number"},
               {QStringLiteral("布尔"), "bool"}},
              "string");
        line(QStringLiteral("变量值（布尔为 true / false）"), "value");
    } else if (type == "log")
        area(QStringLiteral("步骤日志内容"), "text");
    else if (type == "close") {
        line(QStringLiteral("WebSocket 关闭码"), "code");
        line(QStringLiteral("关闭原因"), "reason");
        label(QStringLiteral("释放借用租约时保留原始连接；流程拥有的连接关闭并释放。"));
    } else
        label(type == "start" ? QStringLiteral("显式运行后，从此处启动不可变执行快照。")
                              : QStringLiteral("流程结束，释放流程拥有的资源，保留借用连接。"));
    layout->addStretch();
    parameters->setWidget(body);
}

void WorkflowPage::Impl::runtime() {
    const bool active = runner->active();
    model->setLocked(active);
    for (auto *b : {run, save, templates, open, exportFile})
        b->setEnabled(!active);
    title->setEnabled(!active);
    palette->setEnabled(!active);
    undo->setEnabled(!active && scene->undoStack().canUndo());
    redo->setEnabled(!active && scene->undoStack().canRedo());
    copy->setEnabled(!active && !scene->selectedItems().isEmpty());
    remove->setEnabled(!active && !scene->selectedItems().isEmpty());
    pause->setEnabled(active);
    stop->setEnabled(active);
    pause->setText(runner->state() == WorkflowRunState::Paused ? QStringLiteral("继续执行")
                                                               : QStringLiteral("暂停后续步骤"));
    for (auto *action : view->actions()) {
        bool mutation = false;
        for (const auto &key : action->shortcuts())
            if (key == QKeySequence::Undo || key == QKeySequence::Redo || key == QKeySequence::Delete ||
                key == QKeySequence::Paste || key == QKeySequence(Qt::CTRL | Qt::Key_D))
                mutation = true;
        if (mutation)
            action->setEnabled(!active);
    }
    QString state;
    switch (runner->state()) {
    case WorkflowRunState::Idle:
        state = QStringLiteral("尚未运行");
        break;
    case WorkflowRunState::Running:
        state = QStringLiteral("执行中 · 当前步骤仍按截止时间运行");
        break;
    case WorkflowRunState::Paused:
        state = QStringLiteral("已暂停后续步骤 · 当前操作继续");
        break;
    case WorkflowRunState::Completed:
        state = QStringLiteral("已完成");
        break;
    case WorkflowRunState::Failed:
        state = QStringLiteral("执行失败 · 查看节点结果");
        break;
    case WorkflowRunState::Stopped:
        state = QStringLiteral("已停止 · 后续步骤已取消");
        break;
    }
    status->setText(state);
    resource->setText(redactText(runner->resourceSummary(), secrets()));
    resource->setToolTip(resource->text());
    auto r = runner->result(selected);
    auto values = secrets();
    QString content;
    if (selected.isEmpty())
        content = QStringLiteral("选择节点查看上次结果。");
    else if (r.state == WorkflowNodeState::Pending)
        content = QStringLiteral("这个节点尚未执行。运行后可查看响应、状态与错误详情。");
    else
        content =
            QStringLiteral("%1 · %2 ms\n运行 %3\n节点 %4\n\n%5\n\n%6")
                .arg(nodeState(r.state))
                .arg(r.elapsedMs)
                .arg(runner->runId(), selected, redactText(r.detail, values), preview(r.output, values));
    result->setPlainText(content);
    resultStatus->setText(r.state==WorkflowNodeState::Pending ? QStringLiteral("尚未执行") :
        nodeState(r.state) + QStringLiteral(" · %1 ms").arg(r.elapsedMs) +
        (r.output.contains("status") ? QStringLiteral(" · HTTP %1").arg(r.output.value("status").toInt()) : QString()));
    resultStatus->setStyleSheet(r.state==WorkflowNodeState::Failed
        ? QStringLiteral("color:%1").arg(dark ? "#ef7e7e" : "#a93731") : QString());
    resultStatus->setToolTip(QStringLiteral("运行 %1\n节点 %2").arg(runner->runId(),selected));
    resultDetail->setText(redactText(r.detail.left(2048),values));
    const bool http=r.output.contains("status") && r.output.contains("headers");
    const bool message=r.output.contains("bytesBase64");
    responseTabs->setTabVisible(0,http || message);
    responseTabs->setTabVisible(1,http);
    if (http) {
        responseTabs->setTabText(0,QStringLiteral("Body"));
        responseBody->setPlainText(preview(r.output.value("body"),values));
        responseHeaders->setPlainText(preview(r.output.value("headers"),values));
    } else if (message) {
        const bool binary=r.output.value("binary").toBool();
        responseTabs->setTabText(0,binary?QStringLiteral("HEX"):QStringLiteral("消息"));
        if (binary) {
            // Inspect a bounded prefix beyond the visible bytes to catch credentials
            // crossing the preview boundary, without scanning the full message on the GUI thread.
            const auto encoded=r.output.value("bytesBase64").toString();
            const auto bytes=redact(encoded.left(21844),"bytesBase64",values).toString();
            responseBody->setPlainText(bytes.startsWith('[') ? bytes :
                QStringLiteral("总长度 %1 字节 · 最多显示前 4095 字节\n\n%2")
                    .arg(r.output.value("length").toInt())
                    .arg(QString::fromLatin1(QByteArray::fromBase64(bytes.left(5460).toLatin1()).toHex(' '))));
        } else responseBody->setPlainText(preview(r.output.value("text"),values));
        responseHeaders->clear();
    } else {responseBody->clear();responseHeaders->clear();}
    scene->update();
}
void WorkflowPage::Impl::records() {
    auto entries = runner->logs();
    const int max = std::min(model->document().limits.maxLogEntries, 2000);
    int begin = std::max(0, int(entries.size()) - max);
    auto values = secrets();
    if (displayedRun != runner->runId()) {
        displayedRun = runner->runId();
        logs->removeRows(0, logs->rowCount());
        hasLastLog = false;
    }
    if (hasLastLog) {
        bool found = false;
        for (int i = int(entries.size()) - 1; i >= begin; --i) {
            const auto &e = entries[i];
            if (e.timestampMs == lastLog.timestampMs && e.nodeId == lastLog.nodeId &&
                e.status == lastLog.status && e.detail == lastLog.detail) {
                begin = i + 1;
                found = true;
                break;
            }
        }
        if (!found)
            logs->removeRows(0, logs->rowCount());
    }
    for (int i = begin; i < entries.size(); ++i) {
        const auto &entry = entries[i];
        QList<QStandardItem *> row;
        for (const auto &text :
             QStringList{QDateTime::fromMSecsSinceEpoch(entry.timestampMs).toString("HH:mm:ss.zzz"),
                         entry.nodeTitle, entry.status, redactText(entry.detail.left(2048), values),
                         QString::number(entry.elapsedMs) + " ms"}) {
            auto *item = new QStandardItem(text);
            item->setToolTip(text);
            row.append(item);
        }
        row[0]->setData(entry.nodeId, Qt::UserRole);
        logs->appendRow(row);
    }
    if (logs->rowCount() > max)
        logs->removeRows(0, logs->rowCount() - max);
    if (!entries.isEmpty()) {
        lastLog = entries.last();
        hasLastLog = true;
    }
    logTabs->setTabText(0, QStringLiteral("运行记录 %1").arg(logs->rowCount()));
    recordTab->setText(logTabs->tabText(0));
    if (logs->rowCount())
        logView->scrollToBottom();
    runtime();
}
void WorkflowPage::Impl::variableRows() {
    auto object = runner->variables();
    auto values = secrets();
    variables->removeRows(0, variables->rowCount());
    int count = 0;
    for (auto it = object.constBegin(); it != object.constEnd() && count < 1024; ++it, ++count) {
        const auto v = it.value();
        QString type = v.isObject()   ? "object"
                       : v.isArray()  ? "array"
                       : v.isBool()   ? "bool"
                       : v.isDouble() ? "number"
                       : v.isString() ? "string"
                                      : "null";
        variables->appendRow({new QStandardItem(it.key()), new QStandardItem(type),
                              new QStandardItem(preview(v, values, it.key()).left(4096))});
    }
    logTabs->setTabText(1, QStringLiteral("运行变量 %1").arg(variables->rowCount()));
    variableTab->setText(logTabs->tabText(1));
    runtime();
}
void WorkflowPage::validateFlow() {
    if (auto *focused = QApplication::focusWidget())
        focused->clearFocus();
    auto issues = document().validate();
    if (issues.isEmpty()) {
        d->message(QStringLiteral("检查通过：顺序、连线与基础参数有效。"));
        return;
    }
    auto first = issues.first();
    for (const auto &i : issues)
        if (!i.nodeId.isEmpty()) {
            first = i;
            break;
        }
    d->message(QStringLiteral("%1（%2 项问题）").arg(first.message).arg(issues.size()));
    if (!first.nodeId.isEmpty()) {
        selectNode(first.nodeId);
        if (auto *field = d->parameters->findChild<QWidget *>("workflowParam_" + first.field)) {
            for(auto* parent=field->parentWidget();parent;parent=parent->parentWidget())
                if (auto* section=dynamic_cast<FoldSection*>(parent)) section->expand();
            d->parameters->ensureWidgetVisible(field);
            field->setFocus();
        }
    }
}
void WorkflowPage::startRun() {
    if (d->runner->active())
        return;
    if (auto *focus = QApplication::focusWidget())
        focus->clearFocus();
    const auto snapshot = document();
    auto issues = snapshot.validate();
    if (!issues.isEmpty()) {
        validateFlow();
        return;
    }
    QString why;
    if (d->prepare && !d->prepare(snapshot, &why)) {
        d->message(why.isEmpty() ? QStringLiteral("运行资源准备被拒绝；当前流程未启动。") : why);
        return;
    }
    if (!d->runner->start(snapshot, &why)) {
        d->message(why);
        return;
    }
    d->message({});
    d->logPanel->setMaximumHeight(QWIDGETSIZE_MAX);
    d->logPanel->setMinimumHeight(140);
    d->logTabs->show();
    d->collapse->setText(QStringLiteral("折叠"));
    d->runtime();
}
void WorkflowPage::setDarkTheme(bool dark) {
    d->dark = dark;
    setProperty("darkTheme", dark);
    const QString panel = dark ? "#171c1f" : "#ffffff", side = dark ? "#14191c" : "#f7f9f9",
                  ink = dark ? "#e5edee" : "#213336", muted = dark ? "#b5c4cb" : "#435960",
                  line = dark ? "#2b3438" : "#d5dfe1", accent = dark ? "#85dec4" : "#176e58";
    setStyleSheet(
        QStringLiteral(
            "QWidget#workflowPage { background:%1; color:%2; } QWidget { color:%2; font-size:16px; } "
            "QLabel { font-size:16px; } QWidget#workflowHeading { background:%1; border-bottom:1px solid %4; } "
            "QWidget#workflowExecution { background:%1; border-top:1px solid %4; } "
            "QWidget#workflowPalettePanel, QWidget#workflowInspector, QWidget#workflowToolbar, "
            "QWidget#workflowParameterBody { background:%3; } QWidget#workflowToolbar { border-bottom:1px solid %4; } "
            "QLabel[muted=\"true\"], QLabel#workflowResource, QLabel#workflowRunStatus { color:%6; font-size:14px; } "
            "QLabel#workflowEyebrow { color:%6; font-size:14px; letter-spacing:1px; } "
            "QLabel#workflowSaveState { color:%6; border:1px solid %4; border-radius:3px; padding:2px 6px; font-size:14px; } "
            "QFrame[divider=\"true\"] { background:%4; border:0; } "
            "QLabel#workflowInspectorHeading, QLabel#workflowLibraryTitle, QLabel#workflowInspectorLabel { font-size:16px; font-weight:600; } "
            "QLabel#workflowInspectorMeta { font-size:14px; color:%6; } QLabel#workflowInspectorIcon { background:%1; border-radius:5px; padding:7px; } "
            "QLabel#workflowInspectorFooter { font-size:14px; color:%5; border-top:1px solid %4; } "
            "QLabel#workflowResultStatus { color:%5; font-size:16px; font-weight:600; } "
            "QLineEdit,QPlainTextEdit,QComboBox { background:%1; color:%2; border:1px solid %4; border-radius:4px; padding:4px 6px; min-height:22px; font-size:16px; placeholder-text-color:%6; selection-background-color:%5; selection-color:%3; } "
            "QLineEdit#workflowTitle { border:0; background:transparent; padding:0; font-size:21px; font-weight:600; } "
            "QLineEdit#workflowTitle:focus { border-bottom:1px solid %5; } "
            "QTreeWidget#workflowPalette { background:transparent; border:0; padding:0; outline:0; } "
            "QTableView { background:%1; color:%2; border:0; gridline-color:%4; selection-background-color:%4; selection-color:%2; } "
            "QPushButton { background:transparent; border:1px solid %4; border-radius:5px; padding:4px 10px; color:%2; font-size:16px; } "
            "QPushButton:hover { background:%4; border-color:%5; } QPushButton:focus, QLineEdit:focus, QComboBox:focus { border:1px solid %5; } "
            "QPushButton[quiet=\"true\"] { border:0; padding:4px 6px; color:%6; } QPushButton[quiet=\"true\"]:hover { color:%2; background:%4; } "
            "QPushButton[section=\"true\"] { border:0; border-top:1px solid %4; border-radius:0; text-align:left; padding:10px 0; color:%6; } "
            "QPushButton[viewTab=\"true\"] { border:0; border-bottom:2px solid transparent; border-radius:0; color:%6; padding:7px 4px; } "
            "QPushButton[viewTab=\"true\"]:checked { color:%2; border-bottom-color:%5; } "
            "QPushButton[primary=\"true\"] { background:%5; color:%3; font-weight:600; padding:6px 22px; } QPushButton:disabled { color:%6; } "
            "QLabel#workflowIssue { background:%3; color:%7; border-bottom:1px solid %4; } "
            "QHeaderView::section { background:%1; color:%6; border:0; border-bottom:1px solid %4; padding:7px; font-size:14px; } "
            "QTabWidget::pane { border:0; } QTabBar::tab { background:transparent; color:%6; padding:9px 12px; border-bottom:2px solid transparent; font-size:14px; } "
            "QTabBar::tab:selected { color:%2; border-bottom:2px solid %5; } QSplitter::handle { background:%4; } QSplitter::handle:hover { background:%5; } "
            "QScrollArea { border:0; } QCheckBox { spacing:7px; } "
            "QScrollBar:vertical { background:transparent; width:6px; margin:0; } QScrollBar::handle:vertical { background:%4; min-height:32px; border-radius:3px; } "
            "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical { height:0; } QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical { background:transparent; }")
            .arg(panel, ink, side, line, accent, muted, dark ? "#ed9693" : "#b04040"));
    QPalette palette = this->palette();
    palette.setColor(QPalette::Window, QColor(side));
    palette.setColor(QPalette::Base, QColor(panel));
    palette.setColor(QPalette::Text, QColor(ink));
    palette.setColor(QPalette::WindowText, QColor(ink));
    setPalette(design::textPalette(palette, dark));
    for (const auto& entry : QList<QPair<QString,design::Icon>>{
        {"workflowTemplates",design::Icon::Folder},{"workflowSave",design::Icon::Save},
        {"workflowRun",design::Icon::Send},{"workflowPaletteToggle",design::Icon::Plus},
        {"workflowInspectorToggle",design::Icon::Menu},{"workflowUndo",design::Icon::Reset},
        {"workflowRedo",design::Icon::Right},{"workflowDuplicate",design::Icon::Copy},
        {"workflowDelete",design::Icon::Trash},{"workflowValidate",design::Icon::Check},
        {"workflowImport",design::Icon::Import},{"workflowExport",design::Icon::Export},
        {"workflowPause",design::Icon::Pause},{"workflowStop",design::Icon::Stop}}) {
        if (auto* button=findChild<QPushButton*>(entry.first)) {
            button->setIcon(design::icon(entry.second,QColor(entry.first=="workflowRun"?side:muted),14));
            button->setIconSize({14,14});
        }
    }
    d->palette->viewport()->update();
    d->updateDocumentUi();
    d->form();
    d->runtime();
    d->view->resetCachedContent();
    for (auto id : d->model->allNodeIds()) {
        if (auto *object = d->scene->nodeGraphicsObject(id)) {
            object->setCacheMode(QGraphicsItem::NoCache);
            object->update();
        }
    }
    d->scene->update();
}
void WorkflowPage::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    bool compact = width() < 1120;
    if (compact != d->compact) {
        d->compact = compact;
        if (compact)
            d->palettePanel->hide();
        else
            d->palettePanel->show();
    }
    if (width() < 850) {
        d->palettePanel->hide();
        d->inspector->hide();
    }
    if (d->logTabs->isHidden())
        d->vertical->setSizes({std::max(200, d->vertical->height()-44),44});
    if (height() < 800 && d->logTabs->isVisible() && d->logPanel->height()>164)
        d->vertical->setSizes({std::max(200, d->editor->height()+d->logPanel->height()-164), 164});
}

bool WorkflowPage::setDocument(const WorkflowDocument &doc, QString *error) {
    if (d->runner->active()) {
        if (error)
            *error = QStringLiteral("运行中不能替换流程；请先停止。");
        return false;
    }
    if (doc.nodes.size() > 256 || doc.edges.size() > 512) {
        if (error)
            *error = QStringLiteral("流程超过 256 节点或 512 连线上限。");
        return false;
    }
    for (const auto &issue : doc.validate())
        if (issue.field == "type" || issue.field == "parameters" || issue.field == "limits" ||
            issue.field == "id" || issue.field == "title") {
            if (error)
                *error = issue.message;
            return false;
        }
    d->scene->undoStack().clear();
    d->model->forgetHistory();
    d->historyEstimate = 0;
    d->historyCount = 0;
    d->selected = {};
    d->model->setDocument(doc);
    d->form();
    d->runtime();
    QString focus;
    for (const auto &n : doc.nodes)
        if (n.type == "http" || n.type == "sendWait") {
            focus = n.id;
            break;
        }
    if (focus.isEmpty() && !doc.nodes.isEmpty())
        focus = doc.nodes.first().id;
    QTimer::singleShot(0, this, [this, focus] {
        d->view->setupScale(.85);
        if (!focus.isEmpty())
            selectNode(focus);
    });
    return true;
}
bool WorkflowPage::loadFile(const QString &filename, QString *error) {
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法打开工作流：%1").arg(file.errorString());
        return false;
    }
    if (file.size() > 16 * 1024 * 1024) {
        if (error)
            *error = QStringLiteral("文件超过 16 MiB 导入上限。");
        return false;
    }
    QJsonParseError parse;
    auto json = QJsonDocument::fromJson(file.readAll(), &parse);
    if (!json.isObject()) {
        if (error)
            *error = QStringLiteral("工作流 JSON 无效：%1").arg(parse.errorString());
        return false;
    }
    if (json.object().value("prototypeOnly").toBool()) {
        if (error)
            *error = QStringLiteral(
                "这是设计原型文件，不能执行。请选择正式模板重新创建流程并保存为 .pbflow.json。");
        return false;
    }
    WorkflowDocument doc;
    if (!WorkflowDocument::fromJson(json.object(), &doc, error))
        return false;
    if (!setDocument(doc, error))
        return false;
    d->path = filename;
    d->saved = doc.toJson();
    d->message(QStringLiteral("已导入流程，未执行通信；请检查参数和资源。"));
    d->updateDocumentUi();
    return true;
}
bool WorkflowPage::saveFile(const QString &filename, QString *error) {
    if (d->runner->active()) {
        if (error)
            *error = QStringLiteral("运行中不能保存或导出可编辑流程。");
        return false;
    }
    if (auto *focused = QApplication::focusWidget())
        focused->clearFocus();
    QString path = filename;
    if (!path.endsWith(".pbflow.json", Qt::CaseInsensitive))
        path += ".pbflow.json";
    auto object = document().toJson();
    auto safe = redact(object, {}, d->secrets()).toObject();
    auto bytes = QJsonDocument(safe).toJson(QJsonDocument::Indented);
    if (bytes.size() > 16 * 1024 * 1024) {
        if (error)
            *error = QStringLiteral("流程超过 16 MiB 保存上限。");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error)
            *error = QStringLiteral("无法保存工作流：%1").arg(file.errorString());
        return false;
    }
    d->path = path;
    d->saved = object;
    d->updateDocumentUi();
    d->message(
        object == safe
            ? QStringLiteral("已保存工作流。")
            : QStringLiteral("已保存工作流，字面凭据已遮蔽。重新打开后请使用 ${变量} 或重新输入凭据。"));
    return true;
}
bool WorkflowPage::applyTemplate(const QString &key) {
    if (d->runner->active())
        return false;
    auto before = document(), after = WorkflowDocument::templateDocument(key);
    if (key == "blank") {
        after.nodes.clear();
        after.edges.clear();
    }
    if (key == "login") {
        after.description = QStringLiteral("获取凭据，建立消息订阅，验证设备响应。");
        for (int i = 0; i < after.nodes.size(); ++i) {
            auto &n = after.nodes[i];
            n.position = i < 4 ? QPointF(i ? 210 + (i - 1) * 280 : 36, i ? 96 : 124)
                               : QPointF(i == 7 ? 36 : 210 + (6 - i) * 280, i == 7 ? 364 : 336);
        }
    }
    if (key == "udp")
        after.description = QStringLiteral("使用明确连接资源，发送指令并校验响应。");
    d->mutate(
        QStringLiteral("应用模板"),
        [this, before] {
            d->model->setDocument(before);
            d->selected = {};
            d->form();
        },
        [this, after] {
            d->model->setDocument(after);
            d->selected = {};
            d->form();
        });
    d->path = {};
    d->saved = {};
    d->message(QStringLiteral("模板已应用，未执行通信；可撤销恢复。"));
    QString focus;
    for (const auto &n : after.nodes)
        if (n.type == "http" || n.type == "sendWait") {
            focus = n.id;
            break;
        }
    d->view->setupScale(.85);
    if (!focus.isEmpty())
        selectNode(focus);
    return true;
}
void WorkflowPage::showTemplates() {
    if (d->runner->active())
        return;
    QDialog dialog(this);
    dialog.setObjectName("workflowTemplatesDialog");
    dialog.setWindowTitle(QStringLiteral("选择流程模板"));
    dialog.setMinimumWidth(520);
    auto *layout = new QVBoxLayout(&dialog);
    auto *title = new QLabel(QStringLiteral("以常见联调任务为起点，所有步骤都可以继续修改。"));
    title->setWordWrap(true);
    layout->addWidget(title);
    for (const auto &item : QList<QPair<QString, QString>>{
             {"login", QStringLiteral("设备登录与订阅\nHTTP 获取凭据 → WebSocket 订阅 → 断言")},
             {"udp", QStringLiteral("UDP 指令响应\n使用方案 → 发送并等待 → 校验响应")},
             {"blank", QStringLiteral("空白流程\n从零组合设备联调步骤")}}) {
        auto *b = button(item.second, "workflowTemplate_" + item.first, &dialog);
        b->setMinimumHeight(66);
        layout->addWidget(b);
        connect(b, &QPushButton::clicked, &dialog, [this, &dialog, key = item.first] {
            applyTemplate(key);
            dialog.accept();
        });
    }
    layout->addWidget(new QLabel(QStringLiteral("替换画布可撤销；模板不会自动连接或运行。")));
    auto *close = new QDialogButtonBox(QDialogButtonBox::Cancel);
    connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(close);
    dialog.exec();
}
bool WorkflowPage::confirmLeave() {
    if (!isDirty())
        return true;
    const bool active = d->runner->active();
    QMessageBox prompt(
        QMessageBox::Question, QStringLiteral("流程有未保存的修改"),
        active ? QStringLiteral("当前流程仍在运行。保存并离开会停止当前操作及后续步骤；取消将继续保留运行。")
               : QStringLiteral("离开前保存工作流？"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
    prompt.setDefaultButton(QMessageBox::Save);
    if (active)
        prompt.button(QMessageBox::Save)->setText(QStringLiteral("停止并保存"));
    auto answer = prompt.exec();
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Discard)
        return true;
    auto path = d->path;
    if (path.isEmpty())
        path = QFileDialog::getSaveFileName(this, QStringLiteral("保存工作流"), "workflow.pbflow.json",
                                            QStringLiteral("工作流 (*.pbflow.json)"));
    if (path.isEmpty())
        return false;
    if (active)
        d->runner->stop();
    QString why;
    if (!saveFile(path, &why)) {
        d->message(why);
        return false;
    }
    return true;
}
void WorkflowPage::closeEvent(QCloseEvent *event) {
    if (!confirmLeave()) {
        event->ignore();
        return;
    }
    QWidget::closeEvent(event);
}

} // namespace portbridge
