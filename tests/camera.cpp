#include "pslcamera.h"
#include "mainwindow.h"
#include <QApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkProxy>
#include <QElapsedTimer>
#include <QTimer>
#include <QThread>
#include <QPointer>
#include <QCoreApplication>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QProgressBar>
#include <QTextBrowser>
#include <QMessageBox>
#include <limits>
#include <functional>
#include <iostream>
#include <cstdlib>
#include <atomic>

static void require(bool ok,const char *message) {
    if(!ok) { std::cerr<<"FAIL: "<<message<<'\n'; std::exit(1); }
}
static bool spin(const std::function<bool()> &done,int timeout=3000) {
    QElapsedTimer timer; timer.start();
    while(!done() && timer.elapsed()<timeout) {
        QCoreApplication::processEvents(); QThread::msleep(1);
    }
    QCoreApplication::processEvents();
    return done();
}
static QByteArray frame(int width,int height,const QByteArray &pixels) {
    const QByteArray compressed=qCompress(pixels).mid(4);
    return QByteArray::number(width)+';'+QByteArray::number(height)+';'+QByteArray::number(compressed.size())+compressed;
}
static QByteArray pixels(std::initializer_list<quint16> values) {
    QByteArray data;
    for(quint16 v:values) { data+=char(v&255); data+=char(v>>8); }
    return data;
}
class MockCamera : public QTcpServer {
public:
    QByteArray image;
    QStringList commands;
    int failAt=-1, stallAt=-1, disconnectAt=-1, truncateImageAt=-1;
    bool fragment=false;
    std::function<void(int)> observed;
    MockCamera() {
        require(listen(QHostAddress::LocalHost,0),"mock camera can listen locally");
        QObject::connect(this,&QTcpServer::newConnection,this,[this] {
            while(hasPendingConnections()) {
                QTcpSocket *socket=nextPendingConnection();
                QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
                QObject::connect(socket,&QTcpSocket::readyRead,this,[this,socket] {
                    if(!socket->canReadLine()) return;
                    QString command=QString::fromLatin1(socket->readLine()).trimmed();
                    int index=commands.size(); commands<<command;
                    if(observed) observed(index);
                    if(index==stallAt) return;
                    if(index==disconnectAt) { socket->disconnectFromHost(); return; }
                    QByteArray reply=index==failAt ? QByteArray("False") :
                        command=="GetImage" ? image : command=="Snap" ? QByteArray("True\n") : QByteArray("[]\n");
                    if(command=="GetImage" && truncateImageAt>=0) {
                        socket->write(reply.left(truncateImageAt));
                        socket->disconnectFromHost();
                        return;
                    }
                    if(fragment) {
                        for(int i=0;i<reply.size();++i) {
                            QPointer<QTcpSocket> target=socket;
                            QTimer::singleShot(i,this,[target,reply,i] {
                                if(target && target->state()==QAbstractSocket::ConnectedState) target->write(reply.mid(i,1));
                            });
                        }
                    } else socket->write(reply);
                });
            }
        });
    }
};
static void configure(PSLCameraThread &camera,MockCamera &server) {
    camera.setHost("127.0.0.1",qint16(server.serverPort()));
    camera.setIntegrationTime(.01); camera.setTimeout(250); camera.setBinning(2);
}
static void receiveChecks() {
    MockCamera server; PSLCameraThread camera; configure(camera,server);
    std::atomic<int> successes{0},errors{0};
    QObject::connect(&camera,&PSLCameraThread::newImage,[&] { ++successes; require(!camera.getNewImage().isNull(),"image visible when signaled"); });
    QObject::connect(&camera,&PSLCameraThread::error,[&](int,const QString &) { ++errors; });
    const QByteArray raw=pixels({0,256,1024,65535});
    server.image=frame(2,2,raw); server.fragment=true;
    camera.getImage(); require(spin([&] {return camera.isFinished();}),"fragmented acquisition completes");
    require(successes==1 && errors==0,"fragmented frame succeeded");
    QImage image=camera.getNewImage();
    require(image.size()==QSize(2,2) && qRed(image.pixel(0,0))==0 && qRed(image.pixel(1,1))==255,"16-bit little-endian pixel normalization");
    require(server.commands==QStringList({"SetBinning;2;2","SetExposure;10","Snap","GetImage"}),"original command protocol preserved");
    server.commands.clear(); server.image=frame(2,2,pixels({42,42,42,42}));
    camera.getImage(); require(spin([&] {return camera.isFinished();}),"uniform acquisition completes");
    require(successes==2 && errors==0 && qRed(camera.getNewImage().pixel(0,0))==0,"uniform image avoids division by zero");
    // qCompress level 0 leaves literal newlines in the binary zlib payload.
    server.commands.clear();
    const QByteArray binary=pixels({10,2560,10,2560});
    const QByteArray zlib=qCompress(binary,0).mid(4);
    require(zlib.contains('\n'),"test payload contains binary newline");
    server.image="2;2;"+QByteArray::number(zlib.size())+zlib;
    camera.getImage(); require(spin([&] {return camera.isFinished();}),"binary newline acquisition completes");
    require(successes==3 && errors==0,"binary newline retained");
    const QImage previous=camera.getNewImage();
    QList<QByteArray> malformed={"0;2;9x", "-1;2;9x", "1;;9x", "1;1;0x", "16385;1;9x", "16384;16384;9x",
        "1;1;134217729x", "99999999999999999999999;1;9x", "1;1;3x!!", frame(2,2,pixels({1})), frame(1,1,pixels({1,2}))};
    server.fragment=false;
    for(const QByteArray &bad:malformed) {
        server.commands.clear(); server.image=bad; int oldErrors=errors;
        camera.getImage(); require(spin([&] {return camera.isFinished();}),"malformed acquisition completes");
        require(errors==oldErrors+1 && successes==3 && camera.getNewImage()==previous,"malformed frame rejected with previous image retained");
    }
    server.commands.clear(); server.image=frame(2,2,raw);
    server.truncateImageAt=server.image.size()-2;
    const int oldErrors=errors;
    camera.getImage(); require(spin([&] {return camera.isFinished();}),"truncated binary acquisition completes");
    require(errors==oldErrors+1 && successes==3 && camera.getNewImage()==previous,"truncated compressed frame retains previous image");
}

