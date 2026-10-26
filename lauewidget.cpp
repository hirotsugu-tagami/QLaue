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
#include <QtGui>
#include <QtWidgets>
#include <QScopedValueRollback>
#include <algorithm>
#include <cmath>

#include "matrix.h"
#include "version.h"
#include "laue.h"
#include "crystal.h"
#include "lauewidget.h"
#include "htmlgenerators.h"
#include "filmsizedialog.h"
#include "indexingdialog.h"
#include "setubmat.h"
#include "adjustimage.h"

#ifdef CALC_IN_THREAD

LaueIndexingThread::LaueIndexingThread(QObject *parent) : QThread(parent), lauegram() {}

LaueIndexingThread::~LaueIndexingThread() {
    requestInterruption();
    wait();
}

bool LaueIndexingThread::indexLaue(const Crystal &crystal, const QVector<QPointF> &points) {
    if(isRunning()) return false;
    wait();
    newOrient = crystal;
    indexSpots = points;
    indexingResult = LaueIndexingResult();
    start(LowPriority);
    return true;
}

void LaueIndexingThread::run() {
    int lastPercent = -1;
    indexingResult = lauegram::indexLaue(newOrient,indexSpots,[&](int percent) {
        if(percent != lastPercent) {
            emit progress(percent);
            lastPercent = percent;
        }
        return isInterruptionRequested();
    });
}

LaueThread::LaueThread(QObject *parent) : QThread(parent), lauegram() {
    connect(this, &QThread::finished, this, &LaueThread::finishCalculation,
            Qt::QueuedConnection);
}

LaueThread::~LaueThread() {
    requestInterruption();
    wait();
}

void LaueThread::run() {
    int lastPercent = -1;
    activeComplete = activeCalculation.calc(&activeCrystal, [&](int percent) {
        if(percent != lastPercent) {
            emit spotsToGo(percent);
            lastPercent = percent;
        }
        return isInterruptionRequested();
    });
}

void LaueThread::startCalculation(Crystal *c) {
    if(!c) return;
    pendingCrystal = *c;
    pendingCalculation = static_cast<const lauegram &>(*this);
    hasPending = true;
    if(calculationActive) requestInterruption();
    else launchPending();
}

void LaueThread::launchPending() {
    activeCrystal = pendingCrystal;
    activeCalculation = pendingCalculation;
    hasPending = false;
    activeComplete = false;
    calculationActive = true;
    emit calcStarted();
    start(NormalPriority);
}

void LaueThread::finishCalculation() {
    // finished() may be emitted before all native thread cleanup is complete.
    wait();
    calculationActive = false;
    if(hasPending) {
        launchPending();
        return;
    }
    if(activeComplete) {
        copySpotsFrom(activeCalculation);
    } else {
        setNspots(0);
        emit calculationFailed(tr("The diffraction calculation could not finish. Check the lattice, wavelength range and accuracy settings."));
    }
    emit calculated();
}


#else

LaueThread::LaueThread(QObject *parent) :
QObject(parent), lauegram()
{
	return;
}

LaueThread::~LaueThread() {
	return;
}

void LaueThread::startCalculation(Crystal *c){
	calc(c);
	emit calculated();
}

#endif 


LaueFilm::LaueFilm(QWidget *parent, Crystal* c)
: QWidget(parent)
{	
	
	setMinimumSize(QSize(300,500));
	importedImage = new QImage;
	importedAdjustedImage = new QImage;
	importedScaledImage = new QImage;
	importedImageDX = 0.0;
	importedImageDY = 0.0;
	
	// Initialize arrays
	
	orientationSpotsX = new double[lauegram::MaxIndexingSpots];
	orientationSpotsY = new double[lauegram::MaxIndexingSpots];
	
	// Setup laue class
	
	laue = new LaueThread;
	laueIndexing = new LaueIndexingThread;
	
	crystal = c;
	
	// Connect thread finished
	connect(laue, SIGNAL(calculated()), this, SLOT(displayAfterCalc()));
	connect(laue, SIGNAL(calculationFailed(QString)), this, SLOT(displayMessage(QString)));
	connect(laue, SIGNAL(partialCalc()), this, SLOT(displayAfterCalc()));
	
	laue->setDebug(false);
	laue->setDistance(35.0);
	laue->setLambdaRange(0.3,3);
	
	reCalc();
	
	// Define the origin of the film
	
	laueOrigin = QPointF(0,0);
	origin_circle = 0.5;

	pixels_per_mm = 1000.0 / 172; // This is approximately 172 micron pixel size for PSL (2 x 2 binning)
	rubberBand = 0;
	
	max_spot_size = 50;
	
	num_hkl_labels = 1;
	mouseover_hkl_label = 1;
	
	numOrientationSpots = 0;
	numUBOrientationSpots = 0;
	
	display = LaueFilm::DisplayLaue;
	display |= LaueFilm::DisplayIntensities;
	display |= LaueFilm::DisplayOrigin;
	display |= LaueFilm::DisplayLabels;
	display |= LaueFilm::DisplayCharacteristic;
	
	userAction = LaueFilm::UANone;
	userActionStage = 0;
	
	laueMessage = QString("");
	
	setMouseTracking(true);
	setFocusPolicy(Qt::StrongFocus);
	
}

LaueFilm::~LaueFilm(){
	delete importedImage;
	delete importedScaledImage;
	delete importedAdjustedImage;
	delete laueIndexing;
	delete laue;
	delete [] orientationSpotsX;
	delete [] orientationSpotsY;
}

bool LaueFilm::getDisplayLaue(void){
	return getBit(display,LaueFilm::DisplayLaue);
}

void LaueFilm::setDisplayLaue(int yesno){
	setBit(&display, (int)LaueFilm::DisplayLaue, yesno);
	update();
}

bool LaueFilm::getDisplayIntensities(void){
	return getBit(display,LaueFilm::DisplayIntensities);
}

void LaueFilm::setDisplayIntensities(int yesno){
	setBit(&display, LaueFilm::DisplayIntensities, yesno);
	getLaue()->setCalcIntensities(yesno);
	reCalc();
	update();
}

void LaueFilm::setDisplayIntensitiesAsColor(bool yesno){ 
	setBit(&display, (int)LaueFilm::DisplayIntensitiesColor,yesno);
	update();
}

bool LaueFilm::getDisplayIntensitiesAsColor(void){ 
	return getBit(display,LaueFilm::DisplayIntensitiesColor);
}

void LaueFilm::setDisplayCharacteristicInt(int yesno){ 
	setBit(&display, (int)LaueFilm::DisplayCharacteristic,yesno); 
	update();
}

bool LaueFilm::getDisplayCharacteristicInt(void){
	return getBit(display, LaueFilm::DisplayCharacteristic);
}

