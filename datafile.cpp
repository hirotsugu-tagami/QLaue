/*

	QLaue - Orientation of single crystal samples by the Laue method. 
	Copyright (C) 2007 Stuart Brian Wilkins

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
	
	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
	
	$Revision$
	$Author$
	$Date$
	$HeadURL$

*/

#include <QtCore>
#include <QtXml/QtXml>
#include <QImage>
#include <QSaveFile>
#include <cmath>
#include "datafile.h"
#include "cifreader.h"
#include "crystal.h"
#include "laue.h"
#include "matrix.h"
#include "version.h"
//#include "singlecrystal.h"

DataFile::DataFile(): 
domDocument(APP_NAME) {
	
	experimentDom = domDocument.createElement(APP_NAME);
    domDocument.appendChild(experimentDom);
	QDomComment comment = domDocument.createComment(QString(APP_NAME) + QString(" ") + QString(APP_COPYLEFT));
	experimentDom.appendChild(comment);
	comment = domDocument.createComment(QString("All Rights Reserved"));
	experimentDom.appendChild(comment);
	QDateTime now = QDateTime::currentDateTime();
	comment = domDocument.createComment(QString("Created on ") 
													+ now.toString("ddd MMMM d yy hh:mm:ss"));
	experimentDom.appendChild(comment);
	
    errorLine = errorColumn = 0;
    ox = oy = 0;
    scale = 100;
    lambda_min = 0.3;
    lambda_max = 3;
    det_thresh = 0.02;
    det_sat = 0.45;
    distance = 35;
	
}

bool DataFile::write(QString filename) {
    errorStr.clear();
    errorLine = errorColumn = 0;
    DataFile checked;
    checked.domDocument = domDocument;
    if(!checked.parse()) {
        errorStr = checked.errorStr;
        return false;
    }
    QSaveFile file(filename);
    if(!file.open(QIODevice::WriteOnly)) {
        errorStr = file.errorString();
        return false;
    }
    const QByteArray bytes = domDocument.toByteArray(4);
    if(file.write(bytes) != bytes.size()) {
        errorStr = file.errorString();
        file.cancelWriting();
        return false;
    }
    if(!file.commit()) {
        errorStr = file.errorString();
        return false;
    }
    return true;
}

QByteArray DataFile::documentState() const {
    QDomDocument state = domDocument.cloneNode(true).toDocument();
    QDomElement root = state.documentElement();
    for(QDomNode node = root.firstChild(); !node.isNull();) {
        QDomNode next = node.nextSibling();
        if(node.isComment()) root.removeChild(node);
        node = next;
    }
    return state.toByteArray(0);
}

bool DataFile::read(QString filename) {
    errorStr.clear();
    errorLine = errorColumn = 0;
    QFile file(filename);
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        errorStr = file.errorString();
        return false;
    }
    // Decode and validate a separate document; a failed load never replaces
    // the caller's existing crystal, image or calibration.
    DataFile candidate;
    if(!candidate.domDocument.setContent(&file, true, &errorStr, &errorLine, &errorColumn))
        return false;
    if(file.error() != QFile::NoError) {
        errorStr = file.errorString();
        return false;
    }
    if(!candidate.parse()) {
        errorStr = candidate.errorStr;
        errorLine = candidate.errorLine;
        return false;
    }
    *this = candidate;
    return true;
}

Crystal DataFile::getCrystal(void){
	return crystal;
}

void DataFile::getLaue(LaueFilm* laue){
	laue->getLaue()->setLambdaRange(lambda_min, lambda_max);
	laue->getLaue()->setDet(det_thresh, det_sat);
	laue->getLaue()->setDistance(distance);
	laue->getLaue()->setCalcPrecision(precision);
	laue->getLaue()->setIntensityCorrection(corrections);
	laue->getLaue()->setTubeZ(tubeZ);
	laue->getLaue()->setIncidentPoln(polarization);
	laue->setDisplayIntensities(intensities);
}

void DataFile::applyImageSettings(LaueFilm *film) const {
    film->adjustImage(brightness,contrast,maximum,minimum);
    film->setInvertImage(inverted);
    film->setRotateImage(rotated);
    film->setDisplayImage(imageDisplayed);
}

