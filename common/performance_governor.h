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
    int    volume_width;
    int    volume_height;
    int    volume_depth;
    int    render_width;
    int    render_height;
    size_t estimated_volume_bytes;
    size_t estimated_gradient_bytes;
    size_t estimated_label_mask_bytes;
    size_t estimated_render_target_bytes;
    size_t gpu_memory_available;
    size_t gpu_memory_total;
    int    has_gpu_memory_telemetry;
    float  render_ms_last;
    float  render_ms_average;
    int    render_quality_mode;
    int    cinematic_enabled;
    int    label_overlay_enabled;
    int    label_mask_present;
    int    semantic_callouts_enabled;
    int    semantic_callout_top_regions;
    int    semantic_region_count;
} VoxelPilotPerformanceInputs;

typedef struct {
    int    status;
    int    pressure_score;
    float  memory_budget_ratio;
    size_t estimated_working_set_bytes;
    int    safe_to_run_asd;
    int    safe_to_export_mask;
    int    suggest_interactive_gpu;
    int    suggest_reduce_callouts;
    int    suggest_brick_streaming;
    char   status_label[32];
    char   primary_reason[VOXELPILOT_GOVERNOR_TEXT_MAX];
    char   recommendation[VOXELPILOT_GOVERNOR_TEXT_MAX];
} VoxelPilotPerformanceResult;
#define VOXELPILOT_ADAPTIVE_QUALITY_TEXT_MAX 192

typedef struct {
    int   enabled;
    int   interaction_active;
    int   cinematic_enabled;
    float base_step_size;
    float base_skip_mult;
    float current_multiplier;
    float render_ms_last;
    float render_ms_average;
} VoxelPilotAdaptiveQualityInputs;

typedef struct {
    int   active;
    int   pressure;
    int   recovering;
    float multiplier;
    float step_size;
    float skip_mult;
    char  status[VOXELPILOT_ADAPTIVE_QUALITY_TEXT_MAX];
} VoxelPilotAdaptiveQualityResult;

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

static inline float voxelpilot_update_render_ms_average(
    float previous_average,
    float latest_ms,
    int   has_previous_sample)
{
    if (latest_ms < 0.0f) latest_ms = 0.0f;
    if (!has_previous_sample || previous_average <= 0.0f) return latest_ms;
    return previous_average * 0.85f + latest_ms * 0.15f;
}

static inline void voxelpilot_adaptive_quality_inputs_defaults(
    VoxelPilotAdaptiveQualityInputs *inputs)
{
    if (!inputs) return;
    memset(inputs, 0, sizeof(*inputs));
    inputs->enabled = 1;
    inputs->base_step_size = 0.0020f;
    inputs->base_skip_mult = 1.50f;
    inputs->current_multiplier = 1.0f;
}

static inline int voxelpilot_update_adaptive_quality(
    const VoxelPilotAdaptiveQualityInputs *inputs,
    VoxelPilotAdaptiveQualityResult       *result)
{
    float base_step;
    float base_skip;
    float multiplier;
    float next_multiplier;
    float render_ms;

    if (!inputs || !result) return 0;

    memset(result, 0, sizeof(*result));
    base_step = voxelpilot_ai_clampf(inputs->base_step_size, 0.0005f, 0.0100f);
    base_skip = voxelpilot_ai_clampf(inputs->base_skip_mult, 1.0f, 8.0f);
    multiplier = voxelpilot_ai_clampf(inputs->current_multiplier, 1.0f, 3.0f);
    render_ms = inputs->render_ms_average > 0.0f
                    ? inputs->render_ms_average
                    : inputs->render_ms_last;
    if (render_ms < 0.0f) render_ms = 0.0f;

    if (!inputs->enabled) {
        result->multiplier = 1.0f;
        result->step_size = base_step;
        result->skip_mult = base_skip;
        snprintf(result->status, sizeof(result->status),
                 "Adaptive quality disabled: using the selected GPU preset.");
        return 1;
    }

    next_multiplier = multiplier;
    if (render_ms > 140.0f) {
        next_multiplier += 0.65f;
        result->pressure = 1;
    } else if (render_ms > 90.0f) {
        next_multiplier += 0.50f;
        result->pressure = 1;
    } else if (inputs->interaction_active && render_ms > 55.0f) {
        next_multiplier += 0.35f;
        result->pressure = 1;
    } else if (inputs->cinematic_enabled && render_ms > 50.0f) {
        next_multiplier += 0.25f;
        result->pressure = 1;
    } else if (multiplier > 1.0f) {
        if (render_ms < 30.0f) {
            next_multiplier -= 0.12f;
            result->recovering = 1;
        } else if (!inputs->interaction_active && render_ms < 42.0f) {
            next_multiplier -= 0.06f;
            result->recovering = 1;
        }
    }

    next_multiplier = voxelpilot_ai_clampf(next_multiplier, 1.0f, 3.0f);
    result->multiplier = next_multiplier;
    result->step_size = voxelpilot_ai_clampf(base_step * next_multiplier, base_step, 0.0100f);
    result->skip_mult = voxelpilot_ai_clampf(base_skip + (next_multiplier - 1.0f) * 0.50f, base_skip, 8.0f);
    result->active = next_multiplier > 1.0001f;

    if (result->pressure) {
        snprintf(result->status, sizeof(result->status),
                 "Adaptive quality %.2fx: render average %.1f ms, using wider ray steps while %s.",
                 next_multiplier,
                 render_ms,
                 inputs->interaction_active ? "navigating" : "under load");
    } else if (result->recovering) {
        snprintf(result->status, sizeof(result->status),
                 "Adaptive quality %.2fx: frame time %.1f ms, gradually restoring detail.",
                 next_multiplier,
                 render_ms);
    } else if (result->active) {
        snprintf(result->status, sizeof(result->status),
                 "Adaptive quality %.2fx: holding temporary ray-step adjustment.",
                 next_multiplier);
    } else {
        snprintf(result->status, sizeof(result->status),
                 "Adaptive quality ready: using the selected GPU preset.");
    }

    return 1;
}
static inline void voxelpilot_performance_inputs_defaults(
    VoxelPilotPerformanceInputs *inputs)
{
    if (!inputs) return;
    memset(inputs, 0, sizeof(*inputs));
    inputs->render_quality_mode = VOXELPILOT_RENDER_QUALITY_BALANCED;
}