void LaueFilm::setInvertImage(bool yesno){ 
	setBit(&display, (int)LaueFilm::DisplayImageInverted, yesno);
	setAdjustedImage();
	update();
}

void LaueFilm::setDisplayImage(bool yesno){ 
	setBit(&display, LaueFilm::DisplayImage, yesno); 
	setAdjustedImage();
	update();
}

void LaueFilm::setRotateImage(bool yesno){
    const bool changed = getBit(display, LaueFilm::DisplayImageRotated) != yesno;
    if(changed) clearOrientation();
    setBit(&display, LaueFilm::DisplayImageRotated, yesno);
    setAdjustedImage();
    if(changed) imageZoomRect = importedAdjustedImage->rect();
    update();
}

void LaueFilm::showLabels(bool flag){ 
	setBit(&display, LaueFilm::DisplayLabels, flag);
	update();
}

void LaueFilm::paintEvent(QPaintEvent *) {
	
	QPainter painter(this);
	
	laueRect = QRect(10,50,width() - 20, height() - 60);
	messageRect = QRect(10,10,width() - 20, 30);
	
	paintDummyFilm(&painter, laueRect, messageRect);
	
	paintMsgBox(&painter, messageRect);
	paintLaueBox(&painter, laueRect);
	
	prepareImage(laueRect);
	if(getBit(display,LaueFilm::DisplayImage))
		paintImage(&painter, laueRect);
	
	paintLaueAxes(&painter, laueRect);
	
	if(getBit(display,LaueFilm::DisplayLaue) &&
	   (userAction & (UASetOrigin|UAMeasureScale|UASelectOrientation)) == 0)
		paintLaue(&painter, laueRect);
	
	if(getBit(userAction,UASetOrigin) || (getBit(display,DisplayOrigin) && !getBit(userAction,UAMeasureScale)))
		paintOrigin(&painter, laueRect, true);
		
	if(getBit(display, LaueFilm::DisplayIndexing))
		paintIndexing(&painter, laueRect);
	
	if(getBit(display,LaueFilm::DisplayUBIndexing))
		paintUBOrientationSpots(&painter, laueRect);
	
	if(getBit(display,DisplayRotateAboutCross))
		paintRotateAboutCross(&painter, laueRect);
}

void LaueFilm::print(QPrinter *printer, QPainter *painter, int mode){
	QRect laueR;
	QRectF text;
	QDateTime now = QDateTime::currentDateTime();
	
	bool isLandscape = printer->width() > printer->height();
	
	if(isLandscape){
		laueR = QRect(10,10,(printer->width() / 2) - 20, printer->height() - 20);
		text = QRectF((printer->width() / 2) + 10, 10, (printer->width() / 2) - 20, printer->height() - 20);
	} else {
		laueR = QRect(10,10,printer->width() - 20, (printer->height() / 2) - 20);
		text = QRectF(10,(printer->height() / 2) + 10, printer->width() - 20, (printer->height()/2) - 20);
	}

	// Coordinate conversion must use the same viewport as the printed image.
	// Restore the interactive image geometry when printing is finished.
	QScopedValueRollback<QRect> printRect(laueRect, laueR);
	QScopedValueRollback<QImage> screenImage(*importedScaledImage);
	QScopedValueRollback<QPoint> screenImagePos(importedImagePos);
	QScopedValueRollback<QPoint> screenImageCenter(importedImageCenter);
	
	paintLaueBox(painter, laueR);
	prepareImage(laueR);
	if(getBit(display,LaueFilm::DisplayImage))
		paintImage(painter, laueR);
	paintLaueAxes(painter, laueR);
	if(getBit(display,LaueFilm::DisplayLaue))
		paintLaue(painter,laueR);
	paintOrigin(painter, laueR);

	// Draw Bounding Rectangle
	
	QRectF innertext = QRectF(0,0,
							  text.width() - (text.width() / 10),
							  text.height() - (text.width() / 10));
	
	painter->setPen(QColor(0,0,0));
	painter->setBrush(QColor(255,255,255));
	painter->drawRect(text);
	painter->resetMatrix();
	painter->translate(text.topLeft() + QPoint(text.width() / 20.0, text.width() / 20.0));
	
	QTextDocument textbox;
	textbox.documentLayout()->setPaintDevice(printer);
	textbox.setPageSize(innertext.size());
	QFont font = QApplication::font(); 
	font.setPointSize(8);
	textbox.setDefaultFont(font);
	
	QString html;
	html = "<html><body>";
	html += HtmlGenerators::appHeader();
	html += QString("<b>Printed on : %1</b><br>&nbsp;<br>")
		.arg(now.toString("dddd d MMMM yyyy hh:mm"));
 	html += HtmlGenerators::crystalToHtml(crystal, HtmlGenerators::CrystalBasic);
	html += HtmlGenerators::laueToHtml(laue);
	html += "<b>Crystal Orientation:</b><br>";
	html +=  HtmlGenerators::crystalOrientationToHtml(crystal);
	html += "&nbsp;<br>";
	if(mode == PrintReorientation){
		html += HtmlGenerators::crystalOrientationToHtml(&laue->reorientation);
	}
	html += "</body></html>";
	textbox.setHtml(html);
	
	QAbstractTextDocumentLayout::PaintContext ctx;
	ctx.clip = QRectF(0,0,innertext.width(), innertext.height());
	//painter->drawRect(ctx.clip);
	textbox.documentLayout()->draw(painter, ctx);
}

void LaueFilm::paintRotateAboutCross(QPainter *painter, QRect size){
	QPoint cross = worldToPixels(rotateAboutAxisPoint);
	int spot_size = (int)(max_spot_size * ((double)size.height() / 2000));
	
	painter->setBrush(Qt::NoBrush);
	painter->setPen(QColor("darkGreen"));
	
	QRect crossRect(0,0,spot_size,spot_size);
	QRect circleRect(0,0,spot_size / 2,spot_size / 2);

	crossRect.moveCenter(cross);
	circleRect.moveCenter(cross);
		
	if(size.contains(crossRect,true)) {
		painter->drawLine(crossRect.topLeft(), crossRect.bottomRight());
		painter->drawLine(crossRect.topRight(), crossRect.bottomLeft());
		painter->drawEllipse(circleRect);
	}
}

void LaueFilm::paintDummyFilm(QPainter *painter, QRect laue, QRect msg){
	painter->setPen(QColor(0,0,0));
	painter->setBrush(QColor(255,255,255));
	
	painter->drawRect(laue);
	painter->drawRect(msg);
}

void LaueFilm::paintMsgBox(QPainter *painter, QRect msg){
	
	painter->setRenderHint(QPainter::Antialiasing);
	QColor hourColor(255,0,0);
	
	painter->setPen(QColor(0,0,0));
	painter->setBrush(QColor(255,255,255));
	painter->drawRect(msg);
	
	// Draw Text for message area
	
	painter->drawText(msg, Qt::AlignCenter, laueMessage);
}

