@echo off
rem Transparent XP build: one cl invocation per translation unit (a multi-file
rem cl invocation with this toolchain intermittently produced phantom C2065
rem errors on valid code; per-file invocations are deterministic).
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul 2>&1
cd /d "%~dp0.."
if not exist build\obj mkdir build\obj
if not exist build\out mkdir build\out
if not exist build\obj\testdir mkdir build\obj\testdir
if not exist build\obj\probedir mkdir build\obj\probedir

cl /c /Isrc /nologo /W4 /WX- /O2 /Ob2 /Oy- /D _WINDLL /D WIN32 /D _WINDOWS /D NDEBUG /D WIN32_LEAN_AND_MEAN /D NOMINMAX /D _WIN32_WINNT=0x0501 /D WINVER=0x0501 /EHsc /MT /GS- /GR- /std:c++17 /TP /Fo"build\obj\bootstrap.obj" src\bootstrap.cpp
if errorlevel 1 exit /b 1
cl /c /Isrc /nologo /W4 /WX- /O2 /Ob2 /Oy- /D _WINDLL /D WIN32 /D _WINDOWS /D NDEBUG /D WIN32_LEAN_AND_MEAN /D NOMINMAX /D _WIN32_WINNT=0x0501 /D WINVER=0x0501 /EHsc /MT /GS- /GR- /std:c++17 /TP /Fo"build\obj\config.obj" src\config.cpp
if errorlevel 1 exit /b 1
cl /c /Isrc /nologo /W4 /WX- /O2 /Ob2 /Oy- /D _WINDLL /D WIN32 /D _WINDOWS /D NDEBUG /D WIN32_LEAN_AND_MEAN /D NOMINMAX /D _WIN32_WINNT=0x0501 /D WINVER=0x0501 /EHsc /MT /GS- /GR- /std:c++17 /TP /Fo"build\obj\detour.obj" src\detour.cpp
if errorlevel 1 exit /b 1
cl /c /Isrc /nologo /W4 /WX- /O2 /Ob2 /Oy- /D _WINDLL /D WIN32 /D _WINDOWS /D NDEBUG /D WIN32_LEAN_AND_MEAN /D NOMINMAX /D _WIN32_WINNT=0x0501 /D WINVER=0x0501 /EHsc /MT /GS- /GR- /std:c++17 /TP /Fo"build\obj\dllmain.obj" src\dllmain.cpp
if errorlevel 1 exit /b 1
cl /c /Isrc /nologo /W4 /WX- /O2 /Ob2 /Oy- /D _WINDLL /D WIN32 /D _WINDOWS /D NDEBUG /D WIN32_LEAN_AND_MEAN /D NOMINMAX /D _WIN32_WINNT=0x0501 /D WINVER=0x0501 /EHsc /MT /GS- /GR- /std:c++17 /TP /Fo"build\obj\format.obj" src\format.cpp
if errorlevel 1 exit /b 1
cl /c /Isrc /nologo /W4 /WX- /O2 /Ob2 /Oy- /D _WINDLL /D WIN32 /D _WINDOWS /D NDEBUG /D WIN32_LEAN_AND_MEAN /D NOMINMAX /D _WIN32_WINNT=0x0501 /D WINVER=0x0501 /EHsc /MT /GS- /GR- /std:c++17 /TP /Fo"build\obj\geometry.obj" src\geometry.cpp
if errorlevel 1 exit /b 1
cl /c /Isrc /nologo /W4 /WX- /O2 /Ob2 /Oy- /D _WINDLL /D WIN32 /D _WINDOWS /D NDEBUG /D WIN32_LEAN_AND_MEAN /D NOMINMAX /D _WIN32_WINNT=0x0501 /D WINVER=0x0501 /EHsc /MT /GS- /GR- /std:c++17 /TP /Fo"build\obj\hook_flip.obj" src\hook_flip.cpp
if errorlevel 1 exit /b 1
cl /c /Isrc /nologo /W4 /WX- /O2 /Ob2 /Oy- /D _WINDLL /D WIN32 /D _WINDOWS /D NDEBUG /D WIN32_LEAN_AND_MEAN /D NOMINMAX /D _WIN32_WINNT=0x0501 /D WINVER=0x0501 /EHsc /MT /GS- /GR- /std:c++17 /TP /Fo"build\obj\logging.obj" src\logging.cpp
if errorlevel 1 exit /b 1
cl /c /Isrc /nologo /W4 /WX- /O2 /Ob2 /Oi- /Oy- /D _WINDLL /D WIN32 /D _WINDOWS /D NDEBUG /D WIN32_LEAN_AND_MEAN /D NOMINMAX /D _WIN32_WINNT=0x0501 /D WINVER=0x0501 /EHsc /MT /GS- /GR- /std:c++17 /TP /Fo"build\obj\nocrt.obj" src\nocrt.cpp
if errorlevel 1 exit /b 1
cl /c /Isrc /nologo /W4 /WX- /O2 /Ob2 /Oy- /D _WINDLL /D WIN32 /D _WINDOWS /D NDEBUG /D WIN32_LEAN_AND_MEAN /D NOMINMAX /D _WIN32_WINNT=0x0501 /D WINVER=0x0501 /EHsc /MT /GS- /GR- /std:c++17 /TP /Fo"build\obj\resize.obj" src\resize.cpp
if errorlevel 1 exit /b 1
cl /c /Isrc /nologo /W4 /WX- /O2 /Ob2 /Oy- /D _WINDLL /D WIN32 /D _WINDOWS /D NDEBUG /D WIN32_LEAN_AND_MEAN /D NOMINMAX /D _WIN32_WINNT=0x0501 /D WINVER=0x0501 /EHsc /MT /GS- /GR- /std:c++17 /TP /Fo"build\obj\sha256.obj" src\sha256.cpp
if errorlevel 1 exit /b 1

link /nologo /DLL /NODEFAULTLIB /ENTRY:DllMain /SUBSYSTEM:WINDOWS,5.01 /MACHINE:X86 /IGNORE:4210,4078,4103 /OUT:"build\out\DisplayManager.dll" build\obj\bootstrap.obj build\obj\config.obj build\obj\detour.obj build\obj\dllmain.obj build\obj\format.obj build\obj\geometry.obj build\obj\hook_flip.obj build\obj\logging.obj build\obj\nocrt.obj build\obj\resize.obj build\obj\sha256.obj kernel32.lib user32.lib
if errorlevel 1 exit /b 1

cl /nologo /W4 /EHsc /O2 /std:c++17 /Isrc test\sha256_test.cpp src\format.cpp src\geometry.cpp src\resize.cpp src\sha256.cpp /Fe:"build\out\dm_sha256_test.exe" /Fo:"build\obj\testdir\\" /Fd:"build\obj\testdir\\"
if errorlevel 1 exit /b 1

cl /nologo /W3 /EHsc /O2 /LD test\reset_probe.cpp /Fe:"build\out\ResetProbe.dll" /Fo:"build\obj\probedir\\" /Fd:"build\obj\probedir\\"
if errorlevel 1 exit /b 1

build\out\dm_sha256_test.exe
if errorlevel 1 exit /b 1
echo BUILD OK: build\out\DisplayManager.dll
