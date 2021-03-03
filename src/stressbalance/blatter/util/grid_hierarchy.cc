/* Copyright (C) 2020, 2021 PISM Authors
 *
 * This file is part of PISM.
 *
 * PISM is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 3 of the License, or (at your option) any later
 * version.
 *
 * PISM is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PISM; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "grid_hierarchy.hh"
#include "DataAccess.hh"
#include "pism/util/fem/FEM.hh"
#include "pism/util/node_types.hh"

namespace pism {

DMInfo::DMInfo(DM dm) {
  if (setup(dm) != 0) {
    throw RuntimeError(PISM_ERROR_LOCATION, "failed to get DM information");
  }
}

PetscErrorCode DMInfo::setup(DM dm) {
  PetscErrorCode ierr;
  ierr = DMDAGetInfo(dm,
                     &dims,         // dimensions
                     &Mx, &My, &Mz, // grid size
                     &mx, &my, &mz, // number of processors in each direction
                     &dof,          // number of degrees of freedom
                     &stencil_width,
                     &bx, &by, &bz, // types of ghost nodes at the boundary
                     &stencil_type); CHKERRQ(ierr);

  ierr = DMDAGetOwnershipRanges(dm, &lx, &ly, &lz);
  CHKERRQ(ierr);

  return 0;
}

/*!
 * Transpose DMInfo.
 *
 * NB: This uses the STORAGE_ORDER.
 */
DMInfo DMInfo::transpose() const {
  DMInfo result = *this;

  result.mx = this->my;
  result.my = this->mz;
  result.mz = this->mx;

  result.Mx = this->My;
  result.My = this->Mz;
  result.Mz = this->Mx;

  result.lx = this->ly;
  result.ly = this->lz;
  result.lz = this->lx;

  result.bx = this->by;
  result.by = this->bz;
  result.bz = this->bx;

  return result;
}

/*!
 * z coordinates of the nodes
 *
 * @param[in] b surface elevation of the bottom of the domain
 * @param[in] H domain thickness
 * @param[in] Mz number of grid points in each vertical column
 * @param[in] k node index in the z direction
 */
double grid_z(double b, double H, int Mz, int k) {
  return b + H * k / (Mz - 1.0);
}

/* Transpose a DMDALocalInfo structure to map from PETSc's ordering to PISM's order needed
   to ensure that vertical columns are stored contiguously in RAM.

   (What a pain.)

   Map from PETSc to PISM order:

   | PETSc | PISM |
   |-------+------|
   | x     | z    |
   | y     | x    |
   | z     | y    |

   Assuming that i, j, k indexes correspond to x, y, and z PETSc's indexing order is
   [k][j][i]. After this transpose we have to use [j][i][k].

   Note that this indexing order is compatible with the PETSc-standard indexing for 2D
   Vecs: [j][i].

   All the lines changed to implement this transpose are marked with STORAGE_ORDER: that
   way you can use grep to find them.
 */
DMDALocalInfo grid_transpose(const DMDALocalInfo &input) {
  DMDALocalInfo result = input;

  result.mx = input.my;
  result.my = input.mz;
  result.mz = input.mx;

  result.xs = input.ys;
  result.ys = input.zs;
  result.zs = input.xs;

  result.xm = input.ym;
  result.ym = input.zm;
  result.zm = input.xm;

  result.gxs = input.gys;
  result.gys = input.gzs;
  result.gzs = input.gxs;

  result.gxm = input.gym;
  result.gym = input.gzm;
  result.gzm = input.gxm;

  result.bx = input.by;
  result.by = input.bz;
  result.bz = input.bx;

  return result;
}


/*!
 * Set up storage for 3D data inputs (DMDAs and Vecs)
 */
PetscErrorCode setup_level(DM dm, int mg_levels) {
  PetscErrorCode ierr;

  MPI_Comm comm;
  ierr = PetscObjectGetComm((PetscObject)dm, &comm); CHKERRQ(ierr);

  // Get grid information
  DMInfo info (dm);

  // Create a 3D DMDA and a global Vec, then stash them in dm.
  {

    DM  da;
    Vec parameters;
    int dof = 1;

    ierr = DMDACreate3d(comm,
                        info.bx, info.by, info.bz,
                        info.stencil_type,
                        info.Mx, info.My, info.Mz,
                        info.mx, info.my, info.mz,
                        dof,
                        info.stencil_width,
                        info.lx, info.ly, info.lz,
                        &da); CHKERRQ(ierr);

    ierr = DMSetUp(da); CHKERRQ(ierr);

    ierr = DMCreateGlobalVector(da, &parameters); CHKERRQ(ierr);

    ierr = PetscObjectCompose((PetscObject)dm, "3D_DM", (PetscObject)da); CHKERRQ(ierr);
    ierr = PetscObjectCompose((PetscObject)dm, "3D_DM_data", (PetscObject)parameters); CHKERRQ(ierr);

    ierr = DMDestroy(&da); CHKERRQ(ierr);

    ierr = VecDestroy(&parameters); CHKERRQ(ierr);
  }

  // get coarsening level
  PetscInt level = 0;
  ierr = DMGetCoarsenLevel(dm, &level); CHKERRQ(ierr);

  // report
  {
    info = info.transpose();
    int
      Mx = info.Mx,
      My = info.My,
      Mz = info.Mz;
    ierr = PetscPrintf(comm,
                       "BP multigrid level %D: %3d x %3d x %3d (%8d) elements\n",
                       (mg_levels - 1) - level, Mx, My, Mz, Mx * My * Mz); CHKERRQ(ierr);
  }
  return 0;
}

