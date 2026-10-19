@echo off
setlocal
set "coop_python=%~dp0.runtime\python\python.exe"
if not exist "%coop_python%" set "coop_python=%~dp0deps\python\python.exe"
if not exist "%coop_python%" (
    echo Run this from an Unleashed setup or repository folder with its Python runtime.
    pause
    exit /b 1
)
"%coop_python%" "%~dp0steamvr\coop_setup.py" --enable-tcpip %*
set "coop_result=%errorlevel%"
echo.
pause
exit /b %coop_result%
