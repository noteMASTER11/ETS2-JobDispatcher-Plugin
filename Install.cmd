@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\install-portable.ps1" %*
if errorlevel 1 pause
