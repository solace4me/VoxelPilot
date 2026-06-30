# Cinematic Lite Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an opt-in GPU-native Cinematic Lite render mode without changing VoxelPilot's default laptop-safe rendering path.

**Architecture:** Extend the existing render command and volume parameter structs with a small cinematic parameter set, clamp those values through a shared helper, and branch inside the CUDA raymarcher only when `cinematic_enabled` is non-zero. Wire the same state through the Controls UI, workspace presets, and snapshot CLI so Phase 0/default and cinematic snapshots can be tested separately.

**Tech Stack:** CUDA C/C++, ImGui, GLFW/OpenGL, Windows batch/PowerShell tests, Python stdlib PNG regression helper.

---

## File Structure

- Modify `common/volume_structs.h`: add cinematic fields to `VolumeParams`.
- Modify `common/renderer_api.h`: add matching cinematic fields to `RendererInput`.
- Modify `common/ui_state_helpers.h`: add `VoxelPilotCinematicParams` and clamp/default helper.
- Modify `renderer/renderer.cu`: copy cinematic fields into device params and initialize disabled defaults.
- Modify `renderer/raymarch_kernel.cu`: add guarded rim/glow/contrast math when cinematic is enabled.
- Modify `standalone_main.cu`: add app state, UI controls, workspace persistence, CLI flags, status text, and render command wiring.
- Modify `tests/volume_import_tests.cpp`: add helper tests for safe defaults and clamping.
- Modify `tools/verify_render_snapshot.py`: allow cinematic snapshot comparison metrics if needed.
- Modify `README.md`: document Cinematic Lite and CLI snapshot flags.

## Task 1: Shared Cinematic Parameter Helper

**Files:**
- Modify: `common/ui_state_helpers.h`
- Modify: `tests/volume_import_tests.cpp`

- [ ] **Step 1: Write failing tests**

Add tests named `test_cinematic_params_default_disabled` and `test_cinematic_params_clamp_safe_ranges` to `tests/volume_import_tests.cpp`:

```cpp
static int test_cinematic_params_default_disabled(void)
{
    VoxelPilotCinematicParams params;
    voxelpilot_cinematic_defaults(&params);

    if (!expect_true(params.enabled == 0, "cinematic mode should be disabled by default")) return 0;
    if (!expect_float_close(params.rim_strength, 0.35f, "cinematic default rim strength")) return 0;
    if (!expect_float_close(params.glow_strength, 0.20f, "cinematic default glow strength")) return 0;
    return expect_float_close(params.contrast, 1.10f, "cinematic default contrast");
}

static int test_cinematic_params_clamp_safe_ranges(void)
{
    VoxelPilotCinematicParams params;
    params.enabled = 9;
    params.rim_strength = -2.0f;
    params.glow_strength = 9.0f;
    params.contrast = 3.0f;
    voxelpilot_cinematic_clamp(&params);

    if (!expect_true(params.enabled == 1, "cinematic enabled should clamp to one")) return 0;
    if (!expect_float_close(params.rim_strength, 0.0f, "cinematic rim should clamp low")) return 0;
    if (!expect_float_close(params.glow_strength, 1.0f, "cinematic glow should clamp high")) return 0;
    return expect_float_close(params.contrast, 1.6f, "cinematic contrast should clamp high");
}
```

Call both tests from `main()`.

- [ ] **Step 2: Verify red**

Run: `.
un_tests_windows.bat`

Expected: compile failure naming `VoxelPilotCinematicParams` or `voxelpilot_cinematic_defaults` as undefined.

- [ ] **Step 3: Implement helper**

Add to `common/ui_state_helpers.h`:

```c
typedef struct {
    int   enabled;
    float rim_strength;
    float glow_strength;
    float contrast;
} VoxelPilotCinematicParams;

static inline float voxelpilot_clampf(float value, float min_value, float max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static inline void voxelpilot_cinematic_defaults(VoxelPilotCinematicParams *params)
{
    if (!params) return;
    params->enabled = 0;
    params->rim_strength = 0.35f;
    params->glow_strength = 0.20f;
    params->contrast = 1.10f;
}

static inline void voxelpilot_cinematic_clamp(VoxelPilotCinematicParams *params)
{
    if (!params) return;
    params->enabled = params->enabled ? 1 : 0;
    params->rim_strength = voxelpilot_clampf(params->rim_strength, 0.0f, 1.0f);
    params->glow_strength = voxelpilot_clampf(params->glow_strength, 0.0f, 1.0f);
    params->contrast = voxelpilot_clampf(params->contrast, 0.8f, 1.6f);
}
```

- [ ] **Step 4: Verify green**

Run: `.
un_tests_windows.bat`

Expected: `volume_import_tests: PASS` and Python render regression unit tests pass.

## Task 2: Renderer API And State Wiring

