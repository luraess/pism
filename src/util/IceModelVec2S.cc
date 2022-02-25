/* Copyright (C) 2022 PISM Authors
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

#include "IceModelVec2S.hh"

#include "IceModelVec2V.hh"
#include "pism/util/VariableMetadata.hh"
#include "pism/util/pism_utilities.hh"

namespace pism {

// protected constructor
IceModelVec2S::IceModelVec2S(IceGrid::ConstPtr grid, const std::string &name,
                             int width)
  : IceModelVec2<double>(grid, name,
                         width > 0 ? WITH_GHOSTS : WITHOUT_GHOSTS, width) {
  // empty
}

// public constructor
IceModelVec2S::IceModelVec2S(IceGrid::ConstPtr grid, const std::string &name)
  : IceModelVec2<double>(grid, name, WITHOUT_GHOSTS, 1) {
  // empty
}

std::shared_ptr<IceModelVec2S> IceModelVec2S::duplicate() const {
  auto result = std::make_shared<IceModelVec2S>(this->grid(),
                                                this->get_name());
  result->metadata() = this->metadata();

  return result;
}

//! Sets an IceModelVec2 to the magnitude of a 2D vector field with components `v_x` and `v_y`.
/*! Computes the magnitude \b pointwise, so any of v_x, v_y and the IceModelVec
  this is called on can be the same.

  Does not communicate.
 */
void compute_magnitude(const IceModelVec2S &v_x,
                       const IceModelVec2S &v_y,
                       IceModelVec2S &result) {
  IceModelVec::AccessList list{&result, &v_x, &v_y};

  for (Points p(*result.grid()); p; p.next()) {
    const int i = p.i(), j = p.j();

    result(i, j) = Vector2(v_x(i, j), v_y(i, j)).magnitude();
  }

  result.inc_state_counter();          // mark as modified
}

void compute_magnitude(const IceModelVec2V &input, IceModelVec2S &result) {
  IceModelVec::AccessList list{&result, &input};

  for (Points p(*result.grid()); p; p.next()) {
    const int i = p.i(), j = p.j();

    result(i, j) = input(i, j).magnitude();
  }

  result.inc_state_counter();          // mark as modified
}

//! Masks out all the areas where \f$ M \le 0 \f$ by setting them to `fill`.
void apply_mask(const IceModelVec2S &M, double fill, IceModelVec2S &result) {
  IceModelVec::AccessList list{&result, &M};

  for (Points p(*result.grid()); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (M(i, j) <= 0.0) {
      result(i,j) = fill;
    }
  }

  result.inc_state_counter();          // mark as modified
}

//! \brief Returns the x-derivative at i,j approximated using centered finite
//! differences.
double diff_x(const IceModelVec2S &array, int i, int j) {
  return (array(i + 1,j) - array(i - 1,j)) / (2 * array.grid()->dx());
}

//! \brief Returns the y-derivative at i,j approximated using centered finite
//! differences.
double diff_y(const IceModelVec2S &array, int i, int j) {
  return (array(i,j + 1) - array(i,j - 1)) / (2 * array.grid()->dy());
}

//! \brief Returns the x-derivative at i,j approximated using centered finite
//! differences. Respects grid periodicity and uses one-sided FD at grid edges
//! if necessary.
double diff_x_p(const IceModelVec2S &array, int i, int j) {
  const auto &grid = *array.grid();

  if ((grid.periodicity() & X_PERIODIC) != 0) {
    return diff_x(array, i,j);
  }

  if (i == 0) {
    return (array(i + 1,j) - array(i,j)) / (grid.dx());
  } else if (i == (int)grid.Mx() - 1) {
    return (array(i,j) - array(i - 1,j)) / (grid.dx());
  } else {
    return diff_x(array, i,j);
 }
}

//! \brief Returns the y-derivative at i,j approximated using centered finite
//! differences. Respects grid periodicity and uses one-sided FD at grid edges
//! if necessary.
double diff_y_p(const IceModelVec2S &array, int i, int j) {
  const auto &grid = *array.grid();

  if ((grid.periodicity() & Y_PERIODIC) != 0) {
    return diff_y(array, i,j);
  }

  if (j == 0) {
    return (array(i,j + 1) - array(i,j)) / (grid.dy());
  }

  if (j == (int)grid.My() - 1) {
    return (array(i,j) - array(i,j - 1)) / (grid.dy());
  }

  return diff_y(array, i,j);
}

//! Sums up all the values in an IceModelVec2S object. Ignores ghosts.
/*! Avoids copying to a "global" vector.
 */
double sum(const IceModelVec2S &input) {
  double result = 0;

  IceModelVec::AccessList list(input);

  // sum up the local part:
  for (Points p(*input.grid()); p; p.next()) {
    result += input(p.i(), p.j());
  }

  // find the global sum:
  return GlobalSum(input.grid()->com, result);
}

//! Finds maximum over all the values in an IceModelVec2S object.  Ignores ghosts.
double max(const IceModelVec2S &input) {
  IceModelVec::AccessList list(input);

  auto grid = input.grid();

  double result = input(grid->xs(), grid->ys());
  for (Points p(*grid); p; p.next()) {
    result = std::max(result, input(p.i(), p.j()));
  }

  return GlobalMax(grid->com, result);
}

//! Finds maximum over all the absolute values in an IceModelVec2S object.  Ignores ghosts.
double absmax(const IceModelVec2S &input) {

  double result = 0.0;

  IceModelVec::AccessList list(input);
  for (Points p(*input.grid()); p; p.next()) {
    result = std::max(result, std::abs(input(p.i(), p.j())));
  }

  return GlobalMax(input.grid()->com, result);
}


//! Finds minimum over all the values in an IceModelVec2S object.  Ignores ghosts.
double min(const IceModelVec2S &input) {
  IceModelVec::AccessList list(input);

  auto grid = input.grid();

  double result = input(grid->xs(), grid->ys());
  for (Points p(*grid); p; p.next()) {
    result = std::min(result, input(p.i(), p.j()));
  }

  return GlobalMin(grid->com, result);
}

} // end of namespace pism
