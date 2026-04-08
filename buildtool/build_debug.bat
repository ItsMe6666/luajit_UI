@echo off
setlocal
cd /d "%~dp0\.."
xmake project -k compile_commands -y -P .
if errorlevel 1 (
	echo [ERROR] luajit_UI: xmake project -k compile_commands failed.
	exit /b 1
)
xmake f -m debug -a x64 -y
if errorlevel 1 exit /b 1
xmake -r -y
if errorlevel 1 exit /b 1
echo.
echo Output: bin\debug-windows-x64\luajit_ui.exe
endlocal
