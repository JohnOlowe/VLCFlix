@echo off
setlocal
title VLCFlix
cd /d "%~dp0"

echo.
echo   VLCFlix - Netflix-style front-end for VLC
echo   ----------------------------------------
echo.

where node >nul 2>nul
if errorlevel 1 (
  echo   Node.js was not found.
  echo   Install the LTS build from https://nodejs.org and run this again.
  echo.
  pause
  exit /b 1
)

if not exist "node_modules" (
  echo   First run - installing dependencies...
  call npm install --no-audit --no-fund
  if errorlevel 1 (
    echo   Dependency install failed.
    pause
    exit /b 1
  )
)

if not exist "data\catalog.json" (
  echo   No library scanned yet.
  echo   Opening VLCFlix - add a folder from Settings, or load the demo library.
  echo.
)

start "" http://localhost:4173
node server\index.js

echo.
echo   VLCFlix stopped.
pause
