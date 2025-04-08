cmake_minimum_required(VERSION 3.20)
set(PROJ loccorr)
set(MINOR_VERSION "1")
set(MID_VERSION "0")
set(MAJOR_VERSION "0")
set(VERSION "${MAJOR_VERSION}.${MID_VERSION}.${MINOR_VERSION}")

project(${PROJ} VERSION ${VERSION} LANGUAGES C)

set(CMAKE_COLOR_MAKEFILE ON)

# here is one of two variants: all .c in directory or .c files in list
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} SOURCES)

set(CMAKE_VERBOSE_MAKEFILE "ON")

# list of options
option(DEBUG "Compile in debug mode" OFF)
option(BASLER "Add Basler cameras support" OFF)
option(GRASSHOPPER "Add GrassHopper cameras support" OFF)
option(HIKROBOT "Add HikRobot cameras support" OFF)

# default flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -W -Wextra")
set(CMAKE_COLOR_MAKEFILE ON)

# cmake -DDEBUG=yes -> debugging
if(DEBUG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Og -g3 -ggdb -fno-builtin-strlen -Werror")
    add_definitions(-DEBUG)
    set(CMAKE_BUILD_TYPE DEBUG)
    set(CMAKE_VERBOSE_MAKEFILE "ON")
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -march=native -fdata-sections -ffunction-sections")
    SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")
    set(CMAKE_BUILD_TYPE RELEASE)
endif()

message("Build type: ${CMAKE_BUILD_TYPE}")

###### pkgconfig ######
# pkg-config modules (for pkg-check-modules)
set(MODULES usefull_macros cfitsio improc)

# find packages:
SET(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR})
find_package(PkgConfig REQUIRED)
if(GRASSHOPPER)
    find_package(FLYCAP REQUIRED)
    add_definitions("-DFLYCAP_FOUND=1")
else()
    list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/grasshopper.c")
endif()
if(BASLER)
    find_package(BASLER REQUIRED)
    add_definitions("-DBASLER_FOUND=1")
else()
    list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/basler.c")
endif()
if(HIKROBOT)
    pkg_check_modules(MVS REQUIRED mvs>=2.1)
    add_definitions("-DMVS_FOUND=1")
else()
    list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/hikrobot.c")
endif()
pkg_check_modules(MODULES REQUIRED ${MODULES})

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
target_include_directories(${PROJ} PUBLIC ${MODULES_INCLUDE_DIRS} ${FLYCAP_INCLUDE_DIRS} ${BASLER_INCLUDE_DIRS} ${MVS_INCLUDE_DIRS})
# -L
target_link_directories(${PROJ} PUBLIC ${MODULES_LIBRARY_DIRS} ${FLYCAP_LIBRARY_DIRS} ${BASLER_LIBRARY_DIRS} ${MVS_LIBRARY_DIRS})
message("MOD: ${MODULES_LIBRARY_DIRS}, FC: ${FLYCAP_LIBRARY_DIRS}, MVS: ${MVS_LIBRARY_DIRS}")
# -D
add_definitions(${CFLAGS} -DLOCALEDIR=\"${LOCALEDIR}\"
    -DPACKAGE_VERSION=\"${VERSION}\" -DGETTEXT_PACKAGE=\"${PROJ}\"
    -DMINOR_VERSION=\"${MINOR_VERSION}\" -DMID_VERSION=\"${MID_VERSION}\"
    -DMAJOR_VERSION=\"${MAJOR_VERSION}\" -DTHREAD_NUMBER=${PROCESSOR_COUNT})

# -l
target_link_libraries(${PROJ} ${MODULES_LIBRARIES} ${FLYCAP_LIBRARIES} ${BASLER_LIBRARIES} ${MVS_LIBRARIES} -lm)

# Installation of the program
INSTALL(TARGETS ${PROJ} DESTINATION "bin")
