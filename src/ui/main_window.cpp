#include "ui/main_window.hpp"
#include <QCloseEvent>
#include "ui/protocol_debug_page.hpp"
#include "ui/http_project_store.hpp"
#include "ui/workflow_page.hpp"
#include "ui/record_model.hpp"
#include "ui/byte_text_preview.hpp"
#include "ui/background_job.hpp"
#include "ui/background_worker.hpp"
#include "ui/connection_dialog.hpp"
#include "ui/design_widgets.hpp"
#include <QStyleFactory>
#include <QFrame>
#include "portbridge/session_controller.hpp"
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QElapsedTimer>
#include <QPersistentModelIndex>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QNetworkInterface>
#include <QPainter>
#include <QPainterPath>
#include <type_traits>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QProgressBar>
#include <atomic>
#include <thread>
#include <QSaveFile>
#include <QScrollBar>
#include <QScrollArea>
#include <QSerialPortInfo>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStorageInfo>
#include <QTableView>
#include <QTabWidget>
#include <QTabBar>
#include <QTextDocumentFragment>
#include <QTextBlock>
#include <QTextBlockUserData>
#include <QJsonParseError>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QWidgetAction>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFontDatabase>
#include <algorithm>
#include <cmath>
#include <deque>

namespace portbridge {
namespace {
QString fromStd(const std::string& s) { return QString::fromUtf8(s.data(), qsizetype(s.size())); }
std::string toStd(const QString& s) { const auto b = s.toUtf8(); return std::string(b.constData(), std::size_t(b.size())); }
QString bytesLabel(double n) {
    if (n >= 1024.0 * 1024 * 1024) return QStringLiteral("%1 GiB").arg(n / (1024.0 * 1024 * 1024), 0, 'f', 2);
    if (n >= 1024.0 * 1024) return QStringLiteral("%1 MiB").arg(n / (1024.0 * 1024), 0, 'f', 2);
    if (n >= 1024) return QStringLiteral("%1 KiB").arg(n / 1024, 0, 'f', 1);
    return QStringLiteral("%1 B").arg(n, 0, 'f', 0);
}
QString transportText(TransportKind k) {
    switch (k) { case TransportKind::Serial: return QStringLiteral("串口"); case TransportKind::TcpClient: return QStringLiteral("TCP 客户端"); case TransportKind::TcpServer: return QStringLiteral("TCP 服务端"); case TransportKind::Udp: return QStringLiteral("UDP"); } return {};
}
template<class T> T* named(T* w, const char* name) { w->setObjectName(QString::fromLatin1(name)); return w; }
QPushButton* button(const QString& text, const char* name) { auto* b = named(new QPushButton(text), name); b->setAccessibleName(text); b->setCursor(Qt::PointingHandCursor); return b; }
QLabel* label(const QString& text, const char* name = nullptr) { auto* l = new QLabel(text); l->setTextFormat(Qt::PlainText); if (name) named(l,name); return l; }
QComboBox* combo(const QStringList& items, const char* name) { auto* c = named(new QComboBox, name); c->setMinimumContentsLength(1); c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); c->addItems(items); return c; }
QString eolText(const QString& value) { return value == QStringLiteral("none") ? QStringLiteral("无") : value; }
QComboBox* eolCombo(const char* name) { auto* c = combo({QStringLiteral("无"),"CR","LF","CRLF"},name); for(int i=0;i<c->count();++i)c->setItemData(i,i?c->itemText(i):QStringLiteral("none")); return c; }
void selectEol(QComboBox* c,const QString& value) { c->setCurrentIndex(std::max(0,c->findData(value))); }
void chineseButtons(QDialogButtonBox* buttons,const QString& save) { buttons->button(QDialogButtonBox::Save)->setText(save); buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消")); }
QSpinBox* spin(int min, int max, int value, const char* name) { auto* s = named(new QSpinBox,name); s->setRange(min,max); s->setValue(value); return s; }
QWidget* panel(QLayout* l) { auto* w = new QWidget; w->setLayout(l); return w; }
QScrollArea* scrollPanel(QWidget* w) { auto* s = new QScrollArea; s->setWidgetResizable(true); s->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); s->setFrameShape(QFrame::NoFrame); s->setWidget(w); return s; }
QFont fixedFont(int points=9) { const auto family=QFontDatabase::families().contains(QStringLiteral("Consolas"))?QStringLiteral("Consolas"):QFontDatabase::systemFont(QFontDatabase::FixedFont).family(); QFont f(family,points); f.setFamilies({family,QStringLiteral("Microsoft YaHei UI")}); f.setStyleHint(QFont::Monospace); return f; }
QHBoxLayout* rowLayout() { auto* l = new QHBoxLayout; l->setContentsMargins(0,0,0,0); l->setSpacing(7); return l; }
void buddy(QFormLayout* f, const QString& text, QWidget* field) { auto* l = label(text); l->setBuddy(field); field->setAccessibleName(text); f->addRow(l,field); }

class Trend final : public QWidget {
public:
    explicit Trend(QWidget* p = nullptr) : QWidget(p) { setObjectName("throughputTrend"); setAccessibleName(QStringLiteral("真实接收吞吐趋势，最近 60 秒")); setMinimumHeight(65); setMaximumHeight(75); clock.start(); }
    void sample(double v) { const auto now=clock.elapsed(); points.emplace_back(now,std::isfinite(v) ? std::max(0.0,v) : 0.0); while (points.size() > 240 || (!points.empty() && now-points.front().first>60000)) points.pop_front(); update(); }
    void clear() { points.clear(); clock.restart(); update(); }
    void theme(bool d) { dark = d; update(); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        const QColor muted(dark ? "#8b9a9f" : "#5e7379"), line(dark ? "#2b3438" : "#d5dfe1"), accent(dark ? "#85dec4" : "#176e58");
        p.setFont(design::font(11));p.setPen(muted);p.drawText(0,18,QStringLiteral("接收吞吐"));
        p.setFont(design::font(16,true,true));p.setPen(accent);p.drawText(0,43,QStringLiteral("%1 MB/s").arg(points.empty()?0:points.back().second/1e6,0,'f',3));
        p.setFont(design::font(9,true));p.setPen(muted);p.drawText(0,61,QStringLiteral("最近 60 秒 · 实测"));
        QRectF r(170,9,std::max(1,width()-180),height()-33);
        p.setPen(QPen(line,1,Qt::DashLine)); for (int n=0;n<3;++n) p.drawLine(QPointF(r.left(),r.top()+r.height()*n/2),QPointF(r.right(),r.top()+r.height()*n/2));
        double peak=1; for (const auto& entry:points) peak=std::max(peak,entry.second);
        p.setPen(muted); p.drawText(QPointF(r.left(),height()-7),QStringLiteral("−60 s")); p.drawText(QPointF(r.right()-38,height()-7),QStringLiteral("现在"));
        QPainterPath path; std::size_t n=0;
        const auto now=clock.elapsed();
        for (const auto& entry:points) { QPointF point(r.right()-r.width()*double(now-entry.first)/60000,r.bottom()-r.height()*entry.second/peak); if (!n) path.moveTo(point); else path.lineTo(point); ++n; }
        if(points.size()>1){auto area=path;area.lineTo(r.right(),r.bottom());area.lineTo(path.pointAtPercent(0).x(),r.bottom());area.closeSubpath();QColor fill=accent;fill.setAlpha(14);p.fillPath(area,fill);}p.setPen(QPen(accent,1.7));p.drawPath(path);
    }
private: std::deque<std::pair<qint64,double>> points; QElapsedTimer clock; bool dark=true;
};
}

struct MainWindow::Impl : QObject {
    MainWindow* q;
    SessionController* c;
    SessionController *parkedController=nullptr,*browseController=nullptr;
    int parkedProfile=-1;
    RecordModel* parkedModel=nullptr;
    QTextDocument* parkedDocument=nullptr;
    std::uint64_t parkedOrdinal=0,parkedOmitted=0;
    QString parkedError;
    int parkedTextFormat=0;
    Command parkedComposer;
    QString parkedRecordDirectory;
    int parkedRotation=0,parkedDuration=0,parkedRecordQueue=0;
    QComboBox* textFormat=nullptr;
    QSpinBox* bytePage=nullptr;
    std::uint64_t inspectedOrdinal=0;
    bool emptyInspectorRendered=false;
    QPersistentModelIndex inspectedIndex;
    int inspectedPage=0;
    bool inspectedDark=false;
    design::Assets assets;
    QPushButton* navigation[4]{};
    QPushButton* railNavigation[4]{};
    WorkflowPage* workflowPage=nullptr;
    QVector<ConnectionConfig> workflowOwnedConfigs;
    QByteArray communicationShellState;
    QPushButton *ordinaryMode=nullptr,*fastMode=nullptr;
    QLabel *eyebrow=nullptr,*profileCount=nullptr,*interfaceInfo=nullptr,*healthTitle=nullptr,*healthDetail=nullptr,*transportTag=nullptr;
    QProgressBar* queueMeter=nullptr;
    QLabel* metricNotes[4]{};
    QLabel* metricTags[4]{};
    QLabel* byteTextStatus=nullptr;
    QLabel* diagnosticValues[7]{};
    QFormLayout *localForm=nullptr,*advancedForm=nullptr;
    QVBoxLayout* connectionLayout=nullptr;
    QGridLayout* remoteLayout=nullptr;
    QLabel *remoteAddressCaption=nullptr,*remotePortCaption=nullptr;
    QWidget* udpSendFields=nullptr;
    QHBoxLayout* udpTargetLayout=nullptr;
    QLabel* udpTargetStatus=nullptr;
    QLabel* localPortCaption=nullptr;
    QWidget *advancedFields=nullptr,*quickChips=nullptr;
    QHBoxLayout* quickChipLayout=nullptr;
    QSettings settings;
    QVector<ConnectionConfig> profiles;
    QVector<Command> commands;
    QVector<Command> history;
    bool loading=false, dark=true, stateDirty=true;
    int profileIndex=0, ticks=0;
    std::uint64_t lastDisplaySequence=0,streamOmittedCharacters=0;
    QSplitter *shell=nullptr,*dataSplitter=nullptr,*workspaceSplitter=nullptr;
    QList<int> composerSplitSizes;
    QToolButton* expandComposer=nullptr;
    QWidget* trendPanel=nullptr;
    QStackedWidget *pages=nullptr,*transportForms=nullptr,*workspaceModes=nullptr;
    QJsonObject initialWorkflowDocument;
    ProtocolDebugPage *httpPage=nullptr,*webSocketPage=nullptr;
    QPushButton* workspaceModeButtons[3]{};
    int workspaceMode=0;
    QListWidget *profileList=nullptr,*commandList=nullptr,*captureList=nullptr;
    design::ProfilePicker* profilePicker=nullptr;
    QMenu* profilesPopup=nullptr;
    QLineEdit* profileSearch=nullptr;
    QLabel* noProfileMatches=nullptr;
    QPushButton* returnActiveProfile=nullptr;
    QWidget *connectionFields=nullptr,*serialFields=nullptr,*remoteFields=nullptr,*localFields=nullptr,*recordFields=nullptr,*sendFields=nullptr,*periodicFields=nullptr;
    QComboBox *localAddress=nullptr,*serialPort=nullptr,*baud=nullptr,*dataBits=nullptr,*parity=nullptr,*stopBits=nullptr,*flow=nullptr,*sendFormat=nullptr,*encoding=nullptr,*eol=nullptr,*filterKind=nullptr,*displayFormat=nullptr,*clientTarget=nullptr,*historyBox=nullptr,*quickBox=nullptr;
    QLineEdit *remoteAddress=nullptr,*filter=nullptr,*recordDirectory=nullptr;
    QSpinBox *localPort=nullptr,*remotePort=nullptr,*timeout=nullptr,*rxBuffer=nullptr,*txBuffer=nullptr,*sendQueue=nullptr,*seqOffset=nullptr,*seqWindow=nullptr,*interval=nullptr,*count=nullptr,*rotation=nullptr,*duration=nullptr,*recordQueue=nullptr;
    QCheckBox *sequence=nullptr,*seqBigEndian=nullptr,*high=nullptr,*paused=nullptr,*autoScroll=nullptr,*periodic=nullptr,*broadcast=nullptr;
    QPushButton *connectButton=nullptr,*sendButton=nullptr,*recordButton=nullptr,*disconnectClient=nullptr,*themeButton=nullptr;
    QPlainTextEdit *sendInput=nullptr,*byteView=nullptr,*byteOffsets=nullptr,*byteAscii=nullptr,*byteUtf8=nullptr,*streamView=nullptr;
    QLabel *byteRange=nullptr,*txRate=nullptr;
    QLabel *title=nullptr,*state=nullptr,*endpoint=nullptr,*connectionError=nullptr,*validation=nullptr,*byteSummary=nullptr,*notice=nullptr,*banner=nullptr,*diagnostics=nullptr,*queueStatus=nullptr,*capacity=nullptr,*storage=nullptr,*footer=nullptr,*rxMetric=nullptr,*ppsMetric=nullptr,*lossMetric=nullptr,*recordMetric=nullptr,*retained=nullptr;
    QTableView* table=nullptr;
    RecordModel* model=nullptr;
    RecordFilter* proxy=nullptr;
    Trend* trend=nullptr;
    QTabWidget* dataTabs=nullptr;
    QTimer* timer=nullptr;
    QElapsedTimer metricsClock;
    QByteArray payload;
    bool payloadValid=false;
    struct ExportJob : detail::BackgroundCompletion {
        std::atomic<bool> cancel{false};
        std::atomic<std::uint64_t> processed{0},total{0};
        QString output,error;
        bool success=false;
    };
    std::shared_ptr<ExportJob> exportJob;
    detail::BackgroundWorker exportWorker;
    QProgressBar* exportProgress=nullptr;
    QLabel* exportStatus=nullptr;
    QPushButton *cancelExport=nullptr,*exportCaptureButton=nullptr,*openCaptureButton=nullptr,*sampleExportButton=nullptr,*cancelSamples=nullptr;
    std::shared_ptr<ExportJob> sampleJob;
    detail::BackgroundWorker sampleWorker;
    struct StorageJob : detail::BackgroundCompletion {
        std::atomic<bool> cancel{false};
        QString requestedPath,result;
    };
    std::shared_ptr<StorageJob> storageJob;
    detail::BackgroundWorker storageWorker;
    QString pendingStoragePath;
    QString storageResultPath;
    QStringList captureSignature;
    std::shared_ptr<ExportJob> deleteJob;
    detail::BackgroundWorker deleteWorker;


    explicit Impl(MainWindow* owner, SessionController* controller) : QObject(owner),q(owner),c(controller) {
        QString error; profiles=loadProfiles(&error); QString startupError=error;
        const auto configDirectory=settings.value("storage/directory",QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).toString();
        if(!QFileInfo::exists(QDir(configDirectory).filePath("profiles.json"))) {
            for(auto& profile:profiles) { profile.localAddress="127.0.0.1"; profile.name=toStd(transportText(profile.kind)+QStringLiteral(" 调试")); }
        }
        commands=loadCommands(&error); if (!error.isEmpty()) startupError += '\n'+error;
        if (profiles.isEmpty()) {
            for (auto kind : {TransportKind::Udp,TransportKind::Serial,TransportKind::TcpClient,TransportKind::TcpServer}) {
                ConnectionConfig cfg; cfg.kind=kind; cfg.name=toStd(transportText(kind)+QStringLiteral(" 调试")); cfg.localAddress="127.0.0.1"; profiles.push_back(cfg);
            }
        }
        if(QApplication::style()->objectName()!=QStringLiteral("fusion")) QApplication::setStyle(QStyleFactory::create("Fusion"));
        build();
        refreshProfiles(); refreshCommands(); enumerate();
        profileIndex=std::clamp(settings.value("ui/profile",0).toInt(),0,int(profiles.size())-1);
        {const QSignalBlocker block(profileList);profileList->setCurrentRow(profileIndex);}
        activateProfile(profileIndex);
        autoScroll->setChecked(settings.value("ui/autoScroll",true).toBool());
        displayFormat->setCurrentIndex(settings.value("ui/displayFormat",0).toInt()==1?1:0);
        high->setChecked(settings.value("ui/highSpeed",false).toBool());
        interval->setValue(settings.value("send/interval",1000).toInt());
        count->setValue(settings.value("send/count",10).toInt());
        periodic->setChecked(false);
        dark=settings.value("ui/dark",true).toBool(); applyTheme();
        q->resize(1440,960); q->setMinimumSize(1100,760);
        if (settings.contains("ui/geometry")) q->restoreGeometry(settings.value("ui/geometry").toByteArray());
        if (settings.contains("ui/size")) q->resize(settings.value("ui/size").toSize());
        if (settings.contains("ui/shell")) shell->restoreState(settings.value("ui/shell").toByteArray());
        if (settings.contains("ui/data")) dataSplitter->restoreState(settings.value("ui/data").toByteArray());
        if(settings.contains("ui/workspaceSplit"))workspaceSplitter->restoreState(settings.value("ui/workspaceSplit").toByteArray());
        else QTimer::singleShot(0,this,[this]{workspaceSplitter->setSizes({220,360});});
        connectController(c);
        timer=named(new QTimer(q),"uiSnapshotTimer"); timer->setInterval(50);
        QObject::connect(timer,&QTimer::timeout,q,[this]{ snapshot(false); }); timer->start();
        q->installEventFilter(this);
        trendPanel->setVisible(q->height()>=850);
        updateState(); snapshot(); if (!startupError.trimmed().isEmpty()) showError(startupError.trimmed());
    }
    bool eventFilter(QObject* watched,QEvent* event) override {
        if(watched==q&&event->type()==QEvent::Resize&&trendPanel)trendPanel->setVisible(q->height()>=850&&!expandComposer->isChecked());
        return QObject::eventFilter(watched,event);
    }
    ~Impl() {
        q->removeEventFilter(this);
        timer->stop();
        if(exportJob)exportJob->cancel.store(true,std::memory_order_relaxed);
        exportWorker.stop(std::chrono::seconds(1));
        if(sampleJob)sampleJob->cancel.store(true,std::memory_order_relaxed);
        if(storageJob)storageJob->cancel.store(true,std::memory_order_relaxed);
        if(deleteJob)deleteJob->cancel.store(true,std::memory_order_relaxed);
        deleteWorker.stop(std::chrono::seconds(1));
        sampleWorker.stop(std::chrono::seconds(1));
        storageWorker.stop(std::chrono::seconds(1));
        if(httpPage)httpPage->session()->cancel();
        if(webSocketPage)webSocketPage->session()->cancel();
        if(workflowPage)workflowPage->runner()->stop();
        if(parkedController)parkedController->stop();
        c->stopPeriodic(); c->stopRecording(); c->stop();
        QString error;
        ConnectionConfig cfg; if (readConfig(&cfg,&error) && profileIndex >= 0 && profileIndex < profiles.size()) profiles[profileIndex]=cfg;
        saveProfiles(profiles,&error); saveCommands(commands,&error);
        settings.setValue("ui/profile",profileIndex); settings.setValue("ui/dark",dark);
        settings.setValue("ui/geometry",q->saveGeometry()); settings.setValue("ui/size",q->size()); settings.setValue("ui/shell",q->findChild<QWidget*>("connectionSidebar")->isHidden()&&!communicationShellState.isEmpty()?communicationShellState:shell->saveState()); settings.setValue("ui/data",dataSplitter->saveState());
        if(expandComposer->isChecked())setComposerExpanded(false);
        settings.setValue("ui/workspaceSplit",workspaceSplitter->saveState());
        settings.setValue("capture/directory",recordDirectory->text()); settings.setValue("capture/rotation",rotation->value()); settings.setValue("capture/duration",duration->value());
        settings.setValue("capture/queueMiB",recordQueue->value());
        settings.setValue("ui/autoScroll",autoScroll->isChecked()); settings.setValue("ui/displayFormat",displayFormat->currentIndex()); settings.setValue("ui/highSpeed",high->isChecked());
        settings.setValue("send/interval",interval->value()); settings.setValue("send/count",count->value());
        settings.sync();
    }
    bool prepareManualProtocol(ProtocolDebugSession* target,QString* error);
    bool prepareWorkflowRun(const WorkflowDocument& document,QString* error);
    void synchronizeWorkflowResource();
    bool workflowOwnsUi() const { return workflowPage&&workflowPage->runner()->active(); }
    bool protectWorkflowProfiles(bool wholeList=false) {
        if(!workflowOwnsUi())return false;
        const auto selected=workflowConnectionConfigToJson(profiles[profileIndex]);
        const bool reserved=workflowPage->runner()->usesSession(activityController())&&profileIndex==(parkedController?parkedProfile:profileIndex);
        const bool planned=std::any_of(workflowOwnedConfigs.begin(),workflowOwnedConfigs.end(),[&](const ConnectionConfig& config){return workflowConnectionConfigToJson(config)==selected;});
        if(!wholeList&&!reserved&&!planned)return false;
        showError(QStringLiteral("工作流运行中，不能修改其活动或预留方案；可继续浏览、编辑其他方案，或停止流程后导入。"));return true;
    }
    SessionController* activityController() const { return parkedController?parkedController:c; }
    void build();
    QWidget* buildSidebar();
    QWidget* buildManualWorkspace();
    QWidget* buildWorkspace();
    QWidget* buildInspector();
    QWidget* buildComposer();
    void setComposerExpanded(bool expanded){
        trendPanel->setVisible(q->height()>=850&&!expanded);
        if(expanded){composerSplitSizes=workspaceSplitter->sizes();workspaceSplitter->setSizes({0,workspaceSplitter->height()});}
        else workspaceSplitter->setSizes(composerSplitSizes.isEmpty()?QList<int>{220,360}:composerSplitSizes);
        expandComposer->setText(expanded?QStringLiteral("收起编辑"):QStringLiteral("展开编辑"));
        expandComposer->setArrowType(expanded?Qt::DownArrow:Qt::UpArrow);
        sendInput->setFocus();
    }
    QWidget* buildCommands();
    QWidget* buildCaptures();
    void enumerate();
    void applyTheme();
    void applyConfig(const ConnectionConfig&);
    bool readConfig(ConnectionConfig*,QString*) const;
    void refreshProfiles();
    void filterProfiles(){
        const auto query=profileSearch->text().trimmed();int visible=0;
        for(int i=0;i<profileList->count();++i){auto* item=profileList->item(i);const bool match=item->toolTip().contains(query,Qt::CaseInsensitive);item->setHidden(!match);if(match)++visible;}
        noProfileMatches->setVisible(!visible);profileList->setVisible(visible>0);profileList->setFixedHeight(std::clamp(visible,1,6)*64);
        profileList->parentWidget()->layout()->activate();
        // QMenu caches widget-action size hints; result-count changes invalidate them.
        if(!profilesPopup->actions().isEmpty())profilesPopup->actions().first()->setData(visible);
        if(profilesPopup->isVisible())profilesPopup->adjustSize();
    }
    void updateProfilePicker(){
        if(profileIndex<0||profileIndex>=profileList->count())return;
        const auto* selected=profileList->item(profileIndex);ConnectionConfig cfg=profiles[profileIndex];QString error;readConfig(&cfg,&error);
        const bool active=selected->data(Qt::UserRole+1).toBool();
        const auto shownLocal=active&&cfg.kind!=TransportKind::TcpClient?c->localEndpoint():Endpoint{cfg.localAddress,cfg.localPort};
        const auto detail=cfg.kind==TransportKind::Serial?QStringLiteral("%1 · %2 baud").arg(cfg.serialPort.empty()?QStringLiteral("端口未选择"):fromStd(cfg.serialPort)).arg(cfg.baudRate):transportText(cfg.kind)+QStringLiteral(" · ")+endpointText(cfg.kind==TransportKind::TcpClient?Endpoint{cfg.remoteAddress,cfg.remotePort}:shownLocal);
        profilePicker->profile(fromStd(cfg.name),detail,int(cfg.kind),active);
        const bool background=parkedController&&(parkedController->connected()||parkedController->connecting());returnActiveProfile->setVisible(background);
        if(background){const auto text=QStringLiteral("●  返回运行方案 · %1").arg(fromStd(profiles[parkedProfile].name));returnActiveProfile->setText(returnActiveProfile->fontMetrics().elidedText(text,Qt::ElideRight,std::max(60,profilePicker->width()-20)));returnActiveProfile->setToolTip(text+'\n'+endpointText(parkedController->localEndpoint()));}
    }
    void selectProfile(int);
    void activateProfile(int,bool preserve=false);
    void connectController(SessionController* controller) {
        for(auto signal:{&SessionController::stateChanged,&SessionController::clientsChanged,&SessionController::periodicChanged,&SessionController::recordingChanged})QObject::connect(controller,signal,this,[this]{stateDirty=true;});
        QObject::connect(controller,&SessionController::errorOccurred,this,[this,controller](const QString& error){if(controller==c)showError(error);else if(controller==parkedController)parkedError=error;});
    }
    void restoreParked();
    QColor textInk(Direction direction) const {return QColor(direction==Direction::Receive?(dark?"#68ED9D":"#116B35"):direction==Direction::Transmit?(dark?"#FFAD5C":"#9A4300"):(dark?"#8b9a9f":"#5e7379"));}
    QColor textFill(Direction direction) const {return QColor(direction==Direction::Receive?(dark?"#142C20":"#E5F5EA"):direction==Direction::Transmit?(dark?"#392817":"#FFF0DD"):(dark?"#171c1f":"#ffffff"));}
    void recolorText();
    void renderTextPreview();
    void appendTextSegments(std::deque<RecordModel::TextSegment> segments);
    void editProfile(bool add);
    void persistProfiles();
    void refreshCommands();
    void editCommand(bool add);
    void loadCommand(const Command&);
    void persistCommands();
    void validateSend();
    void send();
    void updateState();
    void navigate(int index);
    void updateTargetState();
    void snapshot(bool forceMetrics=true);
    void revealByteText(){const auto ordinal=inspectedOrdinal;QTimer::singleShot(0,this,[this,ordinal]{if(ordinal&&ordinal==inspectedOrdinal&&q->findChild<QTabWidget*>("byteFormatTabs")->currentIndex()==2){auto* scroll=q->findChild<QScrollArea*>("byteDetailsScroll");scroll->widget()->layout()->activate();scroll->verticalScrollBar()->setValue(byteUtf8->mapTo(scroll->widget(),QPoint()).y()-8);}});}
    void inspect();
    void toggleRecording();
    void updateStorage();
    void pollStorage();
    void exportSamples();
    void pollSamples();
    void refreshCaptures(bool rescan=false);
    void deleteCapture(const QString& path);
    void pollDelete();
    void beginCaptureExport(const QString& input,const QString& output);
    void pollExport();
    void showError(const QString& e) { banner->setText(e); banner->show(); q->statusBar()->showMessage(e,10000); }
};

