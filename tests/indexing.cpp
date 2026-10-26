#include <QApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QProgressDialog>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <cmath>
#include <cstdlib>
#include <limits>
#include "mainwindow.h"
#include "indexingdialog.h"
#include "setubmat.h"

static void require(bool ok, const QString &message) {
    if(!ok) { qCritical().noquote() << "FAIL:" << message; std::exit(1); }
}

static double matrixError(const Matrix &a, const Matrix &b) {
    double error = 0;
    for(int i=0;i<3;++i) for(int j=0;j<3;++j)
        error = std::max(error,std::abs(a.Get(i,j)-b.Get(i,j)));
    return error;
}

static Matrix knownOrientation() {
    Matrix r(3,3);
    return r.rotateZ(0.11) * r.rotateY(-0.17) * r.rotateX(0.13);
}

static QVector<QPointF> observations(Crystal &crystal) {
    // Independent forward construction by reflecting the incident ray in the
    // lattice-plane normal. No calls to the indexing or calcHKL implementation.
    const int indices[][3] = {{3,1,0},{3,0,1},{2,-1,1},{3,1,1},{2,1,-1},{1,0,0}};
    QVector<QPointF> points;
    for(const auto &hkl : indices) {
        Matrix n = knownOrientation() * crystal.getB() * Matrix(hkl[0],hkl[1],hkl[2]);
        n = n/n.mag();
        Matrix ray = Matrix(-1,0,0) + n*(2*n.Get(0,0));
        require(ray.Get(0,0)>0,"Test reflection does not reach the back-reflection detector");
        points.append(QPointF(-35*ray.Get(1,0)/ray.Get(0,0),35*ray.Get(2,0)/ray.Get(0,0)));
    }
    return points;
}

static void configure(lauegram &engine) {
    engine.setDistance(35);
    engine.setLambdaRange(0.3,3);
    engine.setIndexingMaxH(3);
    engine.setIndexingMaxK(1);
    engine.setIndexingMaxL(1);
    engine.setIndexingAngularTol(0.5*M_PI/180);
    engine.setIndexingSpotTol(0.25);
}

static void checkRecovery(Crystal &crystal, const QString &name) {
    lauegram engine;
    configure(engine);
    const Matrix original = crystal.getU();
    const auto points = observations(crystal);
    int progress = -1;
    auto result = engine.indexLaue(crystal,points,[&](int value) {
        require(value>=progress && value<=100,"Progress is not monotonic");
        progress=value;
        return false;
    });
    require(result.error.isEmpty() && !result.canceled && progress==100,name+": search did not complete");
    require(!result.orientations.isEmpty(),name+": no solutions");
    require(result.orientations.first().getNumMatches()==points.size(),name+": best solution does not match all spots");
    bool recovered = false;
    Matrix identity(3,3); identity.identity();
    for(int i=0;i<result.orientations.size();++i) {
        const auto &solution = result.orientations[i];
        Matrix u=solution.getU();
        require(matrixError(u*u.transpose(),identity)<1e-10 && std::abs(u.det()-1)<1e-10,
                name+": solution is not a proper rotation");
        require(std::isfinite(solution.getPatternError()),name+": invalid score");
        if(i) require(!(solution<result.orientations[i-1]),name+": candidates are not ranked");
        if(matrixError(u,knownOrientation())<1e-8) {
            recovered=true;
            require(solution.getNumMatches()==points.size() && solution.getPatternError()<1e-8,
                    name+": known orientation has incorrect matches or RMS");
        }
    }
    require(recovered,name+": known orientation not recovered");
    require(matrixError(crystal.getU(),original)==0 && engine.calcIntensities(),"Indexing modified the source/settings");

    auto noisy=points;
    for(int i=0;i<noisy.size();++i) noisy[i]+=QPointF(0.012*(i%3-1),0.013*((i+1)%3-1));
    noisy.append(QPointF(1234,2345));
    result=engine.indexLaue(crystal,noisy);
    require(!result.orientations.isEmpty() && result.orientations.first().getNumMatches()==points.size(),
            name+": noisy observations and outlier ranked incorrectly");
    require(result.orientations.first().getPatternError()<0.1,name+": noisy RMS is too large");
    qInfo().noquote()<<"PASS:"<<name<<"known orientation, noisy spots, outlier, proper rotations and RMS ranking";
}

