// Copyright (C) 2004-2015 Jed Brown, Ed Bueler and Constantine Khroulev
//
// This file is part of PISM.
//
// PISM is free software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation; either version 3 of the License, or (at your option) any later
// version.
//
// PISM is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License
// along with PISM; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

#include <cmath>

#include "tests/exactTestsFG.h"
#include "tests/exactTestK.h"
#include "tests/exactTestO.h"
#include "iceCompModel.hh"
#include "PISMStressBalance.hh"
#include "PISMTime.hh"
#include "IceGrid.hh"
#include "pism_options.hh"
#include "error_handling.hh"
#include "PISMBedDef.hh"
#include "PISMConfig.hh"

namespace pism {

// boundary conditions for tests F, G (same as EISMINT II Experiment F)
const double IceCompModel::Ggeo = 0.042;
const double IceCompModel::ST = 1.67e-5;
const double IceCompModel::Tmin = 223.15;  // K
const double IceCompModel::LforFG = 750000; // m
const double IceCompModel::ApforG = 200; // m


/*! Re-implemented so that we can add compensatory strain_heating in Tests F and G. */
void IceCompModel::temperatureStep(unsigned int *vertSacrCount, unsigned int *bulgeCount) {

  if ((testname == 'F') || (testname == 'G')) {
    // FIXME: This code messes with the strain heating field owned by
    // stress_balance. This is BAD.
    IceModelVec3 &strain_heating3 = const_cast<IceModelVec3&>(stress_balance->volumetric_strain_heating());

    strain_heating3.add(1.0, strain_heating3_comp);      // strain_heating = strain_heating + strain_heating_c
    IceModel::temperatureStep(vertSacrCount, bulgeCount);
    strain_heating3.add(-1.0, strain_heating3_comp); // strain_heating = strain_heating - strain_heating_c
  } else {
    IceModel::temperatureStep(vertSacrCount, bulgeCount);
  }
}


void IceCompModel::initTestFG() {
  int        Mz = grid.Mz();
  double     H, accum;

  std::vector<double> dummy1(Mz);
  std::vector<double> dummy2(Mz);
  std::vector<double> dummy3(Mz);
  std::vector<double> dummy4(Mz);

  IceModelVec2S bed_topography;
  bed_topography.create(grid, "topg", WITHOUT_GHOSTS);
  bed_topography.set(0);
  beddef->set_elevation(bed_topography);

  geothermal_flux.set(Ggeo);

  std::vector<double> T(grid.Mz());

  IceModelVec::AccessList list;
  list.add(ice_thickness);
  list.add(T3);

  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    double r = radius(grid, i, j);

    if (r > LforFG - 1.0) { // if (essentially) outside of sheet
      ice_thickness(i, j) = 0.0;
      for (int k = 0; k < Mz; k++) {
        T[k] = Tmin + ST * r;
      }
    } else {
      r = std::max(r, 1.0); // avoid singularity at origin
      if (testname == 'F') {
        bothexact(0.0, r, &(grid.z()[0]), Mz, 0.0,
                  &H, &accum, &T[0], &dummy1[0], &dummy2[0], &dummy3[0], &dummy4[0]);
        ice_thickness(i, j) = H;

      } else {
        bothexact(grid.time->current(), r, &(grid.z()[0]), Mz, ApforG,
                  &H, &accum, &T[0], &dummy1[0], &dummy2[0], &dummy3[0], &dummy4[0]);
        ice_thickness(i, j) = H;

      }
    }
    T3.set_column(i, j, &T[0]);
  }

  ice_thickness.update_ghosts();

  T3.update_ghosts();

  ice_thickness.copy_to(ice_surface_elevation);
}


