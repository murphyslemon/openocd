cmake_minimum_required(VERSION 3.15)
project(OpenOCD C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Ensure PRIu32, PRIx32, and other macros are available
add_definitions(-DBINDIR="C:/Program Files/OpenOCD/bin")
add_definitions(-DPKGDATADIR="C:/Program Files/OpenOCD/share")
add_definitions(-D__STDC_FORMAT_MACROS -D__STDC_CONSTANT_MACROS)

# Manually specify Jim Tcl paths
set(JIMTCL_INCLUDE_DIRS "C:/msys64/mingw64/include")
set(JIMTCL_LIBRARIES "C:/msys64/mingw64/lib/libjim.a")

# Check for standard headers
include(CheckIncludeFile)
check_include_file("stdint.h" HAVE_STDINT_H)
check_include_file("inttypes.h" HAVE_INTTYPES_H)

# Find required libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
pkg_check_modules(LIBFTDI REQUIRED libftdi1)


# Include directories
include_directories(
        SYSTEM
        ${CMAKE_SOURCE_DIR}/src
        ${LIBUSB_INCLUDE_DIRS}
        ${LIBFTDI_INCLUDE_DIRS}
        ${JIMTCL_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/src/jtag/drivers/OpenULINK/include
)

# Source files
file(GLOB_RECURSE OPENOCD_SOURCES src/*.c)

# Define the executable
add_executable(openocd ${OPENOCD_SOURCES}
        src/jtag/drivers/OpenULINK/include/io.h)

# Link required libraries
target_link_libraries(openocd
        ${LIBUSB_LIBRARIES}
        ${LIBFTDI_LIBRARIES}
        ${JIMTCL_LIBRARIES}
)

# Set installation path
install(TARGETS openocd DESTINATION bin)