void MainWindow::Impl::build() {
    q->setWindowTitle(QStringLiteral("PortBridge · 通信调试工作台")); q->setObjectName("mainWindow");
    auto* central=new QWidget;auto* outer=new QVBoxLayout(central);outer->setContentsMargins(0,0,0,0);outer->setSpacing(0);
    auto* header=named(new QWidget,"appHeader");header->setFixedHeight(60);auto* nav=new QHBoxLayout(header);nav->setContentsMargins(16,0,22,0);nav->setSpacing(0);
    auto* mark=named(new QLabel,"brandMark");mark->setFixedSize(30,32);nav->addWidget(mark);nav->addSpacing(9);
    auto* brand=label(QStringLiteral("PortBridge"),"brandLabel");nav->addWidget(brand);nav->addSpacing(14);
    auto* version=label(QStringLiteral("/  LAB"),"brandVersion");version->setProperty("muted",true);nav->addWidget(version);nav->addSpacing(38);
    const QString names[]={QStringLiteral("调试工作台"),QStringLiteral("采集文件"),QStringLiteral("命令库"),QStringLiteral("工作流")};
    const char* ids[]={"workspaceNavigation","capturesNavigation","commandsNavigation","workflowNavigation"};
    for(int i=0;i<4;++i){auto* b=button(names[i],ids[i]);b->setCheckable(true);b->setProperty("navigation",true);b->setFixedHeight(60);navigation[i]=b;nav->addWidget(b);if(i!=3)nav->addSpacing(24);QObject::connect(b,&QPushButton::clicked,q,[this,i]{navigate(i);});}
    nav->addStretch();auto* scope=label(QStringLiteral("●  一个活动会话 · 本地调试"),"headerScope");scope->setProperty("muted",true);nav->addWidget(scope);nav->addSpacing(16);
    auto* menu=named(new QToolButton,"operationsMenu");menu->setToolTip(QStringLiteral("操作与快捷键"));menu->setAccessibleName(menu->toolTip());menu->setProperty("iconOnly",true);menu->setFixedSize(28,28);menu->setPopupMode(QToolButton::InstantPopup);nav->addWidget(menu);nav->addSpacing(7);
    themeButton=button(QStringLiteral("浅色主题"),"themeButton");themeButton->setFixedSize(28,28);themeButton->setProperty("iconOnly",true);nav->addWidget(themeButton);outer->addWidget(header);
    auto* body=new QWidget;auto* bodyLayout=new QHBoxLayout(body);bodyLayout->setContentsMargins(0,0,0,0);bodyLayout->setSpacing(0);
    auto* rail=named(new QWidget,"activityRail");rail->setFixedWidth(54);auto* rl=new QVBoxLayout(rail);rl->setContentsMargins(9,16,9,14);rl->setSpacing(15);
    for(int i=0;i<4;++i){auto* b=button(names[i],("railNavigation"+QByteArray::number(i)).constData());b->setText({});b->setToolTip(names[i]);b->setCheckable(true);b->setProperty("rail",true);b->setFixedSize(36,36);b->setIconSize(QSize(20,20));railNavigation[i]=b;rl->addWidget(b);QObject::connect(b,&QPushButton::clicked,q,[this,i]{navigate(i);});}
    rl->addStretch();auto* avatar=label(QStringLiteral("PB"),"railAvatar");avatar->setAlignment(Qt::AlignCenter);avatar->setFixedSize(28,28);rl->addWidget(avatar,0,Qt::AlignHCenter);bodyLayout->addWidget(rail);
    shell=named(new QSplitter(Qt::Horizontal),"mainSplitter");shell->setHandleWidth(1);shell->addWidget(buildSidebar());
    pages=named(new QStackedWidget,"mainPages");pages->addWidget(buildManualWorkspace());pages->addWidget(buildCaptures());pages->addWidget(buildCommands());
    workflowPage=new WorkflowPage(q);initialWorkflowDocument=workflowPage->document().toJson();pages->addWidget(workflowPage);
    workflowPage->setSessionProvider([this]{return activityController();});
    workflowPage->setProfilesProvider([this]{return profiles;});
    workflowPage->setRunPreparation([this](const WorkflowDocument& document,QString* error){return prepareWorkflowRun(document,error);});
    QObject::connect(workflowPage->runner(),&WorkflowRunner::stateChanged,q,[this]{updateState();});
    QObject::connect(workflowPage->runner(),&WorkflowRunner::resourceChanged,q,[this]{synchronizeWorkflowResource();updateState();});
    shell->addWidget(pages);shell->setChildrenCollapsible(false);
    shell->setStretchFactor(0,0);shell->setStretchFactor(1,1);shell->setSizes({232,1154});bodyLayout->addWidget(shell,1);outer->addWidget(body,1);q->setCentralWidget(central);
    QObject::connect(themeButton,&QPushButton::clicked,q,[this]{dark=!dark;applyTheme();});navigate(0);
    footer=label({},"statusSummary");q->statusBar()->setFixedHeight(28);q->statusBar()->setSizeGripEnabled(false);q->statusBar()->addWidget(footer,1);
    auto* operations=q->menuBar()->addMenu(QStringLiteral("操作(&O)"));menu->setMenu(operations);q->menuBar()->hide();
    auto action=[this,operations](const QString& text,const QKeySequence& shortcut,auto fn){auto* a=new QAction(text,q);a->setShortcut(shortcut);q->addAction(a);operations->addAction(a);QObject::connect(a,&QAction::triggered,q,fn);};
    action(QStringLiteral("搜索样本 / 聚焦请求URL"),QKeySequence("Ctrl+K"),[this]{navigate(0);if(workspaceMode==1)httpPage->focusUrl();else if(workspaceMode==2)webSocketPage->focusUrl();else{filter->setFocus();filter->selectAll();}});
    action(QStringLiteral("发送当前内容 / 取消HTTP / 停止周期"),QKeySequence("Ctrl+Return"),[this]{send();});
    action(QStringLiteral("开始 / 停止记录"),QKeySequence("Ctrl+Shift+R"),[this]{toggleRecording();});
    action(QStringLiteral("停止周期 / 取消连接或请求"),QKeySequence(Qt::Key_Escape),[this]{if(pages->currentIndex()==0&&workspaceMode>0){auto* session=(workspaceMode==1?httpPage:webSocketPage)->session();if(session->active()&&!session->connected())session->cancel();}else if(c->periodicActive())c->stopPeriodic();else if(c->connecting())c->stop();updateState();});
    auto* copyAction=new QAction(QStringLiteral("复制选中原始 HEX"),table);copyAction->setShortcut(QKeySequence::Copy);copyAction->setShortcutContext(Qt::WidgetShortcut);table->addAction(copyAction);operations->addAction(copyAction);
    QObject::connect(copyAction,&QAction::triggered,q,[this]{if(table->selectionModel()->hasSelection()){const auto* r=model->record(proxy->mapToSource(table->currentIndex()).row());if(r)QApplication::clipboard()->setText(hexBytes(recordBytes(*r)));}});
}
void MainWindow::Impl::navigate(int index) {
    if(!pages) return;
    if(index==1) refreshCaptures(true);
    auto* side=q->findChild<QWidget*>("connectionSidebar");
    const bool hideSide=index==3||(index==0&&workspaceMode!=0);
    if(hideSide&&!side->isHidden()){communicationShellState=shell->saveState();side->hide();}
    else if(!hideSide&&side->isHidden()){side->show();if(!communicationShellState.isEmpty())shell->restoreState(communicationShellState);}
    q->setMinimumSize(hideSide?QSize(1024,768):QSize(1100,760));
    pages->setCurrentIndex(index);
    for(int i=0;i<4;++i){navigation[i]->setChecked(i==index);railNavigation[i]->setChecked(i==index);railNavigation[i]->setIcon(design::icon(i==0?design::Icon::Workspace:i==1?design::Icon::Folder:i==2?design::Icon::Terminal:design::Icon::Workflow,QColor(i==index?(dark?"#85dec4":"#176e58"):(dark?"#8b9a9f":"#5e7379")),20));}
}

bool MainWindow::Impl::prepareManualProtocol(ProtocolDebugSession* target,QString* error) {
    if(error)error->clear();
    auto* raw=activityController();const bool rawActive=target&&(raw->connected()||raw->connecting()||raw->recording()||raw->periodicActive());
    const bool flowActive=workflowOwnsUi();
    auto* http=httpPage?httpPage->session():nullptr;auto* ws=webSocketPage?webSocketPage->session():nullptr;
    const bool httpActive=http&&http!=target&&http->active(),wsActive=ws&&ws!=target&&ws->active();
    if(!rawActive&&!flowActive&&!httpActive&&!wsActive)return true;
    const auto rawEpoch=raw->sessionEpoch();const auto rawConfig=workflowConnectionConfigToJson(raw->config());
    const auto httpEpoch=http?http->epoch():0,wsEpoch=ws?ws->epoch():0;
    const auto httpPhase=http?http->phase():ProtocolDebugSession::Phase::Idle,wsPhase=ws?ws->phase():ProtocolDebugSession::Phase::Idle;
    const auto flowId=workflowPage?workflowPage->runner()->runId():QString();
    QMessageBox confirm(q);confirm.setObjectName("manualProtocolResourceConfirmation");confirm.setWindowTitle(QStringLiteral("切换活动通信"));
    confirm.setText(QStringLiteral("发起新的通信前，需要结束当前活动。浏览和编辑页面不会结束活动。"));
    QStringList activities;if(rawActive)activities<<QStringLiteral("通信方案：%1（连接、周期发送和原始记录将停止）").arg(fromStd(raw->config().name));if(flowActive)activities<<QStringLiteral("正在运行的工作流");if(httpActive)activities<<QStringLiteral("正在等待响应的HTTP请求");if(wsActive)activities<<QStringLiteral("WebSocket连接或握手");confirm.setInformativeText(activities.join('\n'));
    auto* proceed=confirm.addButton(QStringLiteral("结束旧活动并继续"),QMessageBox::AcceptRole);auto* keep=confirm.addButton(QStringLiteral("保留当前活动"),QMessageBox::RejectRole);confirm.setDefaultButton(keep);confirm.exec();
    if(confirm.clickedButton()!=proceed){if(error)*error=QStringLiteral("已保留当前活动，新的通信未开始。");return false;}
    if(raw!=activityController()||raw->sessionEpoch()!=rawEpoch||workflowConnectionConfigToJson(raw->config())!=rawConfig||(http&&http->epoch()!=httpEpoch)||(ws&&ws->epoch()!=wsEpoch)||(http&&http->phase()!=httpPhase)||(ws&&ws->phase()!=wsPhase)||workflowOwnsUi()!=flowActive||(workflowPage&&workflowPage->runner()->runId()!=flowId)){
        if(error)*error=QStringLiteral("确认期间活动状态已变更；保留现状，请重新操作。");
        return false;
    }
    if(target){const auto contextError=target->pendingContextError();if(!contextError.isEmpty()){if(error)*error=contextError;return false;}}
    if(flowActive)workflowPage->runner()->stop();
    if(httpActive)http->cancel();
    if(wsActive)ws->cancel();
    if(rawActive){raw->stopPeriodic();raw->stopRecording();raw->stop();}
    updateState();return true;
}

bool MainWindow::Impl::prepareWorkflowRun(const WorkflowDocument& document,QString* error) {
    if(error)error->clear();
    if(workflowOwnsUi()){if(error)*error=QStringLiteral("已有工作流正在执行。");return false;}
    const auto issues=document.validate();
    if(!issues.isEmpty()){if(error)*error=issues.front().message;return false;}
    bool borrowed=false,hasWs=false,hasHttp=false;std::optional<ConnectionConfig> ownedConfig;
    QVector<ConnectionConfig> ownedSnapshots;
    for(const auto& node:document.nodes){
        hasWs|=node.type==QStringLiteral("ws");hasHttp|=node.type==QStringLiteral("http");
        if(node.type!=QStringLiteral("raw"))continue;
        const auto ownership=node.parameters.value("ownership").toString(QStringLiteral("borrow"));
        if(ownership==QStringLiteral("owned")||ownership==QStringLiteral("流程建立并释放")){
            ConnectionConfig config;QString why;
            if(!workflowConnectionConfigFromJson(node.parameters.value("config").toObject(),&config,&why)){if(error)*error=why;return false;}
            if(!ownedConfig)ownedConfig=config;
            ownedSnapshots.push_back(config);
        }else borrowed=true;
    }
    auto* active=activityController();
    if(borrowed&&(hasWs||hasHttp||ownedConfig)){if(error)*error=QStringLiteral("借用连接的流程不能同时切换到其他持久资源；请使用流程建立的连接并显式关闭后切换。");return false;}
    if(borrowed&&!active->connected()){if(error)*error=QStringLiteral("此流程需要借用已连接的调试会话，请先在工作台建立连接。");return false;}
    if(borrowed)for(const auto& node:document.nodes)if(node.type==QStringLiteral("raw")&&node.parameters.value("config").isObject()){
        ConnectionConfig expected;QString why;
        if(!workflowConnectionConfigFromJson(node.parameters.value("config").toObject(),&expected,&why)||workflowConnectionConfigToJson(expected)!=workflowConnectionConfigToJson(active->config())){
            if(error)*error=QStringLiteral("借用的活动会话与工作流配置快照不一致，原任务已保留。");
            return false;
        }
    }
    if(!prepareManualProtocol(nullptr,error))return false;
    const auto activityEpoch=active->sessionEpoch();
    const auto activitySnapshot=workflowConnectionConfigToJson(active->config());
    const bool occupied=active->connected()||active->connecting();
    const bool replace=occupied&&(hasWs||hasHttp||ownedConfig.has_value());
    const bool stopPeriodic=borrowed&&active->periodicActive();
    if(replace||stopPeriodic){
        QMessageBox confirm(q);confirm.setObjectName("workflowResourceConfirmation");confirm.setWindowTitle(QStringLiteral("工作流资源计划"));
        confirm.setIcon(QMessageBox::Information);
        confirm.setText(replace?QStringLiteral("运行此流程将停止当前连接、周期发送和记录，再建立流程所需的资源。"):
                              QStringLiteral("流程将借用当前连接，需要先停止周期发送。已有连接和记录会保留。"));
        confirm.setInformativeText(QStringLiteral("当前方案：%1\n周期发送：%2 · 原始记录：%3")
            .arg(fromStd(active->config().name),active->periodicActive()?QStringLiteral("运行中"):QStringLiteral("未开启"),active->recording()?QStringLiteral("记录中"):QStringLiteral("未开启")));
        auto* proceed=confirm.addButton(replace?QStringLiteral("停止旧任务并运行"):QStringLiteral("停止周期并运行"),QMessageBox::AcceptRole);
        auto* cancel=confirm.addButton(QStringLiteral("保留当前任务"),QMessageBox::RejectRole);confirm.setDefaultButton(cancel);confirm.exec();
        if(confirm.clickedButton()!=proceed){if(error)*error=QStringLiteral("已保留当前任务，流程未启动。");return false;}
        if(active!=activityController()||active->sessionEpoch()!=activityEpoch||workflowConnectionConfigToJson(active->config())!=activitySnapshot){if(error)*error=QStringLiteral("确认期间活动会话已变更，原任务已保留；请重新运行并确认。");return false;}
        active->stopPeriodic();if(replace){active->stopRecording();active->stop();}
    }
    if(ownedConfig){
        const auto snapshot=workflowConnectionConfigToJson(*ownedConfig);int index=-1;
        for(int i=0;i<profiles.size();++i)if(workflowConnectionConfigToJson(profiles[i])==snapshot){index=i;break;}
        if(index<0){profiles.push_back(*ownedConfig);index=profiles.size()-1;}
        activateProfile(index);
    }
    workflowOwnedConfigs=std::move(ownedSnapshots);
    updateState();return true;
}

void MainWindow::Impl::synchronizeWorkflowResource() {
    if(!workflowOwnsUi()||workflowOwnedConfigs.isEmpty())return;
    auto* active=activityController();
    if(!workflowPage->runner()->usesSession(active)||(!active->connected()&&!active->connecting()))return;
    const auto actual=active->config();const auto snapshot=workflowConnectionConfigToJson(actual);
    const bool owned=std::any_of(workflowOwnedConfigs.begin(),workflowOwnedConfigs.end(),[&](const ConnectionConfig& config){return workflowConnectionConfigToJson(config)==snapshot;});
    if(!owned)return;
    const int activityIndex=parkedController?parkedProfile:profileIndex;
    if(activityIndex>=0&&activityIndex<profiles.size()&&workflowConnectionConfigToJson(profiles[activityIndex])==snapshot)return;
    int index=-1;for(int i=0;i<profiles.size();++i)if(workflowConnectionConfigToJson(profiles[i])==snapshot){index=i;break;}
    if(index<0){profiles.push_back(actual);index=profiles.size()-1;}
    active->clearDisplay();
    // Owned resource transitions occur on the captured controller. Keep the
    // parked model with that controller, but retire the previous resource's text.
    if(parkedController){
        parkedProfile=index;parkedModel->clear();parkedDocument->clear();parkedOrdinal=0;parkedOmitted=0;parkedError.clear();
        refreshProfiles();
    }else{
        profileIndex=index;model->clear();streamView->clear();lastDisplaySequence=0;streamOmittedCharacters=0;trend->clear();
        refreshProfiles();applyConfig(actual);settings.setValue("ui/profile",index);
    }
}

