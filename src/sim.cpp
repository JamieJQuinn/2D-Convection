#include <iostream>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <cassert>

#include "sim.hpp"
#include "precision.hpp"
#include "utility.hpp"
#include "numerical_methods.hpp"

using namespace std;

Sim::Sim(int nZ, int nN, double dt,
    double Ra, double Pr, int a,
#ifdef DDC
    double RaXi, double tau,
#endif
    double timeBetweenSaves, bool modifydt,
          int current, double t, double totalTime,
    std::string saveFolder, std::string icFile)
  : nZ {nZ}
  , nN {nN}
  , dt {dt}
  , Ra {Ra}
#ifdef DDC
  , RaXi {RaXi}
  , tau {tau}
#endif
  , Pr {Pr}
  , a {a}
  , timeBetweenSaves {timeBetweenSaves}
  , modifydt {modifydt}
  , current {current}
  , t {t}
  , totalTime {totalTime}
  , saveFolder {saveFolder}
  , icFile {icFile}
{
  init(nZ, nN, dt, Ra, Pr, a,
#ifdef DDC
     RaXi,  tau,
#endif
    timeBetweenSaves, modifydt, current, t, totalTime, saveFolder, icFile);
}

void Sim::reinit() {
  for(int i=0; i<nZ*nN; ++i) {
    psi[i] = 0.0;
    omg[i] = 0.0;
    tmp[i] = 0.0;
#ifdef DDC
    xi[i] = 0.0;
#endif
  }
  for(int i=0; i<nZ*nN*2; ++i) {
    dTmpdt[i] = 0.0;
    dOmgdt[i] = 0.0;
#ifdef DDC
    dXidt[i] = 0.0;
#endif
  }
}


void Sim::init(int nZ, int nN, double dt, double Ra, double Pr, int a,
#ifdef DDC
    double RaXi, double tau,
#endif
    double timeBetweenSaves,
    bool modifydt,
          int current, double t, double totalTime,
    std::string saveFolder, std::string icFile)
{
  // Derived Constants
  nX = nZ*a;
  dz = double(1)/(nZ-1);
  dx = double(a)/(nX-1);
  oodz2 = pow(1.0/dz, 2);

  kePrev = 0.0f;
  keCurrent = 0.0f;
  saveNumber=0;
  KEsaveNumber=0;

  // Initialise Arrays
  psi = new double [nN*nZ];
  omg = new double [nN*nZ];
  tmp = new double [nN*nZ];

#ifdef DDC
  xi = new double [nN*nZ];
  dXidt = new double [2*nN*nZ];
#endif

  dTmpdt = new double [2*nN*nZ];
  dOmgdt = new double [2*nN*nZ];

  for(int i=0; i<nZ*nN; ++i) {
    psi[i] = 0.0;
    omg[i] = 0.0;
    tmp[i] = 0.0;
#ifdef DDC
    xi[i] = 0.0;
#endif
  }
  for(int i=0; i<nZ*nN*2; ++i) {
    dTmpdt[i] = 0.0;
    dOmgdt[i] = 0.0;
#ifdef DDC
    dXidt[i] = 0.0;
#endif
  }

thomasAlgorithm = new ThomasAlgorithm(nZ, nN, a, oodz2);
}

Sim::~Sim() {
  // Destructor
  delete[] psi  ;
  delete[] omg  ;
  delete[] tmp  ;
#ifdef DDC
  delete[] xi;
  delete[] dXidt;
#endif

  delete[] dTmpdt  ;
  delete[] dOmgdt  ;

  delete thomasAlgorithm;
}

void Sim::save() {
  std::ofstream file (saveFolder+std::string("vars")+strFromNumber(saveNumber++)+std::string(".dat"), std::ios::out | std::ios::binary);
  if(file.is_open()) {
    file.write(reinterpret_cast<char*>(tmp), sizeof(tmp[0])*nN*nZ);
    file.write(reinterpret_cast<char*>(omg), sizeof(omg[0])*nN*nZ);
    file.write(reinterpret_cast<char*>(psi), sizeof(psi[0])*nN*nZ);
    file.write(reinterpret_cast<char*>(dTmpdt+current*nZ*nN), sizeof(dTmpdt[0])*nN*nZ);
    file.write(reinterpret_cast<char*>(dTmpdt+((current+1)%2)*nZ*nN), sizeof(dTmpdt[0])*nN*nZ);
    file.write(reinterpret_cast<char*>(dOmgdt+current*nZ*nN), sizeof(dOmgdt[0])*nN*nZ);
    file.write(reinterpret_cast<char*>(dOmgdt+((current+1)%2)*nZ*nN), sizeof(dOmgdt[0])*nN*nZ);

  } else {
    cout << "Couldn't open " << saveFolder << " for writing. Aborting." << endl;
    exit(-1);
  }
  file.close();
}

