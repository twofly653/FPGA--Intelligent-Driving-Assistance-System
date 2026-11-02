@echo off
setlocal
cd /d "%~dp0"
set PYTHONDONTWRITEBYTECODE=1

rem Usage: 启动车辆网页.cmd [COM5]
set "COMPORT=%~1"
if "%COMPORT%"=="" set "COMPORT=COM5"

set "PYEXE=python"
where python >nul 2>nul || set "PYEXE=%LOCALAPPDATA%\Programs\Python\Python311\python.exe"

echo Board port: %COMPORT%   Dashboard: http://127.0.0.1:8767/#vehicle
"%PYEXE%" -B server.py %COMPORT% --http-port 8767 --open

if errorlevel 1 (
  echo.
  echo Dashboard exited with an error.
  echo If port 8767 is already in use, close the old dashboard first.
  pause
)
endlocal
