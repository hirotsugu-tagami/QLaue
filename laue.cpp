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

#include <QApplication>
#include <algorithm>
#include <cmath>
#include <limits>
using namespace std;

#include "matrix.h"
#include "atom.h"
#include "crystal.h"
#include "laue.h"
#include <climits>
#include "xray.h"

LaueSpot::LaueSpot(void) : x(0), y(0), polarX(0), polarY(0), h(0), k(0), l(0),
    theta(0), phi(0), inten(0), detInten(0), multiplicity(0), fromCharateristic(false) {}

double LaueSpot::H(void){
	return h;
}

double LaueSpot::K(void){
	return k;
}

double LaueSpot::L(void){
	return l;
}

double LaueSpot::X(void){
	return x;
}

double LaueSpot::Y(void){
	return y;
}

double LaueSpot::I(void){
	return inten;
}

double LaueSpot::detI(void){
	return detInten;
}

void LaueSpot::setI(double i){
	inten = i;
}

void LaueSpot::setDetI(double i){
	detInten = i;
}

int LaueSpot::getMaxdo(void){
	return multiplicity;
}

void LaueSpot::setXYIN(double xx, double yy, double ii, int n){
	x = xx; y = yy;
	inten = ii;
	multiplicity = n;
}

void LaueSpot::setHKL(double hh, double kk, double ll){
	h = hh; k = kk; l = ll;
}

LaueOrientations::LaueOrientations():
U(3,3), P(3,1), S(3,1)
{
	U.identity();
	P.identity();
	S.identity();
	numMatches = 0;
	patternError = 0;
}

LaueOrientations::LaueOrientations(const LaueOrientations& L):
U(3,3), P(3,1), S(3,1)
{
	U = L.U;
	P = L.P;
	S = L.S;
	numMatches = L.numMatches;
	patternError = L.patternError;
}

lauegram::lauegram() { init(); }
lauegram::lauegram(const lauegram& L) = default;

void lauegram::init(void) {
	spot.clear();
	
	nspots = 0;	
	fsDistance = 35; // mm
	
	debug = 0;
	
	// Set the K vector range to zero (no spots)
	
	minKvec = 0;
	maxKvec = 0;
	
	// Intensity threshold (to stop rounding errors)
	
	inten_thresh = 1; // Electrons
	
	// Intensity modifiers
	
	setIntensityCorrection(1023);
	
	// Detector Levels
	
	detlevel = 0.02;
	detsaturation = 0.45;

	calc_precision = 5;
	
	tube_z = XRay::elementNumber("Cu");
	
	calc_intensities = true;
	
	// Maximum 
	
	indexingMaxH = 4;
	indexingMaxK = 4;
	indexingMaxL = 4;
	
	indexingSpotTol = 5;
	indexingAngularTol = 1.5 * 4 * atan(1) / 180;
}

lauegram::~lauegram() = default;
lauegram& lauegram::operator=(const lauegram &L) = default;

void lauegram::copySpotsFrom(const lauegram &other) {
    spot = other.spot;
    nspots = spot.size();
}

void lauegram::setDet(double dl, double ds){
	detlevel = dl;
	detsaturation = ds;
}

double lauegram::getDetLevel(void){
	return detlevel;
}

void lauegram::setDetLevel(double dl){
	detlevel = dl;
}

double lauegram::getDetSatLevel(void){
	return detsaturation;
}

void lauegram::setDetSatLevel(double ds){
	detsaturation = ds;
}

double lauegram::detLevel(void){
	return detlevel;
}

double lauegram::detSaturation(void){
	return detsaturation;
}

void lauegram::setDebug(int value){
	debug = value;
}

void lauegram::setDistance(double d){
	if(std::isfinite(d) && d > 0) fsDistance = d;
}

double lauegram::getDistance(void) {
	return fsDistance;
}

int lauegram::getNspots(void) {
	return nspots;
}

double lauegram::maxSpotInten(void){
	double maxinten = 0;
	
	for(int i = 0; i < nspots ; i++){
		if(maxinten < spot[i].I())
			maxinten = spot[i].I();
	}
	
	return maxinten;
}

