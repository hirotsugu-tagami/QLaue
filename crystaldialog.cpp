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

namespace {
bool matchesCell(const spacegroup &group, const double *cell) {
    // Keep the metric convention and 1e-3 tolerance used by cifreader.cpp's
    // matchesCell: opening a supported, rounded CIF must not make it invalid.
    const double metric[3][3] = {
        {cell[0]*cell[0], cell[0]*cell[1]*std::cos(cell[5]), cell[0]*cell[2]*std::cos(cell[4])},
        {cell[0]*cell[1]*std::cos(cell[5]), cell[1]*cell[1], cell[1]*cell[2]*std::cos(cell[3])},
        {cell[0]*cell[2]*std::cos(cell[4]), cell[1]*cell[2]*std::cos(cell[3]), cell[2]*cell[2]}
    };
    for(int n=0; n<group.ngenerators; ++n) {
        for(int i=0; i<3; ++i) for(int j=0; j<3; ++j) {
            double transformed = 0;
            for(int k=0; k<3; ++k) for(int l=0; l<3; ++l)
                transformed += group.matrix[n][k*3+i] * metric[k][l] * group.matrix[n][l*3+j];
            if(std::abs(transformed-metric[i][j]) > 1e-3 * cell[i] * cell[j]) return false;
        }
    }
    return true;
}
}

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
	
	connect(ui.LatticeA,SIGNAL(textChanged(const QString)),this,SLOT(syncLatticeConstraints()));
	connect(ui.LatticeAlpha,SIGNAL(textChanged(const QString)),this,SLOT(syncLatticeConstraints()));
	connect(ui.SpacegroupBox,SIGNAL(currentIndexChanged(int)),this,SLOT(syncLatticeConstraints()));
	
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
	const QSignalBlocker latticeAlphaBlocker(ui.LatticeAlpha);
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
	// Validate before touching the caller's crystal, then publish atomically.
    if(!validationError().isEmpty()) return;
    Crystal candidate = *crystal;
    // Save the crystal parameters
	
	double a,b,c,alpha,beta,gamma;
	
	a = ui.LatticeA->text().toDouble();
	b = ui.LatticeB->text().toDouble();
	c = ui.LatticeC->text().toDouble();

	alpha = M_PI * ui.LatticeAlpha->text().toDouble() / 180.0;
	beta = M_PI * ui.LatticeBeta->text().toDouble() / 180.0;
	gamma = M_PI * ui.LatticeGamma->text().toDouble() / 180.0;

	candidate.setLattice(a,b,c,alpha,beta,gamma);
	
	// Now set the atoms ... cycle trough the table
	
	candidate.delAllAtoms();
	
	for(int i=0; i<ui.AtomsTable->rowCount(); ++i){
		QComboBox *box = (QComboBox*)ui.AtomsTable->cellWidget(i,0);
		if(box->currentIndex() > 0){
			a = ui.AtomsTable->item(i,1)->text().toDouble();
			b = ui.AtomsTable->item(i,2)->text().toDouble();
			c = ui.AtomsTable->item(i,3)->text().toDouble();
			if(!candidate.addAtom(box->currentIndex(),a,b,c)) return;
		}
	}
	
	// Set the spacegroup
	// qDebug("CrystalDialog::getCrystal() : SpacegroupBox Index = %d",ui.SpacegroupBox->currentIndex());
	
	candidate.setSpaceGroup(SpacegroupBoxIndex[ui.SpacegroupBox->currentIndex()]);
	if(!candidate.spaceGroupGenerate()) return;
	
	candidate.setName(ui.DescriptionTextEdit->toPlainText().toUtf8().constData());
    *crystal = candidate;
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
	const spacegroup &group = spacegroups[SpacegroupBoxIndex[ui.SpacegroupBox->currentIndex()]];
	if(!matchesCell(group, cell)) {
		return tr("The unit-cell lengths and angles do not match the selected space-group setting.");
	}
	Crystal candidate;
    candidate.setLattice(cell[0],cell[1],cell[2],cell[3],cell[4],cell[5]);
	candidate.setSpaceGroup(SpacegroupBoxIndex[ui.SpacegroupBox->currentIndex()]);
	int atoms = 0;
	for(int row=0; row<ui.AtomsTable->rowCount(); ++row){
		QComboBox *box = qobject_cast<QComboBox *>(ui.AtomsTable->cellWidget(row,0));
		if(box->currentIndex() <= 0) continue;
		if(++atoms > candidate.maxRootAtoms())
			return tr("Too many atom sites for this space group (maximum %1 sites).")
				.arg(candidate.maxRootAtoms());
        double position[3];
        for(int column=1; column<4; ++column){
            bool ok = false;
            const auto item = ui.AtomsTable->item(row,column);
            position[column-1] = item ? item->text().toDouble(&ok) : 0;
            if(!ok || !std::isfinite(position[column-1]))
                return tr("Enter valid fractional coordinates for atom %1.").arg(row+1);
        }
        if(!candidate.addAtom(box->currentIndex(),position[0],position[1],position[2]))
            return tr("Atom %1 cannot be represented in this structure.").arg(row+1);
    }
    if(!candidate.spaceGroupGenerate())
        return tr("The symmetry-expanded structure exceeds QLaue's atom limit.");
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
    if(!state) return;
    if(ui.OrthorhombicButton->isChecked()) setSpacegroupBox(Crystal::Orthorhombic);
    else if(ui.TetragonalButton->isChecked()) setSpacegroupBox(Crystal::Tetragonal);
    else if(ui.CubicButton->isChecked()) setSpacegroupBox(Crystal::Cubic);
    else if(ui.MonoclinicButton->isChecked()) setSpacegroupBox(Crystal::Monoclinic);
    else if(ui.HexagonalButton->isChecked()) setSpacegroupBox(Crystal::Hexagonal);
    else if(ui.TriclinicButton->isChecked()) setSpacegroupBox(Crystal::Triclinic);
    else if(ui.TrigonalButton->isChecked()) setSpacegroupBox(Crystal::Trigonal);
    else setSpacegroupBox(-1);
}