/*! @brief Create the restriction matrix.
 *
 * The result of this call is attached to `dm_fine` under `mat_name`.
 *
 * @param[in] fine DM corresponding to the fine grid
 * @param[in] coarse DM corresponding to the coarse grid
 * @param[in] dm_name name of the DM for 2D or 3D parameters
 * @param[in] mat_name name to use when attaching the restriction matrix to `fine`
 */
PetscErrorCode create_restriction(DM fine, DM coarse, const char *dm_name) {
  PetscErrorCode ierr;
  DM da_fine, da_coarse;
  Mat mat;
  Vec scale;

  std::string
    prefix = dm_name,
    mat_name = prefix + "_restriction",
    vec_name = prefix + "_scaling";

  /* Get the DM for parameters from the fine grid DM */
  ierr = PetscObjectQuery((PetscObject)fine, dm_name, (PetscObject *)&da_fine); CHKERRQ(ierr);
  if (!da_fine) {
    SETERRQ1(PETSC_COMM_SELF, PETSC_ERR_ARG_WRONG, "No %s composed with given DMDA", dm_name);
  }

  /* Get the DM for parameters from the coarse grid DM */
  ierr = PetscObjectQuery((PetscObject)coarse, dm_name, (PetscObject *)&da_coarse); CHKERRQ(ierr);
  if (!da_coarse) {
    SETERRQ1(PETSC_COMM_SELF, PETSC_ERR_ARG_WRONG, "No %s composed with given DMDA", dm_name);
  }

  /* call DMCreateInterpolation */
  ierr = DMCreateInterpolation(da_coarse, da_fine, &mat, &scale); CHKERRQ(ierr);

  /* attach to the fine grid DM */
  ierr = PetscObjectCompose((PetscObject)fine, vec_name.c_str(), (PetscObject)scale); CHKERRQ(ierr);
  ierr = VecDestroy(&scale); CHKERRQ(ierr);

  ierr = PetscObjectCompose((PetscObject)fine, mat_name.c_str(), (PetscObject)mat); CHKERRQ(ierr);
  ierr = MatDestroy(&mat); CHKERRQ(ierr);

  return 0;
}


/*! @brief Restrict model parameters from the `fine` grid onto the `coarse` grid.
 *
 * This function uses the restriction matrix created by coarsening_hook().
 */
PetscErrorCode restrict_data(DM fine, DM coarse, const char *dm_name) {
  PetscErrorCode ierr;
  Vec X_fine, X_coarse, scaling;
  DM da_fine, da_coarse;
  Mat mat;

  std::string
    prefix = dm_name,
    mat_name = prefix + "_restriction",
    scaling_name = prefix + "_scaling",
    vec_name = prefix + "_data";

  /* get the restriction matrix from the fine grid DM */
  ierr = PetscObjectQuery((PetscObject)fine, mat_name.c_str(), (PetscObject *)&mat); CHKERRQ(ierr);
  if (!mat) {
    SETERRQ(PETSC_COMM_SELF, PETSC_ERR_ARG_WRONG, "Failed to get the restriction matrix");
  }

  /* get the scaling vector from the fine grid DM */
  ierr = PetscObjectQuery((PetscObject)fine, scaling_name.c_str(), (PetscObject *)&scaling); CHKERRQ(ierr);
  if (!scaling) {
    SETERRQ(PETSC_COMM_SELF, PETSC_ERR_ARG_WRONG, "Failed to get the scaling vector");
  }

  /* get the DMDA from the fine grid DM */
  ierr = PetscObjectQuery((PetscObject)fine, dm_name, (PetscObject *)&da_fine); CHKERRQ(ierr);
  if (!da_fine) {
    SETERRQ(PETSC_COMM_SELF, PETSC_ERR_ARG_WRONG, "Failed to get the fine grid DM");
  }

  /* get the storage vector from the fine grid DM */
  ierr = PetscObjectQuery((PetscObject)fine, vec_name.c_str(), (PetscObject *)&X_fine); CHKERRQ(ierr);
  if (!X_fine) {
    SETERRQ(PETSC_COMM_SELF, PETSC_ERR_ARG_WRONG, "Failed to get the fine grid Vec");
  }

  /* get the DMDA from the coarse grid DM */
  ierr = PetscObjectQuery((PetscObject)coarse, dm_name, (PetscObject *)&da_coarse); CHKERRQ(ierr);
  if (!da_coarse) {
    SETERRQ(PETSC_COMM_SELF, PETSC_ERR_ARG_WRONG, "Failed to get the coarse grid DM");
  }

  /* get the storage vector from the coarse grid DM */
  ierr = PetscObjectQuery((PetscObject)coarse, vec_name.c_str(), (PetscObject *)&X_coarse); CHKERRQ(ierr);
  if (!X_coarse) {
    SETERRQ(PETSC_COMM_SELF, PETSC_ERR_ARG_WRONG, "Failed to get the coarse grid Vec");
  }

  ierr = MatRestrict(mat, X_fine, X_coarse); CHKERRQ(ierr);

  ierr = VecPointwiseMult(X_coarse, X_coarse, scaling); CHKERRQ(ierr);

  return 0;
}

} // end of namespace pism