void Sim::load( double* tmp, double* omg, double* psi, std::string icFile) {
  std::ifstream file (icFile, std::ios::in | std::ios::binary);
  if(file.is_open()) {
    file.read(reinterpret_cast<char*>(tmp), sizeof(tmp[0])*nN*nZ);
    file.read(reinterpret_cast<char*>(omg), sizeof(omg[0])*nN*nZ);
    file.read(reinterpret_cast<char*>(psi), sizeof(psi[0])*nN*nZ);
    file.read(reinterpret_cast<char*>(dTmpdt+current*nZ*nN), sizeof(dTmpdt[0])*nN*nZ);
    file.read(reinterpret_cast<char*>(dTmpdt+((current+1)%2)*nZ*nN), sizeof(dTmpdt[0])*nN*nZ);
    file.read(reinterpret_cast<char*>(dOmgdt+current*nZ*nN), sizeof(dOmgdt[0])*nN*nZ);
    file.read(reinterpret_cast<char*>(dOmgdt+((current+1)%2)*nZ*nN), sizeof(dOmgdt[0])*nN*nZ);
  } else {
    cout << "Couldn't open " << icFile << " for reading. Aborting." << endl;
    exit(-1);
  }
}

void Sim::saveKineticEnergy() {
  // Save total energy
  std::ofstream file (saveFolder+"KineticEnergy"+std::string(".dat"), std::ios::out | std::ios::app | std::ios::binary);
  double ke = calcKineticEnergy();
  kePrev = keCurrent;
  keCurrent = ke;
  file.write(reinterpret_cast<char*>(&ke), sizeof(double));
  file.flush();
  file.close();
  // save energy per mode
  for(int n=1; n<nN; ++n) {
    std::ofstream file (saveFolder+"KineticEnergyMode"+strFromNumber(n)+std::string(".dat"), std::ios::out | std::ios::app | std::ios::binary);
    double ke = calcKineticEnergyForMode(n);
    file.write(reinterpret_cast<char*>(&ke), sizeof(double));
    file.flush();
    file.close();
  }
}


#ifdef DDC
void Sim::updateXi(double f=1.0) {
  for(int n=0; n<nN; ++n) {
    for(int k=0; k<nZ; ++k) {
      xi[n*nZ+k] += adamsBashforth(dXidt[current*nZ*nN+n*nZ+k], dXidt[((current+1)%2)*nZ*nN+n*nZ+k], f, dt);
    }
  } 
}
#endif

void Sim::updateTmpAndOmg(double f = 1.0) {
  // Update variables using Adams-Bashforth Scheme
  // f is the proportional change between the new dt and old dt
  // ( if dt changed )
  for(int n=0; n<nN; ++n) {
    for(int k=0; k<nZ; ++k) {
      tmp[n*nZ+k] += adamsBashforth(dTmpdt[current*nZ*nN+n*nZ+k], dTmpdt[((current+1)%2)*nZ*nN+n*nZ+k], f, dt);
      omg[n*nZ+k] += adamsBashforth(dOmgdt[current*nZ*nN+n*nZ+k], dOmgdt[((current+1)%2)*nZ*nN+n*nZ+k], f, dt);

      assert(!isnan(tmp[n*nZ+k]));
      assert(!isnan(omg[n*nZ+k]));
    }
    // check BCs
    if(n>0) {
      assert(tmp[n*nZ] < EPSILON);
    } else {
      assert(tmp[n*nZ] - 1.0 < EPSILON);
    }
    assert(tmp[n*nZ+nZ-1] < EPSILON);
    assert(omg[n*nZ] < EPSILON);
    assert(omg[n*nZ+nZ-1] < EPSILON);
  }

  // Boundary Conditions
  // Periodic
  //tmp[3*nZ+0] = sin(OMEGA*t);
}


