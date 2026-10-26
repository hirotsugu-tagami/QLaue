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

	Version 1.0
*/

//#include <math.h>
#include <iostream>
#include <fstream>
#include <stdio.h>
#include <string.h>
#include <cmath>
#include "matrix.h"
#include "atom.h"
#include "crystal.h"
#include "spacegroup.h"
#include "xray.h"

Matrix Crystal::getGenerator(spacegroup *sg, int gennum) {
	Matrix gen(3,4);
	// The table stores a row-major 3x3 rotation followed by three translations.
	for(int r=0; r<3; ++r){
		for(int c=0; c<3; ++c)
			gen.Set(r,c,sg->matrix[gennum][r*3+c]);
		gen.Set(r,3,(double)sg->matrix[gennum][9+r] / Crystal::STBF);
	}
	return gen;
}

Crystal::Crystal(void): 
B(3,3), Binv(3,3), 
U(3,3), Uinv(3,3),
UB(3,3), UBinv(3,3),
R(3,3), Rinv(3,3),
name(NULL)
{
	atoms = new Atom[MaxAtoms];
	natoms = 0;
	natoms_sggen = 0;
	sg = &spacegroups[0];

	freeRotate = true;
	gonioX = gonioY = gonioZ = 0;
	
	U.identity();
	R.identity();
	Rinv.identity();

	setLattice(5.4,5.4,5.4,M_PI / 2, M_PI/2, M_PI/2);
	return;
}

Crystal::Crystal(const Crystal& C):
B(3,3), Binv(3,3), 
U(3,3), Uinv(3,3),
UB(3,3), UBinv(3,3),
R(3,3), Rinv(3,3),
name(NULL)
{
	atoms = new Atom[MaxAtoms];
	natoms = C.natoms;
	natoms_sggen = C.natoms_sggen;
	
	// Copy the atoms across
	for(int i=0;i<(C.natoms+C.natoms_sggen);i++)
		atoms[i] = C.atoms[i];
	
	// Now the name
	
	setName(C.name);
	
	// Lattice parameters
	
	a = C.a;
	b = C.b;
	c = C.c;
	alpha = C.alpha;
	beta = C.beta;
	gamma = C.gamma;
	
	as = C.as;
	bs = C.bs;
	cs = C.cs;
	alphas = C.alphas;
	betas = C.betas;
	gammas = C.gammas;
	
	sinalphas = C.sinalphas;
	sinbetas = C.sinbetas;
	singammas = C.singammas;
	
	cosalphas = C.cosalphas;
	cosbetas = C.cosbetas;
	cosgammas = C.cosgammas;
	
	volume = C.volume;
	volumes = C.volumes;
	
	sg = C.sg;
	
	freeRotate = C.freeRotate;
	gonioX = C.gonioX;
	gonioY = C.gonioY;
	gonioZ = C.gonioZ;
	
	B = C.B;
	Binv = C.Binv;	
	U = C.U;
	Uinv = C.Uinv;
	UB = C.UB;
	UBinv = C.UBinv;
	R = C.R;
	Rinv = C.Rinv;
}

Crystal::~Crystal(void) {
	delete [] atoms;
	if(name != NULL)
		delete [] name;
	return;
}

Crystal& Crystal::operator=(const Crystal &C) {
	if(this == &C)
		return *this;
	// Copy the atoms across
	
	natoms = C.natoms;
	natoms_sggen = C.natoms_sggen;
	
	for(int i=0;i<(C.natoms+C.natoms_sggen);i++)
		atoms[i] = C.atoms[i];
	
	// Now the name
	
	setName(C.name);
	
	// Lattice parameters
	
	a = C.a;
	b = C.b;
	c = C.c;
	alpha = C.alpha;
	beta = C.beta;
	gamma = C.gamma;
	
	as = C.as;
	bs = C.bs;
	cs = C.cs;
	alphas = C.alphas;
	betas = C.betas;
	gammas = C.gammas;
	
	sinalphas = C.sinalphas;
	sinbetas = C.sinbetas;
	singammas = C.singammas;
	
	cosalphas = C.cosalphas;
	cosbetas = C.cosbetas;
	cosgammas = C.cosgammas;
	
	volume = C.volume;
	volumes = C.volumes;
	
	sg = C.sg;
	
	freeRotate = C.freeRotate;
	gonioX = C.gonioX;
	gonioY = C.gonioY;
	gonioZ = C.gonioZ;
	
	B = C.B;
	Binv = C.Binv;
	U = C.U;
	Uinv = C.Uinv;
	UB = C.UB;
	UBinv = C.UBinv;
	R = C.R;
	Rinv = C.Rinv;
	return *this;
}

