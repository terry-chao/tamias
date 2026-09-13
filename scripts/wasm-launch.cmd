@echo off
rem WASM preview, kept for the CMake "serve_viewer" target and the wasm-serve preset.
rem VS Code F5 calls scripts\wasm-preview.ps1 directly.
rem The real logic is in wasm-preview.ps1; -NoBuild because CMake already built
rem tamias_viewer as a dependency of serve_viewer.
rem NOTE: keep this file ASCII-only (cmd reads it with the console codepage).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0wasm-preview.ps1" -NoBuild
exit /b %ERRORLEVEL%