// A local SOCKS5 handshake keeps Qt in ConnectingState without external traffic.
// Delaying it reproduces connections taking longer than the 100ms interrupt poll.
static void connectionChecks() {
    const QNetworkProxy oldProxy=QNetworkProxy::applicationProxy();
    for(int mode=0;mode<3;++mode) {
        QTcpServer proxy;
        require(proxy.listen(QHostAddress::LocalHost,0),"connection mock can listen locally");
        bool greeted=false,commandSeen=false;
        QObject::connect(&proxy,&QTcpServer::newConnection,&proxy,[&] {
            QTcpSocket *socket=proxy.nextPendingConnection();
            QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
            QByteArray buffer;
            int stage=0;
            QObject::connect(socket,&QTcpSocket::readyRead,&proxy,[&,socket,buffer,stage]() mutable {
                buffer+=socket->readAll();
                if(stage==0 && buffer.size()>=2 && buffer.size()>=2+static_cast<unsigned char>(buffer[1])) {
                    buffer.remove(0,2+static_cast<unsigned char>(buffer[1]));
                    greeted=true; stage=1;
                    if(mode==0) QTimer::singleShot(220,socket,[socket] { socket->write(QByteArray::fromHex("0500")); });
                }
                if(stage==1 && mode==0 && buffer.size()>=5) {
                    const int length=static_cast<unsigned char>(buffer[3])==3 ?
                        7+static_cast<unsigned char>(buffer[4]) : 10;
                    if(buffer.size()<length) return;
                    buffer.remove(0,length); stage=2;
                    socket->write(QByteArray::fromHex("050000017f0000010001"));
                }
                if(stage==2 && buffer.contains('\n')) {
                    commandSeen=buffer.startsWith("SetBinning;");
                    socket->write("False\n"); buffer.clear();
                }
            });
        });
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::Socks5Proxy,"127.0.0.1",proxy.serverPort()));
        PSLCameraThread *camera=new PSLCameraThread;
        camera->setHost("camera.invalid",12345); camera->setTimeout(mode==1 ? 250 : 1000);
        camera->setIntegrationTime(.01);
        std::atomic<int> errors{0};
        int errorCode=0;
        QObject::connect(camera,&PSLCameraThread::error,[&](int code,const QString &) {errorCode=code; ++errors;});
        camera->getImage();
        require(spin([&] {return greeted;}),"proxy handshake starts without external traffic");
        if(mode==2) {
            QElapsedTimer timer; timer.start(); delete camera;
            require(timer.elapsed()<1000,"shutdown interrupts connection handshake");
        } else {
            require(spin([&] {return camera->isFinished();}),"connection wait terminates");
            require(errors==1,"connection test has one error");
            if(mode==0) require(commandSeen,"connection survives more than one interrupt interval");
            else require(errorCode==QAbstractSocket::SocketTimeoutError && !commandSeen,"overall connection deadline enforced");
            delete camera;
        }
        QNetworkProxy::setApplicationProxy(oldProxy);
    }
}
static void failureChecks() {
    for(int index=0;index<4;++index) for(int type=0;type<3;++type) {
        MockCamera server; PSLCameraThread camera; configure(camera,server);
        server.image=frame(1,1,pixels({1}));
        if(type==0) server.failAt=index;
        if(type==1) server.stallAt=index;
        if(type==2) server.disconnectAt=index;
        std::atomic<int> errors{0},successes{0};
        QObject::connect(&camera,&PSLCameraThread::error,[&](int,const QString &) {++errors;});
        QObject::connect(&camera,&PSLCameraThread::newImage,[&] {++successes;});
        camera.getImage(); require(spin([&] {return camera.isFinished();}),"failed command terminates");
        require(errors==1 && successes==0 && server.commands.size()==index+1,"failed command stops acquisition pipeline");
    }
    for(int stage=0;stage<4;++stage) {
        MockCamera server; PSLCameraThread *camera=new PSLCameraThread; configure(*camera,server);
        camera->setTimeout(60000); server.stallAt=stage;
        camera->getImage(); require(spin([&] {return server.commands.size()==stage+1;}),"shutdown reaches blocked read");
        QElapsedTimer timer; timer.start(); delete camera;
        require(timer.elapsed()<1000,"shutdown interrupts blocked read promptly");
    }
}
static void snapshotChecks() {
    MockCamera server; PSLCameraThread camera; configure(camera,server);
    server.image=frame(1,1,pixels({123}));
    server.observed=[&](int index) {
        if(index==0) {
            camera.setHost("invalid.invalid",1); camera.setBinning(7); camera.setIntegrationTime(1);
        }
    };
    std::atomic<int> successes{0};
    QObject::connect(&camera,&PSLCameraThread::newImage,[&] {++successes;});
    camera.getImage(); require(spin([&] {return camera.isFinished();}),"snapshot acquisition completes");
    require(successes==1 && server.commands.at(1)=="SetExposure;10","ongoing acquisition retains request settings");
    require(camera.getHostname()=="invalid.invalid" && camera.getBinning()==7 && camera.getIntegrationTime()==1,"new settings retained for next request");
}

