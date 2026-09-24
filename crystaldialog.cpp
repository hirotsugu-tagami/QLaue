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

#include "atom.h"
#include "crystal.h"
#include "crystaldialog.h"
#include "datafile.h"
#include <cmath>

extern spacegroup spacegroups[];

CrystalDialog::CrystalDialog() {
	ui.setupUi(this);
	ui.AtomsTable->setHorizontalHeaderLabels(QStringList() << tr("Element") << tr("x") << tr("y") << tr("z"));
	connect(ui.importCifButton, SIGNAL(clicked()), this, SLOT(importCif()));
	
	// Setup the radiobuttons for lattice type
	
	connect(ui.CubicButton,SIGNAL(toggled(bool)),this,SLOT(latticeTypeChanged(bool)));
	connect(ui.TetragonalButton,SIGNAL(toggled(bool)),this,SLOT(latticeTypeChanged(bool)));
	connect(ui.OrthorhombicButton,SIGNAL(toggled(bool)),this,SLOT(latticeTypeChanged(bool)));
	connect(ui.HexagonalButton,SIGNAL(toggled(bool)),this,SLOT(latticeTypeChanged(bool)));
	connect(ui.MonoclinicButton,SIGNAL(toggled(bool)),this,SLOT(latticeTypeChanged(bool)));
	connect(ui.TriclinicButton,SIGNAL(toggled(bool)),this,SLOT(latticeTypeChanged(bool)));
	connect(ui.TrigonalButton,SIGNAL(toggled(bool)),this,SLOT(latticeTypeChanged(bool)));
	connect(ui.AllSpaceGroupsButton,SIGNAL(toggled(bool)),this,SLOT(latticeTypeChanged(bool)));
	
	connect(ui.LatticeA,SIGNAL(textChanged(const QString)),this,SLOT(latticeAChanged(const QString)));
	
	ui.CubicButton->setChecked(true);
	ui.DescriptionTextEdit->setAcceptRichText(false);
	resetAtomTable(50);
}

void CrystalDialog::resetAtomTable(int rows) {
	
	// Setup the table
	
	QStringList elements;
	elements << "";
	for(int i=0;i<98;i++){
		elements << QString("%1 %2").arg(i+1).arg(the_elements[i]);
	}
	
	int xwidth = (int)((double)(ui.AtomsTable->width() - 150) / 3.0);
	
	ui.AtomsTable->setRowCount(0);
	ui.AtomsTable->setRowCount(rows);
	for(int i=0;i<rows;i++){
		ui.AtomsTable->setColumnWidth(0,100);
		ui.AtomsTable->setRowHeight(i,25);
		QComboBox *newItem = new QComboBox;
		newItem->setEditable(false);
		newItem->insertItems(0,elements);
		ui.AtomsTable->setCellWidget(i,0,newItem);
		for(int j=1;j<4;j++){
			ui.AtomsTable->setColumnWidth(j,xwidth);
			QTableWidgetItem *newItem = new QTableWidgetItem();
			newItem->setFlags(Qt::ItemIsEditable | Qt::ItemIsEnabled);
			ui.AtomsTable->setItem(i,j, newItem);
		}
	}
	
}