void LaueFilm::paintLaueBox(QPainter *painter, QRect laue){
	// Draw border polygon
	
	painter->setPen(QColor(0,0,0));
	painter->setBrush(QColor(255,255,255));
	painter->drawRect(laue);	
}

void LaueFilm::paintLaueAxes(QPainter *painter, QRect laue){
	
	painter->setRenderHint(QPainter::Antialiasing);
	QColor hourColor(255,0,0);
	
	// Draw Coordinate axes
	
	painter->setBrush(QColor(0,0,0));
	painter->setPen(QColor(0,0,0));
	
	int pad = laue.height() / 24;
	int length = laue.width() / 6;
	
	QPointF points[3] = {
        QPointF(0.0, 0.0),
        QPointF(pad / 4, pad / 4),
        QPointF(pad / -4, pad / 4)
    };
	
	painter->resetMatrix();
    painter->translate(pad,laue.bottom() - pad);
	
	painter->drawLine(0,0,0,-1 * length);
	painter->drawLine(0,0,length,0);
	painter->drawText(0,-1 * length,tr("Z"));
	painter->drawText(length, 0,tr("Y"));
	painter->translate(0,-1 * length);
	painter->drawPolygon(points, 3);
	
	painter->translate(length,length);
	painter->rotate(90);
	painter->drawPolygon(points, 3);
	
	painter->resetMatrix();
	painter->translate(laue.right() - pad,laue.bottom() - pad);
	
	double world_length = (pixelsToWorld(QPoint(2 * length,0)) - pixelsToWorld(QPoint(0,0))).x();
	QString mmlen = QString("%1 mm").arg(world_length, 5, 'g', 2);
	
	painter->drawLine(-2 * length,0,0,0);
	painter->drawLine(0,0,0,-1 * pad / 2);
	painter->drawLine(-2 * length,0,-2 * length,-1 * pad / 2);
	painter->drawText(-2 * length,0,2*length,-1 * pad,Qt::AlignCenter,
					  mmlen);
}

void LaueFilm::paintOrigin(QPainter *painter, QRect size, bool preview){
	
	painter->setBrush(QColor(0,0,255));
	painter->resetMatrix();
		
	QPoint oxy = worldToPixels(preview && getBit(userAction,UASetOrigin) ? originPreview-laueOrigin : QPointF(0,0));
	painter->setPen(QColor(0,0,255));
	painter->setBrush(Qt::NoBrush);
	painter->translate(oxy);
	painter->rotate(45);
	QRect centrect1 = QRect(0,0,size.height() * origin_circle / 10,size.height() * origin_circle / 10);
	QRect centrect2 = QRect(0,0,size.height() / 10,size.height() / 10);
	
	centrect1.moveCenter(QPoint(0,0));
	centrect2.moveCenter(QPoint(0,0));
	painter->drawLine(centrect2.topLeft(), centrect2.bottomRight());
	painter->drawLine(centrect2.topRight(), centrect2.bottomLeft());
	painter->drawEllipse(centrect1);
	
	painter->resetMatrix();
}

void LaueFilm::prepareImage(QRect size) {
	// Coordinate geometry belongs to the viewport, even when the bitmap is hidden.
	if(importedAdjustedImage->isNull()) {
		*importedScaledImage = QImage();
		importedImagePos = importedImageCenter = size.center();
		return;
	}
	QImage newimage = importedAdjustedImage->copy(imageZoomRect);
	newimage = newimage.scaled(size.size(), Qt::KeepAspectRatio);
	*importedScaledImage = newimage;
	
	int xpad = (size.width() - newimage.width()) / 2;
	int ypad = (size.height() - newimage.height()) / 2;
	
	QRect paintsize;
	if(ypad == 0){
		paintsize = QRect(size.translated(xpad,0).topLeft(),
										size.translated(-1 * xpad, 0).bottomRight());
	} else {	
		paintsize = QRect(size.translated(0, ypad).topLeft(), 
										size.translated(0, -1 * ypad).bottomRight());
	}
	
	importedImagePos = paintsize.topLeft();
	importedImageCenter = paintsize.center();
}

void LaueFilm::paintImage(QPainter *painter, QRect size)
{
	painter->resetMatrix();
	painter->setClipRect(size,Qt::ReplaceClip);
	const QImage &newimage = *importedScaledImage;
	painter->drawImage(importedImagePos, newimage);
	
	if(getBit(userAction, UASelectOrientation) || getBit(userAction, UASelectUBOrientation)){
		if(QRect(importedImagePos.x(), importedImagePos.y(), newimage.width(),newimage.height()).contains(zoomImagePos)){
			QRect cutSize = QRect(0,0,15,15);
			cutSize.moveCenter(zoomImagePos - importedImagePos);
			QImage zoomImage = newimage.copy(cutSize);
			
			// Draw the image
	
			painter->drawImage(size.topRight() - QPoint(110,-10), zoomImage.scaledToHeight(100,Qt::SmoothTransformation));
			
			// Now draw some crosshairs
			
			painter->setPen("Red");
			
			painter->drawLine(size.topRight() - QPoint(60,-20),
							  size.topRight() - QPoint(60,-100));
			painter->drawLine(size.topRight() - QPoint(100,-60),
							  size.topRight() - QPoint(20,-60));
		}
	}
	
	painter->setClipping(false);
	painter->setPen("Black");
	painter->setBrush(Qt::NoBrush);
	painter->drawRect(size);
}

void LaueFilm::paintIndexing(QPainter *painter, QRect size){
	// Draw orientation spots
	
	painter->setBrush(Qt::NoBrush);
	painter->setPen(QColor("darkMagenta"));
	
	for(int i=0 ; i < numOrientationSpots ; i++){
		int spot_size;
		spot_size = (int)(max_spot_size * ((double)size.height() / 2000));
		
		QRect spot(0,0,spot_size,spot_size);
		
		QPoint spotpos = worldToPixels(QPointF(orientationSpotsX[i], orientationSpotsY[i])); 
		
		spot.moveCenter(spotpos);
		
		if(size.contains(spot,true)){
			painter->drawEllipse(spot);
			painter->drawText(spot.bottomRight() + QPoint(0,spot.width() / 2) ,QString("%1").arg(i+1));
		}
	}
}

