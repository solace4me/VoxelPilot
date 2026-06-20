@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0"
cd /d "%ROOT%"

set "BUILD_DIR=build\standalone"
set "STANDALONE_BIN=volume_renderer_standalone.exe"
set "GLFW_LIB=imgui\examples\libs\glfw\lib-vc2010-64\glfw3.lib"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

call :resolve_cuda
if errorlevel 1 exit /b 1

call :resolve_vs
if errorlevel 1 exit /b 1

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

if not "%~1"=="" (
    set "CUDA_ARCH=%~1"
) else (
    call :detect_arch
)

if not defined CUDA_ARCH set "CUDA_ARCH=sm_75"

call "%VS_DEV_CMD%" -arch=amd64 >nul
if errorlevel 1 (
    echo Failed to initialize the Visual Studio build environment.
    exit /b 1
)

set "PATH=%CUDA_HOME%\bin;%PATH%"

echo VoxelPilot Windows Build
echo ========================
echo Repo Root : %ROOT%
echo CUDA Home : %CUDA_HOME%
echo VS DevCmd : %VS_DEV_CMD%
echo CUDA Arch : %CUDA_ARCH%
echo.

if not exist "%GLFW_LIB%" (
    echo Missing GLFW import library:
    echo   %GLFW_LIB%
    exit /b 1
)

if not exist "%CUDA_HOME%\bin\nvcc.exe" (
    echo Missing nvcc:
    echo   %CUDA_HOME%\bin\nvcc.exe
    exit /b 1
)

call :run_nvcc "renderer\renderer.cu" "%BUILD_DIR%\renderer.obj"
if errorlevel 1 exit /b 1

call :run_nvcc "renderer\raymarch_kernel.cu" "%BUILD_DIR%\raymarch_kernel.obj"
if errorlevel 1 exit /b 1

call :run_nvcc "renderer\volume_textures.cu" "%BUILD_DIR%\volume_textures.obj"
if errorlevel 1 exit /b 1

call :run_nvcc "standalone_main.cu" "%BUILD_DIR%\standalone_main.obj"
if errorlevel 1 exit /b 1

call :run_nvcc_xcu "imgui\imgui.cpp" "%BUILD_DIR%\imgui.obj"
if errorlevel 1 exit /b 1

call :run_nvcc_xcu "imgui\imgui_draw.cpp" "%BUILD_DIR%\imgui_draw.obj"
if errorlevel 1 exit /b 1

call :run_nvcc_xcu "imgui\imgui_tables.cpp" "%BUILD_DIR%\imgui_tables.obj"
if errorlevel 1 exit /b 1

call :run_nvcc_xcu "imgui\imgui_widgets.cpp" "%BUILD_DIR%\imgui_widgets.obj"
if errorlevel 1 exit /b 1

call :run_nvcc_xcu "imgui\backends\imgui_impl_glfw.cpp" "%BUILD_DIR%\imgui_impl_glfw.obj"
if errorlevel 1 exit /b 1

call :run_nvcc_xcu "imgui\backends\imgui_impl_opengl3.cpp" "%BUILD_DIR%\imgui_impl_opengl3.obj"
if errorlevel 1 exit /b 1

call :run_nvcc_xcu "third_party\tinyfiledialogs\tinyfiledialogs.c" "%BUILD_DIR%\tinyfiledialogs.obj"
if errorlevel 1 exit /b 1

echo [build] common\volume_import.cpp
cl /nologo /EHsc /std:c++17 /O2 /Icommon /Fo"%BUILD_DIR%\volume_import.obj" /c "common\volume_import.cpp"
if errorlevel 1 exit /b 1

echo [build] third_party\glew-2.2.0\src\glew.c
cl /nologo /TP /EHsc /std:c++17 /O2 /D_CRT_SECURE_NO_WARNINGS /DGLEW_STATIC /DGLFW_HAS_PER_MONITOR_DPI=0 /DGLFW_HAS_GAMEPAD_API=0 /DGLFW_HAS_GETERROR=0 /Icommon /Iimgui /Iimgui\backends /Ithird_party\tinyfiledialogs /Ithird_party\glew-2.2.0\include /Iglfw\include /Fo"%BUILD_DIR%\glew.obj" /c "third_party\glew-2.2.0\src\glew.c"
if errorlevel 1 exit /b 1

