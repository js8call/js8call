cmake_minimum_required (VERSION 3.16 FATAL_ERROR)

if (APPLE)
  #
  # The following variables define the portability and compatability attributes of the Mac OS X build
  # they are choosen with care and should not be changed without good cause.
  #
  # Among other things these options are chosen to match the portability and compatability options of the
  # Qt framework dylibs which can be checked as follows:
  #
  # otool -l <binary> | grep -A3 minos
  #
  set (CMAKE_OSX_DEPLOYMENT_TARGET 11.0
    CACHE STRING "Earliest version of OS X supported

Earliest version we can support with Qt 6 is 11.0.
Do not override this if you intend to build an official deployable installer.")
endif (APPLE)

project (js8call C CXX Fortran)

#
# CMake policies
#
if (POLICY CMP0020)
  cmake_policy (SET CMP0020 NEW) # link to Qt winmain on Windows
endif (POLICY CMP0020)

if (POLICY CMP0043)
  cmake_policy (SET CMP0043 NEW) # ignore COMPILE_DEFINITIONS_<CONFIG>
endif (POLICY CMP0043)

if (POLICY CMP0063)
  cmake_policy (SET CMP0063 NEW) # honour visibility properties for all library types
endif (POLICY CMP0063)

if (POLICY CMP0167)
  cmake_policy (SET CMP0167 NEW) # require boost installation to have cmake support
endif (POLICY CMP0167)

include (${PROJECT_SOURCE_DIR}/CMake/VersionCompute.cmake)
message (STATUS "Building ${CMAKE_PROJECT_NAME}-${wsjtx_VERSION}")

#
# project information
#
set (PROJECT_NAME "JS8Call")
set (PROJECT_VENDOR "Jordan Sherer, KN4CRD")
set (PROJECT_CONTACT "Jordan Sherer <kn4crd@gmail.com>")
set (PROJECT_COPYRIGHT "Copyright (C) 2001-2018 by Joe Taylor, K1JT, (C) 2018 by Jordan Sherer, KN4CRD")
set (PROJECT_HOMEPAGE https://groups.io/g/js8call)
set (PROJECT_SUMMARY_DESCRIPTION "${PROJECT_NAME} - Digital Modes for Weak Signal Communications in Amateur Radio.")
set (PROJECT_DESCRIPTION "${PROJECT_SUMMARY_DESCRIPTION}
 ${PROJECT_NAME} is a computer program designed to facilitate amateur
 radio communication using very weak signals.")

set (CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/CMake/Modules ${CMAKE_MODULE_PATH})

# make sure that the default configuration is a RELEASE
if (NOT CMAKE_BUILD_TYPE)
  set (CMAKE_BUILD_TYPE RELEASE CACHE STRING
    "Choose the type of build, options are: None Debug Release."
    FORCE)
endif (NOT CMAKE_BUILD_TYPE)
if (CMAKE_BUILD_TYPE MATCHES "[Dd][Ee][Bb][Uu][Gg]")
  set (is_debug_build 1)
endif ()


#
# Options & features
#
#	Some of these directly effect compilation by being defined in
#	wsjtx_config.h.in which makes them available to the C/C++
#	pre-processor.
#
include (CMakeDependentOption)

# Allow the developer to select if Dynamic or Static libraries are built
OPTION (BUILD_SHARED_LIBS "Build Shared Libraries" OFF)
# Set the LIB_TYPE variable to STATIC
SET (LIB_TYPE STATIC)
if (BUILD_SHARED_LIBS)
  # User wants to build Dynamic Libraries, so change the LIB_TYPE variable to CMake keyword 'SHARED'
  set (LIB_TYPE SHARED)
endif (BUILD_SHARED_LIBS)

option (UPDATE_TRANSLATIONS "Update source translation translations/*.ts
files (WARNING: make clean will delete the source .ts files! Danger!)")
option (WSJT_SHARED_RUNTIME "Debugging option that allows running from a shared Cloud directory.")
option (WSJT_QDEBUG_TO_FILE "Redirect Qt debuging messages to a trace file.")
option (WSJT_TRACE_CAT "Debugging option that turns on CAT diagnostics.")
option (WSJT_TRACE_CAT_POLLS "Debugging option that turns on CAT diagnostics during polling.")
option (WSJT_HAMLIB_TRACE "Debugging option that turns on minimal Hamlib internal diagnostics.")
option (WSJT_SKIP_MANPAGES "Skip *nix manpage generation." ON)
option (WSJT_RIG_NONE_CAN_SPLIT "Allow split operation with \"None\" as rig.")

CMAKE_DEPENDENT_OPTION (WSJT_HAMLIB_VERBOSE_TRACE "Debugging option that turns on full Hamlib internal diagnostics." OFF WSJT_HAMLIB_TRACE OFF)
CMAKE_DEPENDENT_OPTION (WSJT_QDEBUG_IN_RELEASE "Leave Qt debugging statements in Release configuration." OFF
  "NOT is_debug_build" OFF)
CMAKE_DEPENDENT_OPTION (WSJT_ENABLE_EXPERIMENTAL_FEATURES "Enable features not fully ready for public releases." ON
  is_debug_build OFF)
CMAKE_DEPENDENT_OPTION (WSJT_CREATE_WINMAIN
  "The target is normally built as GUI executable with a WinMain entry point on Windows,
if you want a console application instead then set this option to OFF.

If you just want to see the debug output from the application then the easiest way is to
attach a debugger which will then receive the console output inside its console." ON
  "WIN32" OFF)


set (PROJECT_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
if (NOT PROJECT_ARCHITECTURE)
  # This is supposed to happen already on Windows
  set (PROJECT_ARCHITECTURE "$ENV{PROCESSOR_ARCHITECTURE}")
endif (NOT PROJECT_ARCHITECTURE)
message (STATUS "******************************************************")
message (STATUS "Building for for: ${CMAKE_SYSTEM_NAME}-${PROJECT_ARCHITECTURE}")
message (STATUS "******************************************************")


#
# install locations
#

if (APPLE)
  set (CMAKE_INSTALL_BINDIR ${CMAKE_PROJECT_NAME}.app/Contents/MacOS)
  set (CMAKE_INSTALL_DATAROOTDIR ${CMAKE_PROJECT_NAME}.app/Contents/Resources)
endif ()

include (GNUInstallDirs)

set (PLUGIN_DESTINATION ${CMAKE_INSTALL_LIBDIR}/plugins)
set (QT_CONF_DESTINATION ${CMAKE_INSTALL_BINDIR})
if (WIN32)
  set (PLUGIN_DESTINATION plugins)
elseif (APPLE)
  set (PLUGIN_DESTINATION ${CMAKE_INSTALL_BINDIR}/../PlugIns)
  set (QT_CONF_DESTINATION ${CMAKE_INSTALL_DATAROOTDIR})
endif ()

set (WSJT_PLUGIN_DESTINATION ${PLUGIN_DESTINATION} CACHE PATH "Path for plugins")
set (WSJT_QT_CONF_DESTINATION ${QT_CONF_DESTINATION} CACHE PATH "Path for the qt.conf file")


#
# Project sources
#
set (wsjt_qt_CXXSRCS
  qt_helpers.cpp
  MessageBox.cpp
  MetaDataRegistry.cpp
  NetworkServerLookup.cpp
  revision_utils.cpp
  WF.cpp
  Radio.cpp
  RadioMetaType.cpp
  IARURegions.cpp
  Bands.cpp
  Modes.cpp
  FrequencyList.cpp
  StationList.cpp
  FrequencyLineEdit.cpp
  CandidateKeyFilter.cpp
  ForeignKeyDelegate.cpp
  TraceFile.cpp
  AudioDevice.cpp
  Transceiver.cpp
  TransceiverBase.cpp
  EmulateSplitTransceiver.cpp
  TransceiverFactory.cpp
  PollingTransceiver.cpp
  HamlibTransceiver.cpp
  HRDTransceiver.cpp
  DXLabSuiteCommanderTransceiver.cpp
  Message.cpp
  MessageError.cpp
  MessageClient.cpp
  MessageServer.cpp
  TCPClient.cpp
  HelpTextWindow.cpp
  DisplayManual.cpp
  MultiSettings.cpp
  MaidenheadLocatorValidator.cpp
  CallsignValidator.cpp
  )

set (wsjt_qtmm_CXXSRCS
  Audio/BWFFile.cpp
  )

set (js8_FSRCS
  lib/jt9.f90
  lib/jt9a.f90
  )

set (js8_CXXSRCS
  lib/ipcomm.cpp
  )

set (wsjtx_CXXSRCS
  DriftingDateTime.cpp
  logbook/adif.cpp
  logbook/countrydat.cpp
  logbook/countriesworked.cpp
  logbook/logbook.cpp
  fileutils.cpp
  PSKReporter.cpp
  Modulator.cpp
  Detector.cpp
  logqso.cpp
  decodedtext.cpp
  soundout.cpp
  soundin.cpp
  SignalMeter.cpp
  plotter.cpp
  widegraph.cpp
  about.cpp
  varicode.cpp
  jsc.cpp
  jsc_list.cpp
  jsc_map.cpp
  jsc_checker.cpp
  SelfDestructMessageBox.cpp
  messagereplydialog.cpp
  APRSISClient.cpp
  SpotClient.cpp
  Inbox.cpp
  messagewindow.cpp
  mainwindow.cpp
  Configuration.cpp
  main.cpp
  TransmitTextEdit.cpp
  AudioDecoder.cpp
  NotificationAudio.cpp
  ProcessThread.cpp
  Decoder.cpp
  LazyFillComboBox.cpp
  JS8Submode.cpp
  AttenuationSlider.cpp
  )

set (wsjt_CXXSRCS
  lib/crc12.cpp
  )
# deal with a GCC v6 UB error message
set_source_files_properties (
  lib/crc12.cpp
  PROPERTIES COMPILE_FLAGS -fpermissive)

if (WIN32)
  set (wsjt_CXXSRCS
    ${wsjt_CXXSRCS}
    killbyname.cpp
    )

  set (wsjt_qt_CXXSRCS
    ${wsjt_qt_CXXSRCS}
    OmniRigTransceiver.cpp
    )
endif (WIN32)

set (wsjt_FSRCS
  # put module sources first in the hope that they get rebuilt before use
  lib/crc.f90
  lib/fftw3mod.f90
  lib/iso_c_utilities.f90
  lib/options.f90
  lib/packjt.f90
  lib/readwav.f90
  lib/timer_C_wrapper.f90
  lib/timer_impl.f90
  lib/timer_module.f90
  lib/wavhdr.f90
  lib/js8a_module.f90
  lib/js8a_decode.f90
  lib/js8b_module.f90
  lib/js8b_decode.f90
  lib/js8c_module.f90
  lib/js8c_decode.f90
  lib/js8e_module.f90
  lib/js8e_decode.f90
  lib/js8i_module.f90
  lib/js8i_decode.f90

  # remaining non-module sources
  lib/azdist.f90
  lib/ft8/bpdecode174.f90
  lib/ft8/chkcrc12a.f90
  lib/coord.f90
  lib/db.f90
  lib/decoder.f90
  lib/deg2grid.f90
  lib/determ.f90
  lib/ft8/encode174.f90
  lib/ft8/extractmessage174.f90
  lib/fil4.f90
  lib/filbig.f90
  lib/flat1.f90
  lib/flat4.f90
  lib/fmtmsg.f90
  lib/four2a.f90
  lib/js8/genjs8.f90
  lib/geodist.f90
  lib/grid2deg.f90
  lib/indexx.f90
  lib/init_random_seed.f90
  lib/nuttal_window.f90
  lib/ft8/osd174.f90
  lib/pctile.f90
  lib/polyfit.f90
  lib/prog_args.f90
  lib/sleep_msec.f90
  lib/smo.f90
  lib/shell.f90
  lib/stdmsg.f90
  lib/symspec.f90
  lib/twkfreq.f90
  lib/ft8/twkfreq1.f90
  )

# temporary workaround for a gfortran v7.3 ICE on Fedora 27 64-bit

set (sqlite3_CSRCS
  vendor/sqlite3/sqlite3.c
  )

set (wsjt_CSRCS
  lib/gran.c
  lib/init_random_seed.c
  lib/usleep.c
  lib/wisdom.c
  ${ldpc_CSRCS}
  )

set (wsjt_qt_UISRCS
  wf_palette_design_dialog.ui
  )

set (wsjtx_UISRCS
  mainwindow.ui
  messagewindow.ui
  about.ui
  widegraph.ui
  logqso.ui
  Configuration.ui
  messagereplydialog.ui
  )

set (all_CXXSRCS
  ${wsjt_CXXSRCS}
  ${wsjt_qt_CXXSRCS}
  ${wsjt_qtmm_CXXSRCS}
  ${js8_CXXSRCS}
  ${wsjtx_CXXSRCS}
  )

set (all_C_and_CXXSRCS
  ${sqlite3_CSRCS}
  ${wsjt_CSRCS}
  ${all_CXXSRCS}
  )

set (TOP_LEVEL_RESOURCES
  cty.dat
  icons/Darwin/JS8Call.iconset/icon_128x128.png
  contrib/gpl-v3-logo.svg
  )

set (PALETTE_FILES
  Palettes/Banana.pal
  Palettes/Blue1.pal
  Palettes/Blue2.pal
  Palettes/Blue3.pal
  Palettes/Brown.pal
  Palettes/Cyan1.pal
  Palettes/Cyan2.pal
  Palettes/Cyan3.pal
  Palettes/Default.pal
  Palettes/Digipan.pal
  Palettes/Fldigi.pal
  Palettes/Gray1.pal
  Palettes/Gray2.pal
  Palettes/Green1.pal
  Palettes/Green2.pal
  Palettes/Jungle.pal
  Palettes/Linrad.pal
  Palettes/Negative.pal
  Palettes/Orange.pal
  Palettes/Pink.pal
  Palettes/Rainbow.pal
  Palettes/Scope.pal
  Palettes/Sunburst.pal
  Palettes/VK4BDJ.pal
  Palettes/YL2KF.pal
  Palettes/Yellow1.pal
  Palettes/Yellow2.pal
  Palettes/ZL1FZ.pal
)

if (APPLE)
  set (WSJTX_ICON_FILE ${CMAKE_PROJECT_NAME}.icns)
  set (ICONSRCS
    icons/Darwin/${CMAKE_PROJECT_NAME}.iconset/icon_16x16.png
    icons/Darwin/${CMAKE_PROJECT_NAME}.iconset/icon_16x16@2x.png
    icons/Darwin/${CMAKE_PROJECT_NAME}.iconset/icon_32x32.png
    icons/Darwin/${CMAKE_PROJECT_NAME}.iconset/icon_32x32@2x.png
    icons/Darwin/${CMAKE_PROJECT_NAME}.iconset/icon_128x128.png
    icons/Darwin/${CMAKE_PROJECT_NAME}.iconset/icon_128x128@2x.png
    icons/Darwin/${CMAKE_PROJECT_NAME}.iconset/icon_256x256.png
    icons/Darwin/${CMAKE_PROJECT_NAME}.iconset/icon_256x256@2x.png
    icons/Darwin/${CMAKE_PROJECT_NAME}.iconset/icon_512x512.png
    icons/Darwin/${CMAKE_PROJECT_NAME}.iconset/icon_512x512@2x.png
    )
  add_custom_command (
    OUTPUT ${WSJTX_ICON_FILE}
    COMMAND iconutil -c icns --output "${CMAKE_BINARY_DIR}/${WSJTX_ICON_FILE}" "${CMAKE_SOURCE_DIR}/icons/Darwin/${CMAKE_PROJECT_NAME}.iconset"
    DEPENDS ${ICONSRCS}
    COMMENT "Building Icons"
    )
endif (APPLE)

set_source_files_properties (${WSJTX_ICON_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

# The filbig subroutine calls fftwf_plan_dft_1d using the same actual argument for the
# in and out parameters, which is perfectly fine, but the compiler gets upset about it.
set_source_files_properties (lib/filbig.f90 PROPERTIES COMPILE_FLAGS "-Wno-aliasing")

if (WSJT_QDEBUG_IN_RELEASE)
  # context info in Qt message handler in release configuration
  set_property (DIRECTORY APPEND PROPERTY
    COMPILE_DEFINITIONS $<$<NOT:$<CONFIG:Debug>>:QT_MESSAGELOGCONTEXT>
    )
else (WSJT_QDEBUG_IN_RELEASE)
  # disable Qt trace and warning messages from release configurations
  set_property (DIRECTORY APPEND PROPERTY
    COMPILE_DEFINITIONS $<$<NOT:$<CONFIG:Debug>>:QT_NO_DEBUG_OUTPUT;QT_NO_WARNING_OUTPUT>
    )
endif (WSJT_QDEBUG_IN_RELEASE)

set_property (SOURCE ${all_C_and_CXXSRCS} APPEND_STRING PROPERTY COMPILE_FLAGS " -include wsjtx_config.h")
set_property (SOURCE ${all_C_and_CXXSRCS} APPEND PROPERTY OBJECT_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/wsjtx_config.h)

if (WIN32)
  # generate the OmniRig COM interface source
  find_program (DUMPCPP dumpcpp)
  if (DUMPCPP-NOTFOUND)
    message (FATAL_ERROR "dumpcpp tool not found")
  endif (DUMPCPP-NOTFOUND)
  execute_process (
    COMMAND ${DUMPCPP} -getfile {4FE359C5-A58F-459D-BE95-CA559FB4F270}
    OUTPUT_VARIABLE AXSERVER
    OUTPUT_STRIP_TRAILING_WHITESPACE
    )
  string (STRIP "${AXSERVER}" AXSERVER)
  if (NOT AXSERVER)
    message (FATAL_ERROR "You need to install OmniRig on this computer")
  endif (NOT AXSERVER)
  string (REPLACE "\"" "" AXSERVER ${AXSERVER})
  file (TO_CMAKE_PATH ${AXSERVER} AXSERVERSRCS)
endif (WIN32)


#
# decide on platform specifc packing and fixing up
#
if (APPLE)
  set (WSJTX_BUNDLE_VERSION ${wsjtx_VERSION})
  set (CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR})

  # make sure CMAKE_INSTALL_PREFIX ends in /
  string (LENGTH "${CMAKE_INSTALL_PREFIX}" LEN)
  math (EXPR LEN "${LEN} -1" )
  string (SUBSTRING "${CMAKE_INSTALL_PREFIX}" ${LEN} 1 ENDCH)
  if (NOT ${ENDCH} STREQUAL "/")
    set (CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}/")
  endif ()
endif (APPLE)


#
# find some useful tools
#
include (CheckTypeSize)
include (CheckSymbolExists)
find_program(CTAGS ctags)
find_program(ETAGS etags)

#
# Boost
#
find_package (Boost 1.63 REQUIRED)
  include_directories (${Boost_INCLUDE_DIRS})

#
# OpenMP
#
find_package (OpenMP)

#
# fftw3 single precision library
#
find_package (FFTW3 REQUIRED COMPONENTS double single threads)

#
# hamlib setup
#
find_package (Hamlib REQUIRED)
find_program (RIGCTL_EXE rigctl)
find_program (RIGCTLD_EXE rigctld)

check_type_size (CACHE_ALL HAMLIB_OLD_CACHING)
check_symbol_exists (rig_set_cache_timeout_ms "hamlib/rig.h" HAVE_HAMLIB_CACHING)

# find_package (Usb REQUIRED)

message (STATUS "Hamlib_INCLUDE_DIRS: ${Hamlib_INCLUDE_DIRS}")
message (STATUS "Hamlib_LIBRARIES: ${Hamlib_LIBRARIES}")

#
# Qt6 setup
#
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Multimedia SerialPort)

if (WIN32)
  add_definitions (-DQT_NEEDS_QTMAIN)
  find_package (Qt6 REQUIRED COMPONENTS AxContainer AxServer)
endif (WIN32)


#
# Library building setup
#
include (GenerateExportHeader)
set (CMAKE_CXX_VISIBILITY_PRESET hidden)
set (CMAKE_C_VISIBILITY_PRESET hidden)
set (CMAKE_Fortran_VISIBILITY_PRESET hidden)
set (CMAKE_VISIBILITY_INLINES_HIDDEN ON)
#set (CMAKE_INCLUDE_CURRENT_DIR_IN_INTERFACE ON)
set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)


#
# C & C++ setup
#
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -fexceptions -frtti")

if (NOT APPLE)
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-pragmas")
  if (${OPENMP_FOUND})
    if (OpenMP_C_FLAGS)
      set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_C_FLAGS}")
      set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    endif ()
  endif ()
  set (CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -fdata-sections -ffunction-sections")
  set (CMAKE_C_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL} -fdata-sections -ffunction-sections")
  set (CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fdata-sections -ffunction-sections")
  set (CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL} -fdata-sections -ffunction-sections")
endif (NOT APPLE)

if (WIN32)
  set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c++17 -std=gnu++17")
endif (WIN32)
if (APPLE AND ${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -stdlib=libc++")
else ()
  set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --std=gnu++17 -pthread")
endif ()


#
# Fortran setup
#
set (General_FFLAGS "-Wall -Wno-conversion -fno-second-underscore")

# FFLAGS depend on the compiler
get_filename_component (Fortran_COMPILER_NAME ${CMAKE_Fortran_COMPILER} NAME)

if (Fortran_COMPILER_NAME MATCHES "gfortran.*")
  # gfortran

  # CMake compiler test is supposed to do this but doesn't yet
  if (CMAKE_OSX_DEPLOYMENT_TARGET)
    set (CMAKE_Fortran_FLAGS "${CMAKE_Fortran_FLAGS} -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
  endif (CMAKE_OSX_DEPLOYMENT_TARGET)
  if (CMAKE_OSX_SYSROOT)
    set (CMAKE_Fortran_FLAGS "${CMAKE_Fortran_FLAGS} -isysroot ${CMAKE_OSX_SYSROOT}")
  endif (CMAKE_OSX_SYSROOT)

  set (CMAKE_Fortran_FLAGS_RELEASE "${CMAKE_Fortran_FLAGS_RELEASE} -frecursive -fbounds-check -funroll-all-loops -fno-f2c ${General_FFLAGS}")
  set (CMAKE_Fortran_FLAGS_DEBUG   "${CMAKE_Fortran_FLAGS_DEBUG} -frecursive -fbounds-check -fno-f2c ${General_FFLAGS}")
elseif (Fortran_COMPILER_NAME MATCHES "ifort.*")
  # ifort (untested)
  set (CMAKE_Fortran_FLAGS_RELEASE "${CMAKE_Fortran_FLAGS_RELEASE} -f77rtl ${General_FFLAGS}")
  set (CMAKE_Fortran_FLAGS_DEBUG   "${CMAKE_Fortran_FLAGS_DEBUG} -f77rtl ${General_FFLAGS}")
elseif (Fortran_COMPILER_NAME MATCHES "g77")
  # g77
  set (CMAKE_Fortran_FLAGS_RELEASE "${CMAKE_Fortran_FLAGS_RELEASE} -funroll-all-loops -fno-f2c -m32 ${General_FFLAGS}")
  set (CMAKE_Fortran_FLAGS_DEBUG   "${CMAKE_Fortran_FLAGS_DEBUG} -fbounds-check -fno-f2c -m32 ${General_FFLAGS}")
else (Fortran_COMPILER_NAME MATCHES "gfortran.*")
  message ("CMAKE_Fortran_COMPILER full path: " ${CMAKE_Fortran_COMPILER})
  message ("Fortran compiler: " ${Fortran_COMPILER_NAME})
  message ("No optimized Fortran compiler flags are known, we just try -O3...")
  set (CMAKE_Fortran_FLAGS_RELEASE "${CMAKE_Fortran_FLAGS_RELEASE} -O3 ${General_FFLAGS}")
  set (CMAKE_Fortran_FLAGS_DEBUG   "${CMAKE_Fortran_FLAGS_DEBUG} -fbounds-check ${General_FFLAGS}")
endif (Fortran_COMPILER_NAME MATCHES "gfortran.*")


#
# Linker setup
#
if (NOT APPLE)
  set (CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -Wl,--gc-sections")
  set (CMAKE_EXE_LINKER_FLAGS_MINSIZEREL "${CMAKE_EXE_LINKER_FLAGS_MINSIZEREL} -Wl,--gc-sections")
endif (NOT APPLE)


#
# setup and test Fortran C/C++ interaction
#

include (FortranCInterface)
FortranCInterface_VERIFY (CXX)
FortranCInterface_HEADER (FC.h MACRO_NAMESPACE "FC_" SYMBOL_NAMESPACE "FC_"
  SYMBOLS
  grayline
  )


#
# sort out pre-requisites
#

#
# Setup RPATH so that built executable targets will run in both the
# build tree and the install location without having to set a
# (DYLD|LD)_LIBRARY_PATH override.
#

# use the full RPATH of the build tree
set (CMAKE_SKIP_BUILD_RPATH FALSE)

# when building, don't use the install RPATH, it will still be used
# later on in the install phase
set (CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)

# set (CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")

# add the automaticaly determined parts of the RPATH which point to
# directories outside of the build tree to the install RPATH
set (CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# the RPATH to be used when installing, but only if it's not a system
# directory
# list (FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}" isSystemDir)
# if ("${isSystemDir}" STREQUAL "-1")
#   set (CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
# endif ("${isSystemDir}" STREQUAL "-1")

set (QT_NEED_RPATH FALSE)
if (NOT "${QT_LIBRARY_DIR}" STREQUAL "/lib" AND NOT "${QT_LIBRARY_DIR}" STREQUAL "/usr/lib" AND NOT "${QT_LIBRARY_DIR}" STREQUAL "/lib64" AND NOT "${QT_LIBRARY_DIR}" STREQUAL "/usr/lib64")
  set (QT_NEED_RPATH TRUE)
endif ()

#
# stuff only qmake can tell us
#
get_target_property (QMAKE_EXECUTABLE Qt6::qmake LOCATION)
function (QUERY_QMAKE VAR RESULT)
  execute_process (
    COMMAND ${QMAKE_EXECUTABLE} -query ${VAR}
    RESULT_VARIABLE return_code
    OUTPUT_VARIABLE output
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if (NOT return_code)
    file (TO_CMAKE_PATH "${output}" output)
    set (${RESULT} ${output} PARENT_SCOPE)
  endif (NOT return_code)
  message (STATUS "Asking qmake for ${RESULT} and got ${output}")
endfunction (QUERY_QMAKE)

query_qmake (QT_INSTALL_PLUGINS QT_PLUGINS_DIR)
query_qmake (QT_HOST_DATA QT_DATA_DIR)
set (QT_MKSPECS_DIR ${QT_DATA_DIR}/mkspecs)

# Tell CMake to run moc when necessary
set (CMAKE_AUTOMOC ON)
include_directories (${CMAKE_CURRENT_BINARY_DIR})

# Tell CMake to run rcc when necessary
set (CMAKE_AUTORCC ON)

# don't use Qt "keywords" signal, slot, emit in generated files to
# avoid compatability issue with other libraries
# ADD_DEFINITIONS (-DQT_NO_KEYWORDS)
# ADD_DEFINITIONS (-DUNICODE)	#as per qmake

# As moc files are generated in the binary dir, tell CMake to always
# look for includes there:
set (CMAKE_INCLUDE_CURRENT_DIR ON)

# project definitions
add_definitions (-DQT6 -DCMAKE_BUILD -DBIGSYM=1)
if (CMAKE_HOST_UNIX)
  add_definitions (-DUNIX)
elseif (CMAKE_HOST_WIN32)
  add_definitions (-DWIN32)
endif ()

#
# source navigation
#
set (sources
  ${CMAKE_SOURCE_DIR}/*
  ${CMAKE_SOURCE_DIR}/logbook/*
  ${CMAKE_SOURCE_DIR}/lib/*
  )
add_custom_target (ctags COMMAND ${CTAGS} -o ${CMAKE_SOURCE_DIR}/tags -R ${sources})
add_custom_target (etags COMMAND ${ETAGS} -o ${CMAKE_SOURCE_DIR}/TAGS -R ${sources})


# embedded resources
function (add_resources resources path)
  foreach (resource_file_ ${ARGN})
    get_filename_component (name_ ${resource_file_} NAME)
    file (TO_NATIVE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/${resource_file_} source_)
    file (TO_NATIVE_PATH ${path}/${name_} dest_)
    set (resources_ "${resources_}\n    <file alias=\"${dest_}\">${source_}</file>")
    set (${resources} ${${resources}}${resources_} PARENT_SCOPE)
  endforeach (resource_file_ ${ARGN})
endfunction (add_resources resources path)

add_resources (wsjtx_RESOURCES "" ${TOP_LEVEL_RESOURCES})
add_resources (wsjtx_RESOURCES /Palettes ${PALETTE_FILES})

configure_file (wsjtx.qrc.in wsjtx.qrc @ONLY)


# UI generation
qt6_wrap_ui (wsjt_qt_GENUISRCS ${wsjt_qt_UISRCS})
qt6_wrap_ui (wsjtx_GENUISRCS ${wsjtx_UISRCS})

# Resource generation
qt6_add_resources (wsjtx_RESOURCES_RCC ${CMAKE_BINARY_DIR}/wsjtx.qrc)

# AX COM servers
if (WIN32)
  include (QtAxMacros)
  wrap_ax_server (GENAXSRCS ${AXSERVERSRCS})
endif (WIN32)


#
# targets
#

# build a library of package functionality (without and optionally with OpenMP support)
add_library (wsjt_cxx STATIC ${wsjt_CSRCS} ${wsjt_CXXSRCS})

# build an OpenMP variant of the Fortran library routines
add_library (wsjt_fort STATIC ${wsjt_FSRCS})
target_link_libraries (wsjt_fort  ${FFTW3_LIBRARIES})
if (${OPENMP_FOUND} OR APPLE)
  add_library (wsjt_fort_omp STATIC ${wsjt_FSRCS})
  target_link_libraries (wsjt_fort_omp  ${FFTW3_LIBRARIES})
  if (OpenMP_C_FLAGS)
    set_target_properties (wsjt_fort_omp
      PROPERTIES
      COMPILE_FLAGS "${OpenMP_C_FLAGS}"
      )
  endif ()
  set_target_properties (wsjt_fort_omp
    PROPERTIES
    Fortran_MODULE_DIRECTORY ${CMAKE_BINARY_DIR}/fortran_modules_omp
    )
  file (MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/fortran_modules_omp)
  if (APPLE)
    # On  Mac  we don't  have  working  OpenMP  support in  the  C/C++
    # compilers so  we have to manually  set the correct flags  to get
    # OpenMP support in js8.
    target_compile_options (wsjt_fort_omp
      PRIVATE
      $<$<COMPILE_LANGUAGE:Fortran>:-fopenmp>   # assumes GNU style Fortran compiler
      )
  endif (APPLE)
endif (${OPENMP_FOUND} OR APPLE)

# build a library of package Qt functionality
add_library (wsjt_qt STATIC ${wsjt_qt_CXXSRCS} ${wsjt_qt_GENUISRCS} ${GENAXSRCS})
# set wsjtx_udp exports to static variants
target_compile_definitions (wsjt_qt PUBLIC UDP_STATIC_DEFINE)
target_link_libraries (wsjt_qt Hamlib::Hamlib Qt6::Widgets Qt6::Network Qt6::SerialPort)
if (WIN32)
  target_link_libraries (wsjt_qt Qt6::AxContainer Qt6::AxServer)
endif (WIN32)

add_library (wsjt_qtmm STATIC ${wsjt_qtmm_CXXSRCS} ${wsjt_qtmm_GENUISRCS})
target_link_libraries (wsjt_qtmm Qt6::Multimedia)

add_executable (ldpcsim174js8a lib/js8/ldpcsim174js8a.f90 wsjtx.rc)
target_link_libraries (ldpcsim174js8a wsjt_fort wsjt_cxx)

add_executable (ldpcsim174js8b lib/js8/ldpcsim174js8b.f90 wsjtx.rc)
target_link_libraries (ldpcsim174js8b wsjt_fort wsjt_cxx)

add_executable (ldpcsim174js8c lib/js8/ldpcsim174js8c.f90 wsjtx.rc)
target_link_libraries (ldpcsim174js8c wsjt_fort wsjt_cxx)

add_executable (ldpcsim174js8e lib/js8/ldpcsim174js8e.f90 wsjtx.rc)
target_link_libraries (ldpcsim174js8e wsjt_fort wsjt_cxx)

add_executable (ldpcsim174js8i lib/js8/ldpcsim174js8i.f90 wsjtx.rc)
target_link_libraries (ldpcsim174js8i wsjt_fort wsjt_cxx)

add_executable (js8 ${js8_FSRCS} ${js8_CXXSRCS} wsjtx.rc)
if (${OPENMP_FOUND} OR APPLE)
  if (APPLE)
    # On  Mac  we don't  have  working  OpenMP  support in  the  C/C++
    # compilers so we  have to manually set the  correct linking flags
    # and libraries to get OpenMP support in js8.
    set_target_properties (js8
      PROPERTIES
      Fortran_MODULE_DIRECTORY ${CMAKE_BINARY_DIR}/fortran_modules_omp
      LINK_LIBRARIES "gomp;gcc_s.1" # assume GNU libgcc OpenMP
      )
    target_compile_options (js8
      PRIVATE
      $<$<COMPILE_LANGUAGE:Fortran>:-fopenmp>   # assumes GNU style Fortran compiler
      )
  else (APPLE)
    if (OpenMP_C_FLAGS)
      set_target_properties (js8
        PROPERTIES
        COMPILE_FLAGS "${OpenMP_C_FLAGS}"
        LINK_FLAGS "${OpenMP_C_FLAGS}"
        )
    endif ()
    set_target_properties (js8
      PROPERTIES
      Fortran_MODULE_DIRECTORY ${CMAKE_BINARY_DIR}/fortran_modules_omp
      )
  endif (APPLE)
  if (WIN32)
    # set_target_properties (js8 PROPERTIES
    #   LINK_FLAGS -Wl,--stack,16777216
    #   )
  endif ()
  target_link_libraries (js8 wsjt_fort_omp wsjt_cxx Qt6::Core)
else (${OPENMP_FOUND} OR APPLE)
  target_link_libraries (js8 wsjt_fort wsjt_cxx Qt6::Core)
endif (${OPENMP_FOUND} OR APPLE)

# build the main application
add_executable (js8call MACOSX_BUNDLE
  ${sqlite3_CSRCS}
  ${wsjtx_CXXSRCS}
  ${wsjtx_GENUISRCS}
  wsjtx.rc
  ${WSJTX_ICON_FILE}
  ${wsjtx_RESOURCES_RCC}
  images.qrc
  )

if (WSJT_CREATE_WINMAIN)
  set_target_properties (js8call PROPERTIES WIN32_EXECUTABLE ON)
endif (WSJT_CREATE_WINMAIN)

set_target_properties (js8call PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Darwin/Info.plist.in"
  MACOSX_BUNDLE_INFO_STRING "${WSJTX_DESCRIPTION_SUMMARY}"
  MACOSX_BUNDLE_ICON_FILE "${WSJTX_ICON_FILE}"
  MACOSX_BUNDLE_BUNDLE_VERSION ${wsjtx_VERSION}
  MACOSX_BUNDLE_SHORT_VERSION_STRING "v${wsjtx_VERSION}"
  MACOSX_BUNDLE_LONG_VERSION_STRING "Version ${wsjtx_VERSION}"
  MACOSX_BUNDLE_BUNDLE_NAME "${PROJECT_NAME}"
  MACOSX_BUNDLE_BUNDLE_EXECUTABLE_NAME "${PROJECT_NAME}"
  MACOSX_BUNDLE_COPYRIGHT "${PROJECT_COPYRIGHT}"
  MACOSX_BUNDLE_GUI_IDENTIFIER "org.kn4crd.js8call"
  )

target_include_directories (js8call PRIVATE ${FFTW3_INCLUDE_DIRS})
if (APPLE)
  target_link_libraries (js8call wsjt_fort wsjt_cxx wsjt_qt wsjt_qtmm Hamlib::Hamlib  ${FFTW3_LIBRARIES})
else ()
  target_link_libraries (js8call wsjt_fort_omp wsjt_cxx wsjt_qt wsjt_qtmm Hamlib::Hamlib  ${FFTW3_LIBRARIES})
  if (OpenMP_C_FLAGS)
    set_target_properties (js8call PROPERTIES
      COMPILE_FLAGS "${OpenMP_C_FLAGS}"
      LINK_FLAGS "${OpenMP_C_FLAGS}"
      )
  endif ()
  set_target_properties (js8call PROPERTIES
    Fortran_MODULE_DIRECTORY ${CMAKE_BINARY_DIR}/fortran_modules_omp
    )
  if (WIN32)
    set_target_properties (js8call PROPERTIES
      LINK_FLAGS -Wl,--stack,16777216
      )
  endif ()
endif ()

# if (UNIX)
#   if (NOT WSJT_SKIP_MANPAGES)
#     add_subdirectory (manpages)
#     add_dependencies (js8call manpages)
#   endif (NOT WSJT_SKIP_MANPAGES)
#   if (NOT APPLE)
#     add_subdirectory (debian)
#     add_dependencies (js8call debian)
#   endif (NOT APPLE)
# endif (UNIX)


#
# installation
#
install (TARGETS js8call
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
  BUNDLE DESTINATION . COMPONENT runtime
  )

install (TARGETS js8 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
  BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
  )

install (PROGRAMS
  ${RIGCTL_EXE}
  DESTINATION ${CMAKE_INSTALL_BINDIR}
  #COMPONENT runtime
  RENAME rigctl-local${CMAKE_EXECUTABLE_SUFFIX}
  )

install (PROGRAMS
  ${RIGCTLD_EXE}
  DESTINATION ${CMAKE_INSTALL_BINDIR}
  #COMPONENT runtime
  RENAME rigctld-local${CMAKE_EXECUTABLE_SUFFIX}
  )

install (FILES
  README
  COPYING
  INSTALL
  INSTALL-WSJTX
  DESTINATION ${CMAKE_INSTALL_DOCDIR}
  #COMPONENT runtime
  )

install (FILES
  eclipse.txt
  DESTINATION ${CMAKE_INSTALL_DATADIR}/${CMAKE_PROJECT_NAME}
  #COMPONENT runtime
  )

#
# Mac installer files
#
if (APPLE)
    install (FILES
        Darwin/ReadMe.txt
        Darwin/sysctl.conf
        DESTINATION .
        #COMPONENT runtime
    )
endif (APPLE)

#
# uninstall support
#
configure_file (
  "${CMAKE_CURRENT_SOURCE_DIR}/CMake/cmake_uninstall.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
  @ONLY)
add_custom_target (uninstall
  "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake")


# creates or updates ${PROJECT_BINARY_DIR}/scs_version.h using cmake script
# add_custom_target (revisiontag
#   COMMAND ${CMAKE_COMMAND}
#   -D SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
#   -D BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
#   -D OUTPUT_DIR=${PROJECT_BINARY_DIR}
#   -P ${CMAKE_CURRENT_SOURCE_DIR}/CMake/getsvn.cmake
#   COMMENT "Generating Subversion revision information"
#   VERBATIM
#   )
# explicitly say that the wsjt_qt depends on custom target, this is
# done indirectly so that the revisiontag target gets built exactly
# once per build
# add_dependencies(wsjt_qt)


#
# versioning and configuration
#
configure_file (
  "${CMAKE_CURRENT_SOURCE_DIR}/wsjtx_config.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/wsjtx_config.h"
  )


if (NOT WIN32 AND NOT APPLE)
  # install a desktop file so js8call appears in the application start
  # menu with an icon
  install (
    FILES js8call.desktop
    DESTINATION /usr/share/applications
    #COMPONENT runtime
    )
  install (
    FILES icons/Unix/js8call_icon.png
    DESTINATION /usr/share/pixmaps
    #COMPONENT runtime
    )

    IF("${CMAKE_INSTALL_PREFIX}" STREQUAL "/opt/js8call")
        execute_process(COMMAND ln -s /opt/js8call/bin/js8call ljs8call)

        install(FILES
            ${CMAKE_BINARY_DIR}/ljs8call DESTINATION /usr/bin/ RENAME js8call
            #COMPONENT runtime
        )
    endif()
endif (NOT WIN32 AND NOT APPLE)


#
# bundle fixup only done in Release or MinSizeRel configurations
#
if (NOT is_debug_build)

  get_target_property (QtCore_location Qt6::Core LOCATION)
  get_filename_component (QtCore_location ${QtCore_location} PATH)
  list (APPEND fixup_library_dirs ${QtCore_location})

  if (APPLE)
    set (CMAKE_POSTFLIGHT_SCRIPT
      "${CMAKE_BINARY_DIR}/postflight.sh")
    set (CMAKE_POSTUPGRADE_SCRIPT
      "${CMAKE_BINARY_DIR}/postupgrade.sh")
    configure_file ("${CMAKE_SOURCE_DIR}/Darwin/postflight.sh.in"
      "${CMAKE_POSTFLIGHT_SCRIPT}")
    configure_file ("${CMAKE_SOURCE_DIR}/Darwin/postupgrade.sh.in"
      "${CMAKE_POSTUPGRADE_SCRIPT}")
  endif ()

  if (APPLE OR WIN32)
    # install rules for including 3rd party libs such as Qt

    # install a qt.conf file
    install (CODE "
        get_filename_component (the_qt_conf \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${WSJT_QT_CONF_DESTINATION}/qt.conf\" REALPATH)
        file (WRITE \"\${the_qt_conf}\"
\"[Paths]
\")
"
      #COMPONENT runtime
      )

    # if a system Qt is used (e.g. installed in /usr/lib/), it will not be included in the installation
    set (fixup_exe "\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/${CMAKE_PROJECT_NAME}${CMAKE_EXECUTABLE_SUFFIX}")

    #get_filename_component (hamlib_lib_dir ${hamlib_LIBRARIES} PATH)

    if (APPLE)

      get_filename_component (fortran_lib_dir ${OpenMP_gomp_LIBRARY} PATH)
      list (APPEND fixup_library_dirs ${fortran_lib_dir})

      # install required Qt plugins
      install (
	DIRECTORY
	${QT_PLUGINS_DIR}/platforms
	${QT_PLUGINS_DIR}/audio
	${QT_PLUGINS_DIR}/accessible
	${QT_PLUGINS_DIR}/imageformats
  ${QT_PLUGINS_DIR}/styles
	DESTINATION ${WSJT_PLUGIN_DESTINATION}
	CONFIGURATIONS Release MinSizeRel RelWithDebInfo
	#COMPONENT runtime
  FILES_MATCHING PATTERN "*${CMAKE_SHARED_LIBRARY_SUFFIX}"
  PATTERN "*minimal*${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
  PATTERN "*offscreen*${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
  PATTERN "*quick*${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
  PATTERN "*webgl*${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
  PATTERN "*_debug${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
  PATTERN "*${CMAKE_SHARED_LIBRARY_SUFFIX}.dSYM" EXCLUDE
	)
      # install (
      # 	DIRECTORY
      # 	${QT_PLUGINS_DIR}/platforms
      # 	${QT_PLUGINS_DIR}/audio
      #         ${QT_PLUGINS_DIR}/accessible
      # 	DESTINATION ${WSJT_PLUGIN_DESTINATION}
      # 	CONFIGURATIONS Debug
      # 	#COMPONENT runtime
      # 	FILES_MATCHING PATTERN "*_debug${CMAKE_SHARED_LIBRARY_SUFFIX}"
      # 	PATTERN "*minimal*${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
      # 	PATTERN "*offscreen*${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
      # 	PATTERN "*quick*${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
      # 	)

      # add plugins path for Mac Bundle
      install (CODE "
          get_filename_component (the_qt_conf \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${WSJT_QT_CONF_DESTINATION}/qt.conf\" REALPATH)
          file (APPEND \"\${the_qt_conf}\"
\"Plugins = PlugIns
\")
"
	#COMPONENT runtime
	)
    endif (APPLE)

    if (WIN32)
      # DLL directories
      get_filename_component (hamlib_lib_dir ${Hamlib_LIBRARY} PATH)
      list (APPEND fixup_library_dirs ${hamlib_lib_dir}/../bin)

      get_filename_component (usb_lib_dir ${Usb_LIBRARY} PATH)
      list (APPEND fixup_library_dirs ${usb_lib_dir})

      get_filename_component (fftw_lib_dir ${FFTW3F_LIBRARY} PATH)
      list (APPEND fixup_library_dirs ${fftw_lib_dir})

      # install required Qt plugins
      install (
	DIRECTORY
	${QT_PLUGINS_DIR}/platforms
	${QT_PLUGINS_DIR}/accessible
	${QT_PLUGINS_DIR}/audio
	${QT_PLUGINS_DIR}/imageformats
  ${QT_PLUGINS_DIR}/styles
	DESTINATION ${WSJT_PLUGIN_DESTINATION}
	CONFIGURATIONS Release MinSizeRel RelWithDebInfo
	#COMPONENT runtime
  FILES_MATCHING PATTERN "*${CMAKE_SHARED_LIBRARY_SUFFIX}"
  PATTERN "*minimal*${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
  PATTERN "*offscreen*${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
  PATTERN "*quick*${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
  PATTERN "*d${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
	)
      # install (
      # 	DIRECTORY
      # 	${QT_PLUGINS_DIR}/platforms
      # 	${QT_PLUGINS_DIR}/accessible
      # 	${QT_PLUGINS_DIR}/audio
      # 	DESTINATION ${WSJT_PLUGIN_DESTINATION}
      # 	CONFIGURATIONS Debug
      # 	#COMPONENT runtime
      # 	FILES_MATCHING PATTERN "*d${CMAKE_SHARED_LIBRARY_SUFFIX}"
      # 	PATTERN "*minimal*${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
      # 	PATTERN "*offscreen*${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
      # 	PATTERN "*quick*${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
      # 	)

      # add plugins path for WIN32
      file (RELATIVE_PATH _plugins_path "${CMAKE_INSTALL_PREFIX}/${WSJT_QT_CONF_DESTINATION}" "${CMAKE_INSTALL_PREFIX}/${WSJT_PLUGIN_DESTINATION}")
      install (CODE "
        get_filename_component (the_qt_conf \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${WSJT_QT_CONF_DESTINATION}/qt.conf\" REALPATH)
        file (APPEND \"\${the_qt_conf}\"
\"Plugins = ${_plugins_path}
\")
"
	#COMPONENT runtime
	)

      set (gp_tool "objdump")	# we want MinGW tool - not MSVC (See GetPrerequisites.cmake)
    endif (WIN32)

    install (CODE "
        get_filename_component (the_path \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${WSJT_PLUGIN_DESTINATION}\" REALPATH)
        file (GLOB_RECURSE QTPLUGINS \"\${the_path}/*${CMAKE_SHARED_LIBRARY_SUFFIX}\")
        set (CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/CMake/Modules ${CMAKE_MODULE_PATH})
        include (BundleUtilities)
        set (BU_CHMOD_BUNDLE_ITEMS ON)
        set (gp_tool ${gp_tool})
        # canonicalize path in install context
        get_filename_component (the_exe ${fixup_exe} REALPATH)
        fixup_bundle (\"\${the_exe}\" \"\${QTPLUGINS}\" \"${fixup_library_dirs}\")"
      #COMPONENT runtime
      )
  endif (APPLE OR WIN32)

  # Bit of a hack; we're not code signing the application, and as such any code signatures
  # that the fixup borked will be invalid at this point, so we must remove them. If we were
  # to sign the application, this in theory would not be required.

  if (APPLE)
    install(CODE "
    execute_process(COMMAND sh -c \"
      echo Removing library code signatures in `pwd`
      for file in *.dylib; do
        codesign --force -s - $file
      done
      exit 0
    \"
    WORKING_DIRECTORY \${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR})
  ")
  endif(APPLE)

endif (NOT is_debug_build)


#
# packaging
#
set (CPACK_MONOLITHIC_INSTALL 1)
set (CPACK_PACKAGE_NAME "${CMAKE_PROJECT_NAME}")
set (CPACK_PACKAGE_VERSION_MAJOR ${WSJTX_VERSION_MAJOR})
set (CPACK_PACKAGE_VERSION_MINOR ${WSJTX_VERSION_MINOR})
set (CPACK_PACKAGE_VERSION_PATCH ${WSJTX_VERSION_PATCH})

if (WIN32)
  set (CPACK_GENERATOR "NSIS")
elseif (APPLE)
  set (CPACK_GENERATOR "DragNDrop")
else ()
  find_program (DPKG_BUILDER dpkg-buildpackage DOC "Debian package builder")
  if (DPKG_BUILDER)
    #
    # Derive the correct filename for a Debian package because the DEB
    # generator doesn't do this correctly at present.
    #
    find_program (DPKG_PROGRAM dpkg DOC "dpkg program of Debian-based systems")
    if (DPKG_PROGRAM)
      execute_process (
	COMMAND ${DPKG_PROGRAM} --print-architecture
	OUTPUT_VARIABLE CPACK_DEBIAN_PACKAGE_ARCHITECTURE
	OUTPUT_STRIP_TRAILING_WHITESPACE
	)
    else (DPKG_PROGRAM)
      set (CPACK_DEBIAN_PACKAGE_ARCHITECTURE noarch)
    endif (DPKG_PROGRAM)

    list (APPEND CPACK_GENERATOR "DEB")
  endif (DPKG_BUILDER)

  find_program (RPMBUILDER rpmbuild DOC "RPM package builder")
  if (RPMBUILDER)
    list (APPEND CPACK_GENERATOR "RPM")
  endif (RPMBUILDER)
endif ()

set (CPACK_DEBIAN_PACKAGE_DESCRIPTION "${PROJECT_DESCRIPTION}")
set (CPACK_DEBIAN_PACKAGE_HOMEPAGE "${PROJECT_HOMEPAGE}")
set (CPACK_DEBIAN_PACKAGE_DEPENDS "libgfortran5 (>=8.3) | libgfortran4 (>=7.3) | libgfortran3 (>=6.3), libfftw3-single3 (>=3.3), libgomp1 (>=6), libqt6serialport6 (>=5.7), libqt6multimedia6 (>=5.7), libqt6widgets6 (>=5.7), libqt6network6 (>=5.7), libusb-1.0-0 (>=1.0.21)")
set (CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

set (CPACK_RPM_PACKAGE_ARCHITECTURE ${CMAKE_SYSTEM_PROCESSOR})
set (CPACK_RPM_PACKAGE_REQUIRES "qt6-qtbase >= 5.9, qt6-qtserialport >= 5.9, qt6-qtmultimedia >= 5.9, qt6-qtsvg >= 5.9, libusbx >= 1.0.22, libgfortran >= 7, libgomp >= 7, fftw-libs-single >= 3.3")
set (CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION /usr/share/pixmaps /usr/share/applications /usr/share/man /usr/share/man1)

configure_file ("${PROJECT_SOURCE_DIR}/CMakeCPackOptions.cmake.in"
  "${PROJECT_BINARY_DIR}/CMakeCPackOptions.cmake" @ONLY)
set (CPACK_PROJECT_CONFIG_FILE "${PROJECT_BINARY_DIR}/CMakeCPackOptions.cmake")

include (CPack)
