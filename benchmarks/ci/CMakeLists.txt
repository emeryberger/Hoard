cmake_minimum_required(VERSION 3.15)
project(hoard-benchmarks C)

set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Common compile options
if(MSVC)
  add_compile_options(/W3 /O2)
else()
  add_compile_options(-Wall -O2)
endif()

# Thread library
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Define all benchmarks
set(BENCHMARKS
  cfrac
  larson
  cache_scratch
  cache_thrash
  mstress
  alloc_test
  malloc_large
  xmalloc_test
  sh6bench
  sh8bench
  rptest
)

# Build each benchmark
foreach(bench ${BENCHMARKS})
  add_executable(${bench} ${bench}.c)
  target_link_libraries(${bench} PRIVATE Threads::Threads)
  if(NOT WIN32)
    target_link_libraries(${bench} PRIVATE m)
  endif()
endforeach()

# Installation
install(TARGETS ${BENCHMARKS}
  RUNTIME DESTINATION bin
)