double lauegram::maxSpotInten(double minx, double maxx, double miny, double maxy){
	double maxinten = 0;
	
	for(int i = 0; i < nspots ; i++){
		if((spot[i].X() >= minx) && (spot[i].X() <= maxx) &&
		   (spot[i].Y() >= miny) && (spot[i].Y() <= maxy)){
			if(maxinten < spot[i].I()){
				maxinten = spot[i].I();
			}
		}
	}
	
	return maxinten;
}

void lauegram::setLambdaRange(double min, double max){
	if(!std::isfinite(min) || !std::isfinite(max) || min <= 0 || max < min ||
	   !std::isfinite(1/min) || 1/max <= 0) return;
	minKvec = 1 / max;
	maxKvec = 1 / min;
}

double lauegram::getLambdaRangeMax(void){
	return 1 / minKvec;
}

double lauegram::getLambdaRangeMin(void){
	return 1 / maxKvec;
}

bool lauegram::calc(Crystal *crystal, const std::function<bool(int)> &progress) {
    spot.clear();
    nspots = 0;
    if(!crystal || calc_precision <= 0 || !std::isfinite(fsDistance) || fsDistance <= 0 ||
       !std::isfinite(minKvec) || minKvec <= 0 || !std::isfinite(maxKvec) || maxKvec < minKvec)
        return false;
    const double bounds[] = {2*maxKvec*crystal->getLatticeA()/calc_precision,
                             2*maxKvec*crystal->getLatticeB()/calc_precision,
                             2*maxKvec*crystal->getLatticeC()/calc_precision};
    for(double bound : bounds)
        if(!std::isfinite(bound) || bound < 0 || bound >= INT_MAX) return false;
    const int maxH = int(bounds[0]), maxK = int(bounds[1]), maxL = int(bounds[2]);
    for(int h=0; h<=maxH; ++h) {
        for(int k=0; k<=maxK; ++k) {
            if(progress && progress(int(100.0 * (h + double(k)/(maxK+1.0))/(maxH+1.0))))
                return false;
            for(int l=0; l<=maxL; ++l) {
                if((l & 63) == 0 && progress &&
                   progress(int(100.0*(h+(k+double(l)/(maxL+1.0))/(maxK+1.0))/(maxH+1.0))))
                    return false;
                const int mask = (h == 0 ? 1 : 0) | (k == 0 ? 2 : 0) | (l == 0 ? 4 : 0);
                for(int n=0; n<8; ++n) {
                    if(n & mask) continue;
                    LaueSpot candidate;
                    if(calcHKL(crystal, &candidate, h*(n&1 ? -1 : 1),
                               k*(n&2 ? -1 : 1), l*(n&4 ? -1 : 1))) {
                        spot.append(candidate);
                        nspots = spot.size();
                    }
                }
            }
        }
    }
    return !progress || !progress(100);
}

bool lauegram::isFundamental(int h, int k, int l){
	if(h == 0 && k == 0 && l == 0) return false;
	int maxdiv = abs(h);
	
	if( abs(k) > maxdiv )
		maxdiv = abs(k);
	if( abs(l) > maxdiv )
		maxdiv = abs(l);
	
	if(debug)
		cerr << "maxdiv = " << maxdiv << endl;
	
	for(int n = 2; n<=maxdiv ; n++){
		// Divide by n and see if we get integers!
		if((((double)h / n) == int((double)h / n)) && (((double)k / n) == int((double)k / n)) && (((double)l / n) == int((double)l / n))){
			if(debug)
				cerr << "Not a fundimental spot" << endl;
			return false;
		}
	}
	
	return true;
}
	