QImage DataFile::getLaueImage(void){
	return laueImage;
}

namespace {
bool finiteNumber(const QString &text, double &value) {
    bool ok = false;
    value = text.toDouble(&ok);
    return ok && std::isfinite(value);
}

bool readAttributes(const QDomElement &element, const QStringList &names, double *values) {
    for(int i=0; i<names.size(); ++i)
        if(!finiteNumber(element.attribute(names.at(i)), values[i])) return false;
    return true;
}

bool optionalInteger(const QDomElement &element, const QString &name, int &value, int low, int high) {
    if(!element.hasAttribute(name)) return true;
    bool ok = false;
    const int parsed = element.attribute(name).toInt(&ok);
    if(!ok || parsed < low || parsed > high) return false;
    value = parsed;
    return true;
}

bool optionalBool(const QDomElement &element, const QString &name, bool &value) {
    int parsed = value ? 1 : 0;
    if(!optionalInteger(element,name,parsed,0,1)) return false;
    value = parsed != 0;
    return true;
}
}

bool DataFile::parse() {
    experimentDom = domDocument.documentElement();
    auto fail = [&](const QString &message) {
        errorStr = message;
        return false;
    };
    if(experimentDom.tagName() != APP_NAME)
        return fail("This is not a QLaue experiment file.");
    const QDomElement structure = experimentDom.firstChildElement("crystal");
    const QDomElement lattice = structure.firstChildElement("lattice");
    if(structure.isNull() || lattice.isNull() ||
       !structure.nextSiblingElement("crystal").isNull() ||
       !lattice.nextSiblingElement("lattice").isNull())
        return fail("The experiment needs exactly one crystal and lattice.");
    double cell[6];
    if(!readAttributes(lattice, {"a","b","c","alpha","beta","gamma"}, cell))
        return fail("The lattice needs six finite numeric parameters.");
    for(int i=3; i<6; ++i) cell[i] *= M_PI / 180;
    if(!Crystal::isValidLattice(cell[0],cell[1],cell[2],cell[3],cell[4],cell[5]))
        return fail("The unit cell has invalid lengths, angles or volume.");
    crystal.setLattice(cell[0],cell[1],cell[2],cell[3],cell[4],cell[5]);
    crystal.setName(structure.firstChildElement("description").text().toUtf8().constData());
    const QDomElement group = structure.firstChildElement("spacegroup");
    if(!group.isNull()) {
        const QDomElement hall = group.firstChildElement("hall_name");
        const QDomElement iuc = group.firstChildElement("iucname");
        if(!hall.isNull()) {
            if(!crystal.setSpaceGroup(hall.attribute("name").toLatin1().constData()))
                return fail("Unknown Hall space-group name.");
        } else if(!iuc.isNull()) {
            if(!crystal.setSpaceGroupIUC(iuc.attribute("name").toLatin1().constData()))
                return fail("Unknown space-group name.");
        } else return fail("The space group has no recognized name.");
    }
    crystal.delAllAtoms();
    for(QDomElement atom=lattice.firstChildElement("atom"); !atom.isNull();
        atom=atom.nextSiblingElement("atom")) {
        double position[3];
        bool ok = false;
        const int z = atom.attribute("Z").toInt(&ok);
        if(!ok || !readAttributes(atom, {"a","b","c"}, position) ||
           !crystal.addAtom(z,position[0],position[1],position[2]))
            return fail("Invalid atom number, fractional coordinates or atom count.");
    }
    if(!crystal.spaceGroupGenerate())
        return fail("The symmetry-expanded structure exceeds QLaue's atom limit.");
    const QDomElement orientation = structure.firstChildElement("orientation");
    if(!orientation.isNull()) {
        double x[3], z[3];
        if(!readAttributes(orientation.firstChildElement("x-axis"), {"x","y","z"}, x) ||
           !readAttributes(orientation.firstChildElement("z-axis"), {"x","y","z"}, z) ||
           !crystal.setU(x[0],x[1],x[2],z[0],z[1],z[2]))
            return fail("The orientation needs two finite, nonzero, independent directions.");
    }
    // Older crystal-only files omit Laue parameters. Keep defined defaults for
    // missing optional elements, but reject invalid values that are present.
    const QDomElement laue = experimentDom.firstChildElement("laue");
    if(!laue.nextSiblingElement("laue").isNull())
        return fail("The experiment has more than one Laue configuration.");
    const QDomElement parameters = laue.firstChildElement("parameters");
    if(!optionalInteger(parameters,"precision",precision,1,11) ||
       !optionalInteger(parameters,"corrections",corrections,0,1023) ||
       !optionalInteger(parameters,"tube-Z",tubeZ,1,92) ||
       !optionalInteger(parameters,"polarization",polarization,1,3) ||
       !optionalBool(parameters,"intensities",intensities))
        return fail("Invalid diffraction calculation settings.");
    QDomElement parameter = parameters.firstChildElement("lambda-range");
    if(!parameter.isNull()) {
        double range[2];
        if(!readAttributes(parameter, {"min","max"}, range) ||
           range[0] <= 0 || range[1] < range[0] || !std::isfinite(1/range[0]) ||
           1/range[1] <= 0)
            return fail("The wavelength range must have 0 < min <= max and a finite wave vector.");
        lambda_min=range[0]; lambda_max=range[1];
    }
    parameter = parameters.firstChildElement("distance");
    if(!parameter.isNull() &&
       (!finiteNumber(parameter.attribute("d"),distance) || distance <= 0))
        return fail("The sample-detector distance must be positive.");
    parameter = parameters.firstChildElement("detector");
    if(!parameter.isNull()) {
        double detector[2];
        if(!readAttributes(parameter, {"threshold","saturation"}, detector) ||
           detector[0] < 0 || detector[0] > 1 || detector[1] < 0 || detector[1] > 1)
            return fail("Detector threshold and saturation must each be between 0 and 1.");
        det_thresh=detector[0]; det_sat=detector[1];
    }
    const QDomElement image = laue.firstChildElement("laue-image");
    if(!image.isNull()) {
        const QDomElement data = image.firstChildElement("imagedata");
        if(!optionalInteger(data,"brightness",brightness,-100,100) ||
           !optionalInteger(data,"contrast",contrast,0,100) ||
           !optionalInteger(data,"maximum",maximum,0,255) ||
           !optionalInteger(data,"minimum",minimum,0,255) ||
           !optionalBool(data,"inverted",inverted) ||
           !optionalBool(data,"rotated",rotated) ||
           !optionalBool(data,"displayed",imageDisplayed))
            return fail("Invalid image adjustment settings.");
        double geometry[3];
        if(!readAttributes(data, {"ox","oy","scale"}, geometry) || geometry[2] <= 0)
            return fail("The image needs finite origins and a positive scale.");
        if(!laueImage.loadFromData(QByteArray::fromBase64(data.text().toLatin1())))
            return fail("The experiment contains an unreadable image.");
        ox=geometry[0]; oy=geometry[1]; scale=geometry[2];
    }
    return true;
}

