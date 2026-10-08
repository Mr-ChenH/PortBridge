// Research-only compatibility probe. No PortBridge session or workflow executor.
#include <QApplication>
#include <memory>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSslSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <QWebSocket>
#include <QWebSocketServer>
#include <QtNodes/BasicGraphicsScene>
#include <QtNodes/GraphicsView>
#include "SimpleGraphModel.hpp"

class ProbeModel final : public SimpleGraphModel {
public:
 QVariant nodeData(NodeId id,NodeRole role) const override {
  if(role==NodeRole::Caption)return id==0?QStringLiteral("发送数据 · Send"):QStringLiteral("等待响应 · Wait");
  return SimpleGraphModel::nodeData(id,role);
 }
};
int main(int argc,char** argv){
 QApplication app(argc,argv);bool httpOk=false,wsOk=false,dragOk=false,jsonOk=false,screenshotOk=false;
 ProbeModel model;const auto first=model.addNode(),second=model.addNode();
 model.setNodeData(first,NodeRole::Position,QPointF(0,0));model.setNodeData(second,NodeRole::Position,QPointF(400,0));model.addConnection({first,0,second,0});
 QtNodes::BasicGraphicsScene scene(model);QtNodes::GraphicsView view(&scene);view.resize(1000,560);view.setWindowTitle(QStringLiteral("QtNodes 适配验证 · 非最终工作流界面"));view.show();
 QTimer::singleShot(150,[&]{
  view.fitInView(scene.itemsBoundingRect().adjusted(-80,-80,80,80),Qt::KeepAspectRatio);
  const auto before=model.nodeData(first,NodeRole::Position).toPointF();const auto from=view.mapFromScene(before+QPointF(70,20));const auto to=from+QPoint(55,40);
  QTest::mousePress(view.viewport(),Qt::LeftButton,Qt::NoModifier,from);
  QMouseEvent move(QEvent::MouseMove,QPointF(to),QPointF(view.viewport()->mapToGlobal(to)),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view.viewport(),&move);QTest::mouseRelease(view.viewport(),Qt::LeftButton,Qt::NoModifier,to);
  const auto after=model.nodeData(first,NodeRole::Position).toPointF();dragOk=after!=before;const auto saved=model.saveNode(first);ProbeModel restored;restored.loadNode(saved);jsonOk=restored.nodeData(first,NodeRole::Position).toPointF()==after;
  QTimer::singleShot(60,[&]{screenshotOk=view.grab().save(QString::fromLocal8Bit(argv[1]));});
 });
 QTcpServer httpServer;httpServer.listen(QHostAddress::LocalHost,0);
 QObject::connect(&httpServer,&QTcpServer::newConnection,&app,[&]{
  auto* socket=httpServer.nextPendingConnection();auto request=std::make_shared<QByteArray>();
  QObject::connect(socket,&QTcpSocket::readyRead,socket,[socket,request]{*request+=socket->readAll();if(!request->contains("\r\n\r\n"))return;const QByteArray body="{\"token\":\"demo-token\"}";socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);socket->disconnectFromHost();});QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
 });
 QWebSocketServer wsServer(QStringLiteral("research"),QWebSocketServer::NonSecureMode);wsServer.listen(QHostAddress::LocalHost,0);
 QObject::connect(&wsServer,&QWebSocketServer::newConnection,&app,[&]{auto* socket=wsServer.nextPendingConnection();QObject::connect(socket,&QWebSocket::textMessageReceived,socket,[socket](const QString& msg){if(msg==QStringLiteral("subscribe"))socket->sendTextMessage(QStringLiteral("{\"ok\":true}"));});QObject::connect(socket,&QWebSocket::disconnected,socket,&QObject::deleteLater);});
 QWebSocket ws;
 QObject::connect(&ws,&QWebSocket::connected,&app,[&]{ws.sendTextMessage(QStringLiteral("subscribe"));});
 QObject::connect(&ws,&QWebSocket::textMessageReceived,&app,[&](const QString& msg){wsOk=QJsonDocument::fromJson(msg.toUtf8()).object()["ok"].toBool();ws.close();});
 QNetworkAccessManager manager;auto* reply=manager.get(QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1/token").arg(httpServer.serverPort()))));
 QObject::connect(reply,&QNetworkReply::finished,&app,[&]{httpOk=reply->error()==QNetworkReply::NoError&&reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()==200&&QJsonDocument::fromJson(reply->readAll()).object()["token"].toString()==QStringLiteral("demo-token");reply->deleteLater();if(httpOk)ws.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/").arg(wsServer.serverPort())));});
 QTimer finish;finish.start(100);QObject::connect(&finish,&QTimer::timeout,&app,[&]{if(httpOk&&wsOk&&screenshotOk)app.quit();});QTimer::singleShot(5000,&app,&QApplication::quit);app.exec();
 const bool success=httpOk&&wsOk&&dragOk&&jsonOk&&screenshotOk;
 QFile result(QString::fromLocal8Bit(argv[2]));result.open(QIODevice::WriteOnly);result.write(QJsonDocument(QJsonObject{{"success",success},{"qtnodes_tag","3.0.16"},{"qt_version",qVersion()},{"nodes",int(model.allNodeIds().size())},{"connections",int(model.allConnectionIds(first).size())},{"mouse_drag_moves_node",dragOk},{"node_position_json_roundtrip",jsonOk},{"http_local_response",httpOk},{"websocket_text_roundtrip",wsOk},{"ssl_backend",QSslSocket::activeBackend()},{"ssl_supported",QSslSocket::supportsSsl()},{"https_wss_connection_tested",false},{"screenshot",screenshotOk}}).toJson());return success?0:1;
}
