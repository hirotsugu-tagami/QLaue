/*
 
 QLaue - Orientation of single crystal samples by the Laue method. 
 Copyright (C) 2007 Stuart Brian Wilkins
 
 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 
 $Revision: 24 $
 $Author: stuwilkins $
 $Date: 2010-05-20 08:22:17 -0400 (Thu, 20 May 2010) $
 $HeadURL: https://qlaue.svn.sourceforge.net/svnroot/qlaue/trunk/mainwindow.cpp $
 
 */

#include <QtNetwork>
#include <QTextStream>
#include <QColor>
#include <QPixmap>
#include <QSignalBlocker>
#include <QMessageBox>
#include <cmath>
#include <limits>
#include "pslcamera.h"

namespace {
bool validIntegrationTime(double value) {
    return std::isfinite(value) && value>0 &&
           value*1200<double(std::numeric_limits<qint64>::max());
}
}

PSLCameraWidget::PSLCameraWidget(QWidget *parent) : QWidget(parent)
{
	ui.setupUi(this);
	setFixedSize(QSize(ui.groupBox->width() + 20,ui.groupBox->height() + 20));
    ui.integrationTimeLineEdit->setToolTip(tr("Exposure time in seconds; must be finite and greater than zero."));
    connect(ui.binningSpinBox,QOverload<int>::of(&QSpinBox::valueChanged),this,[this] { applySettings(); });
    connect(ui.integrationTimeLineEdit,&QLineEdit::editingFinished,this,[this] { applySettings(); });
    connect(ui.snapPushButton,&QPushButton::clicked,this,&PSLCameraWidget::acquireImage);
    setBusy(false);
}

void PSLCameraWidget::setCamera(PSLCameraThread *value) {
    if(camera) {
        disconnect(camera,nullptr,this,nullptr);
        disconnect(camera,nullptr,ui.textBrowser,nullptr);
    }
    camera=value;
    if(camera) {
        connect(camera.data(),&PSLCameraThread::settingsChanged,this,&PSLCameraWidget::syncSettings);
        connect(camera.data(),&PSLCameraThread::statusChange,ui.textBrowser,&QTextBrowser::setPlainText);
        connect(camera.data(),&QThread::started,this,[this] { setBusy(true); });
        connect(camera.data(),&QThread::finished,this,[this] { setBusy(false); });
        connect(camera.data(),&PSLCameraThread::newImage,this,[this] {
            ui.progressBar->setRange(0,100);
            ui.progressBar->setValue(100);
        });
        syncSettings();
    }
    setBusy(camera && camera->isRunning());
}

void PSLCameraWidget::syncSettings() {
    if(!camera) return;
    const QSignalBlocker binning(ui.binningSpinBox), exposure(ui.integrationTimeLineEdit);
    ui.binningSpinBox->setMaximum(qMax(4,camera->getBinning()));
    ui.binningSpinBox->setValue(camera->getBinning());
    ui.integrationTimeLineEdit->setText(QString::number(camera->getIntegrationTime(),'g',17));
}

bool PSLCameraWidget::applySettings() {
    if(!camera) return false;
    bool ok=false;
    const double exposure=ui.integrationTimeLineEdit->text().toDouble(&ok);
    if(!ok || !validIntegrationTime(exposure)) {
        ui.textBrowser->setPlainText(tr("Enter a finite exposure time greater than zero (seconds)."));
        return false;
    }
    camera->setBinning(ui.binningSpinBox->value());
    camera->setIntegrationTime(exposure);
    return true;
}

void PSLCameraWidget::acquireImage() {
    if(!camera || camera->isRunning() || !applySettings()) return;
    setBusy(true);
    camera->getImage();
}

