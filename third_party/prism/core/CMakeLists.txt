add_library(prism_core STATIC
    src/Ballistics.cpp
    src/KWeighting.cpp
    src/LoudnessHistory.cpp
    src/LoudnessMeter.cpp
    src/SpectrumAnalyzer.cpp
    src/StereoAnalyzer.cpp
    src/TruePeakMeter.cpp
)

target_include_directories(prism_core PUBLIC include)
target_compile_features(prism_core PUBLIC cxx_std_20)
set_target_properties(prism_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(prism_core PRIVATE /W4)
else()
    target_compile_options(prism_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wno-sign-conversion)
endif()
