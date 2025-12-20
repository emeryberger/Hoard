cmake_minimum_required(VERSION 3.15)
project(hoard LANGUAGES CXX)

#
# ─── OPTIONS ──────────────────────────────────────────────────────────
#

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
  endif()

elseif(APPLE)
  set(HOARD_SOURCES ${MACOS_SOURCES})
  add_compile_options(-DNDEBUG -ftls-model=initial-exec -ftemplate-depth=1024)
else()
  set(HOARD_SOURCES ${UNIX_SOURCES})
  add_definitions(-DNDEBUG)
endif()

set(CMAKE_BUILD_TYPE RelWithDebInfo)

add_library(hoard SHARED ${HOARD_SOURCES})

# Platform-specific linking
if(WIN32)
  # Link with Detours and Windows libraries
  target_link_libraries(hoard PRIVATE Detours::Detours psapi)
  target_include_directories(hoard PRIVATE ${Detours_INCLUDE_DIR})

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

install(TARGETS hoard
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin)
