#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <cmath>
#include <cstdlib>
#include <functional>
#include "datafile.h"
#include "crystaldialog.h"
#include "mainwindow.h"
#include "rotatewidget.h"

static void require(bool ok, const QString &message) {
    if(!ok) {
        qCritical().noquote() << "FAIL:" << message;
        std::exit(1);
    }
}

static void near(double actual, double expected, const QString &message) {
    require(std::isfinite(actual) && std::abs(actual-expected) < 1e-10,
            message + QString(" (actual %1, expected %2)").arg(actual,0,'g',15).arg(expected,0,'g',15));
}

static QString writeCif(const QTemporaryDir &directory, const QString &text) {
    const QString path = directory.filePath(QString::fromUtf8("結晶テスト.CIF"));
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "Cannot write test CIF");
    require(file.write(text.toUtf8()) == text.toUtf8().size(), "Cannot finish test CIF");
    return path;
}

static QString cell(const QString &group = "_space_group_name_H-M_alt 'P 1'\n") {
    return "data_test\n_cell_length_a 5.431(2)\n_cell_length_b 6.4\n_cell_length_c 7.2\n"
           "_cell_angle_alpha 78\n_cell_angle_beta 101\n_cell_angle_gamma 113\n" + group;
}

static QString atoms() {
    // Column order, wrapped rows, quoted labels, signed fractional coordinates and H.
    return "loop_\n_atom_site_fract_z\n_atom_site_label\n_atom_site_fract_x\n"
           "_atom_site_type_symbol\n_atom_site_occupancy\n_atom_site_fract_y\n"
           "0.3 'H1' -0.125 H 1\n1.25 # split atom row\n0.6 O1 0.4 O 1 0.5\n";
}

static void checkCell(Crystal &crystal) {
    near(crystal.getLatticeA(), 5.431, "CIF uncertainty must not change central value");
    near(crystal.getLatticeB(), 6.4, "b changed");
    near(crystal.getLatticeC(), 7.2, "c changed");
    near(crystal.getLatticeAlpha(), 78*M_PI/180, "alpha changed");
    near(crystal.getLatticeBeta(), 101*M_PI/180, "beta changed");
    near(crystal.getLatticeGamma(), 113*M_PI/180, "gamma changed");
    const double a=5.431, b=6.4, c=7.2;
    Matrix metric(a*a, a*b*cos(113*M_PI/180), a*c*cos(101*M_PI/180),
                  a*b*cos(113*M_PI/180), b*b, b*c*cos(78*M_PI/180),
                  a*c*cos(101*M_PI/180), b*c*cos(78*M_PI/180), c*c);
    Matrix reciprocal = crystal.getB();
    Matrix identity = reciprocal.transpose() * reciprocal * metric;
    for(int i=0; i<3; ++i) for(int j=0; j<3; ++j)
        near(identity.Get(i,j), i==j ? 1 : 0, "Reciprocal metric is inconsistent with imported cell");
    identity = crystal.getB() * crystal.getBinv();
    for(int i=0; i<3; ++i) for(int j=0; j<3; ++j)
        near(identity.Get(i,j), i==j ? 1 : 0, "B inverse is incorrect");
}

static void importViaDialog(CrystalDialog &dialog, const QString &path,
                            bool cancel=false, bool expectError=false) {
    bool selected=false, reported=false;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, [] { require(false, "CIF dialog timed out"); });
    timeout.start(10000);
    QTimer driver;
    QObject::connect(&driver, &QTimer::timeout, [&] {
        QWidget *modal = QApplication::activeModalWidget();
        if(QMessageBox *message = qobject_cast<QMessageBox *>(modal)) {
            require(expectError, "Unexpected CIF import error: " + message->text());
            require(message->text().contains(QFileInfo(path).fileName()), "Error must identify the CIF");
            reported=true;
            message->accept();
        } else if(QFileDialog *picker = qobject_cast<QFileDialog *>(modal)) {
            require(picker->nameFilters().first().contains("*.CIF"), "Missing uppercase CIF filter");
            require(picker->nameFilters().size()==2, "Missing all files filter");
            if(cancel) picker->reject();
            else if(!selected) {
                picker->setDirectory(QFileInfo(path).absolutePath());
                picker->selectFile(path);
                picker->findChild<QLineEdit *>("fileNameEdit")->setText(path);
                selected=true;
            } else QMetaObject::invokeMethod(picker, "accept", Qt::DirectConnection);
        }
    });
    driver.start(20);
    QPushButton *button = dialog.findChild<QPushButton *>("importCifButton");
    require(button != nullptr, "Import CIF button missing");
    button->click();
    require(reported==expectError, "Incorrect CIF error behavior");
}

