#include "ui/main_window.hpp"
#include "portbridge/session_controller.hpp"
#include <QtTest>
#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QComboBox>
#include <QListWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QCheckBox>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QDialog>
#include <QDialogButtonBox>
#include <QScrollBar>
#include <QUdpSocket>
#include <QJsonDocument>
#include <QJsonObject>
using namespace portbridge;
class Audit:public QObject {
 Q_OBJECT
 QTemporaryDir settingsDir;
 QString output;
 template<class T>T* get(MainWindow& w,const char* name){auto* p=w.findChild<T*>(name);if(!p)qFatal("missing %s",name);return p;}
 void shot(MainWindow& w,const QString& name){QTest::qWait(80);QVERIFY(w.grab().save(output+"/"+name+".png"));}
 void udp(MainWindow& w,SessionController& c,QUdpSocket& peer){QVERIFY(peer.bind(QHostAddress::LocalHost,0));get<QListWidget>(w,"profileList")->setCurrentRow(3);get<QComboBox>(w,"localAddress")->setCurrentText("127.0.0.1");get<QSpinBox>(w,"localPort")->setValue(0);get<QSpinBox>(w,"remotePort")->setValue(peer.localPort());get<QPushButton>(w,"connectButton")->click();QTRY_VERIFY(c.connected());QTRY_VERIFY(get<QPushButton>(w,"recordButton")->isEnabled());}
 void receive(SessionController& c,QUdpSocket& peer,const QByteArray& b){auto before=c.statistics().rxBytes;QCOMPARE(peer.writeDatagram(b,QHostAddress::LocalHost,c.localEndpoint().port),qint64(b.size()));QTRY_COMPARE(c.statistics().rxBytes,before+std::uint64_t(b.size()));QTest::qWait(80);}
 void record(MainWindow& w,SessionController& c,QUdpSocket& peer,const QString& dir){get<QLineEdit>(w,"recordDirectory")->setText(dir);get<QPushButton>(w,"recordButton")->click();QTRY_VERIFY(c.recording());receive(c,peer,"actual-capture");get<QPushButton>(w,"recordButton")->click();QTRY_VERIFY(!c.captures().empty()&&c.captures().back().complete);}
private slots:
 void initTestCase(){QCoreApplication::setOrganizationName("PortBridgeDesignAudit");QCoreApplication::setApplicationName("PortBridgeDesignAudit");QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settingsDir.path());output=qEnvironmentVariable("AUDIT_OUTPUT");QVERIFY(!output.isEmpty());QVERIFY(QDir().mkpath(output));}
 void init(){QSettings s;s.clear();s.setValue("storage/directory",settingsDir.path()+"/"+QString::fromLatin1(QTest::currentTestFunction()));}
 void visualStates(){SessionController c;MainWindow w(&c);w.resize(1440,1000);w.show();QUdpSocket peer;udp(w,c,peer);get<QPushButton>(w,"highSpeedModeButton")->click();QByteArray payload=QByteArray::fromHex("aa550210010005c06f7c8996a3b0bdca");payload.append(QByteArray(1472-payload.size(),'x'));receive(c,peer,payload);auto* table=get<QTableView>(w,"recordTable");for(int i=0;i<table->model()->rowCount();++i)if(table->model()->index(i,2).data().toString()=="RX")table->setCurrentIndex(table->model()->index(i,0));get<QPlainTextEdit>(w,"sendInput")->setPlainText("AA 55 01 00 00 00 01 FE");shot(w,"native-udp");get<QPushButton>(w,"themeButton")->click();shot(w,"native-udp-light");get<QPushButton>(w,"themeButton")->click();get<QTabWidget>(w,"inspectorTabs")->setCurrentIndex(1);shot(w,"native-capture-settings");get<QLineEdit>(w,"recordDirectory")->setText(settingsDir.path()+"/capture-visual");get<QPushButton>(w,"recordButton")->click();QTRY_VERIFY(c.recording());receive(c,peer,payload);shot(w,"native-recording");get<QPushButton>(w,"recordButton")->click();QTRY_VERIFY(!c.captures().empty()&&c.captures().back().complete);get<QPushButton>(w,"capturesNavigation")->click();shot(w,"native-captures");Command cmd;cmd.name=QStringLiteral("读取设备信息");cmd.input="AA 55 01 00 00 00 01 FE";QVERIFY(saveCommands({cmd}));get<QPushButton>(w,"workspaceNavigation")->click();get<QPushButton>(w,"dataViewTab2")->click();shot(w,"native-diagnostics");get<QListWidget>(w,"profileList")->setCurrentRow(1);get<QPushButton>(w,"dataViewTab0")->click();get<QTabWidget>(w,"inspectorTabs")->setCurrentIndex(0);shot(w,"native-tcp-client");}
 void startupAndSmallLightScreenshots(){SessionController c;MainWindow w(&c);w.show();w.resize(1440,1000);shot(w,"native-startup-dark");get<QPushButton>(w,"themeButton")->click();shot(w,"native-startup-light");w.resize(1100,760);shot(w,"native-small-light");get<QPushButton>(w,"themeButton")->click();shot(w,"native-small-dark");QVERIFY(!c.connected());QVERIFY(!c.periodicActive());QVERIFY(!c.recording());}
 void continuousPeriodicAccessible(){SessionController c;MainWindow w(&c);w.show();auto* count=get<QSpinBox>(w,"periodicCount");qInfo()<<"UI periodic range"<<count->minimum()<<count->maximum()<<"API uses count=0 for continuous";QVERIFY2(count->minimum()==0,"FR-016: continuous periodic mode cannot be selected in UI");}
 void priorSequenceDoesNotLeakIntoTcpProfile(){SessionController c;MainWindow w(&c);w.resize(1440,1000);w.show();QUdpSocket peer;QVERIFY(peer.bind(QHostAddress::LocalHost,0));get<QListWidget>(w,"profileList")->setCurrentRow(3);get<QComboBox>(w,"localAddress")->setCurrentText("127.0.0.1");get<QSpinBox>(w,"localPort")->setValue(0);get<QSpinBox>(w,"remotePort")->setValue(peer.localPort());get<QCheckBox>(w,"sequenceEnabled")->setChecked(true);get<QSpinBox>(w,"sequenceWindow")->setValue(1);get<QPushButton>(w,"connectButton")->click();QTRY_VERIFY(c.connected());receive(c,peer,QByteArray::fromHex("0000000000000064"));receive(c,peer,QByteArray::fromHex("0000000000000066"));QCOMPARE(c.statistics().sequenceMissing,std::uint64_t(1));get<QListWidget>(w,"profileList")->setCurrentRow(1);QTest::qWait(200);shot(w,"stale-sequence-in-tcp");qInfo()<<"profile"<<get<QLabel>(w,"connectionTransportTag")->text()<<"analysis UI checked"<<get<QCheckBox>(w,"sequenceEnabled")->isChecked()<<"statistics enabled"<<c.statistics().sequenceEnabled<<"missing display"<<get<QLabel>(w,"sequenceMetric")->text()<<"rx"<<c.statistics().rxBytes;QVERIFY2(get<QLabel>(w,"sequenceMetric")->text()!=QStringLiteral("1"),"UDP sequence result appears under newly selected TCP profile");}
 void newProfilePreservesPreviousEdits(){SessionController c;MainWindow w(&c);w.show();get<QListWidget>(w,"profileList")->setCurrentRow(3);get<QSpinBox>(w,"remotePort")->setValue(34567);QTimer::singleShot(0,[]{auto* d=qobject_cast<QDialog*>(QApplication::activeModalWidget());QVERIFY(d);d->findChild<QLineEdit*>("profileName")->setText("new-audit-profile");d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();});get<QPushButton>(w,"addProfile")->click();get<QListWidget>(w,"profileList")->setCurrentRow(3);qInfo()<<"previous profile port after add"<<get<QSpinBox>(w,"remotePort")->value();QCOMPARE(get<QSpinBox>(w,"remotePort")->value(),34567);}
 void refreshCatalogFindsExternalFile(){SessionController c;MainWindow w(&c);w.resize(1440,1000);w.show();QUdpSocket peer;udp(w,c,peer);QString dir=settingsDir.path()+"/catalog";record(w,c,peer,dir);auto captures=c.captures();QCOMPARE(captures.size(),std::size_t(1));QString original=QString::fromStdString(captures[0].path);QString extra=dir+"/external-copy.pbc";QVERIFY(QFile::copy(original,extra));QVERIFY(QFile::copy(original+".meta.json",extra+".meta.json"));get<QPushButton>(w,"capturesNavigation")->click();get<QPushButton>(w,"refreshCaptures")->click();QTest::qWait(1200);shot(w,"catalog-refresh-missing-file");qInfo()<<"disk pbc files"<<QDir(dir).entryList({"*.pbc"},QDir::Files).size()<<"UI files"<<get<QListWidget>(w,"captureList")->count();QCOMPARE(get<QListWidget>(w,"captureList")->count(),2);}
 void selectionInspectorClearsOnProfileSwitch(){SessionController c;MainWindow w(&c);w.resize(1440,1000);w.show();QUdpSocket peer;udp(w,c,peer);receive(c,peer,"audit-selected-data");auto* table=get<QTableView>(w,"recordTable");for(int i=0;i<table->model()->rowCount();++i)if(table->model()->index(i,2).data().toString()=="RX")table->setCurrentIndex(table->model()->index(i,0));QVERIFY(!get<QPlainTextEdit>(w,"byteInspector")->toPlainText().isEmpty());get<QListWidget>(w,"profileList")->setCurrentRow(1);QTest::qWait(120);shot(w,"stale-byte-inspector");qInfo()<<"new table rows"<<table->model()->rowCount()<<"current valid"<<table->currentIndex().isValid()<<"old bytes length"<<get<QPlainTextEdit>(w,"byteInspector")->toPlainText().size()<<"copy enabled"<<get<QPushButton>(w,"copyInspectorBytes")->isEnabled();QVERIFY2(get<QPlainTextEdit>(w,"byteInspector")->toPlainText().isEmpty(),"New profile has no records but inspector still shows previous UDP bytes");}
 void savedPreferencesRestore(){QString dir=settingsDir.path()+"/prefs-capture";{SessionController c;MainWindow w(&c);w.show();get<QCheckBox>(w,"autoScroll")->setChecked(false);get<QComboBox>(w,"displayFormat")->setCurrentIndex(1);get<QPushButton>(w,"highSpeedModeButton")->click();get<QSpinBox>(w,"recordQueueMiB")->setValue(77);get<QSpinBox>(w,"periodicInterval")->setValue(333);get<QSpinBox>(w,"periodicCount")->setValue(17);get<QLineEdit>(w,"recordDirectory")->setText(dir);}SessionController c;MainWindow w(&c);qInfo()<<"restored autoScroll/display/high/recordQueue/interval/count"<<get<QCheckBox>(w,"autoScroll")->isChecked()<<get<QComboBox>(w,"displayFormat")->currentIndex()<<c.highSpeed()<<get<QSpinBox>(w,"recordQueueMiB")->value()<<get<QSpinBox>(w,"periodicInterval")->value()<<get<QSpinBox>(w,"periodicCount")->value();QCOMPARE(get<QSpinBox>(w,"recordQueueMiB")->value(),77);QCOMPARE(get<QCheckBox>(w,"autoScroll")->isChecked(),false);QCOMPARE(get<QComboBox>(w,"displayFormat")->currentIndex(),1);QVERIFY(c.highSpeed());QCOMPARE(get<QSpinBox>(w,"periodicInterval")->value(),333);QCOMPARE(get<QSpinBox>(w,"periodicCount")->value(),17);QVERIFY(!c.connected());QVERIFY(!c.connecting());QVERIFY(!c.periodicActive());QVERIFY(!c.recording());}
 void errorSurvivesStatisticsReset(){SessionController c;MainWindow w(&c);w.resize(1440,1000);w.show();QUdpSocket peer;udp(w,c,peer);auto* send=get<QPlainTextEdit>(w,"sendInput");send->setPlainText(QString(140000,'A'));get<QPushButton>(w,"sendButton")->click();QTRY_VERIFY(!get<QLabel>(w,"runtimeBanner")->text().isEmpty());get<QPushButton>(w,"resetStatistics")->click();get<QListWidget>(w,"profileList")->setCurrentRow(2);QTest::qWait(120);shot(w,"old-error-new-profile");qInfo()<<"banner after switch"<<get<QLabel>(w,"runtimeBanner")->isVisible()<<get<QLabel>(w,"runtimeBanner")->text();QVERIFY2(!get<QLabel>(w,"runtimeBanner")->isVisible(),"Error from previous UDP send stays visible in new TCP server profile");}
 void continuousPeriodicActuallySendsAndStops(){
  SessionController c; MainWindow w(&c); w.show(); QUdpSocket peer; udp(w,c,peer);
  get<QPlainTextEdit>(w,"sendInput")->setPlainText("AA 55");
  get<QCheckBox>(w,"periodicEnabled")->setChecked(true);
  get<QSpinBox>(w,"periodicInterval")->setValue(20);
  auto* count=get<QSpinBox>(w,"periodicCount");QVERIFY(count->minimum()==0);count->setValue(0);
  get<QPushButton>(w,"sendButton")->click();QTRY_VERIFY(c.periodicActive());QTRY_VERIFY(c.periodicSent()>=5);
  int received=0;while(peer.hasPendingDatagrams()){QByteArray b(int(peer.pendingDatagramSize()),0);peer.readDatagram(b.data(),b.size());QCOMPARE(b,QByteArray::fromHex("aa55"));++received;}QVERIFY(received>=4);
  get<QPushButton>(w,"sendButton")->click();QVERIFY(!c.periodicActive());auto sent=c.periodicSent();QTest::qWait(100);QCOMPARE(c.periodicSent(),sent);
  get<QPushButton>(w,"sendButton")->click();QTRY_VERIFY(c.periodicActive());get<QListWidget>(w,"profileList")->setCurrentRow(1);QVERIFY(!c.periodicActive());QVERIFY(!c.connected());
 }
 void populatedCommandsAndDialogs(){Command cmd;cmd.name=QStringLiteral("读取设备信息");cmd.input="AA 55 01 00 00 00 01 FE";QVERIFY(saveCommands({cmd}));SessionController c;MainWindow w(&c);w.resize(1440,1000);w.show();get<QPushButton>(w,"commandsNavigation")->click();shot(w,"native-commands");QTimer::singleShot(100,[&]{auto* d=qobject_cast<QDialog*>(QApplication::activeModalWidget());QVERIFY(d);QVERIFY(d->grab().save(output+"/native-command-dialog.png"));d->reject();});get<QPushButton>(w,"addCommand")->click();QTimer::singleShot(100,[&]{auto* d=qobject_cast<QDialog*>(QApplication::activeModalWidget());QVERIFY(d);QVERIFY(d->grab().save(output+"/native-profile-dialog.png"));d->reject();});get<QPushButton>(w,"addProfile")->click();}
};
QTEST_MAIN(Audit)
#include "audit.moc"