void IceCompModel::getCompSourcesTestFG() {
  double accum, dummy0;

  std::vector<double> dummy1(grid.Mz());
  std::vector<double> dummy2(grid.Mz());
  std::vector<double> dummy3(grid.Mz());
  std::vector<double> dummy4(grid.Mz());

  std::vector<double> strain_heating_C(grid.Mz());

  const double
    ice_rho   = config.get("ice_density"),
    ice_c     = config.get("ice_specific_heat_capacity");

  // before temperature and flow step, set strain_heating_c from exact values

  IceModelVec::AccessList list;
  list.add(strain_heating3_comp);

  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    double r = radius(grid, i, j);
    if (r > LforFG - 1.0) {  // outside of sheet
      strain_heating3_comp.set_column(i, j, 0.0);
    } else {
      r = std::max(r, 1.0); // avoid singularity at origin
      if (testname == 'F') {
        bothexact(0.0, r, &(grid.z()[0]), grid.Mz(), 0.0,
                  &dummy0, &accum, &dummy1[0], &dummy2[0], &dummy3[0], &dummy4[0],
                  &strain_heating_C[0]);
      } else {
        bothexact(grid.time->current(), r, &(grid.z()[0]), grid.Mz(), ApforG,
                  &dummy0, &accum, &dummy1[0], &dummy2[0], &dummy3[0], &dummy4[0],
                  &strain_heating_C[0]);
      }
      for (unsigned int k=0;  k<grid.Mz();  k++) {
        // scale strain_heating to J/(s m^3)
        strain_heating_C[k] = strain_heating_C[k] * ice_rho * ice_c;
      }
      strain_heating3_comp.set_column(i, j, &strain_heating_C[0]);
    }
  }
}


void IceCompModel::fillSolnTestFG() {
  // fills Vecs ice_thickness, ice_surface_elevation, vAccum, T3, u3, v3, w3, strain_heating3, v_strain_heating_Comp
  double     H, accum;
  double     Ts;

  // FIXME: This code messes with the fields owned by stress_balance.
  // This is BAD.
  IceModelVec3
    &strain_heating3 = const_cast<IceModelVec3&>(stress_balance->volumetric_strain_heating()),
    &u3 = const_cast<IceModelVec3&>(stress_balance->velocity_u()),
    &v3 = const_cast<IceModelVec3&>(stress_balance->velocity_v()),
    &w3 = const_cast<IceModelVec3&>(stress_balance->velocity_w());

  std::vector<double> Uradial(grid.Mz());

  std::vector<double> T(grid.Mz());
  std::vector<double> u(grid.Mz());
  std::vector<double> v(grid.Mz());
  std::vector<double> w(grid.Mz());
  std::vector<double> strain_heating(grid.Mz());
  std::vector<double> strain_heating_C(grid.Mz());

  const double
    ice_rho   = config.get("ice_density"),
    ice_c     = config.get("ice_specific_heat_capacity");

  IceModelVec::AccessList list;
  list.add(ice_thickness);
  list.add(T3);
  list.add(u3);
  list.add(v3);
  list.add(w3);
  list.add(strain_heating3);
  list.add(strain_heating3_comp);

  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    double xx = grid.x(i), yy = grid.y(j), r = radius(grid, i, j);
    if (r > LforFG - 1.0) {  // outside of sheet

      ice_thickness(i, j) = 0.0;
      Ts = Tmin + ST * r;
      T3.set_column(i, j, Ts);
      u3.set_column(i, j, 0.0);
      v3.set_column(i, j, 0.0);
      w3.set_column(i, j, 0.0);
      strain_heating3.set_column(i, j, 0.0);
      strain_heating3_comp.set_column(i, j, 0.0);
    } else {  // inside the sheet
      r = std::max(r, 1.0); // avoid singularity at origin
      if (testname == 'F') {
        bothexact(0.0, r, &(grid.z()[0]), grid.Mz(), 0.0,
                  &H, &accum, &T[0], &Uradial[0], &w[0], &strain_heating[0], &strain_heating_C[0]);
        ice_thickness(i, j)   = H;

      } else {
        bothexact(grid.time->current(), r, &(grid.z()[0]), grid.Mz(), ApforG,
                  &H, &accum, &T[0], &Uradial[0], &w[0], &strain_heating[0], &strain_heating_C[0]);
        ice_thickness(i, j)   = H;

      }
      for (unsigned int k = 0; k < grid.Mz(); k++) {
        u[k] = Uradial[k]*(xx/r);
        v[k] = Uradial[k]*(yy/r);
        strain_heating[k] = strain_heating[k] * ice_rho * ice_c; // scale strain_heating to J/(s m^3)
        strain_heating_C[k] = strain_heating_C[k] * ice_rho * ice_c; // scale strain_heating_C to J/(s m^3)
      }
      T3.set_column(i, j, &T[0]);
      u3.set_column(i, j, &u[0]);
      v3.set_column(i, j, &v[0]);
      w3.set_column(i, j, &w[0]);
      strain_heating3.set_column(i, j, &strain_heating[0]);
      strain_heating3_comp.set_column(i, j, &strain_heating_C[0]);
    }
  }

  ice_thickness.update_ghosts();
  ice_thickness.copy_to(ice_surface_elevation);

  T3.update_ghosts();

  u3.update_ghosts();

  v3.update_ghosts();
}