void LaueFilm::paintUBOrientationSpots(QPainter *painter, QRect size){
	// Draw orientation spots
	
	painter->setBrush(Qt::NoBrush);
	painter->setPen(QColor("darkMagenta"));
	
	for(int i=0 ; i < numUBOrientationSpots ; i++){
		int spot_size;
		spot_size = (int)(max_spot_size * ((double)size.height() / 2000));
		
		QRect spot(0,0,spot_size,spot_size);
		
		QPoint spotpos = worldToPixels(QPointF(UBOrientationSpotsX[i], UBOrientationSpotsY[i])); 
		
		spot.moveCenter(spotpos);
		
		if(size.contains(spot,true)){
			painter->drawEllipse(spot);
			if(i == 0)
				painter->drawText(spot.bottomRight() + QPoint(0,spot.width() / 2) ,QString("P"));
			else
				painter->drawText(spot.bottomRight() + QPoint(0,spot.width() / 2) ,QString("S"));
		}
	}
}

void LaueFilm::paintLaue(QPainter *painter, QRect size) {

	painter->setBrush(QColor(0,0,0));
	painter->setPen(QColor(0,0,0));
	painter->resetMatrix();
	QPoint cent = size.center();
	
	// Calculate the screensize
	
	if(getBit(display, LaueFilm::DisplayIntensities)){
		QPointF tl = pixelsToWorld(size.topLeft());
		QPointF br = pixelsToWorld(size.bottomRight());
		//qDebug() << "Screensize = " << tl << " " << br;
		laue->calcInten(tl.x(), br.x(), br.y(), tl.y());
	}
	
	QColor bremsColor(255,0,0,127);
	QColor charColor("darkBlue");
	
	// Set font for labels
	
	QFont font = QApplication::font(); 
	font.setPointSize(10);
	painter->setFont(font);
	int fontpad = painter->boundingRect(size,0,tr("(0 0 0)")).height() / 2;
	
	for(int i=0 ; i < laue->getNspots() ; i++){
		int spot_size;
		
		if(getBit(display, LaueFilm::DisplayIntensities)){
			if(!getBit(display, LaueFilm::DisplayIntensitiesColor)){
				spot_size = (int)(max_spot_size * ((double)size.height() / 4000) * sqrt(laue->getSpot(i)->detI()));
			} else {
				spot_size = (int)(max_spot_size * ((double)size.height() / 4000));
				int clevel = (int)((1.0 - laue->getSpot(i)->detI()) * 255);
				bremsColor = QColor(clevel,clevel,clevel);
			}
		} else {
			spot_size = (int)(max_spot_size * ((double)size.height() / 4000));
			bremsColor = QColor(255,0,0);
		}
		
		QRect spot(0,0,spot_size,spot_size);
		
		if(laue->getSpot(i)->characteristic() && getBit(display, LaueFilm::DisplayCharacteristic)){
			painter->setBrush(charColor);
			painter->setPen(charColor);
		} else {
			painter->setBrush(bremsColor);
			painter->setPen(bremsColor);
		}
		
		// draw the spots!
		QPoint spotpos = worldToPixels(QPointF(laue->getSpot(i)->X(), laue->getSpot(i)->Y())); 
		
		spot.moveCenter(spotpos);
		
		if(size.contains(spot,true)){
			painter->setPen(Qt::NoPen);
			painter->drawEllipse(spot);
		
			painter->setBrush(bremsColor);
			painter->setPen(QColor("blue"));

			if(getBit(display, LaueFilm::DisplayLabels)){
				int hkl = abs(laue->getSpot(i)->H()) + abs(laue->getSpot(i)->K()) + abs(laue->getSpot(i)->L());
				if(hkl <= 3){
					painter->drawText(spot.topRight() + QPoint(spot_size,fontpad), 
									  QString("(%1 %2 %3)")
									  .arg(laue->getSpot(i)->H())
									  .arg(laue->getSpot(i)->K())
									  .arg(laue->getSpot(i)->L()));
				}
				
				for(int j=mouseover_hkl_label;j<num_hkl_labels;j++){
					if((laue->getSpot(i)->H() == hkl_labels[j][0]) &&
					   (laue->getSpot(i)->K() == hkl_labels[j][1]) &&
					   (laue->getSpot(i)->L() == hkl_labels[j][2])){
						painter->drawText(spot.topRight() + QPoint(spot_size,fontpad), 
										  QString("(%1 %2 %3)")
										  .arg(laue->getSpot(i)->H())
										  .arg(laue->getSpot(i)->K())
										  .arg(laue->getSpot(i)->L()));
					}
				}
			}
		}
	}
}

void LaueFilm::setU(double h1, double k1, double l1,
					double h2, double k2, double l2){
	crystal->setU(h1,k1,l1,h2,k2,l2);
	reCalc();
	emit rotationsChanged(0,0,0);
}

void LaueFilm::setU(Matrix U){
	crystal->setU(U);
	reCalc();
	emit rotationsChanged(0,0,0);
}

void LaueFilm::setFSDistance(double d){
	const double previous = laue->getDistance();
	laue->setDistance(d);
	if(laue->getDistance() != previous) clearOrientationResults();
	reCalc();
}

void LaueFilm::setZoom(double zoom){
	update();
}