int Crystal::getLatticeType(void){
	//return lattice_type;
	return 0;
}

void Crystal::calc(void) {
	calcRlattice();
	calcUB();
}

void Crystal::setLattice(double newa,  double newb, double newc,
				double newalpha, double newbeta, double newgamma){
	a = newa;
	b = newb;
	c = newc;
	
	alpha = newalpha;
	beta = newbeta;
	gamma = newgamma;
	
	calcRlattice();
	calcUB();
}

bool Crystal::isValidLattice(double a, double b, double c,
							 double alpha, double beta, double gamma){
	if(!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c) ||
	   !std::isfinite(alpha) || !std::isfinite(beta) || !std::isfinite(gamma) ||
	   a <= 0 || b <= 0 || c <= 0 || !std::isfinite(a*b*c) || a*b*c <= 0 ||
	   alpha <= 0 || alpha >= M_PI || beta <= 0 || beta >= M_PI ||
	   gamma <= 0 || gamma >= M_PI)
		return false;
	double ca = cos(alpha), cb = cos(beta), cg = cos(gamma);
	return 1 - ca*ca - cb*cb - cg*cg + 2*ca*cb*cg > 1e-12;
}

int Crystal::maxRootAtoms() const {
	// Conservative bound: reserve space for every symmetry equivalent site.
	int centering = 1;
	switch(sg->LatticeType){
		case 'F': centering = 4; break;
		case 'R': centering = 3; break;
		case 'A': case 'B': case 'C': case 'I': centering = 2; break;
	}
	return MaxAtoms / (sg->ngenerators * centering);
}

double Crystal::getLatticeA(void){
	return a;
}

double Crystal::getLatticeB(void){
	return b;
}

double Crystal::getLatticeC(void){
	return c;
}

double Crystal::getLatticeAlpha(void){
	return alpha;
}

double Crystal::getLatticeBeta(void){
	return beta;
}

double Crystal::getLatticeGamma(void){
	return gamma;
}

int Crystal::getNAtoms(void){
	return natoms + natoms_sggen;
}

int Crystal::getNRootAtoms(void){
	return natoms;
}

void Crystal::delAllAtoms(void){
	natoms = 0;
	natoms_sggen = 0;
}

Atom * Crystal::getAtom(int i){
	return &(atoms[i]);
}

Matrix Crystal::getAtom4(int i){
	Matrix pos(4,1);
	pos.Set(0,0,atoms[i].getXPos() - std::floor(atoms[i].getXPos()));
	pos.Set(1,0,atoms[i].getYPos() - std::floor(atoms[i].getYPos()));
	pos.Set(2,0,atoms[i].getZPos() - std::floor(atoms[i].getZPos()));
	pos.Set(3,0,1.0);
	return pos;
}

void Crystal::calcRlattice(void) {

	double abc = a*b*c;
	
	volume = sqrt( 1 - pow(cos(alpha),2) 
				  - pow(cos(beta),2) - pow(cos(gamma),2) 
				  + (2*cos(alpha)*cos(beta)*cos(gamma)));
	
	as = /*2 * M_PI * */sin(alpha) / ( a * volume);
	bs = /*2 * M_PI * */sin(beta) / ( b * volume);
	cs = /*2 * M_PI * */sin(gamma) / ( c * volume);
	
	sinalphas = (volume / (sin(beta) * sin(gamma)));
	sinbetas = (volume / (sin(alpha) * sin(gamma)));
	singammas = (volume / (sin(alpha) * sin(beta)));
	
	cosalphas = (cos(beta)*cos(gamma) - cos(alpha)) / (sin(beta) * sin(gamma));
	cosbetas = (cos(alpha)*cos(gamma) - cos(beta)) / (sin(alpha) * sin(gamma));
	cosgammas = (cos(alpha)*cos(beta) - cos(gamma)) / (sin(alpha) * sin(beta));
	alphas = atan2(sinalphas, cosalphas);
	betas = atan2(sinbetas, cosbetas);
	gammas = atan2(singammas, cosgammas);
	
	volume*=abc;
	
	volumes = /*2 * M_PI*/ 1 / volume;
}

