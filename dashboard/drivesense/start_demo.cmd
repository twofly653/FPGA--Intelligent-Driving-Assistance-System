@echo off
cd /d "%~dp0"
where py >nul 2>nul
if not errorlevel 1 (
  py -3 launch_demo.py
) else (
  python launch_demo.py
)
if errorlevel 1 (
  echo Python 3 is required for the one-click launcher.
  echo Start server.py from the parent dashboard folder using Python 3.
  pause
)
