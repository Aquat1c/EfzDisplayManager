cmake_minimum_required(VERSION 3.15)
project(DisplayManager VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static linking - no VC++ runtime dependency (also required for XP-clean
# imports: no UCRT/api-ms-win-crt DLLs).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(DisplayManager SHARED
    src/bootstrap.cpp
    src/config.cpp
    src/detour.cpp
    src/dllmain.cpp
    src/format.cpp
    src/geometry.cpp
    src/hook_flip.cpp
    src/logging.cpp
    src/resize.cpp
    src/nocrt.cpp
    src/sha256.cpp
)

target_include_directories(DisplayManager PRIVATE src)

# Output name without "lib" prefix (mod loader expects mods/DisplayManager/DisplayManager.dll)
set_target_properties(DisplayManager PROPERTIES
    PREFIX ""
    OUTPUT_NAME "DisplayManager"
)

# Windows XP compatibility: XP-era API level, minimum OS version 5.01 in the
# PE headers, and no post-XP imports anywhere in the code. Build with the
# v141 (MSVC 14.16) toolset - the last fully XP-clean CRT generation:
#   cmake -S . -B build-xp -A Win32 -T v141
target_compile_definitions(DisplayManager PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0501
    WINVER=0x0501
)

if(MSVC)
    target_compile_options(DisplayManager PRIVATE /W4 /GS- /GR-)
    # 5.01 minimum OS/subsystem version in the PE optional header.
    # CRT-free link: the v141 static CRT imports Vista+ FlsAlloc/
    # InitializeCriticalSectionEx, which would not load on XP x86. With no
    # CRT the import table contains only the Win32 APIs this module calls.
    target_link_options(DisplayManager PRIVATE
        "/SUBSYSTEM:WINDOWS,5.01"
        "/NODEFAULTLIB"
        "/ENTRY:DllMain"
        "/IGNORE:4210,4078,4103"
    )
endif()

# Intrinsics must be off for the CRT replacement definitions.
set_source_files_properties(src/nocrt.cpp PROPERTIES COMPILE_OPTIONS "/Oi-")

# Self-test (normal CRT console app; not deployed).
add_executable(dm_sha256_test test/sha256_test.cpp src/format.cpp src/geometry.cpp src/resize.cpp src/sha256.cpp)
target_include_directories(dm_sha256_test PRIVATE src)
if(MSVC)
    target_compile_options(dm_sha256_test PRIVATE /W4)
target_link_options(dm_sha256_test PRIVATE "/MAP:../test.map")
endif()
enable_testing()
add_test(NAME sha256_vectors COMMAND dm_sha256_test)
