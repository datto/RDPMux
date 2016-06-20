include(FindPkgConfig)
PKG_CHECK_MODULES(PC_ZMQ "libzmq")

find_path(
        ZMQ_INCLUDE_DIRS
        NAMES zmq.h
        HINTS ${PC_ZMQ_INCLUDE_DIRS}
)

find_library(
        ZMQ_LIBRARIES
        NAMES zmq
        HINTS ${PC_ZMQ_LIBRARY_DIRS}
)

find_library(
        ZMQ_STATIC_LIBRARIES
        NAMES libzmq.a
        HINTS ${PC_ZMQ_LIBRARY_DIRS}
)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(ZMQ DEFAULT_MSG ZMQ_LIBRARIES ZMQ_INCLUDE_DIRS)
mark_as_advanced(ZMQ_LIBRARIES ZMQ_STATIC_LIBRARIES ZMQ_INCLUDE_DIRS)
