cmake_minimum_required(VERSION 3.15)
project(hoard
  VERSION 3.2
  LANGUAGES C CXX
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

if(NOT WIN32)
  # Write a version map to enable replacing direct libc allocation calls (Linux only).
  set(VERS_SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/vers.map)
  file(WRITE ${VERS_SCRIPT}
  "GLIBC_2.2.5 {\n  global:\n    __libc_malloc;\n    __libc_free;\n    __libc_realloc;\n    __libc_calloc;\n    __libc_memalign;\n    local: custom*;\n      xx*;\n};\n")

  # Ensure that the compiler doesn't replace anything with malloc/free
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden -fno-builtin-malloc -fno-builtin-free")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden -fno-builtin-malloc -fno-builtin-free")
endif()

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

set(UNIX_SOURCES
#  ${heap-layers_SOURCE_DIR}/wrappers/gnuwrapper.cpp
  src/source/unixtls.cpp
  src/source/libhoard.cpp
)

set(MACOS_SOURCES
  ${heap-layers_SOURCE_DIR}/wrappers/macwrapper.cpp
  src/source/mactls.cpp
  src/source/libhoard.cpp
)

set(WINDOWS_SOURCES
  src/source/winwrapper-detours.cpp
  src/source/wintls.cpp
  src/source/libhoard.cpp
)

if(WIN32)
  #
  # ─── WINDOWS BUILD WITH DETOURS ─────────────────────────────────────
  #
  set(HOARD_SOURCES ${WINDOWS_SOURCES})
  add_definitions(-DNDEBUG -D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN)

  # Option to use system-installed Detours instead of fetching
  option(USE_SYSTEM_DETOURS "Use system-installed Detours instead of fetching" OFF)

  if(USE_SYSTEM_DETOURS)
    find_package(Detours REQUIRED)
  else()
    # Fetch Microsoft Detours from main branch (v4.0.1 has ARM64 bugs)
    FetchContent_Declare(
      Detours
      GIT_REPOSITORY https://github.com/microsoft/Detours.git
      GIT_TAG        main
      GIT_SHALLOW    TRUE
    )
    FetchContent_GetProperties(Detours)
    if(NOT detours_POPULATED)
      FetchContent_Populate(Detours)
    endif()

    # Core Detours sources
    set(DETOURS_SOURCES
      "${detours_SOURCE_DIR}/src/creatwth.cpp"
      "${detours_SOURCE_DIR}/src/detours.cpp"
      "${detours_SOURCE_DIR}/src/modules.cpp"
      "${detours_SOURCE_DIR}/src/image.cpp"
      "${detours_SOURCE_DIR}/src/disasm.cpp"
    )

    # Add architecture-specific disassembler (required alongside disasm.cpp)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
      list(APPEND DETOURS_SOURCES "${detours_SOURCE_DIR}/src/disolarm64.cpp")
      message(STATUS "Detours: Building for ARM64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM")
      list(APPEND DETOURS_SOURCES "${detours_SOURCE_DIR}/src/disolarm.cpp")
      message(STATUS "Detours: Building for ARM")
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
      list(APPEND DETOURS_SOURCES "${detours_SOURCE_DIR}/src/disolx64.cpp")
      message(STATUS "Detours: Building for x64")
    else()
      list(APPEND DETOURS_SOURCES "${detours_SOURCE_DIR}/src/disolx86.cpp")
      message(STATUS "Detours: Building for x86")
    endif()

    # Build Detours as a static library
    add_library(detours STATIC ${DETOURS_SOURCES})

    target_include_directories(detours PUBLIC "${detours_SOURCE_DIR}/src")

    target_compile_definitions(detours PRIVATE
      WIN32_LEAN_AND_MEAN
      _WIN32_WINNT=0x0600
    )

    # Suppress warnings in Detours code
    target_compile_options(detours PRIVATE /W3 /WX-)

    # Create an alias to match the find_package target name
    add_library(Detours::Detours ALIAS detours)
    set(Detours_INCLUDE_DIR "${detours_SOURCE_DIR}/src")

    # Build Detours tools (withdll.exe and setdll.exe)
    add_executable(withdll "${detours_SOURCE_DIR}/samples/withdll/withdll.cpp")
    target_link_libraries(withdll PRIVATE detours)
    target_include_directories(withdll PRIVATE "${detours_SOURCE_DIR}/src")
    target_compile_definitions(withdll PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0600)
    target_compile_options(withdll PRIVATE /W3 /WX-)

    add_executable(setdll "${detours_SOURCE_DIR}/samples/setdll/setdll.cpp")
    target_link_libraries(setdll PRIVATE detours)
    target_include_directories(setdll PRIVATE "${detours_SOURCE_DIR}/src")
    target_compile_definitions(setdll PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0600)
    target_compile_options(setdll PRIVATE /W3 /WX-)

    # Install the tools alongside the library
    install(TARGETS withdll setdll RUNTIME DESTINATION bin)
  endif()

