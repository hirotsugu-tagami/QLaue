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

#ifndef _LAUEFILM_H
#define _LAUEFILM_H

#include <QWidget>
#include <QThread>
#include <QRubberBand>
#include <QPrinter>
#include "laue.h"
#include "crystal.h"

#ifdef CALC_IN_THREAD

class LaueThread : public QThread, public lauegram {
	Q_OBJECT
	// The GUI owns the inherited settings and published spots. run() only
	// touches activeCrystal/activeCalculation until finished() has been joined.
	Crystal activeCrystal, pendingCrystal;
	lauegram activeCalculation, pendingCalculation;
	bool hasPending = false;
	bool calculationActive = false;
	bool activeComplete = false;
	void launchPending();
private slots:
	void finishCalculation();
public:
	LaueThread(QObject *parent = 0);
	~LaueThread();
	void startCalculation(Crystal *c);
	bool isCalculating() const { return calculationActive || hasPending; }
protected:
	void run();
signals:
	void calculated(void);
	void calculationFailed(QString message);
	void partialCalc(void);
	void spotsToGo(int);
	void calcStarted(void);
};

class LaueIndexingThread : public QThread, public lauegram {
	Q_OBJECT
	Crystal newOrient;
	QVector<QPointF> indexSpots;
	LaueIndexingResult indexingResult;
public:
	LaueIndexingThread(QObject *parent = 0);
	~LaueIndexingThread();
	bool indexLaue(const Crystal &crystal, const QVector<QPointF> &points);
	// Read only after finished() and wait(); the worker owns this during a run.
	const LaueIndexingResult &result() const { return indexingResult; }
protected:
	void run();
signals:
	void progress(int percent);
};
#else

class LaueThread : public QObject, public lauegram {
	Q_OBJECT
public:
	LaueThread(QObject *parent = 0);
	~LaueThread();
	void startCalculation(Crystal *c);
signals:
	void calculated(void);
	void partialCalc(void);
	void spotsToGo(int);
	void calcStarted(void);
};

#endif

