@echo off
setlocal
cd /d "%~dp0"
set PYTHONDONTWRITEBYTECODE=1
set "PHOTO_PY=%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
if exist "%PHOTO_PY%" (
  "%PHOTO_PY%" -E -B -c "from PIL import Image" >nul 2>nul
  if not errorlevel 1 goto :bundled
)
python -B -c "import sys; from PIL import Image; sys.exit(0 if sys.version_info >= (3, 10) else 1)" >nul 2>nul
if not errorlevel 1 (
  python -B server.py COM10 --http-port 8765 --open
  goto :done
)
echo Python 3.10+ with Pillow is required. The existing Codex Python runtime is supported.
pause
exit /b 1
:bundled
"%PHOTO_PY%" -E -B server.py COM10 --http-port 8765 --open
:done
if errorlevel 1 (
  echo Startup failed. If port 8765 is occupied, close the old dashboard first.
  echo This script does not stop any existing process or program the board.
  pause
  exit /b 1
)
endlocal