static void checkCif(const QTemporaryDir &directory) {
    const QString source = cell() +
        "_chemical_name_common\n;Imported crystal\nfrom CIF\n;\n"
        "loop_\n_ignored_label\n_ignored_text\n'loop_' '_quoted_tag'\n" + atoms();
    DataFile data;
    require(data.readCif(writeCif(directory, source)), data.getErrorStr());
    Crystal parsed = data.getCrystal();
    checkCell(parsed);
    require(parsed.getNRootAtoms()==2, "Atom loop not loaded");
    require(parsed.getAtom(0)->getZ()==1 && parsed.getAtom(1)->getZ()==8, "Elements changed");
    near(parsed.getAtom(0)->getXPos(), -0.125, "Negative fractional coordinate changed");
    near(parsed.getAtom(0)->getYPos(), 1.25, "Fractional coordinate over one changed");
    require(parsed.getNAtoms()==2, "P1 added periodic copies of the source atoms");
    require(QString::fromUtf8(parsed.getName())=="Imported crystal\nfrom CIF", "Multiline name lost");

    CrystalDialog dialog;
    Crystal original;
    original.setSpaceGroupNum(225);
    original.setName("Original crystal");
    dialog.setCrystal(&original);
    importViaDialog(dialog, writeCif(directory, source));
    near(original.getLatticeA(), 5.4, "Import changed live crystal before OK");
    const QString screenshot = qEnvironmentVariable("QLAUE_TEST_SCREENSHOT");
    if(!screenshot.isEmpty()) {
        dialog.show();
        QApplication::processEvents();
        require(dialog.grab().save(screenshot), "Cannot save dialog preview");
    }
    dialog.accept();
    require(dialog.result()==QDialog::Accepted, "Valid imported dialog cannot be accepted");
    Crystal roundTrip;
    dialog.getCrystal(&roundTrip);
    checkCell(roundTrip);
    require(roundTrip.getNRootAtoms()==2 && roundTrip.getAtom(0)->getZ()==1, "Hydrogen lost in dialog");
    near(roundTrip.getAtom(0)->getXPos(), -0.125, "Dialog changed negative fractional x");
    near(roundTrip.getAtom(0)->getYPos(), 1.25, "Dialog changed fractional y over one");
    importViaDialog(dialog, directory.filePath("anything.CIF"), true);
    dialog.getCrystal(&roundTrip);
    checkCell(roundTrip);

    const QString mono = "data_mono\n_cell_length_a 5\n_cell_length_b 6\n_cell_length_c 7\n"
        "_cell_angle_alpha 90\n_cell_angle_beta 102\n_cell_angle_gamma 90\n"
        "_symmetry_space_group_name_H-M 'P 21/c'\n_symmetry_Int_Tables_number 14\n"
        "loop_\n_symmetry_equiv_pos_as_xyz\n'x,y,z'\n'-x,y+1/2,-z+1/2'\n"
        "'-x,-y,-z'\n'x,-y+1/2,z+1/2'\n"
        "loop_\n_atom_site_label\n_atom_site_fract_x\n_atom_site_fract_y\n_atom_site_fract_z\nC1 0.12 0.23 0.34\n";
    require(data.readCif(writeCif(directory, mono)), data.getErrorStr());
    parsed = data.getCrystal();
    require(parsed.getSpaceGroup()->number==14, "Wrong space group");
    require(QString(parsed.getSpaceGroup()->Extn)=="b1", "Wrong monoclinic setting");
    require(parsed.getNAtoms()==4, "Symmetry did not generate four general positions");
    dialog.setCrystal(&parsed);
    dialog.getCrystal(&roundTrip);
    near(roundTrip.getLatticeBeta(), 102*M_PI/180, "Dialog reset monoclinic beta");
    require(roundTrip.getNRootAtoms()==1, "Repeated import retained old atoms");

    const QString trigonal = "data_trigonal\n_cell_length_a 5\n_cell_length_b 5\n_cell_length_c 7\n"
        "_cell_angle_alpha 90\n_cell_angle_beta 90\n_cell_angle_gamma 120\n_space_group_IT_number 143\n"
        "loop_\n_space_group_symop_operation_xyz\n'x,y,z'\n'-y,x-y,z'\n'y-x,-x,z'\n"
        "loop_\n_atom_site_label\n_atom_site_fract_x\n_atom_site_fract_y\n_atom_site_fract_z\nSi1 0.1 0.3 0.2\n";
    require(data.readCif(writeCif(directory,trigonal)),data.getErrorStr());
    parsed=data.getCrystal();
    require(parsed.getNAtoms()==3,"P3 must generate three atoms");
    bool found1=false,found2=false;
    for(int i=0;i<parsed.getNAtoms();++i) {
        Atom *atom=parsed.getAtom(i);
        found1 |= std::abs(atom->getXPos()-0.7)<1e-10 && std::abs(atom->getYPos()-0.8)<1e-10;
        found2 |= std::abs(atom->getXPos()-0.2)<1e-10 && std::abs(atom->getYPos()-0.9)<1e-10;
    }
    require(found1 && found2,"Trigonal symmetry rotation was transposed");
    dialog.setCrystal(&parsed);
    dialog.getCrystal(&roundTrip);
    near(roundTrip.getLatticeGamma(),120*M_PI/180,"Dialog lost trigonal cell angle");

    const QString rhombohedral = "data_rhombohedral\n_cell_length_a 5\n_cell_length_b 5\n_cell_length_c 5\n"
        "_cell_angle_alpha 75\n_cell_angle_beta 75\n_cell_angle_gamma 75\n_space_group_name_H-M_alt 'R 3'\n";
    require(data.readCif(writeCif(directory,rhombohedral)),data.getErrorStr());
    parsed=data.getCrystal();
    require(QString(parsed.getSpaceGroup()->Extn)=="R","Rhombohedral axes mistaken for hexagonal axes");

    const QStringList groupTags = {
        "_space_group_name_Hall '-P 1'\n",
        "_space_group_IT_number 2\n",
        "_symmetry_space_group_name_H-M 'P -1'\n",
        "loop_\n_space_group_symop_operation_xyz\n'x,y,z'\n'-x,-y,-z'\n"
    };
    for(const QString &tags : groupTags) {
        require(data.readCif(writeCif(directory, cell(tags)+atoms())), data.getErrorStr());
        parsed = data.getCrystal();
        require(parsed.getSpaceGroup()->number==2 && parsed.getNAtoms()==4, "Space group alias/operations failed");
    }
    QString variants=source;
    variants.replace("_cell_length_a 5.431(2)", "_CELL_LENGTH_A\n5.431e0(2)");
    variants.replace("\n", "\r\n");
    require(data.readCif(writeCif(directory, variants)), data.getErrorStr());
    parsed=data.getCrystal();
    checkCell(parsed);

    QString many=cell()+"loop_\n_atom_site_type_symbol\n_atom_site_fract_x\n_atom_site_fract_y\n_atom_site_fract_z\n";
    for(int i=0; i<51; ++i) many += QString("C %1 0 0\n").arg(i/51.0,0,'g',15);
    require(data.readCif(writeCif(directory, many)), data.getErrorStr());
    parsed=data.getCrystal();
    dialog.setCrystal(&parsed);
    dialog.getCrystal(&roundTrip);
    require(roundTrip.getNRootAtoms()==51, "Dialog cannot round-trip more than 50 sites");

    require(data.readCif(writeCif(directory, cell())), data.getErrorStr());
    parsed=data.getCrystal();
    require(parsed.getNRootAtoms()==0, "Cell-only import retained atoms");

    QStringList bad;
    bad << "" << "not a CIF" << "data_bad\n_cell_length_a";
    bad << QString(source).replace("5.431(2)","?");
    bad << QString(source).replace("5.431(2)","nan");
    bad << QString(source).replace("_cell_angle_alpha 78","_cell_angle_alpha 1");
    bad << QString(source).replace("0.6 O1 0.4 O 1 0.5", "0.6 O1 0.4 O 1");
    bad << QString(source).replace("O1 0.4 O 1", "O1 0.4 O 0.5");
    bad << QString(source).replace("O1 0.4 O 1", "O1 0.4 Xx 1");
    bad << source+"data_second\n"+cell().section('\n',1);
    bad << source+"_cell_length_a 9\n";
    bad << cell("_space_group_IT_number 999\n");
    bad << cell("");
    bad << QString(mono).replace("-x,y+1/2,-z+1/2", "-x,y+1/2,-z+1/3");
    bad << "#\\#CIF_2.0\n"+source;
    bad << source+"_extra 'unclosed\n";
    bad << source+"_extra\n;unclosed\n";
    QString excessive=cell("_space_group_IT_number 225\n")+many.section("loop_",1).prepend("loop_");
    excessive.replace("_cell_length_b 6.4","_cell_length_b 5.431");
    excessive.replace("_cell_length_c 7.2","_cell_length_c 5.431");
    excessive.replace("_cell_angle_alpha 78","_cell_angle_alpha 90");
    excessive.replace("_cell_angle_beta 101","_cell_angle_beta 90");
    excessive.replace("_cell_angle_gamma 113","_cell_angle_gamma 90");
    require(!data.readCif(writeCif(directory,excessive)) && data.getErrorStr().contains("Too many atom sites"),
            "Symmetry-expanded atom limit was not enforced");
    bad << excessive;
    require(data.readCif(writeCif(directory, source)), data.getErrorStr());
    for(const QString &invalid : bad) {
        require(!data.readCif(writeCif(directory, invalid)), "Invalid CIF was accepted");
        require(!data.getErrorStr().isEmpty(), "CIF error has no explanation");
        parsed=data.getCrystal();
        checkCell(parsed);
        require(parsed.getNRootAtoms()==2, "Failed CIF import changed previous data");
    }
    require(!data.readCif(directory.filePath("does-not-exist.cif")), "Missing file accepted");
    importViaDialog(dialog, writeCif(directory, "broken"), false, true);
    dialog.getCrystal(&roundTrip);
    require(roundTrip.getNRootAtoms()==51, "Failed import erased editor contents");

    dialog.setCrystal(&original);
    importViaDialog(dialog, writeCif(directory, source));
    dialog.reject();
    near(original.getLatticeA(),5.4,"Dialog cancel changed original crystal");
    require(QString(original.getName())=="Original crystal", "Cancel changed crystal name");
    qInfo() << "PASS: CIF parsing, symmetry, triclinic metric, atom tables, dialog import/cancel/errors";
}

