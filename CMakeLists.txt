cmake_minimum_required(VERSION 3.15)
project(DisplayManager VERSION 1.1.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(CTest)
option(DM_BUILD_RESET_PROBE "Build the manual D3D9 DEFAULT-pool coexistence probe" OFF)
option(DM_BUILD_UPLOAD_PROBE "Build the manual exclusive D3D9 upload probe" OFF)
option(DM_BUILD_PROFILE_FIXTURE_TEST "Build the explicit Windows file/profile hash validator" OFF)

# These sources have no Windows dependency; the same code is used by the DLL
# and the host tests. File I/O is isolated in sha256_file.cpp.
set(DM_PORTABLE_SOURCES
    src/format.cpp
    src/fullscreen.cpp
    src/geometry.cpp
    src/resize.cpp
    src/reset_transaction.cpp
    src/sha256.cpp
)

if(WIN32 OR BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
endif()

if(WIN32)
    set(DM_CONFIG_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/config_defaults.h")
    add_custom_command(OUTPUT "${DM_CONFIG_HEADER}"
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_config.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/DisplayManager.ini" "${DM_CONFIG_HEADER}"
        DEPENDS DisplayManager.ini tools/embed_config.py
        VERBATIM)
    add_custom_target(dm_config_defaults DEPENDS "${DM_CONFIG_HEADER}")
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
        message(FATAL_ERROR "DisplayManager requires Win32/x86. Use -A Win32 with MSVC.")
    endif()
    if(NOT MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "The x86 assembly shim requires MSVC or Clang with Microsoft assembly extensions.")
    endif()

    add_library(DisplayManager SHARED
        ${DM_PORTABLE_SOURCES}
        src/bootstrap.cpp
        src/compat.cpp
        src/config.cpp
        src/detour.cpp
        src/dllmain.cpp
        src/flip_model.cpp
        src/hook_flip.cpp
        src/logging.cpp
        src/nocrt.cpp
        src/renderer_reset.cpp
        src/sha256_file.cpp
        src/shaders.cpp
        src/upload_trace.cpp
    )
    add_dependencies(DisplayManager dm_config_defaults)
    target_include_directories(DisplayManager PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set_target_properties(DisplayManager PROPERTIES PREFIX "" OUTPUT_NAME "DisplayManager")
    target_compile_definitions(DisplayManager PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0501 WINVER=0x0501
    )
    target_link_libraries(DisplayManager PRIVATE kernel32 user32)

    # The shipped DLL is CRT-free. Static CRT selection alone is insufficient:
    # even a static CRT can add imports unavailable on Windows XP.
    if(MSVC)
        target_compile_options(DisplayManager PRIVATE /W4 /GS- /GR- /Oy- /arch:IA32)
        set_source_files_properties(src/nocrt.cpp PROPERTIES COMPILE_OPTIONS "/Oi-")
        target_link_options(DisplayManager PRIVATE
            "/NODEFAULTLIB" "/ENTRY:DllMain" "/MACHINE:X86"
            "/SUBSYSTEM:WINDOWS,5.01" "/IGNORE:4210,4078,4103"
            "/ALTERNATENAME:__fltused=__dm_fltused"
        )
    else()
        # LLVM-MinGW provides a real PE32 compile/link check from Linux too.
        target_compile_options(DisplayManager PRIVATE
            -Wall -Wextra -march=i686 -fms-extensions -fasm-blocks
            -fno-exceptions -fno-rtti -fno-stack-protector
            -fno-omit-frame-pointer -ffreestanding -fno-builtin
        )
        target_link_options(DisplayManager PRIVATE
            -nostdlib -fuse-ld=lld
            "-Wl,--entry,_DllMain@12" "-Wl,--subsystem,windows:5.01"
            "-Wl,--major-os-version,5" "-Wl,--minor-os-version,1"
            "-Wl,--disable-auto-import"
        )
    endif()

    if(DM_BUILD_RESET_PROBE)
        add_library(dm_reset_probe SHARED test/reset_probe.cpp)
        set_target_properties(dm_reset_probe PROPERTIES PREFIX "" OUTPUT_NAME "ResetProbe")
    endif()

    if(DM_BUILD_UPLOAD_PROBE)
        add_executable(dm_upload_probe tools/upload_probe.cpp)
        target_link_libraries(dm_upload_probe PRIVATE d3d9 user32 gdi32 winmm)
    endif()

    if(DM_BUILD_PROFILE_FIXTURE_TEST)
        add_executable(dm_profile_fixture_test
            test/profile_fixture_test.cpp src/sha256.cpp src/sha256_file.cpp
        )
        target_include_directories(dm_profile_fixture_test PRIVATE src)
        # This generated fixture exercises the actual Win32 file-reading path.
        # Proprietary profile binaries are never required or discovered implicitly.
        if(BUILD_TESTING)
            file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/sha256_abc_fixture.txt" "abc")
            add_test(NAME sha256_file_abc COMMAND dm_profile_fixture_test --file
                "${CMAKE_CURRENT_BINARY_DIR}/sha256_abc_fixture.txt"
                ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
            )
            # A game folder outside the ANSI code page must still hash.
            file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/日本語フォルダ/abc.txt" "abc")
            add_test(NAME sha256_file_unicode_path COMMAND dm_profile_fixture_test --file
                "${CMAKE_CURRENT_BINARY_DIR}/日本語フォルダ/abc.txt"
                ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
            )
        endif()
    endif()
elseif(DM_BUILD_RESET_PROBE OR DM_BUILD_PROFILE_FIXTURE_TEST OR DM_BUILD_UPLOAD_PROBE)
    message(FATAL_ERROR "The manual D3D9 probe and file/profile validator require Windows.")
endif()

if(BUILD_TESTING)
    add_test(NAME config_template COMMAND ${Python3_EXECUTABLE} -B -m unittest discover
        -s ${CMAKE_CURRENT_SOURCE_DIR}/test -p config_template_test.py)
    add_test(NAME frame_trace COMMAND ${Python3_EXECUTABLE} -B -m unittest discover
        -s ${CMAKE_CURRENT_SOURCE_DIR}/test -p frame_trace_test.py)
    add_executable(dm_sha256_test test/sha256_test.cpp ${DM_PORTABLE_SOURCES})
    target_include_directories(dm_sha256_test PRIVATE src)
    add_executable(DisplayManagerResetTests
        test/reset_transaction_test.cpp src/reset_transaction.cpp
    )
    target_include_directories(DisplayManagerResetTests PRIVATE src)
    if(MSVC)
        target_compile_options(dm_sha256_test PRIVATE /W4)
        target_compile_options(DisplayManagerResetTests PRIVATE /W4)
    else()
        target_compile_options(dm_sha256_test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(DisplayManagerResetTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME portable_core COMMAND dm_sha256_test)
    add_test(NAME reset_transaction COMMAND DisplayManagerResetTests)

    if(WIN32)
        add_executable(dm_config_test test/config_test.cpp src/config.cpp)
        add_dependencies(dm_config_test dm_config_defaults)
        target_include_directories(dm_config_test PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
        add_test(NAME config_files COMMAND dm_config_test)
        # Exercise the production adapter and participant discovery with fake
        # COM objects and real loader APIs. No graphics device is required.
        add_executable(DisplayManagerRendererResetTests
            test/renderer_reset_test.cpp
            src/renderer_reset.cpp src/reset_transaction.cpp src/compat.cpp src/fullscreen.cpp
        )
        target_include_directories(DisplayManagerRendererResetTests PRIVATE src test)
        target_compile_definitions(DisplayManagerRendererResetTests PRIVATE
            WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0501 WINVER=0x0501
        )
        target_link_libraries(DisplayManagerRendererResetTests PRIVATE kernel32 user32)
        set(DM_RENDERER_TEST_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/renderer-reset-tests/$<CONFIG>")
        set_target_properties(DisplayManagerRendererResetTests PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${DM_RENDERER_TEST_OUTPUT_DIR}"
        )
        foreach(kind IN ITEMS training netplay legacy)
            add_library(dm_reset_fixture_${kind} SHARED test/reset_participant_fixture.cpp)
            target_include_directories(dm_reset_fixture_${kind} PRIVATE src test)
            set_target_properties(dm_reset_fixture_${kind} PROPERTIES
                PREFIX ""
                RUNTIME_OUTPUT_DIRECTORY "${DM_RENDERER_TEST_OUTPUT_DIR}"
                LIBRARY_OUTPUT_DIRECTORY "${DM_RENDERER_TEST_OUTPUT_DIR}"
            )
            add_dependencies(DisplayManagerRendererResetTests dm_reset_fixture_${kind})
        endforeach()
        set_target_properties(dm_reset_fixture_training PROPERTIES OUTPUT_NAME "efz_training_mode")
        set_target_properties(dm_reset_fixture_netplay PROPERTIES OUTPUT_NAME "efz_netplay_mod")
        set_target_properties(dm_reset_fixture_legacy PROPERTIES OUTPUT_NAME "ResetProbe")
        target_compile_definitions(dm_reset_fixture_legacy PRIVATE DM_RESET_FIXTURE_LEGACY=1)
        add_test(NAME renderer_reset COMMAND DisplayManagerRendererResetTests)
    endif()

endif()
