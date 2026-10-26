#include <QtWidgets>
#include <cmath>
#include <functional>
#include <limits>
#include "adjustimage.h"
#include "filmsizedialog.h"
#include "imagecontrols.h"
#include "lauewidget.h"

static void require(bool ok, const char *message) {
    if(!ok) qFatal("%s",message);
}

static void mouse(LaueFilm &film, QEvent::Type type, QPoint point,
                  Qt::MouseButton button = Qt::LeftButton,
                  Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    const Qt::MouseButtons buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : button;
    QMouseEvent event(type,point,type == QEvent::MouseMove ? Qt::NoButton : button,buttons,modifiers);
    QApplication::sendEvent(&film,&event);
}

static void answerDialog(const std::function<void()> &action,
                         const std::function<void(QDialog *)> &answer) {
    bool answered=false;
    QTimer timer;
    QObject::connect(&timer,&QTimer::timeout,[&] {
        auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if(!dialog) return;
        require(!answered,"Unexpected second dialog");
        answered=true;
        answer(dialog);
    });
    timer.start(10);
    action();
    require(answered,"Expected calibration dialog");
}

static void waitForCalculation(LaueFilm &film) {
    QEventLoop loop;
    bool complete=false;
    const auto connection=QObject::connect(film.getLaue(),&LaueThread::calculated,&loop,[&] {
        complete=true; loop.quit();
    });
    QTimer::singleShot(10000,&loop,&QEventLoop::quit);
    film.setDisplayIntensities(false);
    loop.exec();
    QObject::disconnect(connection);
    require(complete,"Calculation did not finish");
}

static void adjustmentChecks() {
    QImage levels(4,1,QImage::Format_RGB32);
    const int input[]={0,64,128,255}, expected[]={0,0,85,255};
    for(int x=0;x<4;++x) levels.setPixel(x,0,qRgb(input[x],input[x],input[x]));
    const QImage minimum=changeImageMin(levels,64);
    for(int x=0;x<4;++x)
        require(qRed(minimum.pixel(x,0))==expected[x],"Minimum must rescale the remaining tonal range");
    require(changeMin(192,128)==128 && changeMin(255,255)==255 && changeMin(0,255)==0,
            "Minimum midpoint/endpoints are incorrect");

    Crystal crystal;
    LaueFilm film(nullptr,&crystal);
    QImage source(4,3,QImage::Format_Indexed8);
    QVector<QRgb> palette;
    for(int i=0;i<256;++i) palette.append(qRgb(i,i,i));
    source.setColorTable(palette);
    for(int y=0;y<3;++y) for(int x=0;x<4;++x) source.setPixel(x,y,input[x]);
    film.setImage(source,true);
    film.adjustImage(10,75,200,40);
    const QImage adjusted=*film.importedAdjustedImage;
    require(qRed(adjusted.pixel(2,1))==159,"Combined image adjustments are incorrect");
    require(*film.importedImage==source,"Image adjustments changed source pixels");
    require(film.imageBrightness()==10 && film.imageContrast()==75 &&
            film.imageMaximum()==200 && film.imageMinimum()==40,"Adjustment state was not retained");
    film.setDisplayImage(false);
    require(!film.isImageDisplayed() && *film.importedAdjustedImage==adjusted,"Hide discarded adjustments");
    film.setDisplayImage(true);
    require(film.isImageDisplayed() && *film.importedAdjustedImage==adjusted,"Show discarded adjustments");
    film.setInvertImage(true);
    require(film.isImageInverted() && qRed(film.importedAdjustedImage->pixel(2,1))==157,
            "Invert did not apply the retained adjustments");
    film.setInvertImage(false);
    require(*film.importedAdjustedImage==adjusted,"Invert round trip lost adjustments");
    film.setRotateImage(true);
    require(film.isImageRotated() && film.importedAdjustedImage->size()==QSize(3,4) &&
            film.imageZoomRect==film.importedAdjustedImage->rect(),"Rotation crop mismatch");
    require(film.importedAdjustedImage->convertToFormat(QImage::Format_RGB32)==
            adjusted.transformed(QTransform(0,1,1,0,0,0)).convertToFormat(QImage::Format_RGB32),
            "Rotation discarded adjustments or changed the legacy transformation");
    film.resetZoom();
    require(film.imageZoomRect==film.importedAdjustedImage->rect(),"Reset zoom lost rotated image dimensions");
    film.setRotateImage(false);
    require(*film.importedAdjustedImage==adjusted,"Rotation round trip lost adjustments");
    for(int i=0;i<5;++i) film.setAdjustedImage();
    require(*film.importedAdjustedImage==adjusted,"Rebuilding an image accumulated adjustments");

    ImageControlWidget controls;
    QObject::connect(&controls,QOverload<double,double,double,double>::of(&ImageControlWidget::valueChanged),
                     &film,&LaueFilm::adjustImage);
    controls.findChild<QScrollBar *>("BrightnessScroll")->setValue(20);
    controls.findChild<QScrollBar *>("ContrastScroll")->setValue(60);
    controls.findChild<QScrollBar *>("MaximumScroll")->setValue(220);
    controls.findChild<QScrollBar *>("MinimumScroll")->setValue(30);
    QImage replacement(7,2,QImage::Format_RGB32); replacement.fill(QColor(30,60,90));
    film.setImage(replacement,true);
    controls.setDefaults();
    require(film.imageBrightness()==0 && film.imageContrast()==100 &&
            film.imageMaximum()==255 && film.imageMinimum()==0 &&
            *film.importedAdjustedImage==replacement,"New image did not reset controls and adjustments together");
    film.adjustImage(25,50,150,50);
    QImage empty; film.setImage(empty,false);
    require(film.importedAdjustedImage->isNull() && film.importedScaledImage->isNull() &&
            film.imageBrightness()==0 && film.imageContrast()==100 &&
            film.imageMaximum()==255 && film.imageMinimum()==0,"Clearing image retained adjustments");
    qInfo("PASS: minimum levels, retained adjustments, display/invert/rotate round trips and new-image reset");
}

