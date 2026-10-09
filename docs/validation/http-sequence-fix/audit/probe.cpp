#include "ui/http_assertions.hpp"
#include "ui/http_project_store.hpp"
#include "ui/http_sequence_runner.hpp"
#include <QApplication>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>
using namespace portbridge;
int main(int argc,char** argv){
    QApplication app(argc,argv);
    QJsonObject output;
    auto rule=[](QString path,QJsonValue expected){return QJsonObject{{"enabled",true},{"source","json"},{"path",path},{"relation","equals"},{"expected",expected}};};
    const QJsonObject element{{"id",7}};
    QJsonArray nestedRules{rule("$.data[0].id",7)};
    QJsonArray rootRules{rule("$[0].id",7)};
    output["nestedArrayPathValidation"]=HttpAssertions::validate(nestedRules);
    output["nestedArrayPassed"]=HttpAssertions::passed(HttpAssertions::evaluate({{"body",QJsonObject{{"data",QJsonArray{element}}}}},nestedRules));
    output["rootArrayPathValidation"]=HttpAssertions::validate(rootRules);
    output["rootArrayPassed"]=HttpAssertions::passed(HttpAssertions::evaluate({{"body",QJsonArray{element}}},rootRules));
    HttpProjectStore store(nullptr);
    QString error;
    const QString large(20000,'x');
    output["variableAccepted"]=store.setEnvironmentVariables({QJsonObject{{"name","large"},{"value",large},{"type","string"},{"secret",false}}},&error);
    output["variableError"]=error;
    QJsonArray templates{rule("$.text","{{large}}")};
    auto resolved=HttpAssertions::resolve(templates,store,&error);
    output["resolveError"]=error;
    output["resolvedExpectedCharacters"]=(resolved.isEmpty()?0:resolved.first().toObject().value("expected").toString().size());
    output["resolvedRulesValidation"]=HttpAssertions::validate(resolved);
    output["invalidResolvedRulesRejected"]=resolved.isEmpty()&&!error.isEmpty();
    QTcpServer server;
    if(!server.listen(QHostAddress::LocalHost,0)) return 2;
    int requests=0;
    QObject::connect(&server,&QTcpServer::newConnection,&server,[&]{
        while(server.hasPendingConnections()){
            auto* socket=server.nextPendingConnection();
            auto bytes=std::make_shared<QByteArray>();
            QObject::connect(socket,&QTcpSocket::readyRead,socket,[&,socket,bytes]{
                *bytes+=socket->readAll();
                if(socket->property("answered").toBool()||!bytes->contains("\r\n\r\n")) return;
                socket->setProperty("answered",true);++requests;
                const auto body=QJsonDocument(QJsonObject{{"text",large}}).toJson(QJsonDocument::Compact);
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);
                socket->disconnectFromHost();
            });
        }
    });
    ProtocolDebugSession session(ProtocolDebugSession::Mode::Http);
    HttpSequenceRunner runner(&store,&session);
    QJsonObject request{{"schemaVersion",1},{"kind","http"},{"id","audit-request"},{"projectId",store.projectId()},
        {"params",QJsonObject{{"url",QString("http://127.0.0.1:%1/").arg(server.serverPort())}}},
        {"assertions",templates}};
    output["sequenceAcceptedInvalidResolvedRules"]=runner.start({request},false,&error);
    output["sequenceStartError"]=error;
    QElapsedTimer elapsed;elapsed.start();
    while(runner.running()&&elapsed.elapsed()<5000){app.processEvents();QThread::msleep(1);}
    if(runner.running()){runner.stop();return 3;}
    output["actualRequestsWithInvalidResolvedRules"]=requests;
    output["sequenceReport"]=runner.report();
    QJsonArray overSteps;
    for(int i=0;i<129;++i)overSteps.append(request);
    output["over128Rejected"]=!runner.start(overSteps,false,&error);
    auto overBytes=request;overBytes["auditPadding"]=QString(8*1024*1024,'x');
    output["over8MiBRejected"]=!runner.start({overBytes},false,&error);
    QJsonArray overAssertions;
    for(int i=0;i<33;++i)overAssertions.append(rule("$.id",7));
    output["over32Rejected"]=!HttpAssertions::validate(overAssertions).isEmpty();
    output["requestsAfterBoundaryRejections"]=requests;
    std::cout<<QJsonDocument(output).toJson(QJsonDocument::Indented).toStdString();
    return 0;
}