void Crystal::calcUB(void){
	B.Set(0,0,as);
	B.Set(1,0,0);
	B.Set(2,0,0);
	
	B.Set(0,1,bs * cosgammas);
	B.Set(1,1,bs * singammas);
	B.Set(2,1,0);
	
	B.Set(0,2,cs * cosbetas);
	B.Set(1,2,-1 * cs * sinbetas * cos(alpha));
	B.Set(2,2,/*2 * M_PI*/ 1 / c);
	
	Binv = B.inv();
	Uinv = U.transpose();
	UB = U * B;
	UBinv = Binv * Uinv;
}

void Crystal::setU(const Matrix newU){
	U = newU;
	// Zero the rotations and R matrix
	gonioX = 0; gonioY = 0; gonioZ = 0;
	R.identity() ; Rinv.identity();
	calcUB();
}

namespace {
bool normalizeDirection(Matrix &v) {
    if(v.Rows() != 3 || v.Cols() != 1) return false;
    double scale = 0;
    for(int i=0;i<3;++i) {
        if(!std::isfinite(v.Get(i,0))) return false;
        scale = std::max(scale, std::abs(v.Get(i,0)));
    }
    if(scale == 0) return false;
    v = v / scale;
    v = v / v.mag();
    return true;
}
bool orientationFrame(Matrix first, Matrix second, Matrix &frame) {
    if(!normalizeDirection(first) || !normalizeDirection(second)) return false;
    Matrix third = first ^ second;
    if(third.mag() < 1e-10 || !normalizeDirection(third)) return false;
    second = third ^ first;
    for(int i=0;i<3;++i) {
        frame.Set(i,0,first.Get(i,0));
        frame.Set(i,1,second.Get(i,0));
        frame.Set(i,2,third.Get(i,0));
    }
    return true;
}
double periodic(double value) { return value - std::floor(value); }
Atom periodicAtom(Atom atom) {
    atom.setPos(periodic(atom.getXPos()),periodic(atom.getYPos()),periodic(atom.getZPos()));
    return atom;
}
}

bool Crystal::setU(double h1, double k1, double l1, double h2, double k2, double l2) {
    return setU(Matrix(h1,k1,l1),Matrix(h2,k2,l2));
}

bool Crystal::setU(Matrix Primary, Matrix Secondary) {
    // Normalize before transforming as well, to avoid overflowing B * extreme input.
    if(!normalizeDirection(Primary) || !normalizeDirection(Secondary)) return false;
    Matrix frame(3,3);
    if(!orientationFrame(B*Primary, B*Secondary, frame)) return false;
    Matrix target(1,0,0, 0,0,-1, 0,1,0);
    setU(target * frame.transpose());
    return true;
}

bool Crystal::setU(Matrix Primary, Matrix Secondary, Matrix PrimaryHKL, Matrix SecondaryHKL) {
    if(!normalizeDirection(PrimaryHKL) || !normalizeDirection(SecondaryHKL)) return false;
    Matrix crystalFrame(3,3), detectorFrame(3,3);
    if(!orientationFrame(B*PrimaryHKL,B*SecondaryHKL,crystalFrame) ||
       !orientationFrame(Primary,Secondary,detectorFrame)) return false;
    setU(detectorFrame * crystalFrame.transpose());
    return true;
}

Matrix Crystal::toQ(double x, double y, double z){
	Matrix miller(3,1);
	Matrix Q(3,1);
	
	miller.Set(0,0,x);
	miller.Set(1,0,y);
	miller.Set(2,0,z);
	
	Q = B * miller;
	return Q;
}

