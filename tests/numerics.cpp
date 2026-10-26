#include "crystal.h"
#include "laue.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <set>
#include <tuple>
static void require(bool ok,const char *message) {
    if(!ok) { std::cerr<<"FAIL: "<<message<<'\n'; std::exit(1); }
}
static void rotation(Crystal &c) {
    Matrix u=c.getU(), identity=u*u.transpose();
    for(int r=0;r<3;++r) for(int c=0;c<3;++c)
        require(std::abs(identity.Get(r,c)-(r==c))<1e-10,"U is orthonormal");
    require(std::abs(u.det()-1)<1e-10,"det U is +1");
}
static void orientationChecks() {
    Crystal c;
    require(c.setU(1,0,0,1,0,1),"nonorthogonal two-direction input"); rotation(c);
    Matrix before=c.getU();
    require(!c.setU(1,0,0,-2,0,0),"antiparallel rejection");
    require(!c.setU(1,0,0,2,0,0),"parallel rejection");
    require(!c.setU(0,0,0,0,1,0),"zero rejection");
    require(!c.setU(INFINITY,0,0,0,0,1),"nonfinite rejection");
    require((c.getU()*Matrix(1,0,0)-before*Matrix(1,0,0)).mag()<1e-12,"rejected U preserves orientation");
    require(c.setU(Matrix(1,2,3),Matrix(2,1,-3),Matrix(1,2,0),Matrix(0,2,1)),"four-vector input"); rotation(c);
    require(!c.setU(Matrix(1,0,0),Matrix(-1,0,0),Matrix(1,0,0),Matrix(0,1,0)),"four-vector parallel rejection");
    c.setLattice(4,5,6,1.2,1.3,1.4);
    require(c.setU(1,2,1,2,-1,3),"triclinic orientation"); rotation(c);
    for(bool free : {false,true}) for(int sample=0;sample<10;++sample) {
        Crystal original; original.setFreeRotate(free);
        Matrix p(sample==0?1.0:sample,sample==0?0.0:2-sample,sample==0?0.0:3);
        if(sample==8) p=Matrix(0,1,0);
        if(sample==9) p=Matrix(0,0,1);
        for(int mode=Reorientation::XAxis;mode<=Reorientation::ZAxis;++mode) {
            Reorientation re; re.setUB(original.getUBunrotated()); re.setPrimary(p); re.calc(mode);
            require(re.getMode()==mode && re.getValidCount()>0,"axis mode and valid solutions");
            Matrix target(0,0,0); target.Set(mode-1,0,1);
            for(int row=0;row<8;++row) if(re.isValid(row)) {
                Crystal applied=original; const double *a=re.getAngles(row/2,row%2);
                applied.rotateTo(a[0],a[1],a[2]);
                Matrix actual=applied.getUB()*p; actual=actual/actual.mag();
                require((actual-target).mag()<1e-8,"actual rotateTo aligns requested axis");
            }
        }
    }
    for(int sample=0;sample<6;++sample) {
        Crystal original;
        if(sample==4) original.rotateBy(0,M_PI/2,0);
        else if(sample==5) original.rotateBy(0,-M_PI/2,0);
        else if(sample) original.rotateBy(.3*sample,-.2*sample,.4*sample);
        Reorientation re; re.setUB(original.getUBunrotated()); re.setPrimary(1,0,0); re.setSecondary(0,0,1); re.calc(Reorientation::Zone);
        require(re.getMode()==Reorientation::Zone && re.getValidCount()==2,"zone solutions");
        for(int row=6;row<8;++row) {
            Crystal applied=original; const double *a=re.getAngles(row/2,row%2);
            applied.rotateTo(a[0],a[1],a[2]);
            Matrix p=applied.getUB()*Matrix(1,0,0),s=applied.getUB()*Matrix(0,0,1);
            require((p/p.mag()-Matrix(1,0,0)).mag()<1e-8 && (s/s.mag()-Matrix(0,0,1)).mag()<1e-8,"actual rotateTo aligns both zone axes");
        }
        re.setSecondary(1,0,1); re.calc(Reorientation::Zone);
        require(re.getValidCount()==0,"unattainable zone rejected");
    }
}
static void atomChecks() {
    Crystal c;
    require(!c.addAtom(0,0,0,0) && !c.addAtom(99,0,0,0),"atomic number bounds");
    require(!c.addAtom(1,0,NAN,0),"nonfinite atom rejection");
    auto group=c.getSpaceGroup();
    require(!c.setSpaceGroupNum(-1) && !c.setSpaceGroup("not a group") && !c.setSpaceGroupIUC("not a group") && c.getSpaceGroup()==group,"invalid space group preserves state");
    for(int i=0;i<Crystal::MaxAtoms;++i) require(c.addAtom(1,0,0,0),"atom capacity inclusive");
    require(!c.addAtom(1,0,0,0) && c.getNAtoms()==Crystal::MaxAtoms,"atom capacity enforced");
    c.delAllAtoms(); require(c.addAtom(29,-.125,.25,-.375),"signed root");
    require(c.setSpaceGroupNum(225) && c.spaceGroupGenerate(),"symmetry expansion");
    require(c.getAtom(0)->getXPos()==-.125 && c.getAtom(0)->getZPos()==-.375,"source signed coordinates retained");
    int generated=c.getNAtoms(); require(c.spaceGroupGenerate() && c.getNAtoms()==generated,"repeat expansion stable");
    c.delAllAtoms(); const double extreme=std::numeric_limits<double>::max();
    require(c.addAtom(29,extreme,-extreme,extreme) && c.spaceGroupGenerate(),"extreme finite coordinates safely expanded");
    auto factor=c.calcXSFactor(1,2,3,.25,1.0);
    require(std::isfinite(factor.real()) && std::isfinite(factor.imag()),"extreme roots safely phased");
    c.delAllAtoms(); c.setLattice(10000,10000,10000,M_PI/2,M_PI/2,M_PI/2);
    for(int i=0;i<Crystal::MaxAtoms;++i) require(c.addAtom(29,.0001*i,.123,.234),"roots for overflow");
    require(!c.spaceGroupGenerate() && c.getNAtoms()==Crystal::MaxAtoms,"symmetry capacity failure propagated with roots retained");
}
static void scatteringChecks() {
    require(std::abs(XRay::getXf0(8,.25)-4.8081345356)<1e-9,"oxygen f0 independent reference");
    require(std::abs(XRay::getXf0(29,.5)-13.7116)<1e-4,"copper f0 independent reference");
    const double low=7920.680176,high=8048.790039;
    require(std::abs(XRay::getXf1f2(29,HC_OVER_E*1000/low).imag()+.625722)<1e-10,"f2 lower knot");
    require(std::abs(XRay::getXf1f2(29,HC_OVER_E*1000/((low+high)/2)).imag()+.616635)<1e-10,"f2 midpoint");
    require(std::abs(XRay::getXf1f2(29,HC_OVER_E*1000/high).imag()+.607548)<1e-10,"f2 upper knot");
    Crystal c; c.addAtom(29,0,0,0);
    lauegram gram; gram.setLambdaRange(.3,.5); gram.setIntensityCorrection(lauegram::IntensityXRayTube);
    bool found=false;
    for(int h=1;h<12 && !found;++h) for(int k=0;k<8 && !found;++k) for(int l=0;l<8 && !found;++l) {
        LaueSpot spot; found=gram.calcHKL(&c,&spot,h,k,l);
    }
    require(found,"tube correction preserves in-band reflections");
}
static void spotChecks() {
    Crystal c; c.addAtom(29,0,0,0); c.setU(1,-2,0,0,0,1);
    lauegram gram; gram.setLambdaRange(.3,3); gram.setCalcPrecision(5); gram.setIntensityCorrection(0);
    LaueSpot direct; require(gram.calcHKL(&c,&direct,0,-1,0),"direct negative k reflection");
    require(gram.calc(&c),"calc completion");
    std::set<std::tuple<int,int,int>> found;
    for(int i=0;i<gram.getNspots();++i) {
        auto s=gram.getSpot(i);
        require(found.insert(std::make_tuple(int(s->H()),int(s->K()),int(s->L()))).second,"no duplicate hkl");
    }
    require(found.count(std::make_tuple(0,-1,0))==1,"negative k reflection enumerated");
    for(int h=-7;h<=7;++h) for(int k=-7;k<=7;++k) for(int l=-7;l<=7;++l) {
        LaueSpot s;
        require(bool(found.count(std::make_tuple(h,k,l)))==gram.calcHKL(&c,&s,h,k,l),"complete signed enumeration");
    }
    lauegram copy=gram; lauegram assigned; assigned=gram;
    require(copy.getNspots()==gram.getNspots() && assigned.calcPrecision()==5 && assigned.intensityCorrection()==0,"settings and results copied");
    require(!gram.calc(&c,[](int){ return true; }),"calc cancellation");
    Crystal elongated=c;
    elongated.setLattice(5.4,5.4,1000,M_PI/2,M_PI/2,M_PI/2);
    int callbacks=0;
    require(!gram.calc(&elongated,[&](int){return ++callbacks==3;}) && callbacks==3,"inner-loop cancellation");
    gram.copySpotsFrom(copy); require(gram.getNspots()==copy.getNspots(),"results publication");
    c.setLattice(15,15,15,M_PI/2,M_PI/2,M_PI/2); c.setU(1,0,0,0,0,1); gram.setCalcPrecision(1);
    require(gram.calc(&c),"large pattern completion");
    std::cout<<"Large pattern: "<<gram.getNspots()<<" spots\n";
    require(gram.getNspots()>65000,"dynamic spot storage exceeds old capacity");
}
int main() {
    orientationChecks(); atomChecks(); scatteringChecks(); spotChecks();
    std::cout<<"PASS: numerical and storage regressions\n";
}
