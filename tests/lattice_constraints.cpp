#include <QApplication>
#include <QFile>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTimer>
#include <array>
#include <cmath>
#include <cstdlib>
#include "crystaldialog.h"
#include "datafile.h"

using Cell = std::array<double,6>;
static const char *fieldNames[] = {"LatticeA", "LatticeB", "LatticeC",
                                  "LatticeAlpha", "LatticeBeta", "LatticeGamma"};
static void require(bool ok, const QString &message) {
    if(!ok) { qCritical().noquote() << "FAIL:" << message; std::exit(1); }
}
static void requireNear(double actual, double expected, const QString &message) {
    require(std::isfinite(actual) && std::abs(actual-expected) < 1e-10,
            message + QString(" (actual %1, expected %2)").arg(actual,0,'g',15).arg(expected,0,'g',15));
}
static QLineEdit *field(CrystalDialog &dialog, int index) {
    auto *edit=dialog.findChild<QLineEdit *>(fieldNames[index]);
    require(edit,"Missing lattice field");
    return edit;
}
static Cell cellOf(Crystal &crystal) {
    return {{crystal.getLatticeA(),crystal.getLatticeB(),crystal.getLatticeC(),
        crystal.getLatticeAlpha()*180/M_PI,crystal.getLatticeBeta()*180/M_PI,
        crystal.getLatticeGamma()*180/M_PI}};
}
static Crystal crystalWith(const Cell &cell) {
    Crystal crystal;
    crystal.setSpaceGroupNum(1);
    crystal.setLattice(cell[0],cell[1],cell[2],cell[3]*M_PI/180,cell[4]*M_PI/180,cell[5]*M_PI/180);
    crystal.setName("Lattice test");
    crystal.delAllAtoms();
    require(crystal.addAtom(14,.1,.2,.3),"Cannot add test atom");
    require(crystal.spaceGroupGenerate(),"Cannot expand test structure");
    require(crystal.setU(1,1,0,0,0,1),"Cannot set test orientation");
    return crystal;
}
static void checkFields(CrystalDialog &dialog, const Cell &expected) {
    for(int i=0; i<6; ++i) requireNear(field(dialog,i)->text().toDouble(),expected[i],fieldNames[i]);
}
static void checkCrystal(Crystal &actual, Crystal &expected) {
    const Cell a=cellOf(actual), b=cellOf(expected);
    for(int i=0; i<6; ++i) requireNear(a[i],b[i],QString("Crystal changed: %1").arg(fieldNames[i]));
    require(actual.getSpaceGroup()->index==expected.getSpaceGroup()->index,"Space-group setting changed");
    require(QString::fromUtf8(actual.getName())==QString::fromUtf8(expected.getName()),"Name changed");
    require(actual.getNRootAtoms()==expected.getNRootAtoms(),"Root sites changed");
    for(int i=0; i<expected.getNRootAtoms(); ++i) {
        require(actual.getAtom(i)->getZ()==expected.getAtom(i)->getZ(),"Element changed");
        requireNear(actual.getAtom(i)->getXPos(),expected.getAtom(i)->getXPos(),"Atom x changed");
        requireNear(actual.getAtom(i)->getYPos(),expected.getAtom(i)->getYPos(),"Atom y changed");
        requireNear(actual.getAtom(i)->getZPos(),expected.getAtom(i)->getZPos(),"Atom z changed");
    }
    const Matrix aU=actual.getU(), bU=expected.getU();
    for(int i=0; i<3; ++i) for(int j=0; j<3; ++j)
        requireNear(aU.Get(i,j),bU.Get(i,j),"Orientation changed");
}
static void clickSystem(CrystalDialog &dialog, const char *name) {
    auto *button=dialog.findChild<QRadioButton *>(name);
    require(button,"Missing crystal-system button");
    button->click();
}
static void chooseGroup(CrystalDialog &dialog, int number, const QString &setting=QString()) {
    auto *box=dialog.findChild<QComboBox *>("SpacegroupBox");
    const QString suffix=setting.isEmpty() ? QString(" (%1)").arg(number)
                                          : QString(" : %1 (%2)").arg(setting).arg(number);
    for(int i=0; i<box->count(); ++i) {
        if(box->itemText(i).endsWith(suffix) &&
           (!setting.isEmpty() || !box->itemText(i).contains(" : "))) {
            box->setCurrentIndex(i);
            return;
        }
    }
    require(false,"Missing space-group setting "+suffix);
}
static void acceptDialog(CrystalDialog &dialog, bool expected=true) {
    bool warned=false;
    QTimer driver;
    QObject::connect(&driver,&QTimer::timeout,[&] {
        if(auto *message=qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            warned=true;
            require(message->text().contains("space-group setting"),"Unexpected validation message: "+message->text());
            message->accept();
        }
    });
    driver.start(1);
    dialog.setResult(QDialog::Rejected);
    dialog.findChild<QPushButton *>("okButton")->click();
    require(warned==!expected,"Unexpected lattice validation result");
    require((dialog.result()==QDialog::Accepted)==expected,"Incorrect dialog result");
}
static void checkApplied(CrystalDialog &dialog, const Cell &expected, int number, const QString &setting=QString()) {
    checkFields(dialog,expected);
    acceptDialog(dialog);
    Crystal output;
    dialog.getCrystal(&output);
    const Cell actual=cellOf(output);
    for(int i=0; i<6; ++i) requireNear(actual[i],expected[i],"Applied lattice differs from editor");
    require(output.getSpaceGroup()->number==number && QString(output.getSpaceGroup()->Extn)==setting,
            "Applied the wrong space-group setting");
}

