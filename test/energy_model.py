#!/usr/bin/env python

import PISM

ctx = PISM.Context()

def create_dummy_grid():
    "Create a dummy grid"
    params = PISM.GridParameters(ctx.config)
    params.ownership_ranges_from_options(ctx.size)
    return PISM.IceGrid(ctx.ctx, params)

grid = create_dummy_grid()

zero = PISM.IceModelVec2S()
zero.create(grid, "zero", PISM.WITHOUT_GHOSTS)
zero.set(0.0)

cell_type = PISM.IceModelVec2CellType()
cell_type.create(grid, "mask", PISM.WITHOUT_GHOSTS)
cell_type.set(PISM.MASK_GROUNDED)

basal_heat_flux = PISM.IceModelVec2S()
basal_heat_flux.create(grid, "bheatflx", PISM.WITHOUT_GHOSTS)
basal_heat_flux.set(PISM.convert(ctx.unit_system, 10, "mW m-2", "W m-2"))

ice_thickness = PISM.model.createIceThicknessVec(grid)
ice_thickness.set(4000.0)
# TemperatureModel needs ice_thickness to set enthalpy in restart(...)
grid.variables().add(ice_thickness)

shelf_base_temp = PISM.IceModelVec2S()
shelf_base_temp.create(grid, "shelfbtemp", PISM.WITHOUT_GHOSTS)
shelf_base_temp.set(260.0)

surface_temp = PISM.IceModelVec2S()
surface_temp.create(grid, "surface_temp", PISM.WITHOUT_GHOSTS)
surface_temp.set(260.0)

strain_heating3 = PISM.IceModelVec3()
strain_heating3.create(grid, "sigma", PISM.WITHOUT_GHOSTS)

u = PISM.IceModelVec3()
u.create(grid, "u", PISM.WITHOUT_GHOSTS)

v = PISM.IceModelVec3()
v.create(grid, "v", PISM.WITHOUT_GHOSTS)

w = PISM.IceModelVec3()
w.create(grid, "w", PISM.WITHOUT_GHOSTS)

ice_thickness.set(4000.0)
u.set(0.0)
v.set(0.0)
w.set(0.0)

basal_melt_rate = zero
climatic_mass_balance = zero

inputs = PISM.EnergyModelInputs()

inputs.cell_type                = cell_type
inputs.basal_frictional_heating = zero
inputs.basal_heat_flux          = basal_heat_flux
inputs.ice_thickness            = ice_thickness
inputs.surface_liquid_fraction  = zero
inputs.shelf_base_temp          = shelf_base_temp
inputs.surface_temp             = surface_temp
inputs.till_water_thickness     = zero
inputs.strain_heating3          = strain_heating3
inputs.u3                       = u
inputs.v3                       = v
inputs.w3                       = w

dt = PISM.convert(ctx.unit_system, 1, "years", "seconds")

for M in [PISM.EnthalpyModel, PISM.DummyEnergyModel, PISM.TemperatureModel]:
    model = M(grid, None)

    print ""
    print "Testing %s..." % M

    print "* Bootstrapping using provided basal melt rate..."
    model.initialize(basal_melt_rate,
                     ice_thickness,
                     surface_temp,
                     climatic_mass_balance,
                     basal_heat_flux)

    print "* Performing a time step..."
    model.update(0, dt, inputs)

    try:
        model.update(0, dt)
        raise Exception("this should fail")
    except RuntimeError:
        pass

    try:
        model.max_timestep(0)
        raise Exception("this should fail")
    except RuntimeError:
        pass

    print model.stdout_flags()
    stats = model.stats()
    enthalpy = model.get_enthalpy()
    bmr = model.get_basal_melt_rate()

    try:
        temperature = model.get_temperature()
    except:
        pass

    pio = PISM.PIO(grid.com, "netcdf3")
    pio.open("enthalpy_model_state.nc", PISM.PISM_READWRITE_MOVE)
    PISM.define_time(pio,
                     ctx.config.get_string("time.dimension_name"),
                     ctx.config.get_string("time.calendar"),
                     ctx.time.units_string(),
                     ctx.unit_system)
    PISM.append_time(pio,
                     ctx.config.get_string("time.dimension_name"),
                     ctx.time.current())

    print "* Saving the model state..."
    model.write_model_state(pio)

    print "* Restarting from a saved model state..."
    model.restart(pio, 0)

    print "* Bootstrapping from a saved model state..."
    model.bootstrap(pio,
                    ice_thickness,
                    surface_temp,
                    climatic_mass_balance,
                    basal_heat_flux)

    pio.close()