class LaueFilm : 
public QWidget
{
	Q_OBJECT
private:	
	Crystal *crystal;
	LaueThread *laue;
	LaueIndexingThread *laueIndexing;
	
	QVector<LaueOrientations> orientations;
	bool indexingInProgress = false;

	// Variables to control the user action
	
	int display;
	int userAction;
	int userActionStage;
	QPoint moveStart;
	bool toolActive = false;
	QPointF originPreview;
	QCursor toolCursor;
	bool toolHadCursor = false;
	bool toolMouseTracking = false;
	double toolOriginCircle = 0.5;
	
	// Variables to convert from world to pixels
	
	QPointF laueOrigin;
	QPointF imageOrigin;
	
	// Bitmap laue image variables
	
	double pixels_per_mm;
	int imageBrightnessValue = 0;
	int imageContrastValue = 100;
	int imageMaximumValue = 255;
	int imageMinimumValue = 0;
	
	// Laue parameters
	
	double origin_circle;
	int max_spot_size;
	
	int numOrientationSpots;
	int numUBOrientationSpots;
	quint64 orientationRevision = 0;
	double *orientationSpotsX;
	double *orientationSpotsY;
	double UBOrientationSpotsX[2];
	double UBOrientationSpotsY[2];
	
	QPointF rotateAboutAxisPoint;
	
	QString laueMessage;
	QRect laueRect;
	QRect messageRect;
	QRubberBand *rubberBand;
	QPoint rubberBandOrigin;
	QPoint importedImagePos;
	QPoint importedImageCenter;
	double importedImageDX;
	double importedImageDY;
	QPoint zoomImagePos;
	
	double hkl_labels[200][3];
	int num_hkl_labels;
	int mouseover_hkl_label;
	
	void paintLaue(QPainter *painter, QRect size);
	void paintImage(QPainter *painter, QRect size);
	void prepareImage(QRect size);
	void paintLaueAxes(QPainter *painter, QRect laue);
	void paintLaueBox(QPainter *painter, QRect laue);
	void paintMsgBox(QPainter *painter, QRect msg);
	void paintDummyFilm(QPainter *painter, QRect laue, QRect msg);
	void paintOrigin(QPainter *painter, QRect size, bool preview = false);
	void paintIndexing(QPainter *painter, QRect size);
	void paintUBOrientationSpots(QPainter *painter, QRect size);
	void paintRotateAboutCross(QPainter *painter, QRect size);
	
	void addIndexLabel(double h, double k, double l);
	double pixelsToMm(int pixels);
	int mmToPixels(double mm);
	QPointF pixelsToWorld(QPoint pixel);
	QPoint  worldToPixels(QPointF world);
	void beginTool(int action);
	void finishTool(bool accepted);
	void calibrateScale(double value);
	
	void setBit(int *flags, int flag, int toset);
	bool getBit(int flags, int flag);
	
public:
	
	LaueFilm(QWidget *parent = 0, Crystal* crystal = 0);
	~LaueFilm();
	
	void setCrystal(Crystal* c)		{ crystal = c; }
	Crystal* getCrystal(void)		{ return crystal; }
	
	QImage *importedScaledImage;
	QImage *importedAdjustedImage;
	QImage *importedImage;
	QRect imageZoomRect; 
	
	LaueThread* getLaue(void)		{ return laue; }
	LaueIndexingThread* getIndexing(void)	{return laueIndexing; }
	const QVector<LaueOrientations> &getOrientations() const { return orientations; }
	
	double originX(void)			{ return laueOrigin.x(); }
	double originY(void)			{ return laueOrigin.y(); }
	double scale(void)				{ return pixels_per_mm; }
	void setOriginX(double x);
	void setOriginY(double y);
	void setScale(double x);
	int imageBrightness() const { return imageBrightnessValue; }
	int imageContrast() const { return imageContrastValue; }
	int imageMaximum() const { return imageMaximumValue; }
	int imageMinimum() const { return imageMinimumValue; }
	bool isImageInverted() const { return (display & DisplayImageInverted) != 0; }
	bool isImageRotated() const { return (display & DisplayImageRotated) != 0; }
	bool isImageDisplayed() const { return (display & DisplayImage) != 0; }
	
	enum {
		NoPrintReorientation = 0,
		PrintReorientation = 1
	};
	
	enum {
		UANone =					0x000,
		UASetOrigin =				0x001,
		UAMeasureScale =			0x002,
		UAMeasureZoom =				0x004,
		UASetXAxis =				0x008,
		UARotateX =					0x010,
		UARotateYZ =				0x020,
		UARotateAboutAxis =			0x040,
		UASelectOrientation =		0x080,
		UASelectUBOrientation =		0x100
	};
	
	enum {
		DisplayImage =				0x001,
		DisplayLaue =				0x002,
		DisplayIntensities =		0x004,
		DisplayOrigin =				0x008,
		DisplayLabels =				0x010,
		DisplayCharacteristic =		0x020,
		DisplayIntensitiesColor =	0x040,
		DisplayIndexing =			0x080,
		DisplayUBIndexing =			0x100,
		DisplayImageInverted =		0x200,
		DisplayRotateAboutCross =	0x400,
		DisplayImagePseudoColor =   0x800,
		DisplayImageRotated =       0x1000
	};
	
	enum {
		ImageNone =					0,
		ImageInvert =				0x001
	};
	
private slots:
	void displayAfterCalc(void);	
public slots:
	void print(QPrinter *printer, QPainter *painter, int mode);
	void setOrigin(void);
	void measureScale(void);
	void setImageScale(void);
	void measureZoom(void);
	void rotateAboutAxis(void);
	void resetZoom(void);
	void setU(double h1, double k1, double l1,
			  double h2, double k2, double l2);
	void setU(Matrix U);
	void setFSDistance(double d);
	void setDisplayLaue(int yesno);
	bool getDisplayLaue(void);						
	void setDisplayIntensities(int yesno);
	bool getDisplayIntensities(void);				
	void setDisplayIntensitiesAsColor(bool yesno);	
	bool getDisplayIntensitiesAsColor(void);
	void setDisplayCharacteristicInt(int yesno);
	bool getDisplayCharacteristicInt(void);
	void setZoom(double zoom);
	void setSpotSize(int spotsize)					{ max_spot_size = spotsize; update(); }
	int  getSpotSize(void)							{ return max_spot_size; }
	void setInvertImage(bool yesno);
	void setDisplayImage(bool yesno);
	void setRotateImage(bool yesno);
	void displayMessage(const QString message);
	void clearMessage(void);
	void setPixelsPerMMFromImage(void);
	void resetView(void);
	void setDetLevel(int);
	void setSatLevel(int);
	void resetLabels(void)							{ num_hkl_labels = 1; update(); }
	void showLabels(bool flag);
	void setAccuracy(int newval)					{ laue->setCalcPrecision(11 - newval); reCalc();}
	int  getAccuracy(void)							{ return (11 - laue->calcPrecision()); }
	void solveOrientation(void);
	void resolveOrientation(void);
	void clearOrientationResults(void);
	void clearOrientation(void);
	void setOrientation(void);
	void resetOrientation(void);
	void reCalc(void);
	void adjustImage(double, double, double, double);
	void setImage(QImage& image, bool display);
	void setAdjustedImage(void);
	double imageScaleFactor(void);
protected:
	void paintEvent(QPaintEvent *event);
	void mousePressEvent(QMouseEvent *event);
	void mouseMoveEvent(QMouseEvent *event); 
	void mouseReleaseEvent(QMouseEvent *event);
	void keyPressEvent(QKeyEvent *event);
signals:
	void rotationsChanged(double xx, double yy, double zz);
	void recalculated(void);
	void gotFocus(void);
	void newIndexingAvailable();
};

#endif
