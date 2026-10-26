#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QTemporaryDir>
#include <cmath>
#include <limits>
#include "datafile.h"
#include "lauecontrols.h"
#include "mainwindow.h"
#include "preferences.h"

static void require(bool ok, const QString &message) {
    if(!ok) qFatal("%s", qPrintable(message));
}

static void save(const QString &path, Crystal &crystal, LaueFilm &film) {
    DataFile data;
    data.setCrystal(&crystal);
    data.setLaue(&film);
    require(data.write(path), data.getErrorStr());
}

int main(int argc, char **argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc,argv);
    app.setQuitOnLastWindowClosed(false);
    QTemporaryDir temporary;
    require(temporary.isValid(), "Cannot create temporary directory");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temporary.path());
    QCoreApplication::setOrganizationName("QLaueTests");
    QCoreApplication::setApplicationName("SettingsRegressions");

    Crystal crystal;
    crystal.addAtom(29,0,0,0);
    LaueFilm source(nullptr,&crystal);
    LaueControls controls;
    controls.setAll(&source);
    QObject::connect(&controls,&LaueControls::accuracyLevelChanged,&source,&LaueFilm::setAccuracy);
    auto *accuracy = controls.findChild<QSpinBox *>("AccuracySpinBox");
    require(accuracy && accuracy->minimum()==0 && accuracy->maximum()==10,"Accuracy UI endpoints");
    for(int uiAccuracy : {0,10}) {
        accuracy->setValue(uiAccuracy);
        require(source.getLaue()->calcPrecision()==11-uiAccuracy,"Accuracy model mapping");
        const QString path=temporary.filePath(QString("accuracy-%1.xtl").arg(uiAccuracy));
        save(path,crystal,source);
        DataFile restored;
        require(restored.read(path),restored.getErrorStr());
        restored.getLaue(&source);
        require(source.getLaue()->calcPrecision()==11-uiAccuracy && source.getAccuracy()==uiAccuracy,
                "Accuracy endpoint did not round trip");
    }

    source.setDisplayIntensities(false);
    QImage image(37,25,QImage::Format_RGB32); image.fill(Qt::green);
    source.setImage(image,true);
    source.setOriginX(2.75); source.setOriginY(-1.25); source.setScale(7.25);
    const struct { double threshold, saturation; int shownThreshold, shownSaturation; } levels[] = {
        {.29,.57,29,57}, {.29,.58,29,58}, {.294,.575,29,58}
    };
    for(int i=0;i<3;++i) {
        source.getLaue()->setDet(levels[i].threshold,levels[i].saturation);
        const QString path=temporary.filePath(QString("levels-%1.xtl").arg(i));
        save(path,crystal,source);
        MainWindow window;
        require(QMetaObject::invokeMethod(&window,"loadCrystal",Qt::DirectConnection,Q_ARG(QString,path)),
                "Cannot invoke normal XTL load");
        auto *film=window.findChild<LaueFilm *>();
        auto *loadedControls=window.findChild<LaueControls *>();
        require(film && loadedControls,"Loaded controls missing");
        auto *threshold=loadedControls->findChild<QSpinBox *>("DetLevelSpinBox");
        auto *saturation=loadedControls->findChild<QSpinBox *>("SatLevelSpinBox");
        auto *thresholdSlider=loadedControls->findChild<QSlider *>("DetLevelSlider");
        auto *saturationSlider=loadedControls->findChild<QSlider *>("SatLevelSlider");
        require(threshold && saturation && thresholdSlider && saturationSlider,"Detector controls missing");
        require(threshold->value()==levels[i].shownThreshold &&
                saturation->value()==levels[i].shownSaturation &&
                thresholdSlider->value()==threshold->value() && saturationSlider->value()==saturation->value(),
                "Detector display must round to nearest percent");
        require(film->getLaue()->getDetLevel()==levels[i].threshold &&
                film->getLaue()->getDetSatLevel()==levels[i].saturation,
                "Loading UI must not quantize serialized detector settings");
        require(film->originX()==2.75 && film->originY()==-1.25 && film->scale()==7.25,
                "Loading XTL must restore the saved image calibration exactly");
    }

    for(int mask : {0,4,513}) {
        source.getLaue()->setIntensityCorrection(mask);
        source.getLaue()->setLambdaRange(.300123456,3.000123456);
        PreferencesDialog dialog;
        dialog.setLaue(source.getLaue());
        dialog.getLaue(source.getLaue());
        require(source.getLaue()->intensityCorrection()==mask,"Unchanged Preferences changed correction bits");
        require(source.getLaue()->getLambdaRangeMin()==.300123456 &&
                source.getLaue()->getLambdaRangeMax()==3.000123456,
                "Unchanged Preferences rounded the wavelength range");
    }
    source.getLaue()->setIntensityCorrection(1023);
    source.getLaue()->setTubeZ(26);
    PreferencesDialog importedAnode;
    importedAnode.setLaue(source.getLaue());
    importedAnode.getLaue(source.getLaue());
    require(source.getLaue()->intensityCorrection()==1023 && source.getLaue()->tubeZ()==26,
            "Unchanged Preferences replaced an imported anode or correction mask");
    source.getLaue()->setTubeZ(29);
    PreferencesDialog tungstenChoice;
    tungstenChoice.setLaue(source.getLaue());
    auto *anode=tungstenChoice.findChild<QComboBox *>("LaueAnodeComboBox");
    require(anode && anode->findText("W ")>=0,"Tungsten choice missing");
    anode->setCurrentIndex(anode->findText("W "));
    tungstenChoice.getLaue(source.getLaue());
    require(source.getLaue()->tubeZ()==74,"Selecting tungsten did not change the model anode");
    PreferencesDialog tungstenLoaded;
    tungstenLoaded.setLaue(source.getLaue());
    auto *loadedAnode=tungstenLoaded.findChild<QComboBox *>("LaueAnodeComboBox");
    require(loadedAnode && loadedAnode->currentText()=="W " &&
            loadedAnode->findText("W ")==2 && loadedAnode->count()==4,
            "Opening tungsten created a duplicate anode or changed selection");
    tungstenLoaded.getLaue(source.getLaue());
    require(source.getLaue()->tubeZ()==74,"Unchanged Preferences replaced tungsten");
    source.getLaue()->setIntensityCorrection(0);
    PreferencesDialog preferences;
    preferences.setLaue(source.getLaue());
    auto *voltage=preferences.findChild<QDoubleSpinBox *>("LaueTubeVoltageSpinBox");
    require(voltage,"Tube voltage control missing");
    voltage->setValue(1);
    require(voltage->value()>1,"UI accepted an invalid tube voltage");
    preferences.getLaue(source.getLaue());
    require(source.getLaue()->getLambdaRangeMin()<=source.getLaue()->getLambdaRangeMax(),
            "Preferences accepted an inverted wavelength range");
    voltage->setValue(60);
    preferences.getLaue(source.getLaue());
    require(std::abs(source.getLaue()->getLambdaRangeMin()-HC_OVER_E/60)<1e-12,
            "Valid tube voltage cannot recover after an invalid attempt");

    const double validDistance=source.getLaue()->getDistance();
    const double validMin=source.getLaue()->getLambdaRangeMin();
    const double validMax=source.getLaue()->getLambdaRangeMax();
    source.getLaue()->setDistance(0);
    source.getLaue()->setDistance(std::numeric_limits<double>::quiet_NaN());
    source.getLaue()->setLambdaRange(12.39842,3);
    source.getLaue()->setLambdaRange(-1,3);
    require(source.getLaue()->getDistance()==validDistance &&
            source.getLaue()->getLambdaRangeMin()==validMin &&
            source.getLaue()->getLambdaRangeMax()==validMax,
            "Invalid model setters discarded valid settings");
    auto *distance=controls.findChild<QDoubleSpinBox *>("sampleFilmD");
    require(distance && distance->minimum()>0,"Distance UI permits zero");
    distance->setValue(0);
    require(distance->value()>0,"Distance UI accepted zero");
    source.getLaue()->setDistance(42);
    require(source.getLaue()->getDistance()==42,"Valid distance cannot recover");
    lauegram unconfigured;
    require(!unconfigured.calc(&crystal),"Calculation accepted an unset wavelength range");

    qInfo("PASS: accuracy, detector levels, image calibration, preferences and invalid geometry");
}
