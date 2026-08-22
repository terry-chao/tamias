@echo off
setlocal
set "DIR=%~1"
if "%DIR%"=="" set "DIR=%~dp0..\build\wasm\bin"
if /I "%~x1"==".js" set "DIR=%~dp1"
cd /d "%DIR%"

powershell -NoProfile -File "%~dp0wasm-stop.ps1"

echo Tamias viewer: http://localhost:3000
echo Serving %CD%
echo Close the TamiasWasmServe window, or CMake-build the wasm-stop preset, to stop.
start "TamiasWasmServe" cmd /k "title TamiasWasmServe & echo Tamias viewer: http://localhost:3000 & npx --yes serve . -l 3000"
timeout /t 1 /nobreak >nul
start "" "http://localhost:3000"
