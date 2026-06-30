@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
cd /d "%ROOT%"

if not exist "volume_renderer_standalone.exe" (
    echo Missing volume_renderer_standalone.exe. Run build_windows.ps1 first.
    exit /b 2
)

if not exist "sample-data\foot_256x256x256_uint8.raw" (
    echo Missing sample-data\foot_256x256x256_uint8.raw.
    exit /b 2
)

if not exist build mkdir build

echo [render-regression] foot_256x256x256_uint8 baseline
python tools\verify_render_snapshot.py --exe volume_renderer_standalone.exe --volume sample-data\foot_256x256x256_uint8.raw --snapshot build\foot_render_regression.png --metrics build\foot_render_regression_metrics.json
exit /b %ERRORLEVEL%