echo [link] %STANDALONE_BIN%
"%CUDA_HOME%\bin\nvcc.exe" -rdc=true -cudart hybrid -arch=%CUDA_ARCH% "%BUILD_DIR%\renderer.obj" "%BUILD_DIR%\raymarch_kernel.obj" "%BUILD_DIR%\volume_textures.obj" "%BUILD_DIR%\standalone_main.obj" "%BUILD_DIR%\glew.obj" "%BUILD_DIR%\imgui.obj" "%BUILD_DIR%\imgui_draw.obj" "%BUILD_DIR%\imgui_tables.obj" "%BUILD_DIR%\imgui_widgets.obj" "%BUILD_DIR%\imgui_impl_glfw.obj" "%BUILD_DIR%\imgui_impl_opengl3.obj" "%BUILD_DIR%\tinyfiledialogs.obj" "%BUILD_DIR%\volume_import.obj" -o "%STANDALONE_BIN%" -Xcompiler="/MD" -Xlinker /NODEFAULTLIB:LIBCMT -Xlinker /DEFAULTLIB:MSVCRT opengl32.lib gdi32.lib user32.lib shell32.lib comdlg32.lib ole32.lib "%GLFW_LIB%"
if errorlevel 1 exit /b 1

echo.
echo Build complete:
echo   %ROOT%%STANDALONE_BIN%
exit /b 0

:resolve_cuda
if defined CUDA_PATH if exist "%CUDA_PATH%\bin\nvcc.exe" (
    set "CUDA_HOME=%CUDA_PATH%"
)

if not defined CUDA_HOME (
    for /d %%D in ("C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v*") do (
        if exist "%%~fD\bin\nvcc.exe" set "CUDA_HOME=%%~fD"
    )
)

if not defined CUDA_HOME (
    echo Could not find a CUDA toolkit installation.
    echo Install the NVIDIA CUDA Toolkit so that nvcc is available.
    exit /b 1
)
exit /b 0

:resolve_vs
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
    echo Install Visual Studio 2022 Community or Build Tools with Desktop development with C++.
    exit /b 1
)

set "VS_DEV_CMD=%VS_INSTALL%\Common7\Tools\VsDevCmd.bat"
if not exist "%VS_DEV_CMD%" (
    echo Could not find VsDevCmd.bat:
    echo   %VS_DEV_CMD%
    exit /b 1
)
exit /b 0

:detect_arch
for /f "usebackq delims=" %%I in (`powershell -NoProfile -Command "$cap = (& nvidia-smi --query-gpu=compute_cap --format=csv,noheader 2^>$null | Select-Object -First 1).Trim(); if ($cap -match '^[0-9]+\.[0-9]+$') { 'sm_' + $cap.Replace('.', '') }"`) do (
    if not defined CUDA_ARCH set "CUDA_ARCH=%%I"
)
exit /b 0

:run_nvcc
echo [build] %~1
"%CUDA_HOME%\bin\nvcc.exe" -rdc=true -cudart hybrid -arch=%CUDA_ARCH% -Icommon -Iimgui -Iimgui\backends -Ithird_party\tinyfiledialogs -Ithird_party\glew-2.2.0\include -Iglfw\include -DGLFW_HAS_PER_MONITOR_DPI=0 -DGLFW_HAS_GAMEPAD_API=0 -DGLFW_HAS_GETERROR=0 -Xcompiler="/W3 /EHsc /MD /DGLEW_STATIC /D_CRT_SECURE_NO_WARNINGS" -O2 -c "%~1" -o "%~2"
exit /b %ERRORLEVEL%

:run_nvcc_xcu
echo [build] %~1
"%CUDA_HOME%\bin\nvcc.exe" -rdc=true -cudart hybrid -arch=%CUDA_ARCH% -Icommon -Iimgui -Iimgui\backends -Ithird_party\tinyfiledialogs -Ithird_party\glew-2.2.0\include -Iglfw\include -DGLFW_HAS_PER_MONITOR_DPI=0 -DGLFW_HAS_GAMEPAD_API=0 -DGLFW_HAS_GETERROR=0 -Xcompiler="/W3 /EHsc /MD /DGLEW_STATIC /D_CRT_SECURE_NO_WARNINGS" -O2 -x cu -c "%~1" -o "%~2"
exit /b %ERRORLEVEL%
