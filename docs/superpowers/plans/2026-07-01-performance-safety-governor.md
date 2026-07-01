# Performance Safety Governor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a local advisory Performance Safety Governor that protects laptop responsiveness while preserving VoxelPilot's CUDA-first rendering quality.

**Architecture:** Add a focused CPU-side governor helper that evaluates volume size, estimated memory pressure, render-time moving averages, label/callout pressure, and optional CUDA memory telemetry. Integrate it into `standalone_main.cu` as an advisory AI Assist/status UI block with explicit user-clicked actions only; the CUDA render path remains unchanged.

**Tech Stack:** C/C++ header-only helpers, CUDA runtime telemetry through the existing CUDA build, ImGui UI, current Windows batch verification scripts, and `tests/volume_import_tests.cpp`.

---

## Scope Guard

This implements `docs/superpowers/specs/2026-07-01-performance-safety-governor-design.md`. Do not rewrite the CUDA raymarcher, silently reduce quality, add neural/cloud inference, or remove existing ASD/cinematic/review-bundle behavior. Preserve the existing uncommitted local edit in `common/ai_features.h`; add a new helper header instead of editing prompt parsing.

## File Structure

- Create: `common/performance_governor.h` — pure CPU scoring, memory estimation, status labels, moving-average helper, and recommendation text.
- Modify: `tests/volume_import_tests.cpp` — include the helper and add focused tests for scoring and recommendations.
- Modify: `standalone_main.cu` — add app state, moving average, CUDA memory telemetry, compact AI Assist UI, status-bar label, and UI-only optional-action guards.
- Modify: `README.md` — document the local GPU-aware safety layer.

---

### Task 1: Add Pure Governor Helper With Tests

**Files:**
- Create: `common/performance_governor.h`
- Modify: `tests/volume_import_tests.cpp`

- [ ] **Step 1: Add the include**

Modify `tests/volume_import_tests.cpp` near the other common includes:

```cpp
#include "../common/performance_governor.h"
```

- [ ] **Step 2: Write failing tests**

Add these tests after `test_brick_cache_plan_reports_queue_and_eviction_pressure`:

```cpp
static int test_performance_governor_small_volume_is_healthy(void)
{
    VoxelPilotPerformanceInputs inputs;
    VoxelPilotPerformanceResult result;
    voxelpilot_performance_inputs_defaults(&inputs);
    inputs.volume_width = 256;
    inputs.volume_height = 256;
    inputs.volume_depth = 256;
    inputs.render_width = 1024;
    inputs.render_height = 1024;
    inputs.estimated_volume_bytes = 256u * 256u * 256u * sizeof(float);
    inputs.estimated_gradient_bytes = inputs.estimated_volume_bytes * 3u;
    inputs.estimated_label_mask_bytes = 256u * 256u * 256u;
    inputs.gpu_memory_available = 4ull * 1024ull * 1024ull * 1024ull;
    inputs.gpu_memory_total = 6ull * 1024ull * 1024ull * 1024ull;
    inputs.has_gpu_memory_telemetry = 1;
    inputs.render_ms_average = 35.0f;
    inputs.render_quality_mode = VOXELPILOT_RENDER_QUALITY_BALANCED;
    if (!expect_true(voxelpilot_evaluate_performance(&inputs, &result) == 1,
            "performance governor should evaluate healthy inputs")) return 0;
    if (!expect_true(result.status == VOXELPILOT_GOVERNOR_HEALTHY,
            "small laptop-friendly volume should be healthy")) return 0;
    if (!expect_true(result.safe_to_run_asd == 1,
            "healthy pressure should allow ASD")) return 0;
    return expect_true(result.suggest_interactive_gpu == 0,
        "healthy pressure should not suggest interactive GPU");
}

static int test_performance_governor_high_render_time_suggests_interactive(void)
{
    VoxelPilotPerformanceInputs inputs;
    VoxelPilotPerformanceResult result;
    voxelpilot_performance_inputs_defaults(&inputs);
    inputs.volume_width = 256;
    inputs.volume_height = 256;
    inputs.volume_depth = 256;
    inputs.render_width = 1920;
    inputs.render_height = 1080;
    inputs.estimated_volume_bytes = 256u * 256u * 256u * sizeof(float);
    inputs.estimated_gradient_bytes = inputs.estimated_volume_bytes * 3u;
    inputs.gpu_memory_available = 4ull * 1024ull * 1024ull * 1024ull;
    inputs.gpu_memory_total = 6ull * 1024ull * 1024ull * 1024ull;
    inputs.has_gpu_memory_telemetry = 1;
    inputs.render_ms_average = 155.0f;
    inputs.render_quality_mode = VOXELPILOT_RENDER_QUALITY_QUALITY;
    inputs.cinematic_enabled = 1;
    inputs.label_overlay_enabled = 1;
    if (!expect_true(voxelpilot_evaluate_performance(&inputs, &result) == 1,
            "performance governor should evaluate high render time inputs")) return 0;
    if (!expect_true(result.status >= VOXELPILOT_GOVERNOR_WATCH,
            "high render time should raise at least watch status")) return 0;
    if (!expect_true(result.suggest_interactive_gpu == 1,
            "high render time should suggest interactive GPU")) return 0;
    return expect_true(strstr(result.recommendation, "Interactive GPU") != NULL,
        "recommendation should name the interactive GPU action");
}
```