void LaueFilm::mousePressEvent(QMouseEvent *event){

	if(getBit(userAction,LaueFilm::UASelectOrientation)) {
		
		if(laueRect.contains(event->pos())){
            if(numOrientationSpots >= lauegram::MaxIndexingSpots) {
                displayMessage(tr("Maximum %1 spots selected. Press Enter to start indexing.").arg(lauegram::MaxIndexingSpots));
                return;
            }
			QPointF spot = pixelsToWorld(event->pos());
            for(int i = 0; i < numOrientationSpots; ++i)
                if(QLineF(spot,QPointF(orientationSpotsX[i],orientationSpotsY[i])).length() < 1e-8) return;
			orientationSpotsX[numOrientationSpots] = (double)spot.x();
			orientationSpotsY[numOrientationSpots] = (double)spot.y();
			numOrientationSpots++;
            displayMessage(tr("%1 spots selected; Enter to solve.").arg(numOrientationSpots));
			update();
		}
		
		event->accept();
		return;
	}
	
	if(getBit(userAction, LaueFilm::UASelectUBOrientation)) {
		if(laueRect.contains(event->pos())){
			QPointF spot = pixelsToWorld(event->pos());
			UBOrientationSpotsX[numUBOrientationSpots] = (double)spot.x();
			UBOrientationSpotsY[numUBOrientationSpots] = (double)spot.y();
			numUBOrientationSpots++;
			update();
		}
		
		if(numUBOrientationSpots == 2){
			finishTool(true);
			resetOrientation();
		}
		
		event->accept();
		return;
	}

	if(getBit(userAction,LaueFilm::UAMeasureZoom)){
		setMouseTracking(true);
		rubberBandOrigin = event->pos(); // origin is a QPoint
        if(rubberBand)
			delete rubberBand;
		rubberBand = new QRubberBand(QRubberBand::Rectangle, this);
        rubberBand->setGeometry(QRect(rubberBandOrigin, QSize()));
        rubberBand->show();
		userActionStage = 2;
		return;
	}
	
	if(getBit(userAction,LaueFilm::UASetOrigin)){
		// We were asked to define the origin 
		if(!laueRect.contains(event->pos())) { event->accept(); return; }
		double isf = imageScaleFactor();
		QPoint pixel = event->pos() - laueRect.center();
		const QPointF origin(pixel.x() * isf / pixels_per_mm, -1 * pixel.y() * isf / pixels_per_mm);
		finishTool(true);
		if(origin != laueOrigin) {
			laueOrigin = origin;
			clearOrientation();
		}
		event->accept();
		return;
	}
	
	if(getBit(userAction,LaueFilm::UAMeasureScale)){
		setMouseTracking(true);
		rubberBandOrigin = event->pos(); // origin is a QPoint
		if(rubberBand)
			delete rubberBand;	
		rubberBand = new QRubberBand(QRubberBand::Rectangle, this);
		rubberBand->setGeometry(QRect(rubberBandOrigin, QSize()));
		rubberBand->show();
		userActionStage = 2;
		return;
	}
	
	if(getBit(userAction, UARotateAboutAxis)){
		if(userActionStage == 1){
			userActionStage = 2;
			displayMessage(QString("Use mouse to rotate about axis"));
			rotateAboutAxisPoint = pixelsToWorld(event->pos());
			setBit(&display, DisplayRotateAboutCross, true);
			setCursor(Qt::ArrowCursor);
			update();
		} else if(userActionStage == 2){
			userActionStage = 3;
			moveStart = event->pos();
		}
		event->accept();
		return;
	}
	
	if(event->modifiers() == Qt::ShiftModifier){
		QPointF spot = pixelsToWorld(event->pos());
		int snum = laue->findSpot(spot.x(),spot.y(),1);

		if(snum != -1){
			addIndexLabel(laue->getSpot(snum)->H(), laue->getSpot(snum)->K(), laue->getSpot(snum)->L());
			update();
		}
		event->accept();
		return;
	}
	
	moveStart = event->pos();
	
	if(event->button() == Qt::RightButton){
		setBit(&userAction, UARotateX, true);
	} else if(event->button() == Qt::LeftButton){
		setBit(&userAction, UARotateYZ, true);
	} else {
		userAction = UANone;
	}
		
	//qDebug("LaueFilm::mousePressEvent() : userAction = %d",userAction);
	event->accept();
	return;
}

void LaueFilm::mouseReleaseEvent(QMouseEvent *event){

	if(getBit(userAction, UARotateX)) {
		userAction = UANone;
		event->accept();
		return;
	}
	
	if(getBit(userAction, UARotateYZ)) {
		userAction = UANone;
		event->accept();
		return;
	} 

	if(getBit(userAction, UARotateAboutAxis) && (userActionStage == 3)){
		finishTool(true);
		event->accept();
		return;
	}

	if(getBit(userAction,LaueFilm::UAMeasureScale)){
		const bool measured = rubberBand && userActionStage == 2;
		const double width = std::abs(double(event->pos().x()) - rubberBandOrigin.x());
		finishTool(false);
		QString error;
		if(!measured || width == 0) {
			error = tr("Select a nonzero horizontal width to calibrate the image.");
		} else {
			FilmSizeDialog dialog(this);
			dialog.setWindowTitle(tr("Measured width (millimetres)"));
			if(dialog.exec() == QDialog::Accepted){
				const double reallength = dialog.getFilmWidth();
				const double measuredScale = reallength > 0 ? width * imageScaleFactor() / reallength : 0;
				if(std::isfinite(reallength) && reallength > 0 &&
				   std::isfinite(measuredScale) && measuredScale > 0) {
					calibrateScale(measuredScale);
				} else {
					error = tr("The measured width must be a finite, positive number of millimetres. Scale unchanged.");
				}
			}
		}
		
		if(!error.isEmpty()) displayMessage(error);
		event->accept();
		return;
	}
	
	if(getBit(userAction, LaueFilm::UAMeasureZoom)){
		userAction = LaueFilm::UANone;
		
		rubberBand->hide();
		imageZoomRect = QRect(QPoint(rubberBandOrigin - importedImagePos), 
							  QPoint(event->pos() - importedImagePos));
		
		imageZoomRect = imageZoomRect.intersected(importedScaledImage->rect());
		
		// imageZoomRect is now the rect inside the scaled image.
		// this rect should be a pure subset of the importedAdjustedImage.
		
		QSize newsize = imageZoomRect.size() * imageScaleFactor();
		imageZoomRect.setX(imageZoomRect.x() * imageScaleFactor());
		imageZoomRect.setY(imageZoomRect.y() * imageScaleFactor());
		imageZoomRect.setSize(newsize);
		
		update();
		event->accept();
	}
}

void LaueFilm::setPixelsPerMMFromImage(void){
	finishTool(false);
	const double imageScale = importedImage->dotsPerMeterX() / 1000.0;
	if(importedImage->isNull() || imageScale <= 0) {
		displayMessage(tr("The image has no valid pixel-size metadata. Scale unchanged."));
		return;
	}
	calibrateScale(imageScale);
}

namespace {
double dragRotation(const QPointF &start, const QPointF &stop) {
	const double a = std::hypot(start.x(),start.y()), b = std::hypot(stop.x(),stop.y());
	if(!std::isfinite(a) || !std::isfinite(b) || a == 0 || b == 0) return 0;
	const QPointF u = start/a, v = stop/b;
	return -std::atan2(u.x()*v.y()-u.y()*v.x(),u.x()*v.x()+u.y()*v.y());
}
}