static void checkBoundaries(Crystal &crystal) {
    lauegram engine;
    configure(engine);
    auto points=observations(crystal);
    auto duplicate=points; duplicate[1]=duplicate[0];
    require(!engine.indexLaue(crystal,duplicate).error.isEmpty(),"Duplicate seed accepted");
    require(!engine.indexLaue(crystal,QVector<QPointF>(101,QPointF(1,1))).error.isEmpty(),"Too many spots accepted");
    auto invalid=points; invalid[0].setX(std::numeric_limits<double>::quiet_NaN());
    require(!engine.indexLaue(crystal,invalid).error.isEmpty(),"NaN spot accepted");
    engine.setIndexingMaxH(-1);
    require(!engine.indexLaue(crystal,points).error.isEmpty(),"Negative range accepted");
    engine.setIndexingMaxH(21);
    require(!engine.indexLaue(crystal,points).error.isEmpty(),"Unbounded range accepted");
    configure(engine); engine.setDistance(0);
    require(engine.getDistance()>0 && engine.indexLaue(crystal,points).error.isEmpty(),
            "Invalid detector distance replaced a working indexing configuration");
    configure(engine); engine.setLambdaRange(3,0.3);
    require(engine.getLambdaRangeMin()==0.3 && engine.getLambdaRangeMax()==3 &&
            engine.indexLaue(crystal,points).error.isEmpty(),"Reversed wavelength range replaced valid indexing settings");
    configure(engine);
    auto canceled=engine.indexLaue(crystal,points,[](int value){ return value>=10; });
    require(canceled.canceled && canceled.orientations.isEmpty() && engine.calcIntensities(),"Cancellation published partial candidates");

    // Extra picks near one predicted spot must not all count as different hits.
    points.append(points[2]+QPointF(0.001,0.001));
    auto result=engine.indexLaue(crystal,points);
    require(!result.orientations.isEmpty() && result.orientations.first().getNumMatches()==6,
            "One predicted reflection counted more than once");

    engine.setIndexingMaxH(4); engine.setIndexingMaxK(4); engine.setIndexingMaxL(4);
    engine.setIndexingAngularTol(1.0); engine.setIndexingSpotTol(1000);
    result=engine.indexLaue(crystal,points.mid(0,2));
    require(result.truncated && result.orientations.size()==lauegram::MaxIndexingResults,"Candidate limit not enforced");
    for(int i=1;i<result.orientations.size();++i)
        require(!(result.orientations[i]<result.orientations[i-1]),"Capped candidates not ranked");

    Crystal simple;
    simple.setLattice(1,1,1,M_PI/2,M_PI/2,M_PI/2);
    Matrix identity(3,3); identity.identity(); simple.setU(identity);
    engine.setCalcIntensities(false);
    LaueSpot spot;
    engine.setLambdaRange(0.6,0.7);
    require(engine.calcHKL(&simple,&spot,1,0,0,true),"In-band third-order reflection rejected");
    engine.setLambdaRange(0.7,0.8);
    require(!engine.calcHKL(&simple,&spot,1,0,0,true),"Out-of-band harmonics accepted");
    simple.setSpaceGroupNum(225);
    simple.delAllAtoms(); simple.addAtom(29,0,0,0);
    engine.setLambdaRange(1.9,2.1);
    require(engine.calcHKL(&simple,&spot,1,0,0,true),"Position-only prediction used the intensity filter");
    require(!engine.calcHKL(&simple,&spot,1,0,0),"Position-only indexing changed the simulation extinction filter");
    require(!engine.isFundamental(0,0,0),"Zero reflection accepted");
    qInfo()<<"PASS: invalid/duplicate input, one-to-one matches, cancellation, best-candidate limit and wavelength harmonics";
}

