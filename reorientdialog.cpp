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
#include <cmath>
#include "reorientdialog.h"
#include "laue.h"

ReorientDialog::ReorientDialog(QWidget *parent) : QDialog(parent),
    laue(nullptr), crystal(nullptr), selectedOrientation(false),
    angleX(0), angleY(0), angleZ(0) {
	ui.setupUi(this);
	
	ui.okButton->setEnabled(false);
	ui.printButton->setEnabled(false);
	
	selectedOrientation = false;
	
	connect(ui.XGoButton,SIGNAL(clicked()),this, SLOT(go()));
	connect(ui.ZoneButton, SIGNAL(toggled(bool)), ui.ZHLineEdit, SLOT(setEnabled(bool)));
	connect(ui.ZoneButton, SIGNAL(toggled(bool)), ui.ZKLineEdit, SLOT(setEnabled(bool)));
	connect(ui.ZoneButton, SIGNAL(toggled(bool)), ui.ZLLineEdit, SLOT(setEnabled(bool)));
	connect(ui.okButton, SIGNAL(clicked()),this, SLOT(okPressed()));
	connect(ui.printButton, SIGNAL(clicked()),this, SLOT(printPressed()));
	connect(ui.cancelButton, SIGNAL(clicked()),this, SLOT(cancelPressed()));
	connect(ui.AngleRangeP, SIGNAL(toggled(bool)), this, SLOT(updateTable()));
	connect(ui.AngleRangePM, SIGNAL(toggled(bool)), this, SLOT(updateTable()));
	
    connect(ui.DataTable, &QTableWidget::itemSelectionChanged, this, [this] {
        const int row = ui.DataTable->currentRow();
        ui.okButton->setEnabled(resultsCurrent && laue && laue->reorientation.isValid(row) &&
                               !ui.DataTable->selectedItems().isEmpty());
    });
    auto invalidate = [this] {
        resultsCurrent = false;
        ui.DataTable->clearContents();
        ui.DataTable->clearSelection();
        ui.okButton->setEnabled(false);
        ui.printButton->setEnabled(false);
        selectedOrientation = false;
    };
    for(QLineEdit *field : {ui.XHLineEdit,ui.XKLineEdit,ui.XLLineEdit,
                           ui.ZHLineEdit,ui.ZKLineEdit,ui.ZLLineEdit})
        connect(field, &QLineEdit::textChanged, this, invalidate);
    for(QRadioButton *button : {ui.XAxisButton,ui.ZoneButton,ui.XAxisRadioButton,
                                ui.YAxisRadioButton,ui.ZAxisRadioButton})
        connect(button, &QRadioButton::toggled, this, invalidate);
    ui.XAxisButton->setChecked(true);
	ui.ZHLineEdit->setEnabled(false);
	ui.ZKLineEdit->setEnabled(false);
	ui.ZLLineEdit->setEnabled(false);

}

void ReorientDialog::okPressed() {
    selectedOrientation = false;
    const int row = ui.DataTable->currentRow();
    if(!laue || !ui.okButton->isEnabled() || ui.DataTable->selectedItems().isEmpty() ||
       !laue->reorientation.isValid(row)) return;
    // Apply the full-precision result, not the rounded text shown in the table.
    const double *angles = laue->reorientation.getAngles(row/2,row%2);
    angleX=angles[0]; angleY=angles[1]; angleZ=angles[2];
    selectedOrientation = true;
    done(OKClicked);
}

void ReorientDialog::printPressed(void){
    if(!ui.printButton->isEnabled()) return;
	selectedOrientation = false;
	done(PrintClicked);
}

void ReorientDialog::cancelPressed(void){
	selectedOrientation = false;
	done(CancelClicked);
}


bool ReorientDialog::getSelectedOrientation(void){
	return selectedOrientation;
}

void ReorientDialog::getAngles(double *x, double *y, double *z){
	*x = angleX;
	*y = angleY;
	*z = angleZ;
}

void ReorientDialog::setLaue(lauegram* newLaue){
	laue = newLaue;
}

void ReorientDialog::setCrystal(Crystal* newCrystal){
	crystal = newCrystal;
}

