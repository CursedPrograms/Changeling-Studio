@echo off
setlocal
cd /d "%~dp0"
rem Changeling Studio: 1. get every Changeling (git), 2. their requirements, 3. their models (each downloaded once
rem and shared), then the Studio window. studio.bat window opens only the window.
set "PY="
where py >nul 2>&1 && set "PY=py -3"
if not defined PY where python >nul 2>&1 && set "PY=python"
if not defined PY (
    echo Changeling Studio needs Python 3.9 or newer: https://www.python.org/downloads/
    pause
    exit /b 1
)
%PY% studio.py %*
if errorlevel 1 pause
