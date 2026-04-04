@echo off
setlocal
cd /d "%~dp0\.."
call "%~dp0build_release.bat"
if errorlevel 1 exit /b 1
start "" "bin\release-windows-x64\luajit_ui.exe"
endlocal
