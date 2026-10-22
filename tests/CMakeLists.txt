add_executable(gl_asr_tests test_options.cpp)
target_link_libraries(gl_asr_tests PRIVATE gl_asr_core)

add_test(NAME cli_options COMMAND gl_asr_tests)

add_executable(gl_asr_audio_tests test_audio_source.cpp)
target_link_libraries(gl_asr_audio_tests PRIVATE gl_asr_core)

add_test(NAME audio_source COMMAND gl_asr_audio_tests)

add_executable(gl_asr_preprocess_tests test_preprocessor.cpp)
target_link_libraries(gl_asr_preprocess_tests PRIVATE gl_asr_core)

add_test(NAME preprocessor COMMAND gl_asr_preprocess_tests)