```cpp
static int test_performance_governor_memory_pressure_suggests_streaming(void)
{
    VoxelPilotPerformanceInputs inputs;
    VoxelPilotPerformanceResult result;
    voxelpilot_performance_inputs_defaults(&inputs);
    inputs.volume_width = 1024;
    inputs.volume_height = 1024;
    inputs.volume_depth = 768;
    inputs.render_width = 1920;
    inputs.render_height = 1080;
    inputs.estimated_volume_bytes = 1024ull * 1024ull * 768ull * sizeof(float);
    inputs.estimated_gradient_bytes = inputs.estimated_volume_bytes * 3ull;
    inputs.estimated_label_mask_bytes = 1024ull * 1024ull * 768ull;
    inputs.gpu_memory_available = 4ull * 1024ull * 1024ull * 1024ull;
    inputs.gpu_memory_total = 6ull * 1024ull * 1024ull * 1024ull;
    inputs.has_gpu_memory_telemetry = 1;
    inputs.render_ms_average = 95.0f;
    inputs.render_quality_mode = VOXELPILOT_RENDER_QUALITY_BALANCED;
    if (!expect_true(voxelpilot_evaluate_performance(&inputs, &result) == 1,
            "performance governor should evaluate high memory inputs")) return 0;
    if (!expect_true(result.status >= VOXELPILOT_GOVERNOR_PRESSURE,
            "large working set should raise pressure status")) return 0;
    if (!expect_true(result.suggest_brick_streaming == 1,
            "large working set should suggest brick streaming path")) return 0;
    return expect_true(result.safe_to_export_mask == 0,
        "critical memory pressure should not recommend full mask export");
}

static int test_performance_governor_callout_pressure_suggests_limit(void)
{
    VoxelPilotPerformanceInputs inputs;
    VoxelPilotPerformanceResult result;
    voxelpilot_performance_inputs_defaults(&inputs);
    inputs.volume_width = 512;
    inputs.volume_height = 512;
    inputs.volume_depth = 512;
    inputs.render_width = 1280;
    inputs.render_height = 720;
    inputs.estimated_volume_bytes = 512ull * 512ull * 512ull * sizeof(float);
    inputs.estimated_gradient_bytes = inputs.estimated_volume_bytes * 3ull;
    inputs.estimated_label_mask_bytes = 512ull * 512ull * 512ull;
    inputs.render_ms_average = 118.0f;
    inputs.render_quality_mode = VOXELPILOT_RENDER_QUALITY_BALANCED;
    inputs.label_overlay_enabled = 1;
    inputs.label_mask_present = 1;
    inputs.semantic_callouts_enabled = 1;
    inputs.semantic_callout_top_regions = 6;
    inputs.semantic_region_count = 6;
    if (!expect_true(voxelpilot_evaluate_performance(&inputs, &result) == 1,
            "performance governor should evaluate callout-heavy inputs")) return 0;
    if (!expect_true(result.suggest_reduce_callouts == 1,
            "callout-heavy pressure should suggest limiting callouts")) return 0;
    return expect_true(strstr(result.recommendation, "callouts") != NULL,
        "recommendation should explain callout reduction");
}

static int test_performance_governor_moving_average_is_stable(void)
{
    float average = voxelpilot_update_render_ms_average(0.0f, 100.0f, 0);
    if (!expect_float_close(average, 100.0f,
            "first render sample should initialize moving average")) return 0;
    average = voxelpilot_update_render_ms_average(average, 200.0f, 1);
    return expect_float_close(average, 115.0f,
        "subsequent render sample should use stable 15 percent smoothing");
}
```