QWidget* MainWindow::Impl::buildSidebar() {
    auto* side=new QWidget;side->setObjectName("connectionSidebar");side->setMinimumWidth(210);side->setMaximumWidth(390);
    auto* l=new QVBoxLayout(side);l->setContentsMargins(14,15,14,14);l->setSpacing(10);
    auto* heading=rowLayout();auto* section=label(QStringLiteral("当前方案"));section->setProperty("sectionHeading",true);heading->addWidget(section);profileCount=label({},"profileCount");profileCount->setProperty("muted",true);heading->addWidget(profileCount);heading->addStretch();
    auto* add=button(QStringLiteral("新建连接方案"),"addProfile");add->setProperty("iconOnly",true);add->setFixedSize(24,24);heading->addWidget(add);l->addLayout(heading);
    profilePicker=named(new design::ProfilePicker(side),"profilePicker");l->addWidget(profilePicker);
    profilesPopup=named(new QMenu(side),"profilesPopup");profilePicker->setMenu(profilesPopup);
    auto* choices=new QWidget;choices->setMinimumWidth(340);auto* choiceLayout=new QVBoxLayout(choices);choiceLayout->setContentsMargins(12,10,12,10);choiceLayout->setSpacing(8);
    auto* choiceHeading=label(QStringLiteral("切换连接方案"));choiceHeading->setProperty("sectionHeading",true);choiceLayout->addWidget(choiceHeading);
    profileSearch=named(new QLineEdit,"profileSearch");profileSearch->setPlaceholderText(QStringLiteral("搜索名称、协议或地址"));profileSearch->setClearButtonEnabled(true);profileSearch->setAccessibleName(QStringLiteral("搜索保存的连接方案"));choiceLayout->addWidget(profileSearch);
    profileList=named(new design::ProfileList,"profileList");profileList->setAccessibleName(QStringLiteral("保存的连接方案"));profileList->setItemDelegate(new design::ProfileDelegate(profileList));profileList->setMouseTracking(true);profileList->setSpacing(0);choiceLayout->addWidget(profileList);
    noProfileMatches=label(QStringLiteral("没有匹配方案，试试其他名称或地址。"),"noProfileMatches");noProfileMatches->setWordWrap(true);noProfileMatches->hide();choiceLayout->addWidget(noProfileMatches);
    auto* choiceNote=label(QStringLiteral("选择仅切换查看；启动另一方案才替换当前通信。"));choiceNote->setProperty("small",true);choiceNote->setWordWrap(true);choiceLayout->addWidget(choiceNote);
    auto* chooseAction=new QWidgetAction(profilesPopup);chooseAction->setDefaultWidget(choices);profilesPopup->addAction(chooseAction);
    returnActiveProfile=button({},"returnActiveProfile");returnActiveProfile->setProperty("textAction",true);returnActiveProfile->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);returnActiveProfile->hide();l->addWidget(returnActiveProfile);
    QObject::connect(returnActiveProfile,&QPushButton::clicked,q,[this]{if(parkedController){profilesPopup->close();selectProfile(parkedProfile);}});
    QObject::connect(profileSearch,&QLineEdit::textChanged,q,[this]{filterProfiles();});
    QObject::connect(profileSearch,&QLineEdit::returnPressed,q,[this]{if(auto* selected=profileList->currentItem();selected&&!selected->isHidden()){profilesPopup->close();return;}for(int i=0;i<profileList->count();++i)if(!profileList->item(i)->isHidden()){profileList->setCurrentRow(i);profilesPopup->close();return;}});
    QObject::connect(profilesPopup,&QMenu::aboutToShow,q,[this]{profileSearch->clear();filterProfiles();profileList->scrollToItem(profileList->currentItem());QTimer::singleShot(0,profileSearch,[this]{profileSearch->setFocus();});});
    QObject::connect(profileList,&QAbstractItemView::clicked,profilesPopup,&QMenu::close);QObject::connect(profileList,&QAbstractItemView::activated,profilesPopup,&QMenu::close);
    auto* edit=button(QStringLiteral("编辑"),"editProfile");auto* remove=button(QStringLiteral("删除"),"removeProfile");auto* save=button(QStringLiteral("保存"),"saveProfiles");auto* imp=button(QStringLiteral("导入"),"importProfiles");auto* exp=button(QStringLiteral("导出"),"exportProfiles");
    auto* more=named(new QToolButton,"profileMoreMenu");more->setText(QStringLiteral("⋯"));more->setAccessibleName(QStringLiteral("连接方案更多操作"));more->setToolTip(more->accessibleName());more->setProperty("iconOnly",true);more->setFixedSize(24,24);more->setPopupMode(QToolButton::InstantPopup);heading->addWidget(more);
    auto* profileMenu=new QMenu(side);more->setMenu(profileMenu);
    for(auto* b:{edit,remove,save,imp,exp}){b->setParent(side);b->hide();auto* a=profileMenu->addAction(b->text());QObject::connect(a,&QAction::triggered,b,&QPushButton::click);}
    profileList->setContextMenuPolicy(Qt::CustomContextMenu);QObject::connect(profileList,&QWidget::customContextMenuRequested,q,[profileMenu,this](const QPoint& point){profileMenu->exec(profileList->mapToGlobal(point));});
    auto* divider=named(new QFrame,"sidebarDivider");divider->setFixedHeight(1);l->addWidget(divider);
    auto* configHeader=rowLayout();auto* configHeading=label(QStringLiteral("连接配置"),"connectionConfigHeading");configHeading->setProperty("sectionHeading",true);configHeader->addWidget(configHeading,1);transportTag=label({},"connectionTransportTag");transportTag->setProperty("small",true);transportTag->setFont(design::font(9,true));configHeader->addWidget(transportTag);l->addLayout(configHeader);
    connectionFields=named(new QWidget,"connectionFields"); auto* fields=new QVBoxLayout(connectionFields);connectionLayout=fields;fields->setContentsMargins(0,0,0,0);fields->setSpacing(10);
    serialFields=new QWidget; auto* sf=new QFormLayout(serialFields);sf->setContentsMargins(0,0,0,0);
    serialPort=combo({},"serialPort");serialPort->setEditable(true);serialPort->lineEdit()->setPlaceholderText(QStringLiteral("无设备；可输入端口"));serialPort->setToolTip(QStringLiteral("真实串口枚举；无设备时可手动输入端口名称。"));
    auto* serialRow=rowLayout();serialRow->addWidget(serialPort,1);auto* refresh=button(QStringLiteral("刷新"),"refreshPorts");serialRow->addWidget(refresh);buddy(sf,QStringLiteral("串口设备"),panel(serialRow));
    baud=combo({"9600","19200","38400","57600","115200","230400","460800","921600"},"baudRate");baud->setEditable(true);baud->setCurrentText("115200");buddy(sf,QStringLiteral("波特率"),baud);
    dataBits=combo({"5","6","7","8"},"dataBits");dataBits->setCurrentText("8");
    parity=combo({"None","Even","Odd","Space","Mark"},"parity");
    stopBits=combo({"1","1.5","2"},"stopBits");flow=combo({"None","Hardware","Software"},"flowControl");
    auto pair=[&](const QString& a,QWidget* first,const QString& b,QWidget* second){auto* r=rowLayout();for(const auto& entry:{qMakePair(a,first),qMakePair(b,second)}){auto* column=new QVBoxLayout;column->setSpacing(5);auto* caption=label(entry.first);caption->setBuddy(entry.second);caption->setProperty("fieldCaption",true);entry.second->setAccessibleName(entry.first);column->addWidget(caption);column->addWidget(entry.second);r->addLayout(column,1);}sf->addRow(panel(r));};
    pair(QStringLiteral("数据位"),dataBits,QStringLiteral("校验"),parity);pair(QStringLiteral("停止位"),stopBits,QStringLiteral("流控"),flow);fields->addWidget(serialFields);
    localFields=new QWidget;auto* lf=new QFormLayout(localFields);localForm=lf;lf->setContentsMargins(0,0,0,0);
    localAddress=combo({},"localAddress");localAddress->setEditable(true);buddy(lf,QStringLiteral("本地 IPv4"),localAddress);
    localPort=spin(0,65535,9000,"localPort");localPort->setSpecialValueText(QStringLiteral("系统分配"));localPortCaption=label(QStringLiteral("本地端口"));localPortCaption->setBuddy(localPort);localPort->setAccessibleName(localPortCaption->text());lf->addRow(localPortCaption,localPort);fields->addWidget(localFields);
    remoteFields=new QWidget;remoteLayout=new QGridLayout(remoteFields);remoteLayout->setContentsMargins(0,0,0,0);remoteLayout->setHorizontalSpacing(7);remoteLayout->setVerticalSpacing(5);
    remoteAddress=named(new QLineEdit("127.0.0.1"),"remoteAddress");remoteAddressCaption=label(QStringLiteral("目标 IP / 域名"));remoteAddressCaption->setBuddy(remoteAddress);remoteAddress->setAccessibleName(remoteAddressCaption->text());
    remotePort=spin(0,65535,9001,"remotePort");remotePortCaption=label(QStringLiteral("目标端口"));remotePortCaption->setBuddy(remotePort);remotePort->setAccessibleName(remotePortCaption->text());fields->addWidget(remoteFields);
    connectionError=label({},"connectionError");connectionError->setWordWrap(true);connectionError->setProperty("error",true);fields->addWidget(connectionError);
    connectButton=button(QStringLiteral("绑定端口"),"connectButton");connectButton->setProperty("primary",true);fields->addWidget(connectButton);
    auto* advanced=named(new QToolButton,"advancedSettings");advanced->setText(QStringLiteral("高级参数"));advanced->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);advanced->setCheckable(true);fields->addWidget(advanced);
    auto* advancedWidget=named(new QWidget,"advancedFields");advancedFields=advancedWidget;auto* af=new QFormLayout(advancedWidget);advancedForm=af;af->setContentsMargins(0,0,0,0);
    timeout=spin(1,600000,5000,"connectTimeout");timeout->setSuffix(" ms");buddy(af,QStringLiteral("连接超时"),timeout);
    // Keep historical objectNames for integrations; these editors expose exact
    // bytes, including non-MiB-aligned imported values across the schema range.
    rxBuffer=spin(1,1073741824,64*1024*1024,"receiveBufferMiB");rxBuffer->setSuffix(" B");buddy(af,QStringLiteral("请求接收缓冲（字节）"),rxBuffer);
    txBuffer=spin(1,1073741824,8*1024*1024,"sendBufferMiB");txBuffer->setSuffix(" B");buddy(af,QStringLiteral("请求发送缓冲（字节）"),txBuffer);
    sendQueue=spin(1,1073741824,8*1024*1024,"sendQueueMiB");sendQueue->setSuffix(" B");buddy(af,QStringLiteral("待发送上限（字节）"),sendQueue);
    sequence=named(new QCheckBox(QStringLiteral("测试序号分析（默认关闭）")),"sequenceEnabled");af->addRow(sequence);
    seqOffset=spin(0,67108856,0,"sequenceOffset");buddy(af,QStringLiteral("uint64 字节偏移"),seqOffset);
    seqBigEndian=named(new QCheckBox(QStringLiteral("大端序")),"sequenceBigEndian");seqBigEndian->setChecked(true);af->addRow(seqBigEndian);
    seqWindow=spin(1,1048576,1024,"sequenceWindow");buddy(af,QStringLiteral("有限观察窗口"),seqWindow);
    auto* seqNote=label(QStringLiteral("仅分析 UDP 数据报内指定位置的 uint64 序号；内部记录 # 不是远端序号。"),"sequenceHelp");seqNote->setWordWrap(true);af->addRow(seqNote);
    advancedWidget->hide();fields->addWidget(advancedWidget);fields->addStretch(1);
    for(auto* form:{sf,lf,af}){form->setRowWrapPolicy(QFormLayout::WrapAllRows);form->setVerticalSpacing(7);}
    QObject::connect(advanced,&QToolButton::toggled,advancedWidget,&QWidget::setVisible);QObject::connect(advanced,&QToolButton::toggled,q,[this,advanced](bool expanded){advanced->setIcon(design::icon(expanded?design::Icon::Down:design::Icon::Right,QColor(dark?"#8b9a9f":"#5e7379"),12));});
    l->addWidget(scrollPanel(connectionFields),1);
    auto* interfaceRow=rowLayout();auto* interfaceIcon=named(new QLabel,"interfaceIcon");interfaceIcon->setFixedSize(18,18);interfaceRow->addWidget(interfaceIcon);interfaceInfo=label({},"interfaceInfo");interfaceInfo->setProperty("muted",true);interfaceRow->addWidget(interfaceInfo,1);l->addLayout(interfaceRow);
    auto* note=label(QStringLiteral("一个活动通信会话\n浏览方案保持当前通信"));note->setWordWrap(true);note->setProperty("muted",true);note->setProperty("small",true);note->setToolTip(QStringLiteral("浏览方案不停止通信；启动另一方案会替换当前会话。UDP绑定不表示对端在线。"));l->addWidget(note);
    QObject::connect(profileList,&QListWidget::currentRowChanged,q,[this](int i){if(!loading)selectProfile(i);});
    QObject::connect(add,&QPushButton::clicked,q,[this]{editProfile(true);});QObject::connect(edit,&QPushButton::clicked,q,[this]{editProfile(false);});
    QObject::connect(remove,&QPushButton::clicked,q,[this]{if(protectWorkflowProfiles())return;if(profiles.size()<=1){showError(QStringLiteral("至少保留一个连接方案。"));return;}const int removed=profileIndex;profiles.removeAt(removed);if(parkedController&&removed<parkedProfile)--parkedProfile;activateProfile(std::min(removed,int(profiles.size())-1),parkedController!=nullptr);persistProfiles();});
    QObject::connect(save,&QPushButton::clicked,q,[this]{ConnectionConfig cfg;QString error;if(!readConfig(&cfg,&error)){connectionError->setText(error);return;}profiles[profileIndex]=cfg;refreshProfiles();persistProfiles();});
    QObject::connect(imp,&QPushButton::clicked,q,[this]{if(protectWorkflowProfiles(true))return;const auto path=QFileDialog::getOpenFileName(q,QStringLiteral("导入版本化连接方案"),{},"JSON (*.json)");if(path.isEmpty())return;QVector<ConnectionConfig> loaded;QString error;if(!importProfiles(path,&loaded,&error)||loaded.isEmpty()){showError(error.isEmpty()?QStringLiteral("导入文件没有连接方案。"):error);return;}profiles=loaded;activateProfile(0);persistProfiles();});
    QObject::connect(exp,&QPushButton::clicked,q,[this]{const auto path=QFileDialog::getSaveFileName(q,QStringLiteral("导出版本化连接方案"),"profiles.json","JSON (*.json)");if(path.isEmpty())return;ConnectionConfig cfg;QString error;if(!readConfig(&cfg,&error)){showError(error);return;}auto saved=profiles;saved[profileIndex]=cfg;if(!exportProfiles(path,saved,&error))showError(error);});
    QObject::connect(refresh,&QPushButton::clicked,q,[this]{enumerate();});
    QObject::connect(connectButton,&QPushButton::clicked,q,[this]{if(c->connected()||c->connecting()){if(workflowPage->runner()->active())workflowPage->runner()->stop();c->stopPeriodic();c->stopRecording();c->stop();}else{ConnectionConfig cfg;QString error;if(!readConfig(&cfg,&error)){connectionError->setText(error);return;}if(!prepareManualProtocol(nullptr,&error)){connectionError->setText(error);return;}connectionError->clear();banner->hide();profiles[profileIndex]=cfg;if(parkedController)activateProfile(profileIndex);c->start(cfg);}updateState();});
    return side;
}

QWidget* MainWindow::Impl::buildManualWorkspace() {
    auto* root=named(new QWidget,"manualWorkspacePage");auto* layout=new QVBoxLayout(root);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);
    auto* modes=named(new QWidget,"workspaceProtocolBar");modes->setFixedHeight(36);auto* row=new QHBoxLayout(modes);row->setContentsMargins(16,4,16,4);row->setSpacing(6);
    const QString names[]={QStringLiteral("通信调试"),QStringLiteral("HTTP"),QStringLiteral("WebSocket")};const char* ids[]={"rawWorkspaceMode","httpWorkspaceMode","webSocketWorkspaceMode"};
    for(int i=0;i<3;++i){auto* b=button(names[i],ids[i]);b->setCheckable(true);b->setChecked(i==0);b->setProperty("segment",true);b->setFixedHeight(28);row->addWidget(b);workspaceModeButtons[i]=b;QObject::connect(b,&QPushButton::clicked,q,[this,i]{workspaceMode=i;workspaceModes->setCurrentIndex(i);for(int j=0;j<3;++j)workspaceModeButtons[j]->setChecked(j==i);navigate(0);updateState();});}
    row->addStretch();auto* create=button(QStringLiteral("新建调试项"),"createConnection");create->setProperty("textAction",true);create->setFixedHeight(28);row->addWidget(create);QObject::connect(create,&QPushButton::clicked,q,[this]{editProfile(true);});auto* note=label(QStringLiteral("编辑不通信 · 仅显式发送 / 连接"));note->setProperty("muted",true);row->addWidget(note);layout->addWidget(modes);
    workspaceModes=named(new QStackedWidget,"workspaceProtocolPages");workspaceModes->addWidget(buildWorkspace());httpPage=new ProtocolDebugPage(ProtocolDebugSession::Mode::Http,&settings,q);webSocketPage=new ProtocolDebugPage(ProtocolDebugSession::Mode::WebSocket,&settings,q);workspaceModes->addWidget(httpPage);workspaceModes->addWidget(webSocketPage);layout->addWidget(workspaceModes,1);
    for(auto* page:{httpPage,webSocketPage}){auto* session=page->session();session->setStartGuard([this,session](QString* error){return prepareManualProtocol(session,error);});QObject::connect(session,&ProtocolDebugSession::changed,q,[this]{stateDirty=true;});}
    return root;
}

QWidget* MainWindow::Impl::buildWorkspace() {
    auto* w=named(new QWidget,"workspacePage");auto* l=new QVBoxLayout(w);l->setContentsMargins(0,0,0,0);l->setSpacing(0);
    auto* header=new QWidget;auto* head=new QHBoxLayout(header);head->setContentsMargins(25,19,25,17);head->setSpacing(14);
    auto* titles=new QVBoxLayout;titles->setSpacing(6);eyebrow=label({},"sessionEyebrow");titles->addWidget(eyebrow);
    auto* titleRow=rowLayout();title=label({},"sessionTitle");title->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);titleRow->addWidget(title,1);state=label(QStringLiteral("未连接"),"connectionState");state->setFixedHeight(20);titleRow->addWidget(state);titleRow->addStretch();titles->addLayout(titleRow);
    endpoint=label(QStringLiteral("实际本地端点：未建立"),"actualEndpoint");endpoint->setProperty("muted",true);endpoint->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);titles->addWidget(endpoint);head->addLayout(titles,1);
    auto* modes=named(new QWidget,"modeSwitch");modes->setFixedHeight(32);auto* modeRow=rowLayout();modeRow->setSpacing(0);modeRow->setContentsMargins(3,3,3,3);modes->setLayout(modeRow);
    ordinaryMode=button(QStringLiteral("普通调试"),"ordinaryModeButton");fastMode=button(QStringLiteral("高速采集"),"highSpeedModeButton");for(auto* b:{ordinaryMode,fastMode}){b->setCheckable(true);b->setProperty("segment",true);modeRow->addWidget(b);}head->addWidget(modes);
    high=named(new QCheckBox(QStringLiteral("高速采集 / 抽样")),"highSpeedMode");high->hide();head->addWidget(high);
    recordButton=button(QStringLiteral("开始记录"),"recordButton");recordButton->setProperty("primary",true);head->addWidget(recordButton);l->addWidget(header);
    QObject::connect(ordinaryMode,&QPushButton::clicked,q,[this]{high->setChecked(false);updateState();});QObject::connect(fastMode,&QPushButton::clicked,q,[this]{high->setChecked(true);updateState();});
    auto* strip=named(new QWidget,"metricsStrip");auto* metrics=new QHBoxLayout(strip);metrics->setContentsMargins(0,14,0,17);metrics->setSpacing(0);
    int metricIndex=0;auto metric=[&](const QString& heading,const char* name,const QString& note){auto* column=named(new QWidget,"metricColumn");auto* col=new QVBoxLayout(column);col->setContentsMargins(metricIndex?22:0,0,metricIndex==3?0:22,0);col->setSpacing(4);auto* headingRow=rowLayout();auto* caption=label(heading);caption->setObjectName(QString::fromLatin1(name)+"Caption");caption->setProperty("muted",true);caption->setProperty("fieldCaption",true);headingRow->addWidget(caption,1);metricTags[metricIndex]=label({},(QByteArray(name)+"Tag").constData());metricTags[metricIndex]->setProperty("metricTag",true);headingRow->addWidget(metricTags[metricIndex]);col->addLayout(headingRow);auto* v=label("0",name);v->setMinimumWidth(0);v->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);v->setMinimumHeight(39);col->addWidget(v);metricNotes[metricIndex]=label(note);metricNotes[metricIndex]->setProperty("small",true);metricNotes[metricIndex]->setProperty("muted",true);col->addWidget(metricNotes[metricIndex]);metrics->addWidget(column,1);if(++metricIndex<4){auto* separator=named(new QFrame,"metricDivider");separator->setFixedWidth(1);metrics->addWidget(separator);}return v;};
    rxMetric=metric(QStringLiteral("接收速率"),"rxRate",QStringLiteral("有效负载 · 实时统计"));rxMetric->setTextFormat(Qt::RichText);txRate=named(metricNotes[0],"txRate");txRate->setAccessibleName(QStringLiteral("发送速率"));
    ppsMetric=metric(QStringLiteral("累计接收字节"),"packetRate",QStringLiteral("应用实际收到的数据"));ppsMetric->setTextFormat(Qt::RichText);
    lossMetric=metric(QStringLiteral("测试序号缺失"),"sequenceMetric",QStringLiteral("未配置协议序号"));recordMetric=metric(QStringLiteral("原始记录"),"recordingMetric",QStringLiteral("开启后保存原始字节与索引"));
    auto* metricsInset=new QHBoxLayout;metricsInset->setContentsMargins(25,0,25,0);metricsInset->addWidget(strip);l->addLayout(metricsInset);
    trendPanel=named(new QWidget,"throughputTrendPanel");auto* trendRow=new QHBoxLayout(trendPanel);trendRow->setContentsMargins(25,13,25,13);trendRow->setSpacing(22);trend=new Trend;trendRow->addWidget(trend,1);
    auto* health=named(new QWidget,"queueHealth");health->setFixedWidth(215);auto* healthLayout=new QVBoxLayout(health);healthLayout->setContentsMargins(20,0,0,0);healthLayout->setSpacing(7);
    healthTitle=label(QStringLiteral("●  接收管线待命"),"queueHealthTitle");healthLayout->addWidget(healthTitle);
    queueStatus=label({},"queueStatus");queueStatus->setProperty("small",true);queueStatus->setProperty("muted",true);healthLayout->addWidget(queueStatus);
    queueMeter=named(new QProgressBar,"queueFill");queueMeter->setRange(0,1000);queueMeter->setTextVisible(false);queueMeter->setFixedHeight(3);healthLayout->addWidget(queueMeter);
    healthDetail=label(QStringLiteral("显示抽样不等于接收丢失"));healthDetail->setProperty("small",true);healthDetail->setProperty("muted",true);healthLayout->addWidget(healthDetail);trendRow->addWidget(health);l->addWidget(trendPanel);
    auto* toolbar=named(new QWidget,"dataToolbar");auto* tools=new QHBoxLayout(toolbar);tools->setContentsMargins(22,0,18,0);tools->setSpacing(7);
    QPushButton* tabs[3];const QString tabNames[]={QStringLiteral("数据样本"),QStringLiteral("文本流"),QStringLiteral("接收诊断")};
    for(int i=0;i<3;++i){tabs[i]=button(tabNames[i],("dataViewTab"+QByteArray::number(i)).constData());tabs[i]->setProperty("viewTab",true);tabs[i]->setCheckable(true);tabs[i]->setFixedHeight(42);tools->addWidget(tabs[i]);}tools->addStretch();
    filterKind=combo({QStringLiteral("来源"),QStringLiteral("HEX"),QStringLiteral("文本")},"filterKind");filterKind->setFixedWidth(62);filterKind->setToolTip(QStringLiteral("搜索条件：来源 / ID、HEX字节、解码文本"));tools->addWidget(filterKind);
    filter=named(new QLineEdit,"recordFilter");filter->setAccessibleName(QStringLiteral("搜索保留数据样本"));filter->setPlaceholderText(QStringLiteral("搜索字节或来源     Ctrl K"));filter->setClearButtonEnabled(true);filter->setMinimumWidth(100);filter->setMaximumWidth(225);tools->addWidget(filter,1);
    displayFormat=combo({"HEX",QStringLiteral("文本")},"displayFormat");displayFormat->setFixedWidth(64);
    paused=named(new QCheckBox(QStringLiteral("暂停")),"pauseDisplay");paused->setAccessibleName(QStringLiteral("暂停显示"));paused->setToolTip(QStringLiteral("暂停显示，接收与记录继续"));tools->addWidget(paused);
    auto* clear=button(QStringLiteral("清空显示"),"clearDisplay");auto* reset=button(QStringLiteral("重置统计"),"resetStatistics");for(auto* b:{clear,reset}){b->setProperty("iconOnly",true);b->setFixedSize(26,26);tools->addWidget(b);}l->addWidget(toolbar);
    banner=label({},"runtimeBanner");banner->setWordWrap(true);banner->setProperty("error",true);banner->hide();l->addWidget(banner);
    dataSplitter=named(new QSplitter(Qt::Horizontal),"dataSplitter");dataSplitter->setHandleWidth(1);dataSplitter->setChildrenCollapsible(false);
    auto* recordsPanel=new QWidget;auto* recordLayout=new QVBoxLayout(recordsPanel);recordLayout->setContentsMargins(0,0,0,0);recordLayout->setSpacing(0);
    auto* noticeRow=rowLayout();noticeRow->setSpacing(6);notice=label({},"displayNotice");notice->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);notice->setWordWrap(true);noticeRow->addWidget(notice,1);noticeRow->addWidget(displayFormat);recordLayout->addLayout(noticeRow);
    dataTabs=named(new QTabWidget,"dataTabs");dataTabs->tabBar()->hide();
    table=named(new design::RecordView,"recordTable");table->setAccessibleName(QStringLiteral("真实收发记录"));model=new RecordModel(q);proxy=new RecordFilter(q);proxy->setSourceModel(model);table->setModel(proxy);table->setItemDelegate(new design::RecordDelegate(table));table->setMouseTracking(true);table->setSelectionBehavior(QAbstractItemView::SelectRows);table->setSelectionMode(QAbstractItemView::SingleSelection);table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->setShowGrid(false);table->setAlternatingRowColors(false);table->verticalHeader()->hide();table->verticalHeader()->setDefaultSectionSize(32);
    table->horizontalHeader()->setStretchLastSection(true);table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft|Qt::AlignVCenter);table->horizontalHeader()->setFixedHeight(30);table->setColumnWidth(0,45);table->setColumnWidth(1,112);table->setColumnWidth(2,65);table->setColumnWidth(3,170);table->setColumnWidth(4,65);dataTabs->addTab(table,QStringLiteral("数据样本"));
    auto* textPanel=new QWidget;auto* textLayout=new QVBoxLayout(textPanel);textLayout->setContentsMargins(12,6,12,6);auto* textTools=rowLayout();
    auto* rxLegend=label(QStringLiteral("▼ RX 接收"),"textRxLegend");auto* txLegend=label(QStringLiteral("▲ TX 发送"),"textTxLegend");textTools->addWidget(rxLegend);textTools->addWidget(txLegend);textTools->addStretch();textFormat=combo({QStringLiteral("UTF-8 原文"),QStringLiteral("HEX · 16字节/行"),QStringLiteral("JSON 格式化")},"textPreviewFormat");textFormat->setMinimumWidth(165);textFormat->setToolTip(QStringLiteral("格式化保留的样本。JSON 仅格式化完整有效的对象/数组；无效或跨读取块内容保留原文。"));textTools->addWidget(textFormat);textLayout->addLayout(textTools);
    streamView=named(new QPlainTextEdit,"normalStreamView");streamView->setAccessibleName(QStringLiteral("收发分色文本预览"));streamView->setReadOnly(true);streamView->setFont(fixedFont());streamView->document()->setMaximumBlockCount(10000);streamView->setPlaceholderText(QStringLiteral("接收或发送后显示内容；RX 为绿色，TX 为橙色。"));textLayout->addWidget(streamView);dataTabs->addTab(textPanel,QStringLiteral("文本预览"));
    QObject::connect(textFormat,&QComboBox::currentIndexChanged,q,[this]{renderTextPreview();});
    auto* diagnosticPanel=named(new QWidget,"diagnosticsPanel");auto* dl=new QVBoxLayout(diagnosticPanel);dl->setContentsMargins(22,10,22,10);dl->setSpacing(3);auto* diagnosticHeading=label(QStringLiteral("接收与记录诊断"));diagnosticHeading->setProperty("diagnosticHeading",true);dl->addWidget(diagnosticHeading);auto* diagnosticNote=label(QStringLiteral("核对实际接收、截断、记录与显示。只有启用报文序号分析才能检查序号缺失。"));diagnosticNote->setProperty("muted",true);diagnosticNote->setWordWrap(true);dl->addWidget(diagnosticNote);
    const QString stages[]={QStringLiteral("应用接收"),QStringLiteral("接收截断"),QStringLiteral("应用队列"),QStringLiteral("原始记录"),QStringLiteral("显示抽样")};const QString descriptions[]={QStringLiteral("本地实际读到的字节"),QStringLiteral("UDP 接收缓冲不足事件，独立累计"),QStringLiteral("明确归因的应用丢弃"),QStringLiteral("队列与磁盘写入结果"),QStringLiteral("有界保留，省略不代表网络丢失")};
    for(int i=2;i<7;++i){auto* stage=named(new QWidget,"diagnosticStage");auto* row=new QHBoxLayout(stage);row->setContentsMargins(2,2,2,2);row->setSpacing(10);auto* ordinal=label(QString::number(i-1).rightJustified(2,'0'));ordinal->setProperty("muted",true);ordinal->setFont(design::font(10,true));row->addWidget(ordinal);auto* caption=new QVBoxLayout;caption->setSpacing(0);caption->addWidget(label(stages[i-2]));auto* description=label(descriptions[i-2]);description->setProperty("small",true);caption->addWidget(description);row->addLayout(caption,1);diagnosticValues[i]=label(QStringLiteral("未知"),("diagnosticValue"+QByteArray::number(i)).constData());diagnosticValues[i]->setAlignment(Qt::AlignRight|Qt::AlignVCenter);diagnosticValues[i]->setFont(design::font(11,true));row->addWidget(diagnosticValues[i]);dl->addWidget(stage);}
    diagnostics=label({},"diagnosticsSummary");diagnostics->setWordWrap(true);diagnostics->setTextInteractionFlags(Qt::TextSelectableByKeyboard|Qt::TextSelectableByMouse);diagnostics->hide();auto* details=named(new QToolButton,"diagnosticDetails");details->setText(QStringLiteral("完整计数与说明"));details->setCheckable(true);details->setArrowType(Qt::RightArrow);details->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);QObject::connect(details,&QToolButton::toggled,diagnostics,&QLabel::setVisible);QObject::connect(details,&QToolButton::toggled,q,[details](bool expanded){details->setArrowType(expanded?Qt::DownArrow:Qt::RightArrow);});dl->addWidget(details,0,Qt::AlignLeft);dl->addWidget(diagnostics);dl->addStretch();dataTabs->addTab(scrollPanel(diagnosticPanel),QStringLiteral("接收诊断"));recordLayout->addWidget(dataTabs,1);
    for(int i=0;i<3;++i){QObject::connect(tabs[i],&QPushButton::clicked,q,[this,i,b=tabs[i]]{dataTabs->setCurrentIndex(i);b->setChecked(true);});}QObject::connect(dataTabs,&QTabWidget::currentChanged,q,[this](int current){for(int i=0;i<3;++i)q->findChild<QPushButton*>(QStringLiteral("dataViewTab%1").arg(i))->setChecked(current==i);});tabs[0]->setChecked(true);
    auto* bottomWidget=named(new QWidget,"recordFooter");auto* bottom=new QHBoxLayout(bottomWidget);bottom->setContentsMargins(22,0,16,0);bottom->setSpacing(10);bottomWidget->setMinimumHeight(31);
    retained=label({},"retainedSamples");retained->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);bottom->addWidget(retained,1);autoScroll=named(new QCheckBox(QStringLiteral("自动滚动")),"autoScroll");autoScroll->setChecked(true);bottom->addWidget(autoScroll);auto* copy=button(QStringLiteral("复制原始 HEX"),"copyRecord");copy->setProperty("iconOnly",true);copy->setFixedSize(24,24);bottom->addWidget(copy);auto* exportButton=button(QStringLiteral("导出显示样本"),"exportSamples");exportButton->setProperty("textAction",true);bottom->addWidget(exportButton);sampleExportButton=exportButton;cancelSamples=button(QStringLiteral("取消样本导出"),"cancelSampleExport");cancelSamples->hide();bottom->addWidget(cancelSamples);QObject::connect(cancelSamples,&QPushButton::clicked,q,[this]{if(sampleJob)sampleJob->cancel.store(true,std::memory_order_relaxed);});recordLayout->addWidget(bottomWidget);
    dataSplitter->addWidget(recordsPanel);dataSplitter->addWidget(buildInspector());dataSplitter->setStretchFactor(0,1);dataSplitter->setStretchFactor(1,0);dataSplitter->setSizes({858,246});
    workspaceSplitter=named(new QSplitter(Qt::Vertical),"workspaceSplitter");workspaceSplitter->setHandleWidth(7);workspaceSplitter->setAccessibleName(QStringLiteral("拖动调整接收区和发送编辑区高度"));workspaceSplitter->setToolTip(QStringLiteral("拖动分隔条调整发送编辑区大小"));
    dataSplitter->setMinimumHeight(100);workspaceSplitter->addWidget(dataSplitter);workspaceSplitter->addWidget(buildComposer());workspaceSplitter->setCollapsible(0,true);workspaceSplitter->setCollapsible(1,false);workspaceSplitter->setStretchFactor(0,1);workspaceSplitter->setStretchFactor(1,1);workspaceSplitter->setSizes({220,360});l->addWidget(workspaceSplitter,1);
    QObject::connect(workspaceSplitter,&QSplitter::splitterMoved,this,[this]{const bool expanded=workspaceSplitter->sizes().front()==0;const QSignalBlocker block(expandComposer);expandComposer->setChecked(expanded);expandComposer->setText(expanded?QStringLiteral("收起编辑"):QStringLiteral("展开编辑"));expandComposer->setArrowType(expanded?Qt::DownArrow:Qt::UpArrow);if(!expanded)composerSplitSizes=workspaceSplitter->sizes();trendPanel->setVisible(q->height()>=850&&!expanded);});
    QObject::connect(filter,&QLineEdit::textChanged,q,[this]{proxy->setQuery(filter->text(),filterKind->currentIndex());inspect();});QObject::connect(filterKind,&QComboBox::currentIndexChanged,q,[this]{proxy->setQuery(filter->text(),filterKind->currentIndex());inspect();});
    QObject::connect(displayFormat,&QComboBox::currentIndexChanged,q,[this](int i){model->setText(i==1);});
    QObject::connect(paused,&QCheckBox::toggled,q,[this](bool v){c->setDisplayPaused(v);if(!v)model->resetDecoders();updateState();});
    QObject::connect(high,&QCheckBox::toggled,q,[this](bool v){c->setHighSpeed(v);model->resetDecoders();streamView->clear();lastDisplaySequence=0;streamOmittedCharacters=0;updateState();});
    QObject::connect(clear,&QPushButton::clicked,q,[this]{c->clearDisplay();model->clear();streamView->clear();lastDisplaySequence=0;streamOmittedCharacters=0;inspect();snapshot();});
    QObject::connect(reset,&QPushButton::clicked,q,[this]{c->resetStatistics();trend->clear();snapshot();});QObject::connect(table->selectionModel(),&QItemSelectionModel::currentRowChanged,q,[this]{inspect();});
    QObject::connect(table->selectionModel(),&QItemSelectionModel::selectionChanged,q,[this]{inspect();});
    for(auto* observed:{static_cast<QAbstractItemModel*>(model),static_cast<QAbstractItemModel*>(proxy)}) {
        QObject::connect(observed,&QAbstractItemModel::modelReset,q,[this]{table->clearSelection();table->setCurrentIndex({});inspect();});
        QObject::connect(observed,&QAbstractItemModel::rowsRemoved,q,[this]{inspect();});
        QObject::connect(observed,&QAbstractItemModel::layoutChanged,q,[this]{inspect();});
    }
    QObject::connect(copy,&QPushButton::clicked,q,[this]{const auto* r=model->record(proxy->mapToSource(table->currentIndex()).row());if(r)QApplication::clipboard()->setText(hexBytes(recordBytes(*r)));});
    QObject::connect(exportButton,&QPushButton::clicked,q,[this]{exportSamples();});QObject::connect(recordButton,&QPushButton::clicked,q,[this]{toggleRecording();});return w;
}

