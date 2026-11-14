# Dino 8 Plug-in SDK ("dino8-plugin-sdk"): the stable, public, ABI-only part
# of the repository, packaged so a third party can build a plug-in WITHOUT
# the Dino 8 source tree, its dependencies, or even a C++ compiler.
#
# It installs exactly one header (include/dino8_plugin.h, the single source
# of truth - this directory never keeps a copy of it) plus a CMake package:
#
#   find_package(Dino8PluginSdk 2 REQUIRED)
#   dino8_add_plugin(my_plugin SOURCES my_plugin.c)
#
# Two ways to use this file:
#   * standalone:  cmake -S plugin-sdk -B build -DCMAKE_INSTALL_PREFIX=<prefix>
#                  cmake --install build          (what the SDK test does)
#   * from the app build (add_subdirectory in dino8-app/CMakeLists.txt):
#                  cmake --build <app-build> --target dino8-plugin-sdk
#                  stages the SDK in <app-build>/dino8-plugin-sdk/
# The install rules use COMPONENT sdk, which CPACK_COMPONENTS_ALL (app only)
# deliberately leaves out of the Dino 8 installers.
cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  # Standalone: derive the SDK version from the header so the two cannot drift.
  set(_hdr "${CMAKE_CURRENT_SOURCE_DIR}/../include/dino8_plugin.h")
  file(STRINGS "${_hdr}" _api_line REGEX "^#define DINO8_PLUGIN_API_VERSION [0-9]+")
  string(REGEX REPLACE ".*DINO8_PLUGIN_API_VERSION ([0-9]+).*" "\\1" _api "${_api_line}")
  project(Dino8PluginSdk VERSION ${_api}.0.0 LANGUAGES NONE)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(_sdk_header "${CMAKE_CURRENT_SOURCE_DIR}/../include/dino8_plugin.h")
file(STRINGS "${_sdk_header}" _sdk_api_line REGEX "^#define DINO8_PLUGIN_API_VERSION [0-9]+")
string(REGEX REPLACE ".*DINO8_PLUGIN_API_VERSION ([0-9]+).*" "\\1" DINO8_SDK_API_VERSION "${_sdk_api_line}")
if(NOT DINO8_SDK_API_VERSION MATCHES "^[0-9]+$")
  message(FATAL_ERROR "could not read DINO8_PLUGIN_API_VERSION from ${_sdk_header}")
endif()
# The SDK version IS the plug-in API version (major) - the ABI is additive
# (see docs/PLUGIN_GUIDE.md), so a newer SDK satisfies find_package(... N)
# for any older N (AnyNewerVersion below).
set(DINO8_SDK_VERSION "${DINO8_SDK_API_VERSION}.0.0")

set(DINO8_SDK_INCLUDE_SUBDIR "dino8-plugin-sdk")
set(DINO8_SDK_CMAKE_DIR "share/cmake/Dino8PluginSdk")

install(FILES "${_sdk_header}"
  DESTINATION "include/${DINO8_SDK_INCLUDE_SUBDIR}" COMPONENT sdk)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/Dino8PluginSdkConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/Dino8PluginSdkConfig.cmake"
  INSTALL_DESTINATION "${DINO8_SDK_CMAKE_DIR}")
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/Dino8PluginSdkConfigVersion.cmake"
  VERSION "${DINO8_SDK_VERSION}"
  COMPATIBILITY AnyNewerVersion)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/Dino8PluginSdkConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/Dino8PluginSdkConfigVersion.cmake"
  "${CMAKE_CURRENT_SOURCE_DIR}/Dino8PluginSdkMacros.cmake"
  DESTINATION "${DINO8_SDK_CMAKE_DIR}" COMPONENT sdk)

# `cmake --build <dir> --target dino8-plugin-sdk` stages the SDK into
# <dir>/dino8-plugin-sdk (point CMAKE_PREFIX_PATH at it, or copy it).
add_custom_target(dino8-plugin-sdk
  COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}" --component sdk
          --prefix "${CMAKE_BINARY_DIR}/dino8-plugin-sdk"
  COMMENT "Staging the Dino 8 plug-in SDK into ${CMAKE_BINARY_DIR}/dino8-plugin-sdk"
  VERBATIM)
