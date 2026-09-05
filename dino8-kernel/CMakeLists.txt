cmake_minimum_required(VERSION 3.20)
project(dino8_kernel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(DINO8_KERNEL_BUILD_TESTS "Build dino8-kernel tests" ON)
option(DINO8_KERNEL_BUILD_EXAMPLES "Build dino8-kernel example programs" ON)

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

# Manifold provides the real mesh-boolean algorithm OpenNURBS doesn't
# (see brep.h's comment for why). Scoped down to just the boolean engine:
# no 2D cross-sections (skips the Clipper2 dependency), no parallel
# backend (skips TBB), no test suite, no Python bindings, static linkage
# to match how we link everything else here.
set(MANIFOLD_CROSS_SECTION OFF CACHE BOOL "" FORCE)
set(MANIFOLD_PAR OFF CACHE BOOL "" FORCE)
set(MANIFOLD_TEST OFF CACHE BOOL "" FORCE)
set(MANIFOLD_PYBIND OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  manifold
  GIT_REPOSITORY https://github.com/elalish/manifold.git
  GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(manifold)

add_library(dino8_kernel STATIC
  src/curve.cpp
  src/surface.cpp
  src/brep.cpp
  src/mesh.cpp
  src/boolean.cpp
  src/file_io.cpp
  src/subd.cpp
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
# --start-group is a GNU ld feature; Apple's ld64 and MSVC's link.exe
# already rescan archives, so they get the plain list.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT APPLE AND NOT MSVC AND NOT WIN32)
  target_link_libraries(dino8_kernel PUBLIC
    "-Wl,--start-group" opennurbsStatic zlib "-Wl,--end-group"
    manifold
  )
else()
  target_link_libraries(dino8_kernel PUBLIC opennurbsStatic zlib manifold)
endif()

if(DINO8_KERNEL_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(DINO8_KERNEL_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
