@echo off
setlocal
cd /d "%~dp0"
echo [render-regression] VTK headsq tiny CT sample
python tools\verify_headsq_snapshot.py %*