static void calibrationChecks() {
    Crystal crystal;
    LaueFilm film(nullptr,&crystal);
    require(std::abs(film.scale()-1000.0/172)<1e-12,"Initial pixel size was truncated");
    film.setScale(17.25);
    for(double value : {0.0,-1.0,std::numeric_limits<double>::quiet_NaN(),
                        std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()}) {
        film.setScale(value);
        require(film.scale()==17.25,"Invalid scale replaced the previous calibration");
    }
    QImage image(24,16,QImage::Format_RGB32); image.fill(Qt::black); image.setDotsPerMeterX(3780);
    film.setImage(image,false); film.setPixelsPerMMFromImage();
    require(std::abs(film.scale()-3.78)<1e-12,"Image DPI conversion was truncated");
    QImage empty; film.setImage(empty,false); film.setPixelsPerMMFromImage();
    require(std::abs(film.scale()-3.78)<1e-12,"Empty image changed calibration");
    film.resize(640,520); film.grab();

    auto measure=[&](QPoint from,QPoint to,const QString &width) {
        film.measureScale();
        mouse(film,QEvent::MouseMove,from); // Moving before the first press is harmless.
        mouse(film,QEvent::MouseButtonPress,from);
        mouse(film,QEvent::MouseMove,to);
        answerDialog([&]{ mouse(film,QEvent::MouseButtonRelease,to); },[&](QDialog *dialog) {
            require(qobject_cast<FilmSizeDialog *>(dialog),"Expected measured-width dialog");
            dialog->findChild<QComboBox *>("FilmTypeComboBox")->setCurrentText("Other");
            dialog->findChild<QLineEdit *>("FilmWidth")->setText(width);
            require(QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection),"Cannot accept measured width");
        });
    };
    measure(QPoint(450,150),QPoint(250,150),"50");
    require(film.scale()==4,"Right-to-left measured width produced an incorrect scale");
    film.setScale(17.25);
    measure(QPoint(250,150),QPoint(450,150),"50");
    require(film.scale()==4,"Left-to-right measured width differs");
    for(const QString &width : {QString("0"),QString("-1"),QString("nan"),QString("invalid")}) {
        measure(QPoint(450,150),QPoint(250,150),width);
        require(film.scale()==4,"Invalid measured width changed calibration");
    }
    film.measureScale();
    mouse(film,QEvent::MouseButtonPress,QPoint(250,150));
    mouse(film,QEvent::MouseButtonRelease,QPoint(250,220));
    require(film.scale()==4 && !QApplication::activeModalWidget(),"Zero-width measurement must preserve calibration without a dialog");
    film.measureScale();
    mouse(film,QEvent::MouseButtonRelease,QPoint(300,220));
    require(film.scale()==4,"Release without measurement changed calibration");
    answerDialog([&]{ film.setImageScale(); },[](QDialog *dialog) {
        auto input=qobject_cast<QInputDialog *>(dialog);
        require(input,"Expected pixel-size dialog");
        input->setDoubleValue(0);
        input->accept();
    });
    require(film.scale()==4,"Zero pixel size changed calibration");
    answerDialog([&]{ film.setImageScale(); },[](QDialog *dialog) {
        auto input=qobject_cast<QInputDialog *>(dialog);
        require(input,"Expected pixel-size dialog");
        input->setDoubleValue(172);
        input->accept();
    });
    require(std::abs(film.scale()-1000.0/172)<1e-12,"Valid pixel size was not applied");
    qInfo("PASS: scale validation, fractional DPI/pixel size and safe bidirectional width measurement");
}