void Sim::computeLinearDerivatives(int linearSim) {
  // Computes the (linear) derivatives of Tmp and omg
  // If linear sim is 0, we start n from 0 and the advection approximation
  // in dTmpdt vanishes
  for(int n=linearSim; n<nN; ++n) {
    for(int k=1; k<nZ-1; ++k) {
      // Setup indices
      int di = current*nZ*nN+n*nZ+k;;

      int i = k+n*nZ;

      dTmpdt[di] = dfdz2(tmp, i, dz) - pow(n*M_PI/a, 2)*tmp[i];
#ifdef DDC
      dXidt[di] = tau*(dfdz2(xi, i, dz) - pow(n*M_PI/a, 2)*xi[i]);
#endif
      if (linearSim == 1) {
#ifdef DDC
        dXidt[di] += -1*xiGrad*n*M_PI/a * psi[i];
#endif
        dTmpdt[di] += -1*tmpGrad*n*M_PI/a * psi[i];
      }
      assert(!isnan(dfdz2(tmp, i, dz) - pow(n*M_PI/a, 2)*tmp[i]
        + n*M_PI/a * psi[i]*linearSim));
      dOmgdt[di] =
        Pr*(dfdz2(omg, i, dz) - pow(n*M_PI/a, 2)*omg[i]
        + Ra*n*M_PI/a*tmp[i]
        );
#ifdef DDC
      dOmgdt[di] += -RaXi*tau*Pr*(n*M_PI/a)*xi[i];
#endif
      assert(dOmgdt[nZ*0+k] < EPSILON);
    }
  }
}

void Sim::computeNonLinearDerivatives() {
  for(int n=1; n<nN; ++n) {
    for(int k=1; k<nZ-1; ++k) {
      int in = n*nZ + k;
      // Contribution TO tmp[n=0]
      dTmpdt[current*nZ*nN+0*nN+k] +=
        -M_PI/(2*a)*n*(
          dfdz(psi, in, dz)*tmp[in] +
          dfdz(tmp, in, dz)*psi[in]
          );
    }
  }
  #pragma omp parallel for schedule(dynamic)
  for(int n=1; n<nN; ++n) {
    // Contribution FROM tmp[n=0]
    for(int k=1; k<nZ-1; ++k) {
      int in = n*nZ+k;
      dTmpdt[current*nZ*nN + in] +=
        -n*M_PI/a*psi[in]*dfdz(tmp, 0*nZ+k, dz);
    }
    // Contribution FROM tmp[n>0] and omg[n>0]
    int im, io, o;
    for(int m=1; m<n; ++m){
      // Case n = n' + n''
      o = n-m;
      assert(o>0 and o<nN);
      assert(m>0 and m<nN);
      for(int k=1; k<nZ-1; ++k) {
        im = nZ*m+k;
        io = nZ*o + k;
        dTmpdt[current*nZ*nN+nZ*n+k] +=
          -M_PI/(2*a)*(
          -m*dfdz(psi, io, dz)*tmp[im]
          +o*dfdz(tmp, im, dz)*psi[io]
          );
        dOmgdt[current*nZ*nN+nZ*n+k] +=
          -M_PI/(2*a)*(
          -m*dfdz(psi, io, dz)*omg[im]
          +o*dfdz(omg, im, dz)*psi[io]
          );
      }
    }
    for(int m=n+1; m<nN; ++m){
      // Case n = n' - n''
      o = m-n;
      assert(o>0 and o<nN);
      assert(m>0 and m<nN);
      for(int k=1; k<nZ-1; ++k) {
        im = nZ*m+k;
        io = nZ*o + k;
        dTmpdt[current*nZ*nN+nZ*n+k] +=
          -M_PI/(2*a)*(
          +m*dfdz(psi, io, dz)*tmp[im]
          +o*dfdz(tmp, im, dz)*psi[io]
          );
        dOmgdt[current*nZ*nN+nZ*n+k] +=
          -M_PI/(2*a)*(
          +m*dfdz(psi, io, dz)*omg[im]
          +o*dfdz(omg, im, dz)*psi[io]
          );
      }
    }
    for(int m=1; m+n<nN; ++m){
      // Case n= n'' - n'
      o = n+m;
      assert(o>0 and o<nN);
      assert(m>0 and m<nN);
      for(int k=1; k<nZ-1; ++k) {
        im = nZ*m+k;
        io = nZ*o + k;
        dTmpdt[current*nZ*nN+nZ*n+k] +=
          -M_PI/(2*a)*(
          +m*dfdz(psi, io, dz)*tmp[im]
          +o*dfdz(tmp, im, dz)*psi[io]
          );
        dOmgdt[current*nZ*nN+nZ*n+k] +=
          +M_PI/(2*a)*(
          +m*dfdz(psi, io, dz)*omg[im]
          +o*dfdz(omg, im, dz)*psi[io]
          );
      }
    }
  }
}

