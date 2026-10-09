#include "ui/http_project_store.hpp"
#include "ui/http_sequence_runner.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <iostream>
using namespace portbridge;
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (argc < 2)
        return 2;
    const QString output = QString::fromLocal8Bit(argv[1]);
    HttpProjectStore store(nullptr);
    ProtocolDebugSession session(ProtocolDebugSession::Mode::Http);
    HttpSequenceRunner runner(&store, &session);
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0))
        return 3;
    int requests = 0;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (server.hasPendingConnections()) {
            auto *socket = server.nextPendingConnection();
            auto input = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, input] {
                *input += socket->readAll();
                if (socket->property("scheduled").toBool() || !input->contains("\r\n\r\n"))
                    return;
                socket->setProperty("scheduled", true);
                ++requests;
                QTimer::singleShot(47000, socket, [socket] {
                    if (socket->state() != QAbstractSocket::ConnectedState)
                        return;
                    socket->write(
                        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                        "2\r\nConnection: close\r\n\r\n{}");
                    socket->disconnectFromHost();
                });
            });
        }
    });
    QJsonArray plan;
    for (int i = 0; i < 128; ++i)
        plan.append(QJsonObject{
            {"schemaVersion", 1},
            {"kind", "http"},
            {"id", QString("deadline-%1").arg(i)},
            {"projectId", store.projectId()},
            {"params",
             QJsonObject{{"url", QString("http://127.0.0.1:%1/slow").arg(server.serverPort())},
                         {"timeoutMs", 60000}}}});
    QString error;
    QElapsedTimer wall;
    wall.start();
    if (!runner.start(plan, false, &error)) {
        std::cerr << error.toStdString();
        return 4;
    }
    QElapsedTimer heartbeat;
    heartbeat.start();
    auto write = [&](QJsonObject result) {
        QFile file(output);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        file.write(QJsonDocument(result).toJson());
        return true;
    };
    while (runner.running() && wall.elapsed() < 620000) {
        app.processEvents();
        QThread::msleep(2);
        if (heartbeat.elapsed() > 1000) {
            write({{"state", "running"},
                   {"elapsedMs", wall.elapsed()},
                   {"requestsReceived", requests},
                   {"completedSteps", runner.results().size()},
                   {"limitMs", 600000}});
            heartbeat.restart();
        }
    }
    const auto stoppedMs = wall.elapsed();
    const int stoppedRequests = requests;
    const auto frozenReport = runner.report();
    const bool stoppedNaturally = !runner.running();
    if (!stoppedNaturally)
        runner.stop();
    QElapsedTimer late;
    late.start();
    while (late.elapsed() < 15000) {
        app.processEvents();
        QThread::msleep(2);
    }
    const bool passed = stoppedNaturally && stoppedMs >= 570000 && stoppedMs <= 605000 &&
                        !session.active() && frozenReport.value("cancelled").toBool() &&
                        frozenReport.value("counts").toObject().value("cancelled").toInt() == 1 &&
                        requests == stoppedRequests && runner.report() == frozenReport;
    write({{"state", passed ? "passed" : "failed"},
           {"test", "production 600000ms sequence deadline; actual wall-clock execution"},
           {"limitMs", 600000},
           {"observedStopMs", stoppedMs},
           {"lateObservationMs", late.elapsed()},
           {"requestsReceived", requests},
           {"noFurtherRequests", requests == stoppedRequests},
           {"reportUnchangedAfterLateServerTimer", runner.report() == frozenReport},
           {"sessionReleased", !session.active()},
           {"report", runner.report()}});
    std::cout << QJsonDocument(runner.report()).toJson().toStdString();
    return passed ? 0 : 1;
}
