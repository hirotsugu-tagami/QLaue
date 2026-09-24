#include <QApplication>
#include <QDir>
#include <QDebug>
#include <QFileInfo>
#include <QLineF>
#include <QPainter>
#include <QPrinter>
#include <QSemaphore>
#include <cmath>
#include "lauewidget.h"

static void require(bool ok, const char *message) {
    if(!ok) qFatal("%s",message);
}

static QPointF colorCenter(const QImage &image, bool red) {
    double xsum=0, ysum=0, count=0;
    for(int y=0; y<image.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for(int x=0; x<image.width(); ++x) {
            const QRgb pixel = row[x];
            // The synthetic markers use flat colors. Broad thresholds also
            // pick up colored fringes from Windows' subpixel text rendering.
            const bool match = pixel == (red ? qRgb(255,0,0) : qRgb(0,160,0));
            if(match) { xsum+=x; ysum+=y; ++count; }
        }
    }
    require(count>0,red ? "Calculated spot is missing" : "Imported reference marker is missing");
    return QPointF(xsum/count,ysum/count);
}

int main(int argc, char **argv) {
    QApplication app(argc,argv);
    require(argc==2,"Usage: print-alignment-check OUTPUT_DIRECTORY");
    QDir output(QString::fromLocal8Bit(argv[1]));
    require(QDir().mkpath(output.path()),"Cannot create output directory");
    Crystal crystal;
    crystal.setName("Print alignment regression (synthetic data)");
    LaueFilm film(nullptr,&crystal);
    // Pause the worker after calculation while rendering known test spots.
    QSemaphore calculated, resume;
    const auto paused = QObject::connect(film.getLaue(),&LaueThread::calculated,&film,[&]{
        calculated.release();
        resume.acquire();
    },Qt::DirectConnection);
    film.setDisplayIntensities(false);
    require(calculated.tryAcquire(1,10000),"Calculation did not finish");
    film.getLaue()->setNspots(1);
    LaueSpot *spot = film.getLaue()->getSpot(0);
    spot->setHKL(1,0,0);
    spot->setCharacteristic(false);
    spot->setDetI(1);
    film.showLabels(true);

    bool allAligned=true;
    for(int dpi : {72,300}) for(bool landscape : {false,true}) {
        // A known marker in the imported image and a calculated spot at that
        // same position must still overlap after changing the output device.
        const QSize sourceSize = landscape ? QSize(1000,400) : QSize(500,800);
        const QPoint marker = landscape ? QPoint(650,145) : QPoint(180,510);
        QImage source(sourceSize,QImage::Format_RGB32);
        source.fill(QColor(210,210,210));
        {
            QPainter painter(&source);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0,160,0));
            painter.drawEllipse(marker,25,25);
        }
        film.resize(landscape ? QSize(920,600) : QSize(580,850));
        film.setImage(source,true);
        film.setScale(5.5);
        film.setOriginX(12.3);
        film.setOriginY(-7.6);
        spot->setXYIN((marker.x()-(source.width()-1)/2.0)/film.scale()-film.originX(),
                     -(marker.y()-(source.height()-1)/2.0)/film.scale()-film.originY(),1,1);
        const QString name=QString("%1-%2dpi").arg(landscape ? "landscape" : "portrait").arg(dpi);
        require(film.grab().save(output.filePath(name+"-screen.png")),"Cannot save screen reference");
        const QImage screenScaledImage = *film.importedScaledImage;
        const double screenScale = film.imageScaleFactor();

        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(output.filePath(name+".pdf"));
        printer.setResolution(dpi);
        printer.setPaperSize(QPrinter::A4);
        printer.setOrientation(landscape ? QPrinter::Landscape : QPrinter::Portrait);
        QImage page(printer.width(),printer.height(),QImage::Format_RGB32);
        page.fill(Qt::white);
        {
            QPainter painter(&page);
            film.print(&printer,&painter,LaueFilm::NoPrintReorientation);
        }
        require(page.save(output.filePath(name+"-raster.png")),"Cannot save print raster");
        {
            QPainter painter;
            require(painter.begin(&printer),"Cannot start PDF printing");
            film.print(&printer,&painter,LaueFilm::NoPrintReorientation);
            require(painter.end(),"Cannot finish PDF printing");
        }
        require(QFileInfo(printer.outputFileName()).size()>1000,"PDF output is empty");
        const QPointF reference=colorCenter(page,false), calculated=colorCenter(page,true);
        const double error=QLineF(reference,calculated).length();
        const bool preserved=*film.importedScaledImage==screenScaledImage &&
                             std::abs(film.imageScaleFactor()-screenScale)<1e-12;
        qInfo().noquote() << name << "marker" << reference << "spot" << calculated
                         << "error (pixels)" << error << "screen state preserved" << preserved;
        allAligned &= error<3 && preserved;
    }
    QObject::disconnect(paused);
    resume.release();
    delete film.getLaue();
    delete film.getIndexing();
    require(allAligned,"Printed spots must overlap imported markers and preserve screen coordinates");
    qInfo() << "PASS: image/spot alignment and preserved screen state at 72/300 dpi, portrait/landscape";
    return 0;
}