QWidget* MainWindow::Impl::buildInspector() {
    auto* tabs=named(new QTabWidget,"inspectorTabs");tabs->setMinimumWidth(222);tabs->setMaximumWidth(640);
    auto* bytes=named(new QWidget,"bytePanel");auto* bl=new QVBoxLayout(bytes);bl->setContentsMargins(16,12,16,12);bl->setSpacing(7);
    byteSummary=label(QStringLiteral("未选择记录"),"selectedByteSummary");byteSummary->setTextFormat(Qt::RichText);byteSummary->setWordWrap(true);bl->addWidget(byteSummary);
    auto* contentHeading=rowLayout();contentHeading->addWidget(label(QStringLiteral("字节页")));bytePage=spin(1,1,1,"bytePage");bytePage->setEnabled(false);bytePage->setAccessibleName(QStringLiteral("完整字节分页，每页 4096 字节"));contentHeading->addWidget(bytePage,1);auto* copyBytes=button(QStringLiteral("复制所选记录完整原始 HEX"),"copyInspectorBytes");copyBytes->setProperty("iconOnly",true);copyBytes->setFixedSize(24,24);copyBytes->setEnabled(false);contentHeading->addWidget(copyBytes);bl->addLayout(contentHeading);
    auto* views=named(new QTabWidget,"byteFormatTabs");views->setFixedHeight(180);
    auto* hexPanel=new QWidget;auto* hexLayout=new QHBoxLayout(hexPanel);hexLayout->setContentsMargins(0,4,0,0);hexLayout->setSpacing(4);
    auto byteEditor=[&](const char* name){auto* view=named(new QPlainTextEdit,name);view->setReadOnly(true);view->setLineWrapMode(QPlainTextEdit::NoWrap);view->setFont(design::font(10,true));return view;};
    byteOffsets=byteEditor("byteOffsets");byteOffsets->setFixedWidth(45);byteOffsets->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);byteOffsets->setAccessibleName(QStringLiteral("当前页字节偏移"));hexLayout->addWidget(byteOffsets);
    byteView=byteEditor("byteInspector");byteView->setAccessibleName(QStringLiteral("完整 HEX，按 4096 字节分页，页内可滚动"));hexLayout->addWidget(byteView,1);views->addTab(hexPanel,"HEX");
    byteAscii=byteEditor("byteAscii");byteAscii->setLineWrapMode(QPlainTextEdit::WidgetWidth);byteAscii->setAccessibleName(QStringLiteral("完整 ASCII，按 4096 字节分页，页内可滚动；非可打印字节显示点"));byteAscii->setToolTip(QStringLiteral("ASCII 只显示可打印单字节字符；中文请切换 UTF-8 页签。"));views->addTab(byteAscii,"ASCII");
    byteUtf8=byteEditor("byteUtf8");byteUtf8->setLineWrapMode(QPlainTextEdit::WidgetWidth);byteUtf8->setAccessibleName(QStringLiteral("UTF-8 文本，中文与完整字符分页；无效字节提示编码错误"));views->addTab(byteUtf8,"UTF-8");views->setTabToolTip(1,QStringLiteral("逐字节 ASCII，中文等非 ASCII 字节显示点"));views->setTabToolTip(2,QStringLiteral("UTF-8 解码，可阅读中文；不改变原始字节"));views->setCurrentIndex(1);bl->addWidget(views);
    byteTextStatus=label(QStringLiteral("中文请查看 UTF-8；ASCII 的点表示非可打印字节。"),"byteTextStatus");byteTextStatus->setWordWrap(true);byteTextStatus->setProperty("small",true);bl->addWidget(byteTextStatus);
    QObject::connect(byteView->verticalScrollBar(),&QScrollBar::valueChanged,byteOffsets->verticalScrollBar(),&QScrollBar::setValue);
    byteRange=label(QStringLiteral("选择记录后显示字节范围"),"byteRange");byteRange->setWordWrap(true);bl->addWidget(byteRange);
    QObject::connect(byteOffsets->verticalScrollBar(),&QScrollBar::valueChanged,byteView->verticalScrollBar(),&QScrollBar::setValue);
    QObject::connect(bytePage,&QSpinBox::valueChanged,q,[this]{inspect();revealByteText();});
    QObject::connect(views,&QTabWidget::currentChanged,q,[this](int index){if(index==2)revealByteText();});bl->addStretch();tabs->addTab(named(scrollPanel(bytes),"byteDetailsScroll"),QStringLiteral("字节详情"));
    QObject::connect(copyBytes,&QPushButton::clicked,q,[this]{const auto* r=model->record(proxy->mapToSource(table->currentIndex()).row());if(r)QApplication::clipboard()->setText(hexBytes(recordBytes(*r)));});
    auto* capture=named(new QWidget,"capturePanel");auto* cl=new QVBoxLayout(capture);cl->setContentsMargins(12,12,12,12);auto* text=label(QStringLiteral("原始二进制记录\n保留来源、方向及 UDP 边界。记录独立于显示。"));text->setWordWrap(true);cl->addWidget(text);
    recordFields=new QWidget;auto* f=new QFormLayout(recordFields);f->setContentsMargins(0,0,0,0);f->setRowWrapPolicy(QFormLayout::WrapAllRows);f->setVerticalSpacing(8);
    const auto defaultDir=QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath("PortBridge/Captures");
    recordDirectory=named(new QLineEdit(settings.value("capture/directory",defaultDir).toString()),"recordDirectory");auto* dirRow=rowLayout();dirRow->addWidget(recordDirectory,1);auto* pick=button(QStringLiteral("…"),"chooseRecordDirectory");pick->setToolTip(QStringLiteral("选择真实采集目录"));dirRow->addWidget(pick);buddy(f,QStringLiteral("保存目录"),panel(dirRow));
    rotation=spin(1,4096,settings.value("capture/rotation",1024).toInt(),"recordRotationMiB");rotation->setSuffix(" MiB");
    duration=spin(0,86400,settings.value("capture/duration",0).toInt(),"recordDurationSeconds");duration->setSpecialValueText(QStringLiteral("手动停止"));duration->setSuffix(" s");
    auto* strategy=rowLayout();for(const auto& entry:{qMakePair(QStringLiteral("文件轮转"),static_cast<QWidget*>(rotation)),qMakePair(QStringLiteral("采集时长"),static_cast<QWidget*>(duration))}){auto* col=new QVBoxLayout;col->setSpacing(5);auto* caption=label(entry.first);caption->setBuddy(entry.second);entry.second->setAccessibleName(entry.first);col->addWidget(caption);col->addWidget(entry.second);strategy->addLayout(col,1);}f->addRow(panel(strategy));
    recordQueue=spin(1,1024,settings.value("capture/queueMiB",128).toInt(),"recordQueueMiB");recordQueue->setSuffix(" MiB");buddy(f,QStringLiteral("记录队列上限"),recordQueue);cl->addWidget(recordFields);
    capacity=label({},"recordCapacityEstimate");capacity->setWordWrap(true);cl->addWidget(capacity);storage=label({},"storageFacts");storage->setWordWrap(true);cl->addWidget(storage);
    auto* warning=label(QStringLiteral("持续写入能力：未知（未测量）\n容量按当前有效负载估算，未计文件索引开销。未开启记录不能据失败计数为 0 推断完整。"));warning->setWordWrap(true);warning->setProperty("muted",true);cl->addWidget(warning);cl->addStretch();tabs->addTab(scrollPanel(capture),QStringLiteral("采集设置"));
    QObject::connect(pick,&QPushButton::clicked,q,[this]{const auto path=QFileDialog::getExistingDirectory(q,QStringLiteral("采集目录"),recordDirectory->text());if(!path.isEmpty())recordDirectory->setText(path);});
    QObject::connect(recordDirectory,&QLineEdit::textChanged,q,[this]{updateStorage();});return tabs;
}

QWidget* MainWindow::Impl::buildComposer() {
    auto* w=new QWidget;w->setObjectName("composer");auto* l=new QVBoxLayout(w);l->setContentsMargins(22,8,22,6);l->setSpacing(4);l->setSizeConstraint(QLayout::SetMinimumSize);
    auto* heading=rowLayout();auto* composerIcon=named(new QLabel,"composerIcon");composerIcon->setFixedSize(14,14);heading->addWidget(composerIcon);heading->addWidget(label(QStringLiteral("发送数据")),1);
    clientTarget=combo({},"serverClientTarget");clientTarget->setMinimumWidth(140);heading->addWidget(clientTarget);
    broadcast=named(new QCheckBox(QStringLiteral("广播所有客户端")),"serverBroadcast");heading->addWidget(broadcast);disconnectClient=button(QStringLiteral("断开客户端"),"disconnectClient");heading->addWidget(disconnectClient);
    expandComposer=named(new QToolButton,"expandComposer");expandComposer->setText(QStringLiteral("展开编辑"));expandComposer->setCheckable(true);expandComposer->setArrowType(Qt::UpArrow);expandComposer->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);expandComposer->setAccessibleName(QStringLiteral("展开或收起发送编辑区"));expandComposer->setToolTip(QStringLiteral("展开编辑区；不改变连接、接收或发送内容"));heading->addWidget(expandComposer);QObject::connect(expandComposer,&QToolButton::toggled,this,[this](bool expanded){setComposerExpanded(expanded);});
    auto* commandLink=button(QStringLiteral("命令库"),"composerCommands");commandLink->setProperty("textAction",true);heading->addWidget(commandLink);QObject::connect(commandLink,&QPushButton::clicked,q,[this]{navigate(2);});l->addLayout(heading);
    udpSendFields=named(new QWidget,"udpSendFields");udpTargetLayout=rowLayout();udpSendFields->setLayout(udpTargetLayout);
    auto* targetCaption=label(QStringLiteral("UDP 发送目标"));targetCaption->setProperty("sectionHeading",true);udpTargetLayout->addWidget(targetCaption);
    udpTargetStatus=label({},"udpTargetStatus");udpTargetStatus->setWordWrap(true);udpTargetStatus->setMinimumWidth(110);udpTargetStatus->setMaximumWidth(160);udpTargetStatus->setProperty("muted",true);udpTargetLayout->addWidget(udpTargetStatus);l->addWidget(udpSendFields);
    QObject::connect(remoteAddress,&QLineEdit::textChanged,q,[this]{if(sendButton)validateSend();});QObject::connect(remotePort,&QSpinBox::valueChanged,q,[this]{if(sendButton)validateSend();});
    auto* body=new QVBoxLayout;body->setSpacing(8);sendFields=named(new QWidget,"sendEditor");sendFields->setMinimumHeight(120);auto* editorLayout=new QVBoxLayout(sendFields);editorLayout->setContentsMargins(0,0,0,0);editorLayout->setSpacing(0);
    sendInput=named(new QPlainTextEdit,"sendInput");sendInput->setAccessibleName(QStringLiteral("待发送原文"));sendInput->setMinimumHeight(96);sendInput->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);sendInput->setFont(design::font(11,true));sendInput->setPlaceholderText(QStringLiteral("输入待发送数据 · 支持多行编辑"));editorLayout->addWidget(sendInput,1);
    validation=label(QStringLiteral("0 字节"),"sendValidation");validation->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);validation->setMinimumHeight(24);validation->setToolTip(QStringLiteral("Ctrl+Enter发送 / 停止；本地写出不等于对端确认"));editorLayout->addWidget(validation);body->addWidget(sendFields,1);
    auto* settingsPanel=named(new QWidget,"sendSettings");auto* settingsLayout=new QHBoxLayout(settingsPanel);settingsLayout->setContentsMargins(0,0,0,0);settingsLayout->setSpacing(18);
    auto* formats=rowLayout();formats->setSpacing(9);sendFormat=combo({"HEX",QStringLiteral("文本")},"sendFormat");encoding=combo({"UTF-8","ASCII"},"sendEncoding");eol=eolCombo("sendEol");sendFormat->setAccessibleName(QStringLiteral("发送格式"));encoding->setAccessibleName(QStringLiteral("文本编码"));eol->setAccessibleName(QStringLiteral("附加换行"));
    auto* formatColumn=new QVBoxLayout;formatColumn->setSpacing(4);auto* formatCaption=label(QStringLiteral("发送格式"));formatCaption->setProperty("small",true);formatColumn->addWidget(formatCaption);auto* choices=rowLayout();choices->setSpacing(4);choices->addWidget(sendFormat,1);choices->addWidget(encoding,1);formatColumn->addLayout(choices);formats->addLayout(formatColumn,1);
    auto* eolColumn=new QVBoxLayout;eolColumn->setSpacing(4);auto* eolCaption=label(QStringLiteral("附加换行"));eolCaption->setProperty("small",true);eolColumn->addWidget(eolCaption);eolColumn->addWidget(eol);formats->addLayout(eolColumn,1);settingsLayout->addLayout(formats,1);
    auto* sendRow=rowLayout();sendRow->setSpacing(6);periodicFields=new QWidget;auto* pf=rowLayout();pf->setSpacing(4);periodicFields->setLayout(pf);
    periodic=named(new QCheckBox(QStringLiteral("周期")),"periodicEnabled");periodic->setAccessibleName(QStringLiteral("有限或持续周期发送"));periodic->setToolTip(QStringLiteral("运行中锁定内容、格式、换行、间隔及次数"));pf->addWidget(periodic);
    interval=spin(1,86400000,1000,"periodicInterval");interval->setSuffix(" ms");interval->setFixedWidth(106);interval->setAccessibleName(QStringLiteral("周期发送间隔"));interval->setToolTip(QStringLiteral("1000 ms · 允许 1–86,400,000 ms"));QObject::connect(interval,&QSpinBox::valueChanged,q,[this](int value){interval->setToolTip(QStringLiteral("%1 ms · 允许 1–86,400,000 ms").arg(value));});pf->addWidget(interval);
    count=spin(0,1000000,10,"periodicCount");count->setSpecialValueText(QStringLiteral("持续"));count->setSuffix(QStringLiteral(" 次"));count->setFixedWidth(94);count->setAccessibleName(QStringLiteral("发送次数，0 为持续"));count->setToolTip(QStringLiteral("0：持续，直到停止、断线或启动其他方案；其他值：有限次数"));pf->addWidget(count);sendRow->addWidget(periodicFields);
    sendButton=button(QStringLiteral("发送"),"sendButton");sendButton->setProperty("primary",true);sendRow->addWidget(sendButton,1);settingsLayout->addLayout(sendRow);body->addWidget(settingsPanel);l->addLayout(body,1);
    auto* quickRow=rowLayout();auto* chipsCaption=label(QStringLiteral("快捷命令"));chipsCaption->setProperty("small",true);quickRow->addWidget(chipsCaption);quickChips=named(new QWidget,"quickCommandChips");quickChipLayout=rowLayout();quickChips->setLayout(quickChipLayout);quickRow->addWidget(quickChips,1);
    auto* overflow=named(new QToolButton,"quickCommandOverflow");overflow->setText(QStringLiteral("更多 / 历史"));overflow->setProperty("quickMenu",true);overflow->setPopupMode(QToolButton::InstantPopup);overflow->setAccessibleName(QStringLiteral("所有快捷命令与发送历史"));quickRow->addWidget(overflow);l->addLayout(quickRow);
    auto* quick=new QVBoxLayout;quick->setContentsMargins(10,10,10,10);auto* quickCaption=label(QStringLiteral("全部快捷命令（仅载入）"));quick->addWidget(quickCaption);quickBox=combo({},"quickCommands");quick->addWidget(quickBox);auto* loadQuick=button(QStringLiteral("载入"),"loadQuickCommand");quick->addWidget(loadQuick);auto* historyCaption=label(QStringLiteral("发送历史（仅载入）"));quick->addWidget(historyCaption);historyBox=combo({},"sendHistory");quick->addWidget(historyBox);auto* loadHistory=button(QStringLiteral("载入"),"loadHistory");quick->addWidget(loadHistory);
    auto* quickMenu=new QMenu(overflow);auto* quickAction=new QWidgetAction(quickMenu);quickAction->setDefaultWidget(panel(quick));quickMenu->addAction(quickAction);overflow->setMenu(quickMenu);QObject::connect(loadQuick,&QPushButton::clicked,quickMenu,&QMenu::close);QObject::connect(loadHistory,&QPushButton::clicked,quickMenu,&QMenu::close);
    for(auto* cb:{sendFormat,encoding,eol}) { QObject::connect(cb,&QComboBox::currentIndexChanged,q,[this]{validateSend();}); }
    QObject::connect(sendInput,&QPlainTextEdit::textChanged,q,[this]{validateSend();});QObject::connect(sendButton,&QPushButton::clicked,q,[this]{send();});
    QObject::connect(loadQuick,&QPushButton::clicked,q,[this]{const int i=quickBox->currentIndex();if(i>=0&&i<commands.size())loadCommand(commands[i]);});
    QObject::connect(loadHistory,&QPushButton::clicked,q,[this]{const int i=historyBox->currentIndex();if(i>=0&&i<history.size())loadCommand(history[i]);});
    QObject::connect(disconnectClient,&QPushButton::clicked,q,[this]{const auto id=clientTarget->currentData().toULongLong();if(id)c->disconnectClient(id);});QObject::connect(broadcast,&QCheckBox::toggled,q,[this]{updateState();});QObject::connect(clientTarget,&QComboBox::currentIndexChanged,q,[this]{updateTargetState();});return w;
}

