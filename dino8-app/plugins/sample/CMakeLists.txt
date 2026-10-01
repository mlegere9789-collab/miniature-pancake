# The HelloDino sample plug-in: proves the Dino 8 plug-in ABI end to end.
# A shared library built completely independently of the Dino8 target - it
# only needs the public dino8_plugin.h header, exactly like a third-party
# plug-in would.
add_library(dino8_plugin_sample SHARED sample_plugin.c)
set_target_properties(dino8_plugin_sample PROPERTIES
  PREFIX ""
  OUTPUT_NAME "hello_dino"
  C_STANDARD 99)
target_include_directories(dino8_plugin_sample PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
if(WIN32)
  set_target_properties(dino8_plugin_sample PROPERTIES SUFFIX ".dll")
elseif(APPLE)
  set_target_properties(dino8_plugin_sample PROPERTIES SUFFIX ".dylib")
else()
  set_target_properties(dino8_plugin_sample PROPERTIES SUFFIX ".so")
endif()
# Copied next to the app's own plugins/ folder so PlugInManager finds it
# without any configuration, and the plug-in test can load it by name.
add_custom_command(TARGET dino8_plugin_sample POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:Dino8>/plugins
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:dino8_plugin_sample> $<TARGET_FILE_DIR:Dino8>/plugins/
  COMMENT "Copying the HelloDino sample plug-in next to the executable")