static void checkSystemSwitches() {
    Crystal source=crystalWith({{4,5,6,80,95,105}});
    CrystalDialog dialog;
    dialog.setCrystal(&source);
    clickSystem(dialog,"CubicButton");
    checkApplied(dialog,{{4,4,4,90,90,90}},195);
    require(!field(dialog,1)->isEnabled() && !field(dialog,2)->isEnabled(),"Cubic dependent axes are editable");
    field(dialog,0)->setText("7.25");
    checkApplied(dialog,{{7.25,7.25,7.25,90,90,90}},195);

    dialog.setCrystal(&source);
    clickSystem(dialog,"TetragonalButton");
    checkApplied(dialog,{{4,4,6,90,90,90}},75);
    field(dialog,0)->setText("7"); field(dialog,2)->setText("9");
    checkApplied(dialog,{{7,7,9,90,90,90}},75);
    dialog.setCrystal(&source);
    clickSystem(dialog,"HexagonalButton");
    checkApplied(dialog,{{4,4,6,90,90,120}},168);
    field(dialog,0)->setText("7");
    checkApplied(dialog,{{7,7,6,90,90,120}},168);
    dialog.setCrystal(&source);
    clickSystem(dialog,"OrthorhombicButton");
    checkApplied(dialog,{{4,5,6,90,90,90}},16);
    require(field(dialog,1)->isEnabled() && field(dialog,2)->isEnabled(),"Orthorhombic axes are locked");
    clickSystem(dialog,"TriclinicButton");
    const Cell general={{7,8,9,78,101,113}};
    for(int i=0; i<6; ++i) {
        require(field(dialog,i)->isEnabled(),"Triclinic field is locked");
        field(dialog,i)->setText(QString::number(general[i]));
    }
    checkApplied(dialog,general,1);

    dialog.setCrystal(&source);
    clickSystem(dialog,"TrigonalButton");
    checkApplied(dialog,{{4,4,6,90,90,120}},143);
    require(field(dialog,2)->isEnabled(),"P3 must allow c to change independently");
    field(dialog,0)->setText("7"); field(dialog,2)->setText("8");
    checkApplied(dialog,{{7,7,8,90,90,120}},143);
    chooseGroup(dialog,146,"H");
    checkApplied(dialog,{{7,7,8,90,90,120}},146,"H");
    chooseGroup(dialog,146,"R");
    field(dialog,0)->setText("6.25"); field(dialog,3)->setText("76");
    checkApplied(dialog,{{6.25,6.25,6.25,76,76,76}},146,"R");
    require(field(dialog,3)->isEnabled() && !field(dialog,4)->isEnabled() && !field(dialog,5)->isEnabled(),
            "Rhombohedral angles must follow alpha");
    clickSystem(dialog,"AllSpaceGroupsButton");
    checkApplied(dialog,{{6.25,6.25,6.25,76,76,76}},146,"R");
    clickSystem(dialog,"TrigonalButton");
    checkApplied(dialog,{{6.25,6.25,6.25,76,76,76}},146,"R");
    chooseGroup(dialog,146,"H");
    field(dialog,2)->setText("9");
    checkApplied(dialog,{{6.25,6.25,9,90,90,120}},146,"H");

    dialog.setCrystal(&source);
    clickSystem(dialog,"MonoclinicButton");
    checkApplied(dialog,{{4,5,6,90,95,90}},3,"b");
    const QString settings[]={"a","b","c","-a1","-b1","-c1"};
    for(int i=0; i<6; ++i) {
        const int axis=i%3, number=i<3 ? 3 : 9;
        chooseGroup(dialog,number,settings[i]);
        for(int angle=0; angle<3; ++angle)
            require(field(dialog,angle+3)->isEnabled()==(angle==axis),"Wrong monoclinic free angle");
        Cell expected={{4,5,6,90,90,90}};
        expected[axis+3]=102+i;
        field(dialog,axis+3)->setText(QString::number(expected[axis+3]));
        checkApplied(dialog,expected,number,settings[i]);
    }
    qInfo() << "PASS: all crystal systems synchronize on selection/edit; P3, R:H/R:R and signed unique axes";
}

