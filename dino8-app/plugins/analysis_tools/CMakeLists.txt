# AnalysisTools sample plug-in: selection/bbox analysis and Fibonacci-sphere
# point distribution, built completely independently of the Dino8 target.
add_library(dino8_plugin_analysis_tools SHARED analysis_tools_plugin.c)
set_target_properties(dino8_plugin_analysis_tools PROPERTIES
  PREFIX ""
  OUTPUT_NAME "analysis_tools"
  C_STANDARD 99)
target_include_directories(dino8_plugin_analysis_tools PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
if(WIN32)
  set_target_properties(dino8_plugin_analysis_tools PROPERTIES SUFFIX ".dll")
elseif(APPLE)
  set_target_properties(dino8_plugin_analysis_tools PROPERTIES SUFFIX ".dylib")
else()
  set_target_properties(dino8_plugin_analysis_tools PROPERTIES SUFFIX ".so")
endif()
if(NOT WIN32)
  target_link_libraries(dino8_plugin_analysis_tools PRIVATE m)
endif()
add_custom_command(TARGET dino8_plugin_analysis_tools POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:Dino8>/plugins
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:dino8_plugin_analysis_tools> $<TARGET_FILE_DIR:Dino8>/plugins/
  COMMENT "Copying the AnalysisTools sample plug-in next to the executable")