static void checkSourceCoordinates(const QTemporaryDir &directory) {
    // Minimal regression structure from the reported Na2Co2TeO6 CIF.
    // QLAUE_COORDINATE_CIF can point to the original file for the same checks.
    QString path = qEnvironmentVariable("QLAUE_COORDINATE_CIF");
    if(path.isEmpty()) path = writeCif(directory,
        "data_Na2Co2TeO6\n_space_group_name_H-M_alt 'P 63 2 2'\n_space_group_IT_number 182\n"
        "_cell_length_a 5.2709(2)\n_cell_length_b 5.2709(2)\n_cell_length_c 11.2615(15)\n"
        "_cell_angle_alpha 90\n_cell_angle_beta 90\n_cell_angle_gamma 120\n"
        "loop_\n_atom_site_label\n_atom_site_type_symbol\n_atom_site_fract_x\n"
        "_atom_site_fract_y\n_atom_site_fract_z\n_atom_site_U_iso_or_equiv\n"
        "Te Te -0.3333 -0.6667 -0.2500 0.0080(6)\n"
        "Co1 Co 0.0000 0.0000 -0.2500 0.0058(8)\n"
        "Co2 Co -0.6667 -0.3333 -0.2500 0.0100(9)\n"
        "O O -0.0247(11) -0.3592(10) -0.3442(4) 0.0123(11)\n"
        "Na Na -0.3200(20) -0.3200(20) -0.5000 0.088(5)\n");
    const int elements[] = {52, 27, 27, 8, 11};
    const double positions[][3] = {{-.3333,-.6667,-.25}, {0,0,-.25},
        {-.6667,-.3333,-.25}, {-.0247,-.3592,-.3442}, {-.32,-.32,-.5}};
    const auto check = [&](Crystal &crystal) {
        require(crystal.getSpaceGroup()->number==182, "Reported CIF space group changed");
        require(crystal.getNRootAtoms()==5, "Reported CIF lost source sites");
        for(int row=0; row<5; ++row) {
            Atom *atom = crystal.getAtom(row);
            require(atom->getZ()==elements[row], "Reported CIF element changed");
            near(atom->getXPos(),positions[row][0],QString("CIF site %1 x changed").arg(row+1));
            near(atom->getYPos(),positions[row][1],QString("CIF site %1 y changed").arg(row+1));
            near(atom->getZPos(),positions[row][2],QString("CIF site %1 z changed").arg(row+1));
        }
        // Independently cross-checked using Gemmi 0.7.5 with the original CIF.
        require(crystal.getNAtoms()==24, "Incorrect P6322 expansion of the reported CIF");
        int counts[99] = {};
        for(int i=0; i<crystal.getNAtoms(); ++i) ++counts[crystal.getAtom(i)->getZ()];
        require(counts[52]==2 && counts[27]==4 && counts[8]==12 && counts[11]==6,
                "Reported CIF symmetry multiplicities changed");
    };
    DataFile data;
    require(data.readCif(path),data.getErrorStr());
    Crystal parsed = data.getCrystal();
    check(parsed);
    CrystalDialog dialog;
    importViaDialog(dialog,path);
    QTableWidget *table = dialog.findChild<QTableWidget *>("AtomsTable");
    require(table != nullptr,"Atom table is missing");
    for(int row=0; row<5; ++row) for(int column=0; column<3; ++column)
        near(table->item(row,column+1)->text().toDouble(),positions[row][column],
             "Set Lattice display differs from source coordinates");
    const QString screenshot = qEnvironmentVariable("QLAUE_COORDINATE_SCREENSHOT");
    if(!screenshot.isEmpty()) {
        dialog.show();
        QApplication::processEvents();
        require(dialog.grab().save(screenshot),"Cannot save coordinate dialog preview");
    }
    dialog.accept();
    require(dialog.result()==QDialog::Accepted,"Reported CIF cannot be applied");
    Crystal roundTrip;
    dialog.getCrystal(&roundTrip);
    check(roundTrip);
    dialog.setCrystal(&roundTrip);
    dialog.getCrystal(&roundTrip);
    check(roundTrip);

    // Integer cell translations must not add atoms or change their phases.
    Crystal translated(parsed);
    translated.delAllAtoms();
    for(int row=0; row<5; ++row)
        translated.addAtom(elements[row],positions[row][0]+3,positions[row][1]-4,positions[row][2]+2);
    translated.spaceGroupGenerate();
    require(translated.getNAtoms()==parsed.getNAtoms(),"Cell translations duplicated atoms");
    for(int h=-2; h<=2; ++h) for(int k=-2; k<=2; ++k) for(int l=-2; l<=2; ++l) {
        std::complex<double> before(0,0), after(0,0);
        for(int i=0; i<parsed.getNAtoms(); ++i) {
            before += double(parsed.getAtom(i)->getZ()) * parsed.getAtom(i)->getPhase(h,k,l);
            after += double(translated.getAtom(i)->getZ()) * translated.getAtom(i)->getPhase(h,k,l);
        }
        near(std::abs(before-after),0,"Cell translations changed atomic phases");
    }
    qInfo() << "PASS: all 15 reported CIF coordinates survive import, display and repeated apply; 24 symmetry sites and periodic phases";
}