bool lauegram::calcHKL(Crystal *crystal, LaueSpot *spot, int h, int k, int l, bool positionsOnly){
	if(!crystal || !spot || !std::isfinite(fsDistance) || fsDistance <= 0 ||
	   !std::isfinite(maxKvec) || !std::isfinite(minKvec) || minKvec <= 0 || maxKvec < minKvec)
		return false;
	Matrix G(3,1);
	Matrix R(3,1);
	Matrix kpri(3,1);
	double xx,yy;
	
	//debug = 1;
	
	if(debug){
		cerr << "----------------------------------------" << endl;
		cerr << "HKL = " << h << "," << k << "," << l << endl;
	}	
	
	// Check if we have a fundimental spot or not
	
	if((abs(h) + abs(k) + abs(l)) == 0){
		if(debug){
			cerr << "ORIGIN" << endl;
		}
		return false;
	}
	
	if(!isFundamental(h,k,l)){
		if(debug){
			cerr << "Not a fundamental HKL" << endl;
		}
		return false;
	}
	
	R = Matrix(h,k,l);
	G = crystal->getUB() * R;
	double magG = G.mag();
	if(!std::isfinite(magG) || magG <= 0 || G.Get(0,0) <= 0) return false;
	
	if(debug){
		cerr << "G:" << endl;
		cerr << G;
		cerr << "magG == " << magG << endl;
	}
	
	// Calculate the length of the k_vector.
	// This is the energy of the spot.
	
	Matrix k_in(-1,0,0);
	double k_in_len = 0.5 * magG * magG * (k_in * -1.0).mag() / ((k_in * -1.0).dot(G));
	
	// If the k_in_len is negative then the spot is pointing in the wrong direction!
	
	if(!std::isfinite(k_in_len) || k_in_len <= 0)
		return 0;
	
	// Now calculate the k_out vector
	kpri = (G * (1/k_in_len)) + k_in;
	
	// Remove spots pointing in the wrong direction
	
	if(kpri.Get(0,0) <= 1e-12){
		return 0;
	}
	
	// Calculate the maximum diffraction order
	
	double maxdo = maxKvec / k_in_len;
	const double firstOrder = std::max(1.0, std::ceil(minKvec/k_in_len - 1e-10));
	const double lastOrder = std::floor(maxdo + 1e-10);
	if(!std::isfinite(lastOrder) || lastOrder > std::numeric_limits<int>::max() || firstOrder > lastOrder)
		return false;

	if(positionsOnly) {
		const double x = -fsDistance * kpri.Get(1,0) / kpri.Get(0,0);
		const double y = fsDistance * kpri.Get(2,0) / kpri.Get(0,0);
		if(!std::isfinite(x) || !std::isfinite(y)) return false;
		spot->setXYIN(x,y,1,int(lastOrder-firstOrder+1));
		spot->setHKL(h,k,l);
		spot->setCharacteristic(false);
		return true;
	}
	
	if(debug){
		cerr << "maxdo = " << maxdo << endl;
		cerr << "k_in_len = " << k_in_len << endl;
	}
	
	// Trap a k_vector which is too big.
	
	if(abs(maxdo) < 1){
		if(debug)
			cerr << "k-vector too big (maxdo < 1)" << endl;
		return 0;
	}
	
	double tth = kpri.dot(k_in * -1) / ((k_in * -1).mag() * kpri.mag());
	tth = M_PI - acos(std::max(-1.0, std::min(1.0, tth)));
	
	if(debug){
		if((h == 1) && ((k + l) == 0)){
			// debug for 100
			cerr << "(100) Debug" << endl;
			cerr << "G = " << endl;
			cerr << G << endl;
			cerr << "kpri = " << endl;
			cerr << kpri << endl;
		}
	}
	
	// Calculate intensity 
	
	double inten = 0;
	int ndocalc = 1;
	
	for(int n = int(firstOrder); n <= int(lastOrder); n++){
		int hh = h * n;
		int kk = k * n;
		int ll = l * n;
		double this_k_in_len = n * k_in_len;
		
		if((this_k_in_len < minKvec) && calc_intensities){
			if(debug)
				cerr << "k-vector too small" << endl;
			continue;
		}
		
		// sin(theta) / lambda is basically magG .. 
		double sinthoverlambda = magG * n / 2;
		
		if(debug)
			cerr << "sin(th) / lambda = " << sinthoverlambda << endl;
		
		complex<double> sfactor = crystal->calcXSFactor(hh,kk,ll, sinthoverlambda, 
													   1.0 / this_k_in_len);
		if(debug)
			cerr << "f = " << sfactor << endl;
		double thisinten = real(conj(sfactor) * sfactor);
		if(intensityCorrection() & IntensitySelfAbsorption)
			thisinten = thisinten * calcAbs( crystal, 1.0 / this_k_in_len , tth / 2);
		if(intensityCorrection() & IntensityXRayTube)
			thisinten = thisinten * tubeIntensity( 1.0 / this_k_in_len , HC_OVER_E * maxKvec);
		thisinten = thisinten * calcIntensityCorrection( 1.0 / this_k_in_len, tth);	
		inten = inten + thisinten;
		
		double xci = XRay::tubeCharacteristicIntensity(tube_z, 1.0 / this_k_in_len );
		if(xci > 0.3){
			if(debug){
				cerr << "Characteristic Intensity (" << hh << "," << kk << "," << ll << ") E = " << HC_OVER_E * this_k_in_len << " I = " << xci << endl;
			}
			spot->setCharacteristic(true);
		}
		
		if(debug)
			cerr << "Calculated (" << hh << "," << kk << "," << ll << ")" << endl;
		
		ndocalc++;
	}

	if(inten < inten_thresh){
		if(debug){
			cerr << "Spot Extinct (" 
			<< h << "," << k << "," << l << ")" << endl;
		}
		return 0;
	}
	
	// Make spot
	// This could be converted to calculate any arbitary film
	// location.
	
	double m = fsDistance / (kpri / kpri.mag()).dot(Matrix(1, 0, 0));
	xx = kpri.dot(Matrix(0, 1, 0)) * m * -1.0;
	yy = kpri.dot(Matrix(0, 0, 1)) * m;
	
	// Set the LaueSpot class variable to update the values
	
	spot->setXYIN(xx,yy,inten,ndocalc);
	spot->setHKL(h,k,l);
	return 1;
}