static void checkGuardAndCancel() {
    Crystal source=crystalWith({{4,5,6,90,90,90}}), before=source;
    CrystalDialog dialog;
    dialog.setCrystal(&source);
    clickSystem(dialog,"AllSpaceGroupsButton");
    chooseGroup(dialog,195);
    checkFields(dialog,{{4,5,6,90,90,90}});
    for(int i=0; i<6; ++i) require(field(dialog,i)->isEnabled(),"All Space Groups must remain a free editor");
    acceptDialog(dialog,false);
    dialog.getCrystal(&source);
    checkCrystal(source,before);
    checkFields(dialog,{{4,5,6,90,90,90}});
    field(dialog,1)->setText("4"); field(dialog,2)->setText("4");
    checkApplied(dialog,{{4,4,4,90,90,90}},195);

    // Preserve an old inconsistent cell while editing, but never publish it.
    Crystal invalid=before;
    invalid.setSpaceGroupNum(195);
    dialog.setCrystal(&invalid);
    checkFields(dialog,{{4,5,6,90,90,90}});
    acceptDialog(dialog,false);
    dialog.getCrystal(&source);
    checkCrystal(source,before);
    dialog.setCrystal(&source);
    clickSystem(dialog,"CubicButton"); field(dialog,0)->setText("12");
    dialog.findChild<QPushButton *>("cancelButton")->click();
    if(dialog.result()==QDialog::Accepted) dialog.getCrystal(&source);
    checkCrystal(source,before);
    dialog.setCrystal(&source);
    acceptDialog(dialog);
    dialog.getCrystal(&source);
    checkCrystal(source,before);
    qInfo() << "PASS: incompatible metric rejected atomically; unrestricted editing, cancel and no-op preserve source";
}

static void checkImportedCells(const QTemporaryDir &directory) {
    struct Example { const char *name; const char *group; Cell cell; int number; const char *setting; };
    const Example examples[]={
        {"rounded cubic","P 2 3",{{5,5.001,4.9995,90.005,89.994,90.003}},195,""},
        {"rounded P3","P 3",{{5,5.0004,7,90.005,90.002,120.003}},143,""},
        {"rounded R:H","R 3:H",{{5,5.0004,7,90.004,89.997,120.001}},146,"H"},
        {"rounded R:R","R 3:R",{{5,5.0004,4.9998,75,75.005,74.997}},146,"R"},
        {"unique a","P 2 1 1",{{4,5,6,103,90,90}},3,"a"},
        {"unique b","P 1 2 1",{{4,5,6,90,104,90}},3,"b"},
        {"unique c","P 1 1 2",{{4,5,6,90,90,105}},3,"c"},
        {"general P1","P 1",{{4,5,6,78,101,113}},1,""}
    };
    const char *tags[]={"_cell_length_a","_cell_length_b","_cell_length_c",
                        "_cell_angle_alpha","_cell_angle_beta","_cell_angle_gamma"};
    CrystalDialog dialog;
    for(const Example &example : examples) {
        QString text="data_lattice\n";
        for(int i=0; i<6; ++i) text+=QString("%1 %2\n").arg(tags[i]).arg(example.cell[i],0,'g',15);
        text+=QString("_space_group_name_H-M_alt '%1'\n").arg(example.group);
        text+="loop_\n_atom_site_label\n_atom_site_fract_x\n_atom_site_fract_y\n_atom_site_fract_z\nSi1 .1 .2 .3\n";
        const QString path=directory.filePath("cell.cif");
        QFile file(path);
        require(file.open(QIODevice::WriteOnly),"Cannot create test CIF");
        require(file.write(text.toUtf8())==text.toUtf8().size(),"Cannot write test CIF");
        file.close();
        DataFile data;
        require(data.readCif(path),QString(example.name)+": "+data.getErrorStr());
        Crystal parsed=data.getCrystal(), output;
        require(parsed.getSpaceGroup()->number==example.number && QString(parsed.getSpaceGroup()->Extn)==example.setting,
                "CIF selected the wrong setting");
        dialog.setCrystal(&parsed);
        checkFields(dialog,example.cell);
        acceptDialog(dialog);
        dialog.getCrystal(&output);
        checkCrystal(output,parsed);
        dialog.setCrystal(&output);
        acceptDialog(dialog);
        dialog.getCrystal(&output);
        checkCrystal(output,parsed);
    }
    qInfo() << "PASS: exact and rounded CIF cells, both trigonal settings and all monoclinic axes survive repeated no-op apply";
}

int main(int argc,char **argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc,argv);
    QTemporaryDir directory;
    require(directory.isValid(),"Cannot create temporary test directory");
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout,&QTimer::timeout,[]{ require(false,"Lattice constraint test timed out"); });
    timeout.start(30000);
    checkSystemSwitches();
    checkGuardAndCancel();
    checkImportedCells(directory);
    return 0;
}
