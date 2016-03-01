# - Try to find GIOmm-2.4
# Once done this will define
#
#  GIOMM_FOUND - system has glibmm2
#  GIOMM_INCLUDE_DIR - the glibmm2 include directory
#  GIOMM_LIBRARY - glibmm2 library

if(GIOMM_INCLUDE_DIR AND GIOMM_LIBRARIES)
    # Already in cache, be silent
    set(GIOMM_FIND_QUIETLY TRUE)
endif(GIOMM_INCLUDE_DIR AND GIOMM_LIBRARIES)

if (NOT WIN32)
    include(UsePkgConfig)
    pkgconfig(giomm-2.4 _LibGIOMMIncDir _LibGIOMMLinkDir _LibGIOMMLinkFlags _LibGIOMMCflags)
endif(NOT WIN32)

find_path(GIOMM_MAIN_INCLUDE_DIR giomm.h
        PATH_SUFFIXES giomm-2.4
        PATHS ${_LibGIOMMIncDir} )

# search the giommconfig.h include dir under the same root where the library is found

find_library(GIOMM_LIBRARY
        NAMES giomm-2.4
        PATHS ${_LibGIOMMLinkDir} )


get_filename_component(giomm2LibDir "${GIOMM_LIBRARY}" PATH)

find_path(GIOMM_INTERNAL_INCLUDE_DIR giommconfig.h
        PATH_SUFFIXES giomm-2.4/include
        PATHS ${_LibGIOMMIncDir} "${giomm2LibDir}" ${CMAKE_SYSTEM_LIBRARY_PATH})

set(GIOMM_INCLUDE_DIR "${GIOMM_MAIN_INCLUDE_DIR}")

# not sure if this include dir is optional or required
# for now it is optional
if(GIOMM_INTERNAL_INCLUDE_DIR)
    set(GIOMM_INCLUDE_DIR ${GIOMM_INCLUDE_DIR} "${GIOMM_INTERNAL_INCLUDE_DIR}")
endif(GIOMM_INTERNAL_INCLUDE_DIR)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GIOMM  DEFAULT_MSG  GIOMM_LIBRARY GIOMM_MAIN_INCLUDE_DIR)

mark_as_advanced(GIOMM_INCLUDE_DIR GIOMM_LIBRARY)