static QImage grabImage(LaueFilm &film) {
    const QPixmap pixmap=film.grab();
    QImage image=pixmap.toImage();
    image.setDevicePixelRatio(pixmap.devicePixelRatio());
    return image;
}

static QPointF colorCenter(const QImage &image, bool blue, const QRect &logicalArea=QRect()) {
    double xs=0,ys=0,count=0;
    const QRect area=logicalArea.isNull() ? image.rect() :
        QTransform::fromScale(image.devicePixelRatio(),image.devicePixelRatio())
            .mapRect(QRectF(logicalArea)).toAlignedRect().intersected(image.rect());
    for(int y=area.top();y<=area.bottom();++y) for(int x=area.left();x<=area.right();++x) {
        const QRgb p=image.pixel(x,y);
        // The green fixture is flat. The antialiased origin has no pure-blue
        // pixels at DPR 1, so restrict its coverage mask to the feature area.
        const bool match=blue ? qBlue(p)>qRed(p)+40 && qBlue(p)>qGreen(p)+40
                              : p==qRgb(0,160,0);
        if(match) { xs+=x; ys+=y; ++count; }
    }
    require(count>0,"Missing calibration marker");
    // Widget mouse events and comparison tolerances use logical pixels.
    return QPointF(xs/count,ys/count)/image.devicePixelRatio();
}

static bool hasCalculatedSpot(const QImage &image) {
    for(int y=qRound(50*image.devicePixelRatio());y<image.height();++y) for(int x=0;x<image.width();++x)
        if(image.pixel(x,y)==qRgb(255,0,0)) return true;
    return false;
}

