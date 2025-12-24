# FindDetours.cmake
#
# Find Microsoft Detours library
# https://github.com/microsoft/Detours
#
# This module defines:
#   Detours_FOUND        - True if Detours was found
#   Detours_INCLUDE_DIR  - Include directory for Detours headers
#   Detours_LIBRARY      - Library to link against
#   Detours::Detours     - Imported target for Detours
#
# User-configurable hints:
#   DETOURS_ROOT         - Root directory of Detours installation
#   DETOURS_INCLUDE_DIR  - Hint for include directory
#   DETOURS_LIBRARY      - Hint for library path
#
# Environment variables:
#   DETOURS_ROOT         - Root directory of Detours installation
#
# To build Detours from source:
#   git clone https://github.com/microsoft/Detours.git
#   cd Detours
#   nmake
#   # Headers are in include/, libraries in lib.X64/ or lib.X86/

include(FindPackageHandleStandardArgs)

# Determine library subdirectory based on architecture
if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
  set(_DETOURS_LIB_SUBDIR "lib.ARM64")
  set(_DETOURS_LIB_SUBDIR_ALT "arm64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM")
  set(_DETOURS_LIB_SUBDIR "lib.ARM")
  set(_DETOURS_LIB_SUBDIR_ALT "arm")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(_DETOURS_LIB_SUBDIR "lib.X64")
  set(_DETOURS_LIB_SUBDIR_ALT "x64")
else()
  set(_DETOURS_LIB_SUBDIR "lib.X86")
  set(_DETOURS_LIB_SUBDIR_ALT "x86")
endif()

# Search paths
set(_DETOURS_SEARCH_PATHS
  ${DETOURS_ROOT}
  $ENV{DETOURS_ROOT}
  ${CMAKE_SOURCE_DIR}/external/Detours
  ${CMAKE_SOURCE_DIR}/third_party/Detours
  ${CMAKE_SOURCE_DIR}/vendor/Detours
  "C:/Detours"
  "C:/Program Files/Detours"
  "C:/Program Files (x86)/Detours"
)

# Find include directory
find_path(Detours_INCLUDE_DIR
  NAMES detours.h
  HINTS
    ${DETOURS_INCLUDE_DIR}
    ${_DETOURS_SEARCH_PATHS}
  PATH_SUFFIXES
    include
    src
  DOC "Detours include directory"
)

# Find library
find_library(Detours_LIBRARY
  NAMES detours
  HINTS
    ${DETOURS_LIBRARY}
    ${_DETOURS_SEARCH_PATHS}
  PATH_SUFFIXES
    ${_DETOURS_LIB_SUBDIR}
    ${_DETOURS_LIB_SUBDIR_ALT}
    lib
    lib/${_DETOURS_LIB_SUBDIR_ALT}
  DOC "Detours library"
)

# Handle standard find_package arguments
find_package_handle_standard_args(Detours
  REQUIRED_VARS
    Detours_LIBRARY
    Detours_INCLUDE_DIR
  FAIL_MESSAGE
    "Could not find Microsoft Detours. Please set DETOURS_ROOT to the Detours installation directory, or install via vcpkg: vcpkg install detours"
)

# Create imported target
if(Detours_FOUND AND NOT TARGET Detours::Detours)
  add_library(Detours::Detours STATIC IMPORTED)
  set_target_properties(Detours::Detours PROPERTIES
    IMPORTED_LOCATION "${Detours_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${Detours_INCLUDE_DIR}"
  )
endif()

# Mark advanced variables
mark_as_advanced(Detours_INCLUDE_DIR Detours_LIBRARY)
