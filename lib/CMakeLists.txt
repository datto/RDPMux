cmake_minimum_required(VERSION 3.2)
project(librdpmux C)

set(MAJOR_VERSION 0)
set(MINOR_VERSION 6)
set(PATCH_VERSION 0)
set(MUX_VERSION "${MAJOR_VERSION}.${MINOR_VERSION}.${PATCH_VERSION}")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(GENERAL_WARNING_FLAGS "-Wall -Werror -Wno-unused-variable -Wno-unused-function")
set(GENERAL_DEBUG_FLAGS "-g3 -O0 -fvar-tracking-assignments")
set(GENERAL_RELEASE_FLAGS "-mtune=native -O2 -fomit-frame-pointer")
set(GENERAL_COMPILER_FLAGS "-pipe -fstack-protector-strong --param=ssp-buffer-size=4 -fvisibility=hidden -Wl,--exclude-libs,ALL")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${GENERAL_WARNING_FLAGS} ${GENERAL_COMPILER_FLAGS}")
set(CMAKE_C_FLAGS_DEBUG "${GENERAL_DEBUG_FLAGS}")
set(CMAKE_C_FLAGS_RELEASE "${GENERAL_RELEASE_FLAGS}")

# Add debug output for debug builds
if(CMAKE_BUILD_TYPE MATCHES Debug)
         add_definitions(-DUSE_DEBUG_OUTPUT)
endif(CMAKE_BUILD_TYPE MATCHES Debug)

include(GNUInstallDirs)
file(GLOB_RECURSE SHIM_SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

add_library(librdpmux SHARED "${SHIM_SOURCE_FILES}")

set_target_properties(librdpmux PROPERTIES SOVERSION ${MAJOR_VERSION} VERSION ${MUX_VERSION})
set_target_properties(librdpmux PROPERTIES PREFIX "")

install(TARGETS
        librdpmux
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
        )

install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/include/rdpmux.h" DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}")
install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/librdpmux.pc" DESTINATION "${CMAKE_INSTALL_FULL_LIBDIR}/pkgconfig")

find_package(Glib2 REQUIRED)
if(GLIB2_FOUND)
    include_directories(${GLIB2_INCLUDE_DIRS})
    target_link_libraries(librdpmux ${GLIB2_LIBRARIES})
endif(GLIB2_FOUND)

find_package(GIO REQUIRED)
if(GIO_FOUND)
    include_directories(${GIO_INCLUDE_DIR})
    target_link_libraries(librdpmux ${GIO_LIBRARIES})
endif(GIO_FOUND)

find_package(ZeroMQ REQUIRED)
if(ZEROMQ_FOUND)
    include_directories(librdpmux ${ZEROMQ_INCLUDE_DIRS})
    target_link_libraries(librdpmux ${ZEROMQ_LIBRARIES})
endif(ZEROMQ_FOUND)

find_package(CZMQ REQUIRED)
if(CZMQ_FOUND)
    include_directories(librdpmux ${CZMQ_INCLUDE_DIRS})
    target_link_libraries(librdpmux ${CZMQ_LIBRARIES})
endif(CZMQ_FOUND)

find_package(Pixman REQUIRED)
target_link_libraries(librdpmux ${PIXMAN_LIBRARY})
include_directories(${PIXMAN_INCLUDE_DIR})

set(GLIB2_PKGCONFIG_DIRS "")

## pkgconfig variable substitution
foreach(turtles ${GLIB2_INCLUDE_DIRS})
    string(CONCAT GLIB2_PKGCONFIG_DIRS ${GLIB2_PKGCONFIG_DIRS} " -I" ${turtles})
endforeach(turtles)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/librdpmux.pc.in ${CMAKE_CURRENT_SOURCE_DIR}/librdpmux.pc @ONLY)