static void originCalibrationChecks() {
    Crystal crystal; LaueFilm film(nullptr,&crystal);
    waitForCalculation(film);
    film.getLaue()->setNspots(1);
    auto *spot=film.getLaue()->getSpot(0);
    spot->setXYIN(20,-12,1,1); spot->setHKL(1,0,0); spot->setCharacteristic(false); spot->setDetI(1);
    film.showLabels(false);
    QImage source(600,600,QImage::Format_RGB32); source.fill(QColor(210,210,210));
    source.setDotsPerMeterX(3780);
    { QPainter p(&source); p.setPen(Qt::NoPen); p.setBrush(QColor(0,160,0)); p.drawEllipse(QPoint(350,250),15,15); }
    film.resize(700,700); film.setImage(source,true); film.setScale(5);
    film.setOriginX(12); film.setOriginY(-8);
    film.setCursor(Qt::PointingHandCursor); film.setMouseTracking(false);
    const auto escape=[&] {
        QKeyEvent event(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);
        QApplication::sendEvent(&film,&event);
    };
    for(bool simulation : {true,false}) {
        film.setDisplayLaue(simulation);
        const QImage before=grabImage(film);
        require(hasCalculatedSpot(before)==simulation,"Simulation visibility fixture is invalid");
        film.setOrigin();
        mouse(film,QEvent::MouseMove,QPoint(220,150),Qt::NoButton);
        require(!hasCalculatedSpot(grabImage(film)),"Origin selection did not temporarily hide the simulation");
        require(film.originX()==12 && film.originY()==-8,"Origin preview changed committed calibration");
        QKeyEvent bigger(QEvent::KeyPress,Qt::Key_Up,Qt::NoModifier);
        QApplication::sendEvent(&film,&bigger);
        escape();
        require(film.originX()==12 && film.originY()==-8 && film.getDisplayLaue()==simulation &&
                film.cursor().shape()==Qt::PointingHandCursor && !film.hasMouseTracking(),
                "Origin cancel did not restore state");
        require(grabImage(film)==before,"Origin cancel changed the displayed result");

        film.measureScale();
        require(!hasCalculatedSpot(grabImage(film)),"Scale measurement did not temporarily hide the simulation");
        mouse(film,QEvent::MouseButtonPress,QPoint(200,150));
        mouse(film,QEvent::MouseMove,QPoint(400,150));
        escape();
        require(film.findChild<QRubberBand *>() && film.findChild<QRubberBand *>()->isHidden(),
                "Canceled scale measurement left its selection visible");
        mouse(film,QEvent::MouseButtonRelease,QPoint(400,150));
        require(film.scale()==5 && film.originX()==12 && film.originY()==-8 &&
                film.getDisplayLaue()==simulation && !film.hasMouseTracking() &&
                film.cursor().shape()==Qt::PointingHandCursor && grabImage(film)==before,
                "Scale cancel or subsequent release changed state");

        film.setOrigin(); mouse(film,QEvent::MouseMove,QPoint(220,150),Qt::NoButton);
        film.measureScale(); film.setOrigin(); escape();
        require(film.originX()==12 && film.originY()==-8 && grabImage(film)==before &&
                !film.hasMouseTracking(),"Switching calibration tools retained a preview or temporary state");

        for(int tool=0;tool<3;++tool) {
            if(tool==0) {
                film.solveOrientation();
                require(!hasCalculatedSpot(grabImage(film)),"Spot selection did not temporarily hide the simulation");
            }
            else if(tool==1) film.setOrientation();
            else film.rotateAboutAxis();
            film.setOrigin(); mouse(film,QEvent::MouseMove,QPoint(220,150),Qt::NoButton); escape();
            require(film.getDisplayLaue()==simulation && film.cursor().shape()==Qt::PointingHandCursor &&
                    !film.hasMouseTracking() && grabImage(film)==before,
                    "A previous selection tool leaked its cursor or display state into calibration");
        }
    }

    // Pick an actual image feature, then change calibration through all three UI routes.
    film.setDisplayLaue(false);
    const QImage featureImage=grabImage(film);
    const QPoint marker=colorCenter(featureImage,false).toPoint();
    qInfo()<<"Origin feature: widget"<<film.size()<<"image"<<featureImage.size()
           <<"DPR"<<featureImage.devicePixelRatio()<<"logical click"<<marker;
    film.solveOrientation(); film.setOrigin(); mouse(film,QEvent::MouseButtonPress,marker);
    const QImage before=grabImage(film);
    const QRect beamArea(marker-QPoint(60,60),QSize(121,121));
    const QPointF beam=colorCenter(before,true,beamArea);
    qInfo()<<"Origin selection: image"<<before.size()<<"DPR"<<before.devicePixelRatio()
           <<"logical beam"<<beam<<"feature"<<colorCenter(before,false)
           <<"error"<<QLineF(beam,colorCenter(before,false)).length();
    require(QLineF(beam,colorCenter(before,false)).length()<2,"Initial beam selection missed the image feature");
    const QPointF pixelOffset(film.originX()*film.scale(),film.originY()*film.scale());
    auto unchangedBeam=[&] {
        require(QLineF(pixelOffset,QPointF(film.originX()*film.scale(),film.originY()*film.scale())).length()<1e-9,
                "Scale calibration moved the selected beam pixel");
        const QImage view=grabImage(film);
        qInfo()<<"Calibrated origin: image"<<view.size()<<"DPR"<<view.devicePixelRatio()
               <<"logical beam"<<colorCenter(view,true,beamArea)<<"feature"<<colorCenter(view,false);
        require(QLineF(beam,colorCenter(view,true,beamArea)).length()<1 &&
                QLineF(colorCenter(view,true,beamArea),colorCenter(view,false)).length()<2,
                "Calibrated beam marker no longer overlays the imported image feature");
        require(!film.getDisplayLaue() && !film.hasMouseTracking(),"Calibration changed the user's display/input state");
    };
    answerDialog([&]{ film.setImageScale(); },[](QDialog *dialog) {
        auto input=qobject_cast<QInputDialog *>(dialog); require(input,"Pixel-size dialog missing");
        input->setDoubleValue(100); input->accept();
    });
    require(film.scale()==10,"Pixel-size change not applied"); unchangedBeam();
    film.setPixelsPerMMFromImage();
    require(std::abs(film.scale()-3.78)<1e-12,"DPI change not applied"); unchangedBeam();
    film.solveOrientation(); film.measureScale(); mouse(film,QEvent::MouseButtonPress,QPoint(250,150));
    answerDialog([&]{ mouse(film,QEvent::MouseButtonRelease,QPoint(450,150)); },[](QDialog *dialog) {
        auto width=qobject_cast<FilmSizeDialog *>(dialog); require(width,"Width dialog missing");
        width->findChild<QComboBox *>("FilmTypeComboBox")->setCurrentText("Other");
        width->findChild<QLineEdit *>("FilmWidth")->setText("50");
        require(QMetaObject::invokeMethod(width,"accept",Qt::DirectConnection),"Cannot accept measured width");
    });
    unchangedBeam();
    const double scale=film.scale();
    answerDialog([&]{ film.setImageScale(); },[](QDialog *dialog) { dialog->reject(); });
    require(film.scale()==scale,"Canceled pixel-size dialog changed calibration"); unchangedBeam();

    // File restoration supplies both physical origin and scale; it must not recalibrate them.
    film.setOriginX(7.25); film.setOriginY(-4.125); film.setScale(9.3);
    require(film.originX()==7.25 && film.originY()==-4.125,"Raw state restoration rescaled the origin");
    film.setOrigin(); mouse(film,QEvent::MouseMove,QPoint(220,150),Qt::NoButton);
    QImage empty; film.setImage(empty,false); film.resetView(); escape();
    require(film.originX()==0 && film.originY()==0 && film.scale()==100,
            "A canceled tool restored calibration from the previous image");
    qInfo("PASS: origin preview/cancel/restart, tool switching and fixed beam pixels across scale/DPI/width changes");
}

