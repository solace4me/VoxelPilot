/* Performance Safety Governor integration: adaptive-quality refresh,
   the governor evaluation itself, the optional-action gate, the status
   color helper, and the AI Assist safety block UI.
   Extracted from standalone_main.cu as part of the Tier 3c module split
   (see docs/superpowers/plans/2026-09-30-standalone-main-split.md).

   estimate_current_volume_bytes / estimate_current_label_mask_bytes stay
   `static` here - they're private helpers used only by
   refresh_performance_governor, with no other callers anywhere in the
   codebase.

   refresh_performance_governor calls the CUDA Runtime API directly
   (cudaMemGetInfo) for GPU memory telemetry - the first extracted file
   to do so, and it took two attempts to get the headers right.

   First attempt: assumed cuda_runtime.h reached this file transitively
   via standalone_app.h -> common/volume_structs.h. Wrong - that header
   only includes the real cuda_runtime.h when __CUDACC__ is defined
   (i.e. compiled by nvcc); under cl.exe it falls back to a minimal
   compatibility shim (float3/float4/uchar4 as plain structs,
   cudaArray_t/cudaTextureObject_t/cudaGraphicsResource_t/cudaEvent_t/
   cudaStream_t as void*) that has no cudaError_t, cudaSuccess, or
   cudaMemGetInfo. build_windows.ps1 caught this as C2065/C3861
   undeclared-identifier errors.

   Second attempt (this one): tried including <cuda_runtime_api.h>
   directly (the host-callable C API subset, without __CUDACC__-gated
   kernel-launch syntax) plus -I"%CUDA_HOME%\include" in
   build_windows.bat. Rejected before it reached a build: that header
   declares cudaArray_t/cudaTextureObject_t/cudaGraphicsResource_t/
   cudaEvent_t/cudaStream_t as real opaque CUDA types, which collide
   with volume_structs.h's void*-based shim typedefs of the exact same
   names - a hard redefinition error whichever order the two includes
   land in, since cl.exe never defines __CUDACC__ either way.

   Fixed by not including any CUDA header here at all: cudaError_t,
   cudaSuccess, and cudaMemGetInfo are declared by hand, scoped to this
   file. None of the three names appear in volume_structs.h's shim, so
   there's no redefinition risk. cudaError_t is declared as `int`
   rather than the real ~100-value enum: every cudaError_t value fits
   in int, and every compiler CUDA supports (cl.exe included) lays out
   a plain C enum as int, so this matches the real ABI for the purposes
   of a function return value and an equality comparison against 0
   (cudaSuccess), without needing the enum's other names. The
   cudaMemGetInfo symbol itself still resolves at link time against the
   cudart that the final nvcc link step already pulls in via -cudart
   hybrid (the same mechanism that resolves detect_cuda_runtime()'s
   cudaGetDeviceCount call in standalone_main.cu today) - that part of
   the original reasoning held up across both attempts.

   voxelpilot_pressure_color and draw_performance_safety_block use
   ImGui directly - the first extracted file to need it. standalone_app.h
   now includes imgui.h itself (it has to: it declares
   voxelpilot_pressure_color, which returns ImVec4, and three earlier
   extracted files - export_bundles.cpp, annotation_and_semantic_memory.cpp,
   workspace_presets.cpp - include standalone_app.h without otherwise
   including imgui.h). The cl.exe include paths already have -Iimgui
   from the export_bundles.cpp step, so nothing else to add there.

   apply_render_quality_preset stays defined in standalone_main.cu
   (declared in standalone_app.h) - draw_performance_safety_block calls
   back into it. voxelpilot_score_color and voxelpilot_same_line_if_fits
   also stay in standalone_main.cu: despite sitting right next to this
   cluster before extraction, neither is used by anything moved here -
   voxelpilot_score_color is called only from the AI Assist region
   confidence/stability readouts, voxelpilot_same_line_if_fits only from
   AI Assist panel button rows. */

#include <stdio.h>

#include "standalone_app.h"

/* Hand-declared in place of a CUDA header - see the file comment above. */
extern "C" {
typedef int cudaError_t;
cudaError_t cudaMemGetInfo(size_t *free, size_t *total);
}
#define cudaSuccess 0