void DataFile::setCrystal(Crystal *crystal){
	
	QDomElement crystalDom = domDocument.createElement("crystal");
	experimentDom.appendChild(crystalDom);
	
	char *desc = crystal->getName();
	if(desc != NULL){
		QDomElement name = domDocument.createElement("description");
		QDomText text = domDocument.createTextNode(QString("%1").arg(desc));
		name.appendChild(text);
		crystalDom.appendChild(name);
	}
	
	QDomElement lattice = domDocument.createElement("lattice");
	lattice.setAttribute("a",crystal->getLatticeA());
	lattice.setAttribute("b",crystal->getLatticeB());
	lattice.setAttribute("c",crystal->getLatticeC());
	lattice.setAttribute("alpha",180 * crystal->getLatticeAlpha() / M_PI);
	lattice.setAttribute("beta",180 * crystal->getLatticeBeta() / M_PI);
	lattice.setAttribute("gamma",180 * crystal->getLatticeGamma() / M_PI);
	
	for(int i=0;i<crystal->getNRootAtoms();i++){
		QDomElement atom = domDocument.createElement("atom");
		atom.setAttribute("a",crystal->getAtom(i)->getXPos());
		atom.setAttribute("b",crystal->getAtom(i)->getYPos());
		atom.setAttribute("c",crystal->getAtom(i)->getZPos());
		atom.setAttribute("Z",crystal->getAtom(i)->getZ());
		lattice.appendChild(atom);
	}
	
    crystalDom.appendChild(lattice);
	
	QDomElement sge = domDocument.createElement("spacegroup");
	QDomElement hallname = domDocument.createElement("hall_name");
	hallname.setAttribute("name",crystal->getSpaceGroup()->HallName);
	sge.appendChild(hallname);
	crystalDom.appendChild(sge);
	
	QDomElement x,y,z;
	x = vectorToElement("x-axis", crystal->xAxis());
	y = vectorToElement("y-axis", crystal->yAxis());
	z = vectorToElement("z-axis", crystal->zAxis());
	QDomElement orientation = domDocument.createElement("orientation");
	orientation.appendChild(x);
	orientation.appendChild(y);
	orientation.appendChild(z);
	crystalDom.appendChild(orientation);
}