void IceCompModel::computeTemperatureErrors(double &gmaxTerr,
                                            double &gavTerr) {
  double maxTerr = 0.0, avTerr = 0.0, avcount = 0.0;

  double junk0, junk1;

  std::vector<double> Tex(grid.Mz());
  std::vector<double> dummy1(grid.Mz());
  std::vector<double> dummy2(grid.Mz());
  std::vector<double> dummy3(grid.Mz());
  std::vector<double> dummy4(grid.Mz());

  IceModelVec::AccessList list;
  list.add(ice_thickness);
  list.add(T3);

  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    double r = radius(grid, i, j);
    double *T;
    T = T3.get_column(i, j);
    if ((r >= 1.0) and (r <= LforFG - 1.0)) {
      // only evaluate error if inside sheet and not at central
      // singularity
      switch (testname) {
      case 'F':
        bothexact(0.0, r, &(grid.z()[0]), grid.Mz(), 0.0,
                  &junk0, &junk1, &Tex[0], &dummy1[0], &dummy2[0], &dummy3[0], &dummy4[0]);
        break;
      case 'G':
        bothexact(grid.time->current(), r, &(grid.z()[0]), grid.Mz(), ApforG,
                  &junk0, &junk1, &Tex[0], &dummy1[0], &dummy2[0], &dummy3[0], &dummy4[0]);
        break;
      default:
        throw RuntimeError("temperature errors only computable for tests F and G");
      }
      const int ks = grid.kBelowHeight(ice_thickness(i, j));
      for (int k = 0; k < ks; k++) {  // only eval error if below num surface
        const double Terr = fabs(T[k] - Tex[k]);
        maxTerr = std::max(maxTerr, Terr);
        avcount += 1.0;
        avTerr += Terr;
      }
    }
  }

  gmaxTerr = GlobalMax(grid.com, maxTerr);
  gavTerr = GlobalSum(grid.com, avTerr);
  double gavcount = GlobalSum(grid.com, avcount);
  gavTerr = gavTerr / std::max(gavcount, 1.0);  // avoid div by zero
}


void IceCompModel::computeIceBedrockTemperatureErrors(double &gmaxTerr, double &gavTerr,
                                                      double &gmaxTberr, double &gavTberr) {

  if ((testname != 'K') && (testname != 'O')) {
    throw RuntimeError("ice and bedrock temperature errors only computable for tests K and O");
  }

  double    maxTerr = 0.0, avTerr = 0.0, avcount = 0.0;
  double    maxTberr = 0.0, avTberr = 0.0, avbcount = 0.0;

  const double *Tb, *T;
  double    FF;
  std::vector<double> Tex(grid.Mz());

  BTU_Verification *my_btu = dynamic_cast<BTU_Verification*>(btu);
  if (my_btu == NULL) {
    throw RuntimeError("my_btu == NULL");
  }
  const IceModelVec3Custom *bedrock_temp = my_btu->temperature();

  std::vector<double> zblevels = bedrock_temp->get_levels();
  unsigned int Mbz = (unsigned int)zblevels.size();
  std::vector<double> Tbex(Mbz);

  switch (testname) {
    case 'K':
      for (unsigned int k = 0; k < grid.Mz(); k++) {
        exactK(grid.time->current(), grid.z(k), &Tex[k], &FF,
               (bedrock_is_ice_forK==true));
      }
      for (unsigned int k = 0; k < Mbz; k++) {
        exactK(grid.time->current(), zblevels[k], &Tbex[k], &FF,
               (bedrock_is_ice_forK==true));
      }
      break;
    case 'O':
      double dum1, dum2, dum3, dum4;
      for (unsigned int k = 0; k < grid.Mz(); k++) {
        exactO(grid.z(k), &Tex[k], &dum1, &dum2, &dum3, &dum4);
      }
      for (unsigned int k = 0; k < Mbz; k++) {
        exactO(zblevels[k], &Tbex[k], &dum1, &dum2, &dum3, &dum4);
      }
      break;
    default:
      throw RuntimeError("ice and bedrock temperature errors only for tests K and O");
  }

  IceModelVec::AccessList list;
  list.add(T3);
  list.add(*bedrock_temp);

  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    Tb = bedrock_temp->get_column(i, j);
    for (unsigned int kb = 0; kb < Mbz; kb++) {
      const double Tberr = fabs(Tb[kb] - Tbex[kb]);
      maxTberr = std::max(maxTberr, Tberr);
      avbcount += 1.0;
      avTberr += Tberr;
    }
    T = T3.get_column(i, j);
    for (unsigned int k = 0; k < grid.Mz(); k++) {
      const double Terr = fabs(T[k] - Tex[k]);
      maxTerr = std::max(maxTerr, Terr);
      avcount += 1.0;
      avTerr += Terr;
    }
  }

  gmaxTerr = GlobalMax(grid.com, maxTerr);
  gavTerr = GlobalSum(grid.com, avTerr);
  double  gavcount;
  gavcount = GlobalSum(grid.com, avcount);
  gavTerr = gavTerr/std::max(gavcount, 1.0);  // avoid div by zero

  gmaxTberr = GlobalMax(grid.com, maxTberr);
  gavTberr = GlobalSum(grid.com, avTberr);
  double  gavbcount;
  gavbcount = GlobalSum(grid.com, avbcount);
  gavTberr = gavTberr/std::max(gavbcount, 1.0);  // avoid div by zero
}


