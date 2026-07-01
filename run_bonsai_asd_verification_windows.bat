@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VS_INSTALL="
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VS_INSTALL=%%I"
    )
)
if not defined VS_INSTALL if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL=C:\Program Files\Microsoft Visual Studio\2022\Community"
if not defined VS_INSTALL if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" set "VS_INSTALL=C:\Program Files\Microsoft Visual Studio\2022\BuildTools"
if not defined VS_INSTALL (
    echo Could not find a Visual Studio 2022 installation with C++ tools.
    exit /b 1
)

if not exist build mkdir build
call "%VS_INSTALL%\Common7\Tools\VsDevCmd.bat" -arch=amd64 >nul
if errorlevel 1 exit /b 1

echo [asd-build] tools\verify_asd_dataset.cpp
cl /nologo /EHsc /std:c++17 /Icommon tools\verify_asd_dataset.cpp /Fe:build\verify_asd_dataset.exe
if errorlevel 1 exit /b 1

echo [asd-run] Bonsai 256x256x256 uint8
build\verify_asd_dataset.exe --volume sample-data\bonsai_256x256x256_uint8.raw --width 256 --height 256 --depth 256 --type uint8 --sensitivity 0.72 --min-region-voxels 2048 --min-regions 1 --report build\bonsai_asd_report.json
