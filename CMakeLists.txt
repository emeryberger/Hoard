cmake_minimum_required(VERSION 3.15)
project(hoard LANGUAGES CXX)

#
# ─── OPTIONS ──────────────────────────────────────────────────────────
#

# Default to C++14 (adjust as desired)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Generate position-independent code; required for shared libraries on many platforms
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

#
# ─── INCLUDE DIRECTORIES ──────────────────────────────────────────────
#

include_directories(
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/src/include
  ${PROJECT_SOURCE_DIR}/src/include/hoard
  ${PROJECT_SOURCE_DIR}/src/include/superblocks
  ${PROJECT_SOURCE_DIR}/src/include/util
  ${PROJECT_SOURCE_DIR}/include
  ${PROJECT_SOURCE_DIR}/include/layers
  ${PROJECT_SOURCE_DIR}/include/math
  ${PROJECT_SOURCE_DIR}/include/static
  ${PROJECT_SOURCE_DIR}/include/rng
  ${PROJECT_SOURCE_DIR}/include/hoard
  ${PROJECT_SOURCE_DIR}/include/superblocks
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
  ${heap-layers_SOURCE_DIR}/wrappers/gnuwrapper.cpp
  src/source/unixtls.cpp
  src/source/libhoard.cpp
)

set(MACOS_SOURCES
  ${heap-layers_SOURCE_DIR}/wrappers/macwrapper.cpp
  src/source/mactls.cpp
  src/source/libhoard.cpp
)

if(APPLE)
  set(HOARD_SOURCES ${MACOS_SOURCES})
else()
  set(HOARD_SOURCES ${UNIX_SOURCES})
endif()

set(CMAKE_BUILD_TYPE RelWithDebInfo)

add_library(hoard SHARED ${HOARD_SOURCES})
target_compile_definitions(hoard
    PRIVATE
    _REENTRANT=1
)
# Link with pthread/dl on Unix-like systems; on Windows, CMake will adjust automatically
target_link_libraries(hoard PRIVATE pthread dl)
# Output the final library name as libhoard.so/.dylib on most platforms
set_target_properties(hoard PROPERTIES
  OUTPUT_NAME "hoard"
)