void CrystalDialog::setCrystal(Crystal *crystal){
	// Load Atoms
	
	int natoms = crystal->getNRootAtoms();
	resetAtomTable(qMax(50, natoms));
	
	for(int i=0; i<natoms; i++){
		ui.AtomsTable->item(i,1)->setText(QString::number(crystal->getAtom(i)->getXPos(), 'g', 15));
		ui.AtomsTable->item(i,2)->setText(QString::number(crystal->getAtom(i)->getYPos(), 'g', 15));
		ui.AtomsTable->item(i,3)->setText(QString::number(crystal->getAtom(i)->getZPos(), 'g', 15));
		QComboBox *box = (QComboBox*)ui.AtomsTable->cellWidget(i,0);
		box->setCurrentIndex(crystal->getAtom(i)->getZ());
	}
	
	// Do Spacegroup stuff.
		
	if(!strcmp(crystal->getSpaceGroup()->Extn,"")){
	   switch(crystal->getSpaceGroup()->XtalSystem){
		   case Crystal::Triclinic:
			   ui.TriclinicButton->setChecked(true);
			   break;
		   case Crystal::Monoclinic:
			   ui.MonoclinicButton->setChecked(true);
			   break;
		   case Crystal::Orthorhombic:
			   ui.OrthorhombicButton->setChecked(true);
			   break;
		   case Crystal::Tetragonal:
			   ui.TetragonalButton->setChecked(true);
			   break;
		   case Crystal::Trigonal:
			   ui.TrigonalButton->setChecked(true);
			   break;
		   case Crystal::Hexagonal:
			   ui.HexagonalButton->setChecked(true);
			   break;
		   case Crystal::Cubic:
			   ui.CubicButton->setChecked(true);
			   break;
	   }
	} else {
		ui.AllSpaceGroupsButton->setChecked(true);
	}
	
	// Now select in the spacegroup combobox 
	
	for(int i=0;i<ui.SpacegroupBox->count();i++){
		if(crystal->getSpaceGroup()->index == SpacegroupBoxIndex[i]){
			ui.SpacegroupBox->setCurrentIndex(i);
		}
	}
	
	// Changing the lattice type resets fields; restore the exact values afterwards.
	const QSignalBlocker latticeABlocker(ui.LatticeA);
	ui.LatticeA->setText(QString::number(crystal->getLatticeA(), 'g', 15));
	ui.LatticeB->setText(QString::number(crystal->getLatticeB(), 'g', 15));
	ui.LatticeC->setText(QString::number(crystal->getLatticeC(), 'g', 15));
	ui.LatticeAlpha->setText(QString::number(180.0 * crystal->getLatticeAlpha() / M_PI, 'g', 15));
	ui.LatticeBeta->setText(QString::number(180.0 * crystal->getLatticeBeta() / M_PI, 'g', 15));
	ui.LatticeGamma->setText(QString::number(180.0 * crystal->getLatticeGamma() / M_PI, 'g', 15));

	// Set the description text
	
	ui.DescriptionTextEdit->setPlainText(QString::fromUtf8(crystal->getName()));
}

void CrystalDialog::getCrystal(Crystal *crystal){
	// Save the crystal parameters
	
	double a,b,c,alpha,beta,gamma;
	
	a = ui.LatticeA->text().toDouble();
	b = ui.LatticeB->text().toDouble();
	c = ui.LatticeC->text().toDouble();

	alpha = M_PI * ui.LatticeAlpha->text().toDouble() / 180.0;
	beta = M_PI * ui.LatticeBeta->text().toDouble() / 180.0;
	gamma = M_PI * ui.LatticeGamma->text().toDouble() / 180.0;

	crystal->setLattice(a,b,c,alpha,beta,gamma);
	
	// Now set the atoms ... cycle trough the table
	
	crystal->delAllAtoms();
	
	for(int i=0; i<ui.AtomsTable->rowCount(); ++i){
		QComboBox *box = (QComboBox*)ui.AtomsTable->cellWidget(i,0);
		if(box->currentIndex() > 0){
			a = ui.AtomsTable->item(i,1)->text().toDouble();
			b = ui.AtomsTable->item(i,2)->text().toDouble();
			c = ui.AtomsTable->item(i,3)->text().toDouble();
			crystal->addAtom(box->currentIndex(),a-std::floor(a),b-std::floor(b),c-std::floor(c));
		}
	}
	
	// Set the spacegroup
	// qDebug("CrystalDialog::getCrystal() : SpacegroupBox Index = %d",ui.SpacegroupBox->currentIndex());
	
	crystal->setSpaceGroup(SpacegroupBoxIndex[ui.SpacegroupBox->currentIndex()]);
	crystal->spaceGroupGenerate();
	
	crystal->setName(ui.DescriptionTextEdit->toPlainText().toUtf8().constData());
}

