#include <QApplication>
#include <QFileDialog>
#include <QImageReader>
#include <QImageWriter>
#include <QLineEdit>
#include <QMessageBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include "mainwindow.h"

static void require(bool success, const char *message)
{
    if (!success)
        qFatal("%s", message);
}

// Exercise the actual import slot, including its dialog, without user interaction.
static void importFile(MainWindow &window, const QString &path,
                       bool cancel = false, bool expectError = false)
{
    bool selected = false;
    bool reportedError = false;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, [] {
        qFatal("Image import dialog timed out");
    });
    timeout.start(10000);

    QTimer driver;
    QObject::connect(&driver, &QTimer::timeout, [&] {
        QWidget *modal = QApplication::activeModalWidget();
        if (QMessageBox *message = qobject_cast<QMessageBox *>(modal)) {
            require(expectError, "Unexpected image import error");
            require(message->text().contains(QFileInfo(path).fileName()),
                    "Image error must identify the file");
            reportedError = true;
            message->accept();
        } else if (QFileDialog *dialog = qobject_cast<QFileDialog *>(modal)) {
            const QString filter = dialog->nameFilters().first();
            require(filter.contains("*.bmp"), "BMP missing from image filter");
            require(filter.contains("*.png"), "PNG missing from image filter");
            require(dialog->nameFilters().size() == 2, "Missing all-files fallback");
            if (cancel) {
                dialog->reject();
            } else if (!selected) {
                if (expectError || QFileInfo(path).suffix().isEmpty())
                    dialog->selectNameFilter(dialog->nameFilters().last());
                dialog->setDirectory(QFileInfo(path).absolutePath());
                dialog->selectFile(path);
                QLineEdit *fileName = dialog->findChild<QLineEdit *>("fileNameEdit");
                require(fileName != nullptr, "File name input missing");
                fileName->setText(path);
                selected = true;
            } else {
                require(QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection),
                        "Could not accept file dialog");
            }
        }
    });
    driver.start(25);
    require(QMetaObject::invokeMethod(&window, "importImage", Qt::DirectConnection),
            "Could not invoke image import");
    require(reportedError == expectError, "Incorrect image error handling");
}

int main(int argc, char **argv)
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    require(temporary.isValid(), "Could not create temporary directory");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    QCoreApplication::setOrganizationName("QLaueTests");
    QCoreApplication::setApplicationName("ImageImport");

    MainWindow window;
    LaueFilm *film = window.findChild<LaueFilm *>();
    require(film != nullptr, "Laue film widget missing");
    QImage source(24, 16, QImage::Format_RGB32);
    source.fill(qRgb(96, 160, 224));
    source.setPixel(0, 0, qRgb(255, 0, 0));

    QStringList suffixes;
    suffixes << "bmp" << "BMP" << "png" << "jpeg";
    if (QImageReader::supportedImageFormats().contains("tiff") &&
        QImageWriter::supportedImageFormats().contains("tiff"))
        suffixes << "tiff";
    foreach (const QString &suffix, suffixes) {
        const QString path = temporary.filePath(QString::fromUtf8("回折 像.") + suffix);
        require(source.save(path), "Could not create image fixture");
        importFile(window, path);
        require(film->importedImage->size() == source.size(), "Wrong imported size");
        require(*film->importedImage == QImage(path), "Imported pixels changed");
        qInfo() << "PASS import" << suffix;
    }

    const QImage::Format formats[] = { QImage::Format_Indexed8, QImage::Format_Mono };
    for (QImage::Format format : formats) {
        const QString path = temporary.filePath("palette.bmp");
        require(source.convertToFormat(format).save(path), "Could not save paletted BMP");
        importFile(window, path);
        require(*film->importedImage == QImage(path), "Paletted BMP import changed");
        qInfo() << "PASS BMP format" << format;
    }

    const QString noSuffix = temporary.filePath("no-extension");
    require(source.save(noSuffix, "BMP"), "Could not save extensionless BMP");
    importFile(window, noSuffix);
    require(*film->importedImage == source, "All-files BMP import failed");
    qInfo() << "PASS extensionless BMP";

    const QImage previous = *film->importedImage;
    importFile(window, QString(), true);
    require(*film->importedImage == previous, "Cancel replaced the image");
    const QString corrupt = temporary.filePath("broken.bmp");
    QFile broken(corrupt);
    require(broken.open(QIODevice::WriteOnly), "Could not create invalid image");
    broken.write("not a bitmap");
    broken.close();
    importFile(window, corrupt, false, true);
    require(*film->importedImage == previous, "Failed import replaced the image");
    qInfo() << "PASS cancel and invalid-image preservation";
    return 0;
}
