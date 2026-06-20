@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
cd /d "%ROOT%"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VS_INSTALL="
set "VS_DEV_CMD="
set "TEST_EXE=build\volume_import_tests.exe"
set "TEST_SOURCES=tests\volume_import_tests.cpp"

if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VS_INSTALL=%%I"
    )
)

if not defined VS_INSTALL if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" (
    set "VS_INSTALL=C:\Program Files\Microsoft Visual Studio\2022\Community"
)

if not defined VS_INSTALL if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" (
    set "VS_INSTALL=C:\Program Files\Microsoft Visual Studio\2022\BuildTools"
)

if not defined VS_INSTALL (
    echo Could not find a Visual Studio 2022 installation with C++ tools.
    exit /b 1
)

set "VS_DEV_CMD=%VS_INSTALL%\Common7\Tools\VsDevCmd.bat"
if not exist "%VS_DEV_CMD%" (
    echo Could not find VsDevCmd.bat:
    echo   %VS_DEV_CMD%
    exit /b 1
)

if not exist build mkdir build

call "%VS_DEV_CMD%" -arch=amd64 >nul
if errorlevel 1 (
    echo Failed to initialize the Visual Studio build environment.
    exit /b 1
)

if exist common\volume_import.cpp (
    set "TEST_SOURCES=%TEST_SOURCES% common\volume_import.cpp"
)

echo [test-build] %TEST_SOURCES%
cl /nologo /EHsc /std:c++17 /Icommon %TEST_SOURCES% /Fe:%TEST_EXE%
if errorlevel 1 exit /b 1

echo [test-run] %TEST_EXE%
"%TEST_EXE%"
exit /b %ERRORLEVEL%