QWidget* MainWindow::Impl::buildCommands() {
    auto* w=named(new QWidget,"commandsPage");auto* l=new QVBoxLayout(w);l->setContentsMargins(25,22,25,22);l->setSpacing(16);auto* header=rowLayout();auto* heading=label(QStringLiteral("命令库"));heading->setProperty("pageTitle",true);header->addWidget(heading,1);
    auto* add=button(QStringLiteral("添加命令"),"addCommand");add->setProperty("primary",true);header->addWidget(add);
    auto* edit=button(QStringLiteral("编辑"),"editCommand");auto* remove=button(QStringLiteral("删除"),"removeCommand");auto* load=button(QStringLiteral("载入工作台"),"loadCommand");auto* imp=button(QStringLiteral("导入 JSON"),"importCommands");auto* exp=button(QStringLiteral("导出 JSON"),"exportCommands");
    auto* more=named(new QToolButton,"commandMoreMenu");more->setText(QStringLiteral("更多"));more->setPopupMode(QToolButton::InstantPopup);auto* menu=new QMenu(more);more->setMenu(menu);header->addWidget(more);l->addLayout(header);
    for(auto* b:{edit,remove,load,imp,exp}){b->setParent(w);b->hide();auto* a=menu->addAction(b->text());QObject::connect(a,&QAction::triggered,b,&QPushButton::click);}
    auto* note=label(QStringLiteral("管理常用发送数据。载入工作台检查格式后，再主动发送。"));note->setProperty("muted",true);l->addWidget(note);
    commandList=named(new design::LibraryList(design::Icon::Terminal,QStringLiteral("尚未保存快捷命令"),QStringLiteral("添加常用数据，检查后载入工作台")),"commandList");commandList->setItemDelegate(new design::LibraryDelegate(design::Icon::Terminal,commandList));commandList->setAccessibleName(QStringLiteral("保存的命令库"));l->addWidget(commandList,1);
    QObject::connect(add,&QPushButton::clicked,q,[this]{editCommand(true);});QObject::connect(edit,&QPushButton::clicked,q,[this]{editCommand(false);});QObject::connect(remove,&QPushButton::clicked,q,[this]{const int i=commandList->currentRow();if(i>=0&&i<commands.size()){commands.removeAt(i);refreshCommands();persistCommands();}});
    QObject::connect(load,&QPushButton::clicked,q,[this]{const int i=commandList->currentRow();if(i>=0&&i<commands.size()){loadCommand(commands[i]);navigate(0);}});
    QObject::connect(imp,&QPushButton::clicked,q,[this]{const auto path=QFileDialog::getOpenFileName(q,QStringLiteral("导入版本化命令"),{},"JSON (*.json)");if(path.isEmpty())return;QVector<Command> imported;QString error;if(!importCommands(path,&imported,&error)){showError(error);return;}commands=imported;refreshCommands();persistCommands();});
    QObject::connect(exp,&QPushButton::clicked,q,[this]{const auto path=QFileDialog::getSaveFileName(q,QStringLiteral("导出版本化命令"),"commands.json","JSON (*.json)");if(path.isEmpty())return;QString error;if(!exportCommands(path,commands,&error))showError(error);});return w;
}
QWidget* MainWindow::Impl::buildCaptures() {
    auto* w=named(new QWidget,"capturesPage");auto* l=new QVBoxLayout(w);l->setContentsMargins(25,22,25,22);l->setSpacing(16);auto* header=rowLayout();auto* heading=label(QStringLiteral("采集文件"));heading->setProperty("pageTitle",true);header->addWidget(heading,1);auto* back=button(QStringLiteral("返回工作台"),"captureReturn");header->addWidget(back);QObject::connect(back,&QPushButton::clicked,q,[this]{navigate(0);});l->addLayout(header);
    auto* note=label(QStringLiteral("记录真实通信字节、来源和方向。完整收尾后可导出为 JSON 或文本。"));note->setToolTip(QStringLiteral("最近实际目录索引最多1024条，包含活动采集；历史磁盘文件不会因索引上限删除，较早文件可显式选择导出。"));note->setProperty("muted",true);note->setWordWrap(true);l->addWidget(note);
    auto* refresh=button(QStringLiteral("刷新"),"refreshCaptures");refresh->setProperty("textAction",true);header->insertWidget(1,refresh);
    auto* exp=button(QStringLiteral("导出选中实际采集"),"exportCapture");auto* open=button(QStringLiteral("选择实际采集文件导出…"),"openCapture");auto* more=named(new QToolButton,"captureMoreMenu");more->setText(QStringLiteral("更多"));more->setPopupMode(QToolButton::InstantPopup);auto* captureMenu=new QMenu(more);more->setMenu(captureMenu);header->insertWidget(2,more);for(auto* b:{exp,open}){b->setParent(w);b->hide();auto* action=captureMenu->addAction(b->text());QObject::connect(action,&QAction::triggered,b,&QPushButton::click);}
    exportCaptureButton=exp;openCaptureButton=open;
    auto* progressRow=rowLayout();exportStatus=label(QStringLiteral("没有导出任务"),"captureExportStatus");exportStatus->hide();progressRow->addWidget(exportStatus,1);cancelExport=button(QStringLiteral("取消导出"),"cancelCaptureExport");cancelExport->setEnabled(false);cancelExport->hide();progressRow->addWidget(cancelExport);l->addLayout(progressRow);
    exportProgress=named(new QProgressBar,"captureExportProgress");exportProgress->setRange(0,1000);exportProgress->setValue(0);exportProgress->setTextVisible(false);exportProgress->setFixedHeight(4);exportProgress->hide();l->addWidget(exportProgress);
    captureList=named(new design::LibraryList(design::Icon::Folder,QStringLiteral("暂无采集文件"),QStringLiteral("连接设备并开始记录后，实际文件会显示在这里")),"captureList");captureList->setItemDelegate(new design::LibraryDelegate(design::Icon::Folder,captureList));captureList->setAccessibleName(QStringLiteral("最近真实采集文件"));l->addWidget(captureList,1);
    QObject::connect(cancelExport,&QPushButton::clicked,q,[this]{if(exportJob){exportJob->cancel.store(true,std::memory_order_relaxed);exportStatus->setText(QStringLiteral("正在取消导出；原输出文件会保留。"));cancelExport->setEnabled(false);}});
    auto exportFile=[this](const QString& input){if(input.isEmpty())return;const auto output=QFileDialog::getSaveFileName(q,QStringLiteral("导出实际采集"),QFileInfo(input).completeBaseName()+".json","JSON (*.json);;Text (*.txt)");if(output.isEmpty())return;beginCaptureExport(input,output);};
    QObject::connect(refresh,&QPushButton::clicked,q,[this]{refreshCaptures(true);});QObject::connect(exp,&QPushButton::clicked,q,[this,exportFile]{auto* item=captureList->currentItem();if(item)exportFile(item->data(Qt::UserRole).toString());});QObject::connect(open,&QPushButton::clicked,q,[this,exportFile]{exportFile(QFileDialog::getOpenFileName(q,QStringLiteral("选择实际原始采集文件"),recordDirectory->text(),"Capture files (*.pbc *.bin);;All files (*)"));});return w;
}