static void controlChecks() {
    MockCamera server; PSLCameraThread camera; configure(camera,server);
    server.image=frame(2,1,pixels({0,100}));
    PSLCameraWidget controls;
    controls.setCamera(&camera);
    auto *binning=controls.findChild<QSpinBox *>("binningSpinBox");
    auto *exposure=controls.findChild<QLineEdit *>("integrationTimeLineEdit");
    auto *snap=controls.findChild<QPushButton *>("snapPushButton");
    auto *status=controls.findChild<QTextBrowser *>("textBrowser");
    auto *progress=controls.findChild<QProgressBar *>("progressBar");
    require(binning && exposure && snap && status && progress,"camera controls available");
    require(binning->value()==2 && exposure->text().toDouble()==.01 && snap->isEnabled(),"camera controls load backend settings");
    binning->setValue(4);
    exposure->setText("0.025");
    QMetaObject::invokeMethod(exposure,"editingFinished",Qt::DirectConnection);
    require(camera.getBinning()==4 && camera.getIntegrationTime()==.025,"dock edits reach shared settings");

    const double precise=.12345678901234567;
    PSLCameraDialog dialog;
    dialog.setIntegrationTime(precise); dialog.setBinning(3);
    require(dialog.getIntegrationTime()==precise,"Camera Setup retains full exposure precision");
    dialog.accept();
    require(dialog.result()==QDialog::Accepted,"valid Camera Setup accepted");
    camera.setBinning(dialog.getBinning());
    camera.setIntegrationTime(dialog.getIntegrationTime());
    require(binning->value()==3 && exposure->text().toDouble()==precise,"accepted Camera Setup updates dock through backend");
    for(double invalid : {0.0,-1.0,std::numeric_limits<double>::quiet_NaN(),
                          std::numeric_limits<double>::infinity(),1e100})
        camera.setIntegrationTime(invalid);
    camera.setBinning(0); camera.setBinning(-1);
    require(camera.getIntegrationTime()==precise && camera.getBinning()==3,"invalid setters retain valid camera settings");

    for(const QString &invalid : {QString("0"),QString("-1"),QString("nan"),QString("inf"),QString("invalid"),QString("1e100")}) {
        exposure->setText(invalid);
        snap->click();
        require(!camera.isRunning() && server.commands.isEmpty() &&
                camera.getIntegrationTime()==precise && !status->toPlainText().isEmpty(),
                "invalid dock exposure blocks acquisition and preserves valid setting");
    }
    PSLCameraDialog invalidDialog;
    invalidDialog.findChild<QLineEdit *>("integrationTime")->setText("nan");
    bool explained=false;
    QTimer::singleShot(0,[&] {
        auto *message=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        require(message,"invalid Camera Setup explains error");
        explained=true; message->accept();
    });
    invalidDialog.accept();
    require(explained && invalidDialog.result()!=QDialog::Accepted,"invalid Camera Setup remains unaccepted");

    exposure->setText("0.025");
    binning->setValue(4);
    snap->click();
    require(!snap->isEnabled() && !binning->isEnabled() && !exposure->isEnabled(),"dock disables duplicate acquisition and settings while busy");
    controls.acquireImage(); // The menu calls this same entry point.
    snap->click();
    require(spin([&] {return camera.isFinished() && snap->isEnabled();}),"dock acquisition returns to ready");
    require(server.commands==QStringList({"SetBinning;4;4","SetExposure;25","Snap","GetImage"}),
            "Snap sends one request with edited settings");
    require(!camera.getNewImage().isNull() && progress->value()==100 && status->toPlainText()=="Done.",
            "dock shows acquisition result and status");

    server.commands.clear(); server.failAt=0;
    controls.acquireImage();
    require(spin([&] {return camera.isFinished() && snap->isEnabled();}),"failed acquisition restores dock controls");
    require(server.commands.size()==1 && status->toPlainText().contains("failed") && progress->value()==0,
            "dock reports failure without false completed progress");
}
static void windowControlChecks() {
    MockCamera server;
    server.image=frame(2,1,pixels({0,100}));
    MainWindow window;
    auto *camera=window.findChild<PSLCameraThread *>();
    auto *controls=window.findChild<PSLCameraWidget *>();
    auto *film=window.findChild<LaueFilm *>();
    require(camera && controls && film,"MainWindow camera components exist");
    auto *setup=window.findChild<QAction *>("actionAquire_Image_Dialog");
    auto *acquire=window.findChild<QAction *>("actionAquire_Image");
    require(setup && acquire && setup->isEnabled() && acquire->isEnabled(),"camera menus available");
    QTimer driver;
    QObject::connect(&driver,&QTimer::timeout,[&] {
        auto *dialog=qobject_cast<PSLCameraDialog *>(QApplication::activeModalWidget());
        require(dialog,"Camera Setup shown from actual menu");
        dialog->setHostname("127.0.0.1");dialog->setPort(server.serverPort());
        dialog->setBinning(4);dialog->setIntegrationTime(.025);dialog->accept();
    });
    driver.start(10);setup->trigger();driver.stop();
    camera->setTimeout(500);
    auto *binning=controls->findChild<QSpinBox *>("binningSpinBox");
    auto *exposure=controls->findChild<QLineEdit *>("integrationTimeLineEdit");
    auto *snap=controls->findChild<QPushButton *>("snapPushButton");
    require(binning->value()==4 && exposure->text().toDouble()==.025,"MainWindow Setup synchronizes dock");
    int previews=0;
    QObject::disconnect(&driver,nullptr,nullptr,nullptr);
    QObject::connect(&driver,&QTimer::timeout,[&] {
        auto *dialog=qobject_cast<PSLCameraImageDialog *>(QApplication::activeModalWidget());
        if(dialog) {++previews;dialog->accept();}
    });
    driver.start(10);acquire->trigger();snap->click();
    require(spin([&]{return previews==1 && camera->isFinished() && snap->isEnabled();}),"menu acquisition and accepted preview complete");
    require(server.commands==QStringList({"SetBinning;4;4","SetExposure;25","Snap","GetImage"}) &&
            film->importedImage->size()==QSize(2,1),"menu uses shared dock settings and imports returned image");
    server.commands.clear();binning->setValue(3);exposure->setText(".04");snap->click();acquire->trigger();
    require(spin([&]{return previews==2 && camera->isFinished() && snap->isEnabled();}),"dock acquisition and preview complete");
    driver.stop();
    require(server.commands==QStringList({"SetBinning;3;3","SetExposure;40","Snap","GetImage"}),"dock Snap uses same route and ignores duplicate menu request");
    server.commands.clear();exposure->setText("0");acquire->trigger();
    require(!camera->isRunning() && server.commands.isEmpty() && camera->getIntegrationTime()==.04,
            "menu rejects invalid dock exposure before acquisition");
    qInfo("PASS: MainWindow Camera Setup, dock Snap, menu acquisition, preview import and shared validation");
}

int main(int argc,char **argv) {
    QApplication app(argc,argv);
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    QCoreApplication::setOrganizationName("QLaueTests");QCoreApplication::setApplicationName("Camera");
    receiveChecks(); failureChecks(); snapshotChecks(); connectionChecks(); controlChecks(); windowControlChecks();
    std::cout<<"PASS: camera mock stream, boundaries, failures, snapshots and shutdown\n";
    std::cout<<"PASS: Camera Controls/Setup synchronization, validated exposure, Snap request and busy/error recovery\n";
}