static inline void voxelpilot_performance_result_defaults(
    VoxelPilotPerformanceResult *result)
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

static inline int voxelpilot_evaluate_performance(
    const VoxelPilotPerformanceInputs *inputs,
    VoxelPilotPerformanceResult       *result)
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

    working_set = inputs->estimated_volume_bytes;
    working_set = voxelpilot_safe_size_add(working_set, inputs->estimated_gradient_bytes);
    working_set = voxelpilot_safe_size_add(working_set, inputs->estimated_label_mask_bytes);
    working_set = voxelpilot_safe_size_add(working_set, inputs->estimated_render_target_bytes);
    if (working_set == 0) {
        working_set = (size_t)inputs->volume_width *
                      (size_t)inputs->volume_height *
                      (size_t)inputs->volume_depth *
                      5u;
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
    if (inputs->semantic_callouts_enabled &&
        inputs->semantic_callout_top_regions > 3 &&
        inputs->semantic_region_count >= inputs->semantic_callout_top_regions &&
        inputs->render_ms_average > 80.0f) {
        score += 10;
        result->suggest_reduce_callouts = 1;
    }

    if (score > 100) score = 100;
    result->pressure_score = score;

    result->suggest_interactive_gpu =
        inputs->render_quality_mode != VOXELPILOT_RENDER_QUALITY_INTERACTIVE &&
        inputs->render_ms_average > 90.0f;
    result->suggest_brick_streaming = memory_ratio > 0.70f;
    result->safe_to_run_asd = memory_ratio < 0.90f;
    result->safe_to_export_mask = memory_ratio < 0.85f;

    if (score >= 75) result->status = VOXELPILOT_GOVERNOR_CRITICAL;
    else if (score >= 45) result->status = VOXELPILOT_GOVERNOR_PRESSURE;
    else if (score >= 18) result->status = VOXELPILOT_GOVERNOR_WATCH;
    else result->status = VOXELPILOT_GOVERNOR_HEALTHY;

    snprintf(result->status_label, sizeof(result->status_label), "%s",
             voxelpilot_governor_status_label(result->status));

    if (result->suggest_reduce_callouts) {
        snprintf(result->primary_reason, sizeof(result->primary_reason),
                 "Semantic labels and many callouts are active while render time is elevated.");
        snprintf(result->recommendation, sizeof(result->recommendation),
                 "Limit callouts to one highlighted region while navigating.");
    } else if (result->suggest_brick_streaming) {
        snprintf(result->primary_reason, sizeof(result->primary_reason),
                 "Estimated working set is %.0f%% of the available GPU memory budget.",
                 memory_ratio * 100.0f);
        snprintf(result->recommendation, sizeof(result->recommendation),
                 "Use a brick-streaming path for this dataset or keep optional AI passes limited.");
    } else if (result->suggest_interactive_gpu) {
        snprintf(result->primary_reason, sizeof(result->primary_reason),
                 "Render average is %.1f ms with the current GPU quality settings.",
                 inputs->render_ms_average);
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
