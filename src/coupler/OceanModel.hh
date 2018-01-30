// Copyright (C) 2008-2011, 2013, 2014, 2015, 2016, 2017, 2018 Ed Bueler, Constantine Khroulev, Ricarda Winkelmann,
// Gudfinna Adalgeirsdottir and Andy Aschwanden
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

#ifndef __PISMOceanModel_hh
#define __PISMOceanModel_hh

#include "pism/util/Component.hh"

namespace pism {
class IceModelVec2S;

//! @brief Ocean models and modifiers: provide sea level elevation,
//! melange back pressure, shelf base mass flux and shelf base
//! temperature.
namespace ocean {
//! A very rudimentary PISM ocean model.
class OceanModel : public Component {
public:
  // "modifier" constructor
  OceanModel(IceGrid::ConstPtr g, OceanModel* input);
  // "model" constructor
  OceanModel(IceGrid::ConstPtr g);

  virtual ~OceanModel();

  void init();

  void update(double t, double dt);

  double sea_level_elevation() const;

  const IceModelVec2S& shelf_base_temperature() const;
  const IceModelVec2S& shelf_base_mass_flux() const;
  const IceModelVec2S& melange_back_pressure_fraction() const;

protected:
  virtual void init_impl() = 0;
  virtual void update_impl(double t, double dt);

  virtual DiagnosticList diagnostics_impl() const;

  virtual double sea_level_elevation_impl() const;
  virtual const IceModelVec2S& shelf_base_temperature_impl() const;
  virtual const IceModelVec2S& shelf_base_mass_flux_impl() const;
  virtual const IceModelVec2S& melange_back_pressure_fraction_impl() const;

protected:
  OceanModel *m_input_model;
  IceModelVec2S::Ptr m_melange_back_pressure_fraction;

  static IceModelVec2S::Ptr allocate_shelf_base_temperature(IceGrid::ConstPtr g);
  static IceModelVec2S::Ptr allocate_shelf_base_mass_flux(IceGrid::ConstPtr g);
  static IceModelVec2S::Ptr allocate_melange_back_pressure(IceGrid::ConstPtr g);
};


} // end of namespace ocean
} // end of namespace pism

#endif  // __PISMOceanModel_hh
