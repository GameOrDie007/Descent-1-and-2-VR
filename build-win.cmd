@echo off
rem Build Descent 1 and 2 VR (on DXX-Redux v1.1) on Windows with MSVC + vcpkg.
rem It links GameOrDieXR, Game Or Die's VR library: from its source where that
rem is present, otherwise the prebuilt one in vr\lib (see README.md).
rem Usage:  build-win.cmd d1     or     build-win.cmd d2
rem
rem Needs Visual Studio or its Build Tools (C++ workload), and vcpkg.
rem   - vcpkg's folder comes from VCPKG_ROOT, vcpkg's own convention.
rem   - Visual Studio is found with vswhere, unless VSROOT is set.
rem Put machine-specific values in build-local.cmd beside this file (git
rem ignores it), e.g.:  set "VCPKG_ROOT=D:\vcpkg"
rem
rem NOTE: uses "if %ERRORLEVEL% NEQ 0", not "if errorlevel 1".  Ninja exits with
rem -1 (4294967295) on a failed link, and "errorlevel 1" tests for >= 1, so it
rem silently reports success on exactly the failure we care about.
rem No parenthesised if-blocks around paths: "Program Files (x86)" would end
rem the block early.
setlocal

if "%~1"=="" goto usage
set "GAME=%~1"
set "EXE="
if /I "%GAME%"=="d1" set "EXE=d1x-redux.exe"
if /I "%GAME%"=="d2" set "EXE=d2x-redux.exe"
if not defined EXE goto badgame

if exist "%~dp0build-local.cmd" call "%~dp0build-local.cmd"

if not defined VCPKG_ROOT goto novcpkg
if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" goto novcpkg
rem Captured NOW: vcvars64.bat below replaces VCPKG_ROOT with Visual Studio's
rem own bundled vcpkg, and a fresh build dir then silently used that one.
set "DXX_VCPKG=%VCPKG_ROOT%"

if defined VSROOT goto havevs
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto novs
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT goto novs
:havevs

set "CMAKE=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
set "OUT=%~dp0build\%GAME%\main\%EXE%"

rem Delete the previous binary so a stale exe can never be mistaken for a fresh build.
if exist "%OUT%" del /q "%OUT%"

echo vcpkg:  %DXX_VCPKG%
echo VS:     %VSROOT%
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if %ERRORLEVEL% NEQ 0 goto novcvars
rem After the check: in a .cmd file a successful SET clears ERRORLEVEL.
set "VCPKG_ROOT=%DXX_VCPKG%"

"%CMAKE%" -S "%~dp0%GAME%" -B "%~dp0build\%GAME%" -G Ninja ^
  -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE="%DXX_VCPKG%\scripts\buildsystems\vcpkg.cmake" ^
  -DVCPKG_TARGET_TRIPLET=x64-windows
if %ERRORLEVEL% NEQ 0 goto failconfig

"%CMAKE%" --build "%~dp0build\%GAME%"
if %ERRORLEVEL% NEQ 0 goto failbuild

rem Final proof: the binary we asked for actually exists on disk.
if not exist "%OUT%" goto nobinary

echo.
echo BUILD OK: %OUT%
goto :eof

:usage
echo Usage: build-win.cmd ^<d1^|d2^>
exit /b 1
:badgame
echo Unknown game "%GAME%" - expected d1 or d2
exit /b 1
:novcpkg
echo FAILED: set VCPKG_ROOT to your vcpkg folder (or put it in build-local.cmd)
exit /b 1
:novs
echo FAILED: no Visual Studio with the C++ tools found - install it, or set VSROOT
exit /b 1
:novcvars
echo FAILED: vcvars64
exit /b 1
:failconfig
echo FAILED: cmake configure
exit /b 1
:failbuild
echo FAILED: build
exit /b 1
:nobinary
echo FAILED: build reported success but %OUT% is missing
exit /b 1
