# Standalone CMake project for the Spike R1 boundary probe. It is NOT part of the application
# build. It shows the integration pattern the real app CMake will use: Corrosion imports the Rust
# crate as CMake targets, and a C++ executable links against it like any other library.
#
#   cmake -S dino8-core/ffi-probe/cpp -B build-probe -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
#   cmake --build build-probe -j2 && ctest --test-dir build-probe --output-on-failure
#
# Sanitizers: -DD8_SANITIZE="address;undefined"  or  -DD8_SANITIZE=thread
# Offline: -DFETCHCONTENT_SOURCE_DIR_CORROSION=/path/to/corrosion checkout (tag v0.6.1)
cmake_minimum_required(VERSION 3.22)
project(dino8_ffi_probe_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(D8_SANITIZE "" CACHE STRING "Sanitizers for the C++ side: 'address;undefined' or 'thread'")

include(FetchContent)
set(D8_CORROSION_TAG v0.6.1 CACHE STRING "Corrosion release to fetch")
FetchContent_Declare(
  Corrosion
  GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
  GIT_TAG ${D8_CORROSION_TAG}
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Corrosion)

# One call imports the crate; it yields a static target (dino8_ffi_probe) and a shared one
# (dino8_ffi_probe-shared). Corrosion runs cargo with the matching profile and passes the system
# libraries the staticlib needs to the final link.
corrosion_import_crate(MANIFEST_PATH ${CMAKE_CURRENT_LIST_DIR}/../Cargo.toml)

function(d8_probe_executable name rust_target)
  add_executable(${name} ${CMAKE_CURRENT_LIST_DIR}/probe_test.cpp)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../include)
  target_link_libraries(${name} PRIVATE ${rust_target})
  target_compile_options(${name} PRIVATE -Wall -Wextra)
  if(D8_SANITIZE)
    string(REPLACE ";" "," _san "${D8_SANITIZE}")
    target_compile_options(${name} PRIVATE -fsanitize=${_san} -fno-omit-frame-pointer)
    target_link_options(${name} PRIVATE -fsanitize=${_san})
    if(_san MATCHES "undefined")
      target_compile_options(${name} PRIVATE -fno-sanitize-recover=undefined)
    endif()
  endif()
  add_test(NAME ${name} COMMAND ${name} --dir ${CMAKE_BINARY_DIR})
endfunction()

enable_testing()
d8_probe_executable(probe_test dino8_ffi_probe)
d8_probe_executable(probe_test_shared dino8_ffi_probe-shared)

# The committed header must match what cbindgen generates from the Rust sources.
find_program(D8_CBINDGEN cbindgen)
if(D8_CBINDGEN)
  add_test(NAME header_up_to_date COMMAND ${CMAKE_CURRENT_LIST_DIR}/../scripts/gen_header.sh --check)
endif()
