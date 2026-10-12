@echo off
setlocal
title World Monitor - Keep this window open
set "MONITOR_PYTHON=%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
if exist "%MONITOR_PYTHON%" (
    "%MONITOR_PYTHON%" -u "%~dp0serve.py" --open %*
) else (
    py -3 -u "%~dp0serve.py" --open %*
)
if errorlevel 1 (
    echo.
    echo Monitor startup failed. Keep the error above for troubleshooting.
    pause
)
endlocal