void PSLCameraWidget::setBusy(bool busy) {
    ui.snapPushButton->setEnabled(camera && !busy);
    ui.binningSpinBox->setEnabled(camera && !busy);
    ui.integrationTimeLineEdit->setEnabled(camera && !busy);
    if(busy) {
        ui.progressBar->setRange(0,0);
    } else if(ui.progressBar->maximum()==0) {
        ui.progressBar->setRange(0,100);
        ui.progressBar->setValue(0);
    }
}

PSLCameraImageDialog::PSLCameraImageDialog(QWidget* parent) :
QDialog(parent)
{
	ui.setupUi(this);
	setModal(true);
}

void PSLCameraDialog::accept() {
    bool ok=false;
    const double exposure=ui.integrationTime->text().toDouble(&ok);
    if(ui.hostname->text().trimmed().isEmpty() || !ok || !validIntegrationTime(exposure)) {
        QMessageBox::warning(this,tr("Camera Setup"),
            tr("Enter a hostname and a finite exposure time greater than zero (seconds)."));
        return;
    }
    QDialog::accept();
}

void PSLCameraImageDialog::setImage(QImage i){
	QPixmap pixmap = QPixmap::fromImage(i);
	QSize newsize = pixmap.size();
	newsize.scale(ui.image->size(), Qt::KeepAspectRatio);
	ui.image->setPixmap(pixmap.scaled(newsize));
}

PSLCameraDialog::PSLCameraDialog(QWidget* parent) :
QDialog(parent)
{
	ui.setupUi(this);
	setModal(true);
    ui.portSpinBox->setRange(1,65535);
    ui.integrationTime->setToolTip(tr("Exposure time in seconds; must be finite and greater than zero."));
}

PSLCameraThread::PSLCameraThread(QObject *parent) : QThread(parent),
    hostName("localhost"), port(50000), timeout(60000), integrationTime(10), binning(2) {}

PSLCameraThread::~PSLCameraThread() {
    requestInterruption();
    wait();
}

void PSLCameraThread::setHost(QString host, qint16 portnum) {
    QMutexLocker locker(&mutex);
    hostName = host;
    port = quint16(portnum);
}
void PSLCameraThread::setIntegrationTime(double time) {
    if(!validIntegrationTime(time)) return;
    {
        QMutexLocker locker(&mutex);
        if(integrationTime==time) return;
        integrationTime = time;
    }
    emit settingsChanged();
}
void PSLCameraThread::setBinning(int bin) {
    if(bin<1) return;
    {
        QMutexLocker locker(&mutex);
        if(binning==bin) return;
        binning = bin;
    }
    emit settingsChanged();
}
void PSLCameraThread::setTimeout(int milliseconds) {
    QMutexLocker locker(&mutex);
    timeout = qMax(1,milliseconds);
}
QString PSLCameraThread::getHostname() { QMutexLocker locker(&mutex); return hostName; }
quint16 PSLCameraThread::getPort() { QMutexLocker locker(&mutex); return port; }
double PSLCameraThread::getIntegrationTime() { QMutexLocker locker(&mutex); return integrationTime; }
int PSLCameraThread::getBinning() { QMutexLocker locker(&mutex); return binning; }
QImage PSLCameraThread::getNewImage() { QMutexLocker locker(&mutex); return receivedImage; }

void PSLCameraThread::getImage() {
    QMutexLocker locker(&mutex);
    if(isRunning()) return;
    pending = {hostName,port,timeout,integrationTime,binning};
    start();
}

bool PSLCameraThread::fail(const QString &message, int code) {
    if(!isInterruptionRequested()) {
        emit error(code,message);
        emit statusChange(tr("Camera acquisition failed: %1").arg(message));
    }
    return false;
}