void IceCompModel::computeBasalTemperatureErrors(double &gmaxTerr, double &gavTerr, double &centerTerr) {
  double     domeT, domeTexact, Terr, avTerr;

  double     dummy, z, Texact, dummy1, dummy2, dummy3, dummy4, dummy5;

  IceModelVec::AccessList list(T3);

  domeT=0; domeTexact = 0; Terr=0; avTerr=0;

  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    double r = radius(grid, i, j);
    switch (testname) {
    case 'F':
      if (r > LforFG - 1.0) {  // outside of sheet
        Texact=Tmin + ST * r;  // = Ts
      } else {
        r=std::max(r, 1.0);
        z=0.0;
        bothexact(0.0, r, &z, 1, 0.0,
                  &dummy5, &dummy, &Texact, &dummy1, &dummy2, &dummy3, &dummy4);
      }
      break;
    case 'G':
      if (r > LforFG -1.0) {  // outside of sheet
        Texact=Tmin + ST * r;  // = Ts
      } else {
        r=std::max(r, 1.0);
        z=0.0;
        bothexact(grid.time->current(), r, &z, 1, ApforG,
                  &dummy5, &dummy, &Texact, &dummy1, &dummy2, &dummy3, &dummy4);
      }
      break;
    default:
      throw RuntimeError("temperature errors only computable for tests F and G");
    }

    const double Tbase = T3.get_column(i,j)[0];
    if (i == ((int)grid.Mx() - 1) / 2 and
        j == ((int)grid.My() - 1) / 2) {
      domeT = Tbase;
      domeTexact = Texact;
    }
    // compute maximum errors
    Terr = std::max(Terr, fabs(Tbase - Texact));
    // add to sums for average errors
    avTerr += fabs(Tbase - Texact);
  }

  double gdomeT, gdomeTexact;

  gmaxTerr = GlobalMax(grid.com, Terr);
  gavTerr = GlobalSum(grid.com, avTerr);
  gavTerr = gavTerr/(grid.Mx()*grid.My());
  gdomeT = GlobalMax(grid.com, domeT);
  gdomeTexact = GlobalMax(grid.com, domeTexact);
  centerTerr = fabs(gdomeT - gdomeTexact);
}