void refresh_adaptive_quality_for_frame(StandaloneApp *app)
{
    VoxelPilotAdaptiveQualityInputs inputs;
    VoxelPilotAdaptiveQualityResult result;

    if (!app) return;

    voxelpilot_adaptive_quality_inputs_defaults(&inputs);
    inputs.enabled = app->adaptive_quality_enabled;
    inputs.interaction_active = app->camera_motion_active;
    inputs.cinematic_enabled = app->cinematic_enabled;
    inputs.base_step_size = app->step_size;
    inputs.base_skip_mult = app->skip_mult;
    inputs.current_multiplier = app->adaptive_quality_multiplier > 0.0f
                                    ? app->adaptive_quality_multiplier
                                    : 1.0f;
    inputs.render_ms_last = app->last_render_ms;
    inputs.render_ms_average = app->render_ms_average_ready
                                    ? app->render_ms_average
                                    : app->last_render_ms;

    if (!voxelpilot_update_adaptive_quality(&inputs, &result)) {
        app->adaptive_quality_multiplier = 1.0f;
        app->adaptive_quality_active = 0;
        app->adaptive_step_size = app->step_size;
        app->adaptive_skip_mult = app->skip_mult;
        snprintf(app->adaptive_quality_status,
                 sizeof(app->adaptive_quality_status),
                 "Adaptive quality unavailable: using the selected GPU preset.");
        return;
    }

    app->adaptive_quality_multiplier = result.multiplier;
    app->adaptive_quality_active = result.active;
    app->adaptive_step_size = result.step_size;
    app->adaptive_skip_mult = result.skip_mult;
    snprintf(app->adaptive_quality_status,
             sizeof(app->adaptive_quality_status),
             "%s",
             result.status);
}
/* The volume's GPU texture: 16-bit, 2 bytes per voxel (renderer/
   volume_textures.cu). This used to assume a 4-byte float texture plus a
   gradient texture three times that size, which the renderer no longer
   has - it overstated a 512x512x373 scan by about 1.4 GB and put the
   Safety badge on "Watch" for no reason. */
static size_t estimate_current_volume_bytes(const StandaloneApp *app)
{
    if (!app || app->renderer.W < 1 || app->renderer.H < 1 || app->renderer.D < 1) {
        return 0;
    }
    return (size_t)app->renderer.W *
           (size_t)app->renderer.H *
           (size_t)app->renderer.D *
           sizeof(unsigned short);
}

static size_t estimate_current_label_mask_bytes(const StandaloneApp *app)
{
    if (!app || !app->label_mask ||
        app->label_mask_W < 1 || app->label_mask_H < 1 || app->label_mask_D < 1) {
        return 0;
    }
    return (size_t)app->label_mask_W *
           (size_t)app->label_mask_H *
           (size_t)app->label_mask_D;
}

void refresh_performance_governor(StandaloneApp *app)
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
    /* Gradients are computed in the kernel; nothing is stored. */
    inputs.estimated_gradient_bytes = 0;
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
        /* The loaded volume, label mask and render target are already
           resident, so they are missing from free_bytes. Compare the
           working set against what it could use - free plus its own
           share - rather than counting it twice. */
        size_t resident = inputs.estimated_volume_bytes +
                          inputs.estimated_label_mask_bytes +
                          inputs.estimated_render_target_bytes;
        inputs.has_gpu_memory_telemetry = 1;
        inputs.gpu_memory_available = free_bytes + resident;
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

int performance_allows_optional_action(
    StandaloneApp *app,
    const char    *action_label,
    int            requires_asd,
    int            requires_mask_export)
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

ImVec4 voxelpilot_pressure_color(int status)
{
    if (status == VOXELPILOT_GOVERNOR_CRITICAL) return ImVec4(0.90f, 0.30f, 0.30f, 1.0f);
    if (status == VOXELPILOT_GOVERNOR_PRESSURE) return ImVec4(0.90f, 0.60f, 0.20f, 1.0f);
    if (status == VOXELPILOT_GOVERNOR_WATCH)    return ImVec4(0.85f, 0.80f, 0.25f, 1.0f);
    return ImVec4(0.35f, 0.85f, 0.55f, 1.0f);
}

void draw_performance_safety_block(StandaloneApp *app)
{
    if (!app) return;

    refresh_performance_governor(app);

    ImGui::Separator();
    ImGui::Text("Performance Safety");
    ImGui::TextColored(voxelpilot_pressure_color(app->performance_result.status),
                "Status: %s", app->performance_result.status_label);
    {
        char pressure_overlay[16];
        snprintf(pressure_overlay, sizeof(pressure_overlay), "%d/100", app->performance_result.pressure_score);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, voxelpilot_pressure_color(app->performance_result.status));
        ImGui::ProgressBar(app->performance_result.pressure_score / 100.0f, ImVec2(120.0f, 0.0f), pressure_overlay);
        ImGui::PopStyleColor();
    }
    ImGui::TextWrapped("%s", app->performance_result.primary_reason);
    ImGui::TextWrapped("Recommendation: %s", app->performance_result.recommendation);

    if (app->performance_result.suggest_interactive_gpu &&
        app->render_quality_mode != VOXELPILOT_RENDER_QUALITY_INTERACTIVE) {
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

    if (app->performance_result.suggest_reduce_callouts &&
        app->semantic_callouts_enabled &&
        app->semantic_callout_top_regions > 1) {
        if (ImGui::Button("Limit Callouts to 1")) {
            app->semantic_callout_top_regions = 1;
            snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                     "Performance Safety limited cinematic callouts to one highlighted region.");
            refresh_performance_governor(app);
        }
    }

    if (ImGui::Button("Refresh Safety Check")) {
        refresh_performance_governor(app);
        snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                 "%s", app->performance_status);
    }
}
