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

#include <QtGui>
#include <QtWidgets>
#include "ui_setubmat.h"
#include "setubmat.h"
#include "version.h"
#include <cmath>

UMatrixDialog::UMatrixDialog(QWidget *parent) : 
QDialog(parent), primary(3,1), secondary(3,1) {
	ui.setupUi(this);
	connect(ui.buttonBox,SIGNAL(accepted()), this, SLOT(calcClicked()));
}

void UMatrixDialog::calcClicked(void){
    QLineEdit *fields[] = {ui.PH,ui.PK,ui.PL,ui.SH,ui.SK,ui.SL};
    bool numeric = true;
    for(int i=0;i<6;++i) {
        bool ok = false;
        const double value = fields[i]->text().toDouble(&ok);
        numeric = numeric && ok;
        (i<3 ? primary : secondary).Set(i%3,0,value);
    }
    const double plen = std::hypot(std::hypot(primary.Get(0,0),primary.Get(1,0)),primary.Get(2,0));
    const double slen = std::hypot(std::hypot(secondary.Get(0,0),secondary.Get(1,0)),secondary.Get(2,0));
    if(!numeric || !std::isfinite(plen) || !std::isfinite(slen) || plen==0 || slen==0 ||
       ((primary/plen) ^ (secondary/slen)).mag() < 1e-10) {
        QMessageBox::critical(this,tr(APP_NAME),
            tr("Enter finite, nonzero primary and secondary vectors that are not parallel or antiparallel."),
            QMessageBox::Cancel);
        return;
    }

	accept();
}