QString CrystalDialog::validationError() const {
	QLineEdit *fields[] = {ui.LatticeA,ui.LatticeB,ui.LatticeC,
		ui.LatticeAlpha,ui.LatticeBeta,ui.LatticeGamma};
	double cell[6];
	for(int i=0; i<6; ++i){
		bool ok = false;
		cell[i] = fields[i]->text().toDouble(&ok);
		if(!ok) return tr("Enter a valid number in each lattice field.");
		if(i >= 3) cell[i] *= M_PI / 180;
	}
	if(!Crystal::isValidLattice(cell[0],cell[1],cell[2],cell[3],cell[4],cell[5]))
		return tr("The unit cell has invalid lengths, angles or volume.");
	if(ui.SpacegroupBox->currentIndex() < 0) return tr("Select a space group.");
	Crystal candidate;
	candidate.setSpaceGroup(SpacegroupBoxIndex[ui.SpacegroupBox->currentIndex()]);
	int atoms = 0;
	for(int row=0; row<ui.AtomsTable->rowCount(); ++row){
		QComboBox *box = qobject_cast<QComboBox *>(ui.AtomsTable->cellWidget(row,0));
		if(box->currentIndex() <= 0) continue;
		if(++atoms > candidate.maxRootAtoms())
			return tr("Too many atom sites for this space group (maximum %1 sites).")
				.arg(candidate.maxRootAtoms());
		for(int column=1; column<4; ++column){
			bool ok = false;
			double position = ui.AtomsTable->item(row,column)->text().toDouble(&ok);
			if(!ok || !std::isfinite(position))
				return tr("Enter valid fractional coordinates for atom %1.").arg(row+1);
		}
	}
	return QString();
}

void CrystalDialog::accept() {
	const QString error = validationError();
	if(!error.isEmpty()) {
		QMessageBox::warning(this, tr("Set Lattice"), error);
		return;
	}
	QDialog::accept();
}

void CrystalDialog::importCif() {
	const QString filename = QFileDialog::getOpenFileName(this, tr("Import crystal from CIF"),
		importDirectory, tr("CIF Files (*.cif *.CIF);;All Files (*)"));
	if(filename.isEmpty()) return;
	DataFile data;
	if(!data.readCif(filename)) {
		QMessageBox::warning(this, tr("CIF import failed"),
			tr("Could not import %1.\n\n%2").arg(QFileInfo(filename).fileName(), data.getErrorStr()));
		return;
	}
	Crystal imported = data.getCrystal();
	setCrystal(&imported);
	importDirectory = QFileInfo(filename).absolutePath();
}

