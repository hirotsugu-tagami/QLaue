#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <functional>
#include "mainwindow.h"
#include "datafile.h"

static void require(bool ok, const QString &message) {
    if(!ok) qFatal("%s", qPrintable(message));
}

struct DialogPlan {
    QStringList files; // An empty path cancels that file dialog.
    QList<QMessageBox::StandardButton> choices;
    int errors = 0;
    std::function<void()> afterFileAccepted;
};

static void drive(const std::function<void()> &action, DialogPlan plan = DialogPlan()) {
    QTimer timeout, driver;
    timeout.setSingleShot(true);
    QObject::connect(&timeout,&QTimer::timeout,[]{ qFatal("Persistence dialog timed out"); });
    timeout.start(10000);
    QPointer<QFileDialog> selected;
    QObject::connect(&driver,&QTimer::timeout,[&] {
        QWidget *modal = QApplication::activeModalWidget();
        if(auto *message = qobject_cast<QMessageBox *>(modal)) {
            if(message->standardButtons().testFlag(QMessageBox::Save)) {
                require(!plan.choices.isEmpty(),"Unexpected unsaved-changes prompt");
                message->button(plan.choices.takeFirst())->click();
            } else {
                require(plan.errors > 0,"Unexpected error: " + message->text());
                --plan.errors;
                message->accept();
            }
        } else if(auto *dialog = qobject_cast<QFileDialog *>(modal)) {
            if(selected == dialog) {
                require(QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection),"Cannot accept file dialog");
                return;
            }
            require(!plan.files.isEmpty(),"Unexpected file dialog");
            const QString path = plan.files.takeFirst();
            if(path.isEmpty()) { dialog->reject(); return; }
            if(plan.afterFileAccepted) {
                QObject::connect(dialog,&QDialog::accepted,dialog,plan.afterFileAccepted);
                plan.afterFileAccepted = {};
            }
            dialog->setDirectory(QFileInfo(path).absolutePath());
            dialog->selectFile(path);
            auto *name = dialog->findChild<QLineEdit *>("fileNameEdit");
            require(name,"File name input is missing");
            name->setText(path);
            selected = dialog;
        }
    });
    driver.start(10);
    action();
    require(plan.files.isEmpty() && plan.choices.isEmpty() && plan.errors == 0,
            "Expected file dialog, prompt or error was not shown");
}

static void invoke(MainWindow &window, const char *slot, const QString &path = QString()) {
    const bool ok = path.isNull()
        ? QMetaObject::invokeMethod(&window,slot,Qt::DirectConnection)
        : QMetaObject::invokeMethod(&window,slot,Qt::DirectConnection,Q_ARG(QString,path));
    require(ok,QString("Cannot invoke ") + slot);
}

static bool save(MainWindow &window, bool as = false) {
    bool result = false;
    require(QMetaObject::invokeMethod(&window,as ? "saveAsCrystal" : "saveCrystal",
            Qt::DirectConnection,Q_RETURN_ARG(bool,result)),"Cannot invoke Save");
    return result;
}

static QByteArray state(LaueFilm *film) {
    DataFile data;
    data.setCrystal(film->getCrystal());
    data.setLaue(film);
    return data.documentState();
}

static QByteArray readBytes(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly),"Cannot read " + path);
    return file.readAll();
}

static void writeBytes(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),"Cannot write fixture " + path);
}

static void setCell(LaueFilm *film, double a) {
    film->getCrystal()->setLattice(a,a,a,M_PI/2,M_PI/2,M_PI/2);
}

static void fixture(const QString &path, LaueFilm *film, double a) {
    Crystal crystal;
    crystal.setLattice(a,a,a,M_PI/2,M_PI/2,M_PI/2);
    crystal.addAtom(29,0,0,0);
    DataFile data;
    data.setCrystal(&crystal);
    data.setLaue(film);
    require(data.write(path),"Cannot save fixture: " + data.getErrorStr());
}

