#
# ─── EXTERNAL ALLOCATORS (for benchmarking) ────────────────────────────
#
# Builds mimalloc, jemalloc, and tcmalloc (gperftools) as shared libraries
# so they can be compared against Hoard via LD_PRELOAD / DYLD_INSERT_LIBRARIES.
# The benchmark harness (scripts/rigorous_benchmark.sh) preloads these; point
# it at the built libs with MIMALLOC_LIB / JEMALLOC_LIB, or install them.
#
# This directory is pulled in by the top-level CMakeLists via
# add_subdirectory(allocators). Nothing here builds unless a BUILD_* option is
# turned on, so a normal Hoard build is unaffected.
#
#   cmake .. -DBUILD_MIMALLOC=ON            # one allocator
#   cmake .. -DBUILD_ALL_ALLOCATORS=ON      # all three
#
# The resulting shared libraries are collected in:
#   ${CMAKE_BINARY_DIR}/allocators/lib
#

# ─── Options ───────────────────────────────────────────────────────────
option(BUILD_ALL_ALLOCATORS "Build all external comparison allocators" OFF)
option(BUILD_MIMALLOC "Build mimalloc for benchmarking"  ${BUILD_ALL_ALLOCATORS})
option(BUILD_JEMALLOC "Build jemalloc for benchmarking"  ${BUILD_ALL_ALLOCATORS})
option(BUILD_TCMALLOC "Build tcmalloc (gperftools) for benchmarking" ${BUILD_ALL_ALLOCATORS})

if(NOT (BUILD_MIMALLOC OR BUILD_JEMALLOC OR BUILD_TCMALLOC))
  # Nothing requested; do not fetch or configure anything.
  return()
endif()

if(WIN32)
  message(WARNING
    "External allocator builds (BUILD_MIMALLOC/JEMALLOC/TCMALLOC) are only "
    "wired up for Unix/macOS; skipping on Windows.")
  return()
endif()

include(FetchContent)
include(ExternalProject)

# Version pins. Bump these to test newer releases.
set(MIMALLOC_TAG   "v2.1.7"    CACHE STRING "mimalloc git tag to build")
set(JEMALLOC_TAG   "5.3.0"     CACHE STRING "jemalloc git tag to build")
set(GPERFTOOLS_TAG "gperftools-2.16" CACHE STRING "gperftools git tag to build")

# Common output directory for the built shared libraries.
set(ALLOC_LIB_DIR "${CMAKE_BINARY_DIR}/allocators/lib")
file(MAKE_DIRECTORY "${ALLOC_LIB_DIR}")

# ─── mimalloc (CMake native) ───────────────────────────────────────────
if(BUILD_MIMALLOC)
  message(STATUS "External allocator: mimalloc ${MIMALLOC_TAG}")

  # Build the shared override library only; skip tests/static/object variants.
  set(MI_BUILD_SHARED ON  CACHE BOOL "" FORCE)
  set(MI_BUILD_STATIC OFF CACHE BOOL "" FORCE)
  set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
  set(MI_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
  set(MI_OVERRIDE     ON  CACHE BOOL "" FORCE)

  FetchContent_Declare(
    mimalloc
    GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
    GIT_TAG        ${MIMALLOC_TAG}
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(mimalloc)

  # Collect the shared library in the common lib directory.
  set_target_properties(mimalloc PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${ALLOC_LIB_DIR}"
  )
endif()

# ─── jemalloc (autotools: configure && make) ───────────────────────────
if(BUILD_JEMALLOC)
  message(STATUS "External allocator: jemalloc ${JEMALLOC_TAG}")

  if(APPLE)
    set(_jemalloc_lib "libjemalloc.dylib")
  else()
    set(_jemalloc_lib "libjemalloc.so")
  endif()

  ExternalProject_Add(jemalloc
    GIT_REPOSITORY    https://github.com/jemalloc/jemalloc.git
    GIT_TAG           ${JEMALLOC_TAG}
    GIT_SHALLOW       TRUE
    BUILD_IN_SOURCE   TRUE
    # jemalloc ships autogen.sh; run it, then configure with an install prefix.
    CONFIGURE_COMMAND ./autogen.sh --prefix=<INSTALL_DIR> --disable-debug
    BUILD_COMMAND     make build_lib_shared -j
    INSTALL_COMMAND   make install_lib_shared install_include
    # Copy the resulting shared library into the common lib directory.
    COMMAND           ${CMAKE_COMMAND} -E copy
                        <INSTALL_DIR>/lib/${_jemalloc_lib}
                        ${ALLOC_LIB_DIR}/${_jemalloc_lib}
    LOG_CONFIGURE     TRUE
    LOG_BUILD         TRUE
    LOG_INSTALL       TRUE
  )
endif()

# ─── tcmalloc / gperftools (CMake native since 2.11) ───────────────────
if(BUILD_TCMALLOC)
  message(STATUS "External allocator: gperftools/tcmalloc ${GPERFTOOLS_TAG}")

  if(APPLE)
    set(_tcmalloc_lib "libtcmalloc_minimal.dylib")
  else()
    set(_tcmalloc_lib "libtcmalloc_minimal.so")
  endif()

  ExternalProject_Add(tcmalloc
    GIT_REPOSITORY    https://github.com/gperftools/gperftools.git
    GIT_TAG           ${GPERFTOOLS_TAG}
    GIT_SHALLOW       TRUE
    CMAKE_ARGS
      -DCMAKE_BUILD_TYPE=Release
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DBUILD_SHARED_LIBS=ON
      -DBUILD_TESTING=OFF
      -DGPERFTOOLS_BUILD_STATIC=OFF
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    # Copy the minimal (malloc-only) shared library into the common lib dir.
    INSTALL_COMMAND   ${CMAKE_COMMAND} --build . --target install
    COMMAND           ${CMAKE_COMMAND} -E copy
                        <INSTALL_DIR>/lib/${_tcmalloc_lib}
                        ${ALLOC_LIB_DIR}/${_tcmalloc_lib}
    LOG_CONFIGURE     TRUE
    LOG_BUILD         TRUE
    LOG_INSTALL       TRUE
  )
endif()

message(STATUS "External allocator libraries will be collected in: ${ALLOC_LIB_DIR}")
