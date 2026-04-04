@echo off
setlocal
cd /d "%~dp0\.."
call "%~dp0build_debug.bat"
if errorlevel 1 exit /b 1
start "" "bin\debug-windows-x64\luajit_ui.exe"
endlocal