void ReorientDialog::go() {
    if(!laue || !crystal) return;
    selectedOrientation = false;
    resultsCurrent = false;
    ui.okButton->setEnabled(false);
    ui.printButton->setEnabled(false);
    setupTable();
    ui.DataTable->clearContents();
    ui.DataTable->clearSelection();
    ui.DataTable->setCurrentCell(-1,-1);
    QLineEdit *fields[] = {ui.XHLineEdit,ui.XKLineEdit,ui.XLLineEdit,
                          ui.ZHLineEdit,ui.ZKLineEdit,ui.ZLLineEdit};
    double directions[6] = {};
    const bool zone = ui.ZoneButton->isChecked();
    for(int i=0; i<(zone ? 6 : 3); ++i) {
        bool ok = false;
        directions[i] = fields[i]->text().toDouble(&ok);
        if(!ok || !std::isfinite(directions[i])) {
            QMessageBox::warning(this, tr("Reorientation"), tr("Enter a finite number in each direction field."));
            return;
        }
    }
    laue->reorientation.setUB(crystal->getUBunrotated());
    laue->reorientation.setPrimary(directions[0],directions[1],directions[2]);
    int mode = ui.YAxisRadioButton->isChecked() ? Reorientation::YAxis :
               ui.ZAxisRadioButton->isChecked() ? Reorientation::ZAxis : Reorientation::XAxis;
    if(zone) {
        laue->reorientation.setSecondary(directions[3],directions[4],directions[5]);
        mode = Reorientation::Zone;
    }
    laue->reorientation.calc(mode);
    const bool valid = laue->reorientation.getValidCount() > 0;
    resultsCurrent = valid;
    updateTable();
    ui.printButton->setEnabled(valid);
    if(!valid)
        QMessageBox::warning(this, tr("Reorientation"),
            tr("No valid rotation was found. Directions must be nonzero. Double Axis requires two perpendicular directions in reciprocal space."));
}

void ReorientDialog::updateTable() {
    if(!resultsCurrent || !laue || ui.DataTable->rowCount() != 8) return;
    const int selectedRow = ui.DataTable->selectedItems().isEmpty() ? -1 : ui.DataTable->currentRow();
    QSignalBlocker blocker(ui.DataTable);
    for(int row=0; row<8; ++row) {
        const bool valid = laue->reorientation.isValid(row);
        const double *angles = laue->reorientation.getAngles(row/2,row%2);
        for(int column=0; column<3; ++column) {
            if(valid) setTableItem(ui.DataTable,row,column,angles[column]*180/M_PI);
            else {
                auto item = new QTableWidgetItem(QString::fromUtf8("—"));
                item->setFlags(Qt::NoItemFlags);
                item->setBackground(QColor("lightgray"));
                ui.DataTable->setItem(row,column,item);
            }
        }
    }
    if(laue->reorientation.isValid(selectedRow)) ui.DataTable->selectRow(selectedRow);
    ui.okButton->setEnabled(laue->reorientation.isValid(selectedRow));
}

void ReorientDialog::setupTable(void){
	
	ui.DataTable->setRowCount(8);
	ui.DataTable->setColumnCount(3);
	
	ui.DataTable->setColumnWidth(0,100);
	ui.DataTable->setColumnWidth(1,100);
	ui.DataTable->setColumnWidth(2,100);
	
	QStringList hHeaders, vHeaders;
	hHeaders << "Rot X" << "Rot Y" << "Rot Z";
	ui.DataTable->setHorizontalHeaderLabels(hHeaders);
	ui.DataTable->verticalHeader()->setVisible(false);
	ui.DataTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	ui.DataTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

void ReorientDialog::setTableItem(QTableWidget *table, int x, int y, double val) {
	
	if(val == 0.0){
		QTableWidgetItem *newItem = new QTableWidgetItem();
		newItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
		newItem->setBackgroundColor(QColor("lightgray"));
		table->setItem(x,y, newItem);
	} else {
		double newval = val;
		if(ui.AngleRangeP->isChecked() == true){
			if(val < 0){
				newval = val + 360.0;
			} 
		} 
		QTableWidgetItem *newItem = new QTableWidgetItem(QString("%1").arg(newval,3,'f',2));
		newItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
		newItem->setTextColor(QColor("black"));
		newItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
		table->setItem(x,y, newItem);
	}
}
