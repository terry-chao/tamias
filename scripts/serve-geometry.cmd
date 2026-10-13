@echo off
rem Geometry server window (the .NET shell over tamias_server.dll).
rem Launched by scripts\wasm-preview.ps1 via Start-Process on purpose, so it does NOT
rem inherit the caller's stdio: otherwise the caller (and VS Code's preLaunchTask) would
rem wait for this window to close before continuing.
rem   %1 = directory holding Tamias.Server.exe + tamias_server.dll, %2 = port
rem Closing this window stops the geometry server (the app runs in the foreground here).
rem NOTE: keep this file ASCII-only (cmd reads it with the console codepage).
setlocal
if "%~1"=="" exit /b 1
if "%~2"=="" exit /b 1
title TamiasGeometryServe
cd /d "%~1" || exit /b 1

rem Development = detailed error pages; local debugging wants the stack traces.
set ASPNETCORE_ENVIRONMENT=Development

echo Tamias geometry server: http://127.0.0.1:%~2
echo   GET /health  -^> native version + ABI
echo   GET /        -^> route table
echo Serving %CD%
echo Close this window to stop the geometry server.

"%~1\Tamias.Server.exe" --urls "http://127.0.0.1:%~2"
