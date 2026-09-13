@echo off
rem Preview server window. Launched by scripts\wasm-preview.ps1 via Start-Process on
rem purpose, so it does NOT inherit the caller's stdio: otherwise the caller (and
rem VS Code's preLaunchTask) would wait for this window to close before continuing.
rem   %1 = directory to serve, %2 = port
rem Closing this window stops the preview - node runs in the foreground here.
rem NOTE: keep this file ASCII-only (cmd reads it with the console codepage).
setlocal
if "%~1"=="" exit /b 1
if "%~2"=="" exit /b 1
title TamiasWasmServe
cd /d "%~1" || exit /b 1
echo Tamias preview: http://localhost:%~2
echo Serving %CD%
echo Close this window to stop the preview.
node "%~dp0serve-wasm.mjs" "%~1" %~2
