cmake_minimum_required(VERSION 3.20)

project(gl_asr VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(GL_ASR_BUILD_TESTS "Build GL_ASR unit tests" ON)
option(GL_ASR_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile)
pkg_check_modules(FFTWF REQUIRED IMPORTED_TARGET fftw3f)

add_library(gl_asr_core
    src/audio/audio_source.cpp
    src/cli/options.cpp
    src/features/feature_extractor.cpp
    src/preprocess/preprocessor.cpp
)
target_include_directories(gl_asr_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(gl_asr_core PUBLIC PkgConfig::SNDFILE PkgConfig::FFTWF)

target_compile_options(gl_asr_core PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic -Wconversion -Wshadow>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

if(GL_ASR_WARNINGS_AS_ERRORS)
    target_compile_options(gl_asr_core PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/WX>
    )
endif()

add_executable(gl_asr src/main.cpp)
target_link_libraries(gl_asr PRIVATE gl_asr_core)

if(GL_ASR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