- [ ] **Step 3: Register the tests**

Add these lines in `main()` after the existing streaming tests:

```cpp
    passed &= test_performance_governor_small_volume_is_healthy();
    passed &= test_performance_governor_high_render_time_suggests_interactive();
    passed &= test_performance_governor_memory_pressure_suggests_streaming();
    passed &= test_performance_governor_callout_pressure_suggests_limit();
    passed &= test_performance_governor_moving_average_is_stable();
```

- [ ] **Step 4: Run tests and verify red**

Run: `./run_tests_windows.bat`

Expected: compile failure for missing `common/performance_governor.h` or missing governor symbols.

- [ ] **Step 5: Create `common/performance_governor.h`**

Create the header with this content:

```c
#ifndef PERFORMANCE_GOVERNOR_H
#define PERFORMANCE_GOVERNOR_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "ai_features.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VOXELPILOT_GOVERNOR_TEXT_MAX 256
#define VOXELPILOT_GOVERNOR_HEALTHY  0
#define VOXELPILOT_GOVERNOR_WATCH    1
#define VOXELPILOT_GOVERNOR_PRESSURE 2
#define VOXELPILOT_GOVERNOR_CRITICAL 3

typedef struct {
    int volume_width, volume_height, volume_depth;
    int render_width, render_height;
    size_t estimated_volume_bytes;
    size_t estimated_gradient_bytes;
    size_t estimated_label_mask_bytes;
    size_t estimated_render_target_bytes;
    size_t gpu_memory_available;
    size_t gpu_memory_total;
    int has_gpu_memory_telemetry;
    float render_ms_last;
    float render_ms_average;
    int render_quality_mode;
    int cinematic_enabled;
    int label_overlay_enabled;
    int label_mask_present;
    int semantic_callouts_enabled;
    int semantic_callout_top_regions;
    int semantic_region_count;
} VoxelPilotPerformanceInputs;

typedef struct {
    int status;
    int pressure_score;
    float memory_budget_ratio;
    size_t estimated_working_set_bytes;
    int safe_to_run_asd;
    int safe_to_export_mask;
    int suggest_interactive_gpu;
    int suggest_reduce_callouts;
    int suggest_brick_streaming;
    char status_label[32];
    char primary_reason[VOXELPILOT_GOVERNOR_TEXT_MAX];
    char recommendation[VOXELPILOT_GOVERNOR_TEXT_MAX];
} VoxelPilotPerformanceResult;

static inline const char *voxelpilot_governor_status_label(int status)
{
    if (status == VOXELPILOT_GOVERNOR_CRITICAL) return "Critical";
    if (status == VOXELPILOT_GOVERNOR_PRESSURE) return "Pressure";
    if (status == VOXELPILOT_GOVERNOR_WATCH) return "Watch";
    return "Healthy";
}

static inline size_t voxelpilot_safe_size_add(size_t a, size_t b)
{
    if (b > (size_t)-1 - a) return (size_t)-1;
    return a + b;
}

static inline size_t voxelpilot_estimate_render_target_bytes(int width, int height)
{
    if (width < 1 || height < 1) return 0;
    return (size_t)width * (size_t)height * 4u;
}

static inline float voxelpilot_update_render_ms_average(float previous_average, float latest_ms, int has_previous_sample)
{
    if (latest_ms < 0.0f) latest_ms = 0.0f;
    if (!has_previous_sample || previous_average <= 0.0f) return latest_ms;
    return previous_average * 0.85f + latest_ms * 0.15f;
}
```

