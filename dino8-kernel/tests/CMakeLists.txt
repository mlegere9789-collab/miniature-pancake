add_executable(dino8_kernel_tests test_basic.cpp)
target_link_libraries(dino8_kernel_tests PRIVATE dino8_kernel)

add_test(NAME dino8_kernel_smoke COMMAND dino8_kernel_tests)
add_executable(dino8_scratch_test scratch_test.cpp)
target_link_libraries(dino8_scratch_test PRIVATE dino8_kernel)
# Standalone BooleanCombineGeneral coverage sweep (a measurement harness,
# deliberately NOT registered with add_test - see its own top comment).
add_executable(dino8_general_boolean_sweep general_boolean_sweep.cpp)
target_link_libraries(dino8_general_boolean_sweep PRIVATE dino8_kernel)
