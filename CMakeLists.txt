cmake_minimum_required(VERSION 3.15)
project(DisplayManager VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static linking - no VC++ runtime dependency
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(DisplayManager SHARED
    src/dllmain.cpp
    src/logging.cpp
)

target_include_directories(DisplayManager PRIVATE src)

# Output name without "lib" prefix (mod loader expects mods/DisplayManager/DisplayManager.dll)
set_target_properties(DisplayManager PROPERTIES
    PREFIX ""
    OUTPUT_NAME "DisplayManager"
)

# Disable unnecessary Windows headers
target_compile_definitions(DisplayManager PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    target_compile_options(DisplayManager PRIVATE /W4)
endif()