```c
static inline void voxelpilot_performance_inputs_defaults(VoxelPilotPerformanceInputs *inputs)
{
    if (!inputs) return;
    memset(inputs, 0, sizeof(*inputs));
    inputs->render_quality_mode = VOXELPILOT_RENDER_QUALITY_BALANCED;
}

static inline void voxelpilot_performance_result_defaults(VoxelPilotPerformanceResult *result)
{
    if (!result) return;
    memset(result, 0, sizeof(*result));
    result->status = VOXELPILOT_GOVERNOR_HEALTHY;
    result->safe_to_run_asd = 1;
    result->safe_to_export_mask = 1;
    snprintf(result->status_label, sizeof(result->status_label), "Healthy");
    snprintf(result->primary_reason, sizeof(result->primary_reason),
             "Performance pressure is within the laptop-safe range.");
    snprintf(result->recommendation, sizeof(result->recommendation),
             "Keep the current GPU settings.");
}

static inline int voxelpilot_evaluate_performance(const VoxelPilotPerformanceInputs *inputs, VoxelPilotPerformanceResult *result)
{
    size_t working_set;
    float memory_ratio = 0.0f;
    int score = 0;
    if (!inputs || !result) return 0;
    voxelpilot_performance_result_defaults(result);

    if (inputs->volume_width < 1 || inputs->volume_height < 1 || inputs->volume_depth < 1) {
        result->status = VOXELPILOT_GOVERNOR_WATCH;
        result->pressure_score = 15;
        snprintf(result->status_label, sizeof(result->status_label), "Watch");
        snprintf(result->primary_reason, sizeof(result->primary_reason),
                 "Load a volume to estimate performance safety.");
        snprintf(result->recommendation, sizeof(result->recommendation),
                 "Import a dataset before running AI safety checks.");
        return 1;
    }

    working_set = voxelpilot_safe_size_add(inputs->estimated_volume_bytes, inputs->estimated_gradient_bytes);
    working_set = voxelpilot_safe_size_add(working_set, inputs->estimated_label_mask_bytes);
    working_set = voxelpilot_safe_size_add(working_set, inputs->estimated_render_target_bytes);
    if (working_set == 0) {
        working_set = (size_t)inputs->volume_width * (size_t)inputs->volume_height * (size_t)inputs->volume_depth * 5u;
    }
    result->estimated_working_set_bytes = working_set;

    if (inputs->has_gpu_memory_telemetry && inputs->gpu_memory_available > 0) {
        memory_ratio = (float)((double)working_set / (double)inputs->gpu_memory_available);
    } else {
        memory_ratio = (float)((double)working_set / (double)(4ull * 1024ull * 1024ull * 1024ull));
    }
    result->memory_budget_ratio = memory_ratio;

    if (memory_ratio > 0.90f) score += 60;
    else if (memory_ratio > 0.70f) score += 40;
    else if (memory_ratio > 0.50f) score += 25;

    if (inputs->render_ms_average > 140.0f) score += 35;
    else if (inputs->render_ms_average > 90.0f) score += 22;
    else if (inputs->render_ms_average > 55.0f) score += 12;

    if (inputs->cinematic_enabled && inputs->render_ms_average > 90.0f) score += 6;
    if (inputs->label_overlay_enabled && inputs->label_mask_present && inputs->render_ms_average > 90.0f) score += 6;
    if (inputs->semantic_callouts_enabled && inputs->semantic_callout_top_regions > 3 &&
        inputs->semantic_region_count >= inputs->semantic_callout_top_regions && inputs->render_ms_average > 80.0f) {
        score += 10;
        result->suggest_reduce_callouts = 1;
    }
    if (score > 100) score = 100;
    result->pressure_score = score;

    result->suggest_interactive_gpu = inputs->render_quality_mode != VOXELPILOT_RENDER_QUALITY_INTERACTIVE && inputs->render_ms_average > 90.0f;
    result->suggest_brick_streaming = memory_ratio > 0.70f;
    result->safe_to_run_asd = memory_ratio < 0.90f;
    result->safe_to_export_mask = memory_ratio < 0.85f;

    if (score >= 75) result->status = VOXELPILOT_GOVERNOR_CRITICAL;
    else if (score >= 45) result->status = VOXELPILOT_GOVERNOR_PRESSURE;
    else if (score >= 18) result->status = VOXELPILOT_GOVERNOR_WATCH;
    else result->status = VOXELPILOT_GOVERNOR_HEALTHY;
    snprintf(result->status_label, sizeof(result->status_label), "%s", voxelpilot_governor_status_label(result->status));

    if (result->suggest_reduce_callouts) {
        snprintf(result->primary_reason, sizeof(result->primary_reason),
                 "Semantic labels and many callouts are active while render time is elevated.");
        snprintf(result->recommendation, sizeof(result->recommendation),
                 "Limit callouts to one highlighted region while navigating.");
    } else if (result->suggest_brick_streaming) {
        snprintf(result->primary_reason, sizeof(result->primary_reason),
                 "Estimated working set is %.0f%% of the available GPU memory budget.", memory_ratio * 100.0f);
        snprintf(result->recommendation, sizeof(result->recommendation),
                 "Use a brick-streaming path for this dataset or keep optional AI passes limited.");
    } else if (result->suggest_interactive_gpu) {
        snprintf(result->primary_reason, sizeof(result->primary_reason),
                 "Render average is %.1f ms with the current GPU quality settings.", inputs->render_ms_average);
        snprintf(result->recommendation, sizeof(result->recommendation),
                 "Use Interactive GPU while orbiting, then return to Balanced or Quality for screenshots.");
    } else if (result->status == VOXELPILOT_GOVERNOR_WATCH) {
        snprintf(result->primary_reason, sizeof(result->primary_reason),
                 "Render or memory pressure is rising but still usable.");
        snprintf(result->recommendation, sizeof(result->recommendation),
                 "Keep current quality, but avoid starting extra AI passes while orbiting.");
    }
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif
```

