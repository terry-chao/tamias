@echo off
rem Thin launcher for scripts\build-server.ps1 (see that file for the real logic).
rem   %* = forwarded to the PowerShell script (e.g. -WithShell, -Offline).
rem NOTE: keep this file ASCII-only (cmd reads it with the console codepage).
setlocal
cd /d "%~dp0.."
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-server.ps1" %*
exit /b %ERRORLEVEL%