void LaueFilm::mouseMoveEvent(QMouseEvent *event){
	
	//qDebug("LaueFilm::mouseMoveEvent() : userAction = %d",userAction);
	
	if(	getBit(userAction,LaueFilm::UAMeasureZoom) || 
		getBit(userAction,LaueFilm::UAMeasureScale)){
		
		if(rubberBand && userActionStage == 2)
			rubberBand->setGeometry(QRect(rubberBandOrigin, event->pos()).normalized());
		event->accept();
		return;
	}
	
	if(getBit(userAction,LaueFilm::UASetOrigin)) {
		if(laueRect.contains(event->pos())){
			double isf = imageScaleFactor();
			QPoint pixel = event->pos() - laueRect.center();
			originPreview = QPointF(pixel.x() * isf / pixels_per_mm, -1 * pixel.y() * isf /  pixels_per_mm);
			update();
			event->accept();
			return;
		}
	}
	
	if(getBit(userAction, UARotateYZ)){
		
		QPointF moveYZ = pixelsToWorld(event->pos()) - pixelsToWorld(moveStart);
		
		double angley = -1.0 * atan(moveYZ.y() / laue->getDistance()) / 2;
		double anglex = -1.0 * atan(moveYZ.x() / laue->getDistance()) / 2;
		
		emit rotationsChanged(0, angley, anglex);
		
		moveStart = event->pos();
		
	} else if(getBit(userAction, UARotateX)){
		
		QPointF stopX = pixelsToWorld(event->pos());
		QPointF startX = pixelsToWorld(moveStart);
		
		const double angle = dragRotation(startX,stopX);
		if(angle != 0) emit rotationsChanged(angle, 0.0, 0.0);
		moveStart = event->pos();
		
	} else if (getBit(userAction, UARotateAboutAxis)) {
		if(userActionStage == 3){
			QPointF stopX = pixelsToWorld(event->pos()) - rotateAboutAxisPoint;
			QPointF startX = pixelsToWorld(moveStart) - rotateAboutAxisPoint;
			
			const double angle = dragRotation(startX,stopX);
			if(angle != 0) {
				Matrix pri = laue->filmposToVector(rotateAboutAxisPoint.x(),rotateAboutAxisPoint.y());
				crystal->rotateAboutBy(pri, angle);
				emit rotationsChanged(0.0 ,0.0 , 0.0);
			}
			moveStart = event->pos();
			update();
		}
		event->accept();
		return;
	} else if(getBit(userAction, UASelectOrientation) || getBit(userAction, UASelectUBOrientation)) {
		setCursor(Qt::CrossCursor);
		// If we are displaying an image then cut out 50 pixels square
		// and send this pixmap as a signal.
		zoomImagePos = event->pos();	
		repaint();
	} else {
		// Is the mouse inside the film ?
		
		if(laueRect.contains(event->pos())){
		
			QPointF spot = pixelsToWorld(event->pos());
			
			int snum = laue->findSpot(spot.x(),spot.y(),1);
		
			if(snum == -1){
				QString msg = QString("X = %1 mm  Y = %2 mm")
								.arg(spot.x(),4,'f',2)
								.arg(spot.y(),4,'f',2);
				displayMessage(msg);
				setCursor(Qt::CrossCursor);
				mouseover_hkl_label = 1;
				update();
			} else {
				// Spot found
				QString msg = QString("Spot Index (%1, %2, %3)    Multiplicity %4    Intensity %5")
								.arg(laue->getSpot(snum)->H())
								.arg(laue->getSpot(snum)->K())
								.arg(laue->getSpot(snum)->L())
								.arg(laue->getSpot(snum)->getMaxdo())
								.arg(laue->getSpot(snum)->detI(), 3, 'f', 2);
				displayMessage(msg);
				setCursor(Qt::CrossCursor);
				mouseover_hkl_label = 0; // Add this spot now to the hkl_labels
				hkl_labels[0][0] = laue->getSpot(snum)->H();
				hkl_labels[0][1] = laue->getSpot(snum)->K();
				hkl_labels[0][2] = laue->getSpot(snum)->L();
				
				//cerr << getCrystal()->getU() * laue->filmposToVector(spot.x(), spot.y());
				update();
			}
		} else {
			setCursor(Qt::ArrowCursor);
		}
	}
}

void LaueFilm::keyPressEvent(QKeyEvent *event){
    if(getBit(userAction,UASelectOrientation)) {
        if(event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete) {
            if(numOrientationSpots > 0) --numOrientationSpots;
            displayMessage(tr("%1 spots selected; Enter to solve.").arg(numOrientationSpots));
            update();
            event->accept();
            return;
        }
        if(event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            finishTool(true);
            resolveOrientation();
            event->accept();
            return;
        }
    }
	if(event->key() == Qt::Key_Escape){
		if(toolActive && !getBit(userAction,UASelectOrientation)) {
			finishTool(false);
			event->accept();
			return;
		}
		
		if(getBit(userAction, UASelectOrientation)){
			finishTool(true);
			displayMessage(QString("Calculating .."));
			update();
			
			unsetCursor();
			resolveOrientation();
			
			event->accept();
			return;
		} else {
			userAction = LaueFilm::UANone;
			displayMessage(QString(""));
			event->accept();
			update();
			return;
		}
	}
	
	if(getBit(userAction,LaueFilm::UASetOrigin)){
		if(event->key() == Qt::Key_Up){
			if(origin_circle <= 1) {
				origin_circle += 0.04;
			}
		}
		if(event->key() == Qt::Key_Down){
			if(origin_circle >= 0) {
				origin_circle -= 0.04;
			}
		}
		update();
		event->accept();
		return;
	}
	
	bool flag = true;
	double step = 0.025;
	
	if(QApplication::keyboardModifiers()  == Qt::ShiftModifier){
		step = step * 10;
	}
	
	double xrot = 0;
	double yrot = 0;
	double zrot = 0;

	switch(event->key()){
		case Qt::Key_Up:
			yrot+=step;
			break;
		case Qt::Key_Down:
			yrot-=step;
			break;
		case Qt::Key_Left:
			zrot-=step;
			break;
		case Qt::Key_Right:
			zrot+=step;
			break;
		case Qt::Key_Z:
			xrot+=(2 * step);
			break;
		case Qt::Key_X:
			xrot-=(2 * step);
			break;
		default:
			//qDebug("LaueFilm::keyPressEvent() : Unknown Key %d",event->key());
			flag = false;
			break;
	}
	
	if(flag){
		emit rotationsChanged(xrot, yrot, zrot);
		event->accept();
	} else {
		event->ignore();
	}
}

QPoint LaueFilm::worldToPixels(QPointF world){
	double isf = imageScaleFactor();
	QPoint origin;
	
	QPointF nworld = world + laueOrigin;
	QPoint pixel = QPoint(qRound(nworld.x()*pixels_per_mm/isf),
						  qRound(-nworld.y()*pixels_per_mm/isf));
	pixel = laueRect.center() + pixel;

	return pixel;
}

QPointF LaueFilm::pixelsToWorld(QPoint pixel){
	double isf = imageScaleFactor();
	pixel = pixel - laueRect.center();
	QPointF world = QPointF(pixel.x() * isf / pixels_per_mm, -1 * pixel.y() * isf /  pixels_per_mm);
	return world - laueOrigin;
}

double LaueFilm::imageScaleFactor(void){
	double isf = 1.0;
	if(importedScaledImage->height() != 0){
		isf = (double)importedAdjustedImage->height() / (double)importedScaledImage->height();
	}	
	return isf;
}

