// Probes for unresolved findings. A nonzero exit indicates the defect persists.
#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <cmath>
#include "mainwindow.h"
#include "datafile.h"
#include "reorientdialog.h"
#include "crystaldialog.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (argc != 2)
        return 2;
    QTemporaryDir directory;
    if (!directory.isValid())
        return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QCoreApplication::setOrganizationName("QLaueTests");
    QCoreApplication::setApplicationName("Audit");
    const QString probe = QString::fromLocal8Bit(argv[1]);

    if (probe == "save") {
        DataFile data;
        bool saved = data.write(directory.filePath("missing/test.xtl"));
        qInfo() << "write to missing directory returned" << saved << "; expected false";
        return saved ? 1 : 0;
    }
    if (probe == "schema") {
        const QString path = directory.filePath("unrelated.xtl");
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return 2;
        file.write("<unrelated/>\n");
        file.close();
        DataFile data;
        bool accepted = data.read(path);
        qInfo() << "unrelated XML accepted" << accepted << "; expected false";
        return accepted ? 1 : 0;
    }

    MainWindow window;
    LaueFilm *film = window.findChild<LaueFilm *>();
    if (!film)
        return 2;
    Crystal *crystal = film->getCrystal();
    QImage image(24, 16, QImage::Format_RGB32);
    image.fill(Qt::white);

    if (probe == "stale-image" || probe == "new-target") {
        const QString path = directory.filePath("original.xtl");
        DataFile data;
        data.setCrystal(crystal);
        data.setLaue(film);
        if (!data.write(path))
            return 2;
        film->setImage(image, true);
        QMetaObject::invokeMethod(&window, "loadCrystal", Qt::DirectConnection,
                                  Q_ARG(QString, path));
        if (probe == "stale-image") {
            qInfo() << "old image retained after loading image-free crystal"
                    << !film->importedImage->isNull() << "; expected false";
            return film->importedImage->isNull() ? 0 : 1;
        }
        QMetaObject::invokeMethod(&window, "newCrystal", Qt::DirectConnection);
        qInfo() << "new crystal title" << window.windowTitle()
                << "; expected no original.xtl";
        return window.windowTitle().contains("original.xtl") ? 1 : 0;
    }
    if (probe == "orientation") {
        crystal->setU(1, 0, 0, 1, 0, 1);
        Matrix rotation = crystal->getU();
        Matrix identity = rotation * rotation.transpose();
        qInfo() << "U*transpose(U) diagonal" << identity.Get(0, 0)
                << identity.Get(1, 1) << identity.Get(2, 2) << "; expected 1 1 1";
        return std::fabs(identity.Get(1, 1) - 1) > 1e-10 ? 1 : 0;
    }
    if (probe == "rotation") {
        film->setImage(image, true);
        film->setRotateImage(true);
        qInfo() << "rotated image" << film->importedAdjustedImage->size()
                << "display crop" << film->imageZoomRect.size();
        return film->importedAdjustedImage->size() == film->imageZoomRect.size() ? 0 : 1;
    }
    if (probe == "reorientation-row") {
        ReorientDialog dialog;
        dialog.setCrystal(crystal);
        dialog.setLaue(film->getLaue());
        QMetaObject::invokeMethod(&dialog, "go", Qt::DirectConnection);
        QTableWidget *table = dialog.findChild<QTableWidget *>("DataTable");
        if (!table || table->rowCount() != 8)
            return 2;
        table->selectRow(6);
        QMetaObject::invokeMethod(&dialog, "okPressed", Qt::DirectConnection);
        qInfo() << "row 7 accepted" << dialog.getSelectedOrientation() << "; expected true";
        return dialog.getSelectedOrientation() ? 0 : 1;
    }
    if (probe == "hydrogen") {
        crystal->delAllAtoms();
        crystal->addAtom(1, 0, 0, 0);
        CrystalDialog dialog;
        dialog.setCrystal(crystal);
        dialog.getCrystal(crystal);
        qInfo() << "hydrogen count after dialog round trip" << crystal->getNRootAtoms()
                << "; expected 1";
        return crystal->getNRootAtoms() == 1 ? 0 : 1;
    }
    return 2;
}
