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

#include "indexingdialog.h"
#include "laue.h"

IndexingDialog::IndexingDialog(QWidget *parent) : QDialog(parent) {
	ui.setupUi(this);
	for(QLineEdit *edit : {ui.maxH,ui.maxK,ui.maxL}) {
		edit->setValidator(new QIntValidator(0,lauegram::MaxIndexingRange,edit));
		edit->setToolTip(tr("Maximum absolute index for all matched spots (0 to %1). Larger ranges take longer.").arg(lauegram::MaxIndexingRange));
	}
	for(QLineEdit *edit : {ui.angTol,ui.spotAccuracy}) {
		auto *validator = new QDoubleValidator(0,edit == ui.angTol ? 89.999 : 1000,6,edit);
		validator->setLocale(QLocale::c());
		validator->setNotation(QDoubleValidator::ScientificNotation);
		edit->setValidator(validator);
	}
}

void IndexingDialog::accept() {
	for(QLineEdit *edit : {ui.maxH,ui.maxK,ui.maxL,ui.angTol,ui.spotAccuracy}) {
		if(!edit->hasAcceptableInput()) {
			QMessageBox::warning(this,tr("Indexing Parameters"),tr("Enter valid search ranges and positive tolerances."));
			edit->setFocus();
			return;
		}
	}
	if(ui.maxH->text().toInt()+ui.maxK->text().toInt()+ui.maxL->text().toInt() == 0 ||
	   ui.angTol->text().toDouble() <= 0 || ui.spotAccuracy->text().toDouble() <= 0) {
		QMessageBox::warning(this,tr("Indexing Parameters"),tr("At least one search range and both tolerances must be greater than zero."));
		return;
	}
	QDialog::accept();
}

void IndexingDialog::setIndexingParams(lauegram *laue){
	ui.maxH->setText(QString("%1").arg(laue->getIndexingMaxH()));
	ui.maxK->setText(QString("%1").arg(laue->getIndexingMaxK()));
	ui.maxL->setText(QString("%1").arg(laue->getIndexingMaxL()));
	ui.angTol->setText(QString("%1").arg(laue->getIndexingAngularTol() * 180 / (4 * atan(1))));
	ui.spotAccuracy->setText(QString("%1").arg(laue->getIndexingSpotTol()));
}

void IndexingDialog::getIndexingParams(lauegram *laue){
	laue->setIndexingMaxH(ui.maxH->text().toInt());
	laue->setIndexingMaxK(ui.maxK->text().toInt());
	laue->setIndexingMaxL(ui.maxL->text().toInt());
	
	laue->setIndexingSpotTol(ui.spotAccuracy->text().toDouble());
	laue->setIndexingAngularTol(ui.angTol->text().toDouble() * 4 * atan(1) / 180);
}
