#!/bin/bash

#   Creates a credible initial state for the EISMINT-Greenland experiments.
# Uses .nc files generated by preprocess.sh.  See PISM User's Manual.

#   Recommended way to run with 2 processors is "./bootstrap.sh 2 >& bootstrap.txt &"
# which gives a transcript in bootstrap.txt.

NN=2  # default number of processors
if [ $# -gt 0 ] ; then  # if user says "bootstrap.sh 8" then NN = 8
  NN="$1"
fi
set -e  # exit on error

echo ""
echo "BOOTSTRAP.SH: running pgrn on eis_green_smoothed.nc for 1 year to smooth surface;"
echo "  creates green20km_y1.nc ..."
mpiexec -n $NN pgrn -boot_from eis_green_smoothed.nc -Mx 141 -My 83 -Lz 4000 -Mz 51 -quadZ \
       -skip 1 -y 1 -o green20km_y1.nc

echo ""
echo "BOOTSTRAP.SH: running pgrn on green20km_y1.nc for 10000 years with fixed surface"
echo "  to equilibriate temps; creates green20km_Tsteady.nc ..."
mpiexec -n $NN pgrn -i green20km_y1.nc -no_mass -y 10000 -o green20km_Tsteady.nc

## uncomment these lines if full 100k model year temperature relaxation is desired:
#mv green20km_Tsteady.nc green20km_10k.nc
#mpiexec -n $NN pgrn -if green20km_10k.nc -no_mass -y 90000 -o green20km_Tsteady

