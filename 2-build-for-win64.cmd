@echo off
rem ===========================================================================
rem  The Black Wall - build TheBlackWall.scr for 64-bit Windows
rem
rem  Uses the portable toolchain that 1-downloads-tools.cmd put in C:\workenv
rem  (override with the BW_WORKENV variable). Nothing else is required: no
rem  Visual Studio, no system CMake, no network - SDL3 is compiled from the
rem  local source copy.
rem
rem  Usage:  2-build-for-win64.cmd [debug] [clean]
rem            debug   build with symbols into build\win-mingw-debug
rem            clean   throw the build folder away first (full rebuild)
rem ===========================================================================
setlocal enabledelayedexpansion

set "WORKENV=C:\workenv"
if not "%BW_WORKENV%"=="" set "WORKENV=%BW_WORKENV%"

set "ROOT=%~dp0"
set "PRESET=win-mingw"
set "CLEAN="

for %%A in (%*) do (
    if /i "%%~A"=="debug"   set "PRESET=win-mingw-debug"
    if /i "%%~A"=="clean"   set "CLEAN=1"
    if /i "%%~A"=="rebuild" set "CLEAN=1"
)

set "D_W64DEVKIT=%WORKENV%\w64devkit"
set "D_CMAKE=%WORKENV%\cmake-4.4.3-windows-x86_64"
set "D_NINJA=%WORKENV%\ninja"
set "D_SDL=%WORKENV%\SDL3-3.4.16"

echo.
echo  The Black Wall - Windows x64 build
echo  ----------------------------------
echo  toolchain : %WORKENV%
echo  preset    : %PRESET%
echo.

rem --- everything the preset and the toolchain file point at ----------------
call :require "%D_W64DEVKIT%\bin\gcc.exe" "w64devkit / GCC"      || goto :nokit
call :require "%D_CMAKE%\bin\cmake.exe"   "CMake"                || goto :nokit
call :require "%D_NINJA%\ninja.exe"       "Ninja"                || goto :nokit
call :require "%D_SDL%\CMakeLists.txt"    "SDL3 source"          || goto :nokit

rem  The pinned CMake goes on PATH ahead of w64devkit, which carries its own
rem  slightly older copy; Ninja likewise before anything the user may have.
set "PATH=%D_CMAKE%\bin;%D_NINJA%;%D_W64DEVKIT%\bin;%PATH%"

if defined CLEAN (
    if exist "%ROOT%build\%PRESET%" (
        echo  Removing build\%PRESET% ...
        rmdir /s /q "%ROOT%build\%PRESET%" || goto :fail
    )
)

pushd "%ROOT%" || goto :fail

echo  Configuring...
cmake --preset %PRESET%
if errorlevel 1 (popd & goto :fail)

echo.
echo  Compiling...
cmake --build --preset %PRESET%
if errorlevel 1 (popd & goto :fail)

popd

set "OUT=%ROOT%build\%PRESET%\TheBlackWall.scr"
if not exist "%OUT%" (
    echo  ERROR: the build reported success but %OUT% is missing.
    goto :fail
)

for %%S in ("%OUT%") do set "BYTES=%%~zS"
echo.
echo  Built: build\%PRESET%\TheBlackWall.scr  (!BYTES! bytes)
echo.
echo  Try it in a window:   build\%PRESET%\TheBlackWall.scr /w
echo  Install it:           powershell -ExecutionPolicy Bypass -File tools\install-windows.ps1
echo.
endlocal
exit /b 0

rem ===========================================================================
:require
if not exist "%~1" (
    echo  MISSING: %~2
    echo           expected at %~1
    exit /b 1
)
exit /b 0

:nokit
echo.
echo  The build toolchain is not in place. Run this first:
echo.
echo      1-downloads-tools.cmd
echo.
endlocal
exit /b 1

:fail
echo.
echo  BUILD FAILED.
endlocal
exit /b 1
