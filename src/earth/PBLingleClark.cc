// Copyright (C) 2010, 2011, 2012, 2013, 2014 Constantine Khroulev
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

#include "PBLingleClark.hh"
#include "PIO.hh"
#include "PISMTime.hh"
#include "IceGrid.hh"
#include "pism_options.hh"
#include "PISMConfig.hh"

#include <stdexcept>
#include "error_handling.hh"

namespace pism {

PBLingleClark::PBLingleClark(IceGrid &g)
  : BedDef(g) {

  if (allocate() != 0) {
    throw std::runtime_error("PBLingleClark allocation failed");
  }

}

PBLingleClark::~PBLingleClark() {

  if (deallocate() != 0) {
    PetscPrintf(m_grid.com, "PBLingleClark::~PBLingleClark(...): deallocate() failed\n");
  }

}

PetscErrorCode PBLingleClark::allocate() {

  topg_initial.allocate_proc0_copy(Hp0);
  topg_initial.allocate_proc0_copy(bedp0);
  topg_initial.allocate_proc0_copy(Hstartp0);
  topg_initial.allocate_proc0_copy(bedstartp0);
  topg_initial.allocate_proc0_copy(upliftp0);

  bool use_elastic_model = m_config.get_flag("bed_def_lc_elastic_model");

  if (m_grid.rank() == 0) {
    bdLC.settings(m_config, use_elastic_model,
                  m_grid.Mx(), m_grid.My(), m_grid.dx(), m_grid.dy(),
                  4,     // use Z = 4 for now; to reduce global drift?
                  &Hstartp0, &bedstartp0, &upliftp0, &Hp0, &bedp0);

    bdLC.alloc();
  }

  return 0;
}

PetscErrorCode PBLingleClark::deallocate() {
  PetscErrorCode ierr;

  ierr = VecDestroy(&Hp0);
  PISM_PETSC_CHK(ierr, "VecDestroy");
  ierr = VecDestroy(&bedp0);
  PISM_PETSC_CHK(ierr, "VecDestroy");
  ierr = VecDestroy(&Hstartp0);
  PISM_PETSC_CHK(ierr, "VecDestroy");
  ierr = VecDestroy(&bedstartp0);
  PISM_PETSC_CHK(ierr, "VecDestroy");
  ierr = VecDestroy(&upliftp0);
  PISM_PETSC_CHK(ierr, "VecDestroy");

  return 0;
}

//! Initialize the Lingle-Clark bed deformation model using uplift.
void PBLingleClark::init() {
  BedDef::init();

  verbPrintf(2, m_grid.com,
             "* Initializing the Lingle-Clark bed deformation model...\n");

  correct_topg();

  topg->copy_to(topg_last);

  thk->put_on_proc0(Hstartp0);
  topg->put_on_proc0(bedstartp0);
  uplift->put_on_proc0(upliftp0);

  if (m_grid.rank() == 0) {
    bdLC.init();
    bdLC.uplift_init();
  }
}

void PBLingleClark::correct_topg() {
  bool use_special_regrid_semantics, topg_exists, topg_initial_exists;

  PIO nc(m_grid, "guess_mode");

  use_special_regrid_semantics = options::Bool("-regrid_bed_special",
                                               "Correct topg when switching to a different grid");

  // Stop if topg correction was not requiested.
  if (not use_special_regrid_semantics) {
    return;
  }

  options::String regrid_file("-regrid_file",
                              "Specifies the name of a file to regrid from");

  options::String boot_file("-boot_file",
                            "Specifies the name of the file to bootstrap from");

  // Stop if it was requested, but we're not bootstrapping *and* regridding.
  if (not (regrid_file.is_set() and boot_file.is_set())) {
    return;
  }

  nc.open(regrid_file, PISM_READONLY);

  topg_initial_exists = nc.inq_var("topg_initial");
  topg_exists = nc.inq_var("topg");
  nc.close();

  // Stop if the regridding file does not have both topg and topg_initial.
  if (not (topg_initial_exists and topg_exists)) {
    return;
  }

  // Stop if the user asked to regrid topg (in this case no correction is necessary).
  options::StringSet regrid_vars("-regrid_vars", "Specifies regridding variables", "");

  if (regrid_vars.is_set()) {
    if (set_contains(regrid_vars, "topg")) {
      verbPrintf(2, m_grid.com,
                 "  Bed elevation correction requested, but -regrid_vars contains topg...\n");
      return;
    }
  }

  verbPrintf(2, m_grid.com,
             "  Correcting topg from the bootstrapping file '%s' by adding the effect\n"
             "  of the bed deformation from '%s'...\n",
             boot_file->c_str(), regrid_file->c_str());

  IceModelVec2S topg_tmp;       // will be de-allocated at 'return 0' below.
  const unsigned int WIDE_STENCIL = m_config.get("grid_max_stencil_width");
  topg_tmp.create(m_grid, "topg", WITH_GHOSTS, WIDE_STENCIL);
  topg_tmp.set_attrs("model_state", "bedrock surface elevation (at the end of the previous run)",
                     "m", "bedrock_altitude");

  // Get topg and topg_initial from the regridding file.
  topg_initial.regrid(regrid_file, CRITICAL);
  topg_tmp.regrid(regrid_file, CRITICAL);

  // After bootstrapping, topg contains the bed elevation field from
  // -boot_file.

  topg_tmp.add(-1.0, topg_initial);
  // Now topg_tmp contains the change in bed elevation computed during the run
  // that produced -regrid_file.

  // Apply this change to topg from -boot_file:
  topg->add(1.0, topg_tmp);

  // Store the corrected topg as the new "topg_initial".
  topg->copy_to(topg_initial);

  return;
}


//! Update the Lingle-Clark bed deformation model.
void PBLingleClark::update(double my_t, double my_dt) {

  if ((fabs(my_t - m_t)   < 1e-12) &&
      (fabs(my_dt - m_dt) < 1e-12)) {
    return;
  }

  m_t  = my_t;
  m_dt = my_dt;

  double t_final = m_t + m_dt;

  // Check if it's time to update:
  double dt_beddef = t_final - t_beddef_last; // in seconds
  if ((dt_beddef < m_config.get("bed_def_interval_years", "years", "seconds") &&
       t_final < m_grid.time->end()) ||
      dt_beddef < 1e-12) {
    return;
  }

  t_beddef_last = t_final;

  thk->put_on_proc0(Hp0);
  topg->put_on_proc0(bedp0);

  if (m_grid.rank() == 0) {  // only processor zero does the step
    bdLC.step(dt_beddef, // time step, in seconds
              t_final - m_grid.time->start()); // time since the start of the run, in seconds
  }

  topg->get_from_proc0(bedp0);

  //! Finally, we need to update bed uplift and topg_last.
  compute_uplift(dt_beddef);
  topg->copy_to(topg_last);

  //! Increment the topg state counter. SIAFD relies on this!
  topg->inc_state_counter();
}

} // end of namespace pism