void IceCompModel::compute_strain_heating_errors(double &gmax_strain_heating_err, double &gav_strain_heating_err) {
  double    max_strain_heating_err = 0.0, av_strain_heating_err = 0.0, avcount = 0.0;

  double   junk0, junk1;

  std::vector<double> strain_heating_exact(grid.Mz());
  std::vector<double> dummy1(grid.Mz());
  std::vector<double> dummy2(grid.Mz());
  std::vector<double> dummy3(grid.Mz());
  std::vector<double> dummy4(grid.Mz());

  const double
    ice_rho   = config.get("ice_density"),
    ice_c     = config.get("ice_specific_heat_capacity");

  const double *strain_heating;
  const IceModelVec3 &strain_heating3 = stress_balance->volumetric_strain_heating();

  IceModelVec::AccessList list;
  list.add(ice_thickness);
  list.add(strain_heating3);

  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    double r = radius(grid, i, j);
    if ((r >= 1.0) && (r <= LforFG - 1.0)) {  // only evaluate error if inside sheet
      // and not at central singularity
      switch (testname) {
      case 'F':
        bothexact(0.0, r, &(grid.z()[0]), grid.Mz(), 0.0,
                  &junk0, &junk1, &dummy1[0], &dummy2[0], &dummy3[0], &strain_heating_exact[0], &dummy4[0]);
        break;
      case 'G':
        bothexact(grid.time->current(), r, &(grid.z()[0]), grid.Mz(), ApforG,
                  &junk0, &junk1, &dummy1[0], &dummy2[0], &dummy3[0], &strain_heating_exact[0], &dummy4[0]);
        break;
      default:
        throw RuntimeError("strain-heating (strain_heating) errors only computable for tests F and G");
      }
      for (unsigned int k = 0; k < grid.Mz(); k++) {
        // scale exact strain_heating to J/(s m^3)
        strain_heating_exact[k] *= ice_rho * ice_c;
      }
      const unsigned int ks = grid.kBelowHeight(ice_thickness(i, j));
      strain_heating = strain_heating3.get_column(i, j);
      for (unsigned int k = 0; k < ks; k++) {  // only eval error if below num surface
        const double strain_heating_err = fabs(strain_heating[k] - strain_heating_exact[k]);
        max_strain_heating_err = std::max(max_strain_heating_err, strain_heating_err);
        avcount += 1.0;
        av_strain_heating_err += strain_heating_err;
      }
    }
  }

  gmax_strain_heating_err = GlobalMax(grid.com, max_strain_heating_err);
  gav_strain_heating_err = GlobalSum(grid.com, av_strain_heating_err);
  double  gavcount;
  gavcount = GlobalSum(grid.com, avcount);
  gav_strain_heating_err = gav_strain_heating_err/std::max(gavcount, 1.0);  // avoid div by zero
}


void IceCompModel::computeSurfaceVelocityErrors(double &gmaxUerr, double &gavUerr,
                                                double &gmaxWerr, double &gavWerr) {
  double    maxUerr = 0.0, maxWerr = 0.0, avUerr = 0.0, avWerr = 0.0;

  const IceModelVec3
    &u3 = stress_balance->velocity_u(),
    &v3 = stress_balance->velocity_v(),
    &w3 = stress_balance->velocity_w();

  IceModelVec::AccessList list;
  list.add(ice_thickness);
  list.add(u3);
  list.add(v3);
  list.add(w3);

  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    double xx = grid.x(i), yy = grid.y(j), r = radius(grid, i, j);
    if ((r >= 1.0) && (r <= LforFG - 1.0)) {  // only evaluate error if inside sheet
      // and not at central singularity
      double radialUex, wex;
      double dummy0, dummy1, dummy2, dummy3, dummy4;
      switch (testname) {
      case 'F':
        bothexact(0.0, r, &ice_thickness(i, j), 1, 0.0,
                  &dummy0, &dummy1, &dummy2, &radialUex, &wex, &dummy3, &dummy4);
        break;
      case 'G':
        bothexact(grid.time->current(), r, &ice_thickness(i, j), 1, ApforG,
                  &dummy0, &dummy1, &dummy2, &radialUex, &wex, &dummy3, &dummy4);
        break;
      default:
        throw RuntimeError("surface velocity errors only computed for tests F and G");
      }
      const double uex = (xx/r) * radialUex;
      const double vex = (yy/r) * radialUex;
      // note that because getValZ does linear interpolation and H[i][j] is not exactly at
      // a grid point, this causes nonzero errors even with option -eo
      const double Uerr = sqrt(PetscSqr(u3.getValZ(i, j, ice_thickness(i, j)) - uex)
                               + PetscSqr(v3.getValZ(i, j, ice_thickness(i, j)) - vex));
      maxUerr = std::max(maxUerr, Uerr);
      avUerr += Uerr;
      const double Werr = fabs(w3.getValZ(i, j, ice_thickness(i, j)) - wex);
      maxWerr = std::max(maxWerr, Werr);
      avWerr += Werr;
    }
  }

  gmaxUerr = GlobalMax(grid.com, maxUerr);
  gmaxWerr = GlobalMax(grid.com, maxWerr);
  gavUerr = GlobalSum(grid.com, avUerr);
  gavUerr = gavUerr/(grid.Mx()*grid.My());
  gavWerr = GlobalSum(grid.com, avWerr);
  gavWerr = gavWerr/(grid.Mx()*grid.My());
}