void LaueFilm::rotateAboutAxis(void) {
	finishTool(false);
	// This only works in free rotate 
	// so check, and if not warn with a message box.
	
	if(!crystal->getFreeRotate()) {
		QMessageBox::information(this, tr(APP_NAME),
			tr("You cannot rotate about an axis when set to goniometer rotations. Please select \"free rotate\" and try again."));
		return;
	}

	beginTool(UARotateAboutAxis);
	userActionStage = 1;
	displayMessage(QString("Click on point to rotate about."));
	setCursor(Qt::CrossCursor);
}

void LaueFilm::setOrigin(void) {
	beginTool(UASetOrigin);
	originPreview = laueOrigin;
	setCursor(Qt::BlankCursor);
	setMouseTracking(true);
	displayMessage(QString("Click in window to define origin."));
}

void LaueFilm::measureScale(void) {
	beginTool(UAMeasureScale);
	userActionStage = 1;
	setCursor(Qt::CrossCursor);
	setMouseTracking(false);
	displayMessage(tr("Drag horizontally across a known width, then enter its width in millimetres."));
	update();
}

void LaueFilm::setImageScale(void) {
	finishTool(false);
	bool ok;
	
	double pixelSize = 1000.0 / pixels_per_mm;
	
	pixelSize = QInputDialog::getDouble(this, tr(APP_NAME),
											tr("Pixel size (microns; must be greater than zero)"), pixelSize, 0, 20000, 2, &ok);
	if(ok){
		if(std::isfinite(pixelSize) && pixelSize > 0)
			calibrateScale(1000.0 / pixelSize);
		else
			displayMessage(tr("Pixel size must be a finite, positive number. Scale unchanged."));
	}	
}

void LaueFilm::setOriginX(double x) {
	if(std::isfinite(x) && x != laueOrigin.x()) {
		laueOrigin.setX(x);
		clearOrientation();
	}
}

void LaueFilm::setOriginY(double y) {
	if(std::isfinite(y) && y != laueOrigin.y()) {
		laueOrigin.setY(y);
		clearOrientation();
	}
}

void LaueFilm::setScale(double value) {
	if(std::isfinite(value) && value > 0 && value != pixels_per_mm) {
		pixels_per_mm = value;
		clearOrientation();
	}
}

void LaueFilm::calibrateScale(double value) {
	if(!std::isfinite(value) || value <= 0) return;
	const QPointF origin = laueOrigin * (pixels_per_mm / value);
	if(!std::isfinite(origin.x()) || !std::isfinite(origin.y())) return;
	// UI calibration keeps the selected beam pixel fixed. setScale alone restores XTL state.
	laueOrigin = origin;
	setScale(value);
}

void LaueFilm::beginTool(int action) {
	finishTool(false);
	toolCursor = cursor();
	toolHadCursor = testAttribute(Qt::WA_SetCursor);
	toolMouseTracking = hasMouseTracking();
	toolOriginCircle = origin_circle;
	toolActive = true;
	userAction = action;
	userActionStage = 0;
}

void LaueFilm::finishTool(bool accepted) {
	if(!toolActive) return;
	if(!accepted) origin_circle = toolOriginCircle;
	if(!accepted && getBit(userAction,UASelectOrientation)) {
		numOrientationSpots = 0;
		setBit(&display,DisplayIndexing,false);
	}
	if(!accepted && getBit(userAction,UASelectUBOrientation)) {
		numUBOrientationSpots = 0;
		setBit(&display,DisplayUBIndexing,false);
	}
	if(getBit(userAction,UARotateAboutAxis)) setBit(&display,DisplayRotateAboutCross,false);
	if(toolHadCursor) setCursor(toolCursor);
	else unsetCursor();
	setMouseTracking(toolMouseTracking);
	if(rubberBand) rubberBand->hide();
	userAction = UANone;
	userActionStage = 0;
	toolActive = false;
	clearMessage();
}

void LaueFilm::measureZoom(void) {
	finishTool(false);
	userAction = LaueFilm::UAMeasureZoom;
	userActionStage = 1;
	setCursor(Qt::CrossCursor);
	displayMessage(QString("Click and drag to zoom."));
	setMouseTracking(false);
	update();
}

void LaueFilm::resetZoom(void) {
	imageZoomRect = importedAdjustedImage->rect();
	update();
}

void LaueFilm::displayMessage(const QString message){
	laueMessage = message;
	update();
}

void LaueFilm::clearMessage(void){
	laueMessage = QString("");
	update();
}

void LaueFilm::resetView(void){
	finishTool(false);
	const bool movedOrigin = !laueOrigin.isNull();
	laueOrigin = QPointF(0,0);
	setScale(100);
	if(movedOrigin) clearOrientation();
	update();
}

void LaueFilm::setDetLevel(int dl){
	laue->setDetLevel((double)dl / 100.0);
	update();
}

void LaueFilm::setSatLevel(int ds){
	laue->setDetSatLevel((double)ds / 100.0);
	update();
}

void LaueFilm::addIndexLabel(double h, double k, double l){
	for(int i=1;i<num_hkl_labels;i++){
		if((h == hkl_labels[i][0]) && (k == hkl_labels[i][1]) && (l == hkl_labels[i][2])){
			return;
		}
	}
	if(num_hkl_labels >= int(sizeof(hkl_labels) / sizeof(hkl_labels[0]))) {
		displayMessage(tr("The label limit has been reached. Clear labels before adding more."));
		return;
	}
	hkl_labels[num_hkl_labels][0] = h;
	hkl_labels[num_hkl_labels][1] = k;
	hkl_labels[num_hkl_labels][2] = l;
	num_hkl_labels++;
}

void LaueFilm::reCalc(void){
	laue->startCalculation(crystal);
}

void LaueFilm::displayAfterCalc(void){
	update();
	emit recalculated();
}