static void dragAndLabelChecks() {
    Crystal crystal;
    LaueFilm film(nullptr,&crystal);
    film.resize(640,520); film.setScale(10); film.grab();
    waitForCalculation(film);
    const QPoint center=QRect(10,50,film.width()-20,film.height()-60).center();
    QVector<double> angles;
    QObject::connect(&film,&LaueFilm::rotationsChanged,&film,[&](double x,double y,double z) {
        require(std::isfinite(x) && std::isfinite(y) && std::isfinite(z),"Drag emitted a non-finite rotation");
        angles.append(x);
    });
    mouse(film,QEvent::MouseButtonPress,center,Qt::RightButton);
    mouse(film,QEvent::MouseMove,center+QPoint(2,0),Qt::RightButton);
    mouse(film,QEvent::MouseMove,center,Qt::RightButton);
    mouse(film,QEvent::MouseButtonRelease,center,Qt::RightButton);
    require(angles.isEmpty(),"Drag to/from the origin must not rotate");
    mouse(film,QEvent::MouseButtonPress,center+QPoint(1,1),Qt::RightButton);
    mouse(film,QEvent::MouseMove,center+QPoint(1,0),Qt::RightButton);
    mouse(film,QEvent::MouseButtonRelease,center+QPoint(1,0),Qt::RightButton);
    require(angles.size()==1 && std::abs(angles.first()+M_PI/4)<1e-12,
            "Small rotation lost its sign through integer truncation");
    const Matrix before=crystal.getU();
    film.rotateAboutAxis();
    mouse(film,QEvent::MouseButtonPress,center);
    mouse(film,QEvent::MouseButtonPress,center);
    mouse(film,QEvent::MouseMove,center+QPoint(5,0));
    mouse(film,QEvent::MouseButtonRelease,center+QPoint(5,0));
    const Matrix after=crystal.getU();
    for(int r=0;r<3;++r) for(int c=0;c<3;++c)
        require(before.Get(r,c)==after.Get(r,c),"Dragging from the selected axis changed orientation");

    film.clearMessage(); film.getLaue()->setNspots(1);
    LaueSpot *spot=film.getLaue()->getSpot(0);
    spot->setXYIN(0,0,1,1); spot->setCharacteristic(false); spot->setDetI(1);
    const QRect labelArea(center+QPoint(4,-12),QSize(160,40));
    for(int i=0;i<199;++i) {
        spot->setHKL(10+i,0,0);
        mouse(film,QEvent::MouseButtonPress,center,Qt::LeftButton,Qt::ShiftModifier);
    }
    const QImage lastAccepted=film.grab(labelArea).toImage();
    spot->setHKL(1000,0,0);
    for(int i=0;i<250;++i) {
        spot->setHKL(1000+i,0,0);
        mouse(film,QEvent::MouseButtonPress,center,Qt::LeftButton,Qt::ShiftModifier);
    }
    const QImage refused=film.grab(labelArea).toImage();
    film.resetLabels();
    require(film.grab(labelArea).toImage()==refused,"Label overflow should refuse extra labels");
    spot->setHKL(208,0,0);
    require(film.grab(labelArea).toImage()!=lastAccepted,"The last in-bounds label was not retained");
    mouse(film,QEvent::MouseButtonPress,center,Qt::LeftButton,Qt::ShiftModifier);
    require(film.grab(labelArea).toImage()==lastAccepted,"Clearing labels did not allow additions again");
    qInfo("PASS: finite signed drags and 199 persistent labels with safe overflow/reset");
}

int main(int argc,char **argv) {
    QApplication app(argc,argv);
    QTemporaryDir temporary; require(temporary.isValid(),"Temporary directory");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temporary.path());
    QCoreApplication::setOrganizationName("QLaueTests");
    QCoreApplication::setApplicationName("ImageState");
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout,&QTimer::timeout,[]{ qFatal("Image-state test timed out"); });
    timeout.start(30000);
    adjustmentChecks(); calibrationChecks(); originCalibrationChecks(); dragAndLabelChecks();
    return 0;
}
