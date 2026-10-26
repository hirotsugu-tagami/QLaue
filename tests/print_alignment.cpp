#include <QApplication>
#include <QDir>
#include <QDebug>
#include <QFileInfo>
#include <QLineF>
#include <QPainter>
#include <QPrinter>
#include <QEventLoop>
#include <QTimer>
#include <cmath>
#include "htmlgenerators.h"
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

static bool hasColor(const QImage &image, QRgb color) {
    for(int y=0; y<image.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for(int x=0; x<image.width(); ++x)
            if(row[x] == color) return true;
    }
    return false;
}

int main(int argc, char **argv) {
    QApplication app(argc,argv);
    require(argc==2,"Usage: print-alignment-check OUTPUT_DIRECTORY");
    QDir output(QString::fromLocal8Bit(argv[1]));
    require(QDir().mkpath(output.path()),"Cannot create output directory");
    Crystal crystal;
    crystal.setName("Print alignment regression (synthetic data)");
    LaueFilm film(nullptr,&crystal);
    // Results are now published on the GUI thread only after the worker has
    // finished. Wait for that stable snapshot before installing known spots.
    QEventLoop ready;
    bool completed = false;
    const auto finished = QObject::connect(film.getLaue(),&LaueThread::calculated,&ready,[&] {
        completed = true;
        ready.quit();
    });
    QTimer::singleShot(10000,&ready,&QEventLoop::quit);
    film.setDisplayIntensities(false);
    ready.exec();
    QObject::disconnect(finished);
    require(completed,"Calculation did not finish");
    film.getLaue()->setNspots(1);
    LaueSpot *spot = film.getLaue()->getSpot(0);
    spot->setHKL(1,0,0);
    spot->setCharacteristic(false);
    spot->setDetI(1);
    film.showLabels(true);
    const QString html=HtmlGenerators::laueToHtml(film.getLaue());
    require(html.count("Detector threshold:")==1 && !html.contains("Detector threshhold:"),
            "Printed detector threshold must appear once with the correct label");

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

        film.setDisplayImage(false);
        // Hidden-image geometry must track screen resizing as well as printer DPI.
        film.resize(film.width()+53,film.height()+71);
        film.grab();
        const QImage hiddenScreenImage=*film.importedScaledImage;
        const double hiddenScreenScale=film.imageScaleFactor();
        QImage hidden(page.size(),QImage::Format_RGB32); hidden.fill(Qt::white);
        { QPainter painter(&hidden); film.print(&printer,&painter,LaueFilm::NoPrintReorientation); }
        const double hiddenError=QLineF(calculated,colorCenter(hidden,true)).length();
        require(hiddenError<1,"Hiding the image or resizing the screen changed printed spot coordinates");
        require(*film.importedScaledImage==hiddenScreenImage &&
                std::abs(film.imageScaleFactor()-hiddenScreenScale)<1e-12 && !film.isImageDisplayed(),
                "Hidden-image print changed interactive geometry or visibility");
        require(hidden.save(output.filePath(name+"-hidden.png")),"Cannot save hidden-image print evidence");

        // Hiding the simulation must affect screen and print independently of
        // image visibility, without changing either calibration or geometry.
        film.setDisplayLaue(false);
        for(bool imageVisible : {false,true}) {
            film.setDisplayImage(imageVisible);
            const QImage screen=film.grab().toImage().convertToFormat(QImage::Format_RGB32);
            require(!hasColor(screen,qRgb(255,0,0)),"Hidden simulation still appears on screen");
            const QImage currentScreenImage=*film.importedScaledImage;
            const double currentScreenScale=film.imageScaleFactor();
            QImage noSimulation(page.size(),QImage::Format_RGB32); noSimulation.fill(Qt::white);
            { QPainter painter(&noSimulation); film.print(&printer,&painter,LaueFilm::NoPrintReorientation); }
            require(!hasColor(noSimulation,qRgb(255,0,0)),"Hidden simulation still appears in print");
            require(hasColor(noSimulation,qRgb(0,160,0))==imageVisible,
                    "Simulation visibility changed the printed image visibility");
            if(imageVisible)
                require(QLineF(reference,colorCenter(noSimulation,false)).length()<1,
                        "Hiding the simulation moved the printed image");
            require(!film.getDisplayLaue() && film.isImageDisplayed()==imageVisible &&
                    *film.importedScaledImage==currentScreenImage &&
                    std::abs(film.imageScaleFactor()-currentScreenScale)<1e-12,
                    "Hidden-simulation print changed interactive geometry or display settings");
            require(noSimulation.save(output.filePath(name+(imageVisible ? "-nosim-image.png" : "-nosim-hidden.png"))),
                    "Cannot save hidden-simulation print evidence");
        }
        printer.setOutputFileName(output.filePath(name+"-nosim.pdf"));
        {
            QPainter painter;
            require(painter.begin(&printer),"Cannot start hidden-simulation PDF printing");
            film.print(&printer,&painter,LaueFilm::NoPrintReorientation);
            require(painter.end(),"Cannot finish hidden-simulation PDF printing");
        }
        require(QFileInfo(printer.outputFileName()).size()>1000,"Hidden-simulation PDF output is empty");
        film.setDisplayLaue(true);
        film.setDisplayImage(true);
    }
    require(allAligned,"Printed spots must overlap imported markers and preserve screen coordinates");
    qInfo() << "PASS: visible/hidden image and simulation, preserved print state and one detector threshold at 72/300 dpi, portrait/landscape";
    return 0;
}