int main(int argc, char **argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc,argv);
    app.setQuitOnLastWindowClosed(false);
    QTemporaryDir temporary;
    require(temporary.isValid(),"Cannot create temporary directory");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temporary.path());
    QCoreApplication::setOrganizationName("QLaueTests");
    QCoreApplication::setApplicationName("Persistence");
    MainWindow window;
    auto *film = window.findChild<LaueFilm *>();
    require(film,"Film is missing");
    const double defaultScale = film->scale();

    const QString savedDir = temporary.filePath("saved");
    require(QDir().mkpath(savedDir),"Cannot create fixture directory");
    const QString original = savedDir + "/original.xtl";
    const QString other = temporary.filePath("other.xtl");
    const QString created = temporary.filePath("new-analysis.xtl");
    fixture(original,film,6);
    fixture(other,film,7);
    const QByteArray originalBytes = readBytes(original);
    drive([&]{ invoke(window,"loadCrystal",original); });
    // Comments containing creation timestamps must not make a clean document dirty.
    QEventLoop elapsed;
    QTimer::singleShot(1100,&elapsed,&QEventLoop::quit);
    elapsed.exec();
    drive([&]{ invoke(window,"newCrystal"); });
    DialogPlan plan;
    plan.files << created;
    drive([&]{ require(save(window),"New document Save failed"); },plan);
    require(readBytes(original) == originalBytes,"New then Save overwrote the previous document");
    require(QFile::exists(created) && window.windowTitle().contains("new-analysis.xtl"),"Save As did not set the new file");

    setCell(film,8);
    const QByteArray dirty = state(film);
    const QString dirtyTitle = window.windowTitle();
    plan = DialogPlan(); plan.choices << QMessageBox::Cancel;
    drive([&]{ invoke(window,"newCrystal"); },plan);
    require(state(film) == dirty && window.windowTitle() == dirtyTitle,"Cancel changed the document");
    plan.choices = {QMessageBox::Save};
    drive([&]{ invoke(window,"newCrystal"); },plan);
    DataFile saved;
    require(saved.read(created) && saved.getCrystal().getLatticeA() == 8,"Save before New lost edits");
    setCell(film,9);
    plan.choices = {QMessageBox::Discard};
    drive([&]{ invoke(window,"newCrystal"); },plan);
    require(film->getCrystal()->getLatticeA() == 5.4,"Discard did not create a new document");
    setCell(film,9);
    plan.choices = {QMessageBox::Save}; plan.files = QStringList{QString()};
    drive([&]{ invoke(window,"newCrystal"); },plan);
    require(film->getCrystal()->getLatticeA() == 9,"Canceled Save As discarded unsaved changes");
    plan = DialogPlan(); plan.choices << QMessageBox::Discard;
    drive([&]{ invoke(window,"loadCrystal",original); },plan);
    qInfo() << "PASS: New target reset, clean timestamps and Save/Discard/Cancel";

    setCell(film,8);
    const QByteArray beforeFailure = state(film);
    const QString beforeTitle = window.windowTitle();
    const QStringList beforeRecent = QSettings().value("recentFileList").toStringList();
    const QString movedDir = temporary.filePath("moved");
    require(QDir().rename(savedDir,movedDir),"Cannot move temporary save directory");
    plan = DialogPlan(); plan.errors = 1;
    drive([&]{ require(!save(window),"Failed write reported success"); },plan);
    require(state(film) == beforeFailure && window.windowTitle() == beforeTitle &&
            QSettings().value("recentFileList").toStringList() == beforeRecent,"Failed Save changed document identity");
    plan.choices << QMessageBox::Save;
    drive([&]{ invoke(window,"newCrystal"); },plan);
    require(state(film) == beforeFailure,"Failed Save allowed New to discard edits");
    require(QDir().rename(movedDir,savedDir),"Cannot restore temporary save directory");
    require(readBytes(original) == originalBytes,"Failed Save damaged the previous file");

    const QString asDir = temporary.filePath("save-as");
    const QString movedAsDir = temporary.filePath("save-as-moved");
    require(QDir().mkpath(asDir),"Cannot create Save As directory");
    plan = DialogPlan(); plan.files << asDir + "/failed.xtl"; plan.errors = 1;
    plan.afterFileAccepted = [&]{ require(QDir().rename(asDir,movedAsDir),"Cannot simulate unavailable Save As directory"); };
    drive([&]{ require(!save(window,true),"Failed Save As reported success"); },plan);
    require(state(film) == beforeFailure && window.windowTitle() == beforeTitle &&
            QSettings().value("recentFileList").toStringList() == beforeRecent,"Failed Save As changed document identity");
    plan = DialogPlan(); plan.files << QString();
    drive([&]{ require(!save(window,true),"Canceled Save As reported success"); },plan);
    require(window.windowTitle() == beforeTitle,"Canceled Save As changed the file name");
    qInfo() << "PASS: Save/Save As failures and cancellation preserve document, target and recent files";

    plan = DialogPlan(); plan.files << QString();
    drive([&]{ invoke(window,"open"); },plan);
    require(state(film) == beforeFailure && window.windowTitle() == beforeTitle,"Canceled Open changed the document");
    plan.files = QStringList{other}; plan.choices << QMessageBox::Cancel;
    drive([&]{ invoke(window,"open"); },plan);
    require(state(film) == beforeFailure,"Open ignored Cancel at the unsaved prompt");
    const QString broken = temporary.filePath("broken.xtl");
    writeBytes(broken,"<unrelated/>");
    plan = DialogPlan(); plan.files << broken; plan.choices << QMessageBox::Discard; plan.errors = 1;
    drive([&]{ invoke(window,"open"); },plan);
    require(state(film) == beforeFailure && window.windowTitle() == beforeTitle &&
            QSettings().value("recentFileList").toStringList() == beforeRecent,"Failed Open changed the document or recent files");
    plan = DialogPlan(); plan.choices << QMessageBox::Cancel;
    drive([&]{ invoke(window,"newCrystal"); },plan); // Failed Open must leave the document dirty.
    plan = DialogPlan(); plan.files << other; plan.choices << QMessageBox::Discard;
    drive([&]{ invoke(window,"open"); },plan);
    require(film->getCrystal()->getLatticeA() == 7,"Open did not load the selected file");

    film->getLaue()->setDistance(42);
    QAction *recent = nullptr;
    for(QAction *action : window.findChildren<QAction *>())
        if(action->data().toString() == original) recent = action;
    require(recent,"Original document is missing from recent files");
    plan = DialogPlan(); plan.choices << QMessageBox::Cancel;
    drive([&]{ recent->trigger(); },plan);
    require(film->getLaue()->getDistance() == 42,"Recent ignored Cancel for detector changes");
    plan.choices = {QMessageBox::Discard};
    drive([&]{ recent->trigger(); },plan);
    require(window.windowTitle().contains("original.xtl"),"Recent did not load its target");
    qInfo() << "PASS: Open cancellation/failure and Recent share unsaved-change protection";

    const QString cel = temporary.filePath("import.cel");
    const QString cif = temporary.filePath("import.cif");
    writeBytes(cel,"CELL 7 7 7 90 90 90\nnatom 1\nCu 29 0 0 0\nrgnr 1\n");
    writeBytes(cif,"data_test\n_cell_length_a 7\n_cell_length_b 7\n_cell_length_c 7\n"
                   "_cell_angle_alpha 90\n_cell_angle_beta 90\n_cell_angle_gamma 90\n"
                   "_space_group_IT_number 1\nloop_\n_atom_site_type_symbol\n_atom_site_fract_x\n"
                   "_atom_site_fract_y\n_atom_site_fract_z\nCu 0 0 0\n");
    for(bool useCif : {false,true}) {
        const char *slot = useCif ? "importCif" : "importCel";
        const QString path = useCif ? cif : cel;
        setCell(film,8);
        const QByteArray before = state(film);
        plan = DialogPlan(); plan.files << path; plan.choices << QMessageBox::Cancel;
        drive([&]{ invoke(window,slot); },plan);
        require(state(film) == before,"Import ignored Cancel");
        plan.choices = {QMessageBox::Discard};
        drive([&]{ invoke(window,slot); },plan);
        require(film->getCrystal()->getLatticeA() == 7 && !window.windowTitle().contains("original.xtl"),
                "Import did not create an unnamed analysis");
        plan = DialogPlan(); plan.choices << QMessageBox::Cancel;
        drive([&]{ invoke(window,"newCrystal"); },plan);
        plan.choices = {QMessageBox::Discard};
        drive([&]{ invoke(window,"loadCrystal",original); },plan);
    }
    qInfo() << "PASS: CIF/CEL imports preserve canceled edits and require saving the imported analysis";

    QImage image(24,16,QImage::Format_RGB32); image.fill(Qt::red);
    const QString imagePath = temporary.filePath("image.png");
    require(image.save(imagePath),"Cannot save image fixture");
    plan = DialogPlan(); plan.files << imagePath;
    drive([&]{ invoke(window,"importImage"); },plan);
    plan = DialogPlan(); plan.choices << QMessageBox::Cancel;
    drive([&]{ invoke(window,"newCrystal"); },plan);
    film->setOriginX(12); film->setOriginY(-7); film->setScale(8);
    drive([&]{ require(save(window),"Cannot save image analysis"); });
    // Changing only calibration must also count as an unsaved change.
    film->setScale(9);
    plan = DialogPlan(); plan.choices << QMessageBox::Cancel;
    drive([&]{ invoke(window,"newCrystal"); },plan);
    drive([&]{ require(save(window),"Cannot save calibration"); });
    film->adjustImage(-15,75,230,20);
    film->setInvertImage(true); film->setRotateImage(true);
    plan = DialogPlan(); plan.choices << QMessageBox::Cancel;
    drive([&]{ invoke(window,"newCrystal"); },plan);
    drive([&]{ require(save(window),"Cannot save image adjustments"); });
    film->getLaue()->setCalcPrecision(3);
    film->getLaue()->setIntensityCorrection(0);
    film->getLaue()->setTubeZ(42);
    plan = DialogPlan(); plan.choices << QMessageBox::Cancel;
    drive([&]{ invoke(window,"newCrystal"); },plan);
    drive([&]{ require(save(window),"Cannot save diffraction settings"); });
    const QImage adjusted = *film->importedAdjustedImage;
    drive([&]{ invoke(window,"loadCrystal",other); });
    drive([&]{ invoke(window,"loadCrystal",original); });
    require(*film->importedAdjustedImage == adjusted && film->imageBrightness() == -15 &&
            film->imageContrast() == 75 && film->isImageInverted() && film->isImageRotated(),
            "Image adjustment state did not survive Save/Open");
    require(film->getLaue()->calcPrecision() == 3 && film->getLaue()->intensityCorrection() == 0 &&
            film->getLaue()->tubeZ() == 42,"Diffraction settings did not survive Save/Open");
    auto *brightness = window.findChild<QScrollBar *>("BrightnessScroll");
    require(brightness && brightness->value() == -15 &&
            window.findChild<QAction *>("actionRotate_Image")->isChecked(),
            "Loaded image settings were not reflected in the controls");
    drive([&]{ invoke(window,"loadCrystal",other); });
    require(film->importedImage->isNull() && film->importedScaledImage->isNull() &&
            film->originX() == 0 && film->originY() == 0 && film->scale() == defaultScale,
            "Image-free document retained the old image or calibration");
    auto *showImage = window.findChild<QAction *>("actionShow_Image");
    auto *controls = window.findChild<ImageControlWidget *>();
    require(showImage && !showImage->isEnabled() && !showImage->isChecked(),"Image-free document retained the image action state");
    auto *info = controls ? controls->findChild<QTextBrowser *>("ImageInfo") : nullptr;
    auto *contrast = controls ? controls->findChild<QScrollBar *>("ContrastScroll") : nullptr;
    require(info && info->toPlainText().isEmpty(),"Image information was not cleared");
    require(contrast && !contrast->isEnabled(),"Image controls remained enabled");
    qInfo() << "PASS: image/calibration/adjustment/calculation dirty checks and Save/Open; image-free load clears state";

    film->getCrystal()->rotateBy(0.05,0,0);
    plan = DialogPlan(); plan.choices << QMessageBox::Cancel;
    QCloseEvent canceledClose;
    drive([&]{ QApplication::sendEvent(&window,&canceledClose); },plan);
    require(!canceledClose.isAccepted(),"Close ignored Cancel for orientation changes");
    plan.choices = {QMessageBox::Save};
    QCloseEvent savedClose;
    drive([&]{ QApplication::sendEvent(&window,&savedClose); },plan);
    require(savedClose.isAccepted(),"Close did not continue after a successful Save");
    require(!QSettings().value("recentFileList").toStringList().contains(QString()),"Unnamed document entered recent files");
    qInfo() << "PASS: Close honors Cancel and requires a successful Save";
    return 0;
}
