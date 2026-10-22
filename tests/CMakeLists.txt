add_executable(gl_asr_tests test_options.cpp)
target_link_libraries(gl_asr_tests PRIVATE gl_asr_core)

add_test(NAME cli_options COMMAND gl_asr_tests)