static void calculate(LaueFilm *film, const std::function<void()> &action) {
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout,&QTimer::timeout,[]{ require(false,"Rotation calculation timed out"); });
    QObject::connect(film,&LaueFilm::recalculated,&loop,&QEventLoop::quit);
    timeout.start(15000);
    action();
    loop.exec();
}

static void checkRotation() {
    Crystal initial;
    require(initial.getFreeRotate(),"Default rotation mode is not initialized");
    near(initial.getGonioX(),0,"Initial X angle is not zero");
    near(initial.getGonioY(),0,"Initial Y angle is not zero");
    near(initial.getGonioZ(),0,"Initial Z angle is not zero");
    initial.setFreeRotate(false);
    initial.rotateBy(0.1,0.2,0.3);
    initial.setName("Rotation copy test");
    Crystal copied(initial), assigned;
    assigned=initial;
    for(Crystal *c : {&copied,&assigned}) {
        require(!c->getFreeRotate(),"Copy lost rotation mode");
        near(c->getGonioX(),0.1,"Copy lost X angle");
        near(c->getGonioY(),0.2,"Copy lost Y angle");
        near(c->getGonioZ(),0.3,"Copy lost Z angle");
        c->setName(c->getName());
        require(QString(c->getName())=="Rotation copy test","Aliased name replacement failed");
    }
    MainWindow window;
    LaueFilm *film=window.findChild<LaueFilm *>();
    RotateWidget *controls=window.findChild<RotateWidget *>();
    require(film && controls,"Rotation controls missing");
    calculate(film,[&]{film->reCalc();});
    Crystal *crystal=film->getCrystal();
    controls->findChild<QDoubleSpinBox *>("stepsize")->setValue(2.0);
    const char *right[] = {"xright","yright","zright"};
    const char *left[] = {"xleft","yleft","zleft"};
    for(bool freeRotate : {true,false}) {
        for(int axis=0; axis<3; ++axis) {
            Matrix identity(3,3); identity.identity();
            crystal->setU(identity);
            crystal->setFreeRotate(freeRotate);
            for(int click=1; click<=3; ++click) {
                QAbstractButton *button=controls->findChild<QAbstractButton *>(right[axis]);
                require(button != nullptr, "Positive rotation button missing");
                calculate(film,[&]{button->click();});
                Matrix u=crystal->getU();
                int first=(axis+1)%3, second=(axis+2)%3;
                const double angle=click*2*M_PI/180;
                near(u.Get(first,first),cos(angle),QString("Axis %1 rotation did not accumulate on click %2 (free=%3)").arg(axis).arg(click).arg(freeRotate));
                near(u.Get(second,first),sin(angle),"Wrong rotation direction");
                near(u.Get(axis,axis),1,"Wrong axis changed");
            }
            QAbstractButton *button=controls->findChild<QAbstractButton *>(left[axis]);
            require(button != nullptr, "Negative rotation button missing");
            for(int click=0; click<3; ++click)
                calculate(film,[&]{button->click();});
            Matrix u=crystal->getU();
            for(int i=0;i<3;++i) for(int j=0;j<3;++j) near(u.Get(i,j),i==j?1:0,"Inverse clicks did not restore orientation");
        }
    }
    // These legacy workers have no QObject parent; stop them before the widgets go away.
    delete film->getLaue();
    delete film->getIndexing();
    qInfo() << "PASS: X/Y/Z positive and negative repeated clicks in both rotation modes";
}

int main(int argc,char **argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc,argv);
    QTemporaryDir directory;
    require(directory.isValid(),"Cannot create test directory");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,directory.path());
    QCoreApplication::setOrganizationName("QLaueTests");
    QCoreApplication::setApplicationName("CrystalFeatures");
    if(argc==1 || QString(argv[1])=="cif") checkCif(directory);
    if(argc==1 || QString(argv[1])=="coordinates") checkSourceCoordinates(directory);
    if(argc==1 || QString(argv[1])=="rotation") checkRotation();
    return 0;
}
