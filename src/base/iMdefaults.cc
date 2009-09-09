// Copyright (C) 2004-2009 Jed Brown, Ed Bueler and Constantine Khroulev
//
// This file is part of PISM.
//
// PISM is free software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation; either version 2 of the License, or (at your option) any later
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

#include <cstring>
#include <cmath>
#include "iceModel.hh"

//! Assigns default values to the many parameters and flags in IceModel.
/*!
The order of precedence for setting parameters in PISM is:
  - default values: Reasonable values to set up the model with are given in setDefaults()
    and in file pism/src/base/iMdefaults.  setDefaults() is called in the constructor for
    IceModel.  It would be reasonable to have setDefaults() read the defaults from a
    (default!) NetCDF file of a format so that others could be substituted.
  - derived class overrides:  The constructor of a derived class can choose its own 
    defaults for data members of IceModel (and its own data members).  These will override
    the above.
  - command line options:  The driver calls IceModel::setFromOptions() after the instance of
    IceModel (or a derived class) is constructed.  setFromOptions() is virtual but should 
    usually be called first if a derived class has a setFromOptions.

The input file (\c -i or \c -boot_from) will not contain (in Feb 2008 version of PISM) any values 
for the quantities which are set in setDefaults().  (There are parameters which can be set at
the command line or by the input file, like \c grid.Mx.  For \c -i the data file has the final
word but for -boot_from the command line options have the final word.)
 
The defaults should be reasonable values under all circumstances or they should indicate 
missing values in some manner.
 */
PetscErrorCode IceModel::setDefaults() {
  PetscErrorCode ierr;
  
  ierr = verbPrintf(3,grid.com, "setting IceModel defaults...\n"); CHKERRQ(ierr);

  char alt_config[PETSC_MAX_PATH_LEN];
  PetscTruth use_alt_config;
  ierr = PetscOptionsGetString(PETSC_NULL, "-config", alt_config, PETSC_MAX_PATH_LEN, &use_alt_config);
  if (use_alt_config) {
    ierr = config.read(alt_config); CHKERRQ(ierr);
  } else {
    ierr = config.read(PISM_DefaultConfigFile); CHKERRQ(ierr);
  }
  config.print();

  // No X11 diagnostics by default, but allow them
  strcpy(diagnostic, "");
  strcpy(diagnosticBIG, "");
  showViewers = PETSC_TRUE;

  ierr = setExecName("pism"); CHKERRQ(ierr);  // drivers typically override this

  grid.vertical_spacing = EQUAL;
  
  computeSIAVelocities = PETSC_TRUE;
  transformForSurfaceGradient = PETSC_FALSE;

  useConstantNuHForSSA     = config.get_flag("use_constant_nuh_for_ssa");
  computeSurfGradInwardSSA = config.get_flag("compute_surf_grad_inward_ssa");
  ssaSystemToASCIIMatlab   = PETSC_FALSE;
  leaveNuHAloneSSA         = false;

  strcpy(ssaMatlabFilePrefix, "pism_SSA");

  holdTillYieldStress       = PETSC_FALSE;
  useConstantTillPhi        = PETSC_FALSE;
  
  shelvesDragToo = PETSC_FALSE;
  
  // set maximum |u|,|v|,|w| in ice to an (obviously) invalid number
  gmaxu = gmaxv = gmaxw = -1.0;

  end_year   = config.get("start_year") + config.get("run_length_years");

  reportHomolTemps = PETSC_TRUE;
  
  updateHmelt = PETSC_TRUE;
  realAgeForGrainSize = PETSC_FALSE;

  // set default locations of soundings and slices
  id = (grid.Mx - 1)/2;
  jd = (grid.My - 1)/2;
  kd = 0;

  // default polar stereographic projection settings: South Pole
  polar_stereographic.strings["pism_intent"] = "mapping";
  polar_stereographic.strings["grid_mapping_name"] = "polar_stereographic";
  polar_stereographic.set("straight_vertical_longitude_from_pole", 0.0);
  polar_stereographic.set("latitude_of_projection_origin", -90.0);
  polar_stereographic.set("standard_parallel", -71.0);

  return 0;
}
