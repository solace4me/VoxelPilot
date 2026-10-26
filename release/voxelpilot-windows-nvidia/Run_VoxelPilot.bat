@echo off
setlocal
rem --demo keeps the presenter banner on for the packaged presentation build.
call "%~dp0launch_voxelpilot_windows.bat" --demo %*
exit /b %ERRORLEVEL%
