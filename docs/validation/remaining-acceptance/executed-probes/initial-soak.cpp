#include "portbridge/session_controller.hpp"
#include "portbridge/network_engine.hpp"
#include "ui/main_window.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSettings>
#include <QThread>
#include <QScreen>
#include <QWindow>
#include <QPushButton>
#include <QTimer>
#include <windows.h>
#include <psapi.h>
#include <atomic>
#include <algorithm>
#include <iostream>
using namespace portbridge;
int main(int argc,char**argv){
 QApplication app(argc,argv);if(argc<4)return 2;const QString output=QString::fromLocal8Bit(argv[1]),captureDir=QString::fromLocal8Bit(argv[2]),protocol=QString::fromLocal8Bit(argv[3]);
 QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,captureDir+"/settings");app.setOrganizationName("PortBridgeLocalAcceptance");app.setApplicationName(protocol);
 auto write=[&](const QJsonObject&value){QFile file(output);if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate))return;file.write(QJsonDocument(value).toJson());};
 auto wait=[&](auto predicate,int ms){QElapsedTimer t;t.start();while(!predicate()&&t.elapsed()<ms){app.processEvents();QThread::msleep(1);}return predicate();};
 ConnectionConfig cfg;cfg.kind=protocol=="tcp"?TransportKind::TcpServer:TransportKind::Udp;cfg.localAddress="127.0.0.1";cfg.localPort=0;cfg.remotePort=9;cfg.sequenceAnalysis=protocol=="udp";cfg.receiveBufferBytes=16*1024*1024;cfg.sendBufferBytes=4*1024*1024;cfg.sequenceWindow=1024;
 SessionController session;MainWindow window(&session);window.resize(1100,760);window.show();session.setHighSpeed(true);
 session.start(cfg);if(!wait([&]{return session.connected();},5000)){write({{"state","failed"},{"error",session.lastError()}});return 3;}
 std::atomic<bool>ready{false};std::atomic<quint64>completed{0};std::atomic<quint64>completedBytes{0};
 NetworkEngine peer({[&](const DataRecord&r){if(r.direction==Direction::Transmit){++completed;completedBytes+=r.payload->size();}return true;},[&](const TransportEvent&e){if(e.kind==EventKind::Connected||e.kind==EventKind::UdpTargetReady)ready=true;}});
 auto peerCfg=cfg;peerCfg.sequenceAnalysis=false;peerCfg.kind=protocol=="tcp"?TransportKind::TcpClient:TransportKind::Udp;peerCfg.remotePort=session.localEndpoint().port;peer.start(peerCfg);
 if(!wait([&]{return ready.load();},5000))return 4;
 RecordingOptions options;options.directory=captureDir.toStdString();options.rotateBytes=32*1024*1024;options.queueBytes=16*1024*1024;QString error;if(!session.startRecording(options,&error)){write({{"state","failed"},{"error",error}});return 5;}
 QElapsedTimer wall;wall.start();QElapsedTimer tick;tick.start();qint64 maxTick=0;QTimer heartbeat;heartbeat.setInterval(10);QObject::connect(&heartbeat,&QTimer::timeout,&app,[&]{maxTick=std::max(maxTick,tick.restart());});heartbeat.start();
 const int durationMs=argc>4?QString::fromLocal8Bit(argv[4]).toInt():300000;const int rate=1000;const int bytesPerFrame=protocol=="tcp"?4096:1472;quint64 admitted=0,rejected=0;size_t queuePeak=0,displayPeak=0;SIZE_T rssPeak=0;QJsonArray samples;
 QJsonArray screenResults;int currentScreen=-1;QElapsedTimer sampleTimer;sampleTimer.start();qint64 nextFrame=0;
 while(wall.elapsed()<durationMs){
  app.processEvents();const qint64 elapsed=wall.elapsed();const int desiredScreen=int(elapsed/10000)%app.screens().size();
  if(desiredScreen!=currentScreen){currentScreen=desiredScreen;auto* screen=app.screens()[currentScreen];window.move(screen->availableGeometry().topLeft()+QPoint(20,20));}
  const int batch=int(std::min<qint64>(32,std::max<qint64>(0,(elapsed-nextFrame)*rate/1000+1)));
  for(int i=0;i<batch;++i){auto data=std::make_shared<Bytes>(bytesPerFrame);for(int p=0;p<bytesPerFrame;++p)(*data)[p]=std::uint8_t((admitted*31)^(p*17));for(int p=0;p<8;++p)(*data)[p]=std::uint8_t(admitted>>(56-8*p));if(peer.send(data)){++admitted;nextFrame=qint64(admitted*1000/rate);}else{++rejected;break;}}
  const auto s=session.statistics();queuePeak=std::max(queuePeak,s.recordingQueueBytes);displayPeak=std::max(displayPeak,s.sampleQueueBytes);
  if(sampleTimer.elapsed()>=1000){PROCESS_MEMORY_COUNTERS pm{};GetProcessMemoryInfo(GetCurrentProcess(),&pm,sizeof(pm));rssPeak=std::max(rssPeak,pm.WorkingSetSize);samples.append(QJsonObject{{"elapsedMs",elapsed},{"workingSetBytes",double(pm.WorkingSetSize)},{"rxBytes",double(s.rxBytes)},{"recordedBytes",double(s.recordedBytes)},{"recordingQueueBytes",double(s.recordingQueueBytes)},{"displayQueueBytes",double(s.sampleQueueBytes)},{"heartbeatMaxMs",maxTick}});sampleTimer.restart();auto* screen=window.windowHandle()->screen();if(screenResults.size()==0||screenResults.last().toObject().value("screen")!=screen->name())screenResults.append(QJsonObject{{"screen",screen->name()},{"devicePixelRatio",screen->devicePixelRatio()},{"windowDevicePixelRatio",window.devicePixelRatioF()},{"logicalDpi",screen->logicalDotsPerInch()},{"windowWidth",window.width()},{"windowHeight",window.height()},{"visible",window.isVisible()}});write({{"state","running"},{"protocol",protocol},{"elapsedMs",elapsed},{"admittedFrames",double(admitted)},{"rxBytes",double(s.rxBytes)},{"recordedBytes",double(s.recordedBytes)},{"heartbeatMaxMs",maxTick},{"samples",samples}});}
  QThread::msleep(1);
 }
 const qint64 sendingMs=wall.elapsed();const bool drained=wait([&]{auto s=session.statistics();return s.rxBytes==admitted*bytesPerFrame&&s.recordedBytes==s.rxBytes&&completedBytes==s.rxBytes;},15000);
 QElapsedTimer stop;stop.start();session.stopRecording();const qint64 stopCallMs=stop.elapsed();const bool finalized=wait([&]{auto captures=session.captures();return !captures.empty()&&std::all_of(captures.begin(),captures.end(),[](const CaptureInfo&c){return c.complete&&c.error.empty();});},15000);
 auto s=session.statistics();QJsonArray captures;quint64 captureBytes=0;for(const auto&c:session.captures()){captures.append(QJsonObject{{"file",QFileInfo(QString::fromStdString(c.path)).fileName()},{"bytes",double(c.bytes)},{"records",double(c.records)},{"complete",c.complete}});captureBytes+=c.bytes;}
 session.stop();peer.stop();window.close();
 const bool passed=drained&&finalized&&s.applicationDroppedRecords==0&&s.recordingFailures==0&&s.sequenceMissing==0&&s.sequenceDuplicates==0&&captureBytes==admitted*bytesPerFrame&&queuePeak<=options.queueBytes&&displayPeak<=2*1024*1024&&maxTick<500&&stopCallMs<500;
 write({{"state",passed?"passed":"failed"},{"scope","local loopback; real Qt MainWindow and real disk recording; not physical 2.5G"},{"protocol",protocol},{"durationMs",sendingMs},{"rateFramesPerSecond",rate},{"payloadBytes",bytesPerFrame},{"admittedFrames",double(admitted)},{"rejectedAdmissions",double(rejected)},{"transmitCompletionRecords",double(completed.load())},{"transmitCompletedBytes",double(completedBytes.load())},{"rxBytes",double(s.rxBytes)},{"recordedBytes",double(s.recordedBytes)},{"capturePayloadBytes",double(captureBytes)},{"droppedRecords",double(s.applicationDroppedRecords)},{"recordingFailures",double(s.recordingFailures)},{"sequenceMissing",double(s.sequenceMissing)},{"sequenceDuplicates",double(s.sequenceDuplicates)},{"displayOmitted",double(s.displayOmitted)},{"recordingQueuePeakBytes",double(queuePeak)},{"displayQueuePeakBytes",double(displayPeak)},{"rssPeakBytes",double(rssPeak)},{"heartbeatMaxMs",maxTick},{"stopRecordingCallMs",stopCallMs},{"finalizationMs",stop.elapsed()},{"captures",captures},{"physicalScreens",screenResults},{"samples",samples}});return passed?0:1;
}
