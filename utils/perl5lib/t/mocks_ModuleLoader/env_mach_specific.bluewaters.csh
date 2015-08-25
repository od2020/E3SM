#!/usr/bin/env csh -f 
#===============================================================================
# Automatically generated module settings for bluewaters
# DO NOT EDIT THIS FILE DIRECTLY!  Please edit env_mach_specific.xml 
# in your CASEROOT. This file is overwritten every time modules are loaded!
#===============================================================================

source /opt/modules/default/init/csh
module rm PrgEnv-pgi
module rm PrgEnv-cray
module rm PrgEnv-gnu
module rm pgi
module rm cray
if ( $COMPILER == "pgi" ) then
	module load PrgEnv-pgi
	module switch pgi pgi/14.2.0
endif
if ( $COMPILER == "gnu" ) then
	module load PrgEnv-gnu/4.2.84
	module switch gcc gcc/4.8.2
endif
if ( $COMPILER == "cray" ) then
	module load PrgEnv-cray/4.2.34
	module switch cce cce/8.2.6
endif
module load papi/5.3.2
module switch cray-mpich cray-mpich/7.0.3
module switch cray-libsci cray-libsci/12.2.0
module load torque/5.0.1
if ( $MPILIB != "mpi-serial" ) then
	module load cray-netcdf-hdf5parallel/4.3.2
	module load cray-parallel-netcdf/1.5.0
endif
if ( $MPILIB == "mpi-serial" ) then
	module load cray-netcdf/4.3.2
endif
module load cmake
module rm darshan