bool Crystal::addAtom(int zn, double x, double y, double z) {
	if(natoms >= MaxAtoms || zn < 1 || zn > 98 ||
	   !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
	natoms_sggen = 0;
	atoms[natoms].setPos(x,y,z);
	atoms[natoms].setZ(zn);
	atoms[natoms].setRootAtom(-1);
	natoms++;
	return true;
}

complex<double> Crystal::calcXSFactor(int h, int k, int l, double sinthoverlambda, 
									  double lambda){
	return Crystal::calcXSFactor((double)h, (double)k, (double)l, sinthoverlambda, lambda);
}

complex<double> Crystal::calcXSFactor(double h, double k, double l, double sinthoverlambda, 
									  double lambda){
	complex<double> sfactor(0,0);
	for(int i=0;i<(natoms + natoms_sggen);i++){
		complex<double> f1f2 = XRay::getXf1f2(atoms[i].getZ(), lambda);
		//cerr << "f1f2 = " << f1f2 << endl;
		double f0 = XRay::getXf0(atoms[i].getZ(), sinthoverlambda);
		double f0_0 = XRay::getXf0(atoms[i].getZ(), 0);
		//cerr << f0_0 << endl;
		sfactor = sfactor + (periodicAtom(atoms[i]).getPhase(h,k,l) * (f1f2 + f0 - f0_0));
		//sfactor = sfactor + (periodicAtom(atoms[i]).getPhase(h,k,l) * f0_0);
	}
	//cerr << "(" << h << " " << k << " " << l <<") sfactor = " << sfactor << endl; 
	return sfactor;
}

bool Crystal::isForbidden(double h, double k, double l){
	complex<double> sfactor(0,0);
	for(int i=0;i<(natoms + natoms_sggen);i++){
		double f0_0 = XRay::getXf0(atoms[i].getZ(), 0);
		sfactor = sfactor + (periodicAtom(atoms[i]).getPhase(h,k,l) * f0_0);
	}
	
	double inten = (sfactor * conj(sfactor)).real();
	
	if(inten < 0.00001)
		return true;
	else
		return false;
}

Matrix Crystal::getB(void){
	return B;
}

Matrix Crystal::getBinv(void){
	return Binv;
}

Matrix Crystal::getU(void){
	return R * U;
}

Matrix Crystal::getUinv(void){
	return Uinv * Rinv;
}

Matrix Crystal::getUB(void){
	return R * UB;
}

Matrix Crystal::getUBunrotated(void){
	return UB;
}

Matrix Crystal::getUBinv(void){
	return UBinv * Rinv;
}

// Rotation routines

void Crystal::setFreeRotate(bool state){
	freeRotate = state;

	if(freeRotate){
		// Free rotate so rotate the actual U Matrix
		// by the current gonio angles
		Matrix X(3,3),Y(3,3),Z(3,3);
		X = X.rotateX(-gonioX);
		Y = Y.rotateY(-gonioY);
		Z = Z.rotateZ(-gonioZ);
		gonioX = 0; gonioY = 0; gonioZ = 0;
		R.identity() ; Rinv.identity();
		U = Z * Y * X * U;
		calcUB();
	}
}

void Crystal::rotateBy(double a, double b, double c){
	if(freeRotate) {
		Matrix X(3,3),Y(3,3),Z(3,3);
		X = X.rotateX(-a);
		Y = Y.rotateY(-b);
		Z = Z.rotateZ(-c);
		U = Z* Y * X * U;
		calcUB();
	} else {
		gonioX = gonioX + a; 
		gonioY = gonioY + b; 
		gonioZ = gonioZ + c; 
		setGoniometer();
	}
}

void Crystal::rotateTo(double a, double b, double c){
	if(freeRotate){
		Matrix X(3,3),Y(3,3),Z(3,3);
		X = X.rotateX(-a);
		Y = Y.rotateY(-b);
		Z = Z.rotateZ(-c);
		cerr << "Start U " << endl << U << endl;
		U = Z * Y * X * U;
		cerr << "RotTo = " << a << "," << b << "," << c << endl;
		cerr << "End U " << endl << U << endl;
		calcUB();
	} else {
		gonioX = a; 
		gonioY = b; 
		gonioZ = c; 
		setGoniometer();
	}
}

void Crystal::setGoniometer(void){
	Matrix X(3,3),Y(3,3),Z(3,3);
	X = X.rotateX(-gonioX);
	Y = Y.rotateY(-gonioY);
	Z = Z.rotateZ(-gonioZ);
	
	R = Z * Y * X;
	Rinv = R.transpose();
}



void Crystal::rotateAboutBy(Matrix axis, double angle){
	// First we need to make the Tphi matricies from
	Matrix R(3,3), Tc(3,3);
	
	Matrix axis2(3,1), Tc2(3,1), Tc3(3,1);
	
	axis = axis / axis.mag();
	
	// choose either x or y axis
	// (whichever is greater in angle)
	
	if(axis.dot(Matrix(0,1,0)) > axis.dot(Matrix(0,0,1)))
		axis2 = Matrix(0,0,1);
	else
		axis2 = Matrix(0,1,0);
	 
	Tc3 = axis ^ axis2;
	Tc3 = Tc3 / Tc3.mag();
	Tc2 = Tc3 ^ axis;
	Tc2 = Tc2 / Tc2.mag();
										
	for(int i = 0 ; i < 3 ; i++ ){
		// Load up the matricies Tc and Td;
		Tc.Set(i,0,axis.Get(i,0));
		Tc.Set(i,1,Tc2.Get(i,0));
		Tc.Set(i,2,Tc3.Get(i,0));
	}
			
	Matrix X(3,3);
	X.identity();
	X = X.rotateX(-angle);
	
	R.identity();
	R = Tc * X * Tc.transpose();

	U = R * U;
	calcUB();
}

// ***************************************************************************************

spacegroup* Crystal::getSpaceGroup(void){
	return sg;
}

bool Crystal::setSpaceGroupNum(const int sgnum) {
    for(spacegroup *candidate=spacegroups; candidate->Name; ++candidate)
        if(candidate->number==sgnum) { sg=candidate; return true; }
    return false;
}

void Crystal::setSpaceGroup(const int sgnum) {
    if(sgnum < 0) return;
    for(int i=0;spacegroups[i].Name;++i)
        if(i==sgnum) { sg=&spacegroups[i]; return; }
}

bool Crystal::setSpaceGroup(const char *name) {
    if(!name) return false;
    for(spacegroup *candidate=spacegroups; candidate->Name; ++candidate)
        if(!strcmp(name,candidate->HallName)) { sg=candidate; return true; }
    return false;
}

bool Crystal::setSpaceGroupIUC(const char *name) {
    if(!name) return false;
    for(spacegroup *candidate=spacegroups; candidate->Name; ++candidate)
        if(!strcmp(name,candidate->Name)) { sg=candidate; return true; }
    return false;
}

bool Crystal::spaceGroupGenerate(void) {
    natoms_sggen = 0;
    if(!sg || !sg->Name || sg->ngenerators < 0 || sg->ngenerators > 192) return false;
    for(int i=0;i<natoms;++i) {
        if(atoms[i].getZ() < 1 || atoms[i].getZ() > 98 ||
           !std::isfinite(atoms[i].getXPos()) || !std::isfinite(atoms[i].getYPos()) ||
           !std::isfinite(atoms[i].getZPos())) return false;
    }
    if(sgTransform()) return true;
    natoms_sggen = 0;
    return false;
}

bool Crystal::sgTransform(void) {
    for(int n=0;n<sg->ngenerators;++n) {
        const Matrix op = getGenerator(sg,n);
        for(int a=0;a<natoms;++a) {
            const int inserted = sgInsertAtom(atoms[a],op * getAtom4(a));
            if(inserted < 0) return false;
            if(inserted > 0) atoms[natoms + natoms_sggen - 1].setRootAtom(a);
        }
    }
    switch(sg->LatticeType) {
        case 'P': return true;
        case 'I': return sgBodyCenter();
        case 'F': return sgFaceCenter();
        case 'A': return sgACenter();
        case 'B': return sgBCenter();
        case 'C': return sgCCenter();
        case 'R': return sgRhomb();
        default: return false;
    }
}

int Crystal::sgInsertAtom(Atom a, const Matrix pos){
	// Keep source coordinates as entered; wrap only generated equivalents.
	Matrix newpos(pos);
	for(int i=0;i<3;i++) {
		if(!std::isfinite(newpos.Get(i,0))) return -1;
		newpos.Set(i,0,periodic(newpos.Get(i,0)));
	}
	
	// Is there an atom already in this place ?
	// Define an accuracy based on the lattice parameter
	
	double xacc = (0.25 / getLatticeA());
	double yacc = (0.25 / getLatticeB());
	double zacc = (0.25 / getLatticeC());
	
	for(int n=0;n<(natoms + natoms_sggen);++n){
		// Positions separated by integer cell translations describe the same site.
		double dx = newpos.Get(0,0) - periodic(atoms[n].getXPos());
		double dy = newpos.Get(1,0) - periodic(atoms[n].getYPos());
		double dz = newpos.Get(2,0) - periodic(atoms[n].getZPos());
		if((std::abs(dx - std::round(dx)) < xacc) &&
		   (std::abs(dy - std::round(dy)) < yacc) &&
		   (std::abs(dz - std::round(dz)) < zacc)) {
			//cerr << "Crystal::sgInsertAtom() : Atom located at this pos" << endl;
				return false;
		}
	}
	
	// Ok so we can insert an atom in this location
	
	if(natoms + natoms_sggen >= MaxAtoms || a.getZ() < 1 || a.getZ() > 98) return -1;
	atoms[natoms + natoms_sggen] = a;
	atoms[natoms + natoms_sggen].setPos(newpos);
	natoms_sggen++;
	return true;
}

bool Crystal::sgFaceCenter(void){
	Matrix v1(0.5, 0.5, 0);
	Matrix v2(0, 0.5, 0.5);
	Matrix v3(0.5, 0, 0.5);
	return sgApplyCenter(v1) && sgApplyCenter(v2) && sgApplyCenter(v3);
}

bool Crystal::sgBodyCenter(void){
	Matrix v(0.5, 0.5, 0.5);
	return sgApplyCenter(v);
}

bool Crystal::sgACenter(void){
	Matrix v(0, 0.5, 0.5);
	return sgApplyCenter(v);
}

bool Crystal::sgBCenter(void){
	Matrix v(0.5, 0, 0.5);
	return sgApplyCenter(v);
}

bool Crystal::sgCCenter(void){
	Matrix v(0.5, 0.5, 0);
	return sgApplyCenter(v);
}

bool Crystal::sgRhomb(void){
	Matrix v1(2.0/3.0, 1.0/3.0,1.0/3.0);
	Matrix v2(1.0/3.0, 2.0/3.0,2.0/3.0);
	return sgApplyCenter(v1) && sgApplyCenter(v2);
}

bool Crystal::sgApplyCenter(Matrix v){
	int max = (natoms + natoms_sggen);
	for(int n=0;n<max;n++){
		Atom source = periodicAtom(atoms[n]);
		if(sgInsertAtom(source,Matrix(source.getXPos(),source.getYPos(),source.getZPos()) + v) < 0) return false;
	}
	return true;
}

// ***************************************************************************************

void Crystal::setName(const char *txt){
	char *replacement = NULL;
	if(txt != NULL){
		replacement = new char[strlen(txt) + 1];
		strcpy(replacement,txt);
	}
	delete [] name;
	name = replacement;
}

char* Crystal::getName(void){
	return name;
}

// ***************************************************************************************

// Calculate absorption (mu) 

double Crystal::xRayAbsorptionCoeff(double lambda){
	double mu = 0;
	for(int i=0; i<(natoms + natoms_sggen); ++i){
		complex<double> f1f2 = XRay::getXf1f2(atoms[i].getZ(), lambda);
		mu = mu + f1f2.imag();
	}
	
	mu = mu * -2.0 * lambda * 1e-10 * R_ZERO / (volume / 1e30);
	//cerr << "mu crystal = " << mu << endl;
	return mu;
}
