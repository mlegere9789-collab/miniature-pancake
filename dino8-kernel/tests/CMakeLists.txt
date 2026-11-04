add_executable(dino8_kernel_tests test_basic.cpp)
target_link_libraries(dino8_kernel_tests PRIVATE dino8_kernel)

# The capped-Loft regression (TestLoftPlanarCapsMeshClosedAndVolumeExact)
# checks what the APP's mesher (dino8-app's face-by-face BrepMesher, behind
# MeshOf()) makes of the kernel's solids, so compile that one self-contained
# translation unit (it needs only opennurbs and the kernel's public headers)
# into the test binary when the app sources are checked out next to the
# kernel. A standalone kernel checkout simply skips those checks.
set(DINO8_APP_BREP_MESHER ${CMAKE_CURRENT_SOURCE_DIR}/../../dino8-app/src/geom/BrepMesher.cpp)
if(EXISTS ${DINO8_APP_BREP_MESHER})
  target_sources(dino8_kernel_tests PRIVATE ${DINO8_APP_BREP_MESHER})
  target_include_directories(dino8_kernel_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../dino8-app/src)
  target_compile_definitions(dino8_kernel_tests PRIVATE DINO8_TEST_HAS_APP_MESHER=1)
endif()

add_test(NAME dino8_kernel_smoke COMMAND dino8_kernel_tests)
add_executable(dino8_scratch_test scratch_test.cpp)
target_link_libraries(dino8_scratch_test PRIVATE dino8_kernel)
# Standalone BooleanCombineGeneral coverage sweep (a measurement harness,
# deliberately NOT registered with add_test - see its own top comment).
add_executable(dino8_general_boolean_sweep general_boolean_sweep.cpp)
target_link_libraries(dino8_general_boolean_sweep PRIVATE dino8_kernel)
