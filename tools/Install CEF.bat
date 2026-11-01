@echo off
title EyeFinder92SA: browser engine installer
rem Double-click this to install CEF. It runs install-cef.ps1, which is next to it, in this window.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-cef.ps1" %*
set result=%errorlevel%
echo.
pause
exit /b %result%
