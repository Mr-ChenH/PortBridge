#include "ui/workflow_graph_model.hpp"
#include "ui/workflow_page.hpp"
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QFileDialog>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QTabWidget>
#include <QTableView>
#include <QTcpServer>
#ifdef PORTBRIDGE_WORKFLOW_REAL_PROTOCOL
#include <QWebSocket>
#include <QWebSocketServer>
#endif
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QUdpSocket>
#include <QUndoStack>
#include <QtNodes/BasicGraphicsScene>
#include <QtNodes/GraphicsView>
#include <QtNodes/internal/NodeGraphicsObject.hpp>
#include <QtNodes/internal/ConnectionGraphicsObject.hpp>
#include <QtNodes/AbstractConnectionPainter>
#include <QtTest>
using namespace portbridge;
using namespace QtNodes;
class WorkflowUiTest : public QObject {
    Q_OBJECT
    static QPushButton *button(WorkflowPage &p, const char *name) { return p.findChild<QPushButton *>(name); }
    static WorkflowDocument logicFlow() {
        auto d = WorkflowDocument::templateDocument("blank");
        d.nodes.insert(1, {"v",
                           "variable",
                           QStringLiteral("设置 token"),
                           {260, 100},
                           {{"variable", "token"}, {"value", "actual-private-credential"}}});
        d.nodes.insert(2, {"a",
                           "assert",
                           QStringLiteral("校验"),
                           {510, 100},
                           {{"source", "token"}, {"expected", "actual-private-credential"}}});
        d.edges = {{"e1", "n1", "v", "success"}, {"e2", "v", "a", "success"}, {"e3", "a", "n2", "success"}};
        return d;
    }
    static void show(WorkflowPage &p, QSize size = {1024, 768}) {
        p.resize(size);
        p.show();
        QTest::qWait(60);
        QCoreApplication::processEvents();
    }
  private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("PortBridgeWorkflowUiTests");
        QCoreApplication::setApplicationName("Isolated");
        QSettings().clear();
    }
    void silentTemplatesAndRoundtrip() {
        WorkflowPage p;
        QSignalSpy state(p.runner(), &WorkflowRunner::stateChanged);
        show(p);
        QVERIFY(p.applyTemplate("blank"));
        QCOMPARE(p.document().nodes.size(), 0);
        QCOMPARE(state.count(), 0);
        p.graphScene()->undoStack().undo();
        QCOMPARE(p.document().nodes.size(), 8);
        QCOMPARE(state.count(), 0);
        QVERIFY(p.applyTemplate("udp"));
        auto document = p.document();
        QTemporaryDir temp;
        QString why;
        auto filename = temp.filePath("roundtrip.pbflow.json");
        QVERIFY2(p.saveFile(filename, &why), qPrintable(why));
        QVERIFY(!p.isDirty());
        QVERIFY(p.loadFile(filename, &why));
        QCOMPARE(p.document().toJson(), document.toJson());
        QCOMPARE(state.count(), 0);
        QVERIFY(!p.runner()->active());
    }
    void importRejectsPrototypeAndUnknown() {
        WorkflowPage p;
        QTemporaryDir temp;
        auto name = temp.filePath("bad.pbflow.json");
        QFile f(name);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"prototypeOnly\":true}");
        f.close();
        QString why;
        auto before = p.document().toJson();
        QVERIFY(!p.loadFile(name, &why));
        QVERIFY(why.contains(QStringLiteral("原型")));
        QCOMPARE(p.document().toJson(), before);
        auto doc = p.document().toJson();
        auto ns = doc["nodes"].toArray();
        auto n = ns[0].toObject();
        n["type"] = "unknown.integration.node";
        ns[0] = n;
        doc["nodes"] = ns;
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(doc).toJson());
        f.close();
        QVERIFY(!p.loadFile(name, &why));
        QVERIFY(!why.isEmpty());
        QCOMPARE(p.document().toJson(), before);
    }
    void paletteSearchNativeDropAndUndo() {
        WorkflowPage p;
        show(p);
        QVERIFY(p.applyTemplate("blank"));
        auto *search = p.findChild<QLineEdit *>("workflowNodeSearch");
        auto *tree = p.findChild<QTreeWidget *>("workflowPalette");
        search->setText("WS");
        int visible = 0;
        for (int g = 0; g < tree->topLevelItemCount(); ++g) {
            auto *group = tree->topLevelItem(g);
            for (int i = 0; i < group->childCount(); ++i)
                if (!group->isHidden() && !group->child(i)->isHidden())
                    ++visible;
        }
        QCOMPARE(visible, 1);
        QMimeData mime;
        mime.setData("application/x-portbridge-workflow-node", "http");
        auto *viewport = p.graphView()->viewport();
        QPoint point = viewport->rect().center();
        QDragEnterEvent enter(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &drop);
        QCOMPARE(p.document().nodes.size(), 1);
        QCOMPARE(p.document().nodes[0].type, QString("http"));
        QTest::mouseClick(button(p, "workflowUndo"), Qt::LeftButton);
        QCOMPARE(p.document().nodes.size(), 0);
        QTest::mouseClick(button(p, "workflowRedo"), Qt::LeftButton);
        QCOMPARE(p.document().nodes.size(), 1);
        QVERIFY(!p.runner()->active());
    }
    void mouseMovePortConnectAndDelete() {
        WorkflowPage p;
        auto doc = WorkflowDocument::templateDocument("blank");
        doc.edges.clear();
        doc.nodes[0].position = {40, 80};
        doc.nodes[1].position = {250, 80};
        QVERIFY(p.setDocument(doc));
        show(p);
        auto *model = p.graphModel();
        auto *view = p.graphView();
        auto start = model->graphId("n1"), end = model->graphId("n2");
        auto *item = p.graphScene()->nodeGraphicsObject(start);
        view->setupScale(1);
        view->centerOn(QPointF(195, 115));
        auto origin = model->node(start)->position;
        auto center = view->mapFromScene(origin + QPointF(52, 24));
        QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, center);
        QTest::mouseMove(view->viewport(), center + QPoint(30, 25), 20);
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, center + QPoint(30, 25));
        QVERIFY(model->node(start)->position != origin);
        QVERIFY(item);
        p.graphScene()->undoStack().undo();
        QCOMPARE(model->node(start)->position, origin);
        auto out = view->mapFromScene(origin + QPointF(108, 38));
        auto in = view->mapFromScene(model->node(end)->position + QPointF(0, 38));
        QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, out);
        QTest::mouseMove(view->viewport(), in, 30);
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, in);
        QCOMPARE(p.document().edges.size(), 1);
        auto edge = p.document().edges.first();
        QCOMPARE(edge.port, QString("success"));
        p.selectNode("n2");
        QTest::mouseClick(button(p, "workflowDelete"), Qt::LeftButton);
        QCOMPARE(p.document().nodes.size(), 1);
        QCOMPARE(p.document().edges.size(), 0);
        p.graphScene()->undoStack().undo();
        QCOMPARE(p.document().nodes.size(), 2);
        QCOMPARE(p.document().edges.first().id, edge.id);
    }
    void canonicalHttpAndWsParameterMutation() {
        WorkflowPage p;
        show(p);
        auto http = p.document().nodes[1].id;
        p.selectNode(http);
        auto *url = p.findChild<QLineEdit *>("workflowParam_url");
        QVERIFY(url);
        url->setFocus();
        url->selectAll();
        QTest::keyClicks(url, "http://127.0.0.1:9999/check");
        QTest::keyClick(url, Qt::Key_Tab);
        QCOMPARE(p.graphModel()->node(p.graphModel()->graphId(http))->parameters["url"].toString(),
                 QString("http://127.0.0.1:9999/check"));
        p.graphScene()->undoStack().undo();
        QVERIFY(p.graphModel()
                    ->node(p.graphModel()->graphId(http))
                    ->parameters["url"]
                    .toString()
                    .contains("8080"));
        p.selectNode(p.document().nodes[3].id);
        QVERIFY(p.findChild<QLineEdit *>("workflowParam_subprotocol"));
        p.selectNode(p.document().nodes[4].id);
        auto *type = p.findChild<QComboBox *>("workflowParam_messageType");
        QVERIFY(type);
        type->setCurrentIndex(type->findData("binary"));
        QCOMPARE(p.document().nodes[4].parameters["messageType"].toString(), QString("binary"));
        QVERIFY(!p.findChild<QPlainTextEdit *>("workflowParam_framing"));
        QVERIFY(!p.findChild<QLineEdit *>("workflowParam_target"));
        auto* resource=p.findChild<QComboBox*>("workflowParam_resource");
        resource->setCurrentIndex(resource->findData("raw"));
        QTRY_VERIFY(p.findChild<QPlainTextEdit *>("workflowParam_framing"));
        QVERIFY(p.findChild<QPlainTextEdit *>("workflowParam_sourceFilter"));
        QTest::mouseClick(button(p,"workflowSectionToggle_framing"),Qt::LeftButton);
        auto *framing = p.findChild<QComboBox *>("workflowFramingMode");
        QVERIFY(framing && framing->isVisible());
        framing->setCurrentIndex(framing->findData("fixed"));
        auto frame = p.document().nodes[4].parameters["framing"].toObject();
        QCOMPARE(frame["mode"].toString(), QString("fixed"));
        QCOMPARE(frame["length"].toInt(), 8);
        QVERIFY(!p.runner()->active());
    }
    void designParameterHierarchyAndValidationReveal() {
        QSettings().clear();
        WorkflowPage p;
        show(p,{1440,1000});
        p.selectNode(p.document().nodes[1].id);
        auto* headers=button(p,"workflowSectionToggle_headers");
        auto* advanced=button(p,"workflowSectionToggle_advanced");
        auto* body=p.findChild<QPlainTextEdit*>("workflowParam_body");
        QVERIFY(headers && advanced && body);
        QVERIFY(!headers->isChecked() && !advanced->isChecked());
        QVERIFY(!p.findChild<QPlainTextEdit*>("workflowParam_headers")->isVisible());
        QVERIFY(body->isVisible());
        auto* scroll=p.findChild<QScrollArea*>();
        QVERIFY(scroll);
        const auto rect=QRect(body->mapTo(scroll->viewport(),QPoint()),body->size());
        if (p.height()>=900) {
            QVERIFY2(scroll->viewport()->rect().contains(rect),qPrintable(QString("window=%1x%2 viewport=%3x%4 body=%5,%6 %7x%8 log=%9")
                .arg(p.width()).arg(p.height()).arg(scroll->viewport()->width()).arg(scroll->viewport()->height())
                .arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height()).arg(p.findChild<QWidget*>("workflowExecution")->height())));
        } else {
            scroll->ensureWidgetVisible(body);
            QCoreApplication::processEvents();
            const auto shown=QRect(body->mapTo(scroll->viewport(),QPoint()),body->size());
            QVERIFY(scroll->viewport()->rect().intersects(shown));
            scroll->verticalScrollBar()->setValue(0);
        }
        QTest::mouseClick(headers,Qt::LeftButton);
        QVERIFY(p.findChild<QPlainTextEdit*>("workflowParam_headers")->isVisible());
        QTest::mouseClick(headers,Qt::LeftButton);
        QVERIFY(!p.runner()->active());
        auto doc=p.document();doc.nodes[1].parameters["timeout"]="0";
        QVERIFY(p.setDocument(doc));p.validateFlow();
        QCOMPARE(p.selectedNodeId(),doc.nodes[1].id);
        advanced=button(p,"workflowSectionToggle_advanced");
        QVERIFY(advanced->isChecked());
        QVERIFY(p.findChild<QLineEdit*>("workflowParam_timeout")->isVisible());
        QVERIFY(p.findChild<QLineEdit*>("workflowParam_timeout")->hasFocus());
        QVERIFY(!p.runner()->active());
        p.selectNode(p.document().nodes[4].id);
        auto* match=p.findChild<QComboBox*>("workflowParam_match");
        match->setCurrentIndex(match->findData("any"));
        QTRY_VERIFY(!p.findChild<QLineEdit*>("workflowParam_path"));
        QVERIFY(!p.findChild<QLineEdit*>("workflowParam_expected"));
        QVERIFY(!p.findChild<QComboBox*>("workflowFramingMode"));
        if (!p.findChild<QWidget*>("workflowPalettePanel")->isVisible())
            QTest::mouseClick(button(p,"workflowPaletteToggle"),Qt::LeftButton);
        auto* search=p.findChild<QLineEdit*>("workflowNodeSearch");
        search->setText("no-such-protocol");
        QVERIFY(p.findChild<QLabel*>("workflowPaletteEmpty")->isVisible());
        search->clear();
        QVERIFY(!p.findChild<QLabel*>("workflowPaletteEmpty")->isVisible());
        doc=p.document();doc.description.clear();
        QVERIFY(p.setDocument(doc));
        QVERIFY(p.findChild<QLabel*>("workflowDescription")->text().startsWith(QStringLiteral("组合步骤")));
    }
    void validationFocusAndNativeTemplates() {
        WorkflowPage p;
        show(p);
        auto doc = p.document();
        doc.nodes[1].parameters["url"] = "bad://url";
        QVERIFY(p.setDocument(doc));
        QCoreApplication::processEvents();
        p.validateFlow();
        QCOMPARE(p.selectedNodeId(), doc.nodes[1].id);
        auto *url = p.findChild<QLineEdit *>("workflowParam_url");
        QVERIFY(url && url->hasFocus());
        QTimer::singleShot(30, &p, [&p] {
            auto *dialog = p.findChild<QDialog *>("workflowTemplatesDialog");
            QVERIFY(dialog);
            auto *udp = dialog->findChild<QPushButton *>("workflowTemplate_udp");
            QVERIFY(udp);
            QTest::mouseClick(udp, Qt::LeftButton);
        });
        QTest::mouseClick(button(p, "workflowTemplates"), Qt::LeftButton);
        QCOMPARE(p.document().nodes.size(), 5);
        QVERIFY(!p.runner()->active());
    }
    void runnerSnapshotLogsVariablesAndRedaction() {
        WorkflowPage p;
        auto doc = logicFlow();
        QVERIFY(p.setDocument(doc));
        show(p);
        bool prepared = false;
        p.setRunPreparation([&](const WorkflowDocument &snapshot, QString *) {
            prepared = snapshot.toJson() == doc.toJson();
            return true;
        });
        QSignalSpy finished(p.runner(), &WorkflowRunner::finished);
        QTest::mouseClick(button(p, "workflowRun"), Qt::LeftButton);
        QVERIFY(prepared);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
        QCOMPARE(p.runner()->state(), WorkflowRunState::Completed);
        auto *table = p.findChild<QTableView *>("workflowLogs");
        QVERIFY(table->model()->rowCount() >= 4);
        p.selectNode("v", true);
        auto *result = p.findChild<QPlainTextEdit *>("workflowResult");
        QVERIFY(!result->toPlainText().contains("actual-private-credential"));
        auto *vars = p.findChild<QTableView *>("workflowVariables");
        QVERIFY(vars->model()->rowCount() > 0);
        for (int i = 0; i < vars->model()->rowCount(); ++i)
            QVERIFY(!vars->model()->index(i, 2).data().toString().contains("actual-private-credential"));
        QTemporaryDir temp;
        QString why;
        QVERIFY(p.saveFile(temp.filePath("safe.pbflow.json"), &why));
        QFile file(temp.filePath("safe.pbflow.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(!file.readAll().contains("actual-private-credential"));
        auto index = table->model()->index(1, 1);
        table->scrollTo(index);
        QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier,
                          table->visualRect(index).center());
        QCOMPARE(p.selectedNodeId(), QString("v"));
        QCOMPARE(p.findChild<QTabWidget *>("workflowInspectorTabs")->currentIndex(), 1);
    }
    void pauseStopRejectionAndMutationLock() {
        WorkflowPage p;
        auto doc = logicFlow();
        doc.nodes.insert(1, {"delay", "delay", QStringLiteral("等待"), {150, 280}, {{"duration", "120"}}});
        doc.edges[0].to = "delay";
        doc.edges.append({"ed", "delay", "v", "success"});
        QVERIFY(p.setDocument(doc));
        show(p);
        p.setRunPreparation([](const WorkflowDocument &, QString *error) {
            *error = QStringLiteral("活动资源替换被拒绝");
            return false;
        });
        QTest::mouseClick(button(p, "workflowRun"), Qt::LeftButton);
        QVERIFY(!p.runner()->active());
        QVERIFY(p.findChild<QLabel *>("workflowIssue")->text().contains(QStringLiteral("拒绝")));
        p.setRunPreparation([](const WorkflowDocument &, QString *) { return true; });
        QTest::mouseClick(button(p, "workflowRun"), Qt::LeftButton);
        QTRY_COMPARE(p.runner()->activeNodeId(), QString("delay"));
        auto snapshot = p.document().toJson();
        QVERIFY(!button(p, "workflowTemplates")->isEnabled());
        QCOMPARE(p.graphModel()->addNode("http"), InvalidNodeId);
        QVERIFY(!p.setDocument(WorkflowDocument::templateDocument("udp")));
        QCOMPARE(p.document().toJson(), snapshot);
        QTest::mouseClick(button(p, "workflowPause"), Qt::LeftButton);
        QTest::qWait(180);
        QCOMPARE(p.runner()->state(), WorkflowRunState::Paused);
        QCOMPARE(p.runner()->result("delay").state, WorkflowNodeState::Succeeded);
        QVERIFY(!p.runner()->variables().contains("token"));
        QVERIFY(button(p, "workflowStop")->isEnabled());
        QTest::mouseClick(button(p, "workflowPause"), Qt::LeftButton);
        QTRY_COMPARE(p.runner()->state(), WorkflowRunState::Completed);
        QTest::mouseClick(button(p, "workflowRun"), Qt::LeftButton);
        QTRY_COMPARE(p.runner()->activeNodeId(), QString("delay"));
        QTest::mouseClick(button(p, "workflowLogToggle"), Qt::LeftButton);
        QVERIFY(button(p, "workflowStop")->isVisible());
        QTest::mouseClick(button(p, "workflowStop"), Qt::LeftButton);
        QCOMPARE(p.runner()->state(), WorkflowRunState::Stopped);
        QTest::qWait(170);
        QVERIFY(!p.runner()->variables().contains("token"));
        QVERIFY(button(p, "workflowRun")->isEnabled());
    }
    void realUdpRunnerAndTimeoutResult() {
        QUdpSocket peer;
        QVERIFY(peer.bind(QHostAddress::LocalHost, 0));
        connect(&peer, &QUdpSocket::readyRead, this, [&] {
            while (peer.hasPendingDatagrams()) {
                QByteArray data(int(peer.pendingDatagramSize()), '\0');
                QHostAddress from;
                quint16 port;
                peer.readDatagram(data.data(), data.size(), &from, &port);
                peer.writeDatagram("ready", from, port);
            }
        });
        SessionController session;
        ConnectionConfig config;
        config.kind = TransportKind::Udp;
        config.name = "UI actual UDP";
        config.localAddress = "127.0.0.1";
        config.localPort = 0;
        config.remoteAddress = "127.0.0.1";
        config.remotePort = peer.localPort();
        session.start(config);
        QTRY_VERIFY(session.connected());
        session.setDisplayPaused(true);
        session.setHighSpeed(true);
        WorkflowPage p;
        auto doc = WorkflowDocument::templateDocument("udp");
        doc.nodes[2].parameters = {
            {"format", "text"},
            {"payload", "ping"},
            {"resource", "raw"},
            {"match", "equals"},
            {"expected", "ready"},
            {"timeout", "500"},
            {"output", "message"},
            {"sourceFilter", QJsonObject{{"address", "127.0.0.1"}, {"port", peer.localPort()}}}};
        doc.nodes[3].parameters = {{"source", "message.text"}, {"expected", "ready"}};
        QVERIFY(p.setDocument(doc));
        p.setSessionProvider([&] { return &session; });
        show(p);
        QTest::mouseClick(button(p, "workflowRun"), Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(p.runner()->state(), WorkflowRunState::Completed, 2500);
        QVERIFY(session.connected());
        QVERIFY(session.takeDisplayRecords().empty());
        QCOMPARE(p.runner()->variables()["message"].toObject()["text"].toString(), QString("ready"));
        p.selectNode(doc.nodes[2].id, true);
        QVERIFY(p.findChild<QPlainTextEdit *>("workflowResult")->toPlainText().contains("ready"));
        doc.nodes[2].parameters["sourceFilter"] = QJsonObject{{"port", 1}};
        doc.nodes[2].parameters["timeout"] = "60";
        QVERIFY(p.setDocument(doc));
        QTest::mouseClick(button(p, "workflowRun"), Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(p.runner()->state(), WorkflowRunState::Failed, 1500);
        QCOMPARE(p.selectedNodeId(), doc.nodes[2].id);
        QCOMPARE(p.findChild<QTabWidget *>("workflowInspectorTabs")->currentIndex(), 1);
        QVERIFY(!p.findChild<QPlainTextEdit *>("workflowResult")->toPlainText().isEmpty());
        QVERIFY(session.connected());
        session.stop();
    }
    void keyboardPaletteAndDirtyPrompt() {
        WorkflowPage p;
        show(p, {1440, 1000});
        QVERIFY(p.applyTemplate("blank"));
        auto *palette = p.findChild<QTreeWidget *>("workflowPalette");
        auto *item = palette->topLevelItem(0)->child(0);
        palette->scrollToItem(item);
        QTest::mouseClick(palette->viewport(), Qt::LeftButton, Qt::NoModifier,
                          palette->visualItemRect(item).center());
        QTest::mouseDClick(palette->viewport(), Qt::LeftButton, Qt::NoModifier,
                           palette->visualItemRect(item).center());
        QTest::mouseRelease(palette->viewport(), Qt::LeftButton, Qt::NoModifier,
                            palette->visualItemRect(item).center());
        QTRY_COMPARE(p.document().nodes.size(), 1);
        p.graphView()->setFocus();
        QTest::keyClick(p.graphView(), Qt::Key_D, Qt::ControlModifier);
        QCOMPARE(p.document().nodes.size(), 2);
        QTest::keyClick(p.graphView(), Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(p.document().nodes.size(), 1);
        auto *title = p.findChild<QLineEdit *>("workflowNodeTitle");
        QVERIFY(title);
        title->setFocus();
        QTest::keyClick(title, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(title, Qt::Key_Delete);
        QCOMPARE(p.document().nodes.size(), 1);
        QTimer::singleShot(20, &p, [] {
            auto *prompt = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY(prompt);
            prompt->button(QMessageBox::Cancel)->click();
        });
        QVERIFY(!p.confirmLeave());
        QTimer::singleShot(20, &p, [] {
            auto *prompt = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY(prompt);
            prompt->button(QMessageBox::Discard)->click();
        });
        QVERIFY(p.confirmLeave());
    }
    void keyboardPortConnectionAndControlPorts() {
        WorkflowPage p;
        auto doc = WorkflowDocument::templateDocument("blank");
        doc.edges.clear();
        QVERIFY(p.setDocument(doc));
        show(p);
        p.graphView()->setFocus();
        QTimer::singleShot(20, &p, [&p] {
            auto *dialog = p.findChild<QDialog *>("workflowConnectDialog");
            QVERIFY(dialog);
            auto *controls = dialog->findChild<QDialogButtonBox *>();
            QVERIFY(controls);
            controls->button(QDialogButtonBox::Ok)->click();
        });
        QTest::keyClick(p.graphView(), Qt::Key_L, Qt::ControlModifier);
        QCOMPARE(p.document().edges.size(), 1);
        QCOMPARE(p.document().edges.first().port, QString("success"));
        p.activateWindow();
        p.graphView()->setFocus();
        QTest::qWait(25);
        QVERIFY(p.graphView()->hasFocus());
        QTest::keyClick(p.graphView(), Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(p.document().edges.size(), 0);
        auto *model = p.graphModel();
        auto branch = model->addNode("branch"), loop = model->addNode("loop");
        QCOMPARE(model->outputs(branch), QStringList({"true", "false", "error"}));
        QCOMPARE(model->outputs(loop), QStringList({"body", "done", "error"}));
        auto a = model->addNode("delay"), b = model->addNode("delay");
        QVERIFY(model->connectionPossible({a, 0, b, 0}));
        model->addConnection({a, 0, b, 0});
        QVERIFY(!model->connectionPossible({b, 0, a, 0}));
        QVERIFY(!p.runner()->active());
    }
    void boundedPreviewAndStaticCredentialExport() {
        WorkflowPage p;
        auto doc = logicFlow();
        doc.nodes[1].parameters["variable"] = "large";
        doc.nodes[1].parameters["value"] =
            QStringLiteral("{\"token\":\"preview-private-secret\",\"data\":\"") + QString(100000, 'x') +
            "\"}";
        doc.nodes[2].parameters = {{"source", "large"}, {"operator", "exists"}};
        QVERIFY(p.setDocument(doc));
        show(p);
        QTest::mouseClick(button(p, "workflowRun"), Qt::LeftButton);
        QTRY_COMPARE(p.runner()->state(), WorkflowRunState::Completed);
        p.selectNode("v", true);
        auto output = p.findChild<QPlainTextEdit *>("workflowResult")->toPlainText();
        QVERIFY(output.size() < 34000);
        QVERIFY(!output.contains("preview-private-secret"));
        auto secretDoc = logicFlow();
        QVERIFY(p.setDocument(secretDoc));
        QTemporaryDir temp;
        QString why;
        auto path = temp.filePath("secret.pbflow.json");
        QVERIFY(p.saveFile(path, &why));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(!file.readAll().contains("actual-private-credential"));
    }
#ifdef PORTBRIDGE_WORKFLOW_REAL_PROTOCOL
    void longCredentialPreviewClipboardAndExport_data() {
        QTest::addColumn<int>("padding");
        QTest::addColumn<QJsonValue>("credential");
        QTest::addColumn<QString>("credentialKey");
        QTest::newRow("5000-character-token") << 0 << QJsonValue(QString(5000,'s')+"private-tail") << QString("token");
        QTest::newRow("encoded-chunk-boundary") << 52000 << QJsonValue(QString(5000,'s')+"private-tail") << QString("token");
        QTest::newRow("numeric-token") << 0 << QJsonValue(1234567890123.) << QString("token");
        QTest::newRow("boolean-token") << 0 << QJsonValue(true) << QString("token");
        QTest::newRow("null-token") << 0 << QJsonValue(QJsonValue::Null) << QString("token");
        QTest::newRow("array-token") << 0 << QJsonValue(QJsonArray{"private-array-credential", 1234567890123.}) << QString("token");
        QTest::newRow("object-token") << 0 << QJsonValue(QJsonObject{{"value","private-object-credential"}}) << QString("token");
        QTest::newRow("escaped-key") << 0 << QJsonValue(1234567890123.) << QString("escaped-token");
        QTest::newRow("large-body-credential") << 300000 << QJsonValue(QString(5000,'s')+"private-tail") << QString("clientCredential");
    }
    void longCredentialPreviewClipboardAndExport() {
        QFETCH(int, padding);
        QFETCH(QJsonValue, credential);
        QFETCH(QString, credentialKey);
        const bool escaped=credentialKey=="escaped-token";
        if (escaped) credentialKey="token";
        const QString secret=credential.isString()?credential.toString():credential.isDouble()?QString::number(credential.toDouble(),'g',16):QString();
        QByteArray body=QJsonDocument(QJsonObject{{"padding",QString(padding,'p')},{credentialKey,credential}}).toJson(QJsonDocument::Compact);
        if (escaped) body.replace("\"token\"","\"to\\u006Ben\"");
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server,&QTcpServer::newConnection,this,[&] {
            auto* socket=server.nextPendingConnection();
            auto request=std::make_shared<QByteArray>();
            connect(socket,&QTcpSocket::readyRead,socket,[socket,request,body] {
                *request+=socket->readAll();
                if (!request->contains("\r\n\r\n")) return;
                request->clear();
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nSet-Cookie: response_private=synthetic-header-value\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);
                socket->disconnectFromHost();
            });
        });
        WorkflowPage p;
        auto doc=WorkflowDocument::templateDocument("blank");
        doc.nodes.insert(1,{"http","http","HTTP",{240,80},{{"method","GET"},{"url",QString("http://127.0.0.1:%1/token").arg(server.serverPort())},{"headers",""},{"body",""},{"connectTimeout","500"},{"timeout","1000"},{"expectedStatus","200"},{"output","response"}}});
        doc.nodes.insert(2,{"log","log","Log",{500,80},{{"text","${response.body."+credentialKey+"}"}}});
        doc.edges={{"e1","n1","http","success"},{"e2","http","log","success"},{"e3","log","n2","success"}};
        QVERIFY(p.setDocument(doc));
        show(p);
        p.startRun();
        QTRY_COMPARE_WITH_TIMEOUT(p.runner()->state(),WorkflowRunState::Completed,2500);
        const auto raw=p.runner()->result("http").output;
        if (raw["body"].isObject())
            QCOMPARE(raw["body"].toObject()[credentialKey],credential);
        QCOMPARE(raw["bodyText"].toString().toUtf8(),body);
        QCOMPARE(QByteArray::fromBase64(raw["bodyBase64"].toString().toLatin1()),body);
        p.selectNode("http",true);
        auto* result=p.findChild<QPlainTextEdit*>("workflowResult");
        const auto displayed=result->toPlainText();
        if (!secret.isEmpty())
            QVERIFY(!displayed.contains(secret.left(128)));
        QVERIFY(!displayed.contains(QString::fromLatin1(body.toBase64())));
        QVERIFY(!displayed.contains("synthetic-header-value"));
        auto* responseTabs=p.findChild<QTabWidget*>("workflowResponseTabs");
        for (int tab : {0,1}) {
            responseTabs->setCurrentIndex(tab);
            auto* pane=p.findChild<QPlainTextEdit*>(tab==0 ? "workflowResponseBody" : "workflowResponseHeaders");
            QVERIFY(pane && pane->isVisible());
            const auto text=pane->toPlainText();
            QVERIFY(text.size()<=34000);
            QVERIFY(!text.contains("synthetic-header-value"));
            if (!secret.isEmpty()) QVERIFY(!text.contains(secret.left(128)));
            if (tab==0) {
                const auto visibleBody=QJsonDocument::fromJson(text.toUtf8()).object();
                if (visibleBody.contains(credentialKey))
                    QCOMPARE(visibleBody[credentialKey].toString(),QString("[已遮蔽]"));
            }
            pane->setFocus();pane->selectAll();
            // Let native OLE rendering/clipboard listeners settle between successive copies.
            for (int attempt=0;attempt<3;++attempt) {
                QTest::qWait(60);QTest::keyClick(pane,Qt::Key_C,Qt::ControlModifier);QTest::qWait(100);
                if (QApplication::clipboard()->text()==text) break;
            }
            QCOMPARE(QApplication::clipboard()->text(),text);
        }
        responseTabs->setCurrentIndex(2);
        for (const char* name : {"workflowLogs","workflowVariables"}) {
            auto* table=p.findChild<QTableView*>(name);
            QVERIFY(table);
            for(int row=0;row<table->model()->rowCount();++row)
                for(int col=0;col<table->model()->columnCount();++col)
                    if (!secret.isEmpty())
                        QVERIFY(!table->model()->index(row,col).data().toString().contains(secret.left(128)));
        }
        result->setFocus();
        result->selectAll();
        QTest::qWait(100);
        QTest::keyClick(result,Qt::Key_C,Qt::ControlModifier);
        QTest::qWait(100);
        QCOMPARE(QApplication::clipboard()->text(),displayed);
        QTemporaryDir directory;
        const auto path=directory.filePath("masked-result.json");
        const bool nativeDisabled=QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QTimer::singleShot(20,&p,[path] {
            auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            dialog->setDirectory(QFileInfo(path).absolutePath());
            dialog->selectFile(QFileInfo(path).fileName());
            QTimer::singleShot(150, dialog, [dialog, path] {
                dialog->selectFile(QFileInfo(path).fileName());
                QTimer::singleShot(50, dialog, [dialog, path] {
                    auto* filename=dialog->findChild<QLineEdit*>("fileNameEdit");
                    QVERIFY(filename);
                    filename->setText(path);
                    QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
                });
            });
        });
        button(p,"workflowResultExport")->click();
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,nativeDisabled);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto contents=file.readAll();
        const auto saved=QJsonDocument::fromJson(contents).object();
        if (raw["body"].isObject())
            QCOMPARE(saved["body"].toObject()[credentialKey].toString(),QString("[已遮蔽]"));
        else
            QCOMPARE(saved["body"].toString(),QString("[大型结构化文本无法安全预览，已遮蔽]"));
        QCOMPARE(saved["bodyBase64"].toString(),QString("[原始字节包含凭据或无法安全检查，已遮蔽]"));
        if (!secret.isEmpty()) {
            QVERIFY(!contents.contains(secret.left(128).toUtf8()));
            QVERIFY(!saved["bodyText"].toString().contains(secret.left(128)));
        }
        if (padding <= 262144) {
            const auto safeBodyText=QJsonDocument::fromJson(saved["bodyText"].toString().toUtf8()).object();
            QCOMPARE(safeBodyText[credentialKey].toString(),QString("[已遮蔽]"));
        } else {
            QCOMPARE(saved["bodyText"].toString(),QString("[大型结构化文本无法安全预览，已遮蔽]"));
        }
        QCOMPARE(p.runner()->result("http").output,raw);
    }
    void realHttpErrorResponseRemainsReviewable() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        int requests = 0;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            auto *socket = server.nextPendingConnection();
            auto data = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket, data] {
                *data += socket->readAll();
                if (!data->contains("\r\n\r\n"))
                    return;
                data->clear();
                ++requests;
                const QByteArray body = "{\"token\":\"http-private-token\",\"error\":\"unauthorized\"}";
                socket->write(
                    "HTTP/1.1 401 Unauthorized\r\nContent-Type: application/json\r\nContent-Length: " +
                    QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        WorkflowPage p;
        auto doc = WorkflowDocument::templateDocument("blank");
        doc.nodes.insert(1, {"http",
                             "http",
                             QStringLiteral("真实 HTTP"),
                             {240, 80},
                             {{"method", "GET"},
                              {"url", QString("http://127.0.0.1:%1/test").arg(server.serverPort())},
                              {"body", ""},
                              {"headers", ""},
                              {"connectTimeout", "500"},
                              {"timeout", "1000"},
                              {"expectedStatus", "200"},
                              {"output", "response"}}});
        doc.edges = {{"e1", "n1", "http", "success"}, {"e2", "http", "n2", "success"}};
        doc.nodes[0].position={40,108};doc.nodes.last().position={500,108};
        QVERIFY(p.setDocument(doc));
        show(p);
        QCOMPARE(requests, 0);
        QTest::mouseClick(button(p, "workflowRun"), Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(p.runner()->state(), WorkflowRunState::Failed, 2500);
        QCOMPARE(requests, 1);
        QCOMPARE(p.runner()->result("http").output["status"].toInt(), 401);
        QCOMPARE(p.selectedNodeId(), QString("http"));
        auto text = p.findChild<QPlainTextEdit *>("workflowResult")->toPlainText();
        QVERIFY(text.contains("401"));
        QVERIFY(text.contains("unauthorized"));
        QVERIFY(!text.contains("http-private-token"));
        auto* responseTabs=p.findChild<QTabWidget*>("workflowResponseTabs");
        QVERIFY(responseTabs);
        QVERIFY(responseTabs->isTabVisible(0) && responseTabs->isTabVisible(1));
        QVERIFY(p.findChild<QLabel*>("workflowResultStatus")->text().contains("401"));
        auto* bodyPreview=p.findChild<QPlainTextEdit*>("workflowResponseBody");
        QVERIFY(bodyPreview->toPlainText().contains("unauthorized"));
        QVERIFY(!bodyPreview->toPlainText().contains("http-private-token"));
        auto* headerPreview=p.findChild<QPlainTextEdit*>("workflowResponseHeaders");
        QVERIFY(headerPreview->toPlainText().contains("content-type"));
        responseTabs->setCurrentIndex(0);
        QVERIFY(bodyPreview->isVisible());
        responseTabs->setCurrentIndex(1);
        QVERIFY(headerPreview->isVisible());
        auto dir=qEnvironmentVariable("PORTBRIDGE_WORKFLOW_SCREENSHOT_DIR");
        if (!dir.isEmpty()) {
            QDir().mkpath(dir);
            p.resize(1440,1000);
            for (bool dark : {true,false}) {
                p.setDarkTheme(dark);
                responseTabs->setCurrentIndex(0);QTest::qWait(30);
                QVERIFY(p.grab().save(dir+QString("/http-body-%1.png").arg(dark?"dark":"light")));
                responseTabs->setCurrentIndex(1);QTest::qWait(30);
                QVERIFY(p.grab().save(dir+QString("/http-headers-%1.png").arg(dark?"dark":"light")));
            }
        }
        doc.nodes[1].parameters["expectedStatus"] = "any";
        QVERIFY(p.setDocument(doc));
        QTest::mouseClick(button(p, "workflowRun"), Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(p.runner()->state(), WorkflowRunState::Completed, 2500);
        QCOMPARE(requests, 2);
        QCOMPARE(p.runner()->variables()["response"].toObject()["status"].toInt(), 401);
    }
    void realWebSocketTextAndBinaryResults() {
        QWebSocketServer server(QStringLiteral("workflow-ui-peer"),QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        int messages = 0;
        connect(&server,&QWebSocketServer::newConnection,this,[&] {
            auto* peer=server.nextPendingConnection();
            QVERIFY(peer);peer->setParent(&server);
            connect(peer,&QWebSocket::textMessageReceived,peer,[&,peer](const QString& text) {
                ++messages;peer->sendTextMessage(text);
            });
            connect(peer,&QWebSocket::binaryMessageReceived,peer,[&,peer](const QByteArray& bytes) {
                ++messages;peer->sendBinaryMessage(bytes);
            });
            connect(peer,&QWebSocket::disconnected,peer,&QObject::deleteLater);
        });
        const auto binaryPayload=QByteArray::fromHex("000102")+QByteArray(18000,'x')+QByteArray("late-payload-tail");
        const auto binaryHex=QString::fromLatin1(binaryPayload.toHex(' '));
        for (bool binary : {false, true}) {
            WorkflowPage p;
            auto doc = WorkflowDocument::templateDocument("blank");
            doc.nodes.insert(1, {"ws",
                                 "ws",
                                 QStringLiteral("真实 WebSocket"),
                                 {240, 80},
                                 {{"url", QString("ws://127.0.0.1:%1/echo").arg(server.serverPort())},
                                  {"headers", ""},
                                  {"timeout", "1000"}}});
            doc.nodes.insert(2, {"message",
                                 "sendWait",
                                 QStringLiteral("消息往返"),
                                 {500, 80},
                                 {{"resource", "ws"},
                                  {"messageType", binary ? "binary" : "text"},
                                  {"format", binary ? "HEX" : "text"},
                                  {"payload", binary ? binaryHex : QString("ping")},
                                  {"match", "equals"},
                                  {"expected", binary ? binaryHex : QString("ping")},
                                  {"expectedFormat", binary ? "HEX" : "UTF-8"},
                                  {"timeout", "1000"},
                                  {"output", "echo"}}});
            doc.nodes.insert(3, {"close",
                                 "close",
                                 QStringLiteral("关闭"),
                                 {760, 80},
                                 {{"code", "1000"}, {"reason", "test complete"}}});
            doc.edges = {{"e1", "n1", "ws", "success"},
                         {"e2", "ws", "message", "success"},
                         {"e3", "message", "close", "success"},
                         {"e4", "close", "n2", "success"}};
            QVERIFY(p.setDocument(doc));
            show(p);
            QTest::mouseClick(button(p, "workflowRun"), Qt::LeftButton);
            QTRY_COMPARE_WITH_TIMEOUT(p.runner()->state(), WorkflowRunState::Completed, 3000);
            auto output = p.runner()->result("message").output;
            QCOMPARE(output["binary"].toBool(), binary);
            QCOMPARE(QByteArray::fromBase64(output["bytesBase64"].toString().toLatin1()),
                     binary ? binaryPayload : QByteArray("ping"));
            p.selectNode("message", true);
            QVERIFY(p.findChild<QPlainTextEdit *>("workflowResult")->toPlainText().contains("bytesBase64"));
            auto* responseTabs=p.findChild<QTabWidget*>("workflowResponseTabs");
            responseTabs->setCurrentIndex(0);
            auto* messagePreview=p.findChild<QPlainTextEdit*>("workflowResponseBody");
            QVERIFY(messagePreview->isVisible());
            const auto visible=messagePreview->toPlainText();
            QVERIFY(visible.size()<14000);
            if (binary) {
                QVERIFY(visible.contains(QString::number(binaryPayload.size())));
                QVERIFY(visible.contains("00 01 02"));
                QVERIFY(!visible.contains(QString::fromLatin1(QByteArray("late-payload-tail").toHex(' '))));
            } else QVERIFY(visible.contains("ping"));
            QCOMPARE(p.runner()->result("message").output,output);
        }
        QCOMPARE(messages, 2);
    }
#endif
    void logPreferenceTracksExplicitCollapseAfterParentHidden() {
        QSettings().clear();
        {
            WorkflowPage p;show(p);
            QVERIFY(!p.findChild<QTabWidget*>("workflowLogTabs")->isHidden());
            p.hide();
        }
        QVERIFY(!QSettings().value("workflow/logCollapsed").toBool());
        {
            WorkflowPage p;show(p);
            auto* tabs=p.findChild<QTabWidget*>("workflowLogTabs");
            QVERIFY(tabs->isVisible());
            QTest::mouseClick(button(p,"workflowLogToggle"),Qt::LeftButton);
            QVERIFY(tabs->isHidden());p.hide();
        }
        QVERIFY(QSettings().value("workflow/logCollapsed").toBool());
        {
            WorkflowPage p;show(p,{1280,900});
            QVERIFY(p.findChild<QTabWidget*>("workflowLogTabs")->isHidden());
            auto* splitter=p.findChild<QSplitter*>("workflowVerticalSplitter");
            QVERIFY(splitter->sizes().at(1)<=50);
            QVERIFY(splitter->sizes().at(0)>std::max(300,p.height()-260));
            QTest::mouseClick(button(p,"workflowLogToggle"),Qt::LeftButton);
            QVERIFY(p.findChild<QTabWidget*>("workflowLogTabs")->isVisible());
            QVERIFY(!p.runner()->active());
        }
        QSettings().clear();
    }
    void layoutsThemesAndScreenshot() {
        WorkflowPage p;
        show(p);
        auto *stop = button(p, "workflowStop");
        QVERIFY(stop->isVisible());
        QVERIFY(p.graphView()->viewport()->width() >= 250);
        QVERIFY(p.graphView()->getScale() >= .85);
        for (auto id : p.graphModel()->allNodeIds()) {
            for (const auto& edge : p.graphModel()->allConnectionIds(id)) {
                auto* object=p.graphScene()->connectionGraphicsObject(edge);
                QVERIFY(object);
                auto stroke=p.graphScene()->connectionPainter().getPainterStroke(*object);
                QVERIFY(object->boundingRect().adjusted(-1,-1,1,1).contains(stroke.boundingRect()));
            }
        }
        for (bool dark : {true, false}) {
            p.setDarkTheme(dark);
            QCoreApplication::processEvents();
            auto node = p.graphModel()->node(p.graphModel()->graphId(p.selectedNodeId()));
            QVERIFY(node);
            const auto scenePoint = node->position + QPointF(100, 95);
            auto pixel = p.graphView()->mapFromScene(scenePoint);
            auto image = p.graphView()->viewport()->grab().toImage();
            if (p.graphView()->viewport()->rect().contains(pixel)) {
                auto color = image.pixelColor(qRound(pixel.x() * image.devicePixelRatio()),
                                              qRound(pixel.y() * image.devicePixelRatio()));
                QVERIFY(dark ? color.lightness() < 90 : color.lightness() > 220);
            }
            QVERIFY(p.rect().contains(p.mapFromGlobal(stop->mapToGlobal(stop->rect().center()))));
            auto dir = qEnvironmentVariable("PORTBRIDGE_WORKFLOW_SCREENSHOT_DIR");
            if (!dir.isEmpty()) {
                QDir().mkpath(dir);
                QVERIFY(p.grab().save(dir + (dark ? "/native-editor-dark.png" : "/native-editor-light.png")));
                auto *screen = p.screen();
                QJsonObject receipt{{"requestedWidth", 1024},
                                    {"requestedHeight", 768},
                                    {"actualWidth", p.width()},
                                    {"actualHeight", p.height()},
                                    {"devicePixelRatio", p.devicePixelRatioF()},
                                    {"logicalDpi", screen->logicalDotsPerInch()},
                                    {"availableScreenWidth", screen->availableGeometry().width()},
                                    {"availableScreenHeight", screen->availableGeometry().height()},
                                    {"canvasWidth", p.graphView()->viewport()->width()},
                                    {"canvasHeight", p.graphView()->viewport()->height()},
                                    {"zoom", p.graphView()->getScale()}};
                QFile file(dir + "/native-screen.json");
                QVERIFY(file.open(QIODevice::WriteOnly));
                file.write(QJsonDocument(receipt).toJson());
                if (p.devicePixelRatioF() == 1.) {
                    for (const auto& size : QList<QSize>{{1440,1000},{1280,900}}) {
                        p.resize(size);
                        QTest::qWait(30);
                        p.graphView()->centerOn(p.graphScene()->itemsBoundingRect().center());
                        const auto name=QString("/native-editor-%1-%2x%3.png").arg(dark?"dark":"light").arg(p.width()).arg(p.height());
                        QVERIFY(p.grab().save(dir+name));
                    }
                    p.resize(1024,768);
                    QTest::qWait(30);
                }
            }
        }
        QTest::mouseClick(button(p, "workflowLogToggle"), Qt::LeftButton);
        QVERIFY(stop->isVisible());
        QVERIFY(!p.findChild<QTabWidget *>("workflowLogTabs")->isVisible());
        QTest::mouseClick(button(p, "workflowLogToggle"), Qt::LeftButton);
        QVERIFY(p.findChild<QTabWidget *>("workflowLogTabs")->isVisible());
    }
};
QTEST_MAIN(WorkflowUiTest)
#include "test_workflow_ui.moc"
