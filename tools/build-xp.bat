@echo off
setlocal EnableExtensions EnableDelayedExpansion
rem One cl invocation per production translation unit; fail immediately.
rem Use an existing x86 developer prompt or locate Visual Studio via vswhere.
if /I "%VSCMD_ARG_TGT_ARCH%"=="x86" goto toolchain_ready
set "DM_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%DM_VSWHERE%" (
    echo ERROR: Open an x86 Native Tools command prompt or install Visual Studio C++ tools.
    exit /b 1
)
for /f "usebackq tokens=*" %%I in (`"%DM_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "DM_VS_ROOT=%%I"
if not defined DM_VS_ROOT (
    echo ERROR: No Visual Studio installation with x86 C++ tools was found.
    exit /b 1
)
call "%DM_VS_ROOT%\VC\Auxiliary\Build\vcvarsall.bat" x86
if errorlevel 1 exit /b 1

:toolchain_ready
cd /d "%~dp0.."
for %%D in (build\obj build\out build\obj\testdir build\obj\resettestdir build\obj\probedir build\obj\fixturedir build\obj\renderer-testdir build\out\renderer-reset-tests) do if not exist "%%D" mkdir "%%D"
set "DM_CXXFLAGS=/c /Isrc /nologo /W4 /O2 /Ob2 /Oy- /D_WINDLL /DWIN32 /D_WINDOWS /DNDEBUG /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_WIN32_WINNT=0x0501 /DWINVER=0x0501 /EHsc /MT /GS- /GR- /arch:IA32 /std:c++17 /TP"
set "DM_OBJECTS="
for %%F in (bootstrap compat config detour dllmain flip_model format fullscreen geometry hook_flip logging renderer_reset reset_transaction resize sha256 sha256_file shaders) do (
    cl !DM_CXXFLAGS! /Fo"build\obj\%%F.obj" "src\%%F.cpp"
    if errorlevel 1 exit /b 1
    set "DM_OBJECTS=!DM_OBJECTS! build\obj\%%F.obj"
)
rem Disable intrinsics for the functions supplied by the CRT-free runtime.
cl %DM_CXXFLAGS% /Oi- /Fo"build\obj\nocrt.obj" src\nocrt.cpp
if errorlevel 1 exit /b 1
link /nologo /DLL /NODEFAULTLIB /ENTRY:DllMain /SUBSYSTEM:WINDOWS,5.01 /MACHINE:X86 /IGNORE:4210,4078,4103 /ALTERNATENAME:__fltused=__dm_fltused /OUT:"build\out\DisplayManager.dll" %DM_OBJECTS% build\obj\nocrt.obj kernel32.lib user32.lib
if errorlevel 1 exit /b 1

cl /nologo /W4 /EHsc /MT /O2 /std:c++17 /Isrc test\sha256_test.cpp src\format.cpp src\fullscreen.cpp src\geometry.cpp src\resize.cpp src\sha256.cpp /Fe:"build\out\dm_sha256_test.exe" /Fo:"build\obj\testdir\\" /Fd:"build\obj\testdir\\"
if errorlevel 1 exit /b 1
cl /nologo /W4 /EHsc /MT /O2 /std:c++17 /Isrc test\reset_transaction_test.cpp src\reset_transaction.cpp /Fe:"build\out\DisplayManagerResetTests.exe" /Fo:"build\obj\resettestdir\\" /Fd:"build\obj\resettestdir\\"
if errorlevel 1 exit /b 1
cl /nologo /W3 /EHsc /MT /O2 /LD test\reset_probe.cpp /Fe:"build\out\ResetProbe.dll" /Fo:"build\obj\probedir\\" /Fd:"build\obj\probedir\\"
if errorlevel 1 exit /b 1

rem Compile adapter tests against real Win32 headers and controlled DLL fixtures.
for %%K in (training netplay legacy) do (
    if not exist "build\obj\resetfixture-%%K" mkdir "build\obj\resetfixture-%%K"
    set "DM_FIXTURE_DEFINE="
    if "%%K"=="training" set "DM_FIXTURE_NAME=efz_training_mode"
    if "%%K"=="netplay" set "DM_FIXTURE_NAME=efz_netplay_mod"
    if "%%K"=="legacy" (
        set "DM_FIXTURE_NAME=ResetProbe"
        set "DM_FIXTURE_DEFINE=/DDM_RESET_FIXTURE_LEGACY=1"
    )
    cl /nologo /W4 /EHsc /MT /O2 /std:c++17 /LD /Isrc /Itest !DM_FIXTURE_DEFINE! test\reset_participant_fixture.cpp /Fe:"build\out\renderer-reset-tests\!DM_FIXTURE_NAME!.dll" /Fo:"build\obj\resetfixture-%%K\\" /Fd:"build\obj\resetfixture-%%K\\"
    if errorlevel 1 exit /b 1
)
cl /nologo /W4 /EHsc /MT /O2 /std:c++17 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_WIN32_WINNT=0x0501 /DWINVER=0x0501 /Isrc /Itest test\renderer_reset_test.cpp src\renderer_reset.cpp src\reset_transaction.cpp src\compat.cpp src\fullscreen.cpp kernel32.lib user32.lib /Fe:"build\out\renderer-reset-tests\DisplayManagerRendererResetTests.exe" /Fo:"build\obj\renderer-testdir\\" /Fd:"build\obj\renderer-testdir\\"
if errorlevel 1 exit /b 1

rem Optional helper: each proprietary fixture still requires explicit CLI input.
if /I "%~1"=="--fixtures" (
    cl /nologo /W4 /EHsc /MT /O2 /std:c++17 /Isrc test\profile_fixture_test.cpp src\sha256.cpp src\sha256_file.cpp /Fe:"build\out\dm_profile_fixture_test.exe" /Fo:"build\obj\fixturedir\\" /Fd:"build\obj\fixturedir\\"
    if errorlevel 1 exit /b 1
)

build\out\dm_sha256_test.exe
if errorlevel 1 exit /b 1
build\out\DisplayManagerResetTests.exe
if errorlevel 1 exit /b 1
build\out\renderer-reset-tests\DisplayManagerRendererResetTests.exe
if errorlevel 1 exit /b 1
python tools\verify_pe.py build\out\DisplayManager.dll
if errorlevel 1 exit /b 1
python tools\package.py
if errorlevel 1 exit /b 1
echo BUILD OK: build\out\DisplayManager.dll