bool PSLCameraThread::connectSocket(QTcpSocket &socket, const Request &request) {
    if(isInterruptionRequested()) return false;
    socket.setReadBufferSize(64*1024);
    QEventLoop loop;
    QTimer interruptPoll, deadline;
    bool timedOut=false;
    deadline.setSingleShot(true);
    QObject::connect(&socket,&QTcpSocket::connected,&loop,&QEventLoop::quit);
    QObject::connect(&socket,&QTcpSocket::errorOccurred,&loop,&QEventLoop::quit);
    QObject::connect(&interruptPoll,&QTimer::timeout,&loop,[&] {
        if(isInterruptionRequested()) loop.quit();
    });
    QObject::connect(&deadline,&QTimer::timeout,&loop,[&] {
        timedOut=true;
        loop.quit();
    });
    // waitForConnected(shortTimeout) aborts the connection at each timeout.
    // Keep one asynchronous connection alive while checking for interruption.
    socket.connectToHost(request.host,request.port);
    if(socket.state()!=QAbstractSocket::ConnectedState &&
       socket.state()!=QAbstractSocket::UnconnectedState) {
        interruptPoll.start(100);
        deadline.start(request.timeout);
        loop.exec();
    }
    if(isInterruptionRequested()) {
        socket.abort();
        return false;
    }
    if(socket.state()==QAbstractSocket::ConnectedState) return true;
    if(timedOut) {
        socket.abort();
        return fail(tr("Connection timed out."),QAbstractSocket::SocketTimeoutError);
    }
    return fail(socket.errorString(),socket.error());
}

bool PSLCameraThread::readBytes(QTcpSocket &socket, QByteArray &bytes, int count,
                              QElapsedTimer &timer, qint64 deadline, double exposure) {
    int remaining=count;
    qint64 nextProgress=timer.elapsed()+1000;
    while(remaining>0) {
        if(isInterruptionRequested()) return false;
        if(socket.bytesAvailable()>0) {
            const QByteArray part=socket.read(remaining);
            if(part.isEmpty()) return fail(tr("Camera read failed."),socket.error());
            bytes.append(part);
            remaining-=part.size();
            continue;
        }
        if(socket.state()==QAbstractSocket::UnconnectedState)
            return fail(tr("Camera disconnected before its reply was complete."),socket.error());
        const qint64 wait=deadline-timer.elapsed();
        if(wait<=0) return fail(tr("Camera reply timed out."),QAbstractSocket::SocketTimeoutError);
        socket.waitForReadyRead(int(qMin<qint64>(100,wait)));
        if(exposure>0 && timer.elapsed()>=nextProgress) {
            emit statusChange(tr("Acquisition %1% complete ...").arg(qMin(100.0,timer.elapsed()/(exposure*10)),4,'f',1));
            nextProgress=timer.elapsed()+1000;
        }
    }
    return true;
}

bool PSLCameraThread::sendCommand(const Request &request, const QString &command, bool snap) {
    QTcpSocket socket;
    if(!connectSocket(socket,request)) return false;
    const QByteArray wire=command.toLatin1()+'\n';
    if(socket.write(wire)!=wire.size()) return fail(tr("Unable to send camera command."),socket.error());
    socket.flush();
    const QByteArray expected=snap ? "True" : "[]";
    const qint64 deadline=snap ? qMax<qint64>(request.timeout,qint64(std::ceil(request.integrationTime*1200))) : request.timeout;
    QElapsedTimer timer; timer.start();
    QByteArray reply;
    if(!readBytes(socket,reply,expected.size(),timer,deadline,snap ? request.integrationTime : 0)) return false;
    if(reply!=expected) return fail(tr("Unexpected response to %1.").arg(command));
    return true;
}

