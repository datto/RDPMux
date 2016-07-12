# - Try to find FreeRDP/winpr
# Once done this will define
#
#  FREERDPNIGHTLY_FOUND - system has FreeRDP/winpr
#  FREERDPNIGHTLY_INCLUDE_DIRS - the FreeRDP/winpr include directories
#  FREERDPNIGHTLY_LIBRARIES - FreeRDP/winpr libraries

#find_package(PkgConfig)
#if(PKG_CONFIG_FOUND)
#    pkg_check_modules(PC_FREERDPNIGHTLY freerdp)
#endif()

set(FREERDPNIGHTLY_DEFINITIONS ${PC_FREERDPNIGHTLY_CFLAGS_OTHER})

find_path(FREERDPNIGHTLY_INCLUDE_DIR NAMES freerdp/freerdp.h
          HINTS "/opt/freerdp-nightly/include/freerdp2/")

find_path(WINPRNIGHTLY_INCLUDE_DIR NAMES winpr/winpr.h
          HINTS "/opt/freerdp-nightly/include/winpr2/")

find_library(FREERDPNIGHTLY_LIBRARY NAMES freerdp
             HINTS "/opt/freerdp-nightly/lib/")

find_library(FREERDPNIGHTLY_WINPR_LIBRARY NAMES winpr
             HINTS "/opt/freerdp-nightly/lib/")

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(FREERDPNIGHTLY DEFAULT_MSG FREERDPNIGHTLY_LIBRARY FREERDPNIGHTLY_INCLUDE_DIR WINPRNIGHTLY_INCLUDE_DIR)

set(FREERDPNIGHTLY_LIBRARIES ${FREERDPNIGHTLY_LIBRARY} ${FREERDPNIGHTLY_WINPR_LIBRARY} )
set(FREERDPNIGHTLY_INCLUDE_DIRS ${FREERDPNIGHTLY_INCLUDE_DIR} ${WINPRNIGHTLY_INCLUDE_DIR})

mark_as_advanced(FREERDPNIGHTLY_INCLUDE_DIR FREERDPNIGHTLY_LIBRARY)