elseif(APPLE)
  set(HOARD_SOURCES ${MACOS_SOURCES})
  add_definitions(-DNDEBUG)
  # initial-exec TLS and deep template depth are only for the Hoard library;
  # they are added as target-specific options below (not globally) to avoid
  # breaking benchmark executables linked against libhoard.
  set(HOARD_APPLE_COMPILE_OPTIONS -ftls-model=initial-exec -ftemplate-depth=1024)
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
  # Respect command-line -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF
  if(NOT DEFINED CMAKE_INTERPROCEDURAL_OPTIMIZATION)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
    if(ipo_supported)
      set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
      message(STATUS "LTO enabled")
    else()
      message(STATUS "LTO not supported: ${ipo_error}")
    endif()
  else()
    message(STATUS "LTO: user-specified CMAKE_INTERPROCEDURAL_OPTIMIZATION=${CMAKE_INTERPROCEDURAL_OPTIMIZATION}")
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

# Apply macOS-specific compile options to the hoard target only
if(APPLE AND DEFINED HOARD_APPLE_COMPILE_OPTIONS)
  target_compile_options(hoard PRIVATE ${HOARD_APPLE_COMPILE_OPTIONS})
endif()

# Platform-specific linking
if(WIN32)
  # Link with Detours and Windows libraries
  target_link_libraries(hoard PRIVATE Detours::Detours psapi)
  target_include_directories(hoard PRIVATE ${Detours_INCLUDE_DIR})

  # Export DetourFinishHelperProcess at ordinal #1 (required for withdll.exe)
  target_link_options(hoard PRIVATE "/export:DetourFinishHelperProcess,@1,NONAME")

  # Disable some MSVC warnings
  target_compile_options(hoard PRIVATE /W3 /WX-)

  # Set Windows-specific properties
  set_target_properties(hoard PROPERTIES
    OUTPUT_NAME "hoard"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # For Linux, link with the version script declared above.
  target_link_options(hoard PRIVATE "LINKER:--version-script=${VERS_SCRIPT}")
  target_link_libraries(hoard PRIVATE pthread dl)
  set_target_properties(hoard PROPERTIES OUTPUT_NAME "hoard")
else()
  # macOS
  target_link_libraries(hoard PRIVATE pthread dl)
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
# ─── BENCHMARKS (opt-in) ────────────────────────────────────────────
#

option(HOARD_BUILD_BENCHMARKS "Build benchmark programs" OFF)
option(HOARD_LINK_BENCHMARKS "Link benchmarks directly against libhoard (for CI where LD_PRELOAD/DYLD_INSERT may be unavailable)" OFF)

if(HOARD_BUILD_BENCHMARKS)
  set(BENCH_DIR ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks)
  set(BENCH_COMMON ${BENCH_DIR}/common)

  # Build a static archive of Hoard for linking into benchmarks.
  # This avoids dylib code-signing issues on macOS CI and LD_PRELOAD
  # symbol issues on Linux.
  if(HOARD_LINK_BENCHMARKS AND NOT WIN32)
    add_library(hoard_static STATIC ${HOARD_SOURCES})
    target_include_directories(hoard_static PRIVATE
      ${PROJECT_SOURCE_DIR}
      ${PROJECT_SOURCE_DIR}/src/include
      ${PROJECT_SOURCE_DIR}/src/include/hoard
      ${PROJECT_SOURCE_DIR}/src/include/superblocks
      ${PROJECT_SOURCE_DIR}/src/include/util
      ${heap-layers_SOURCE_DIR})
    target_compile_definitions(hoard_static PRIVATE _REENTRANT=1 NDEBUG)
    if(APPLE AND DEFINED HOARD_APPLE_COMPILE_OPTIONS)
      target_compile_options(hoard_static PRIVATE ${HOARD_APPLE_COMPILE_OPTIONS})
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      target_link_libraries(hoard_static PRIVATE pthread dl)
    else()
      target_link_libraries(hoard_static PRIVATE pthread dl)
    endif()
  endif()

  # Helper: optionally link a benchmark target against hoard (static).
  # On macOS, -force_load is needed so the linker keeps the
  # __DATA,__interpose sections that replace malloc/free.
  macro(maybe_link_hoard target)
    if(HOARD_LINK_BENCHMARKS AND NOT WIN32)
      if(APPLE)
        add_dependencies(${target} hoard_static)
        target_link_libraries(${target} PRIVATE "-force_load $<TARGET_FILE:hoard_static>" "-lc++")
      else()
        target_link_libraries(${target} PRIVATE hoard_static)
      endif()
    endif()
  endmacro()

  # threadtest
  add_executable(bench-threadtest ${BENCH_DIR}/threadtest/threadtest.cpp)
  target_include_directories(bench-threadtest PRIVATE ${BENCH_COMMON}
    ${PROJECT_SOURCE_DIR}/src/Heap-Layers ${heap-layers_SOURCE_DIR})
  target_compile_definitions(bench-threadtest PRIVATE NDEBUG)
  target_compile_options(bench-threadtest PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O3>)
  set_target_properties(bench-threadtest PROPERTIES
    CXX_STANDARD 17 OUTPUT_NAME threadtest
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/benchmarks)
  if(NOT WIN32)
    target_link_libraries(bench-threadtest PRIVATE pthread)
  endif()
  maybe_link_hoard(bench-threadtest)

  # The following benchmarks use POSIX threading APIs that don't compile
  # cleanly on MSVC. They are built only on Unix/macOS.
  if(NOT WIN32)
    # cache-scratch
    add_executable(bench-cache-scratch ${BENCH_DIR}/cache-scratch/cache-scratch.cpp)
    target_include_directories(bench-cache-scratch PRIVATE ${BENCH_COMMON})
    target_compile_definitions(bench-cache-scratch PRIVATE NDEBUG)
    target_compile_options(bench-cache-scratch PRIVATE -O3)
    set_target_properties(bench-cache-scratch PROPERTIES
      OUTPUT_NAME cache-scratch
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/benchmarks)
    target_link_libraries(bench-cache-scratch PRIVATE pthread)
    maybe_link_hoard(bench-cache-scratch)

    # cache-thrash
    add_executable(bench-cache-thrash ${BENCH_DIR}/cache-thrash/cache-thrash.cpp)
    target_include_directories(bench-cache-thrash PRIVATE ${BENCH_COMMON})
    target_compile_definitions(bench-cache-thrash PRIVATE NDEBUG)
    target_compile_options(bench-cache-thrash PRIVATE -O3)
    set_target_properties(bench-cache-thrash PROPERTIES
      OUTPUT_NAME cache-thrash
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/benchmarks)
    target_link_libraries(bench-cache-thrash PRIVATE pthread)
    maybe_link_hoard(bench-cache-thrash)

    # larson
    add_executable(bench-larson ${BENCH_DIR}/larson/larson.cpp)
    target_include_directories(bench-larson PRIVATE ${BENCH_COMMON})
    target_compile_definitions(bench-larson PRIVATE NDEBUG)
    target_compile_options(bench-larson PRIVATE -O3)
    set_target_properties(bench-larson PROPERTIES
      OUTPUT_NAME larson
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/benchmarks)
    target_link_libraries(bench-larson PRIVATE pthread)
    maybe_link_hoard(bench-larson)

    # linux-scalability (C, uses POSIX barriers)
    add_executable(bench-linux-scalability ${BENCH_DIR}/linux-scalability/linux-scalability.c)
    target_include_directories(bench-linux-scalability PRIVATE ${BENCH_COMMON})
    target_compile_definitions(bench-linux-scalability PRIVATE NDEBUG)
    target_compile_options(bench-linux-scalability PRIVATE -O3)
    set_target_properties(bench-linux-scalability PROPERTIES
      OUTPUT_NAME linux-scalability
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/benchmarks)
    target_link_libraries(bench-linux-scalability PRIVATE pthread m)
    maybe_link_hoard(bench-linux-scalability)
  endif()

  # Windows test programs
  if(WIN32)
    add_executable(test-malloc ${CMAKE_CURRENT_SOURCE_DIR}/test_malloc.cpp)
    target_compile_definitions(test-malloc PRIVATE NDEBUG)
    set_target_properties(test-malloc PROPERTIES
      OUTPUT_NAME test_malloc
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/benchmarks)

    add_executable(bench-malloc ${CMAKE_CURRENT_SOURCE_DIR}/bench_malloc.cpp)
    target_compile_definitions(bench-malloc PRIVATE NDEBUG)
    set_target_properties(bench-malloc PROPERTIES
      OUTPUT_NAME bench_malloc
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/benchmarks)
  endif()
endif()