void LaueFilm::resolveOrientation(void) {
    if(indexingInProgress) return;
    finishTool(getBit(userAction,UASelectOrientation));
    QScopedValueRollback<bool> running(indexingInProgress,true);
    if(numOrientationSpots < 2) {
        QMessageBox::warning(this,tr(APP_NAME),tr("Select at least two separated reflections to index the Laue pattern."));
        update();
        return;
    }
    const quint64 revision = orientationRevision;
    IndexingDialog dialog(this);
    dialog.setIndexingParams(laueIndexing);
    if(dialog.exec() != QDialog::Accepted) { update(); return; }
    dialog.getIndexingParams(laueIndexing);
    if(revision != orientationRevision) {
        displayMessage(tr("Orientation inputs changed; select reflections again."));
        update();
        return;
    }
    laueIndexing->setDistance(laue->getDistance());
    laueIndexing->setLambdaRange(laue->getLambdaRangeMin(),laue->getLambdaRangeMax());
    QVector<QPointF> points;
    for(int i = 0; i < numOrientationSpots; ++i)
        points.append(QPointF(orientationSpotsX[i],orientationSpotsY[i]));

    QProgressDialog progress(tr("Searching crystal orientations..."),tr("Cancel"),0,100,this);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.setMinimumDuration(0);
    QEventLoop loop;
    bool canceled = false;
    bool finished = false;
    connect(laueIndexing,&LaueIndexingThread::progress,&progress,&QProgressDialog::setValue);
    connect(laueIndexing,&QThread::finished,&loop,[&] { finished = true; loop.quit(); });
    connect(&progress,&QProgressDialog::canceled,&loop,[&] {
        canceled = true;
        laueIndexing->requestInterruption();
    });
    if(!laueIndexing->indexLaue(*crystal,points)) return;
    progress.show();
    loop.exec();
    if(!finished) {
        canceled = true;
        laueIndexing->requestInterruption();
    }
    // finished() can precede the last thread cleanup; join before reading the result.
    laueIndexing->wait();
    progress.hide();
    const LaueIndexingResult &result = laueIndexing->result();
    if(revision != orientationRevision) {
        displayMessage(tr("Orientation inputs changed; select reflections again."));
        update();
        return;
    } else if(canceled || result.canceled) {
        displayMessage(tr("Canceled; orientation unchanged."));
    } else if(!result.error.isEmpty()) {
        QMessageBox::warning(this,tr(APP_NAME),result.error);
    } else {
        orientations = result.orientations;
        emit newIndexingAvailable();
        if(orientations.isEmpty()) {
            QMessageBox::information(this,tr(APP_NAME),
                tr("No matching orientation was found. Check the lattice, beam center, image scale, "
                   "detector distance, wavelength range and selected spots, then adjust the search limits or tolerances."));
        } else {
            displayMessage(result.truncated
                ? tr("Best %1 candidates (limit reached).").arg(orientations.size())
                : tr("%1 candidates; select a row below.").arg(orientations.size()));
        }
    }
    setBit(&display,DisplayIndexing,true);
    update();
}

void LaueFilm::solveOrientation(void){
	if(indexingInProgress) return;
	
	beginTool(UASelectOrientation);
	numOrientationSpots = 0;
	setBit(&display, LaueFilm::DisplayIndexing, true);

	setCursor(Qt::BlankCursor);
	setMouseTracking(true);
	displayMessage(tr("Select 2+ spots; Enter to solve."));
}

void LaueFilm::clearOrientationResults(void) {
	++orientationRevision;
	if(laueIndexing->isRunning()) laueIndexing->requestInterruption();
	orientations.clear();
	emit newIndexingAvailable();
	clearMessage();
}

void LaueFilm::clearOrientation(void) {
	finishTool(false);
	numOrientationSpots = 0;
	numUBOrientationSpots = 0;
	setBit(&display, LaueFilm::DisplayIndexing, false);
	setBit(&display, LaueFilm::DisplayUBIndexing, false);
	clearOrientationResults();
}

void LaueFilm::setOrientation(void) {
	beginTool(UASelectUBOrientation);
	numUBOrientationSpots = 0;
	setBit(&display, LaueFilm::DisplayIndexing, false);
	setBit(&display, LaueFilm::DisplayUBIndexing, true);
	
	setCursor(Qt::BlankCursor);
	setMouseTracking(true);
	displayMessage(QString("Select Two Laue spots for Indexing."));
}

void LaueFilm::resetOrientation(void) {
	if(numUBOrientationSpots != 2) return;
	const quint64 revision = orientationRevision;
	UMatrixDialog dialog;
	const int result = dialog.exec();
	if(revision != orientationRevision || numUBOrientationSpots != 2) {
		displayMessage(tr("Orientation inputs changed; select reflections again."));
		return;
	}
	if(result == QDialog::Accepted){
		Matrix pri = laue->filmposToVector(UBOrientationSpotsX[0],UBOrientationSpotsY[0]);
		Matrix sec = laue->filmposToVector(UBOrientationSpotsX[1],UBOrientationSpotsY[1]);
		
        if(getCrystal()->setU(pri,sec,dialog.getPrimary(), dialog.getSecondary())) {
            reCalc();
            emit rotationsChanged(0,0,0);
        } else
            QMessageBox::warning(this, tr("Set orientation"),
                tr("Select two nonzero, independent directions and two distinct diffraction spots."));
	} else {
		setBit(&display, DisplayUBIndexing, false);
	}
	
	setBit(&display, LaueFilm::DisplayLaue, true);
	displayMessage(QString(""));
}

void LaueFilm::setImage(QImage& image, bool display){
	clearOrientation();
	*importedImage = image;
	imageBrightnessValue = imageMinimumValue = 0;
	imageContrastValue = 100;
	imageMaximumValue = 255;
	setAdjustedImage();
	*importedScaledImage = *importedAdjustedImage;
	imageZoomRect = importedAdjustedImage->rect();
	if(display){
		setDisplayImage(true);
	}
}

void LaueFilm::setAdjustedImage(void){
	*importedAdjustedImage = *importedImage;
	if(getBit(display, DisplayImageInverted))
		importedAdjustedImage->invertPixels();
	if(getBit(display, DisplayImagePseudoColor))
		*importedAdjustedImage = convertToPseudoColor(*importedAdjustedImage);	
	if(getBit(display, DisplayImageRotated)){
		QMatrix r = QMatrix(0, 1, 1, 0, 0, 0); 
		QVector<QRgb> ct = importedAdjustedImage->colorTable();
		*importedAdjustedImage = importedAdjustedImage->transformed(r, Qt::FastTransformation);
		*importedAdjustedImage = importedAdjustedImage->convertToFormat(QImage::Format_Indexed8, ct);
	}
	*importedAdjustedImage = changeImageMax(*importedAdjustedImage, imageMaximumValue);
	*importedAdjustedImage = changeImageMin(*importedAdjustedImage, imageMinimumValue);
	*importedAdjustedImage = changeImageBrightness(*importedAdjustedImage, imageBrightnessValue);
	*importedAdjustedImage = changeImageContrast(*importedAdjustedImage, imageContrastValue);
}

void LaueFilm::adjustImage(double b, double c, double max, double min){
	if(!std::isfinite(b) || !std::isfinite(c) || !std::isfinite(max) || !std::isfinite(min)) return;
	imageBrightnessValue = qBound(-100.0,b,100.0);
	imageContrastValue = qBound(0.0,c,100.0);
	imageMaximumValue = qBound(0.0,max,255.0);
	imageMinimumValue = qBound(0.0,min,255.0);
	setAdjustedImage();
	
	repaint();
}

void LaueFilm::setBit(int *flags, int flag, int toset){
	if(toset)
		*flags |= flag;
	else
		*flags &= ~flag;
}

bool LaueFilm::getBit(int flags, int flag){
	if((flags & flag) == flag)
		return true;
	else
		return false;
}
