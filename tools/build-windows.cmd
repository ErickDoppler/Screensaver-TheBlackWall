@echo off
rem One-shot Release build: configures (first time) and builds TheBlackWall.scr
call "%~dp0env.cmd"
cd /d "%~dp0.."
cmake --preset win-mingw || exit /b 1
cmake --build --preset win-mingw || exit /b 1
echo.
echo Built: build\win-mingw\TheBlackWall.scr
