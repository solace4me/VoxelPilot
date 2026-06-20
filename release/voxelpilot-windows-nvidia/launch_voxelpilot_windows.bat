@echo off
setlocal

set "ROOT=%~dp0"
cd /d "%ROOT%"

set "APP=volume_renderer_standalone.exe"
set "CUDA_BIN="

if defined CUDA_PATH if exist "%CUDA_PATH%\bin" (
    set "CUDA_BIN=%CUDA_PATH%\bin"
)

if not defined CUDA_BIN (
    for /d %%D in ("C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v*") do (
        if exist "%%~fD\bin" set "CUDA_BIN=%%~fD\bin"
    )
)

if defined CUDA_BIN (
    set "PATH=%CUDA_BIN%;%PATH%"
)

echo VoxelPilot Launcher
echo ======================
echo.

if not exist "%APP%" (
    echo Error: %APP% was not found in:
    echo   %ROOT%
    echo.
    echo Build the standalone app first, then run this launcher again.
    exit /b 1
)

where /q nvidia-smi
if errorlevel 1 (
    echo Warning: nvidia-smi was not found.
    echo The app needs an NVIDIA GPU with a working driver to run.
    echo.
) else (
    echo NVIDIA driver check:
    nvidia-smi --query-gpu=name,driver_version --format=csv,noheader
    echo.
)

echo Starting VoxelPilot...
echo.
"%APP%" %*
set "APP_EXIT=%ERRORLEVEL%"

if not "%APP_EXIT%"=="0" (
    echo.
    echo VoxelPilot exited with code %APP_EXIT%.
    echo If this is an NVIDIA laptop, confirm that:
    echo   1. The NVIDIA graphics driver is installed.
    echo   2. CUDA runtime support is available.
    echo   3. OpenGL is working on the active display adapter.
    echo.
    if not defined MEDRAY_NO_PAUSE pause
)

exit /b %APP_EXIT%