**Files:**
- Modify: `common/volume_structs.h`
- Modify: `common/renderer_api.h`
- Modify: `renderer/renderer.cu`
- Modify: `standalone_main.cu`

- [ ] **Step 1: Add fields with disabled defaults**

Add these fields to `VolumeParams` and `RendererInput`:

```c
int cinematic_enabled;
float cinematic_rim_strength;
float cinematic_glow_strength;
float cinematic_contrast;
```

Initialize them to disabled defaults in `renderer_state_init`, `StandaloneApp` startup, `reset_workspace`, and any render-command creation.

- [ ] **Step 2: Copy fields to renderer state**

In `apply_render_command`, copy each cinematic field from `RendererInput` into `st->vparams`.

- [ ] **Step 3: Wire snapshot and interactive commands**

In `render_frame_gpu_snapshot` and interactive render command creation, assign cinematic fields from `StandaloneApp`.

- [ ] **Step 4: Verify build-level green**

Run: `.
un_tests_windows.bat`

Expected: all tests pass; no compile errors from struct changes.

## Task 3: UI, Presets, And CLI

**Files:**
- Modify: `standalone_main.cu`
- Modify: `README.md`

- [ ] **Step 1: Add UI controls**

In the Controls panel under Lighting, add:

```cpp
bool cinematic_enabled = (app->cinematic_enabled != 0);
ImGui::Checkbox("Cinematic Mode", &cinematic_enabled);
app->cinematic_enabled = cinematic_enabled ? 1 : 0;
ImGui::SliderFloat("Cinematic Rim", &app->cinematic_rim_strength, 0.0f, 1.0f);
ImGui::SliderFloat("Cinematic Glow", &app->cinematic_glow_strength, 0.0f, 1.0f);
ImGui::SliderFloat("Cinematic Contrast", &app->cinematic_contrast, 0.8f, 1.6f);
clamp_cinematic_settings(app);
```

Only show sliders when enabled.

- [ ] **Step 2: Persist state**

Save/load these workspace keys:

```txt
cinematic_enabled
cinematic_rim_strength
cinematic_glow_strength
cinematic_contrast
```

Clamp after loading.

- [ ] **Step 3: Add CLI flags**

Parse:

```txt
--cinematic-lite
--cinematic-rim VALUE
--cinematic-glow VALUE
--cinematic-contrast VALUE
```

Clamp values before rendering.

- [ ] **Step 4: Add status text**

Append `Cinematic: On/Off` to the bottom status bar.

- [ ] **Step 5: Document usage**

Add README commands for default and cinematic snapshots.

## Task 4: CUDA Cinematic Branch

**Files:**
- Modify: `renderer/raymarch_kernel.cu`
- Modify: `common/math_utils.h` if a small clamp helper is needed

- [ ] **Step 1: Add guarded per-sample effect**

When `vparams.cinematic_enabled != 0`, compute rim intensity from `1 - abs(dot(grad, V))`, add bounded rim color, add glow proportional to sample alpha, and slightly warm high-density material.

- [ ] **Step 2: Add guarded final contrast**

Before writing RGBA output, apply a bounded contrast curve only when cinematic is enabled.

- [ ] **Step 3: Verify default baseline still passes**

Run: `.
un_render_regression_windows.bat`

Expected: foot baseline passes with default cinematic off.

## Task 5: Cinematic Snapshot Verification

**Files:**
- Modify: `tools/verify_render_snapshot.py` only if needed

- [ ] **Step 1: Generate default and cinematic snapshots**

Run:

```powershell
.\volume_renderer_standalone.exe --volume .\sample-data\foot_256x256x256_uint8.raw --width 256 --height 256 --depth 256 --type uint8 --snapshot .\build\foot_default_phase1.png
.\volume_renderer_standalone.exe --volume .\sample-data\foot_256x256x256_uint8.raw --width 256 --height 256 --depth 256 --type uint8 --snapshot .\build\foot_cinematic_phase1.png --cinematic-lite
```

- [ ] **Step 2: Compare metrics**

Use `tools/verify_render_snapshot.py --no-render` for both images and confirm both are visible. Cinematic render time target is below 1.5x the default baseline from this machine.

- [ ] **Step 3: Final verification**

Run:

```powershell
.\run_tests_windows.bat
.\build_windows.ps1
.\run_render_regression_windows.bat
```

Expected: all commands exit 0. Existing third-party warnings are allowed; new compile errors or render-regression failures are not.

## Self-Review

- Spec coverage: UI toggle, disabled default, CUDA branch, CLI snapshot flags, workspace persistence, and default regression protection are covered.
- No incomplete markers are present.
- Type names are consistent: `VoxelPilotCinematicParams`, `cinematic_enabled`, `cinematic_rim_strength`, `cinematic_glow_strength`, and `cinematic_contrast`.
- Commits are intentionally omitted because this user has not requested git commits.