bool PSLCameraThread::sendGetImage(const Request &request) {
    emit statusChange(tr("Retrieving image from server ..."));
    QTcpSocket socket;
    if(!connectSocket(socket,request)) return false;
    if(socket.write("GetImage\n")!=9) return fail(tr("Unable to request camera image."),socket.error());
    socket.flush();
    QElapsedTimer timer; timer.start();
    // Operational limits, not a claim about a particular camera's specifications.
    // ponytail: support at most 32M pixels / 128MiB compressed; raise only for verified hardware.
    const qint64 maxDimension=16384, maxPixels=32LL*1024*1024, maxCompressed=128LL*1024*1024;
    const qint64 limits[]={maxDimension,maxDimension,maxCompressed};
    qint64 values[]={0,0,0};
    int field=0, digits=0;
    QByteArray compressed;
    // The previous parser split the third field at 'x' and counted no separator
    // after its decimal length: retain width;height;length followed by zlib 0x78.
    for(int consumed=0;consumed<40;++consumed) {
        QByteArray byte;
        if(!readBytes(socket,byte,1,timer,request.timeout)) return false;
        const unsigned char c=static_cast<unsigned char>(byte.at(0));
        if(c>='0' && c<='9') {
            values[field]=values[field]*10+(c-'0');
            if(++digits>10 || values[field]>limits[field]) return fail(tr("Camera frame header exceeds supported limits."));
        } else if(field<2 && c==';' && digits>0 && values[field]>0) {
            ++field; digits=0;
        } else if(field==2 && c==0x78 && digits>0 && values[field]>0) {
            compressed=byte;
            break;
        } else return fail(tr("Invalid camera frame header."));
    }
    if(compressed.isEmpty() || values[0]*values[1]>maxPixels)
        return fail(tr("Invalid camera dimensions or frame header."));
    const int width=int(values[0]), height=int(values[1]);
    const int expectedBytes=int(values[0]*values[1]*2);
    const int compressedBytes=int(values[2]);
    if(!readBytes(socket,compressed,compressedBytes-1,timer,request.timeout)) return false;
    QByteArray framed(4,'\0');
    for(int i=0;i<4;++i) framed[i]=char((quint32(expectedBytes)>>(24-8*i))&255);
    framed+=compressed;
    // qUncompress can temporarily expand beyond this declared size; it does not
    // provide a strict decompression-memory limit. Reject any mismatched result.
    const QByteArray pixels=qUncompress(framed);
    if(pixels.size()!=expectedBytes) return fail(tr("Camera image decompression failed or its pixel count is incorrect."));
    if(isInterruptionRequested()) return false;
    unsigned int minimum=65535, maximum=0;
    const auto pixel=[&](int index) {
        return static_cast<unsigned char>(pixels[2*index]) |
               (static_cast<unsigned char>(pixels[2*index+1])<<8);
    };
    for(int i=0;i<width*height;++i) {
        const unsigned int value=pixel(i);
        minimum=qMin(minimum,value); maximum=qMax(maximum,value);
    }
    QImage image(width,height,QImage::Format_RGB32);
    if(image.isNull()) return fail(tr("Unable to allocate the camera image."));
    for(int y=0;y<height;++y) {
        if(isInterruptionRequested()) return false;
        QRgb *line=reinterpret_cast<QRgb *>(image.scanLine(y));
        for(int x=0;x<width;++x) {
            const unsigned int value=pixel(y*width+x);
            const unsigned int gray=maximum==minimum ? 0 : 255*(value-minimum)/(maximum-minimum);
            line[x]=qRgb(gray,gray,gray);
        }
    }
    {
        QMutexLocker locker(&mutex);
        receivedImage=image;
    }
    emit newImage();
    emit statusChange(tr("Done."));
    return true;
}

void PSLCameraThread::run() {
    Request request;
    { QMutexLocker locker(&mutex); request=pending; }
    if(request.host.trimmed().isEmpty() || request.port==0 || request.binning<1 ||
       !validIntegrationTime(request.integrationTime)) {
        fail(tr("Invalid camera settings."));
        return;
    }
    if(!sendCommand(request,QString("SetBinning;%1;%2").arg(request.binning).arg(request.binning)) ||
       !sendCommand(request,QString("SetExposure;%1").arg(request.integrationTime*1000))) return;
    emit statusChange(tr("Starting image acquisition ..."));
    if(sendCommand(request,"Snap",true)) sendGetImage(request);
}