- [ ] **Step 6: Run tests and verify green**

Run: `./run_tests_windows.bat`

Expected: `volume_import_tests: PASS` and Python render regression tests pass.

- [ ] **Step 7: Commit Task 1**

```powershell
git add common/performance_governor.h tests/volume_import_tests.cpp
git commit -m "feat: add performance safety governor core"
```

---

### Task 2: Integrate Governor State Without Changing Render Output

**Files:**
- Modify: `standalone_main.cu`

- [ ] **Step 1: Include helper and app fields**

Add near other includes:

```cpp
#include "common/performance_governor.h"
```

Add after `frame_id` in `StandaloneApp`:

```cpp
    float         render_ms_average;
    int           render_ms_average_ready;
    VoxelPilotPerformanceResult performance_result;
    char          performance_status[512];
```

Add prototypes near `update_streaming_estimate`:

```cpp
static void update_render_performance_average(StandaloneApp *app);
static void refresh_performance_governor(StandaloneApp *app);
```

- [ ] **Step 2: Initialize governor state**

In `reset_workspace`, after report status/path setup, add:

```cpp
    app->render_ms_average = 0.0f;
    app->render_ms_average_ready = 0;
    voxelpilot_performance_result_defaults(&app->performance_result);
    snprintf(app->performance_status, sizeof(app->performance_status),
             "Performance Safety: waiting for render telemetry.");
```

In `main`, after `app.report_status` / `app.report_path` initialization, add:

```cpp
    app.render_ms_average = 0.0f;
    app.render_ms_average_ready = 0;
    voxelpilot_performance_result_defaults(&app.performance_result);
    strcpy(app.performance_status, "Performance Safety: waiting for render telemetry.");
```

- [ ] **Step 3: Add app-side telemetry helpers**

Add before `update_streaming_estimate`:

