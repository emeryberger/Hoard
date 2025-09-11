cmake_minimum_required(VERSION 3.15)
project(hoard LANGUAGES CXX)

#
# ─── OPTIONS ──────────────────────────────────────────────────────────
#

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES arm64 arm64e x86_64)

# Generate position-independent code; required for shared libraries on many platforms
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Write a version map to enable replacing direct libc allocation calls.
set(VERS_SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/vers.map)
file(WRITE ${VERS_SCRIPT}
"GLIBC_2.2.5 {\n  global:\n    __libc_malloc;\n    __libc_free;\n    __libc_realloc;\n    __libc_calloc;\n    __libc_memalign;\n    local: custom*;\n      xx*;\n};\n")

# Ensure that the compiler doesn't replace anything with malloc/free
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden -fno-builtin-malloc -fno-builtin-free")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden -fno-builtin-malloc -fno-builtin-free")

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

# For Linux, link with the version script declared above.
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_options(hoard PRIVATE "LINKER:--version-script=${VERS_SCRIPT}")
endif()

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