static void checkWorker(Crystal &crystal) {
    LaueIndexingThread worker;
    configure(worker);
    auto points=observations(crystal);
    Crystal changed(crystal);
    require(worker.indexLaue(changed,points),"Worker did not start");
    changed.setLattice(9,12,13,M_PI/2,M_PI/2,M_PI/2);
    require(!worker.indexLaue(crystal,points),"Concurrent indexing request accepted");
    require(worker.wait(10000),"Asymmetric search ranges did not finish");
    require(!worker.result().orientations.isEmpty(),"Worker did not publish solutions");
    require(worker.result().orientations.first().getNumMatches()==points.size() &&
            worker.result().orientations.first().getPatternError()<1e-8,"Worker did not use its crystal snapshot");
    worker.setIndexingMaxH(20); worker.setIndexingMaxK(20); worker.setIndexingMaxL(20);
    require(worker.indexLaue(crystal,points),"Worker did not restart");
    worker.requestInterruption();
    require(worker.wait(3000) && worker.result().canceled,"Worker did not stop promptly");
    configure(worker);
    require(worker.indexLaue(crystal,points) && worker.wait(10000),"Restart after cancellation failed");
    require(!worker.result().canceled && !worker.result().orientations.isEmpty(),"Cancellation leaked into next run");
    qInfo()<<"PASS: worker completion, concurrent-start rejection, cancellation and repeated restart";
}

