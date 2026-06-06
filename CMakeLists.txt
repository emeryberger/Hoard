cmake_minimum_required(VERSION 3.15)
project(hoard
  VERSION 3.2
  LANGUAGES CXX
  DESCRIPTION "High-performance scalable memory allocator"
)

#
# ─── OPTIONS ──────────────────────────────────────────────────────────
#

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)

# Add cmake modules path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES arm64 arm64e x86_64)
endif()

# Generate position-independent code; required for shared libraries on many platforms
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Note: alloc8 handles version scripts, visibility flags, and compiler options

#
# ─── INCLUDE DIRECTORIES ──────────────────────────────────────────────
#

include_directories(
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/src/include
  ${PROJECT_SOURCE_DIR}/src/include/hoard
  ${PROJECT_SOURCE_DIR}/src/include/superblocks
  ${PROJECT_SOURCE_DIR}/src/include/util
)

include(FetchContent)
FetchContent_Declare(
  Heap-Layers
  GIT_REPOSITORY https://github.com/emeryberger/Heap-Layers.git
  GIT_TAG        master
)
FetchContent_MakeAvailable(Heap-Layers)
include_directories(${heap-layers_SOURCE_DIR})

# Allocation-free printf (compiled directly into Hoard, not as a library)
FetchContent_Declare(
  printf
  GIT_REPOSITORY https://github.com/emeryberger/printf.git
  GIT_TAG        master
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(printf)
include_directories(${printf_SOURCE_DIR})

# alloc8 - generic allocator interposition library for all platforms
FetchContent_Declare(
  alloc8
  GIT_REPOSITORY https://github.com/emeryberger/alloc8.git
  GIT_TAG        main
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(alloc8)
include_directories(${alloc8_SOURCE_DIR}/include)

set(UNIX_SOURCES
  src/source/unixtls.cpp
  src/source/libhoard.cpp
  ${printf_SOURCE_DIR}/printf.cpp
  ${ALLOC8_INTERPOSE_SOURCES}
)

set(MACOS_SOURCES
  src/source/mactls.cpp
  src/source/libhoard.cpp
  ${printf_SOURCE_DIR}/printf.cpp
  ${ALLOC8_INTERPOSE_SOURCES}
)

set(WINDOWS_SOURCES
  src/source/wintls.cpp
  src/source/libhoard.cpp
  ${printf_SOURCE_DIR}/printf.cpp
  ${ALLOC8_INTERPOSE_SOURCES}
)

if(WIN32)
  #
  # ─── WINDOWS BUILD ───────────────────────────────────────────────────
  # alloc8 handles Detours fetching/building internally
  # ALLOC8_NO_DLLMAIN: Hoard provides its own DllMain in wintls.cpp
  #
  set(HOARD_SOURCES ${WINDOWS_SOURCES})
  add_definitions(-DNDEBUG -D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN -DALLOC8_NO_DLLMAIN)

elseif(APPLE)
  set(HOARD_SOURCES ${MACOS_SOURCES})
  # alloc8 defaults to fast mode (no ownership table, no runtime checks).
  # Hoard handles foreign pointers gracefully via IgnoreInvalidFree.
  # To enable compatibility mode: -DALLOC8_OWNERSHIP_TABLE=1 -DALLOC8_RUNTIME_CHECKS=1
  add_compile_options(-DNDEBUG -ftls-model=initial-exec -ftemplate-depth=1024)
else()
  set(HOARD_SOURCES ${UNIX_SOURCES})
  add_definitions(-DNDEBUG)
endif()

# Force Release build type for optimal performance (memory allocators need aggressive inlining)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# NOTE: Link-Time Optimization (LTO) DISABLED on Windows ARM64 - causes 6x slowdown!
# MSVC's ARM64 LTO codegen produces significantly slower code for memory allocators.
if(NOT WIN32)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
  if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    message(STATUS "LTO enabled")
  else()
    message(STATUS "LTO not supported: ${ipo_error}")
  endif()
else()
  message(STATUS "LTO disabled on Windows (causes ARM64 slowdown)")
endif()

# Enable maximum optimization for MSVC
if(MSVC)
  # /O2 = maximize speed, /Ob2 = inline any suitable function, /Oi = enable intrinsics
  # /Ot = favor fast code
  # NOTE: /GL (whole program optimization) DISABLED - causes 6x slowdown on ARM64!
  # The MSVC ARM64 LTO codegen appears to produce significantly slower code.
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Ob2 /Oi /Ot")
  set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Ob2 /Oi /Ot")
  # NOTE: /LTCG disabled since /GL is disabled
endif()

add_library(hoard SHARED ${HOARD_SOURCES})

# Link with alloc8 interposition library (handles platform-specific linking)
target_link_libraries(hoard PRIVATE alloc8::interpose)

# Platform-specific additional settings
if(WIN32)
  # Export DetourFinishHelperProcess at ordinal #1 (required for withdll.exe)
  target_link_options(hoard PRIVATE "/export:DetourFinishHelperProcess,@1,NONAME")

  # Disable some MSVC warnings
  target_compile_options(hoard PRIVATE /W3 /WX-)

  # Set Windows-specific properties
  set_target_properties(hoard PROPERTIES
    OUTPUT_NAME "hoard"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
else()
  set_target_properties(hoard PROPERTIES OUTPUT_NAME "hoard")
endif()

target_compile_definitions(hoard
    PRIVATE
    _REENTRANT=1
)

#
# ─── EXPORT AND INSTALL ────────────────────────────────────────────────
#

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

# Set up the export name
set(HOARD_EXPORT_NAME HoardTargets)

# Install the library with export
install(TARGETS hoard
  EXPORT ${HOARD_EXPORT_NAME}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Export target with namespace
install(EXPORT ${HOARD_EXPORT_NAME}
  FILE HoardTargets.cmake
  NAMESPACE Hoard::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Hoard
)

# Create an alias for use from build tree
add_library(Hoard::Hoard ALIAS hoard)

# Set export name to maintain consistent Hoard::Hoard naming after installation
set_target_properties(hoard PROPERTIES EXPORT_NAME Hoard)

# Configure the package config file
configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/HoardConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/HoardConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Hoard
)

# Generate version file
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/HoardConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

# Install the config files
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/HoardConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/HoardConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Hoard
)

#
# ─── EXTERNAL ALLOCATORS (for benchmarking) ────────────────────────────
#
# Build external allocators for comparison benchmarking.
# Enable with: -DBUILD_JEMALLOC=ON -DBUILD_MIMALLOC=ON -DBUILD_TCMALLOC=ON
# Or all at once: -DBUILD_ALL_ALLOCATORS=ON
#
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/allocators/CMakeLists.txt")
  add_subdirectory(allocators)
endif()