double lauegram::calcAbs(Crystal* crystal, double lambda, double th){
	double mu = crystal->xRayAbsorptionCoeff(lambda);
	double a = 1.0;
	
	if(mu != 0){
		a = cos((M_PI / 2) - th) + (cos((M_PI / 2) - th) / cos(2*((M_PI / 2) - th)));
		a = 1000.0 / (a * mu);
	} 
	
	return a;
}

void lauegram::calcInten(double minx, double maxx, double miny, double maxy){
	double maxinten = maxSpotInten(minx, maxx, miny, maxy);
	for(int i = 0; i < nspots; i++){
		double inten = spot[i].I() / (maxinten * detsaturation);
		if(inten < detlevel){
			spot[i].setDetI(0.0);
			continue;
		}
		if(inten > 1){
			spot[i].setDetI(1.0);
			continue;
		}
		spot[i].setDetI(inten);
	}
}

int lauegram::findSpot(double xx, double yy, double acc){
	for(int i = 0; i < nspots; i++){
		double x = spot[i].X();
		double y = spot[i].Y();
		if((x > (xx - acc)) && (x < (xx + acc)))
			if((y > (yy - acc)) && (y < (yy + acc)))
				// We have found the spot!
				return i;
	}
	
	// If we are here then we have not found a spot
	return -1;
}

// **********************************************************************************

// Helper routines

Matrix lauegram::filmposToVector(double x, double y){
	Matrix kpri(fsDistance, -1.0 * x, y);
	Matrix k(1,0,0);
	Matrix G(1,0,0);
	
	kpri = kpri / kpri.mag();
	
	G = k + kpri;
	G = G / G.mag();
	return G;
}

// **********************************************************************************

// Indexing routines (opperate on the lauegram class)

