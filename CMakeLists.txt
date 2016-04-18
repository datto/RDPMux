cmake_minimum_required(VERSION 3.2)
project(rdpmux)

# Path settings
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib-static)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Compiler settings
set(GENERAL_WARNING_FLAGS "-Wall -Werror -Wno-unused-variable -Wno-unused-function")
set(GENERAL_DEBUG_FLAGS "-g3 -O0 -fvar-tracking-assignments")
set(GENERAL_RELEASE_FLAGS "-mtune=native -O2 -fomit-frame-pointer")
set(GENERAL_COMPILER_FLAGS "-pipe -fstack-protector-strong --param=ssp-buffer-size=4")
set(CMAKE_CXX_FLAGS "-std=c++11 ${CMAKE_CXX_FLAGS} ${GENERAL_WARNING_FLAGS} ${GENERAL_COMPILER_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DELPP_STL_LOGGING -DELPP_THREAD_SAFE -DELPP_DISABLE_DEFAULT_CRASH_HANDLING")
set(CMAKE_CXX_FLAGS_DEBUG "${GENERAL_DEBUG_FLAGS}")
set(CMAKE_CXX_FLAGS_RELEASE "${GENERAL_RELEASE_FLAGS}")

# Source file globbing
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")
include(GNUInstallDirs)
file(GLOB_RECURSE QEMU_RDP_SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")

add_executable(rdpmux ${QEMU_RDP_SOURCE_FILES})

find_package(RT)
target_link_libraries(rdpmux ${RT_LIBRARIES})

find_package(Threads)
target_link_libraries(rdpmux ${CMAKE_THREAD_LIBS_INIT})

find_package(Nanomsg)
target_link_libraries(rdpmux ${NANOMSG_LIBRARY})

find_package(GLibmm)
if (GLIBMM2_FOUND)
    include_directories(${GLIBMM2_INCLUDE_DIR})
    target_link_libraries(rdpmux ${GLIBMM2_LIBRARY})
endif(GLIBMM2_FOUND)

find_package(Glib2)
if(GLIB2_FOUND)
    include_directories(${GLIB2_INCLUDE_DIRS})
    target_link_libraries(rdpmux ${GLIB2_LIBRARIES})
endif(GLIB2_FOUND)

find_package(Sigc++)
if(SIGC++_FOUND)
    include_directories(${SIGC++_INCLUDE_DIR})
    target_link_libraries(rdpmux ${SIGC++_LIBRARY})
endif(SIGC++_FOUND)

find_package(Giomm)
if(GIOMM_FOUND)
    include_directories(${GIOMM_INCLUDE_DIR})
    target_link_libraries(rdpmux ${GIOMM_LIBRARY})
endif(GIOMM_FOUND)

find_package(FreeRDP)
if(FREERDP_FOUND)
    include_directories(${FREERDP_INCLUDE_DIRS})
    target_link_libraries(rdpmux ${FREERDP_LIBRARIES})
endif(FREERDP_FOUND)

find_package(Msgpack)
if(MSGPACK_FOUND)
    include_directories(${MSGPACK_INCLUDE_DIR})
    target_link_libraries(rdpmux ${MSGPACK_LIBRARIES})
endif(MSGPACK_FOUND)

find_package(Pixman)
target_link_libraries(rdpmux ${PIXMAN_LIBRARY})
include_directories(${PIXMAN_INCLUDE_DIR})

find_package( Boost COMPONENTS program_options REQUIRED)
include_directories( ${Boost_INCLUDE_DIR} )
target_link_libraries(rdpmux ${Boost_LIBRARIES})

install(TARGETS
        rdpmux
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        )