void CrystalDialog::latticeTypeChanged(bool state){
	if(state){
		if(ui.OrthorhombicButton->isChecked()){
			lattice_type = Crystal::Orthorhombic;
			ui.LatticeAlpha->setText("90");
			ui.LatticeBeta->setText("90");
			ui.LatticeGamma->setText("90");
			ui.LatticeAlpha->setEnabled(false);
			ui.LatticeBeta->setEnabled(false);
			ui.LatticeGamma->setEnabled(false);
			ui.LatticeB->setEnabled(true);
			ui.LatticeA->setEnabled(true);
			ui.LatticeC->setEnabled(true);
			setSpacegroupBox(Crystal::Orthorhombic);
			return;
		}
		if(ui.TetragonalButton->isChecked()){
			lattice_type = Crystal::Tetragonal;
			ui.LatticeAlpha->setText("90");
			ui.LatticeBeta->setText("90");
			ui.LatticeGamma->setText("90");
			ui.LatticeAlpha->setEnabled(false);
			ui.LatticeBeta->setEnabled(false);
			ui.LatticeGamma->setEnabled(false);
			ui.LatticeB->setEnabled(false);
			ui.LatticeA->setEnabled(true);
			ui.LatticeC->setEnabled(true);
			setSpacegroupBox(Crystal::Tetragonal);
			return;
		}
		if(ui.CubicButton->isChecked()){
			lattice_type = Crystal::Cubic;
			ui.LatticeAlpha->setText("90");
			ui.LatticeBeta->setText("90");
			ui.LatticeGamma->setText("90");
			ui.LatticeAlpha->setEnabled(false);
			ui.LatticeBeta->setEnabled(false);
			ui.LatticeGamma->setEnabled(false);
			ui.LatticeA->setEnabled(true);
			ui.LatticeB->setEnabled(false);
			ui.LatticeC->setEnabled(false);
			setSpacegroupBox(Crystal::Cubic);
			return;
		}
		if(ui.MonoclinicButton->isChecked()){
			lattice_type = Crystal::Monoclinic;
			ui.LatticeAlpha->setEnabled(false);
			ui.LatticeBeta->setEnabled(true);
			ui.LatticeGamma->setEnabled(false);
			ui.LatticeAlpha->setText("90");
			ui.LatticeGamma->setText("90");
			ui.LatticeA->setEnabled(true);
			ui.LatticeB->setEnabled(true);
			ui.LatticeC->setEnabled(true);
			setSpacegroupBox(Crystal::Monoclinic);
			return;
		}
		if(ui.HexagonalButton->isChecked()){
			lattice_type = Crystal::Hexagonal;
			ui.LatticeAlpha->setText("90");
			ui.LatticeBeta->setText("90");
			ui.LatticeGamma->setText("120");
			ui.LatticeAlpha->setEnabled(false);
			ui.LatticeBeta->setEnabled(false);
			ui.LatticeGamma->setEnabled(false);
			ui.LatticeA->setEnabled(true);
			ui.LatticeB->setEnabled(false);
			ui.LatticeC->setEnabled(true);
			setSpacegroupBox(Crystal::Hexagonal);
			return;
		}
		if(ui.TriclinicButton->isChecked()){
			lattice_type = Crystal::Triclinic;
			ui.LatticeAlpha->setEnabled(true);
			ui.LatticeBeta->setEnabled(true);
			ui.LatticeGamma->setEnabled(true);
			ui.LatticeA->setEnabled(true);
			ui.LatticeB->setEnabled(true);
			ui.LatticeC->setEnabled(true);
			setSpacegroupBox(Crystal::Triclinic);
			return;
		}
		if(ui.TrigonalButton->isChecked()){
			lattice_type = Crystal::Triclinic;
			ui.LatticeAlpha->setEnabled(true);
			ui.LatticeBeta->setEnabled(false);
			ui.LatticeGamma->setEnabled(false);
			ui.LatticeA->setEnabled(true);
			ui.LatticeB->setEnabled(false);
			ui.LatticeC->setEnabled(false);
			setSpacegroupBox(Crystal::Trigonal);
			return;
		}
		if(ui.AllSpaceGroupsButton->isChecked()){
			lattice_type = Crystal::Triclinic;
			ui.LatticeAlpha->setEnabled(true);
			ui.LatticeBeta->setEnabled(true);
			ui.LatticeGamma->setEnabled(true);
			ui.LatticeA->setEnabled(true);
			ui.LatticeB->setEnabled(true);
			ui.LatticeC->setEnabled(true);
			setSpacegroupBox(-1);
			return;
		}
	}
}

void CrystalDialog::setSpacegroupBox(int crystaltype){
	QStringList sgroups;
	int sgnum = 0;
	int index = 0;
	
	while(spacegroups[sgnum].number != 0){
		if((spacegroups[sgnum].XtalSystem == crystaltype) ||
		   (crystaltype == -1)){
			SpacegroupBoxIndex[index++] = sgnum;
			
			if(strcmp(spacegroups[sgnum].Extn,"")){
				sgroups << QString("%2 : %3 (%1)").arg(spacegroups[sgnum].number)
				.arg(spacegroups[sgnum].Name)
				.arg(spacegroups[sgnum].Extn)
				.replace("_","");
			} else {
				sgroups << QString("%2 (%1)").arg(spacegroups[sgnum].number)
				.arg(spacegroups[sgnum].Name)
				.replace("_","");
			}
		}
		sgnum++;
	}
	
	ui.SpacegroupBox->clear();
	ui.SpacegroupBox->insertItems(0,sgroups);	
	ui.SpacegroupBox->setCurrentIndex(0);
}

void CrystalDialog::latticeAChanged(const QString newval){
	if(ui.TetragonalButton->isChecked()){
		ui.LatticeB->setText(newval);
		return;
	}
	if(ui.CubicButton->isChecked()){
		ui.LatticeB->setText(newval);
		ui.LatticeC->setText(newval);
		return;
	}
	if(ui.HexagonalButton->isChecked()){
		ui.LatticeB->setText(newval);
		return;
	}
}