static void click(LaueFilm *film, const QPointF &point) {
    const QPoint center=QRect(10,50,film->width()-20,film->height()-60).center();
    const double scale=film->scale()/film->imageScaleFactor();
    const QPoint pixel=center+QPoint(qRound((point.x()+film->originX())*scale),
                                     qRound(-(point.y()+film->originY())*scale));
    require(film->rect().contains(pixel),"Test spot outside film");
    QMouseEvent event(QEvent::MouseButtonPress,pixel,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(film,&event);
}

static void checkUi() {
    MainWindow window;
    window.resize(1600,1000);
    window.show();
    auto *film=window.findChild<LaueFilm *>();
    require(film,"Film not found");
    QEventLoop ready;
    const auto calculated=QObject::connect(film->getLaue(),&LaueThread::calculated,&ready,&QEventLoop::quit);
    QTimer::singleShot(10000,&ready,&QEventLoop::quit);
    film->reCalc(); ready.exec(); QObject::disconnect(calculated);
    configure(*film->getIndexing());
    auto points=observations(*film->getCrystal());
    double maxX=1,maxY=1;
    for(const auto &point : points) { maxX=std::max(maxX,std::abs(point.x())); maxY=std::max(maxY,std::abs(point.y())); }
    film->setScale(std::min((film->width()-40)/(2*maxX),(film->height()-80)/(2*maxY)));
    const Matrix original=film->getCrystal()->getU();
    film->solveOrientation();
    QApplication::processEvents();
    // ASan exercises the former 100-element overflow through real mouse events.
    for(int i=0;i<105;++i) click(film,QPointF((i%15-7)*2/film->scale(),(i/15-3)*2/film->scale()));
    film->solveOrientation();
    for(const QPointF &point : points) click(film,point);
    click(film,points.last()); // Same pixel is ignored.
    QKeyEvent undo(QEvent::KeyPress,Qt::Key_Backspace,Qt::NoModifier);
    QApplication::sendEvent(film,&undo);
    click(film,points.last());
    bool sawParameters=false;
    QTimer driver;
    QObject::connect(&driver,&QTimer::timeout,[&] {
        if(auto *dialog=qobject_cast<IndexingDialog *>(QApplication::activeModalWidget())) {
            sawParameters=true;
            dialog->findChild<QLineEdit *>("spotAccuracy")->setText("2");
            dialog->findChild<QLineEdit *>("angTol")->setText("3");
            const QString screenshot=qEnvironmentVariable("QLAUE_INDEXING_SCREENSHOT");
            if(!screenshot.isEmpty()) require(dialog->grab().save(screenshot+".parameters.png"),"Cannot save parameters screenshot");
            dialog->accept();
        } else if(auto *message=qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            require(false,"Unexpected indexing dialog: "+message->text());
        }
    });
    driver.start(10);
    QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
    QApplication::sendEvent(film,&enter);
    driver.stop();
    require(sawParameters && !film->getOrientations().isEmpty(),"GUI indexing returned no candidates");
    require(matrixError(film->getCrystal()->getU(),original)==0,"Search applied a candidate without selection");
    require(std::abs(film->getIndexing()->getLambdaRangeMin()-0.3)<1e-10 &&
            std::abs(film->getIndexing()->getLambdaRangeMax()-3)<1e-10,"GUI reversed the wavelength range");
    const auto *dock=window.findChild<QDockWidget *>("Indexing");
    auto *table=dock->findChild<QTableWidget *>();
    require(table && table->rowCount()==film->getOrientations().size(),"Results table is incomplete");
    require(table->horizontalHeaderItem(3)->text()=="RMS (mm)","Score units missing");
    table->setCurrentCell(0,0);
    const Matrix selected=film->getOrientations().first().getU();
    require(matrixError(film->getCrystal()->getU(),selected)==0,"Selected orientation not applied");
    table->clearSelection(); table->setCurrentCell(-1,-1);
    require(matrixError(film->getCrystal()->getU(),selected)==0,"Clearing selection changed orientation");
    const QString screenshot=qEnvironmentVariable("QLAUE_INDEXING_SCREENSHOT");
    if(!screenshot.isEmpty()) require(window.grab().save(screenshot),"Cannot save GUI screenshot");

    // Cancel a long run through the actual progress dialog, then preserve its prior results.
    const int previousCount=film->getOrientations().size();
    bool canceled=false;
    QObject::disconnect(&driver,nullptr,nullptr,nullptr);
    QObject::connect(&driver,&QTimer::timeout,[&] {
        if(auto *dialog=qobject_cast<IndexingDialog *>(QApplication::activeModalWidget())) {
            for(const char *name : {"maxH","maxK","maxL"}) dialog->findChild<QLineEdit *>(name)->setText("20");
            dialog->accept();
        } else if(auto *progress=qobject_cast<QProgressDialog *>(QApplication::activeModalWidget())) {
            canceled=true;
            auto *button=progress->findChild<QPushButton *>();
            require(button,"Cancel button not found"); button->click();
        }
    });
    driver.start(10);
    QElapsedTimer elapsed; elapsed.start();
    film->resolveOrientation(); driver.stop();
    require(canceled && elapsed.elapsed()<3000 && !film->getIndexing()->isRunning(),"GUI cancel did not stop the computation");
    require(film->getOrientations().size()==previousCount && matrixError(film->getCrystal()->getU(),selected)==0,
            "Cancellation changed the results or orientation");

    bool emptyReported=false;
    QObject::disconnect(&driver,nullptr,nullptr,nullptr);
    QObject::connect(&driver,&QTimer::timeout,[&] {
        if(auto *dialog=qobject_cast<IndexingDialog *>(QApplication::activeModalWidget())) {
            dialog->findChild<QLineEdit *>("maxH")->setText("0");
            dialog->findChild<QLineEdit *>("maxK")->setText("0");
            dialog->findChild<QLineEdit *>("maxL")->setText("1");
            dialog->accept();
        } else if(auto *message=qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            emptyReported=message->text().contains("No matching orientation");
            message->accept();
        }
    });
    driver.start(10); film->resolveOrientation(); driver.stop();
    require(emptyReported && table->rowCount()==0 && matrixError(film->getCrystal()->getU(),selected)==0,
            "Empty search retained stale candidates or changed orientation");
    film->clearOrientation();
    require(table->rowCount()==0 && film->getOrientations().isEmpty(),"Clearing observations retained stale candidates");
    qInfo()<<"PASS: manual picks/undo/Enter, candidate table, explicit apply, empty selection, GUI cancel and stale-result clearing";

    QObject::disconnect(&driver,nullptr,nullptr,nullptr);
    bool sawOrientation=false;
    QObject::connect(&driver,&QTimer::timeout,[&] {
        if(auto *dialog=qobject_cast<UMatrixDialog *>(QApplication::activeModalWidget())) {
            sawOrientation=true;
            const char *names[]={"PH","PK","PL","SH","SK","SL"};
            const char *values[]={"1","0","0","0","0","1"};
            for(int i=0;i<6;++i) dialog->findChild<QLineEdit *>(names[i])->setText(values[i]);
            require(QMetaObject::invokeMethod(dialog,"calcClicked",Qt::DirectConnection),"Cannot accept two-spot orientation");
        }
    });
    const Matrix beforeManual=film->getCrystal()->getU();
    film->setOrientation();
    click(film,QPointF(60/film->scale(),0));
    driver.start(10); click(film,QPointF(0,60/film->scale())); driver.stop();
    require(sawOrientation && matrixError(beforeManual,film->getCrystal()->getU())>1e-5,
            "Two-spot orientation did not change the model");
    const auto manualComplete=QObject::connect(film->getLaue(),&LaueThread::calculated,&ready,&QEventLoop::quit);
    QTimer::singleShot(10000,&ready,&QEventLoop::quit); ready.exec();
    QObject::disconnect(manualComplete);
    require(!film->getLaue()->isCalculating(),"Two-spot calculation did not finish");
    const Matrix axes[]={film->getCrystal()->xAxis(),film->getCrystal()->yAxis(),film->getCrystal()->zAxis()};
    for(int a=0;a<3;++a) for(int c=0;c<3;++c) {
        const auto *label=window.findChild<QLabel *>(QString("OrientationLine%1_%2").arg(a+1).arg(c+1));
        require(label && label->text()==QString("%1").arg(axes[a].Get(c,0),5,'f',3),
                "Orientation dock does not match the accepted two-spot model");
    }
    qInfo()<<"PASS: accepted two-spot orientation and all nine dock components remain synchronized";
}

static void checkInputChanges() {
    MainWindow window;
    window.setMaximumSize(2000,1400); window.resize(1600,1100); window.show();
    auto *film=window.findChild<LaueFilm *>();
    auto *table=window.findChild<QDockWidget *>("Indexing")->findChild<QTableWidget *>();
    auto invoke=[&](const char *name) {
        auto *action=window.findChild<QAction *>(name);
        require(action && action->isEnabled() && action->isVisible(),QString("Missing action: ")+name);
        action->trigger(); QApplication::processEvents();
    };
    QImage source(1000,1000,QImage::Format_RGB32); source.fill(Qt::lightGray);
    QTemporaryDir imageFiles;
    const QString imagePath=imageFiles.filePath("input.bmp");
    require(source.save(imagePath),"Cannot write indexing image fixture");
    QPointer<QFileDialog> selectedFile;
    QTimer importer;
    QObject::connect(&importer,&QTimer::timeout,[&] {
        if(!QApplication::activeModalWidget()) return;
        auto *dialog=qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
        require(dialog,"Expected image picker");
        if(selectedFile==dialog) {
            QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
        } else {
            dialog->setDirectory(imageFiles.path());dialog->selectFile(imagePath);
            dialog->findChild<QLineEdit *>("fileNameEdit")->setText(imagePath);selectedFile=dialog;
        }
    });
    importer.start(10);invoke("actionImport_Image");importer.stop();
    auto prepare=[&] {
        film->setImage(source,true); film->setRotateImage(false);
        film->setOriginX(0); film->setOriginY(0); film->setScale(2);
        film->setFSDistance(35); film->getLaue()->setLambdaRange(.3,3);
        film->grab();
    };
    auto search=[&] {
        invoke("actionSolve_Orientation"); film->grab();
        for(const auto &point:observations(*film->getCrystal())) click(film,point);
        QTimer driver;
        QObject::connect(&driver,&QTimer::timeout,[&] {
            if(auto *dialog=qobject_cast<IndexingDialog *>(QApplication::activeModalWidget())) {
                const char *names[]={"maxH","maxK","maxL","angTol","spotAccuracy"};
                const char *values[]={"3","1","1","3","5"};
                for(int i=0;i<5;++i) dialog->findChild<QLineEdit *>(names[i])->setText(values[i]);
                dialog->accept();
            } else if(auto *message=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                require(false,"Unexpected search message: "+message->text());
        });
        driver.start(10);
        QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
        QApplication::sendEvent(film,&enter); driver.stop();
        // These cases check input lifetime. Exact geometric recovery is checked above;
        // screen-pixel rounding can change how many of the six picks match here.
        require(!film->getOrientations().isEmpty() && film->getOrientations().first().getNumMatches()>=2,
                "UI state fixture did not generate valid candidates");
        require(table->rowCount()==film->getOrientations().size(),"Candidate table fixture mismatch");
    };
    auto needsNewPicks=[&] {
        require(film->getOrientations().isEmpty() && table->rowCount()==0,"Input change kept old candidates");
        bool warned=false;
        QTimer driver;
        QObject::connect(&driver,&QTimer::timeout,[&] {
            if(!QApplication::activeModalWidget()) return;
            auto *message=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            require(message && message->text().contains("at least two"),"Resolve reused stale selections");
            warned=true; message->accept();
        });
        driver.start(10); invoke("actionResolve_Orientation"); driver.stop();
        require(warned,"Resolve did not require fresh picks");
    };
    auto scaleDialog=[&](double value,bool accept) {
        QTimer driver;
        QObject::connect(&driver,&QTimer::timeout,[&] {
            if(!QApplication::activeModalWidget()) return;
            auto *dialog=qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
            require(dialog,"Expected pixel-size input");dialog->setDoubleValue(value);
            if(accept) dialog->accept(); else dialog->reject();
        });
        driver.start(10); invoke("actionImage_Scale"); driver.stop();
    };
    prepare(); search();
    const int count=film->getOrientations().size();
    scaleDialog(500,true); scaleDialog(1000,false); film->setScale(0);
    film->setInvertImage(true); film->adjustImage(10,90,245,5);
    film->setDisplayImage(false);film->setDisplayImage(true);
    invoke("actionDefine_Origin");
    QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(film,&escape);
    require(film->getOrientations().size()==count,"No-op/canceled calibration or tonal adjustments discarded valid analysis");
    scaleDialog(1000,true);needsNewPicks();
    prepare();search();invoke("actionDefine_Origin");
    QMouseEvent origin(QEvent::MouseButtonPress,QPoint(200,150),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(film,&origin);needsNewPicks();
    prepare();search();film->setOriginX(1);needsNewPicks();
    prepare();search();film->setOriginY(-1);needsNewPicks();
    prepare();search();film->setImage(source,true);needsNewPicks();
    prepare();search();invoke("actionRotate_Image");needsNewPicks();
    prepare();search();invoke("actionReset");needsNewPicks();

    // A geometry/range edit invalidates scores while retaining the usable mm picks.
    auto hasReusablePicks=[&] {
        require(film->getOrientations().isEmpty() && table->rowCount()==0,"Geometry edit retained old scores");
        bool parameters=false;QTimer driver;
        QObject::connect(&driver,&QTimer::timeout,[&] {
            if(!QApplication::activeModalWidget()) return;
            auto *dialog=qobject_cast<IndexingDialog *>(QApplication::activeModalWidget());
            require(dialog,"Valid picks were lost after a detector/range edit");parameters=true;dialog->reject();
        });
        driver.start(10);invoke("actionResolve_Orientation");driver.stop();require(parameters,"Resolve did not offer a new search");
    };
    prepare();search();
    auto *distance=window.findChild<LaueControls *>()->findChild<QDoubleSpinBox *>("sampleFilmD");
    distance->setValue(36);QMetaObject::invokeMethod(distance,"editingFinished",Qt::DirectConnection);hasReusablePicks();
    prepare();search();
    QTimer preferences;
    QObject::connect(&preferences,&QTimer::timeout,[&] {
        if(!QApplication::activeModalWidget()) return;
        auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());require(dialog,"Expected Preferences");
        dialog->findChild<QDoubleSpinBox *>("LaueTubeVoltageSpinBox")->setValue(60);dialog->accept();
    });
    preferences.start(10);invoke("actionPreferences");preferences.stop();hasReusablePicks();

    // Input changes queued during a modal search must never publish a stale worker snapshot.
    prepare();search();const Matrix orientation=film->getCrystal()->getU();
    bool invalidated=false;QTimer driver;
    QObject::connect(&driver,&QTimer::timeout,[&] {
        if(auto *dialog=qobject_cast<IndexingDialog *>(QApplication::activeModalWidget())) {
            for(const char *name:{"maxH","maxK","maxL"}) dialog->findChild<QLineEdit *>(name)->setText("20");
            dialog->accept();
        } else if(qobject_cast<QProgressDialog *>(QApplication::activeModalWidget()) && !invalidated) {
            invalidated=true;film->setImage(source,true);
        } else if(auto *message=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            require(false,"Stale worker published a result: "+message->text());
    });
    driver.start(10);invoke("actionResolve_Orientation");driver.stop();
    require(invalidated && film->getOrientations().isEmpty() && table->rowCount()==0 && !film->getIndexing()->isRunning(),
            "Changed-input search retained or published stale candidates");
    require(matrixError(orientation,film->getCrystal()->getU())==0,"Invalidated search changed the crystal orientation");
    prepare();search();
    const Matrix beforeManual=film->getCrystal()->getU();bool manualInvalidated=false;
    invoke("actionSet_Laue_Orientation");film->grab();
    click(film,QPointF(-20,10));
    QTimer manual;
    QObject::connect(&manual,&QTimer::timeout,[&] {
        if(!QApplication::activeModalWidget()) return;
        auto *dialog=qobject_cast<UMatrixDialog *>(QApplication::activeModalWidget());require(dialog,"Expected two-spot input dialog");
        const char *names[]={"PH","PK","PL","SH","SK","SL"};const char *values[]={"1","0","0","0","0","1"};
        for(int i=0;i<6;++i) dialog->findChild<QLineEdit *>(names[i])->setText(values[i]);
        manualInvalidated=true;film->setImage(source,true);
        QMetaObject::invokeMethod(dialog,"calcClicked",Qt::DirectConnection);
    });
    manual.start(10);click(film,QPointF(10,20));manual.stop();
    require(manualInvalidated && matrixError(beforeManual,film->getCrystal()->getU())==0,
            "Two-spot input applied old image coordinates after an input change");
    prepare();search();
    qInfo()<<"PASS: image/origin/scale/rotation/reset invalidate picks and candidates; distance/wavelength invalidate scores; no-op/cancel/tonal edits preserve analysis; stale workers are discarded and can restart";
}

static void checkDialog() {
    IndexingDialog dialog(nullptr);
    lauegram engine; configure(engine); dialog.setIndexingParams(&engine);
    dialog.findChild<QLineEdit *>("maxH")->setText("-1");
    bool reported=false;
    QTimer::singleShot(0,[&] {
        auto *message=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        require(message,"Invalid parameter was not reported");
        reported=true; message->accept();
    });
    dialog.accept();
    require(reported && dialog.result()!=QDialog::Accepted,"Invalid parameters closed the dialog");
    dialog.findChild<QLineEdit *>("maxH")->setText("3");
    dialog.accept();
    require(dialog.result()==QDialog::Accepted,"Valid parameters rejected");
    engine.setIndexingAngularTol(1e-8);
    dialog.setIndexingParams(&engine);
    require(dialog.findChild<QLineEdit *>("angTol")->hasAcceptableInput(),
            "A small saved tolerance becomes invalid when reopening parameters");
    qInfo()<<"PASS: parameter dialog rejects invalid input and accepts corrected values";
}

int main(int argc,char **argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc,argv);
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    QCoreApplication::setOrganizationName("QLaueTests");
    QCoreApplication::setApplicationName("Indexing");
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout,&QTimer::timeout,[]{ require(false,"GUI test timed out"); });
    timeout.start(60000);
    Crystal cubic;
    checkRecovery(cubic,"Cubic");
    Crystal hexagonal;
    hexagonal.setLattice(5.2709,5.2709,11.2615,M_PI/2,M_PI/2,2*M_PI/3);
    checkRecovery(hexagonal,"Hexagonal");
    Crystal triclinic;
    triclinic.setLattice(5.431,6.4,7.2,78*M_PI/180,101*M_PI/180,113*M_PI/180);
    checkRecovery(triclinic,"Triclinic");
    checkBoundaries(triclinic);
    checkWorker(triclinic);
    checkDialog();
    checkUi();
    checkInputChanges();
    return 0;
}