void DataFile::setLaue(LaueFilm *film){
	QDomElement l = domDocument.createElement("laue");
	experimentDom.appendChild(l);
	
	QDomElement parameters = domDocument.createElement("parameters");
	parameters.setAttribute("precision",film->getLaue()->calcPrecision());
	parameters.setAttribute("corrections",film->getLaue()->intensityCorrection());
	parameters.setAttribute("tube-Z",film->getLaue()->tubeZ());
	parameters.setAttribute("polarization",film->getLaue()->incidentPoln());
	parameters.setAttribute("intensities",film->getDisplayIntensities() ? 1 : 0);
	
	QDomElement d = domDocument.createElement("distance"); 
	d.setAttribute("d", film->getLaue()->getDistance());
	
	QDomElement lambda = domDocument.createElement("lambda-range");
	lambda.setAttribute("min",film->getLaue()->getLambdaRangeMin());
	lambda.setAttribute("max",film->getLaue()->getLambdaRangeMax());
	
	QDomElement detector = domDocument.createElement("detector");
	detector.setAttribute("saturation",film->getLaue()->getDetSatLevel());
	detector.setAttribute("threshold",film->getLaue()->getDetLevel());
	
	parameters.appendChild(lambda);
	parameters.appendChild(d);
	parameters.appendChild(detector);
	
	l.appendChild(parameters);
	
	if(!film->importedImage->isNull()){
		QByteArray imageOut;
		
		QBuffer buffer(&imageOut);
		buffer.open(QIODevice::WriteOnly);
		film->importedImage->save(&buffer, "PNG");
		
		QDomElement li = domDocument.createElement("laue-image");
		experimentDom.appendChild(l);
		
		QDomElement imagedata = domDocument.createElement("imagedata");
		imagedata.setAttribute("brightness",film->imageBrightness());
		imagedata.setAttribute("contrast",film->imageContrast());
		imagedata.setAttribute("maximum",film->imageMaximum());
		imagedata.setAttribute("minimum",film->imageMinimum());
		imagedata.setAttribute("inverted",film->isImageInverted() ? 1 : 0);
		imagedata.setAttribute("rotated",film->isImageRotated() ? 1 : 0);
		imagedata.setAttribute("displayed",film->isImageDisplayed() ? 1 : 0);
		
		imagedata.setAttribute("ox",film->originX());
		imagedata.setAttribute("oy",film->originY());
		imagedata.setAttribute("scale",film->scale());
		
		QDomText text = domDocument.createTextNode(imageOut.toBase64());
		imagedata.appendChild(text);
		li.appendChild(imagedata);		
		l.appendChild(li);
	}
}

/*
void DataFile::setSingleCrystDiff(SingleCrystalDiff *diff){
	QDomElement l = domDocument.createElement("single-crystal");
	experimentDom.appendChild(l);
	QDomElement parameters = domDocument.createElement("parameters");
	
	QDomElement beam = domDocument.createElement("beam");
	beam.setAttribute("lambda", diff->lambda());
	
	parameters.appendChild(beam);
	l.appendChild(parameters);
}
*/