void Sim::solveForPsi(){
  // Solve for Psi using Thomas algorithm
  for(int n=0; n<nN; ++n) {
    thomasAlgorithm->solve(psi+nZ*n, omg+nZ*n, n);
    // Check Boundary Conditions
    assert(psi[nZ*n+0] == 0.0);
    assert(psi[nZ*n+nZ-1] == 0.0);
  }
  // Check BCs
  for(int k=0; k<nZ; ++k) {
    assert(psi[nZ*0+k] < EPSILON);
  }

}

void Sim::printMaxOf(double *a, std::string name) {
  int nStart = 0; // n level to start from
  // Find max
  double max = a[nStart*nZ];
  int maxLoc[] = {0, nStart};
  for(int n=nStart; n<nN; ++n) {
    for(int k=0; k<nZ; ++k) {
      if(a[n*nZ+k]>max) {
        max = a[n*nZ+k];
        maxLoc[0] = k;
        maxLoc[1] = n;
      }
    }
  }
  // print max
  cout << max << " @ " << "(" << maxLoc[0] << ", " << maxLoc[1] << ")" << endl;
}

void Sim::printBenchmarkData() {
  cout << t << " of " << totalTime << "(" << t/totalTime*100 << ")" << endl;
  for(int n=0; n<21; ++n) {
    printf("%d | %e | %e | %e\n", n, tmp[n*nZ+32], omg[n*nZ+32], psi[n*nZ+32]);
  }
}

double Sim::calcKineticEnergyForMode(int n) {
  double z0 = 0.0; // limits of integration
  double z1 = 1.0;
  double ke = 0; // Kinetic energy
    ke += pow(n*M_PI/a*psi[n*nZ+0], 2)/2.0; // f(0)/2
    ke += pow(n*M_PI/a*psi[n*nZ+(nZ-1)], 2)/2.0; // f(1)/2
    for(int k=1; k<nZ-1; ++k) {
      int in = nZ*n+k;
      // f(k)
      ke += pow(dfdz(psi, in, dz), 2) + pow(n*M_PI/a*psi[in], 2);
    }
  ke *= (z1-z0)*a/(4*(nZ-1));
  return ke;
}

double Sim::calcKineticEnergy() {
  // Uses trapezeoid rule to calc kinetic energy for each mode
  double ke = 0.0;
  for(int n=0; n<nN; ++n) {
    ke += calcKineticEnergyForMode(n);
  }
  return ke;
}

void Sim::runNonLinear() {
  // Load initial conditions
  load(tmp, omg, psi, icFile);
  current = 0;
  double saveTime = 0;
  double KEsaveTime = 0;
  double CFLCheckTime = 0;
  double f = 1.0f; // Fractional change in dt (if CFL condition being breached)
  t = 0;
  while (totalTime-t>EPSILON) {
    if(KEsaveTime-t < EPSILON) {
      saveKineticEnergy();
      KEsaveTime += 1e-4;
    }
    if(CFLCheckTime-t < EPSILON) {
      cout << "Checking CFL" << endl;
      CFLCheckTime += 1e4*dt;
      f = checkCFL(psi, dz, dx, dt, a, nN, nX, nZ);
      cout << std::log(std::abs(keCurrent)) - std::log(std::abs(kePrev)) << endl;
    }
    if(saveTime-t < EPSILON) {
      cout << t << " of " << totalTime << "(" << t/totalTime*100 << "%)" << endl;
      saveTime+=timeBetweenSaves;
      save();
    }
    computeLinearDerivatives(0);
    computeNonLinearDerivatives();
    updateTmpAndOmg(f);
    f=1.0f;
    solveForPsi();
    t+=dt;
    ++current%=2;
  } 
  printf("%e of %e (%.2f%%)\n", t, totalTime, t/totalTime*100);
  save();
}

