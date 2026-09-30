@echo off
setlocal EnableExtensions
set "ROOT=%~dp0"
cd /d "%ROOT%"

if not exist build mkdir build
set "LOG=build\test_results.log"

set "OVERALL_FAILED=0"
set "STAGE1_RESULT=?"
set "STAGE2_RESULT=?"
set "STAGE3_RESULT=?"
set "STAGE4_RESULT=?"
set "STAGE5_RESULT=?"

echo VoxelPilot unified test run - %DATE% %TIME% > "%LOG%"

echo ============================================================
echo VoxelPilot unified test runner
echo ============================================================

REM ---- Stage 1: unit tests + structural regression tests ----
echo.
echo [1/5] Unit tests + structural regression tests (run_tests_windows.bat)
call run_tests_windows.bat
if errorlevel 1 (
    set "STAGE1_RESULT=FAIL"
    set "OVERALL_FAILED=1"
) else (
    set "STAGE1_RESULT=PASS"
)
echo [1/5] Unit + structural tests: %STAGE1_RESULT% >> "%LOG%"

REM ---- Stage 2: foot baseline render regression ----
echo.
echo [2/5] Foot baseline render regression (run_render_regression_windows.bat)
if not exist "volume_renderer_standalone.exe" (
    echo   SKIP: volume_renderer_standalone.exe not built yet. Run build_windows.ps1 first.
    set "STAGE2_RESULT=SKIP - exe not built"
) else if not exist "sample-data\foot_256x256x256_uint8.raw" (
    echo   SKIP: sample-data\foot_256x256x256_uint8.raw not present.
    set "STAGE2_RESULT=SKIP - dataset missing"
) else (
    call run_render_regression_windows.bat
    if errorlevel 1 (
        set "STAGE2_RESULT=FAIL"
        set "OVERALL_FAILED=1"
    ) else (
        set "STAGE2_RESULT=PASS"
    )
)
echo [2/5] Foot render regression: %STAGE2_RESULT% >> "%LOG%"

REM ---- Stage 3: VTK headsq CT sample ----
echo.
echo [3/5] VTK headsq CT sample (run_headsq_verification_windows.bat)
if not exist "volume_renderer_standalone.exe" (
    echo   SKIP: volume_renderer_standalone.exe not built yet.
    set "STAGE3_RESULT=SKIP - exe not built"
) else if not exist "sample-data\vtk-headsq\vtk_headsq_bone_64x64x93_uint8.raw" (
    echo   SKIP: sample-data\vtk-headsq dataset not present.
    set "STAGE3_RESULT=SKIP - dataset missing"
) else (
    call run_headsq_verification_windows.bat
    if errorlevel 1 (
        set "STAGE3_RESULT=FAIL"
        set "OVERALL_FAILED=1"
    ) else (
        set "STAGE3_RESULT=PASS"
    )
)
echo [3/5] VTK headsq verification: %STAGE3_RESULT% >> "%LOG%"

REM ---- Stage 4: Bonsai CT sample ----
echo.
echo [4/5] Bonsai CT sample (run_bonsai_verification_windows.bat)
if not exist "volume_renderer_standalone.exe" (
    echo   SKIP: volume_renderer_standalone.exe not built yet.
    set "STAGE4_RESULT=SKIP - exe not built"
) else if not exist "sample-data\bonsai_256x256x256_uint8.raw" (
    echo   SKIP: sample-data\bonsai_256x256x256_uint8.raw not present.
    set "STAGE4_RESULT=SKIP - dataset missing"
) else (
    call run_bonsai_verification_windows.bat
    if errorlevel 1 (
        set "STAGE4_RESULT=FAIL"
        set "OVERALL_FAILED=1"
    ) else (
        set "STAGE4_RESULT=PASS"
    )
)
echo [4/5] Bonsai verification: %STAGE4_RESULT% >> "%LOG%"

REM ---- Stage 5: Bonsai ASD-specific verification ----
echo.
echo [5/5] Bonsai ASD verification (run_bonsai_asd_verification_windows.bat)
if not exist "sample-data\bonsai_256x256x256_uint8.raw" (
    echo   SKIP: sample-data\bonsai_256x256x256_uint8.raw not present.
    set "STAGE5_RESULT=SKIP - dataset missing"
) else (
    call run_bonsai_asd_verification_windows.bat
    if errorlevel 1 (
        set "STAGE5_RESULT=FAIL"
        set "OVERALL_FAILED=1"
    ) else (
        set "STAGE5_RESULT=PASS"
    )
)
echo [5/5] Bonsai ASD verification: %STAGE5_RESULT% >> "%LOG%"

echo.
echo ============================================================
echo Summary
echo ============================================================
echo [1/5] Unit + structural tests ........ %STAGE1_RESULT%
echo [2/5] Foot render regression ......... %STAGE2_RESULT%
echo [3/5] VTK headsq verification ........ %STAGE3_RESULT%
echo [4/5] Bonsai verification ............ %STAGE4_RESULT%
echo [5/5] Bonsai ASD verification ........ %STAGE5_RESULT%
echo.
echo Full log written to %LOG%

if "%OVERALL_FAILED%"=="1" (
    echo RESULT: FAILED - see failures above.
    exit /b 1
)

echo RESULT: ALL PASSED OR SKIPPED.
exit /b 0
