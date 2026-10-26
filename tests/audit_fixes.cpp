#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <cmath>
#include "datafile.h"
#include "htmlgenerators.h"
#include "reorientdialog.h"

static void require(bool ok, const QString &message) {
    if(!ok) qFatal("%s",qPrintable(message));
}
static void writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly),"Cannot create fixture");
    require(file.write(bytes)==bytes.size(),"Cannot write fixture");
}
static QByteArray readFile(const QString &path) {
    QFile file(path); require(file.open(QIODevice::ReadOnly),"Cannot read fixture");
    return file.readAll();
}
static void fileChecks(const QTemporaryDir &directory) {
    Crystal crystal;
    crystal.addAtom(29,-.125,1.25,-.375);
    crystal.rotateBy(.12,-.23,.34);
    LaueFilm film(nullptr,&crystal);
    QImage source(24,16,QImage::Format_RGB32); source.fill(Qt::red);
    film.setImage(source,true); film.setScale(7.5); film.setOriginX(1.5); film.setOriginY(-2.5);
    film.adjustImage(-15,75,230,20); film.setInvertImage(true); film.setRotateImage(true); film.setDisplayImage(false);
    film.getLaue()->setCalcPrecision(3); film.getLaue()->setIntensityCorrection(0);
    film.getLaue()->setTubeZ(42); film.getLaue()->setIncidentPoln(XRay::PolnUnpol);
    film.setDisplayIntensities(false);
    const QImage adjusted=*film.importedAdjustedImage;
    DataFile data; data.setCrystal(&crystal); data.setLaue(&film);
    require(!data.documentState().contains("Created on"),"Timestamp must not affect document state");
    DataFile duplicate; duplicate.setCrystal(&crystal); duplicate.setLaue(&film);
    require(data.documentState()==duplicate.documentState(),"Equal documents must compare equal");
    const QString path=directory.filePath("analysis.xtl");
    require(data.write(path),data.getErrorStr());
    const QByteArray saved=readFile(path);
    DataFile parsed;
    require(parsed.read(path),parsed.getErrorStr());
    Crystal loaded=parsed.getCrystal();
    require(loaded.getNAtoms()==1 && loaded.getAtom(0)->getXPos()==-.125 &&
            loaded.getAtom(0)->getYPos()==1.25,"XTL must retain source coordinates");
    require(parsed.getLaueImage()==source && parsed.imageScale()==7.5 &&
            parsed.originX()==1.5 && parsed.originY()==-2.5,"Image and calibration round trip");
    LaueFilm restored(nullptr,&loaded);
    QImage restoredSource=parsed.getLaueImage();
    restored.setImage(restoredSource,true); parsed.applyImageSettings(&restored); parsed.getLaue(&restored);
    require(*restored.importedAdjustedImage==adjusted && restored.isImageInverted() && restored.isImageRotated() &&
            !restored.isImageDisplayed() && restored.imageBrightness()==-15,"Image adjustment and flags round trip");
    require(restored.getLaue()->calcPrecision()==3 && restored.getLaue()->intensityCorrection()==0 &&
            restored.getLaue()->tubeZ()==42 && restored.getLaue()->incidentPoln()==XRay::PolnUnpol &&
            !restored.getDisplayIntensities(),"Diffraction settings round trip");
    Matrix before=crystal.getU(), after=loaded.getU();
    for(int r=0;r<3;++r) for(int c=0;c<3;++c)
        require(std::abs(before.Get(r,c)-after.Get(r,c))<1e-12,"XTL orientation round trip");
    require(!data.write(directory.filePath("missing/file.xtl")) && !data.getErrorStr().isEmpty(),
            "Failed saves must report errors");
    require(readFile(path)==saved,"Failed saves must preserve existing files");
    // Distance/wavelength setters now preserve valid state. An invalid internal
    // precision still exercises the writer's independent validation boundary.
    film.getLaue()->setCalcPrecision(0);
    DataFile invalidSave; invalidSave.setCrystal(&crystal); invalidSave.setLaue(&film);
    require(!invalidSave.write(path) && !invalidSave.getErrorStr().isEmpty(),"Invalid analysis must not overwrite a valid file");
    require(readFile(path)==saved,"Invalid analysis save must preserve original bytes");
    film.getLaue()->setCalcPrecision(3);
    const QByteArray lattice="<lattice a='5.4' b='5.4' c='5.4' alpha='90' beta='90' gamma='90'>";
    const QByteArray atom="<atom Z='29' a='-.125' b='1.25' c='-.375'/>";
    const QByteArray good="<QLaue><crystal>"+lattice+atom+"</lattice></crystal></QLaue>";
    QList<QByteArray> invalid={"<unrelated/>","<QLaue/>","<QLaue><crystal/></QLaue>",
        QByteArray(good).replace("a='5.4'","a='nan'"),
        QByteArray(good).replace("gamma='90'","gamma='180'"),
        QByteArray(good).replace("Z='29'","Z='0'"),
        QByteArray(good).replace("Z='29'","Z='99'"),
        QByteArray(good).replace("a='-.125'","a='inf'"),
        QByteArray(good).replace("</crystal>","<spacegroup><hall_name name='unknown'/></spacegroup></crystal>"),
        QByteArray(good).replace("</crystal>","<orientation><x-axis x='1' y='0' z='0'/><z-axis x='-1' y='0' z='0'/></orientation></crystal>"),
        QByteArray(good).replace("</QLaue>","<laue><parameters><lambda-range min='3' max='.3'/></parameters></laue></QLaue>"),
        QByteArray(good).replace("</QLaue>","<laue><parameters><distance d='0'/></parameters></laue></QLaue>"),
        QByteArray(good).replace("</QLaue>","<laue><parameters><detector threshold='1.5' saturation='.1'/></parameters></laue></QLaue>"),
        QByteArray(good).replace("</QLaue>","<laue><laue-image><imagedata ox='0' oy='0' scale='1'>broken</imagedata></laue-image></laue></QLaue>"),
        "<QLaue><crystal>"+lattice+atom.repeated(Crystal::MaxAtoms+1)+"</lattice></crystal></QLaue>"};
    const QString badPath=directory.filePath("invalid.xtl");
    for(const QByteArray &bytes : invalid) {
        writeFile(badPath,bytes);
        require(!parsed.read(badPath) && !parsed.getErrorStr().isEmpty(),"Invalid XTL must be rejected");
        require(parsed.getLaueImage()==source && parsed.getCrystal().getNAtoms()==1,
                "Failed XTL load must retain previous data");
    }
    require(!parsed.read(directory.filePath("not-found.xtl")),"Missing XTL rejected");
    writeFile(path,good);
    require(parsed.read(path),parsed.getErrorStr());
    parsed.getLaue(&film);
    require(parsed.getLaueImage().isNull() && film.getLaue()->getDistance()==35 &&
            film.getLaue()->getLambdaRangeMin()==.3 && film.getLaue()->calcPrecision()==5 &&
            film.getLaue()->intensityCorrection()==1023,"Legacy optional XTL fields get defined defaults");
    for(double saturation : {0.0,0.1,1.0}) {
        film.getLaue()->setDet(.5,saturation);
        DataFile levels; levels.setCrystal(&crystal); levels.setLaue(&film);
        require(levels.write(path) && parsed.read(path),"All UI detector levels must round trip independently");
        parsed.getLaue(&film);
        require(film.getLaue()->getDetLevel()==.5 && film.getLaue()->getDetSatLevel()==saturation,
                "Detector threshold and saturation are independent controls");
    }
    const QByteArray cel="CELL 5.4 5.4 5.4 90 90 90\nnatom 1\nCu 29 -.125 1.25 -.375\nrgnr 1\n";
    const QString celPath=directory.filePath("input.cel");
    writeFile(celPath,cel); require(parsed.readCel(celPath),parsed.getErrorStr());
    const QList<QByteArray> invalidCel={"CELL 5.4\n", "natom 1\n", "CELL 5 5 5 90 90 90\nnatom 1\nCu 29 0\n",
        QByteArray(cel).replace("natom 1","natom -1"), QByteArray(cel).replace("natom 1","natom 2049"),
        QByteArray(cel).replace("Cu 29","Cu 0"),QByteArray(cel).replace("-.125","nan"),
        QByteArray(cel).replace("rgnr 1","rgnr 999"),QByteArray(cel).replace("90 90 90","90 90 180")};
    for(const QByteArray &bytes : invalidCel) {
        writeFile(celPath,bytes);
        require(!parsed.readCel(celPath) && !parsed.getErrorStr().isEmpty() && parsed.getErrorLine()>0,
                "Invalid CEL must report a line and error");
        Crystal retained=parsed.getCrystal();
        require(retained.getNAtoms()==1 && retained.getAtom(0)->getXPos()==-.125,"CEL failure must retain current crystal");
    }
    require(!parsed.readCel(directory.filePath("not-found.cel")),"Missing CEL rejected");
    qInfo("PASS: XTL/CEL validation, atomic save errors, source coordinates and state preservation");
}
static void waitForCalculation(LaueThread &worker) {
    QEventLoop loop;
    bool completed=false;
    const auto connection=QObject::connect(&worker,&LaueThread::calculated,&loop,[&] {
        require(QThread::currentThread()==qApp->thread(),"Results must be published on the GUI thread");
        completed=true; loop.quit();
    });
    QTimer::singleShot(15000,&loop,&QEventLoop::quit);
    loop.exec();
    QObject::disconnect(connection);
    require(completed && !worker.isCalculating(),"Calculation must complete and join");
}
static void sameSpots(lauegram &actual, lauegram &expected) {
    require(actual.getNspots()==expected.getNspots(),"Snapshot spot count differs from reference");
    for(int i=0;i<expected.getNspots();++i) {
        auto a=actual.getSpot(i), e=expected.getSpot(i);
        require(a->H()==e->H() && a->K()==e->K() && a->L()==e->L() &&
                std::abs(a->X()-e->X())<1e-10 && std::abs(a->Y()-e->Y())<1e-10,
                "Worker result differs from immutable synchronous reference");
    }
}
static void workerChecks() {
    Crystal source; source.addAtom(29,0,0,0); source.setU(1,-2,0,0,0,1);
    LaueThread worker; worker.setLambdaRange(.3,3); worker.setIntensityCorrection(0);
    worker.setCalcPrecision(5);
    Crystal snapshot=source;
    lauegram reference=worker;
    worker.startCalculation(&source);
    source.rotateBy(.4,.3,-.5); worker.setDistance(71);
    require(reference.calc(&snapshot),"Reference calculation");
    waitForCalculation(worker);
    sameSpots(worker,reference);
    require(worker.getDistance()==71,"Publication must not restore old settings");
    lauegram old=worker;
    for(int i=0;i<30;++i) {
        source.rotateBy(.013,-.021,.034);
        worker.setDistance(35+i);
        worker.startCalculation(&source);
        sameSpots(worker,old);
    }
    snapshot=source; reference=worker;
    require(reference.calc(&snapshot),"Latest reference calculation");
    waitForCalculation(worker);
    sameSpots(worker,reference);
    bool failed=false;
    const auto error=QObject::connect(&worker,&LaueThread::calculationFailed,&worker,[&](const QString &message) {
        failed=!message.isEmpty();
    });
    worker.setCalcPrecision(0);
    worker.startCalculation(&source);
    waitForCalculation(worker);
    require(failed && worker.getNspots()==0,"Failed calculation must report failure, clear stale spots and finish");
    QObject::disconnect(error);
    QElapsedTimer elapsed; elapsed.start();
    {
        Crystal large=source; large.setLattice(15,15,15,M_PI/2,M_PI/2,M_PI/2);
        LaueThread closing; closing.setLambdaRange(.3,3); closing.setCalcPrecision(1);
        closing.startCalculation(&large);
        closing.startCalculation(&source);
    }
    require(elapsed.elapsed()<5000,"Closing a calculation must interrupt and join promptly");
    QApplication::processEvents();
    qInfo("PASS: immutable worker inputs, GUI-only publication, latest request and active shutdown");
}
static void imageAndReorientationChecks() {
    Crystal crystal; LaueFilm film(nullptr,&crystal);
    QImage image(24,16,QImage::Format_RGB32); image.fill(Qt::red);
    film.setImage(image,true); film.setRotateImage(true);
    require(film.importedAdjustedImage->size()==QSize(16,24) &&
            film.imageZoomRect==film.importedAdjustedImage->rect(),"Rotation must cover the entire adjusted image");
    film.setRotateImage(false);
    require(film.imageZoomRect==image.rect(),"Rotation off must restore full image dimensions");
    film.setRotateImage(true); image=QImage(33,17,QImage::Format_RGB32); image.fill(Qt::blue);
    film.setImage(image,true);
    require(film.imageZoomRect==film.importedAdjustedImage->rect(),"Image replacement while rotated must use adjusted dimensions");
    lauegram gram;
    for(int mode : {Reorientation::XAxis,Reorientation::YAxis,Reorientation::ZAxis,Reorientation::Zone}) {
        ReorientDialog dialog; dialog.setCrystal(&crystal); dialog.setLaue(&gram);
        auto set=[&](const char *name,const char *value) { dialog.findChild<QLineEdit *>(name)->setText(value); };
        set("XHLineEdit","1");
        set("XKLineEdit",mode==Reorientation::Zone?"0":"2");
        set("XLLineEdit",mode==Reorientation::Zone?"0":"3");
        set("ZHLineEdit","0");set("ZKLineEdit","0");set("ZLLineEdit","1");
        const char *button=mode==Reorientation::Zone?"ZoneButton":mode==Reorientation::YAxis?"YAxisRadioButton":
                           mode==Reorientation::ZAxis?"ZAxisRadioButton":"XAxisRadioButton";
        dialog.findChild<QRadioButton *>(button)->setChecked(true);
        require(QMetaObject::invokeMethod(&dialog,"go",Qt::DirectConnection),"Calculate reorientation");
        auto table=dialog.findChild<QTableWidget *>("DataTable");
        require(!dialog.findChild<QPushButton *>("okButton")->isEnabled(),"No automatic selection");
        QMetaObject::invokeMethod(&dialog,"okPressed",Qt::DirectConnection);
        require(!dialog.getSelectedOrientation(),"Unselected candidates must not apply");
        const QString html=HtmlGenerators::crystalOrientationToHtml(&gram.reorientation);
        require(html.count("&#176;")==3*gram.reorientation.getValidCount(),"HTML must show all and only valid candidates");
        require(html.contains(mode==Reorientation::YAxis?"Y-Axis":mode==Reorientation::ZAxis?"Z-Axis":"X-Axis"),
                "HTML must identify the requested target axis");
        for(int row=0;row<8;++row) {
            if(!gram.reorientation.isValid(row)) {
                require(!(table->item(row,0)->flags()&Qt::ItemIsSelectable),"Invalid candidates must not be selectable");
                continue;
            }
            table->setCurrentCell(row,0); table->selectRow(row);
            QMetaObject::invokeMethod(&dialog,"okPressed",Qt::DirectConnection);
            require(dialog.getSelectedOrientation(),"Every valid row, including rows 5-8, must apply");
            double x,y,z; dialog.getAngles(&x,&y,&z);
            const double *expected=gram.reorientation.getAngles(row/2,row%2);
            for(int axis=0;axis<3;++axis)
                require(html.contains(QString("<td>%1&#176;</td>").arg(expected[axis]*180/M_PI,0,'f',3)),
                        "Printed angle must match the calculated candidate");
            require(x==expected[0] && y==expected[1] && z==expected[2],"Apply full precision, not table rounding");
        }
        set("XHLineEdit","2");
        require(!dialog.findChild<QPushButton *>("okButton")->isEnabled() &&
                !dialog.findChild<QPushButton *>("printButton")->isEnabled(),"Changed directions invalidate displayed results");
    }
    qInfo("PASS: image crop, all valid reorientation rows, full-precision apply and axis-aware HTML");
}
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    QTemporaryDir directory; require(directory.isValid(),"Temporary directory");
    fileChecks(directory); workerChecks(); imageAndReorientationChecks();
    return 0;
}