double Sim::runLinear(int nCrit) {
  // Initial Conditions
  // Let psi = omg = dtmpdt = domgdt = 0
  // Let tmp[n>0] = sin(PI*z)
  // and tmp[n=0] = (1-z)/N
  // For DDC Salt-fingering
  tmpGrad = 1;
#ifdef DDC
  xiGrad = 1;
#endif
  /*
  // For DDC SemiConvection
  tmpGrad = -1;
#ifdef DDC
  xiGrad = -1;
#endif
  */
#ifndef DDC
  tmpGrad = -1;
#endif
  for(int k=0; k<nZ; ++k) {
    if(tmpGrad==-1){
      tmp[nZ*0+k] = 1-k*dz;
    } else if(tmpGrad==1) {
      tmp[nZ*0+k] = k*dz;
    }
#ifdef DDC
    if(xiGrad==-1){
      xi[nZ*0+k] = 1-k*dz;
    } else if(xiGrad==1) {
      xi[nZ*0+k] = k*dz;
    }
#endif
    for(int n=1; n<nN; ++n) {
      tmp[nZ*n+k] = sin(M_PI*k*dz);
#ifdef DDC
      xi[nZ*n+k] = sin(M_PI*k*dz);
#endif
    } 
  }
  // Check BCs
  for(int n=0; n<nN; ++n){
    if(n>0) {
      assert(tmp[n*nZ] < EPSILON);
    } else {
      assert(tmp[n*nZ] - 1.0 < EPSILON);
    }
    assert(tmp[n*nZ+nZ-1] < EPSILON);
    assert(omg[n*nZ] < EPSILON);
    assert(omg[n*nZ+nZ-1] < EPSILON);
  }

  // Stuff for critical rayleigh check
  double tmpPrev[nN];
#ifdef DDC
  double xiPrev[nN];
#endif
  double omgPrev[nN];
  double psiPrev[nN];
  for(int n=0; n<nN; ++n){
    tmpPrev[n] = tmp[32+n*nZ];
#ifdef DDC
    xiPrev[n]  = xi [32+n*nZ];
#endif
    psiPrev[n] = psi[32+n*nZ];
    omgPrev[n] = omg[32+n*nZ];
  }
  double logTmpPrev = 0.0;
#ifdef DDC
  double logXiPrev = 0.0;
#endif
  double logPsiPrev =0.0;
  double logOmgPrev =0.0;
  double tolerance = 1e-10;
  current = 0;
  int steps = 0;
  t=0;
  while (t<totalTime) {
    if(steps%500 == 0) {
      double logTmp = std::log(std::abs(tmp[32+nCrit*nZ])) - std::log(std::abs(tmpPrev[nCrit]));
#ifdef DDC
      double logXi = std::log(std::abs(xi[32+nCrit*nZ])) - std::log(std::abs(xiPrev[nCrit]));
#endif
      double logOmg = std::log(std::abs(omg[32+nCrit*nZ])) - std::log(std::abs(omgPrev[nCrit]));
      double logPsi = std::log(std::abs(psi[32+nCrit*nZ])) - std::log(std::abs(psiPrev[nCrit]));
      if(std::abs(logTmp - logTmpPrev)<tolerance) {
#ifdef DDC
      if(std::abs(logXi - logXiPrev)<tolerance) {
#endif
      if(std::abs(logOmg - logOmgPrev)<tolerance) {
      if(std::abs(logPsi - logPsiPrev)<tolerance) {
        return logTmp;
#ifdef DDC
      }
#endif
      }}}
      logTmpPrev = logTmp;
#ifdef DDC
      logXiPrev = logXi;
#endif
      logOmgPrev = logOmg;
      logPsiPrev = logPsi;
      for(int n=1; n<11; ++n){
        tmpPrev[n] = tmp[32+n*nZ];
#ifdef DDC
        xiPrev[n] =  xi [32+n*nZ];
#endif
        psiPrev[n] = psi[32+n*nZ];
        omgPrev[n] = omg[32+n*nZ];
      }
    }
    steps++;
    computeLinearDerivatives(1);
    updateTmpAndOmg();
#ifdef DDC
    updateXi();
#endif
    solveForPsi();
    t+=dt;
    ++current%=2;
  } 
  return 0;
}