void CrystalDialog::syncLatticeConstraints(){
    QLineEdit *fields[] = {ui.LatticeA, ui.LatticeB, ui.LatticeC,
                          ui.LatticeAlpha, ui.LatticeBeta, ui.LatticeGamma};
    for(QLineEdit *field : fields) field->setEnabled(true);
    // The unfiltered editor preserves the user's cell, including imported
    // nonstandard settings. Validation checks it without silently correcting it.
    if(ui.AllSpaceGroupsButton->isChecked() || ui.SpacegroupBox->currentIndex() < 0) return;

    const spacegroup &group = spacegroups[SpacegroupBoxIndex[ui.SpacegroupBox->currentIndex()]];
    const int type = group.XtalSystem;
    const bool rhombohedral = type == Crystal::Trigonal && !strcmp(group.Extn, "R");
    const bool hexagonal = type == Crystal::Hexagonal || (type == Crystal::Trigonal && !rhombohedral);
    const bool equalAB = type == Crystal::Cubic || type == Crystal::Tetragonal || hexagonal || rhombohedral;
    if(equalAB) {
        ui.LatticeB->setText(ui.LatticeA->text());
        ui.LatticeB->setEnabled(false);
    }
    if(type == Crystal::Cubic || rhombohedral) {
        ui.LatticeC->setText(ui.LatticeA->text());
        ui.LatticeC->setEnabled(false);
    }

    // Updating a fixed alpha must not reenter while the other angles are set.
    const QSignalBlocker alphaBlocker(ui.LatticeAlpha);
    if(rhombohedral) {
        ui.LatticeBeta->setText(ui.LatticeAlpha->text());
        ui.LatticeGamma->setText(ui.LatticeAlpha->text());
        ui.LatticeBeta->setEnabled(false);
        ui.LatticeGamma->setEnabled(false);
    } else if(type != Crystal::Triclinic) {
        const QString setting = QString::fromLatin1(group.Extn);
        for(int angle=0; angle<3; ++angle) {
            const bool freeAngle = type == Crystal::Monoclinic && setting.contains(QChar('a' + angle));
            if(!freeAngle) fields[angle+3]->setText(hexagonal && angle == 2 ? "120" : "90");
            fields[angle+3]->setEnabled(freeAngle);
        }
    }
}

void CrystalDialog::setSpacegroupBox(int crystaltype){
    const int previous = ui.SpacegroupBox->currentIndex() < 0 ? -1
        : SpacegroupBoxIndex[ui.SpacegroupBox->currentIndex()];
    int selected = 0;
	QStringList sgroups;
	int sgnum = 0;
	int index = 0;
	
	while(spacegroups[sgnum].number != 0){
		if((spacegroups[sgnum].XtalSystem == crystaltype) ||
		   (crystaltype == -1)){
			if(sgnum == previous) selected = index;
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
	
    {
        const QSignalBlocker groupBlocker(ui.SpacegroupBox);
        ui.SpacegroupBox->clear();
        ui.SpacegroupBox->insertItems(0,sgroups);
        ui.SpacegroupBox->setCurrentIndex(selected);
    }
    syncLatticeConstraints();
}