void MainWindow::Impl::enumerate() {
    const QString selected=serialPort->currentText();const QSignalBlocker block(serialPort);serialPort->clear();
    for(const auto& p:QSerialPortInfo::availablePorts()){serialPort->addItem(p.portName());serialPort->setItemData(serialPort->count()-1,p.description()+" · "+p.manufacturer()+" · "+p.systemLocation(),Qt::ToolTipRole);}serialPort->setCurrentText(selected);
    const QString address=localAddress->currentText();const QSignalBlocker blockAddress(localAddress);localAddress->clear();localAddress->addItem("127.0.0.1");localAddress->addItem("0.0.0.0");
    for(const auto& nic:QNetworkInterface::allInterfaces())for(const auto& e:nic.addressEntries())if(e.ip().protocol()==QAbstractSocket::IPv4Protocol){const auto ip=e.ip().toString();if(localAddress->findText(ip)<0){localAddress->addItem(ip);localAddress->setItemData(localAddress->count()-1,nic.humanReadableName(),Qt::ToolTipRole);}}
    localAddress->setCurrentText(address.isEmpty()?QStringLiteral("127.0.0.1"):address);
}
void MainWindow::Impl::applyTheme() {
    const QString bg=dark?"#101416":"#edf1f1",surface=dark?"#171c1f":"#ffffff",side=dark?"#14191c":"#f7f9f9",input=dark?"#111719":"#ffffff",raised=dark?"#1d2428":"#f3f6f6",text=dark?"#e5edee":"#213336",muted=dark?"#8b9a9f":"#5e7379",dim=dark?"#63757d":"#70868c",line=dark?"#2b3438":"#d5dfe1",soft=dark?"#232b2f":"#e7eded",accent=dark?"#85dec4":"#176e58",accentBg=dark?"#243c35":"#dff1e9",selection=dark?"#21352f":"#e3f1eb",error=dark?"#ed9693":"#b04040";
    QPalette pal;pal.setColor(QPalette::Window,QColor(surface));pal.setColor(QPalette::WindowText,QColor(text));pal.setColor(QPalette::Base,QColor(input));pal.setColor(QPalette::AlternateBase,QColor(surface));pal.setColor(QPalette::Text,QColor(text));pal.setColor(QPalette::Button,QColor(raised));pal.setColor(QPalette::ButtonText,QColor(text));pal.setColor(QPalette::Highlight,QColor(selection));pal.setColor(QPalette::HighlightedText,QColor(text));pal.setColor(QPalette::ToolTipBase,QColor(raised));pal.setColor(QPalette::ToolTipText,QColor(text));pal.setColor(QPalette::Disabled,QPalette::Text,QColor(dim));pal.setColor(QPalette::Disabled,QPalette::ButtonText,QColor(dim));q->setPalette(pal);q->setFont(design::font(12));q->setProperty("darkTheme",dark);
    QString css=QStringLiteral(R"CSS(
QMainWindow {background:@bg;} QDialog, #workspacePage, #capturesPage, #commandsPage, #appHeader, #diagnosticsPanel {background:@surface;}
QProgressBar {border:0;background:@line;border-radius:2px;} QProgressBar::chunk {background:@accent;border-radius:2px;}
QWidget {color:@text;} QLabel {background:transparent;} QLabel[muted=true] {color:@muted;}
QLabel[small=true], #profileCount, #headerScope {font-size:10px;color:@dim;}
QLabel[pageTitle=true] {font-size:24px;font-weight:600;} QLabel[diagnosticHeading=true] {font-size:17px;font-weight:600;}
#diagnosticStage {background:transparent;border:0;border-bottom:1px solid @line;}
QLabel[fieldCaption=true], QLabel[sectionHeading=true], #connectionSidebar QLabel {font-size:11px;}
QLabel[metricTag=true] {font-family:'Consolas';font-size:8px;color:@dim;border:1px solid @line;border-radius:2px;padding:0 3px;}
QLabel[sectionHeading=true] {color:@muted;font-weight:600;} QLabel[error=true] {color:@error;}
#appHeader {border-bottom:1px solid @line;} #brandLabel {font-size:18px;font-weight:600;}
#brandVersion {font-size:10px;color:@dim;} #activityRail, #connectionSidebar {background:@side;border-right:1px solid @line;}
#railAvatar {border:1px solid @line;border-radius:5px;font-size:10px;color:@muted;}
#sidebarDivider {background:@line;} #connectionSidebar QScrollArea, #connectionSidebar QScrollArea > QWidget, #connectionFields {background:@side;}
QPushButton, QToolButton {background:@raised;border:1px solid @line;border-radius:4px;padding:5px 10px;min-height:18px;}
QPushButton:hover, QToolButton:hover {border-color:@muted;} QPushButton:focus, QToolButton:focus {border-color:@accent;}
QPushButton[primary=true] {background:@accent;border-color:@accent;color:@primaryText;}
QPushButton:disabled {background:@raised;color:@dim;border-color:@line;}
QPushButton[navigation=true] {background:transparent;border:0;border-bottom:2px solid transparent;border-radius:0;padding:0;min-height:0;color:@muted;}
QPushButton[navigation=true]:checked {color:@text;border-bottom-color:@accent;}
QPushButton[iconOnly=true], QToolButton[iconOnly=true] {background:transparent;border:0;padding:0;min-height:0;}
QPushButton[iconOnly=true]:hover, QToolButton[iconOnly=true]:hover {background:@raised;}
QPushButton[iconOnly=true]:focus, QToolButton[iconOnly=true]:focus {background:@selection;}
QPushButton[rail=true] {background:transparent;border:0;border-radius:7px;padding:0;min-height:36px;max-height:36px;min-width:36px;max-width:36px;}
QPushButton[rail=true]:hover {background:@raised;} QPushButton[rail=true]:checked {background:@accentBg;}
QPushButton[textAction=true] {background:transparent;border:0;color:@muted;padding:2px 4px;min-height:0;font-size:10px;}
QPushButton[textAction=true]:hover {color:@accent;} QToolButton::menu-indicator {image:none;}
QPushButton[quickChip=true] {background:@raised;border:1px solid @line;border-radius:3px;padding:2px 7px;min-height:14px;font-size:10px;color:@muted;}
QPushButton[quickChip=true]:hover {color:@accent;border-color:@accent;}
QToolButton[quickMenu=true] {background:transparent;border:0;padding:2px 4px;min-height:14px;font-size:10px;color:@muted;}
#modeSwitch {background:@input;border:1px solid @line;border-radius:5px;}
QPushButton[segment=true] {background:transparent;border:0;min-height:0;padding:5px 9px;font-size:10px;color:@muted;}
QPushButton[segment=true]:checked {background:@raised;color:@text;}
#sessionTitle {font-size:24px;font-weight:600;} #sessionEyebrow {font-size:9px;color:@dim;}
#connectionState {font-size:10px;border:1px solid @line;border-radius:3px;padding:1px 6px;color:@muted;}
#connectionState[connected=true] {color:@accent;border-color:@accent;} #actualEndpoint {font-size:10px;color:@muted;}
#metricsStrip {border-top:1px solid @line;border-bottom:1px solid @line;} #metricDivider {background:@line;}
#rxRate, #packetRate {font-family:'Consolas';font-size:29px;font-weight:600;} #rxRate {color:@accent;} #sequenceMetric, #recordingMetric {font-size:21px;}
#queueHealth {border-left:1px solid @line;} #queueHealthTitle {font-size:11px;}
#queueFill {border:0;background:@line;border-radius:1px;} #queueFill::chunk {background:@accent;}
#dataToolbar {border-top:1px solid @line;border-bottom:1px solid @line;}
QPushButton[viewTab=true] {background:transparent;border:0;border-bottom:2px solid transparent;border-radius:0;padding:0 6px;font-size:11px;color:@muted;min-height:0;}
QPushButton[viewTab=true]:checked {color:@text;border-bottom-color:@accent;}
#displayNotice {background:@input;border-bottom:1px solid @soft;color:@muted;font-size:10px;padding:7px 22px;}
#runtimeBanner {background:@accentBg;color:@error;padding:8px 22px;border-bottom:1px solid @line;}
QLineEdit, QComboBox, QSpinBox {background:@input;border:1px solid @line;border-radius:4px;padding:3px 9px;min-height:22px;}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus {border-color:@accent;} QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled {color:@dim;}
QComboBox, QSpinBox {padding-right:3px;} QComboBox::drop-down {subcontrol-origin:border;subcontrol-position:top right;width:20px;border:0;background:transparent;}
QComboBox::down-arrow {image:url("@down");width:10px;height:10px;} QComboBox QAbstractItemView {background:@surface;selection-background-color:@selection;color:@text;border:1px solid @line;outline:0;}
QSpinBox::up-button {subcontrol-origin:border;subcontrol-position:top right;width:13px;border:0;background:transparent;}
QSpinBox::down-button {subcontrol-origin:border;subcontrol-position:bottom right;width:13px;border:0;background:transparent;}
QSpinBox::up-arrow {image:url("@up");width:8px;height:8px;} QSpinBox::down-arrow {image:url("@down");width:8px;height:8px;}
QPlainTextEdit {background:@input;border:1px solid @line;border-radius:4px;padding:7px 10px;selection-background-color:@selection;}
QPlainTextEdit:focus {border-color:@accent;} QListWidget, QTableView {background:@surface;border:0;outline:0;selection-background-color:@selection;selection-color:@text;}
#profileList {background:@side;} QListWidget::item {padding:12px;border-bottom:1px solid @line;} #profileList::item {padding:0;border:0;}
QListWidget::item:selected {background:@selection;} QHeaderView::section {background:@surface;color:@muted;border:0;border-bottom:1px solid @line;font-size:10px;padding:5px 10px;}
QTabWidget::pane {border:0;} QTabBar::tab {background:@side;color:@muted;padding:8px 13px;border-bottom:2px solid transparent;font-size:10px;}
QTabBar::tab:selected {color:@text;border-bottom-color:@accent;} #inspectorTabs {background:@side;border-left:1px solid @line;}
#inspectorTabs QScrollArea, #inspectorTabs QScrollArea > QWidget, #bytePanel, #capturePanel {background:@side;} #byteInspector, #byteOffsets, #byteAscii, #byteUtf8 {border:0;background:@side;padding:0;}
#byteRange {font-size:10px;color:@muted;border-bottom:1px solid @line;padding-bottom:9px;}
#recordCapacityEstimate {background:@accentBg;color:@accent;padding:12px;border:1px solid @line;border-radius:4px;font-size:12px;font-weight:600;}
#recordFooter {border-top:1px solid @line;} #retainedSamples, #recordFooter QCheckBox {font-size:10px;color:@dim;}
#composer {border-top:1px solid @line;} #sendEditor {background:@input;border:1px solid @line;border-radius:4px;}
#sendInput {border:0;background:transparent;padding:8px 10px;} #sendValidation {border-top:1px solid @soft;font-size:9px;color:@dim;padding:3px 10px;}
#sendValidation[error=true] {color:@error;} #sendSettings QComboBox {min-height:18px;font-size:10px;}
#periodicInterval, #periodicCount {font-size:10px;min-height:18px;padding-left:4px;padding-right:2px;} #periodicEnabled {font-size:10px;spacing:4px;}
#sendButton {font-size:10px;min-height:18px;padding:4px 8px;} #quickCommands, #sendHistory {font-size:10px;min-height:12px;max-height:22px;padding:2px 7px;}
#advancedSettings {background:transparent;border:0;border-top:1px solid @line;border-radius:0;padding:12px 0;color:@muted;font-size:10px;text-align:left;}
QCheckBox {spacing:5px;} QCheckBox::indicator {width:12px;height:12px;border:1px solid @dim;border-radius:2px;background:transparent;}
QCheckBox::indicator:checked {background:@accent;border-color:@accent;image:url("@check");} QCheckBox::indicator:disabled {border-color:@line;}
QScrollArea {border:0;background:@surface;} QScrollBar:vertical {background:transparent;width:6px;margin:0;}
QScrollBar::handle:vertical {background:@line;border-radius:3px;min-height:24px;} QScrollBar::handle:vertical:hover {background:@dim;}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {height:0;} QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {background:transparent;}
QScrollBar:horizontal {background:transparent;height:6px;margin:0;} QScrollBar::handle:horizontal {background:@line;border-radius:3px;min-width:24px;}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {width:0;} QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {background:transparent;}
QSplitter::handle {background:@line;} #workspaceSplitter::handle:vertical {background:@soft;border-top:1px solid @line;border-bottom:1px solid @line;} #workspaceSplitter::handle:vertical:hover {background:@accentBg;} QStatusBar {background:@side;border-top:1px solid @line;padding:0 15px;} QStatusBar::item {border:0;}
#statusSummary {font-size:10px;color:@muted;} QMenu {background:@surface;border:1px solid @line;padding:5px;}
QMenu::item {padding:7px 20px;} QMenu::item:selected {background:@selection;} QToolTip {background:@raised;color:@text;border:1px solid @line;padding:5px;}
)CSS");
    const QList<QPair<QString,QString>> tokens={{"@primaryText",dark?"#142721":"#ffffff"},{"@accentBg",accentBg},{"@selection",selection},{"@surface",surface},{"@raised",raised},{"@accent",accent},{"@muted",muted},{"@input",input},{"@error",error},{"@text",text},{"@side",side},{"@soft",soft},{"@line",line},{"@dim",dim},{"@bg",bg},{"@down",assets.file(design::Icon::Down,QColor(muted),dark)},{"@up",assets.file(design::Icon::Up,QColor(muted),dark)},{"@check",assets.file(design::Icon::Check,QColor(dark?"#142721":"#ffffff"),dark)}};
    for(const auto& token:tokens) css.replace(token.first,token.second);
    q->setStyleSheet(css);
    title->setFont(design::font(24,false,true));q->findChild<QLabel*>("brandLabel")->setFont(design::font(18,false,true));eyebrow->setFont(design::font(9,true));endpoint->setFont(design::font(10,true));lossMetric->setFont(design::font(21));recordMetric->setFont(design::font(21));
    q->findChild<QLabel*>("brandMark")->setPixmap(design::icon(design::Icon::Brand,QColor(accent),30).pixmap(30,30));q->findChild<QLabel*>("interfaceIcon")->setPixmap(design::icon(design::Icon::Network,QColor(muted)).pixmap(18,18));q->findChild<QLabel*>("composerIcon")->setPixmap(design::icon(design::Icon::Terminal,QColor(muted),14).pixmap(14,14));
    const QPair<const char*,design::Icon> icons[]={{"addProfile",design::Icon::Plus},{"editProfile",design::Icon::Edit},{"removeProfile",design::Icon::Trash},{"saveProfiles",design::Icon::Save},{"importProfiles",design::Icon::Import},{"exportProfiles",design::Icon::Export},{"clearDisplay",design::Icon::Trash},{"resetStatistics",design::Icon::Reset},{"copyRecord",design::Icon::Copy},{"copyInspectorBytes",design::Icon::Copy}};
    for(const auto& entry:icons){auto* b=q->findChild<QPushButton*>(entry.first);if(!b->text().isEmpty())b->setToolTip(b->text());b->setText({});b->setIcon(design::icon(entry.second,QColor(muted),16));}
    themeButton->setToolTip(dark?QStringLiteral("切换浅色主题"):QStringLiteral("切换深色主题"));themeButton->setAccessibleName(themeButton->toolTip());themeButton->setText({});themeButton->setIcon(design::icon(dark?design::Icon::Sun:design::Icon::Moon,QColor(muted)));
    auto* profileMore=q->findChild<QToolButton*>("profileMoreMenu");profileMore->setText({});profileMore->setIcon(design::icon(design::Icon::More,QColor(muted),18));
    q->findChild<QToolButton*>("operationsMenu")->setIcon(design::icon(design::Icon::Menu,QColor(muted)));
    q->findChild<QToolButton*>("advancedSettings")->setIcon(design::icon(q->findChild<QToolButton*>("advancedSettings")->isChecked()?design::Icon::Down:design::Icon::Right,QColor(muted),12));
    sendButton->setIcon(design::icon(c->periodicActive()?design::Icon::Stop:design::Icon::Send,QColor(dark?"#142721":"#ffffff"),14));recordButton->setIcon(design::icon(c->recording()?design::Icon::Stop:design::Icon::Record,QColor(dark?"#142721":"#ffffff"),14));connectButton->setIcon(design::icon(design::Icon::Plug,QColor(dark?"#142721":"#ffffff"),15));
    for(int i=0;i<4;++i)railNavigation[i]->setIcon(design::icon(i==0?design::Icon::Workspace:i==1?design::Icon::Folder:i==2?design::Icon::Terminal:design::Icon::Workflow,QColor(railNavigation[i]->isChecked()?accent:muted),20));
    profilesPopup->setProperty("darkTheme",dark);profilesPopup->setPalette(q->palette());profilePicker->update();
    if(httpPage)httpPage->setDarkTheme(dark);
    if(webSocketPage)webSocketPage->setDarkTheme(dark);
    if(workflowPage)workflowPage->setDarkTheme(dark);
    trend->theme(dark);profileList->viewport()->update();table->viewport()->update();recolorText();inspect();settings.setValue("ui/dark",dark);
}
void MainWindow::Impl::applyConfig(const ConnectionConfig& cfg) {
    loading=true;localAddress->setCurrentText(fromStd(cfg.localAddress));localPort->setValue(cfg.localPort);remoteAddress->setText(fromStd(cfg.remoteAddress));remotePort->setValue(cfg.remotePort);serialPort->setCurrentText(fromStd(cfg.serialPort));baud->setCurrentText(QString::number(cfg.baudRate));dataBits->setCurrentText(QString::number(cfg.dataBits));parity->setCurrentText(fromStd(cfg.parity));stopBits->setCurrentText(fromStd(cfg.stopBits));flow->setCurrentText(fromStd(cfg.flowControl));timeout->setValue(cfg.connectTimeoutMs);rxBuffer->setValue(int(cfg.receiveBufferBytes));txBuffer->setValue(int(cfg.sendBufferBytes));sendQueue->setValue(int(cfg.sendQueueBytes));sequence->setChecked(cfg.sequenceAnalysis);seqOffset->setValue(int(cfg.sequenceOffset));seqBigEndian->setChecked(cfg.sequenceBigEndian);seqWindow->setValue(int(cfg.sequenceWindow));loading=false;title->setText(fromStd(cfg.name));connectionError->clear();updateState();
}
bool MainWindow::Impl::readConfig(ConnectionConfig* cfg,QString* error) const {
    *cfg=profiles[profileIndex];cfg->localAddress=toStd(localAddress->currentText().trimmed());cfg->localPort=std::uint16_t(localPort->value());cfg->remoteAddress=toStd(remoteAddress->text().trimmed());cfg->remotePort=std::uint16_t(remotePort->value());cfg->serialPort=toStd(serialPort->currentText().trimmed());
    bool validBaud=false;cfg->baudRate=baud->currentText().toInt(&validBaud);cfg->dataBits=dataBits->currentText().toInt();cfg->parity=toStd(parity->currentText());cfg->stopBits=toStd(stopBits->currentText());cfg->flowControl=toStd(flow->currentText());cfg->connectTimeoutMs=timeout->value();cfg->receiveBufferBytes=std::size_t(rxBuffer->value());cfg->sendBufferBytes=std::size_t(txBuffer->value());cfg->sendQueueBytes=std::size_t(sendQueue->value());cfg->sequenceAnalysis=sequence->isChecked();cfg->sequenceOffset=std::size_t(seqOffset->value());cfg->sequenceBigEndian=seqBigEndian->isChecked();cfg->sequenceWindow=std::size_t(seqWindow->value());
    cfg->sequenceAnalysis = cfg->kind == TransportKind::Udp && sequence->isChecked();
    if(cfg->kind==TransportKind::Serial){if(cfg->serialPort.empty()){*error=QStringLiteral("请输入或选择真实串口设备。");return false;}if(!validBaud||cfg->baudRate<=0||cfg->baudRate>4000000){*error=QStringLiteral("波特率必须是 1–4,000,000 范围内的整数。");return false;}}
    else {const QHostAddress local(localAddress->currentText().trimmed());if(local.protocol()!=QAbstractSocket::IPv4Protocol){*error=QStringLiteral("本地绑定地址必须是有效 IPv4 地址。");return false;}if((cfg->kind==TransportKind::TcpClient||cfg->kind==TransportKind::Udp)&&(cfg->remoteAddress.empty()||remoteAddress->text().contains(QChar(' ')))){*error=QStringLiteral("请填写有效目标 IP 或域名。");return false;}}
    if((cfg->kind==TransportKind::TcpClient||cfg->kind==TransportKind::Udp)&&!cfg->remotePort){*error=QStringLiteral("目标端口必须是 1–65,535；0 仅可保留在串口/服务端方案的非活动目标字段。");return false;}
    return true;
}
void MainWindow::Impl::refreshProfiles() {
    const QSignalBlocker block(profileList);if(profileList->count()!=profiles.size())profileList->clear();
    for(int i=0;i<profiles.size();++i){const auto& p=profiles[i];const QString detail=p.kind==TransportKind::Serial?QStringLiteral("%1 · %2 baud").arg(p.serialPort.empty()?QStringLiteral("端口未选择"):fromStd(p.serialPort)).arg(p.baudRate):transportText(p.kind)+QStringLiteral(" · ")+endpointText({p.kind==TransportKind::TcpClient?p.remoteAddress:p.localAddress,p.kind==TransportKind::TcpClient?p.remotePort:p.localPort});auto* item=i<profileList->count()?profileList->item(i):new QListWidgetItem(profileList);item->setText(fromStd(p.name)+'\n'+detail);item->setData(Qt::UserRole,int(p.kind));item->setToolTip(fromStd(p.name)+'\n'+detail+(p.kind==TransportKind::Udp?QStringLiteral("\n发送目标 ")+endpointText({p.remoteAddress,p.remotePort}):QString()));}
    profileCount->setText(QString::number(profiles.size()));profileCount->setToolTip(QStringLiteral("已保存 %1 个方案；点击当前方案搜索和切换。").arg(profiles.size()));profileList->setCurrentRow(std::clamp(profileIndex,0,int(profiles.size())-1));filterProfiles();updateProfilePicker();
}
void MainWindow::Impl::restoreParked() {
    if(!parkedController)return;
    c->stop();c=parkedController;parkedController=nullptr;parkedProfile=-1;
    auto* discarded=model;model=parkedModel;parkedModel=nullptr;proxy->setSourceModel(model);delete discarded;
    streamView->clear();QTextCursor cursor(streamView->document());cursor.insertFragment(QTextDocumentFragment(parkedDocument));delete parkedDocument;parkedDocument=nullptr;
    lastDisplaySequence=parkedOrdinal;streamOmittedCharacters=parkedOmitted;
    sendFormat->setCurrentIndex(parkedComposer.hex?0:1);encoding->setCurrentText(parkedComposer.encoding);selectEol(eol,parkedComposer.eol);sendInput->setPlainText(parkedComposer.input);periodic->setChecked(parkedComposer.periodic);interval->setValue(parkedComposer.intervalMs);count->setValue(parkedComposer.count);
    recordDirectory->setText(parkedRecordDirectory);rotation->setValue(parkedRotation);duration->setValue(parkedDuration);recordQueue->setValue(parkedRecordQueue);
    banner->setText(parkedError);banner->setVisible(!parkedError.isEmpty());model->setText(displayFormat->currentIndex()==1);if(parkedTextFormat!=textFormat->currentIndex())renderTextPreview();recolorText();
}
void MainWindow::Impl::activateProfile(int index,bool preserve) {
    if(index<0||index>=profiles.size())return;
    if(preserve&&index==profileIndex&&(c->connected()||c->connecting()))return;
    if(preserve&&parkedController&&index==parkedProfile){
        restoreParked();profileIndex=index;trend->clear();refreshProfiles();applyConfig(profiles[index]);inspect();snapshot();settings.setValue("ui/profile",index);return;
    }
    if(preserve&&!parkedController&&(c->connected()||c->connecting())){
        parkedController=c;parkedProfile=profileIndex;parkedModel=model;parkedDocument=streamView->document()->clone(this);parkedOrdinal=lastDisplaySequence;parkedOmitted=streamOmittedCharacters;parkedTextFormat=textFormat->currentIndex();parkedError=banner->isVisible()?banner->text():QString();
        parkedComposer.input=sendInput->toPlainText();parkedComposer.hex=sendFormat->currentIndex()==0;parkedComposer.encoding=encoding->currentText();parkedComposer.eol=eol->currentData().toString();parkedComposer.periodic=periodic->isChecked();parkedComposer.intervalMs=interval->value();parkedComposer.count=count->value();
        parkedRecordDirectory=recordDirectory->text();parkedRotation=rotation->value();parkedDuration=duration->value();parkedRecordQueue=recordQueue->value();
        if(!browseController){browseController=new SessionController(this);connectController(browseController);}
        c=browseController;model=new RecordModel(q);model->setText(displayFormat->currentIndex()==1);model->setStreamFormat(textFormat->currentIndex());proxy->setSourceModel(model);
    }else if(!preserve&&parkedController)restoreParked();
    profileIndex=index;
    c->selectConfiguration(profiles[index]);
    model->clear();streamView->clear();lastDisplaySequence=0;streamOmittedCharacters=0;
    trend->clear();ticks=0;banner->clear();banner->hide();connectionError->clear();q->statusBar()->clearMessage();
    refreshProfiles();applyConfig(profiles[index]);inspect();snapshot();settings.setValue("ui/profile",index);
}
void MainWindow::Impl::selectProfile(int index) {
    if(index<0||index>=profiles.size()||loading)return;
    ConnectionConfig cfg;QString error;if(profileIndex>=0&&profileIndex<profiles.size()&&readConfig(&cfg,&error))profiles[profileIndex]=cfg;
    activateProfile(index,true);
}
void MainWindow::Impl::editProfile(bool add) {
    if(!add&&protectWorkflowProfiles())return;
    const auto* httpStore=httpPage->projectStore();
    const auto httpContext=QStringLiteral("保存到项目：%1\n当前环境：%2（仅提供发送时的地址和变量）").arg(httpStore->project().value("name").toString(),httpStore->environment().value("name").toString());
    design::ConnectionDialog dialog(q,add,add?(workspaceMode==1?4:workspaceMode==2?5:3):int(profiles[profileIndex].kind),add?QString():fromStd(profiles[profileIndex].name),dark,httpContext);
    auto* name=dialog.name;auto* kind=dialog.kind;
    QObject::connect(dialog.buttons,&QDialogButtonBox::accepted,&dialog,[&]{
        if(name->text().trimmed().isEmpty()){dialog.showError(kind->currentIndex()==4?QStringLiteral("请填写请求名称，例如用户登录。"):kind->currentIndex()==5?QStringLiteral("请填写连接名称。"):QStringLiteral("请填写方案名称。"));name->setFocus();return;}
        if(kind->currentIndex()>=4){
            auto* page=kind->currentIndex()==4?httpPage:webSocketPage;QString error;
            if(!page->createSavedRequest(name->text(),kind->currentIndex()==4?QString():dialog.url->text(),&error)){dialog.showError(error);return;}
        }
        dialog.accept();
    });
    if(dialog.exec()!=QDialog::Accepted)return;
    if(kind->currentIndex()>=4){workspaceModeButtons[kind->currentIndex()==4?1:2]->click();(kind->currentIndex()==4?httpPage:webSocketPage)->focusUrl();return;}
    // Read before creating another profile: valid unsaved edits belong to the old profile.
    ConnectionConfig prior;QString priorError;if(readConfig(&prior,&priorError))profiles[profileIndex]=prior;
    ConnectionConfig cfg=add?ConnectionConfig():profiles[profileIndex];if(add)cfg.localAddress="127.0.0.1";
    cfg.name=toStd(name->text().trimmed());cfg.kind=TransportKind(kind->currentIndex());if(cfg.kind!=TransportKind::Udp)cfg.sequenceAnalysis=false;
    const int selected=add?int(profiles.size()):profileIndex;if(add)profiles.push_back(cfg);else profiles[selected]=cfg;
    activateProfile(selected,add||parkedController!=nullptr);persistProfiles();if(add)workspaceModeButtons[0]->click();
}
void MainWindow::Impl::persistProfiles() {QString error;if(!saveProfiles(profiles,&error))showError(error);else q->statusBar()->showMessage(QStringLiteral("连接方案已保存；下次启动不会自动连接。"),5000);}
void MainWindow::Impl::refreshCommands() {
    const int selected=commandList->currentRow();commandList->clear();quickBox->clear();
    while(auto* item=quickChipLayout->takeAt(0)){delete item->widget();delete item;}
    for(int i=0;i<commands.size();++i){const auto cmd=commands[i];quickBox->addItem(cmd.name);
        const auto schedule=cmd.periodic?QStringLiteral("周期 %1 ms · %2").arg(cmd.intervalMs).arg(cmd.count?QStringLiteral("%1 次").arg(cmd.count):QStringLiteral("持续")):QStringLiteral("手动发送");
        const auto detail=QStringLiteral("%1 · 换行 %2 · %3").arg(cmd.hex?QStringLiteral("HEX"):cmd.encoding,eolText(cmd.eol),schedule);
        auto* item=new QListWidgetItem(cmd.name+'\n'+detail+'\n'+cmd.input.left(160),commandList);item->setSizeHint({300,92});item->setData(Qt::UserRole+10,true);
        auto* row=new QWidget;auto* layout=new QHBoxLayout(row);layout->setContentsMargins(16,12,16,12);layout->setSpacing(14);
        auto* badge=label(cmd.hex?QStringLiteral("HEX"):QStringLiteral("TEXT"));badge->setFont(design::font(10,true));badge->setProperty("muted",true);badge->setFixedWidth(40);layout->addWidget(badge);
        auto* text=new QVBoxLayout;text->setSpacing(4);auto* name=label(cmd.name);name->setFont(design::font(13,false,true));name->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);text->addWidget(name);auto* preview=label(cmd.input.left(100).replace('\n',' '));preview->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);preview->setFont(design::font(11,true));text->addWidget(preview);auto* meta=label(detail);meta->setProperty("small",true);meta->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);text->addWidget(meta);layout->addLayout(text,1);
        auto* load=button(QStringLiteral("载入"),("commandRowLoad"+QByteArray::number(i)).constData());auto* edit=button(QStringLiteral("编辑"),("commandRowEdit"+QByteArray::number(i)).constData());auto* exp=button(QStringLiteral("导出"),("commandRowExport"+QByteArray::number(i)).constData());auto* remove=button(QStringLiteral("删除"),("commandRowDelete"+QByteArray::number(i)).constData());for(auto* b:{load,edit,exp,remove}){b->setProperty("textAction",true);layout->addWidget(b);}
        QObject::connect(load,&QPushButton::clicked,q,[this,cmd]{loadCommand(cmd);navigate(0);});QObject::connect(edit,&QPushButton::clicked,q,[this,i]{commandList->setCurrentRow(i);editCommand(false);});QObject::connect(remove,&QPushButton::clicked,q,[this,i]{commands.removeAt(i);refreshCommands();persistCommands();});
        QObject::connect(exp,&QPushButton::clicked,q,[this,cmd]{const auto output=QFileDialog::getSaveFileName(q,QStringLiteral("导出命令"),"command.json","JSON (*.json)");if(output.isEmpty())return;QString error;if(!exportCommands(output,{cmd},&error))showError(error);});commandList->setItemWidget(item,row);
        if(i<3){auto* chip=button(cmd.name,("quickCommandChip"+QByteArray::number(i)).constData());chip->setText(chip->fontMetrics().elidedText(cmd.name,Qt::ElideRight,110));chip->setToolTip(cmd.name+QStringLiteral("；仅载入，不发送"));chip->setProperty("quickChip",true);quickChipLayout->addWidget(chip);QObject::connect(chip,&QPushButton::clicked,q,[this,cmd]{loadCommand(cmd);});}
    }
    if(commands.isEmpty()){auto* empty=label(QStringLiteral("尚未保存命令"));empty->setProperty("small",true);quickChipLayout->addWidget(empty);}quickChipLayout->addStretch();
    if(!commands.isEmpty())commandList->setCurrentRow(std::clamp(selected,0,int(commands.size())-1));
}
void MainWindow::Impl::editCommand(bool add) {
    const int i=commandList->currentRow();if(!add&&(i<0||i>=commands.size()))return;Command cmd;if(!add)cmd=commands[i];
    QDialog dialog(q);dialog.setObjectName("commandDialog");dialog.setWindowTitle(add?QStringLiteral("添加命令"):QStringLiteral("编辑命令"));dialog.resize(520,500);auto* l=new QVBoxLayout(&dialog);auto* f=new QFormLayout;f->setRowWrapPolicy(QFormLayout::WrapAllRows);f->setVerticalSpacing(7);
    auto* name=named(new QLineEdit(cmd.name),"commandName");name->setPlaceholderText(QStringLiteral("例如：读取设备状态"));auto* format=combo({"HEX",QStringLiteral("文本")},"commandFormat");format->setCurrentIndex(cmd.hex?0:1);auto* enc=combo({"UTF-8","ASCII"},"commandEncoding");enc->setCurrentText(cmd.encoding);auto* end=eolCombo("commandEol");selectEol(end,cmd.eol);buddy(f,QStringLiteral("名称"),name);buddy(f,QStringLiteral("格式"),format);buddy(f,QStringLiteral("编码"),enc);buddy(f,QStringLiteral("附加换行"),end);l->addLayout(f);
    auto updateEncoding=[f,enc,format]{enc->setVisible(format->currentIndex()==1);if(auto* caption=f->labelForField(enc))caption->setVisible(format->currentIndex()==1);};QObject::connect(format,&QComboBox::currentIndexChanged,&dialog,updateEncoding);updateEncoding();
    auto* input=named(new QPlainTextEdit(cmd.input),"commandInput");input->setAccessibleName(QStringLiteral("命令内容"));input->setFont(design::font(12,true));input->setPlaceholderText(QStringLiteral("HEX：AA 55 01；文本：输入待发送原文"));input->setMaximumHeight(120);auto* contentCaption=label(QStringLiteral("内容"));contentCaption->setBuddy(input);l->addWidget(contentCaption);l->addWidget(input);
    auto* cycle=named(new QCheckBox(QStringLiteral("周期设置（仅配置，载入后主动发送）")),"commandPeriodic");cycle->setChecked(cmd.periodic);l->addWidget(cycle);auto* schedule=rowLayout();auto* intervalValue=spin(1,86400000,cmd.intervalMs,"commandInterval");intervalValue->setSuffix(" ms");auto* countValue=spin(0,1000000,cmd.count,"commandCount");countValue->setSpecialValueText(QStringLiteral("持续"));countValue->setSuffix(QStringLiteral(" 次"));for(const auto& entry:{qMakePair(QStringLiteral("间隔"),intervalValue),qMakePair(QStringLiteral("次数 · 0 为持续"),countValue)}){auto* col=new QVBoxLayout;auto* caption=label(entry.first);caption->setBuddy(entry.second);col->addWidget(caption);col->addWidget(entry.second);schedule->addLayout(col,1);}l->addLayout(schedule);
    l->addStretch();auto* error=label({},"commandDialogError");error->setProperty("error",true);error->setWordWrap(true);l->addWidget(error);auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);chineseButtons(buttons,QStringLiteral("保存命令"));l->addWidget(buttons);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{QString e;if(input->toPlainText().size()>1024*1024)e=QStringLiteral("命令内容超过 1 Mi 字符上限。");else encodePayload(input->toPlainText(),format->currentIndex()==0,enc->currentText(),end->currentData().toString(),&e);if(name->text().trimmed().isEmpty())e=QStringLiteral("命令名称不能为空。");if(!e.isEmpty()){error->setText(e);return;}dialog.accept();});if(dialog.exec()!=QDialog::Accepted)return;
    cmd.name=name->text().trimmed();cmd.input=input->toPlainText();cmd.hex=format->currentIndex()==0;cmd.encoding=enc->currentText();cmd.eol=end->currentData().toString();cmd.periodic=cycle->isChecked();cmd.intervalMs=intervalValue->value();cmd.count=countValue->value();if(add)commands.push_back(cmd);else commands[i]=cmd;refreshCommands();persistCommands();
}
void MainWindow::Impl::persistCommands(){QString error;if(!saveCommands(commands,&error))showError(error);}
void MainWindow::Impl::loadCommand(const Command& cmd){if(c->periodicActive()){showError(QStringLiteral("请先停止周期发送，再修改命令。"));return;}sendFormat->setCurrentIndex(cmd.hex?0:1);encoding->setCurrentText(cmd.encoding);selectEol(eol,cmd.eol);sendInput->setPlainText(cmd.input);periodic->setChecked(cmd.periodic);interval->setValue(cmd.intervalMs);count->setValue(cmd.count);validateSend();}
void MainWindow::Impl::validateSend() {
    QString error;
    const auto input = sendInput->toPlainText();
    if (input.size() > 1024 * 1024) { error=QStringLiteral("发送内容超过 1 Mi 字符上限，请缩短内容。"); payload.clear(); }
    else payload=encodePayload(input,sendFormat->currentIndex()==0,encoding->currentText(),eol->currentData().toString(),&error);
    payloadValid=error.isEmpty();encoding->setVisible(sendFormat->currentIndex()!=0);encoding->setEnabled(sendFormat->currentIndex()!=0&&!c->periodicActive());
    validation->setText(error.isEmpty()?QStringLiteral("%1 字节 · 预览 %2%3 · Ctrl+Enter 发送 / 停止").arg(payload.size()).arg(hexBytes(payload.left(32))).arg(payload.size()>32?QStringLiteral(" …"):QString()):error);
    validation->setProperty("error",!payloadValid);validation->setToolTip(validation->text()+QStringLiteral("\n本地写出不等于对端确认。"));
    const bool server=c->config().kind==TransportKind::TcpServer;const bool target=!server||broadcast->isChecked()||clientTarget->currentData().toULongLong()!=0;
    const bool udp=profiles[profileIndex].kind==TransportKind::Udp;
    bool udpReady=true;
    if(udp){const auto host=remoteAddress->text().trimmed();udpReady=!host.isEmpty()&&host.size()<=253&&std::none_of(host.begin(),host.end(),[](QChar ch){return ch.isSpace();})&&remotePort->value()>0;
        udpTargetStatus->setText(!c->connected()?QStringLiteral("先绑定本地端口。"):
            !udpReady?QStringLiteral("填写目标 IP/域名和端口。"):
            QStringLiteral("点击发送即发往此目标；编辑不发送。"));
    }
    const bool leased=workflowPage&&workflowPage->runner()->active();
    sendButton->setEnabled(!leased&&(c->periodicActive()||(c->connected()&&payloadValid&&target&&udpReady&&(udp||!payload.isEmpty()))));
}
void MainWindow::Impl::send() {
    if(workspaceMode>0){if(pages->currentIndex()==0)(workspaceMode==1?httpPage:webSocketPage)->triggerSend();return;}
    if((httpPage&&httpPage->session()->active())||(webSocketPage&&webSocketPage->session()->active())){showError(QStringLiteral("协议调试仍有活动，请明确结束或启动新的通信会话后发送。"));return;}
    // The application shortcut also fires while a graph field has focus. An
    // idle workflow editor must never send the hidden workbench composer.
    if(pages->currentIndex()==3){showError(QStringLiteral("工作流页面不会触发工作台发送；请返回工作台明确发送。"));return;}
    if(workflowPage&&workflowPage->runner()->active()){showError(QStringLiteral("工作流运行中，请先停止流程再手动发送。"));return;}
    if(c->periodicActive()){c->stopPeriodic();updateState();return;}validateSend();if(!sendButton->isEnabled())return;
    std::optional<Endpoint> udpTarget;
    if(c->config().kind==TransportKind::Udp){udpTarget=Endpoint{toStd(remoteAddress->text().trimmed()),std::uint16_t(remotePort->value())};profiles[profileIndex].remoteAddress=udpTarget->address;profiles[profileIndex].remotePort=udpTarget->port;}
    const auto clientId=clientTarget->currentData().toULongLong();const bool all=broadcast->isChecked();const bool ok=periodic->isChecked()?c->startPeriodic(payload,interval->value(),count->value(),clientId,all,udpTarget):c->send(payload,clientId,all,udpTarget);
    if(!ok){showError(c->lastError().isEmpty()?QStringLiteral("发送请求未接受；检查目标与队列状态。"):c->lastError());return;}
    Command cmd;cmd.name=QDateTime::currentDateTime().toString("HH:mm:ss")+QStringLiteral(" · %1 B").arg(payload.size());cmd.input=sendInput->toPlainText();cmd.hex=sendFormat->currentIndex()==0;cmd.encoding=encoding->currentText();cmd.eol=eol->currentData().toString();cmd.periodic=periodic->isChecked();cmd.intervalMs=interval->value();cmd.count=count->value();history.prepend(cmd);
    auto historyBytes=[this]{qsizetype bytes=0;for(const auto& h:history)bytes+=h.input.size()*2+h.name.size()*2+128;return bytes;};
    while(history.size()>50 || historyBytes()>2*1024*1024)history.removeLast();
    historyBox->clear();for(const auto& h:history)historyBox->addItem(h.name);q->statusBar()->showMessage(QStringLiteral("请求已接受；实际 TX 以本地写出统计为准。"),3000);updateState();
}
void MainWindow::Impl::updateState() {
    if(!connectButton||!sendButton)return;
    const bool active=c->connected(),connecting=c->connecting(),running=c->periodicActive(),recording=c->recording();
    const QColor primaryInk(dark?"#142721":"#ffffff");sendButton->setIcon(design::icon(running?design::Icon::Stop:design::Icon::Send,primaryInk,14));recordButton->setIcon(design::icon(c->recording()?design::Icon::Stop:design::Icon::Record,primaryInk,14));
    const auto kind=profiles[profileIndex].kind;
    transportTag->setText(kind==TransportKind::Serial?QStringLiteral("SERIAL"):kind==TransportKind::TcpClient?QStringLiteral("TCP CLIENT"):kind==TransportKind::TcpServer?QStringLiteral("TCP SERVER"):QStringLiteral("UDP"));
    title->setMaximumWidth(std::min(600,title->fontMetrics().horizontalAdvance(title->text())+3));title->setToolTip(title->text());
    eyebrow->setText(kind==TransportKind::Serial?QStringLiteral("SERIAL / UART"):QStringLiteral("NETWORK / ")+transportText(kind).toUpper());
    ordinaryMode->setChecked(!c->highSpeed());fastMode->setChecked(c->highSpeed());
    interfaceInfo->setText(kind==TransportKind::Serial?QStringLiteral("串口设备\n%1").arg(serialPort->currentText().isEmpty()?QStringLiteral("未选择端口"):serialPort->currentText()):QStringLiteral("本地 IPv4\n%1 · 接口速率未知").arg(localAddress->currentText()));
    for(int i=0;i<profileList->count();++i)profileList->item(i)->setData(Qt::UserRole+1,(i==profileIndex&&active)||(i==parkedProfile&&parkedController&&(parkedController->connected()||parkedController->connecting())));
    updateProfilePicker();
    const bool sequenceApplicable=kind==TransportKind::Udp;
    sequence->setEnabled(sequenceApplicable);seqOffset->setEnabled(sequenceApplicable);seqWindow->setEnabled(sequenceApplicable);seqBigEndian->setEnabled(sequenceApplicable);
    if(!sequenceApplicable)sequence->setChecked(false);
    const bool client=kind==TransportKind::TcpClient,udp=kind==TransportKind::Udp;
    if(udp){if(udpTargetLayout->indexOf(remoteFields)<0){connectionLayout->removeWidget(remoteFields);udpTargetLayout->insertWidget(1,remoteFields,1);}}
    else if(connectionLayout->indexOf(remoteFields)!=(client?1:2)){udpTargetLayout->removeWidget(remoteFields);connectionLayout->removeWidget(remoteFields);connectionLayout->insertWidget(client?1:2,remoteFields);}
    for(QWidget* field:std::initializer_list<QWidget*>{remoteAddressCaption,remoteAddress,remotePortCaption,remotePort})remoteLayout->removeWidget(field);
    remoteLayout->addWidget(remoteAddressCaption,0,0);remoteLayout->addWidget(remoteAddress,udp?0:1,udp?1:0);
    remoteLayout->addWidget(remotePortCaption,udp?0:2,udp?2:0);remoteLayout->addWidget(remotePort,udp?0:3,udp?3:0);
    remoteLayout->setColumnStretch(0,udp?0:1);remoteLayout->setColumnStretch(1,udp?1:0);remotePort->setMaximumWidth(udp?100:QWIDGETSIZE_MAX);
    udpSendFields->setVisible(udp);
    q->findChild<QLabel*>("connectionConfigHeading")->setText(udp?QStringLiteral("本地接收绑定"):QStringLiteral("连接配置"));
    advancedForm->setRowVisible(timeout,client);
    for(QWidget* field:std::initializer_list<QWidget*>{sequence,seqOffset,seqBigEndian,seqWindow,q->findChild<QLabel*>("sequenceHelp")})advancedForm->setRowVisible(field,udp);
    int portRow=-1;QFormLayout::ItemRole portRole;localForm->getWidgetPosition(localPort,&portRow,&portRole);
    if(client&&portRow>=0){const auto taken=localForm->takeRow(localPort);delete taken.labelItem;delete taken.fieldItem;advancedForm->insertRow(0,localPortCaption,localPort);}
    else if(!client&&portRow<0){const auto taken=advancedForm->takeRow(localPort);delete taken.labelItem;delete taken.fieldItem;localForm->addRow(localPortCaption,localPort);}
    serialFields->setVisible(kind==TransportKind::Serial);localFields->setVisible(kind!=TransportKind::Serial);remoteFields->setVisible(client||kind==TransportKind::Udp);
    for(auto* field:{serialFields,localFields,remoteFields,advancedFields})field->setEnabled(!active&&!connecting);
    if(udp)remoteFields->setEnabled(!running&&!connecting);
    state->setText(connecting?(udp?QStringLiteral("绑定中 · 可取消"):QStringLiteral("连接中 · 可取消")):active?(c->config().kind==TransportKind::Udp?QStringLiteral("已绑定"):c->config().kind==TransportKind::TcpServer?QStringLiteral("监听中"):QStringLiteral("已连接")):(udp?QStringLiteral("未绑定"):QStringLiteral("未连接")));
    state->setProperty("connected",active);state->style()->unpolish(state);state->style()->polish(state);state->setToolTip(kind==TransportKind::Udp&&active?QStringLiteral("UDP 已绑定；对端在线状态未知"):state->text());
    if(active)endpoint->setText(QStringLiteral("实际本地端点：")+endpointText(c->localEndpoint()));else endpoint->setText(QStringLiteral("实际本地端点：未建立"));
    connectButton->setText(connecting?QStringLiteral("取消连接"):active?(kind==TransportKind::Udp?QStringLiteral("解除绑定"):kind==TransportKind::TcpServer?QStringLiteral("停止监听"):QStringLiteral("断开连接")):(kind==TransportKind::Serial?QStringLiteral("打开串口"):kind==TransportKind::TcpServer?QStringLiteral("开始监听"):kind==TransportKind::Udp?QStringLiteral("绑定端口"):QStringLiteral("连接设备")));
    const bool server=active&&c->config().kind==TransportKind::TcpServer;clientTarget->setVisible(server);broadcast->setVisible(server);disconnectClient->setVisible(server);
    const auto selected=clientTarget->currentData().toULongLong();const auto clients=c->clients();
    {const QSignalBlocker block(clientTarget);clientTarget->clear();clientTarget->addItem(QStringLiteral("选择客户端"),QVariant::fromValue(qulonglong(0)));for(const auto& client:clients)clientTarget->addItem(QStringLiteral("#%1 · %2").arg(client.id).arg(endpointText(client.peer)),QVariant::fromValue(qulonglong(client.id)));int index=clientTarget->findData(QVariant::fromValue(selected));clientTarget->setCurrentIndex(index<0?0:index);}
    updateTargetState();
    sendInput->setEnabled(!running);sendFormat->setEnabled(!running);eol->setEnabled(!running);periodicFields->setEnabled(!running);sendButton->setText(running?QStringLiteral("停止"):QStringLiteral("发送"));sendButton->setToolTip(running?QStringLiteral("停止周期发送 · 已发 %1 次").arg(c->periodicSent()):QStringLiteral("发送数据 · Ctrl+Enter"));
    recordButton->setEnabled(recording||active);recordButton->setText(recording?QStringLiteral("停止记录"):QStringLiteral("开始记录"));recordFields->setEnabled(!recording);
    {const QSignalBlocker a(high),b(paused);high->setChecked(c->highSpeed());paused->setChecked(c->displayPaused());}
    notice->setText(c->highSpeed()?QStringLiteral("●  抽样显示 · 原始接收独立运行；文本预览不能重建完整流"):udp?QStringLiteral("●  UDP 数据报 · 保留每包边界、来源地址和端口"):QStringLiteral("●  普通显示 · 原始字节保留；流式读取块"));
    notice->setToolTip(QStringLiteral("显示样本有界：最多10MiB / 50,000行；普通文本流最多1Mi字符 / 10,000段。高速只提供有限样本，显示省略不等于网络丢失。"));
    if(c->displayPaused())notice->setText(notice->text()+QStringLiteral("  当前显示冻结；接收、统计、记录继续。恢复后解码状态重置。"));
    q->findChild<QPushButton*>("dataViewTab1")->setText(udp?QStringLiteral("文本预览"):QStringLiteral("文本流"));
    streamView->setPlaceholderText(udp?QStringLiteral("数据报文本预览；每包来源及边界请查看数据样本。"):
        QStringLiteral("连接后显示 UTF-8 文本流；高速模式仅提供数据样本。"));
    validateSend();
}
void MainWindow::Impl::updateTargetState() {
    const bool server=c->connected()&&c->config().kind==TransportKind::TcpServer;
    const bool running=c->periodicActive();const auto selected=clientTarget->currentData().toULongLong();
    const auto clients=c->clients();const bool live=selected!=0&&std::any_of(clients.begin(),clients.end(),[selected](const ClientInfo& client){return client.id==selected;});
    clientTarget->setEnabled(!running&&!broadcast->isChecked());broadcast->setEnabled(!running);
    disconnectClient->setEnabled(server&&live&&!running);
    validateSend();
}
void MainWindow::Impl::appendTextSegments(std::deque<RecordModel::TextSegment> segments) {
    for(const auto& segment:segments){
        const auto previous=streamView->document()->characterCount();QTextCursor cursor(streamView->document());cursor.movePosition(QTextCursor::End);QTextCharFormat format;
        format.setForeground(textInk(segment.direction));format.setBackground(textFill(segment.direction));format.setFontWeight(QFont::Medium);
        format.setProperty(QTextFormat::UserProperty,int(segment.direction));cursor.insertText(segment.text,format);
        const auto trimmed=previous+segment.text.size()-streamView->document()->characterCount();if(trimmed>0)streamOmittedCharacters+=std::uint64_t(trimmed);
        if(streamView->document()->characterCount()>1024*1024){const int amount=streamView->document()->characterCount()-1024*1024;streamOmittedCharacters+=amount;QTextCursor trim(streamView->document());trim.setPosition(0);trim.setPosition(amount,QTextCursor::KeepAnchor);trim.removeSelectedText();trim.insertText(QStringLiteral("[文本显示达到上限，前部已裁剪]\n"));}
    }
    if(autoScroll->isChecked())streamView->moveCursor(QTextCursor::End);
}
void MainWindow::Impl::recolorText(){
    for(const auto& entry:{qMakePair("textRxLegend",Direction::Receive),qMakePair("textTxLegend",Direction::Transmit)})q->findChild<QLabel*>(entry.first)->setStyleSheet(QStringLiteral("color:%1;background:%2;padding:3px 7px;border-radius:3px;font-weight:600;").arg(textInk(entry.second).name(),textFill(entry.second).name()));
    struct Span{int start,length,direction;};std::vector<Span> spans;
    for(auto block=streamView->document()->begin();block.isValid();block=block.next())for(auto it=block.begin();!it.atEnd();++it){const auto fragment=it.fragment();if(fragment.isValid())spans.push_back({fragment.position(),fragment.length(),fragment.charFormat().property(QTextFormat::UserProperty).toInt()});}
    QTextCursor cursor(streamView->document());cursor.beginEditBlock();for(const auto& span:spans){cursor.setPosition(span.start);cursor.setPosition(span.start+span.length,QTextCursor::KeepAnchor);QTextCharFormat format;format.setForeground(textInk(Direction(span.direction)));format.setBackground(textFill(Direction(span.direction)));cursor.mergeCharFormat(format);}cursor.endEditBlock();
}
void MainWindow::Impl::renderTextPreview(){
    if(!model||!streamView)return;
    model->setStreamFormat(textFormat->currentIndex());streamView->clear();streamOmittedCharacters=0;
    if(!c->highSpeed())appendTextSegments(model->takeStreamSegments());else model->takeStreamSegments();
}
void MainWindow::Impl::snapshot(bool forceMetrics) {
    if(stateDirty){stateDirty=false;updateState();forceMetrics=true;}
    pollExport();pollSamples();pollStorage();pollDelete();validateSend();
    auto records=c->takeDisplayRecords();const auto stats=c->statistics();
    if(!c->displayPaused()){
        std::vector<DataRecord> contiguous;
        std::deque<RecordModel::TextSegment> text;
        auto flush=[&]{if(!contiguous.empty()){model->append(contiguous,c->highSpeed());auto segments=model->takeStreamSegments();for(auto& segment:segments)text.push_back(std::move(segment));contiguous.clear();}};
        // This lifetime ordinal advances for every admitted record, including
        // display-omitted RX/TX/SYSTEM records. resetStatistics does not reset it.
        // Reset decoders only at an actual discontinuity, at its batch boundary.
        for(const auto& record:records){
            if(lastDisplaySequence&&record.sequence!=lastDisplaySequence+1){flush();model->resetDecoders();if(!c->highSpeed())text.push_back({Direction::System,QStringLiteral("\n[显示记录不连续；跨块解码状态已重置]\n")});}
            contiguous.push_back(record);lastDisplaySequence=record.sequence;
        }
        flush();
        if(!text.empty()&&!c->highSpeed())appendTextSegments(std::move(text));
        if(!records.empty()&&autoScroll->isChecked())table->scrollToBottom();
    }
    // Keep sample consumption and job polling at 20 Hz. Counters/layouts need
    // only four refreshes per second; explicit user actions render immediately.
    const bool refreshMetrics=forceMetrics||!metricsClock.isValid()||metricsClock.elapsed()>=250;
    if(refreshMetrics){
    metricsClock.restart();
    auto number=[this](const QString& value,const QString& unit){return QStringLiteral("<span style='font-size:29px'>%1</span><span style='font-size:10px;color:%3'> %2</span>").arg(value,unit,dark?"#8b9a9f":"#5e7379");};
    rxMetric->setText(number(QString::number(stats.rxBytesPerSecond/1e6,'f',3),QStringLiteral("MB/s")));
    txRate->setText(QStringLiteral("TX %1 MB/s · 有效负载").arg(stats.txBytesPerSecond/1e6,0,'f',3));
    const auto shownKind=c->connected()?c->config().kind:profiles[profileIndex].kind;
    q->findChild<QLabel*>("packetRateCaption")->setText(shownKind==TransportKind::Udp?QStringLiteral("UDP 数据报率"):shownKind==TransportKind::TcpServer?QStringLiteral("TCP 客户端数"):QStringLiteral("累计接收字节"));
    ppsMetric->setText(shownKind==TransportKind::Udp?number(QString::number(stats.datagramsPerSecond/1000,'f',1),QStringLiteral("kpps")):shownKind==TransportKind::TcpServer?number(QString::number(c->clients().size()),QStringLiteral("客户端")):number(bytesLabel(stats.rxBytes).section(' ',0,0),bytesLabel(stats.rxBytes).section(' ',1)));
    metricTags[0]->setText(c->highSpeed()?QStringLiteral("HIGH SPEED"):QStringLiteral("LIVE"));metricTags[1]->setText(shownKind==TransportKind::Udp?QStringLiteral("UDP"):shownKind==TransportKind::TcpServer?QStringLiteral("CLIENTS"):QStringLiteral("BYTES"));metricTags[2]->setText(QStringLiteral("SEQ CHECK"));metricTags[3]->setText(QStringLiteral("BINARY"));
    metricNotes[1]->setText(shownKind==TransportKind::Udp?QStringLiteral("实际接收 · %1 数据报").arg(stats.rxDatagrams):shownKind==TransportKind::TcpServer?QStringLiteral("独立来源 · 定向 / 广播"):QStringLiteral("应用实际收到的字节"));
    const auto missingRatio=stats.sequenceExpected?QStringLiteral("%1%").arg(double(stats.sequenceMissing)*100/double(stats.sequenceExpected),0,'f',4):QStringLiteral("未知（无已确认位置）");
    metricNotes[2]->setText(stats.sequenceEnabled?QStringLiteral("缺失率 %1 · 乱序 %2").arg(missingRatio).arg(stats.sequenceReordered):QStringLiteral("未配置协议序号"));
    lossMetric->setToolTip(QStringLiteral("缺失率 = 已确认缺失 / 已确认序号位置；分母 %1，待观察窗口、重复观察不计入分母。").arg(stats.sequenceExpected));
    lossMetric->setText(stats.sequenceEnabled?QString::number(stats.sequenceMissing):QStringLiteral("未知 / 未启用"));
    recordMetric->setText(c->recording()?QStringLiteral("记录中"):QStringLiteral("未开启"));
    queueStatus->setText(QStringLiteral("记录队列  %1 / %2").arg(bytesLabel(stats.recordingQueueBytes),bytesLabel(double(recordQueue->value())*1024*1024)));
    queueMeter->setValue(int(std::min(1000.0,double(stats.recordingQueueBytes)*1000/(double(recordQueue->value())*1024*1024))));
    healthTitle->setText(stats.applicationDroppedRecords||stats.recordingFailures?QStringLiteral("●  接收管线异常"):c->connected()?QStringLiteral("●  接收管线运行中"):QStringLiteral("●  接收管线待命"));
    healthDetail->setText(QStringLiteral("显示队列 %1 · 待发送 %2").arg(bytesLabel(stats.sampleQueueBytes),bytesLabel(stats.pendingSendBytes)));
    queueStatus->setToolTip(QStringLiteral("实际系统缓冲：接收 %1 / 发送 %2；接口速度未知。").arg(stats.actualReceiveBufferBytes?bytesLabel(stats.actualReceiveBufferBytes):QStringLiteral("未知"),stats.actualSendBufferBytes?bytesLabel(stats.actualSendBufferBytes):QStringLiteral("未知")));
    diagnosticValues[2]->setText(bytesLabel(stats.rxBytes));diagnosticValues[3]->setText(QStringLiteral("%1 数据报").arg(stats.receiveTruncatedDatagrams));diagnosticValues[4]->setText(QStringLiteral("%1 条 / %2").arg(stats.applicationDroppedRecords).arg(bytesLabel(stats.applicationDroppedBytes)));diagnosticValues[5]->setText(c->recording()?QStringLiteral("%1 · %2 失败").arg(bytesLabel(stats.recordedBytes)).arg(stats.recordingFailures):QStringLiteral("未开启 · %1 失败").arg(stats.recordingFailures));diagnosticValues[6]->setText(QStringLiteral("%1 条省略").arg(stats.displayOmitted));
    retained->setToolTip(QStringLiteral("保留 %1 行 / %2 · 缓存裁剪 %3 · 管线省略 %4 · 文本裁剪 %5 字符").arg(model->rowCount()).arg(bytesLabel(model->retainedBytes())).arg(model->omitted()).arg(stats.displayOmitted).arg(streamOmittedCharacters));
    retained->setText(QStringLiteral("显示 %1 条 · %2 · 裁剪 %3 / 省略 %4").arg(model->rowCount()).arg(bytesLabel(model->retainedBytes())).arg(model->omitted()).arg(stats.displayOmitted));
    const auto diagnosticText=QStringLiteral("接收与记录诊断\n\n网络丢失：无法直接判断（需要发送端或系统计数）\n\n应用实际 RX：%1 B · UDP %2 数据报\n应用队列丢弃：%3 条 / %4 B\n记录失败：%5 · 已记录 %6 B / %7 条\n记录队列峰值：%8\n显示管线省略：%9 · UI 缓存裁剪：%10\n\n协议序号：%11\n%12\n\n显示抽样、暂停、缓存裁剪不是网络丢失；记录未开启时不能证明原始数据完整。")
        .arg(stats.rxBytes).arg(stats.rxDatagrams).arg(stats.applicationDroppedRecords).arg(stats.applicationDroppedBytes).arg(stats.recordingFailures).arg(stats.recordedBytes).arg(stats.recordedRecords).arg(bytesLabel(stats.recordingQueueHighWater)).arg(stats.displayOmitted).arg(model->omitted()).arg(stats.sequenceEnabled?QStringLiteral("已显式启用 uint64 测试序号分析"):QStringLiteral("未知 / 未启用"))
        .arg(stats.sequenceEnabled?QStringLiteral("窗口确认缺失 %1 · 重复 %2 · 乱序 %3").arg(stats.sequenceMissing).arg(stats.sequenceDuplicates).arg(stats.sequenceReordered):QStringLiteral("请仅在数据含协议/测试序号时配置；内部记录 # 不用于推断丢帧。"));
    diagnostics->setText(diagnosticText+QStringLiteral("\nUDP 接收截断：%1 数据报（独立于应用队列丢弃）\n%2").arg(stats.receiveTruncatedDatagrams).arg(stats.sequenceEnabled?QStringLiteral("序号缺失率 %1；已确认位置 %2，待观察窗口与重复不计入分母。").arg(missingRatio).arg(stats.sequenceExpected):QStringLiteral("未启用序号分析，不计算缺失率。")));
    q->findChild<QWidget*>("diagnosticsPanel")->setAccessibleDescription(diagnostics->text());
    metricNotes[3]->setText(stats.recordedBytes?QStringLiteral("累计已记录 %1 · %2 失败").arg(bytesLabel(stats.recordedBytes)).arg(stats.recordingFailures):QStringLiteral("开启后保存原始字节与索引"));
    q->findChild<QPushButton*>("dataViewTab0")->setText(QStringLiteral("数据样本  %1").arg(proxy->rowCount()));
    if(httpPage&&httpPage->session()->active())footer->setText(QStringLiteral("HTTP手动调试  |  ")+httpPage->summary());
    else if(webSocketPage&&webSocketPage->session()->active())footer->setText(QStringLiteral("WebSocket手动调试  |  ")+webSocketPage->summary());
    else if(workflowPage&&workflowPage->runner()->active())footer->setText(QStringLiteral("工作流运行中  |  %1").arg(workflowPage->runner()->resourceSummary()));
    else if(pages->currentIndex()==0&&workspaceMode>0&&!activityController()->connected()&&!activityController()->connecting())footer->setText((workspaceMode==1?QStringLiteral("HTTP手动调试  |  "):QStringLiteral("WebSocket手动调试  |  "))+(workspaceMode==1?httpPage:webSocketPage)->summary());
    else footer->setText(QStringLiteral("%1  |  RX %2 B · TX %3 B  |  %4  |  周期已发 %5").arg(state->text()).arg(stats.rxBytes).arg(stats.txBytes).arg(c->recording()?QStringLiteral("原始记录中"):QStringLiteral("原始记录未开启")).arg(c->periodicSent()));
    if(parkedController)footer->setText(footer->text()+QStringLiteral("  |  后台 %1 · %2 · RX %3 B").arg(fromStd(profiles[parkedProfile].name),parkedController->connected()?endpointText(parkedController->localEndpoint()):QStringLiteral("已停止")).arg(parkedController->statistics().rxBytes));
    capacity->setText(QStringLiteral("按当前 RX+TX：%1 / 小时\n时长预计：%2").arg(bytesLabel((stats.rxBytesPerSecond+stats.txBytesPerSecond)*3600)).arg(duration->value()?bytesLabel((stats.rxBytesPerSecond+stats.txBytesPerSecond)*duration->value()):QStringLiteral("手动停止，时长未知")));
    if(stats.applicationDroppedRecords||stats.recordingFailures){banner->setText(QStringLiteral("管线异常：应用丢弃 %1 条；记录失败 %2。检查接收诊断和实际采集完整性。").arg(stats.applicationDroppedRecords).arg(stats.recordingFailures));banner->show();}
    }
    if(++ticks%5==0)trend->sample(stats.rxBytesPerSecond);
    if(ticks%20==0){updateStorage();if(pages->currentIndex()==1)refreshCaptures();}
    if(c->periodicActive())sendButton->setToolTip(QStringLiteral("停止周期发送 · 已发 %1 次").arg(c->periodicSent()));
}
void MainWindow::Impl::inspect() {
    const auto source=proxy->mapToSource(table->currentIndex()).siblingAtColumn(0);
    const auto* r=source.isValid()&&table->selectionModel()->hasSelection()?model->record(source.row()):nullptr;
    q->findChild<QPushButton*>("copyInspectorBytes")->setEnabled(r!=nullptr);q->findChild<QPushButton*>("copyRecord")->setEnabled(r!=nullptr);
    if(!r){
        if(emptyInspectorRendered)return;
        emptyInspectorRendered=true;
        inspectedIndex=QPersistentModelIndex{};inspectedPage=0;
        inspectedOrdinal=0;{const QSignalBlocker block(bytePage);bytePage->setRange(1,1);bytePage->setValue(1);bytePage->setSuffix(" / 1");}bytePage->setEnabled(false);byteSummary->setText(QStringLiteral("未选择记录"));byteSummary->setToolTip({});byteView->clear();byteOffsets->clear();byteAscii->clear();byteUtf8->clear();byteTextStatus->setText(QStringLiteral("中文请查看 UTF-8；ASCII 的点表示非可打印字节。"));byteRange->setText(QStringLiteral("选择记录后显示预览范围"));return;}
    emptyInspectorRendered=false;
    const int requestedPage=inspectedOrdinal==r->sequence?bytePage->value():1;
    // Retained records are immutable. Front eviction changes their row number,
    // not their bytes; a persistent index tracks the selected record across it.
    if(inspectedIndex==source&&inspectedPage==requestedPage&&inspectedDark==dark)return;
    inspectedIndex=source;inspectedPage=requestedPage;inspectedDark=dark;
    const auto raw=recordBytes(*r);const int pagesCount=std::max(1,int((raw.size()+4095)/4096));
    const bool changedRecord=inspectedOrdinal!=r->sequence;
    {const QSignalBlocker block(bytePage);bytePage->setMaximum(pagesCount);if(changedRecord)bytePage->setValue(1);bytePage->setSuffix(QStringLiteral(" / %1").arg(pagesCount));}inspectedOrdinal=r->sequence;bytePage->setEnabled(pagesCount>1);
    const int offset=(bytePage->value()-1)*4096;const int shown=int(std::min<qsizetype>(4096,raw.size()-offset));
    const auto timestamp=QDateTime::fromMSecsSinceEpoch(qint64(r->timestampUs/1000));
    byteSummary->setText(QStringLiteral("<div style='font-size:9px;color:%1'>RECORD %2 · %3 · ID %4</div><div style='font-size:13px;margin:8px 0'><b>%5</b></div><table width='100%' cellspacing='0' cellpadding='2' style='font-size:10px'><tr><td>时间</td><td align='right'>%6</td></tr><tr><td>来源</td><td align='right'>%7</td></tr></table><div style='font-size:10px;margin-top:5px'>实际长度 %8 B</div>").arg(dark?"#8b9a9f":"#5e7379").arg(r->sequence,3,10,QChar('0')).arg(recordDirection(r->direction)).arg(r->connectionId).arg(r->transport==TransportKind::Udp?QStringLiteral("UDP 数据报"):r->transport==TransportKind::Serial?QStringLiteral("串口读取块"):QStringLiteral("TCP 读取块")).arg(timestamp.toString("hh:mm:ss.zzz"),endpointText(r->peer).toHtmlEscaped()).arg(raw.size()));
    byteSummary->setToolTip(timestamp.toString(Qt::ISODateWithMs)+'\n'+endpointText(r->peer));
    QString dump,offsets,ascii;for(int start=0;start<shown;start+=8){const auto line=raw.mid(offset+start,std::min(8,shown-start));offsets+=QStringLiteral("%1\n").arg(offset+start,4,16,QChar('0'));dump+=hexBytes(line)+'\n';for(unsigned char ch:line)ascii+=ch>=32&&ch<=126?QChar(ch):QChar('.');}
    byteView->setPlainText(dump);byteOffsets->setPlainText(offsets);byteAscii->setPlainText(ascii);
    const auto textPage=byteTextPage(QByteArrayView(raw),offset,shown);byteUtf8->setPlainText(textPage.text);
    byteTextStatus->setText(textPage.invalidUtf8?QStringLiteral("存在无效 UTF-8 字节，显示 �；请核对 HEX 或设备编码。"):
        QStringLiteral("UTF-8 文本 · 控制字节用 \\xNN 表示；ASCII 仅显示单字节字符。"));
    byteTextStatus->setToolTip(QStringLiteral("文本解码字节范围 [%1, %2)；跨页字符完整显示在起始字节所在页，下一页跳过续字节。HEX/ASCII 范围仍严格按 4096 字节分页，复制保持原始字节。").arg(textPage.begin).arg(textPage.end));
    if(changedRecord&&!textPage.invalidUtf8&&std::any_of(textPage.text.begin(),textPage.text.end(),[](QChar ch){return ch.unicode()>127;}))q->findChild<QTabWidget*>("byteFormatTabs")->setCurrentIndex(2);
    if(changedRecord)revealByteText();
    QList<QTextEdit::ExtraSelection> highlights;if(shown){QTextEdit::ExtraSelection firstBytes;firstBytes.cursor=QTextCursor(byteView->document());firstBytes.cursor.setPosition(std::min(4,shown)*3-1,QTextCursor::KeepAnchor);firstBytes.format.setForeground(QColor(dark?"#85dec4":"#176e58"));firstBytes.format.setFontWeight(QFont::DemiBold);highlights.push_back(firstBytes);}byteView->setExtraSelections(highlights);
    byteRange->setText(shown?QStringLiteral("%1–%2 / %3 B").arg(offset).arg(offset+shown-1).arg(raw.size()):QStringLiteral("0 B · 空数据报"));byteRange->setToolTip(QStringLiteral("每页最多 4096 字节，页内可滚动；所有页均可查看，复制按钮复制完整原始 HEX。ASCII 中不可打印字节显示点；中文可在 UTF-8 页签查看。"));
}
void MainWindow::Impl::toggleRecording() {
    if(c->recording()){c->stopRecording();updateState();refreshCaptures();return;}if(!c->connected())return;
    RecordingOptions options;options.directory=toStd(recordDirectory->text().trimmed());options.rotateBytes=std::uint64_t(rotation->value())*1024*1024;options.durationSeconds=std::uint32_t(duration->value());options.queueBytes=std::size_t(recordQueue->value())*1024*1024;QString error;
    if(options.directory.empty()){showError(QStringLiteral("请选择采集目录。"));return;}if(!c->startRecording(options,&error))showError(error);updateState();updateStorage();
}
void MainWindow::Impl::updateStorage() {
    const auto requested=recordDirectory->text().trimmed();
    if(requested!=storageResultPath)storage->setText(QStringLiteral("实际可用空间：正在后台查询…"));
    if(storageJob){pendingStoragePath=requested;return;}
    storageJob=std::make_shared<StorageJob>();storageJob->requestedPath=requested;const auto job=storageJob;
    // Keep the latest successful result visible during periodic background queries.
    if(storage->text().isEmpty()){storage->setText(QStringLiteral("实际可用空间：正在后台查询…"));}
    q->setProperty("storageQueryActive",true);
    if(!storageWorker.submit([job]{
        try {
            QString path=job->requestedPath;
            for(int depth=0;depth<64&&!path.isEmpty()&&!job->cancel.load(std::memory_order_relaxed);++depth){if(QFileInfo::exists(path))break;const auto parent=QFileInfo(path).absolutePath();if(parent==path){path.clear();break;}path=parent;}
            if(!job->cancel.load(std::memory_order_relaxed)){
                const QStorageInfo info(path);job->result=info.isValid()&&info.isReady()?QStringLiteral("实际可用空间：%1\n卷：%2").arg(bytesLabel(double(info.bytesAvailable())),info.rootPath()):QStringLiteral("可用空间：未知（目录卷不可查询）");
            }
        } catch(...) {job->result=QStringLiteral("可用空间：未知（查询失败）");}
        job->complete();
    })){job->result=QStringLiteral("可用空间：未知（后台查询不可用）");job->complete();}
}
void MainWindow::Impl::pollStorage() {
    if(!storageJob||!storageJob->finished.load(std::memory_order_acquire))return;
    const auto job=std::move(storageJob);q->setProperty("storageQueryActive",false);
    if(job->requestedPath==recordDirectory->text().trimmed()){
        storage->setText(job->result);storageResultPath=job->requestedPath;
    }
    const auto next=std::move(pendingStoragePath);pendingStoragePath.clear();if(!next.isNull()&&next!=job->requestedPath)updateStorage();
}
void MainWindow::Impl::exportSamples() {
    if(sampleJob){showError(QStringLiteral("已有样本导出任务，请等待或取消。"));return;}
    const auto path=QFileDialog::getSaveFileName(q,QStringLiteral("导出保留的实际显示样本"),"samples.json","JSON (*.json)");if(path.isEmpty())return;
    // The model is bounded to 10 MiB / 50,000 rows. Snapshot immutable payload
    // handles only; byte conversion and all filesystem work stay off the GUI.
    std::vector<DataRecord> records;records.reserve(std::size_t(proxy->rowCount()));
    for(int i=0;i<proxy->rowCount();++i){const auto* r=model->record(proxy->mapToSource(proxy->index(i,0)).row());if(r)records.push_back(*r);}
    const QJsonObject header{{"schemaVersion",1},{"scope","retained-filtered-display-samples"},{"sampled",c->highSpeed()},{"uiOmitted",QString::number(model->omitted())},{"pipelineOmitted",QString::number(c->statistics().displayOmitted)}};
    sampleJob=std::make_shared<ExportJob>();sampleJob->output=path;sampleJob->total.store(records.size());const auto job=sampleJob;
    sampleExportButton->setEnabled(false);cancelSamples->show();cancelSamples->setEnabled(true);q->setProperty("sampleExportActive",true);
    if(!sampleWorker.submit([job,path,records=std::move(records),header]{
        try {
            QSaveFile file(path);if(!file.open(QIODevice::WriteOnly))job->error=file.errorString();else {
                auto write=[&](const QByteArray& bytes){if(job->cancel.load(std::memory_order_relaxed))return false;if(file.write(bytes)!=bytes.size()){job->error=file.errorString();return false;}return true;};
                auto prefix=QJsonDocument(header).toJson(QJsonDocument::Compact);prefix.chop(1);bool ok=write(prefix+",\"records\":[\n");
                for(std::size_t i=0;ok&&i<records.size();++i){if(job->cancel.load(std::memory_order_relaxed)){ok=false;break;}const auto& r=records[i];const QJsonObject entry{{"sequence",QString::number(r.sequence)},{"timestampUs",QString::number(r.timestampUs)},{"connectionId",QString::number(r.connectionId)},{"direction",recordDirection(r.direction)},{"transport",transportText(r.transport)},{"peer",endpointText(r.peer)},{"length",qint64(r.payload?r.payload->size():0)},{"hex",hexBytes(recordBytes(r))}};ok=write((i?QByteArray(",\n"):QByteArray())+QJsonDocument(entry).toJson(QJsonDocument::Compact));job->processed.store(i+1,std::memory_order_relaxed);}
                if(ok)ok=write("\n]}\n");
                if(ok&&!job->cancel.load(std::memory_order_relaxed)){job->success=file.commit();if(!job->success)job->error=file.errorString();}else file.cancelWriting();
            }
        } catch(const std::exception& e){job->error=QString::fromUtf8(e.what());}catch(...){job->error=QStringLiteral("样本导出遇到未知错误。");}job->complete();
    })){job->error=QStringLiteral("无法启动样本导出后台任务。");job->complete();}
}
void MainWindow::Impl::pollSamples() {
    if(!sampleJob||!sampleJob->finished.load(std::memory_order_acquire))return;
    const auto job=std::move(sampleJob);q->setProperty("sampleExportActive",false);sampleExportButton->setEnabled(true);cancelSamples->hide();
    if(job->success)q->statusBar()->showMessage(QStringLiteral("显示样本已导出：")+job->output,6000);
    else if(job->cancel.load(std::memory_order_relaxed))q->statusBar()->showMessage(QStringLiteral("样本导出已取消；原输出文件未覆盖。"),6000);
    else showError(QStringLiteral("样本导出失败：")+job->error);
}
void MainWindow::Impl::refreshCaptures(bool rescan) {
    auto* captureController=parkedController?parkedController:c;
    if(rescan)captureController->refreshCaptures(recordDirectory->text().trimmed());
    const auto captures=captureController->captures();QStringList signature;
    for(const auto& capture:captures)signature.push_back(QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9").arg(fromStd(capture.path)).arg(capture.startedUs).arg(capture.durationUs).arg(capture.bytes).arg(capture.records).arg(capture.complete).arg(fromStd(capture.error),fromStd(capture.transportSummary)).arg(capture.metadataAvailable));
    signature.push_back(QString::number(captureController->recording())+'|'+QString::number(bool(exportJob))+'|'+QString::number(bool(deleteJob)));
    if(captureSignature==signature)return;
    captureSignature=signature;
    const auto selected=captureList->currentItem()?captureList->currentItem()->data(Qt::UserRole).toString():QString();captureList->clear();
    int index=0;for(const auto& capture:captures){const auto path=fromStd(capture.path);const auto status=!capture.error.empty()?QStringLiteral("失败：")+fromStd(capture.error):capture.complete?QStringLiteral("完整结束"):captureController->recording()?QStringLiteral("记录中 / 待收尾"):QStringLiteral("未完成 / 收尾状态待确认");
        const auto protocol=capture.transportSummary.empty()?QStringLiteral("通信类型未知"):fromStd(capture.transportSummary);
        const auto start=capture.startedUs?QDateTime::fromMSecsSinceEpoch(qint64(capture.startedUs/1000)).toString("yyyy-MM-dd HH:mm:ss"):QStringLiteral("开始时间未知");
        const auto durationText=capture.metadataAvailable?QStringLiteral("%1 s").arg(double(capture.durationUs)/1e6,0,'f',3):QStringLiteral("时长未知");
        const auto summary=QStringLiteral("%1 · %2 · %3 · %4 · %5 条").arg(protocol,start,durationText,bytesLabel(double(capture.bytes))).arg(capture.records);
        const auto indexInfo=capture.metadataAvailable?QStringLiteral("记录 / 索引：二进制原始记录 + 有效元数据目录"):QStringLiteral("记录 / 索引：二进制文件；元数据目录缺失或无效");
        auto* item=new QListWidgetItem(QStringLiteral("%1\n%2\n%3 · %4\n%5").arg(QFileInfo(path).fileName(),summary,status,indexInfo,path),captureList);item->setData(Qt::UserRole,path);item->setSizeHint({300,110});item->setData(Qt::UserRole+10,true);item->setToolTip(item->text());
        auto* row=new QWidget;auto* layout=new QHBoxLayout(row);layout->setContentsMargins(16,12,16,12);layout->setSpacing(14);auto* badge=label(capture.transportSummary.empty()?QStringLiteral("RAW"):protocol.section(' ',0,0));badge->setFont(design::font(10,true));badge->setProperty("muted",true);badge->setFixedWidth(40);layout->addWidget(badge);auto* text=new QVBoxLayout;text->setSpacing(4);
        for(const auto& value:{QFileInfo(path).fileName(),summary,status+QStringLiteral(" · ")+indexInfo}){auto* line=label(value);line->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);line->setToolTip(value);line->setProperty("small",text->count()!=0);text->addWidget(line);}layout->addLayout(text,1);
        auto* exportRow=button(QStringLiteral("导出"),("captureRowExport"+QByteArray::number(index)).constData());exportRow->setProperty("textAction",true);exportRow->setEnabled(!exportJob);layout->addWidget(exportRow);
        QObject::connect(exportRow,&QPushButton::clicked,q,[this,path]{const auto output=QFileDialog::getSaveFileName(q,QStringLiteral("导出实际采集"),QFileInfo(path).completeBaseName()+".json","JSON (*.json);;Text (*.txt)");if(!output.isEmpty())beginCaptureExport(path,output);});
        auto* remove=button(QStringLiteral("删除"),("captureRowDelete"+QByteArray::number(index)).constData());remove->setProperty("textAction",true);remove->setEnabled(!captureController->recording()&&!deleteJob&&!exportJob);layout->addWidget(remove);QObject::connect(remove,&QPushButton::clicked,q,[this,path]{deleteCapture(path);});
        captureList->setItemWidget(item,row);if(path==selected)captureList->setCurrentItem(item);++index;
    }
}
void MainWindow::Impl::deleteCapture(const QString& path) {
    if(deleteJob||exportJob||c->recording()||(parkedController&&parkedController->recording()))return;
    QMessageBox dialog(QMessageBox::Question,QStringLiteral("删除采集文件"),QStringLiteral("删除原始采集与元数据目录？\n%1\n此操作无法撤销。").arg(path),QMessageBox::NoButton,q);auto* remove=dialog.addButton(QStringLiteral("删除"),QMessageBox::DestructiveRole);dialog.addButton(QStringLiteral("取消"),QMessageBox::RejectRole);dialog.exec();if(dialog.clickedButton()!=remove)return;
    deleteJob=std::make_shared<ExportJob>();deleteJob->output=path;const auto job=deleteJob;q->setProperty("captureDeleteActive",true);
    if(!deleteWorker.submit([job,path]{
        try{if(!job->cancel.load(std::memory_order_relaxed)){
            if(!QFile::remove(path))job->error=QStringLiteral("无法删除原始采集：")+path;
            else if(QFileInfo::exists(path+".meta.json")&&!QFile::remove(path+".meta.json"))job->error=QStringLiteral("原始采集已删除，元数据目录删除失败：")+path+".meta.json";
            else job->success=true;
        }}catch(...){job->error=QStringLiteral("删除采集时发生错误。");}job->complete();
    })){job->error=QStringLiteral("无法启动后台删除任务。");job->complete();}refreshCaptures();
}
void MainWindow::Impl::pollDelete() {
    if(!deleteJob||!deleteJob->finished.load(std::memory_order_acquire))return;
    const auto job=std::move(deleteJob);q->setProperty("captureDeleteActive",false);
    if(!job->error.isEmpty())showError(job->error);else if(job->success)q->statusBar()->showMessage(QStringLiteral("采集文件已删除。"),4000);refreshCaptures(true);
}