```cpp
static void update_render_performance_average(StandaloneApp *app)
{
    if (!app) return;
    app->render_ms_average = voxelpilot_update_render_ms_average(
        app->render_ms_average,
        app->last_render_ms,
        app->render_ms_average_ready);
    app->render_ms_average_ready = 1;
}

static size_t estimate_current_volume_bytes(const StandaloneApp *app)
{
    if (!app || app->renderer.W < 1 || app->renderer.H < 1 || app->renderer.D < 1) return 0;
    return (size_t)app->renderer.W * (size_t)app->renderer.H * (size_t)app->renderer.D * sizeof(float);
}

static size_t estimate_current_label_mask_bytes(const StandaloneApp *app)
{
    if (!app || !app->label_mask || app->label_mask_W < 1 || app->label_mask_H < 1 || app->label_mask_D < 1) return 0;
    return (size_t)app->label_mask_W * (size_t)app->label_mask_H * (size_t)app->label_mask_D;
}

static void refresh_performance_governor(StandaloneApp *app)
{
    VoxelPilotPerformanceInputs inputs;
    size_t free_bytes = 0;
    size_t total_bytes = 0;
    cudaError_t telemetry_status;
    if (!app) return;
    voxelpilot_performance_inputs_defaults(&inputs);
    inputs.volume_width = app->renderer.W;
    inputs.volume_height = app->renderer.H;
    inputs.volume_depth = app->renderer.D;
    inputs.render_width = app->render_w;
    inputs.render_height = app->render_h;
    inputs.estimated_volume_bytes = estimate_current_volume_bytes(app);
    inputs.estimated_gradient_bytes = inputs.estimated_volume_bytes * 3u;
    inputs.estimated_label_mask_bytes = estimate_current_label_mask_bytes(app);
    inputs.estimated_render_target_bytes = voxelpilot_estimate_render_target_bytes(app->render_w, app->render_h);
    inputs.render_ms_last = app->last_render_ms;
    inputs.render_ms_average = app->render_ms_average_ready ? app->render_ms_average : app->last_render_ms;
    inputs.render_quality_mode = app->render_quality_mode;
    inputs.cinematic_enabled = app->cinematic_enabled;
    inputs.label_overlay_enabled = app->label_overlay_visible;
    inputs.label_mask_present = app->label_mask != NULL;
    inputs.semantic_callouts_enabled = app->semantic_callouts_enabled;
    inputs.semantic_callout_top_regions = app->semantic_callout_top_regions;
    inputs.semantic_region_count = app->semantic_region_count;
    telemetry_status = cudaMemGetInfo(&free_bytes, &total_bytes);
    if (telemetry_status == cudaSuccess && free_bytes > 0 && total_bytes > 0) {
        inputs.has_gpu_memory_telemetry = 1;
        inputs.gpu_memory_available = free_bytes;
        inputs.gpu_memory_total = total_bytes;
    }
    if (!voxelpilot_evaluate_performance(&inputs, &app->performance_result)) {
        voxelpilot_performance_result_defaults(&app->performance_result);
        snprintf(app->performance_status, sizeof(app->performance_status),
                 "Performance Safety: telemetry unavailable.");
        return;
    }
    snprintf(app->performance_status, sizeof(app->performance_status),
             "Performance Safety: %s (%d/100). %s",
             app->performance_result.status_label,
             app->performance_result.pressure_score,
             app->performance_result.recommendation);
}
```

- [ ] **Step 4: Update average after each render**

In both `render_frame_gpu_snapshot` and `render_frame_standalone`, immediately after `app->last_render_ms = render_frame_gpu(&app->renderer);`, add:

```cpp
    update_render_performance_average(app);
```

Do not change `RendererInput`, CUDA kernels, transfer functions, or label overlay parameters.

- [ ] **Step 5: Verify and commit Task 2**

Run:

```powershell
.\run_tests_windows.bat
.\build_windows.ps1
```

Expected: tests pass and build completes.

Commit:

```powershell
git add standalone_main.cu
git commit -m "feat: integrate performance safety telemetry"
```

---

### Task 3: Add Advisory UI And Explicit Safe Actions

**Files:**
- Modify: `standalone_main.cu`

- [ ] **Step 1: Add UI helper**

Add before `draw_ai_assist_panel`:

```cpp
static void draw_performance_safety_block(StandaloneApp *app)
{
    if (!app) return;
    refresh_performance_governor(app);
    ImGui::Separator();
    ImGui::Text("Performance Safety");
    ImGui::Text("Status: %s | Score: %d/100", app->performance_result.status_label, app->performance_result.pressure_score);
    ImGui::TextWrapped("%s", app->performance_result.primary_reason);
    ImGui::TextWrapped("Recommendation: %s", app->performance_result.recommendation);

    if (app->performance_result.suggest_interactive_gpu && app->render_quality_mode != VOXELPILOT_RENDER_QUALITY_INTERACTIVE) {
        if (ImGui::Button("Use Interactive GPU")) {
            apply_render_quality_preset(app, VOXELPILOT_RENDER_QUALITY_INTERACTIVE);
            snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                     "Performance Safety applied Interactive GPU. Return to Balanced GPU for screenshots when stable.");
            refresh_performance_governor(app);
        }
        ImGui::SameLine();
    }
    if (app->render_quality_mode == VOXELPILOT_RENDER_QUALITY_INTERACTIVE) {
        if (ImGui::Button("Return to Balanced GPU")) {
            apply_render_quality_preset(app, VOXELPILOT_RENDER_QUALITY_BALANCED);
            snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                     "Performance Safety returned rendering to Balanced GPU.");
            refresh_performance_governor(app);
        }
    }
    if (app->performance_result.suggest_reduce_callouts && app->semantic_callouts_enabled && app->semantic_callout_top_regions > 1) {
        if (ImGui::Button("Limit Callouts to 1")) {
            app->semantic_callout_top_regions = 1;
            snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                     "Performance Safety limited cinematic callouts to one highlighted region.");
            refresh_performance_governor(app);
        }
    }
    if (ImGui::Button("Refresh Safety Check")) {
        refresh_performance_governor(app);
        snprintf(app->ai_assist_status, sizeof(app->ai_assist_status), "%s", app->performance_status);
    }
}
```

- [ ] **Step 2: Wire UI placement**

Inside `draw_ai_assist_panel`, after the dataset/intensity text block and before `Auto Object Identification`, add:

```cpp
    draw_performance_safety_block(app);
```

In `draw_status_bar`, after the Cinematic text, add:

```cpp
    ImGui::SameLine();
    ImGui::Text("| Safety: %s", app->performance_result.status_label);
```

- [ ] **Step 3: Verify and commit Task 3**

Run:

```powershell
.\build_windows.ps1
.\volume_renderer_standalone.exe --volume .\sample-data\bonsai_256x256x256_uint8.raw --width 256 --height 256 --depth 256 --type uint8 --snapshot .\build\governor_bonsai_snapshot.png --semantic-demo
```

Expected: build succeeds and snapshot saves visibly.

Commit:

```powershell
git add standalone_main.cu
git commit -m "feat: add performance safety UI"
```

---

### Task 4: Gate Optional Heavy UI Actions With Explanations

**Files:**
- Modify: `standalone_main.cu`

- [ ] **Step 1: Add optional-action guard**

Add before `draw_ai_assist_panel`:

```cpp
static int performance_allows_optional_action(StandaloneApp *app, const char *action_label, int requires_asd, int requires_mask_export)
{
    if (!app) return 0;
    refresh_performance_governor(app);
    if (requires_asd && !app->performance_result.safe_to_run_asd) {
        snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                 "%s paused by Performance Safety: %s",
                 action_label,
                 app->performance_result.recommendation);
        return 0;
    }
    if (requires_mask_export && !app->performance_result.safe_to_export_mask) {
        snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                 "%s paused by Performance Safety: full label-mask export is not recommended for this memory pressure. Use metadata/export snapshot first.",
                 action_label);
        return 0;
    }
    return 1;
}
```

- [ ] **Step 2: Gate UI semantic actions only**

Change `Run Semantic Demo` to:

```cpp
    if (ImGui::Button("Run Semantic Demo")) {
        if (performance_allows_optional_action(app, "Semantic demo", 1, 0)) {
            run_semantic_demo_workflow(app, "Semantic demo");
            refresh_performance_governor(app);
        }
    }
```

Change `Discover Regions` to:

```cpp
    if (ImGui::Button("Discover Regions")) {
        if (performance_allows_optional_action(app, "ASD discovery", 1, 0)) {
            run_adaptive_semantic_discovery(app);
            refresh_performance_governor(app);
        }
    }
```

Change `Export AI Review Bundle` to:

```cpp
    if (ImGui::Button("Export AI Review Bundle")) {
        if (performance_allows_optional_action(app, "AI review bundle", 1, 1)) {
            export_ai_review_bundle(app);
            refresh_performance_governor(app);
        }
    }
```

Do not gate the `--review-bundle` CLI path; scripted proof must remain deterministic.

- [ ] **Step 3: Verify and commit Task 4**

Run:

```powershell
.\run_tests_windows.bat
.\build_windows.ps1
```

Expected: tests and build pass; UI-only guards do not affect CLI snapshot/export scripts.

Commit:

```powershell
git add standalone_main.cu
git commit -m "feat: guard optional AI actions with performance safety"
```

---

### Task 5: Document And Verify Full App Behavior

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Update README**

Add this feature bullet near the existing AI/review bullets:

```markdown
- Local Performance Safety Governor that watches render time, estimated GPU memory pressure, labels, callouts, and optional AI workload before recommending user-approved safe actions
```

Add this Quick Demo Flow bullet after the semantic demo bullet:

```markdown
- Check `Performance Safety` in `AI Assist`; it should explain whether the current dataset is healthy, watch-level, pressure-level, or critical without silently changing render quality.
```

- [ ] **Step 2: Run full verification**

Run:

```powershell
git diff --check
.\run_tests_windows.bat
.\build_windows.ps1
.\run_bonsai_asd_verification_windows.bat
.\run_bonsai_verification_windows.bat
.\run_headsq_verification_windows.bat
.\run_render_regression_windows.bat
.\volume_renderer_standalone.exe --volume .\sample-data\bonsai_256x256x256_uint8.raw --width 256 --height 256 --depth 256 --type uint8 --semantic-demo --review-bundle .\build\review_bundle_cli
```

Expected:

- `git diff --check` prints no whitespace errors.
- `volume_import_tests: PASS`.
- CUDA build prints `Build complete`.
- Bonsai ASD reports at least one region and one relation.
- Bonsai render verification passes.
- Headsq render verification passes.
- Foot render regression passes.
- Review bundle export writes PNG, regions JSON, relationships JSON, label mask RAW, and HTML.

- [ ] **Step 3: Launch GUI and capture screenshot**

Run:

```powershell
Get-Process -Name volume_renderer_standalone -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Process -FilePath '.\volume_renderer_standalone.exe' -ArgumentList @('--volume','.\sample-data\bonsai_256x256x256_uint8.raw','--width','256','--height','256','--depth','256','--type','uint8','--semantic-demo') -WorkingDirectory 'C:\Users\akola\Desktop\HomeProject\VoxelPilot' -WindowStyle Normal
```

Capture the screen to:

```powershell
C:\Users\akola\Desktop\HomeProject\VoxelPilot\build\performance_governor_screen.png
```

Expected screenshot evidence:

- app is maximized;
- rendering is visible;
- `AI Assist` includes `Performance Safety`;
- status bar includes a short `Safety:` label;
- no visible quality downgrade is applied automatically.

- [ ] **Step 4: Commit Task 5**

```powershell
git add README.md
git commit -m "docs: describe performance safety governor"
```

---

## Final Review Checklist

- [ ] `git status --short` shows only expected generated/ignored artifacts or is clean.
- [ ] No edits to `renderer/raymarch_kernel.cu` were made for this feature.
- [ ] No automatic render-quality downgrade occurs on normal frames.
- [ ] Performance buttons require explicit user clicks.
- [ ] Existing uncommitted `common/ai_features.h` changes are preserved or intentionally committed separately only with user approval.
- [ ] Final answer includes commit hashes, verification commands, and screenshot path.

## Execution Recommendation

Use subagent-driven development if available. Each task is independent enough for a fresh worker, and each task has a commit checkpoint. If implementing inline, stop after each task for a quick review because this feature touches app state, AI Assist UI, and optional action behavior.

