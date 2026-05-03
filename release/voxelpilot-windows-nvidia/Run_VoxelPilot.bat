@echo off
setlocal
call "%~dp0launch_voxelpilot_windows.bat" %*
exit /b %ERRORLEVEL%