void MainWindow::Impl::beginCaptureExport(const QString& input,const QString& output) {
    exportStatus->show();cancelExport->show();exportProgress->show();
    if(exportJob){showError(QStringLiteral("已有导出任务，请等待完成或取消。"));return;}
    exportJob=std::make_shared<ExportJob>();exportJob->output=output;
    exportCaptureButton->setEnabled(false);openCaptureButton->setEnabled(false);cancelExport->setEnabled(true);
    exportProgress->setValue(0);exportStatus->setText(QStringLiteral("后台导出：")+QFileInfo(input).fileName());q->setProperty("captureExportActive",true);
    const auto job=exportJob;
    if(!exportWorker.submit([job,input,output]{
            try {
                job->success=exportCapture(input,output,&job->error,[job](std::uint64_t done,std::uint64_t total){job->processed.store(done,std::memory_order_relaxed);job->total.store(total,std::memory_order_relaxed);return !job->cancel.load(std::memory_order_relaxed);});
            } catch(const std::exception& e) { job->error=QString::fromUtf8(e.what()); }
            catch(...) { job->error=QStringLiteral("导出遇到未知错误。"); }
            job->complete();
    })){job->error=QStringLiteral("无法启动采集导出后台任务。");job->complete();}
}
void MainWindow::Impl::pollExport() {
    if(!exportJob)return;
    const auto done=exportJob->processed.load(std::memory_order_relaxed),total=exportJob->total.load(std::memory_order_relaxed);
    exportProgress->setValue(total?int(std::min(1000.0,double(done)*1000/double(total))):0);
    if(!exportJob->cancel.load(std::memory_order_relaxed))exportStatus->setText(QStringLiteral("后台导出 %1 / %2 · 一个任务，接收和 UI 继续").arg(bytesLabel(double(done)),total?bytesLabel(double(total)):QStringLiteral("未知")));
    if(!exportJob->finished.load(std::memory_order_acquire))return;
    const auto job=std::move(exportJob);q->setProperty("captureExportActive",false);exportCaptureButton->setEnabled(true);openCaptureButton->setEnabled(true);cancelExport->setEnabled(false);
    if(job->success){exportProgress->setValue(1000);exportStatus->setText(QStringLiteral("实际采集已导出：")+job->output);}
    else if(job->cancel.load(std::memory_order_relaxed)){exportStatus->setText(QStringLiteral("已取消导出；原输出文件未覆盖。"));}
    else {exportStatus->setText(QStringLiteral("导出失败：")+job->error);showError(job->error);}
}

