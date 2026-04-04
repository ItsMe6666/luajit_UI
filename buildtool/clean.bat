@echo off
setlocal
cd /d "%~dp0\.."

if exist ".xmake" rd /s /q ".xmake"
if exist "bin" rd /s /q "bin"
if exist "bin-int" rd /s /q "bin-int"
if exist "build" rd /s /q "build"
if exist ".cache" rd /s /q ".cache"

echo Cleaned: .xmake\windows\x64, bin, bin-int, build, .cache
endlocal
