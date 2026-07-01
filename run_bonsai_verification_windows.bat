@echo off
setlocal
cd /d "%~dp0"
echo [render-regression] Bonsai 256x256x256 uint8 CT sample
.\volume_renderer_standalone.exe --volume .\sample-data\bonsai_256x256x256_uint8.raw --width 256 --height 256 --depth 256 --type uint8 --snapshot .\build\bonsai_render_regression.png
if errorlevel 1 exit /b %errorlevel%
python tools\verify_render_snapshot.py --no-render --snapshot .\build\bonsai_render_regression.png --metrics .\build\bonsai_render_regression_metrics.json
