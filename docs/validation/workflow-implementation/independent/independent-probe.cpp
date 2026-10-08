#include "portbridge/workflow.hpp"
#include "ui/workflow_page.hpp"
#include "ui/workflow_canvas.hpp"
#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QFile>
#include <QSettings>
#include <QTableView>
#include <QJsonDocument>
#include <QFileDialog>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
using namespace portbridge;
namespace {
WorkflowNode node(QString id, QString type, QJsonObject p = {}) { return {id,type,id,{},p}; }
WorkflowDocument linear(QVector<WorkflowNode> ns) { WorkflowDocument d; d.id="C-independent"; d.name="Independent"; d.nodes=ns; for(int i=1;i<ns.size();++i) d.edges.append({QString::number(i),ns[i-1].id,ns[i].id,"success"}); return d; }
ConnectionConfig udp(quint16 port) { ConnectionConfig c; c.name="C local UDP"; c.localAddress="127.0.0.1"; c.localPort=0; c.remotePort=port; return c; }
}
class Independent : public QObject {
 Q_OBJECT
private slots:
 void initTestCase() { QApplication::setAttribute(Qt::AA_DontUseNativeDialogs); QCoreApplication::setOrganizationName("CIndependentReview"); QCoreApplication::setApplicationName("Isolated"); QSettings().clear(); }
 void tcpFragmentsAndCoalescedFrames_data() { QTest::addColumn<QString>("mode"); QTest::newRow("delimiter")<<"delimiter"; QTest::newRow("fixed")<<"fixed"; QTest::newRow("lengthHeader")<<"lengthHeader"; }
 void tcpFragmentsAndCoalescedFrames() {
  QFETCH(QString,mode); QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost)); QPointer<QTcpSocket> socket;
  connect(&server,&QTcpServer::newConnection,this,[&]{socket=server.nextPendingConnection();});
  SessionController session; auto c=udp(server.serverPort()); c.kind=TransportKind::TcpClient; session.start(c); QTRY_VERIFY(session.connected() && socket); session.setDisplayPaused(true); session.setHighSpeed(true);
  QJsonObject f; QByteArray first,tail;
  if(mode=="delimiter") {f={{"mode","delimiter"},{"delimiter","0A"},{"delimiterFormat","HEX"}};first="ignore\nre";tail="ady\nother\n";}
  if(mode=="fixed") {f={{"mode","fixed"},{"length",5}};first="nope!re";tail="adyother";}
  if(mode=="lengthHeader") {f={{"mode","lengthHeader"},{"headerBytes",2},{"offset",0},{"byteOrder","big"}};first=QByteArray::fromHex("0004")+"nope"+QByteArray::fromHex("0005")+"re";tail="ady"+QByteArray::fromHex("0005")+"other";}
  auto d=linear({node("s","start"),node("r","raw"),node("w","wait",{{"match","equals"},{"expected","ready"},{"framing",f},{"timeout","1000"}}),node("e","end")}); WorkflowRunner runner; runner.setSessionProvider([&]{return &session;}); QString why; QVERIFY2(runner.start(d,&why),qPrintable(why)); QTRY_COMPARE(runner.activeNodeId(),QString("w")); socket->write(first); socket->flush(); QTest::qWait(30); QCOMPARE(runner.state(),WorkflowRunState::Running); socket->write(tail); socket->flush(); QTRY_COMPARE_WITH_TIMEOUT(runner.state(),WorkflowRunState::Completed,2000); QCOMPARE(runner.result("w").output["text"].toString(),QString("ready")); QVERIFY(session.connected()); QVERIFY(session.takeDisplayRecords().empty());
 }
 void tcpServerFiltersIndependentStreamsAndDisconnect() {
  SessionController session; auto c=udp(9); c.kind=TransportKind::TcpServer; session.start(c); QTRY_VERIFY(session.connected());
  QTcpSocket chosen,other; chosen.connectToHost(QHostAddress::LocalHost,session.localEndpoint().port); other.connectToHost(QHostAddress::LocalHost,session.localEndpoint().port); QTRY_VERIFY(chosen.state()==QAbstractSocket::ConnectedState && other.state()==QAbstractSocket::ConnectedState); QTRY_COMPARE(session.clients().size(),size_t(2));
  quint64 chosenId=0; for(auto client:session.clients()) if(client.peer.port==chosen.localPort()) chosenId=client.id; QVERIFY(chosenId);
  auto d=linear({node("s","start"),node("r","raw"),node("w","wait",{{"match","equals"},{"expected","ready"},{"timeout","1000"},{"framing",QJsonObject{{"mode","delimiter"},{"delimiter","0A"},{"delimiterFormat","HEX"}}},{"sourceFilter",QJsonObject{{"clientId",QString::number(chosenId)}}}}),node("e","end")});
  WorkflowRunner runner; runner.setSessionProvider([&]{return &session;}); QString why; QVERIFY2(runner.start(d,&why),qPrintable(why)); QTRY_COMPARE(runner.activeNodeId(),QString("w")); other.write("ready\n"); other.flush(); QTest::qWait(50); QVERIFY(runner.active()); other.disconnectFromHost(); QTest::qWait(30); QVERIFY(runner.active()); chosen.write("re"); chosen.flush(); QTest::qWait(20); chosen.write("ady\n"); chosen.flush(); QTRY_COMPARE_WITH_TIMEOUT(runner.state(),WorkflowRunState::Completed,2000); QVERIFY(session.connected()); QCOMPARE(runner.result("w").output["text"].toString(),QString("ready"));
 }
 void udpSilentLifecycleAdmissionAndReplacementEpoch() {
  QUdpSocket peer; QVERIFY(peer.bind(QHostAddress::LocalHost,0)); SessionController session; const auto c=udp(peer.localPort()); session.selectConfiguration(c); session.start(c); QTRY_VERIFY(session.connected()); QTest::qWait(50); QVERIFY(!peer.hasPendingDatagrams()); QVERIFY(session.takeDisplayRecords().empty());
  WorkflowRunner runner; runner.setSessionProvider([&]{return &session;}); auto d=linear({node("s","start"),node("r","raw"),node("tx","send",{{"format","text"},{"payload","one"}}),node("pause","delay",{{"duration","5000"}}),node("e","end")}); QString why; QVERIFY2(runner.start(d,&why),qPrintable(why)); QTRY_COMPARE(runner.activeNodeId(),QString("pause")); const auto result=runner.result("tx").output; QVERIFY(result["admitted"].toBool()); QVERIFY(!result["localWriteCompleted"].toBool()); QVERIFY(!result["peerAcknowledged"].toBool()); QTRY_VERIFY(peer.hasPendingDatagrams()); QByteArray bytes(32,'\0'); const auto len=peer.readDatagram(bytes.data(),bytes.size()); QCOMPARE(bytes.left(len),QByteArray("one")); runner.stop(); QVERIFY(session.connected());
  session.stop(); QTest::qWait(30); QVERIFY(!peer.hasPendingDatagrams());
  auto owned=linear({node("s","start"),node("r","raw",{{"ownership","owned"},{"config",workflowConnectionConfigToJson(c)}}),node("pause","delay",{{"duration","5000"}}),node("e","end")}); QVERIFY2(runner.start(owned,&why),qPrintable(why)); QTRY_COMPARE(runner.activeNodeId(),QString("pause")); auto replacement=c; replacement.name="Manual replacement"; session.start(replacement); const auto epoch=session.sessionEpoch(); QTRY_VERIFY(session.connected()); QTRY_COMPARE(runner.state(),WorkflowRunState::Failed); QCOMPARE(session.sessionEpoch(),epoch); QVERIFY(session.connected());
 }
 void observerOverflowExplicitFailure() {
  QUdpSocket peer; QVERIFY(peer.bind(QHostAddress::LocalHost,0)); SessionController session; session.start(udp(peer.localPort())); QTRY_VERIFY(session.connected()); WorkflowRunner runner; runner.setSessionProvider([&]{return &session;}); auto d=linear({node("s","start"),node("r","raw"),node("w","wait",{{"match","any"},{"timeout","1000"}}),node("e","end")}); d.limits.observerQueueBytes=256; QString why; QVERIFY2(runner.start(d,&why),qPrintable(why)); QTRY_COMPARE(runner.activeNodeId(),QString("w")); peer.writeDatagram(QByteArray(4096,'x'),QHostAddress::LocalHost,session.localEndpoint().port); QTRY_COMPARE(runner.state(),WorkflowRunState::Failed); QVERIFY(!runner.result("w").detail.isEmpty()); QVERIFY(!runner.variables().contains("message")); QVERIFY(session.connected());
 }
 void longCredentialBase64PreviewAndExportMasked_data() { QTest::addColumn<bool>("exportFile"); QTest::addColumn<int>("variant"); QTest::newRow("preview") << false << 0; QTest::newRow("actual-export") << true << 0; QTest::newRow("numeric-preview") << false << 1; QTest::newRow("numeric-actual-export") << true << 1; QTest::newRow("escaped-key-preview") << false << 2; QTest::newRow("escaped-key-export") << true << 2; QTest::newRow("large-credential-key-preview") << false << 3; QTest::newRow("large-credential-key-export") << true << 3; }
 void longCredentialBase64PreviewAndExportMasked() {
  QFETCH(bool, exportFile); QFETCH(int, variant);
  QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost)); const QString secret=(variant==1||variant==2)?QString("1234567890123"):QString(5000,'s')+"private-tail"; QByteArray body=QJsonDocument(QJsonObject{{"token",variant==1?QJsonValue(secret.toDouble()):QJsonValue(secret)}}).toJson(QJsonDocument::Compact);
  if(variant==2) body="{\"to\\u006Ben\":1234567890123}";
  if(variant==3) body=QJsonDocument(QJsonObject{{"a_padding",QString(300000,'p')},{"clientCredential",secret}}).toJson(QJsonDocument::Compact);
  connect(&server,&QTcpServer::newConnection,this,[&]{auto* s=server.nextPendingConnection();connect(s,&QTcpSocket::readyRead,s,[s,body]{if(!s->readAll().contains("\r\n\r\n"))return;s->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);s->disconnectFromHost();});});
  WorkflowPage page; auto d=linear({node("s","start"),node("h","http",{{"url",QString("http://127.0.0.1:%1/token").arg(server.serverPort())},{"method","GET"},{"body",""},{"headers",""},{"connectTimeout","500"},{"timeout","1000"}}),node("e","end")}); QVERIFY(page.setDocument(d)); page.resize(1024,768);page.show();QTest::qWait(50);page.startRun();QTRY_COMPARE(page.runner()->state(),WorkflowRunState::Completed);page.selectNode("h",true); const auto shown=page.findChild<QPlainTextEdit*>("workflowResult")->toPlainText();
  if (!exportFile) { QVERIFY2(!shown.contains(QString::fromLatin1(body.toBase64())),"HTTP raw base64 preview exposes a credential"); return; }
  QTemporaryDir directory; const auto output=directory.filePath("redacted-result.json");
  QTimer::singleShot(0,&page,[&] { auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget()); if(!dialog) qFatal("Expected actual result export dialog"); dialog->selectFile(output); static_cast<QDialog*>(dialog)->accept(); });
  page.findChild<QPushButton*>("workflowResultExport")->click(); QFile file(output); QVERIFY(file.open(QIODevice::ReadOnly)); const auto saved=QJsonDocument::fromJson(file.readAll()).object();
  const auto decoded=QByteArray::fromBase64(saved["bodyBase64"].toString().toLatin1()); QVERIFY2(!decoded.contains(secret.toUtf8()),"Actual redacted result export preserves credential in bodyBase64");
 }
 void nativeLayoutScreenshots() {
  const auto directory=qEnvironmentVariable("INDEPENDENT_SCREENSHOTS"); if(directory.isEmpty()) return; QDir().mkpath(directory); QJsonArray receipts;
  for(const auto size : {QSize(1440,1000),QSize(1024,768)}) for(bool dark : {true,false}) {
   WorkflowPage page; page.resize(size); page.setDarkTheme(dark); page.show(); QTest::qWait(80); const auto scale=page.devicePixelRatioF(); const auto label=QString("%1-%2x%3-scale%4").arg(dark?"dark":"light").arg(size.width()).arg(size.height()).arg(scale);
   QVERIFY(page.graphView()->getScale()>=.85); auto* stop=page.findChild<QPushButton*>("workflowStop"); QVERIFY(stop->isVisible()); QVERIFY(page.rect().contains(page.mapFromGlobal(stop->mapToGlobal(stop->rect().center())))); QVERIFY(page.graphView()->viewport()->width()>=250);
   QVERIFY(page.grab().save(directory+"/"+label+".png")); page.graphView()->centerOn(QPointF(450,380)); QTest::qWait(30); QVERIFY(page.graphView()->grab().save(directory+"/labels-"+label+".png")); receipts.append(QJsonObject{{"file",label+".png"},{"width",page.width()},{"height",page.height()},{"dpr",scale},{"logicalDpi",page.screen()->logicalDotsPerInch()},{"zoom",page.graphView()->getScale()},{"canvasWidth",page.graphView()->viewport()->width()},{"canvasHeight",page.graphView()->viewport()->height()}});
  }
  QFile file(directory+"/native-layout-"+qEnvironmentVariable("QT_SCALE_FACTOR","1")+".json"); QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(receipts).toJson(QJsonDocument::Indented));
 }
};
QTEST_MAIN(Independent)
#include "independent.moc"