QDomElement DataFile::vectorToElement(const QString name, const Matrix m){
	QDomElement vector = domDocument.createElement(name);
	vector.setAttribute("x",m.Get(0,0));
	vector.setAttribute("y",m.Get(1,0));
	vector.setAttribute("z",m.Get(2,0));
	return vector;
}

int DataFile::getErrorLine(void){
	return errorLine;
}

int DataFile::getErrorColumn(void){
	return errorColumn;
}

QString DataFile::getErrorStr(void){
	return errorStr;
}

bool DataFile::readCif(QString filename){
	errorColumn = 0;
	return readCifFile(filename, crystal, errorStr, errorLine);
}

bool DataFile::readCel(QString filename) {
    errorStr.clear();
    errorLine = errorColumn = 0;
    QFile file(filename);
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        errorStr = file.errorString();
        return false;
    }
    Crystal candidate;
    double cell[6] = {};
    bool haveCell=false, haveAtoms=false, haveGroup=false;
    int lineNumber=0;
    auto fail = [&](const QString &message) {
        errorStr = message;
        errorLine = lineNumber;
        return false;
    };
    auto fields = [&]() {
        ++lineNumber;
        return QString::fromUtf8(file.readLine()).split(QRegExp("\\s+"),Qt::SkipEmptyParts);
    };
    while(!file.atEnd()) {
        const QStringList list = fields();
        if(list.isEmpty() || list.first().startsWith('#')) continue;
        const QString token = list.first();
        if(token == "CELL") {
            if(haveCell || list.size() != 7) return fail("CELL requires six lattice parameters.");
            for(int i=0; i<6; ++i)
                if(!finiteNumber(list.at(i+1),cell[i])) return fail("Invalid CELL parameter.");
            for(int i=3; i<6; ++i) cell[i] *= M_PI / 180;
            if(!Crystal::isValidLattice(cell[0],cell[1],cell[2],cell[3],cell[4],cell[5]))
                return fail("The unit cell has invalid lengths, angles or volume.");
            haveCell=true;
        } else if(token == "natom") {
            bool ok=false;
            const int count = list.size()==2 ? list.at(1).toInt(&ok) : -1;
            if(haveAtoms || !ok || count < 0 || count > Crystal::MaxAtoms)
                return fail("Invalid natom count (supported range: 0 to 2048).");
            candidate.delAllAtoms();
            for(int i=0; i<count; ++i) {
                if(file.atEnd()) return fail("The atom table ended before all atoms were read.");
                const QStringList atom = fields();
                if(atom.size() < 5) return fail("Each atom requires a label, atomic number and x, y, z.");
                const int z = atom.at(1).toInt(&ok);
                double xyz[3];
                if(!ok) return fail("Invalid atomic number.");
                for(int c=0; c<3; ++c)
                    if(!finiteNumber(atom.at(c+2),xyz[c])) return fail("Invalid atom coordinate.");
                if(!candidate.addAtom(z,xyz[0],xyz[1],xyz[2]))
                    return fail("Invalid atom number, fractional coordinates or atom count.");
            }
            haveAtoms=true;
        } else if(token == "rgnr") {
            bool ok=false;
            const int group = list.size()==2 ? list.at(1).toInt(&ok) : 0;
            if(haveGroup || !ok || !candidate.setSpaceGroupNum(group))
                return fail("Invalid space-group number.");
            haveGroup=true;
        }
        // CEL producers may add metadata directives not used by QLaue.
    }
    if(file.error() != QFile::NoError) return fail(file.errorString());
    if(!haveCell || !haveAtoms) return fail("CEL requires CELL and natom records.");
    candidate.setLattice(cell[0],cell[1],cell[2],cell[3],cell[4],cell[5]);
    if(!candidate.spaceGroupGenerate())
        return fail("The symmetry-expanded structure exceeds QLaue's atom limit.");
    crystal = candidate;
    return true;
}
