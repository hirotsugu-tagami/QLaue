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
	
	$Revision: 6 $
	$Author: swilkins $
	$Date: 2007-02-10 23:26:47 -0500 (Sat, 10 Feb 2007) $
	$HeadURL: https://www.stuwilkins.com/svn/QLaue/trunk/lauewidget.cpp $

*/

#include <QtCore>
#include <QtGui>
#include <QtWidgets>
#include <algorithm>
#include <cmath>
#include "laue.h"
#include "xray.h"
#include "preferences.h"

PreferencesDialog::PreferencesDialog() {
	ui.setupUi(this);
	
	connect(ui.LaueTubeVoltageSpinBox, SIGNAL(valueChanged(double)), this, SLOT(tubeVoltageChanged(double)));
	
	anodes << "Cu" << "Mo" << "W " << "Ag";
	ui.LaueAnodeComboBox->insertItems(0,anodes);
}

void PreferencesDialog::setLaue(lauegram* laue){
	ui.LaueLorentzCheckBox->setChecked(laue->intensityCorrection() & XRay::IntensityLorentz);
	ui.LauePolnCheckBox->setChecked(laue->intensityCorrection() & XRay::IntensityPolarization);
	ui.LaueSampleAbsCheckBox->setChecked(laue->intensityCorrection() & lauegram::IntensitySelfAbsorption);
	ui.LaueXRayTubeButton->setChecked(laue->intensityCorrection() & lauegram::IntensityXRayTube);
	ui.LaueOtherButton->setChecked(!(laue->intensityCorrection() & lauegram::IntensityXRayTube));

	// Set tube voltage from max_k
	loadedLambdaMax = laue->getLambdaRangeMax();
	const double minimumVoltage = std::ceil(HC_OVER_E / loadedLambdaMax * 10000) / 10000;
	const QSignalBlocker voltageSignals(ui.LaueTubeVoltageSpinBox);
	ui.LaueTubeVoltageSpinBox->setMaximum(std::max(100.0, HC_OVER_E / laue->getLambdaRangeMin()));
	ui.LaueTubeVoltageSpinBox->setMinimum(minimumVoltage);
	ui.LaueTubeVoltageSpinBox->setValue(HC_OVER_E / laue->getLambdaRangeMin());
	shownTubeVoltage = ui.LaueTubeVoltageSpinBox->value();
	ui.LaueLambdaMinSpinBox->setValue(laue->getLambdaRangeMin());
	ui.LaueLambdaMaxSpinBox->setValue(loadedLambdaMax);
	
	QString setElement = QString(XRay::elementName(laue->tubeZ()));
	int index = ui.LaueAnodeComboBox->findText(setElement);
	if(index < 0) {
		ui.LaueAnodeComboBox->addItem(setElement);
		index = ui.LaueAnodeComboBox->count()-1;
	}
	ui.LaueAnodeComboBox->setCurrentIndex(index);
}

void PreferencesDialog::getLaue(lauegram* laue){
	const bool voltageChanged = ui.LaueTubeVoltageSpinBox->value() != shownTubeVoltage;
	const double newLambdaMin = HC_OVER_E / ui.LaueTubeVoltageSpinBox->value();
	if(voltageChanged && (!std::isfinite(newLambdaMin) || newLambdaMin <= 0 ||
	                      newLambdaMin > loadedLambdaMax)) return;
	const int editable = XRay::IntensityLorentz | XRay::IntensityPolarization |
	                     lauegram::IntensitySelfAbsorption | lauegram::IntensityXRayTube;
	int newinten = laue->intensityCorrection() & ~editable;
	if(ui.LaueLorentzCheckBox->isChecked())
		newinten = newinten | XRay::IntensityLorentz;
	if(ui.LauePolnCheckBox->isChecked())
		newinten = newinten | XRay::IntensityPolarization;
	if(ui.LaueSampleAbsCheckBox->isChecked())
		newinten = newinten | lauegram::IntensitySelfAbsorption;
	if(ui.LaueXRayTubeButton->isChecked())
		newinten = newinten | lauegram::IntensityXRayTube;
	
	laue->setIntensityCorrection(newinten);
	
	if(voltageChanged) laue->setLambdaRange(newLambdaMin, loadedLambdaMax);
	
	int Z = XRay::elementNumber(ui.LaueAnodeComboBox->currentText().toLatin1());
	if(Z != -1){
		laue->setTubeZ(Z);
	}
}

void PreferencesDialog::tubeVoltageChanged(double newval){
	ui.LaueLambdaMinSpinBox->setValue(HC_OVER_E / newval);
}