MainWindow::MainWindow(SessionController* controller,QWidget* parent):QMainWindow(parent),d(std::make_unique<Impl>(this,controller)){}
MainWindow::~MainWindow()=default;
void MainWindow::closeEvent(QCloseEvent* event) {
    // A parent close never guarantees delivery of a child closeEvent. Keep
    // the draft and current operation intact until the user's decision.
    const bool untouchedExample=d->workflowPage&&d->workflowPage->document().toJson()==d->initialWorkflowDocument&&!d->workflowPage->runner()->active();
    if(d->workflowPage&&!untouchedExample&&!d->workflowPage->confirmLeave()){event->ignore();return;}
    if((d->httpPage&&d->httpPage->session()->active())||(d->webSocketPage&&d->webSocketPage->session()->active())||d->httpPage->dirty()||d->webSocketPage->dirty()){
        QMessageBox confirm(this);confirm.setObjectName("manualProtocolExitConfirmation");confirm.setWindowTitle(QStringLiteral("退出协议调试"));confirm.setText(QStringLiteral("退出会取消HTTP请求、释放WebSocket连接，并放弃未保存的协议编辑。已保存的请求与连接配置保留；不会自动保存凭据。"));
        auto* leave=confirm.addButton(QStringLiteral("退出并释放活动"),QMessageBox::AcceptRole);auto* keep=confirm.addButton(QStringLiteral("保留当前窗口"),QMessageBox::RejectRole);confirm.setDefaultButton(keep);confirm.exec();if(confirm.clickedButton()!=leave){event->ignore();return;}
    }
    if(d->httpPage)d->httpPage->session()->cancel();
    if(d->webSocketPage)d->webSocketPage->session()->cancel();
    if(d->workflowPage)d->workflowPage->runner()->stop();
    if(d->parkedController)d->parkedController->stop();
    d->c->stopPeriodic();d->c->stopRecording();d->c->stop();
    QMainWindow::closeEvent(event);
}
}
