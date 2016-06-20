# - Try to find FreeRDP/winpr
# Once done this will define
#
#  FREERDP_FOUND - system has FreeRDP/winpr
#  FREERDP_INCLUDE_DIRS - the FreeRDP/winpr include directories
#  FREERDP_LIBRARIES - FreeRDP/winpr libraries

#find_package(PkgConfig)
#if(PKG_CONFIG_FOUND)
#    pkg_check_modules(PC_FREERDP freerdp)
#endif()

set(FREERDP_DEFINITIONS ${PC_FREERDP_CFLAGS_OTHER})

find_path(FREERDP_INCLUDE_DIR NAMES freerdp/freerdp.h
          HINTS "/opt/freerdp-nightly/include/freerdp2/")

find_path(WINPR_INCLUDE_DIR NAMES winpr/winpr.h
          HINTS "/opt/freerdp-nightly/include/winpr2/")

find_library(FREERDP_LIBRARY NAMES freerdp
             HINTS "/opt/freerdp-nightly/lib/")

find_library(FREERDP_WINPR_LIBRARY NAMES winpr
             HINTS "/opt/freerdp-nightly/lib/")

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(FREERDP DEFAULT_MSG FREERDP_LIBRARY FREERDP_INCLUDE_DIR WINPR_INCLUDE_DIR)

set(FREERDP_LIBRARIES ${FREERDP_LIBRARY} ${FREERDP_WINPR_LIBRARY} )
set(FREERDP_INCLUDE_DIRS ${FREERDP_INCLUDE_DIR} ${WINPR_INCLUDE_DIR})

mark_as_advanced(FREERDP_INCLUDE_DIR FREERDP_LIBRARY)