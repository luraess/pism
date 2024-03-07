/* Copyright (C) 2013, 2014, 2015, 2016, 2017, 2018, 2019, 2021, 2022, 2024 PISM Authors
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

#include "Exp3Calving.hh"

#include "pism/util/Mask.hh"
#include "pism/util/IceGrid.hh"
#include "pism/coupler/util/options.hh"

#include "pism/util/array/CellType.hh"
#include "pism/util/array/Vector.hh"


namespace pism {

//! @brief Calving and iceberg removal code as in https://github.com/JRowanJordan/CalvingMIP/wiki/Experiment-5
namespace calving {


Exp3Calving::Exp3Calving(IceGrid::ConstPtr grid)
  : Component(grid),
    m_calving_rate(grid, "exp3_calving_rate"),
    m_cell_type(grid, "cell_type")
{
  m_calving_rate.metadata().set_name("exp3_calving_rate");
  m_calving_rate.set_attrs("diagnostic",
                           "horizontal calving rate due to Exp5 calving",
                           "m s-1", "m year-1", "", 0);
  m_cell_type.set_attrs("internal", "cell type mask", "", "", "", 0);
}

void Exp3Calving::init() {

  m_log->message(2, "* Initializing the 'EXP3 calving' mechanism...\n");

  //m_calving_threshold = m_config->get_number("calving.exp3_calving.threshold");

  //if (m_calving_threshold <= 0.0) {
  //  throw RuntimeError::formatted(PISM_ERROR_LOCATION, "'calving.exp3_calving.threshold' has to be positive");
  //}
    
  //m_log->message(2, "  Exp3 thickness threshold: %3.3f meters.\n", m_calving_threshold);

  m_calving_along_flow = m_config->get_flag("calving.exp3_calving.calve_along_flow_direction");

  if (m_calving_along_flow) {
    m_log->message(2, "  Exp3 calving along terminal ice flow.\n");
  }

}

/**
 * Updates calving rate according to the
 * calving rule Exp3 removing ice at the shelf front 
 * Cr = - Iv;.
 *
 * @param[in] pism_mask ice cover mask
 * @param[in] ice_velocity ice velocity
 * @param[in] ice_thickness ice thickness
 *
 * @return 0 on success
 */

void Exp3Calving::update(const array::CellType1 &cell_type,
                         const array::Vector1 &ice_velocity,
                         const array::Scalar &ice_thickness) {


  m_log->message(3, "    Update Exp3 calving rate.\n");

  m_cell_type.set(0.0);

  array::AccessScope list{&cell_type, &m_cell_type, &ice_thickness, &m_calving_rate, &ice_velocity};

  // a shortcut for readability:
  const auto &Hc = m_calving_threshold;
  double C = convert(m_sys, 1.0, "m year-1", "m second-1");
  // lower calving rate threshold
  double Cmin = 1.0e-7;

  if (m_calving_along_flow) { // Here, the marginal ice velocity vectors are considered in calving rate

    for (Points p(*m_grid); p; p.next()) {
      const int i = p.i(), j = p.j();

      m_calving_rate(i, j) = 0.0;

      if (cell_type.ice_free_ocean(i, j) and cell_type.next_to_floating_ice(i, j)) {

        //m_log->message(3, "  Exp3 at %d,%d: %3.3f, %3.3f\n",i,j,ice_velocity(i, j).u,ice_velocity(i, j).v);
        if (ice_velocity(i-1, j).u > 0.0 and ice_thickness(i-1,j) > 0.0) {
          m_calving_rate(i, j) += ice_velocity(i-1, j).u;
          //m_log->message(3, "  Exp3 n: %3.3f, %3.3f at %d,%d.\n", ice_velocity(i-1, j).u/C,m_calving_rate(i, j)/C,i,j);
        }
        if (ice_velocity(i+1, j).u < 0.0 and ice_thickness(i+1,j) > 0.0) {
          m_calving_rate(i, j) -= ice_velocity(i+1, j).u;
          //m_log->message(3, "  Exp3 s: %3.3f, %3.3f at %d,%d.\n", ice_velocity(i+1, j).u/C,m_calving_rate(i, j)/C,i,j);
        }
        if (ice_velocity(i, j-1).v > 0.0 and ice_thickness(i,j-1) > 0.0) {
          m_calving_rate(i, j) += ice_velocity(i, j-1).v;
          //m_log->message(3, "  Exp3 w: %3.3f, %3.3f at %d,%d.\n", ice_velocity(i, j-1).v/C,m_calving_rate(i, j)/C,i,j);
        }
        if (ice_velocity(i, j+1).v < 0.0 and ice_thickness(i,j+1) > 0.0) {
          m_calving_rate(i, j) -= ice_velocity(i, j+1).v;
          //m_log->message(3, "  Exp3 e: %3.3f, %3.3f at %d,%d.\n", ice_velocity(i, j+1).v/C,m_calving_rate(i, j)/C,i,j);
        }
        
        m_cell_type(i, j) = 1.0;
      }
      m_calving_rate.update_ghosts();

      
      // This part shall fill up ocean cells next to calving front with mean calving rates, 
      // for cases, when calving front propagates forward with once cell per adaptive timestep (CFL)
      for (Points p(*m_grid); p; p.next()) {
        const int i = p.i(), j = p.j();

         bool next_to_calving_front = (m_cell_type(i+1, j)==1 or m_cell_type(i-1, j)==1 or m_cell_type(i, j+1)==1 or m_cell_type(i, j-1)==1);
        
        if (cell_type.ice_free_ocean(i, j) and m_calving_rate(i, j) < Cmin and next_to_calving_front) {

          auto R = m_calving_rate.star(i, j);

          int N        = 0;
          double R_sum = 0.0;
          for (auto direction : { North, East, South, West }) {
            if (R[direction] > Cmin) {
              R_sum += R[direction];
              N++;
            }
          }

          if (N > 0) {
            m_calving_rate(i, j) = R_sum / N;
          }
        }
      }
    }
  } 
  else { //only velocity magnitudes are considered in calving rate

    for (Points p(*m_grid); p; p.next()) {
      const int i = p.i(), j = p.j();

      if (cell_type.floating_ice(i, j) && cell_type.next_to_ice_free_ocean(i, j)) {
        double Iv            = ice_velocity(i, j).magnitude();
        m_calving_rate(i, j) = Iv;
      } else {
        m_calving_rate(i, j) = 0.0;
      }
    }
    m_calving_rate.update_ghosts();


    for (Points p(*m_grid); p; p.next()) {
      const int i = p.i(), j = p.j();

      if (cell_type.ice_free_ocean(i, j) and cell_type.next_to_ice(i, j)) {

        auto R = m_calving_rate.star(i, j);
        auto M = cell_type.star_int(i, j);

        int N        = 0;
        double R_sum = 0.0;
        for (auto direction : { North, East, South, West }) {
          if (mask::icy(M[direction])) {
            R_sum += R[direction];
            N++;
          }
        }

        if (N > 0) {
          m_calving_rate(i, j) = R_sum / N;
        }
      }
    } 
  }
}


DiagnosticList Exp3Calving::diagnostics_impl() const {
  return {{"exp3_calving_rate", Diagnostic::wrap(m_calving_rate)}};
}

const array::Scalar &Exp3Calving::calving_rate() const {
  return m_calving_rate;
}



} // end of namespace calving
} // end of namespace pism
