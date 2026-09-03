cmake_minimum_required(VERSION 3.20)
project(dino8_kernel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(DINO8_KERNEL_BUILD_TESTS "Build dino8-kernel tests" ON)

include(FetchContent)

# Pin to a specific OpenNURBS tag rather than tracking a moving branch, so
# builds are reproducible. Bump this deliberately when the kernel needs a
# newer OpenNURBS feature.
FetchContent_Declare(
  opennurbs
  GIT_REPOSITORY https://github.com/mcneel/opennurbs.git
  GIT_TAG        v8.34.26223.11001
)
FetchContent_MakeAvailable(opennurbs)

add_library(dino8_kernel STATIC
  src/curve.cpp
  src/surface.cpp
  src/brep.cpp
  src/file_io.cpp
)

target_include_directories(dino8_kernel
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# OpenNURBS' own CMakeLists creates a link-order problem for static
# consumers: its zlib target needs zcalloc/zcfree, which are only defined
# inside opennurbsStatic itself (opennurbs_zlib_memory.cpp), but
# opennurbsStatic is linked *before* zlib in its own target_link_libraries
# call. A plain linker pass never revisits an earlier archive, so the
# symbols go unresolved. Wrapping both in a linker group lets ld rescan
# until everything resolves, without patching the vendored CMakeLists.
target_link_libraries(dino8_kernel PUBLIC
  "-Wl,--start-group" opennurbsStatic zlib "-Wl,--end-group"
)

if(DINO8_KERNEL_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