LaueIndexingResult lauegram::indexLaue(const Crystal &source, const QVector<QPointF> &points,
        const std::function<bool(int)> &progress) {
    LaueIndexingResult result;
    auto fail = [&](const QString &message) {
        result.error = message;
        return result;
    };
    if(points.size() < 2 || points.size() > MaxIndexingSpots)
        return fail(QString("Select between 2 and %1 spots.").arg(MaxIndexingSpots));
    if(indexingMaxH < 0 || indexingMaxH > MaxIndexingRange ||
       indexingMaxK < 0 || indexingMaxK > MaxIndexingRange ||
       indexingMaxL < 0 || indexingMaxL > MaxIndexingRange ||
       indexingMaxH + indexingMaxK + indexingMaxL == 0)
        return fail(QString("H, K and L must be between 0 and %1, with at least one nonzero range.")
                    .arg(MaxIndexingRange));
    if(!std::isfinite(indexingAngularTol) || indexingAngularTol <= 0 || indexingAngularTol >= M_PI/2 ||
       !std::isfinite(indexingSpotTol) || indexingSpotTol <= 0 || indexingSpotTol > 1000)
        return fail("Enter a spot tolerance between 0 and 1000 mm and an angular tolerance between 0 and 90 degrees (both greater than zero).");
    if(!std::isfinite(fsDistance) || fsDistance <= 0 ||
       !std::isfinite(minKvec) || !std::isfinite(maxKvec) || minKvec <= 0 || maxKvec < minKvec)
        return fail("Set a positive sample-to-detector distance and a valid wavelength range.");
    Crystal candidate(source);
    if(!Crystal::isValidLattice(candidate.getLatticeA(), candidate.getLatticeB(), candidate.getLatticeC(),
            candidate.getLatticeAlpha(), candidate.getLatticeBeta(), candidate.getLatticeGamma()))
        return fail("Set a valid crystal lattice before indexing.");
    for(int i = 0; i < points.size(); ++i) {
        if(!std::isfinite(points[i].x()) || !std::isfinite(points[i].y()))
            return fail("Spot coordinates must be finite.");
        for(int j = 0; j < i; ++j)
            if(std::hypot(points[i].x()-points[j].x(), points[i].y()-points[j].y()) < 1e-8)
                return fail("The same spot was selected more than once. Select distinct spots.");
    }
    Matrix primary = filmposToVector(points[0].x(), points[0].y());
    Matrix secondary = filmposToVector(points[1].x(), points[1].y());
    if(!std::isfinite(primary.mag()) || !std::isfinite(secondary.mag()) ||
       (primary ^ secondary).mag() < 1e-6)
        return fail("The first two spots are too close to determine an orientation. Select separated spots.");
    const double theta = std::acos(std::max(-1.0, std::min(1.0, primary.dot(secondary))));

    // Cache reciprocal directions once; (0,0,0) and harmonic duplicates do not define new spots.
    struct Reflection { int h, k, l; Matrix direction; };
    QVector<Reflection> reflections;
    Matrix b = candidate.getB();
    for(int h = -indexingMaxH; h <= indexingMaxH; ++h)
        for(int k = -indexingMaxK; k <= indexingMaxK; ++k)
            for(int l = -indexingMaxL; l <= indexingMaxL; ++l) {
                if(!isFundamental(h,k,l)) continue;
                Matrix g = b * Matrix(h,k,l);
                if(!std::isfinite(g.mag()) || g.mag() <= 0)
                    return fail("The crystal reciprocal lattice is invalid.");
                reflections.append({h,k,l,g/g.mag()});
            }
    const qint64 total = qint64(reflections.size()) * reflections.size();
    qint64 completed = 0;
    auto canceled = [&]() {
        if(progress && progress(total ? int(100*completed/total) : 100)) {
            result.canceled = true;
            result.orientations.clear();
            return true;
        }
        return false;
    };
    if(canceled()) return result;

    // Index positions only. This intentionally does not use the legacy intensity model
    // as a filter; forbidden/weak reflections and experimental intensities need review.
    const double tolerance2 = indexingSpotTol * indexingSpotTol;
    for(int p = 0; p < reflections.size(); ++p) {
        const Reflection &r1 = reflections[p];
        for(int s = 0; s < reflections.size(); ++s) {
            ++completed;
            if((completed & 127) == 0 && canceled()) return result;
            const Reflection &r2 = reflections[s];
            const double gamma = std::acos(std::max(-1.0, std::min(1.0, r1.direction.dot(r2.direction))));
            if(std::abs(gamma-theta) > indexingAngularTol || (r1.direction ^ r2.direction).mag() < 1e-6)
                continue;
            if(!candidate.setU(primary, secondary, Matrix(r1.h,r1.k,r1.l), Matrix(r2.h,r2.k,r2.l))) continue;
            LaueSpot first, second;
            if(!calcHKL(&candidate,&first,r1.h,r1.k,r1.l,true) ||
               !calcHKL(&candidate,&second,r2.h,r2.k,r2.l,true)) continue;
            auto distance2 = [](LaueSpot &spot, const QPointF &point) {
                const double dx = spot.X()-point.x(), dy = spot.Y()-point.y();
                return dx*dx+dy*dy;
            };
            double error = distance2(first,points[0]) + distance2(second,points[1]);
            if(distance2(first,points[0]) > tolerance2 || distance2(second,points[1]) > tolerance2) continue;

            // Reserve the seed reflections. Each predicted spot may explain only one observation.
            struct Match { double error; int point, reflection; };
            QVector<Match> matches;
            for(int r = 0; r < reflections.size() && points.size() > 2; ++r) {
                if((r & 127) == 0 && canceled()) return result;
                if(r == p || r == s) continue;
                const Reflection &reflection = reflections[r];
                LaueSpot spot;
                if(!calcHKL(&candidate,&spot,reflection.h,reflection.k,reflection.l,true)) continue;
                for(int i = 2; i < points.size(); ++i) {
                    const double d2 = distance2(spot,points[i]);
                    if(d2 <= tolerance2) matches.append({d2,i,r});
                }
            }
            std::sort(matches.begin(), matches.end(), [](const Match &a, const Match &b) {
                if(a.error != b.error) return a.error < b.error;
                if(a.point != b.point) return a.point < b.point;
                return a.reflection < b.reflection;
            });
            QVector<bool> usedPoints(points.size(),false), usedReflections(reflections.size(),false);
            int hits = 2;
            for(const Match &match : matches) {
                if(usedPoints[match.point] || usedReflections[match.reflection]) continue;
                usedPoints[match.point] = usedReflections[match.reflection] = true;
                error += match.error;
                ++hits;
            }
            LaueOrientations orientation;
            orientation.setU(candidate.getU());
            orientation.setP(r1.h,r1.k,r1.l);
            orientation.setS(r2.h,r2.k,r2.l);
            orientation.setNumMatches(hits);
            orientation.setPatternError(std::sqrt(error/hits));
            // ponytail: keep the best 1000 candidates in a heap; crystal-symmetry
            // reduction can replace this ceiling if larger searches need it.
            if(result.orientations.size() < MaxIndexingResults) {
                result.orientations.append(orientation);
                std::push_heap(result.orientations.begin(),result.orientations.end());
            } else {
                result.truncated = true;
                if(orientation < result.orientations.first()) {
                    std::pop_heap(result.orientations.begin(),result.orientations.end());
                    result.orientations.last() = orientation;
                    std::push_heap(result.orientations.begin(),result.orientations.end());
                }
            }
        }
    }
    if(canceled()) return result;
    std::sort(result.orientations.begin(),result.orientations.end());
    return result;
}

