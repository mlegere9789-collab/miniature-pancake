# dino8_add_plugin(<target> SOURCES <files...> [OUTPUT_NAME <name>] [DESTINATION <dir>])
#
# Builds a Dino 8 plug-in: a shared library with no "lib" prefix and the
# platform's native suffix (.so / .dll / .dylib - the only suffixes Dino 8
# scans for), C99 / C++17 as the sources need, hidden symbol visibility by
# default (the plug-in exports only what DINO8_PLUGIN_EXPORT marks), linked
# against nothing but Dino8::PluginSdk. If DESTINATION is given the built
# library is also copied there after every build.
function(dino8_add_plugin target)
  cmake_parse_arguments(P "" "OUTPUT_NAME;DESTINATION" "SOURCES" ${ARGN})
  if(NOT P_SOURCES)
    message(FATAL_ERROR "dino8_add_plugin(${target}): SOURCES is required")
  endif()
  if(NOT P_OUTPUT_NAME)
    set(P_OUTPUT_NAME "${target}")
  endif()
  add_library(${target} SHARED ${P_SOURCES})
  target_link_libraries(${target} PRIVATE Dino8::PluginSdk)
  set_target_properties(${target} PROPERTIES
    PREFIX ""
    OUTPUT_NAME "${P_OUTPUT_NAME}"
    C_STANDARD 99
    CXX_STANDARD 17
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
  if(WIN32)
    set_target_properties(${target} PROPERTIES SUFFIX ".dll")
  elseif(APPLE)
    set_target_properties(${target} PROPERTIES SUFFIX ".dylib")
  else()
    set_target_properties(${target} PROPERTIES SUFFIX ".so")
  endif()
  if(P_DESTINATION)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${P_DESTINATION}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${target}> "${P_DESTINATION}/"
      COMMENT "Copying ${target} to ${P_DESTINATION}")
  endif()
endfunction()
