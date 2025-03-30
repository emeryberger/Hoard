cmake_minimum_required(VERSION 3.15)
project(hoard LANGUAGES CXX)

#
# ─── OPTIONS ──────────────────────────────────────────────────────────
#

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

if(APPLE)
  set(HOARD_SOURCES ${MACOS_SOURCES})
  add_compile_options(-DNDEBUG -ftls-model=initial-exec -ftemplate-depth=1024)
else()
  set(HOARD_SOURCES ${UNIX_SOURCES})
  add_definitions(-DNDEBUG)
endif()

set(CMAKE_BUILD_TYPE RelWithDebInfo)

add_library(hoard SHARED ${HOARD_SOURCES})
target_compile_definitions(hoard
    PRIVATE
    _REENTRANT=1
)
# Add threading support if needed
#find_package(Threads)
#if(Threads_FOUND)
#    target_link_libraries(hoard Threads::Threads)
#endif()
# Link with pthread/dl on Unix-like systems; on Windows, CMake will adjust automatically
target_link_libraries(hoard PRIVATE pthread dl)
# Output the final library name as libhoard.so/.dylib on most platforms
set_target_properties(hoard PROPERTIES
  OUTPUT_NAME "hoard"
)