bool operator<(const LaueOrientations& a, const LaueOrientations& b){
	// a < b
	if(a.getNumMatches() == b.getNumMatches())
		return a.getPatternError() < b.getPatternError();
	else
		return a.getNumMatches() > b.getNumMatches();
}

// **********************************************************************************

// Re-orientation routines (class = Reorientation)

Reorientation::Reorientation():
primary(3,1), secondary(3,1), UB(3,3)
{ 
	mode = Unknown;
	for(auto &row : angles) std::fill(row,row+3,0.0);
	std::fill(valid, valid+8, false);
}

int Reorientation::getMode(void){
	return mode;
}

void Reorientation::setPrimary(const Matrix p){
	std::fill(valid,valid+8,false);
	primary = p;
}

void Reorientation::setSecondary(const Matrix s){
	std::fill(valid,valid+8,false);
	secondary = s;
}

void Reorientation::setPrimary(double a, double b, double c){
	std::fill(valid,valid+8,false);
	primary = Matrix(a,b,c);
}

void Reorientation::setSecondary(double a, double b, double c){
	std::fill(valid,valid+8,false);
	secondary = Matrix(a,b,c);
}

void Reorientation::calc(void){
	calc(XAxis);
}

namespace {
bool unitVector(Matrix &v) {
    if(v.Rows()!=3 || v.Cols()!=1) return false;
    double scale = 0;
    for(int i=0;i<3;++i) {
        if(!std::isfinite(v.Get(i,0))) return false;
        scale = std::max(scale,std::abs(v.Get(i,0)));
    }
    if(scale==0) return false;
    v = v / scale;
    v = v / v.mag();
    return true;
}
Matrix axisRotation(int axis, double angle) {
    Matrix rotation(3,3);
    if(axis==0) return rotation.rotateX(-angle);
    if(axis==1) return rotation.rotateY(-angle);
    return rotation.rotateZ(-angle);
}
Matrix appliedRotation(const double *angles) {
    return axisRotation(2,angles[2])*axisRotation(1,angles[1])*axisRotation(0,angles[0]);
}
}