void IceCompModel::computeBasalMeltRateErrors(double &gmaxbmelterr, double &gminbmelterr) {
  double    maxbmelterr = -9.99e40, minbmelterr = 9.99e40, err;
  double    bmelt, dum1, dum2, dum3, dum4;

  if (testname != 'O') {
    throw RuntimeError("basal melt rate errors are only computable for test O");
  }

  // we just need one constant from exact solution:
  exactO(0.0, &dum1, &dum2, &dum3, &dum4, &bmelt);

  IceModelVec::AccessList list(basal_melt_rate);

  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    err = fabs(basal_melt_rate(i, j) - bmelt);
    maxbmelterr = std::max(maxbmelterr, err);
    minbmelterr = std::min(minbmelterr, err);
  }

  gmaxbmelterr = GlobalMax(grid.com, maxbmelterr);
  gminbmelterr = GlobalMin(grid.com, minbmelterr);
}


void IceCompModel::fillTemperatureSolnTestsKO() {

  double       dum1, dum2, dum3, dum4;
  double    FF;
  std::vector<double> Tcol(grid.Mz());

  // evaluate exact solution in a column; all columns are the same
  switch (testname) {
    case 'K':
      for (unsigned int k=0; k<grid.Mz(); k++) {
        exactK(grid.time->current(), grid.z(k), &Tcol[k], &FF,
               (bedrock_is_ice_forK==true));
      }
      break;
    case 'O':
      for (unsigned int k=0; k<grid.Mz(); k++) {
        exactO(grid.z(k), &Tcol[k], &dum1, &dum2, &dum3, &dum4);
      }
      break;
    default:
      throw RuntimeError("only fills temperature solutions for tests K and O");
  }

  // copy column values into 3D arrays
  IceModelVec::AccessList list(T3);

  for (Points p(grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    T3.set_column(i, j, &Tcol[0]);
  }

  // communicate T
  T3.update_ghosts();
}


void IceCompModel::fillBasalMeltRateSolnTestO() {
  double       bmelt, dum1, dum2, dum3, dum4;
  if (testname != 'O') {
    throw RuntimeError("only fills basal melt rate soln for test O");
  }

  // we just need one constant from exact solution:
  exactO(0.0, &dum1, &dum2, &dum3, &dum4, &bmelt);

  basal_melt_rate.set(bmelt);
}


void IceCompModel::initTestsKO() {

  if (testname == 'K') {
    options::Integer Mbz("-Mbz", "Number of levels in the bedrock thermal model",
                         btu->Mbz());
    if (Mbz.is_set() && Mbz < 2) {
      throw RuntimeError("pismv test K requires a bedrock thermal layer 1000m deep");
    }
  }

  IceModelVec2S bed_topography;
  bed_topography.create(grid, "topg", WITHOUT_GHOSTS);
  bed_topography.set(0);
  beddef->set_elevation(bed_topography);

  geothermal_flux.set(0.042);
  ice_thickness.set(3000.0);
  ice_thickness.copy_to(ice_surface_elevation);

  fillTemperatureSolnTestsKO();
}

BTU_Verification::BTU_Verification(const IceGrid &g, int test, bool bii)
  : BedThermalUnit(g) {
  m_testname = test;
  m_bedrock_is_ice = bii;
}

BTU_Verification::~BTU_Verification() {
  // empty
}

const IceModelVec3Custom* BTU_Verification::temperature() {
  return &m_temp;
}

void BTU_Verification::bootstrap() {

  if (m_Mbz < 2) {
    return;
  }

  std::vector<double> Tbcol(m_Mbz),
    zlevels = m_temp.get_levels();
  double dum1, dum2, dum3, dum4;
  double FF;

  // evaluate exact solution in a column; all columns are the same
  switch (m_testname) {
    case 'K':
      for (unsigned int k = 0; k < m_Mbz; k++) {
        if (exactK(m_grid.time->current(), zlevels[k], &Tbcol[k], &FF,
                   (m_bedrock_is_ice==true))) {
          throw RuntimeError::formatted("exactK() reports that level %9.7f is below B0 = -1000.0 m",
                                        zlevels[k]);
        }
      }
      break;
    case 'O':
      for (unsigned int k = 0; k < m_Mbz; k++) {
        exactO(zlevels[k], &Tbcol[k], &dum1, &dum2, &dum3, &dum4);
      }
      break;
    default:
      {
        BedThermalUnit::bootstrap();
      }
  }

  // copy column values into 3D arrays
  IceModelVec::AccessList list(m_temp);

  for (Points p(m_grid); p; p.next()) {
    m_temp.set_column(p.i(), p.j(), &Tbcol[0]);
  }
}

} // end of namespace pism
