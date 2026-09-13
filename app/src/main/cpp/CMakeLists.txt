cmake_minimum_required(VERSION 3.22.1)
project(harmonizer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -ffast-math is deliberately NOT used: the phase accumulators and the cepstral
# envelope both rely on well-behaved denormals and predictable rounding.
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

find_package(oboe REQUIRED CONFIG)

add_library(harmonizer SHARED
        native-lib.cpp
        AudioEngine.cpp
        dsp/Fft.cpp
        dsp/Resampler.cpp
        dsp/Analysis.cpp
        dsp/Voices.cpp
        dsp/Harmonizer.cpp)

target_include_directories(harmonizer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(harmonizer oboe::oboe android log)