int Reorientation::getValidCount() const {
    return std::count(valid,valid+8,true);
}

void Reorientation::calc(int thismode) {
    mode = thismode;
    std::fill(valid,valid+8,false);
    for(auto &row : angles) std::fill(row,row+3,0.0);
    Matrix p = primary;
    if(!unitVector(p)) return;
    p = UB * p;
    if(!unitVector(p)) return;
    if(mode == Zone) {
        Matrix s = secondary;
        if(!unitVector(s)) return;
        s = UB * s;
        if(!unitVector(s) || std::abs(p.dot(s)) > 1e-8) return;
        // Both requested axes must be attainable by a rigid rotation.
        Matrix y = s ^ p;
        if(!unitVector(y)) return;
        Matrix z = p ^ y;
        Matrix rotation(p.Get(0,0),p.Get(1,0),p.Get(2,0),
                        y.Get(0,0),y.Get(1,0),y.Get(2,0),
                        z.Get(0,0),z.Get(1,0),z.Get(2,0));
        angles[6][1] = std::asin(std::max(-1.0,std::min(1.0,-rotation.Get(2,0))));
        if(std::abs(std::cos(angles[6][1])) > 1e-10) {
            angles[6][0] = std::atan2(rotation.Get(2,1),rotation.Get(2,2));
            angles[6][2] = std::atan2(rotation.Get(1,0),rotation.Get(0,0));
        } else {
            angles[6][0] = 0;
            angles[6][2] = std::atan2(-rotation.Get(0,1),rotation.Get(1,1));
        }
        angles[7][0] = angles[6][0] + M_PI;
        angles[7][1] = M_PI - angles[6][1];
        angles[7][2] = angles[6][2] + M_PI;
        for(int row=6;row<8;++row) {
            Matrix r = appliedRotation(angles[row]);
            valid[row] = (r*p-Matrix(1,0,0)).mag() < 1e-8 &&
                         (r*s-Matrix(0,0,1)).mag() < 1e-8;
        }
        return;
    }
    if(mode < XAxis || mode > ZAxis) { mode = Unknown; return; }
    Matrix target(0,0,0);
    target.Set(mode-1,0,1);
    const int pairs[3][2] = {{0,1},{0,2},{1,2}};
    for(int pair=0;pair<3;++pair) {
        const int a=pairs[pair][0], b=pairs[pair][1];
        // The second rotation preserves component b. Solve the first angle
        // from c*cos(angle) + s*sin(angle) + offset = target[b].
        const double offset = a==b ? p.Get(b,0) : 0.0;
        const double c = p.Get(b,0)-offset;
        const double s = (axisRotation(a,M_PI/2)*p).Get(b,0)-offset;
        const double radius = std::hypot(c,s);
        double first[2] = {0,M_PI};
        if(radius > 1e-12) {
            const double ratio = (target.Get(b,0)-offset)/radius;
            if(std::abs(ratio) > 1+1e-12) continue;
            const double delta = std::acos(std::max(-1.0,std::min(1.0,ratio)));
            first[0] = std::atan2(s,c)+delta;
            first[1] = std::atan2(s,c)-delta;
        } else if(std::abs(target.Get(b,0)-offset) > 1e-10) continue;
        for(int choice=0;choice<2;++choice) {
            const int row=pair*2+choice;
            Matrix intermediate = axisRotation(a,first[choice])*p;
            Matrix cross = intermediate ^ target;
            angles[row][a] = first[choice];
            angles[row][b] = std::atan2(cross.Get(b,0),
                intermediate.dot(target)-intermediate.Get(b,0)*target.Get(b,0));
            valid[row] = (appliedRotation(angles[row])*p-target).mag() < 1e-8;
        }
    }
}

void Reorientation::setUB(const Matrix newUB){
	std::fill(valid,valid+8,false);
	UB = newUB;
}

double* Reorientation::getAngles(int n, int setting){
	return angles[(n * 2) + setting ];
}

Matrix Reorientation::getPrimary(void){
	return primary;
}

Matrix Reorientation::getSecondary(void){
	return secondary;
}

// **********************************************************************************

// LaueReorientation Routines
