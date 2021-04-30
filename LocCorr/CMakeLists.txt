cmake_minimum_required(VERSION 3.0)
set(PROJ loccorr)
set(MINOR_VERSION "1")
set(MID_VERSION "0")
set(MAJOR_VERSION "0")
set(VERSION "${MAJOR_VERSION}.${MID_VERSION}.${MINOR_VERSION}")

project(${PROJ} VERSION ${PROJ_VERSION} LANGUAGES C)

# default flags
set(CMAKE_C_FLAGS "-O3 -std=gnu99 -march=native")

set(CMAKE_COLOR_MAKEFILE ON)

# here is one of two variants: all .c in directory or .c files in list
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} SOURCES)

# cmake -DEBUG=1 -> debugging
if(DEFINED EBUG AND EBUG EQUAL 1)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wextra -Wall -Werror -W")
    set(CMAKE_BUILD_TYPE DEBUG)
    set(CMAKE_VERBOSE_MAKEFILE "ON")
    add_definitions(-DEBUG)
else()
    set(CMAKE_BUILD_TYPE RELEASE)
    set(CMAKE_VERBOSE_MAKEFILE "ON")
endif()

###### pkgconfig ######
# pkg-config modules (for pkg-check-modules)
set(MODULES usefull_macros cfitsio)

# find packages:
SET(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR})
find_package(PkgConfig REQUIRED)
find_package(FLYCAP    REQUIRED)
pkg_check_modules(${PROJ} REQUIRED ${MODULES})

include(FindOpenMP)
if(OPENMP_FOUND)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}")
    add_definitions(-DOMP_FOUND)
endif()
if(NOT DEFINED PROCESSOR_COUNT)
    set(PROCESSOR_COUNT 2) # by default 2 cores
    execute_process(COMMAND getconf _NPROCESSORS_ONLN OUTPUT_VARIABLE PROCESSOR_COUNT OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
message("In multithreaded operations will use ${PROCESSOR_COUNT} threads")


# change wrong behaviour with install prefix
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT AND CMAKE_INSTALL_PREFIX MATCHES "/usr/local")
else()
    message("Change default install path to /usr/local")
    set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
message("Install dir prefix: ${CMAKE_INSTALL_PREFIX}")

# exe file
add_executable(${PROJ} ${SOURCES})
# -I
include_directories(${${PROJ}_INCLUDE_DIRS} ${FLYCAP_INCLUDE_DIRS})
# -L
link_directories(${${PROJ}_LIBRARY_DIRS})
# -D
add_definitions(${CFLAGS} -DLOCALEDIR=\"${LOCALEDIR}\"
    -DPACKAGE_VERSION=\"${VERSION}\" -DGETTEXT_PACKAGE=\"${PROJ}\"
    -DMINOR_VERSION=\"${MINOR_VERSION}\" -DMID_VERSION=\"${MID_VERSION}\"
    -DMAJOR_VERSION=\"${MAJOR_VESION}\" -DTHREAD_NUMBER=${PROCESSOR_COUNT})

# -l
target_link_libraries(${PROJ} ${${PROJ}_LIBRARIES} ${FLYCAP_LIBRARIES} -lm)

# Installation of the program
INSTALL(TARGETS ${PROJ} DESTINATION "bin")
