#include <QtWidgets>
#include <cmath>
#include "mainwindow.h"
#include "version.h"

static void require(bool ok, const QString &message) {
    if(!ok) qFatal("%s", qPrintable(message));
}
static void pump() { QApplication::processEvents(); }
static QAction *action(MainWindow &window, const char *name) {
    auto *result=window.findChild<QAction *>(name);
    require(result && result->isEnabled() && result->isVisible(),QString("Missing enabled action: ")+name);
    return result;
}
static double difference(const Matrix &a,const Matrix &b) {
    double result=0;
    for(int row=0;row<3;++row) for(int column=0;column<3;++column)
        result=std::max(result,std::abs(a.Get(row,column)-b.Get(row,column)));
    return result;
}
static void orientationChecks(MainWindow &window) {
    auto *film=window.findChild<LaueFilm *>();
    auto *crystal=film->getCrystal();
    auto *rotate=window.findChild<RotateWidget *>();
    auto *x=rotate->findChild<QToolButton *>("xright");
    auto *y=rotate->findChild<QToolButton *>("yleft");
    auto *z=rotate->findChild<QToolButton *>("zright");
    require(x && y && z,"Rotation buttons missing");
    QImage image(17,13,QImage::Format_RGB32);image.fill(QColor(70,140,210));
    film->setImage(image,true);film->setScale(7.25);film->setOriginX(2.5);film->setOriginY(-1.75);
    const Matrix initial=crystal->getU();
    const int atoms=crystal->getNAtoms();
    for(bool free : {true,false}) {
        crystal->setFreeRotate(free);
        x->click();x->click();y->click();z->click();
        require(difference(crystal->getU(),initial)>1e-5,"Rotation fixture did not change the orientation");
        action(window,"actionReset_Orientation")->trigger();pump();
        require(difference(crystal->getU(),initial)<1e-12,"Reset did not restore the initial orientation");
        require(crystal->getFreeRotate()==free && crystal->getGonioX()==0 &&
                crystal->getGonioY()==0 && crystal->getGonioZ()==0,"Reset changed rotation mode or retained goniometer angles");
        require(*film->importedImage==image && film->scale()==7.25 && film->originX()==2.5 && film->originY()==-1.75,
                "Reset modified the image or calibration");
        require(crystal->getNAtoms()==atoms && crystal->getLatticeA()==5.4 && crystal->getLatticeB()==5.4 &&
                crystal->getLatticeC()==5.4,"Reset modified the structure");
        const Matrix axes[]={crystal->xAxis(),crystal->yAxis(),crystal->zAxis()};
        for(int row=0;row<3;++row) for(int column=0;column<3;++column) {
            auto *label=window.findChild<QLabel *>(QString("OrientationLine%1_%2").arg(row+1).arg(column+1));
            require(label && label->text()==QString("%1").arg(axes[row].Get(column,0),5,'f',3),
                    "Reset did not refresh an orientation panel component");
        }
        action(window,"actionReset_Orientation")->trigger();
        require(difference(crystal->getU(),initial)<1e-12,"Repeated Reset changed the restored orientation");
    }
    // A reset uses the active reciprocal lattice, rather than replacing its cell with the default cubic cell.
    crystal->setLattice(4.2,5.7,7.9,78*M_PI/180,101*M_PI/180,113*M_PI/180);
    x->click();action(window,"actionReset_Orientation")->trigger();pump();
    const Matrix primary=crystal->getUB()*Matrix(1,0,0), secondary=crystal->getUB()*Matrix(0,0,1);
    Matrix identity(3,3);identity.identity();
    require(primary.Get(0,0)>0 && std::abs(primary.Get(1,0))<1e-12 && std::abs(primary.Get(2,0))<1e-12 &&
            std::abs(secondary.Get(1,0))<1e-12 && secondary.Get(2,0)>0,
            "Reset did not restore the primary beam / secondary camera-plane alignment for the current lattice");
    Matrix u=crystal->getU();
    require(difference(u*u.transpose(),identity)<1e-12 && std::abs(u.det()-1)<1e-12,
            "Reset did not retain a proper rotation");
    require(crystal->getLatticeA()==4.2 && crystal->getLatticeB()==5.7 && crystal->getLatticeC()==7.9,
            "Reset replaced a nonorthogonal unit cell");
    qInfo("PASS: Reset Orientation restores the current-lattice alignment, zeroes angles, preserves data and refreshes all nine fields");
}
static void dockChecks(MainWindow &window) {
    const QPair<const char *,const char *> tools[] = {
        {"actionText_Viewer","TextViewDock"},
        {"actionCrystal_Orientation_View","CrystalDock"},
        {"actionIndexing_Viewer","Indexing"},
        {"actionRotations_Toolbox","RotateWidgetDock"},
        {"actionImage_Controls","ImageControlDock"}
    };
    for(const auto &tool:tools) {
        auto *a=action(window,tool.first);
        auto *dock=window.findChild<QDockWidget *>(tool.second);
        require(dock,"Missing dock");
        if(!a->isChecked()) a->trigger();pump();
        require(dock->isVisible() && a->isChecked(),"Menu did not open the dock");
        dock->close();pump();
        require(!dock->isVisible() && !a->isChecked(),"Dock close left its menu checked");
        a->trigger();pump();
        require(dock->isVisible() && a->isChecked(),"One menu activation did not reopen the dock");
        window.hide();pump();
        require(a->isChecked(),"Hiding the parent window changed logical dock visibility");
        window.show();pump();
        require(dock->isVisible() && a->isChecked(),"Showing the parent lost an open dock");
        dock->setFloating(true);pump();dock->close();pump();
        require(!a->isChecked(),"Floating dock close left its menu checked");
        a->trigger();pump();require(dock->isVisible(),"Floating dock failed to reopen in one menu activation");
        dock->setFloating(false);pump();
    }
    auto *rot=window.findChild<QDockWidget *>("RotateWidgetDock");
    auto *image=window.findChild<QDockWidget *>("ImageControlDock");
    window.tabifyDockWidget(rot,image);image->raise();pump();
    require(action(window,"actionRotations_Toolbox")->isChecked() && action(window,"actionImage_Controls")->isChecked(),
            "Tab selection incorrectly unchecked an open dock");
    rot->raise();pump();
    require(action(window,"actionRotations_Toolbox")->isChecked() && action(window,"actionImage_Controls")->isChecked(),
            "Switching dock tabs changed logical visibility");
    rot->close();pump();
    QSettings settings;
    settings.setValue("App/version",double(APP_CURRENT_VERSION));
    settings.setValue("MainWindow/docks",window.saveState(1));
    MainWindow restored;restored.show();pump();
    for(const auto &tool:tools) {
        auto *dock=restored.findChild<QDockWidget *>(tool.second);
        require(action(restored,tool.first)->isChecked()==dock->isVisibleTo(&restored),"Restored dock menu mismatch");
    }
    require(!action(restored,"actionRotations_Toolbox")->isChecked(),"Closed dock reopened on restore");
    action(restored,"actionRotations_Toolbox")->trigger();pump();
    require(restored.findChild<QDockWidget *>("RotateWidgetDock")->isVisible(),"Restored closed dock needed more than one click");
    qInfo("PASS: all five dock actions synchronize close/reopen, floating windows, parent visibility, tabs and restored layout");
}
int main(int argc,char **argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    QCoreApplication::setOrganizationName("QLaueTests");QCoreApplication::setApplicationName("AppControls");
    MainWindow window;window.setMaximumSize(2000,1400);window.resize(1600,1100);window.show();pump();
    orientationChecks(window);dockChecks(window);
}
