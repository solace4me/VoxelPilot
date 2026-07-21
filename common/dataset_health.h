#ifndef VOXELPILOT_DATASET_HEALTH_H
#define VOXELPILOT_DATASET_HEALTH_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "ai_prompt.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VOXELPILOT_TRANSFER_ADVICE_KEEP_CURRENT       0
#define VOXELPILOT_TRANSFER_ADVICE_BRIGHTEN_SPARSE    1
#define VOXELPILOT_TRANSFER_ADVICE_HIGH_DENSITY_FOCUS 2

typedef struct {
    int   kind;
    int   has_recommendation;
    float tf_center;
    float tf_width;
    float tf_opacity_scale;
    float min_intensity;
    float confidence;
    char  action[128];
    char  explanation[256];
} VoxelPilotTransferAdvisorResult;

#define VOXELPILOT_DATASET_HEALTH_OK        0
#define VOXELPILOT_DATASET_HEALTH_WATCH     1
#define VOXELPILOT_DATASET_HEALTH_ATTENTION 2

#define VOXELPILOT_DATASET_HEALTH_TINY_VOLUME         0x01
#define VOXELPILOT_DATASET_HEALTH_FLAT_RANGE          0x02
#define VOXELPILOT_DATASET_HEALTH_BACKGROUND_DOMINANT 0x04
#define VOXELPILOT_DATASET_HEALTH_LOW_CONTRAST        0x08
#define VOXELPILOT_DATASET_HEALTH_LARGE_VOLUME        0x10

typedef struct {
    int    level;
    int    flags;
    float  score;
    float  background_ratio;
    float  active_bin_ratio;
    float  dynamic_range;
    size_t voxel_count;
    size_t estimated_bytes;
    char   summary[256];
    char   recommendation[256];
} VoxelPilotDatasetHealthResult;

static inline int voxelpilot_ai_histogram_percentile_bin(
    const float *histogram,
    int          bin_count,
    float        percentile,
    float        total_samples)
{
    float threshold;
    float cumulative = 0.0f;
    int bin_index;

    if (!histogram || bin_count < 2 || total_samples <= 0.0f) {
        return 0;
    }

    percentile = voxelpilot_ai_clampf(percentile, 0.0f, 1.0f);
    threshold = total_samples * percentile;

    for (bin_index = 0; bin_index < bin_count; ++bin_index) {
        if (histogram[bin_index] > 0.0f) {
            cumulative += histogram[bin_index];
        }
        if (cumulative >= threshold) {
            return bin_index;
        }
    }

    return bin_count - 1;
}

static inline int voxelpilot_compute_auto_enhance_transfer(
    const float *histogram,
    int          bin_count,
    float       *out_center,
    float       *out_width,
    float       *out_opacity_scale)
{
    float total_samples = 0.0f;
    float background_ratio;
    float lower_percentile;
    float upper_percentile;
    float informative_samples = 0.0f;
    float informative_ratio;
    int lower_bin;
    int upper_bin;
    int bin_index;
    float lower_norm;
    float upper_norm;
    float center;
    float width;
    float opacity_scale;

    if (!histogram || bin_count < 2 || !out_center || !out_width || !out_opacity_scale) {
        return 0;
    }

    for (bin_index = 0; bin_index < bin_count; ++bin_index) {
        if (histogram[bin_index] > 0.0f) {
            total_samples += histogram[bin_index];
        }
    }

    if (total_samples <= 0.0f) {
        return 0;
    }

    background_ratio = histogram[0] > 0.0f ? histogram[0] / total_samples : 0.0f;
    lower_percentile = background_ratio > 0.30f
        ? voxelpilot_ai_clampf(background_ratio + 0.02f, 0.02f, 0.82f)
        : 0.02f;
    upper_percentile = background_ratio > 0.55f ? 0.995f : 0.900f;

    lower_bin = voxelpilot_ai_histogram_percentile_bin(
        histogram,
        bin_count,
        lower_percentile,
        total_samples);
    upper_bin = voxelpilot_ai_histogram_percentile_bin(
        histogram,
        bin_count,
        upper_percentile,
        total_samples);

    if (upper_bin < lower_bin) {
        upper_bin = lower_bin;
    }

    lower_norm = (float)lower_bin / (float)(bin_count - 1);
    upper_norm = (float)upper_bin / (float)(bin_count - 1);
    center = (lower_norm + upper_norm) * 0.5f;
    width = upper_norm - lower_norm;

    if (width < 0.20f) {
        width = 0.20f;
    }
    if (width > 0.82f) {
        width = 0.82f;
    }
    if (center - width * 0.5f < 0.0f) {
        center = width * 0.5f;
    }
    if (center + width * 0.5f > 1.0f) {
        center = 1.0f - width * 0.5f;
    }

    for (bin_index = lower_bin; bin_index <= upper_bin && bin_index < bin_count; ++bin_index) {
        if (histogram[bin_index] > 0.0f) {
            informative_samples += histogram[bin_index];
        }
    }
    informative_ratio = informative_samples / total_samples;

    opacity_scale = 1.55f;
    if (background_ratio > 0.60f || informative_ratio < 0.35f) {
        opacity_scale = 2.20f;
    } else if (background_ratio > 0.35f || informative_ratio < 0.55f) {
        opacity_scale = 1.90f;
    } else if (width < 0.28f) {
        opacity_scale = 2.05f;
    } else if (width < 0.42f) {
        opacity_scale = 1.85f;
    }

    *out_center = voxelpilot_ai_clampf(center, 0.0f, 1.0f);
    *out_width = voxelpilot_ai_clampf(width, 0.05f, 1.0f);
    *out_opacity_scale = voxelpilot_ai_clampf(opacity_scale, 1.0f, 3.0f);
    return 1;
}

static inline int voxelpilot_compute_dataset_health(
    const float                   *histogram,
    int                            bin_count,
    int                            width,
    int                            height,
    int                            depth,
    float                          value_min,
    float                          value_max,
    size_t                         estimated_bytes,
    VoxelPilotDatasetHealthResult *health)
{
    float total = 0.0f;
    float background_ratio = 0.0f;
    float active_bin_ratio = 0.0f;
    float dynamic_range;
    float score = 100.0f;
    int nonzero_bins = 0;
    int bin_index;
    size_t voxel_count;

    if (!histogram || bin_count < 2 || width < 1 || height < 1 || depth < 1 || !health) {
        return 0;
    }

    memset(health, 0, sizeof(*health));
    voxel_count = (size_t)width * (size_t)height * (size_t)depth;
    dynamic_range = value_max - value_min;
    if (dynamic_range < 0.0f) dynamic_range = -dynamic_range;
    if (estimated_bytes == 0) {
        estimated_bytes = voxel_count * sizeof(float);
    }

    for (bin_index = 0; bin_index < bin_count; ++bin_index) {
        float count = histogram[bin_index] > 0.0f ? histogram[bin_index] : 0.0f;
        if (count <= 0.0f) continue;
        total += count;
        nonzero_bins++;
    }

    if (total <= 0.0f) {
        health->level = VOXELPILOT_DATASET_HEALTH_ATTENTION;
        health->flags = VOXELPILOT_DATASET_HEALTH_FLAT_RANGE;
        health->score = 0.0f;
        health->voxel_count = voxel_count;
        health->estimated_bytes = estimated_bytes;
        health->dynamic_range = dynamic_range;
        snprintf(health->summary, sizeof(health->summary),
                 "Dataset histogram is empty, so VoxelPilot cannot assess visibility yet.");
        snprintf(health->recommendation, sizeof(health->recommendation),
                 "Reload the volume or verify width, height, depth, and data type before running AI review.");
        return 1;
    }

    background_ratio = histogram[0] > 0.0f ? histogram[0] / total : 0.0f;
    active_bin_ratio = (float)nonzero_bins / (float)bin_count;

    health->level = VOXELPILOT_DATASET_HEALTH_OK;
    health->voxel_count = voxel_count;
    health->estimated_bytes = estimated_bytes;
    health->background_ratio = background_ratio;
    health->active_bin_ratio = active_bin_ratio;
    health->dynamic_range = dynamic_range;

    if (voxel_count < (size_t)64 * 64 * 32) {
        health->flags |= VOXELPILOT_DATASET_HEALTH_TINY_VOLUME;
        score -= 15.0f;
    }

    if (dynamic_range < 1.0e-5f || nonzero_bins <= 1) {
        health->flags |= VOXELPILOT_DATASET_HEALTH_FLAT_RANGE;
        score -= 70.0f;
    } else if (dynamic_range < 0.04f || active_bin_ratio < 0.10f) {
        health->flags |= VOXELPILOT_DATASET_HEALTH_LOW_CONTRAST;
        score -= 20.0f;
    }

    if (background_ratio > 0.75f) {
        health->flags |= VOXELPILOT_DATASET_HEALTH_BACKGROUND_DOMINANT;
        score -= background_ratio > 0.90f ? 35.0f : 22.0f;
    }

    if (estimated_bytes > (size_t)768 * 1024 * 1024 ||
        voxel_count > (size_t)512 * 512 * 384) {
        health->flags |= VOXELPILOT_DATASET_HEALTH_LARGE_VOLUME;
        score -= 15.0f;
    }

    health->score = voxelpilot_ai_clampf(score, 0.0f, 100.0f);

    if ((health->flags & VOXELPILOT_DATASET_HEALTH_FLAT_RANGE) ||
        background_ratio > 0.88f) {
        health->level = VOXELPILOT_DATASET_HEALTH_ATTENTION;
    } else if (health->flags != 0) {
        health->level = VOXELPILOT_DATASET_HEALTH_WATCH;
    }

    if (health->flags & VOXELPILOT_DATASET_HEALTH_FLAT_RANGE) {
        snprintf(health->summary, sizeof(health->summary),
                 "Attention: dataset appears flat or constant with %.6f dynamic range and %d active histogram bin(s).",
                 dynamic_range,
                 nonzero_bins);
        snprintf(health->recommendation, sizeof(health->recommendation),
                 "Verify import dimensions/data type; a constant volume cannot support meaningful rendering, ASD, or Guided Review.");
    } else if (health->flags & VOXELPILOT_DATASET_HEALTH_BACKGROUND_DOMINANT) {
        snprintf(health->summary, sizeof(health->summary),
                 "Attention: %.1f%% of samples sit in the background bin, so the object may import too dark or sparse.",
                 background_ratio * 100.0f);
        snprintf(health->recommendation, sizeof(health->recommendation),
                 "Use AI Transfer Advisor or Auto Enhance View before ASD/Guided Review; keep rendering quality unchanged until visibility improves.");
    } else if (health->flags & VOXELPILOT_DATASET_HEALTH_LARGE_VOLUME) {
        snprintf(health->summary, sizeof(health->summary),
                 "Watch: dataset working set is %.1f MB with %.1f%% active histogram bins.",
                 (double)estimated_bytes / (1024.0 * 1024.0),
                 active_bin_ratio * 100.0f);
        snprintf(health->recommendation, sizeof(health->recommendation),
                 "Check Performance Safety before optional AI passes; keep callouts bounded and use brick telemetry for large volumes.");
    } else if (health->flags & VOXELPILOT_DATASET_HEALTH_LOW_CONTRAST) {
        snprintf(health->summary, sizeof(health->summary),
                 "Watch: dataset has narrow contrast with %.1f%% active histogram bins.",
                 active_bin_ratio * 100.0f);
        snprintf(health->recommendation, sizeof(health->recommendation),
                 "Use AI Transfer Advisor to widen visibility before region discovery.");
    } else if (health->flags & VOXELPILOT_DATASET_HEALTH_TINY_VOLUME) {
        snprintf(health->summary, sizeof(health->summary),
                 "Watch: tiny dataset has %zu voxels; rendering is valid but semantic detail may be limited.",
                 voxel_count);
        snprintf(health->recommendation, sizeof(health->recommendation),
                 "Use it for UI/report testing; semantic discovery and Guided Review are stronger on larger volumes.");
    } else {
        snprintf(health->summary, sizeof(health->summary),
                 "healthy dataset: %zu voxels, %.1f%% active histogram bins, and %.3f dynamic range.",
                 voxel_count,
                 active_bin_ratio * 100.0f,
                 dynamic_range);
        snprintf(health->recommendation, sizeof(health->recommendation),
                 "Proceed with AI Transfer Advisor, ASD-Lite, and Guided Review as needed.");
    }

    return 1;
}

static inline int voxelpilot_compute_transfer_advisor(
    const float                    *histogram,
    int                             bin_count,
    float                           current_center,
    float                           current_width,
    float                           current_opacity_scale,
    int                             semantic_region_count,
    VoxelPilotTransferAdvisorResult *advisor)
{
    float total = 0.0f;
    float low = 0.0f;
    float mid = 0.0f;
    float high = 0.0f;
    float weighted_sum = 0.0f;
    float background_ratio;
    float low_ratio;
    float mid_ratio;
    float high_ratio;
    float mean_intensity;
    int lower_bin;
    int upper_bin;
    int bin_index;
    int nonzero_bins = 0;
    float suggested_center;
    float suggested_width;
    float suggested_opacity;

    if (!histogram || bin_count < 2 || !advisor) {
        return 0;
    }

    memset(advisor, 0, sizeof(*advisor));
    advisor->kind = VOXELPILOT_TRANSFER_ADVICE_KEEP_CURRENT;
    advisor->tf_center = voxelpilot_ai_clampf(current_center, 0.0f, 1.0f);
    advisor->tf_width = voxelpilot_ai_clampf(current_width, 0.02f, 1.0f);
    advisor->tf_opacity_scale = voxelpilot_ai_clampf(current_opacity_scale, 0.1f, 3.0f);
    advisor->min_intensity = 0.0f;

    for (bin_index = 0; bin_index < bin_count; ++bin_index) {
        float count = histogram[bin_index] > 0.0f ? histogram[bin_index] : 0.0f;
        float normalized = (float)bin_index / (float)(bin_count - 1);
        if (count <= 0.0f) {
            continue;
        }
        total += count;
        weighted_sum += count * normalized;
        nonzero_bins++;
        if (normalized >= 0.65f) {
            high += count;
        } else if (normalized >= 0.20f) {
            mid += count;
        } else {
            low += count;
        }
    }

    if (total <= 0.0f) {
        snprintf(advisor->action, sizeof(advisor->action),
                 "Load a populated volume histogram");
        snprintf(advisor->explanation, sizeof(advisor->explanation),
                 "AI Transfer Advisor needs a non-empty histogram before it can recommend visibility settings.");
        return 0;
    }

    background_ratio = histogram[0] > 0.0f ? histogram[0] / total : 0.0f;
    low_ratio = low / total;
    mid_ratio = mid / total;
    high_ratio = high / total;
    mean_intensity = weighted_sum / total;

    if ((background_ratio > 0.45f || low_ratio > 0.70f || nonzero_bins <= bin_count / 3) &&
        voxelpilot_compute_auto_enhance_transfer(
            histogram,
            bin_count,
            &suggested_center,
            &suggested_width,
            &suggested_opacity)) {
        advisor->kind = VOXELPILOT_TRANSFER_ADVICE_BRIGHTEN_SPARSE;
        advisor->has_recommendation = 1;
        advisor->tf_center = suggested_center;
        advisor->tf_width = suggested_width;
        advisor->tf_opacity_scale = voxelpilot_ai_clampf(
            suggested_opacity > current_opacity_scale ? suggested_opacity : current_opacity_scale + 0.25f,
            1.0f,
            3.0f);
        advisor->min_intensity = voxelpilot_ai_clampf(
            advisor->tf_center - advisor->tf_width * 0.5f,
            0.0f,
            1.0f);
        advisor->confidence = voxelpilot_ai_clampf(
            background_ratio > low_ratio ? background_ratio : low_ratio,
            0.45f,
            0.95f);
        snprintf(advisor->action, sizeof(advisor->action),
                 "Brighten sparse/background-dominant volume");
        snprintf(advisor->explanation, sizeof(advisor->explanation),
                 "The histogram is %.1f%% background/low-density with only %d active bins, so the advisor recommends a tighter visible window and stronger opacity without changing render quality.",
                 (background_ratio > low_ratio ? background_ratio : low_ratio) * 100.0f,
                 nonzero_bins);
        return 1;
    }

    if (high_ratio >= 0.42f || mean_intensity >= 0.62f) {
        lower_bin = voxelpilot_ai_histogram_percentile_bin(histogram, bin_count, 0.18f, total);
        upper_bin = voxelpilot_ai_histogram_percentile_bin(histogram, bin_count, 0.96f, total);
        if (lower_bin < (int)((float)(bin_count - 1) * 0.60f)) {
            lower_bin = (int)((float)(bin_count - 1) * 0.60f);
        }
        if (upper_bin < lower_bin) {
            upper_bin = lower_bin;
        }
        advisor->kind = VOXELPILOT_TRANSFER_ADVICE_HIGH_DENSITY_FOCUS;
        advisor->has_recommendation = 1;
        advisor->tf_center = ((float)lower_bin + (float)upper_bin) * 0.5f / (float)(bin_count - 1);
        advisor->tf_width = ((float)upper_bin - (float)lower_bin) / (float)(bin_count - 1);
        advisor->tf_width = voxelpilot_ai_clampf(advisor->tf_width, 0.18f, 0.36f);
        if (advisor->tf_center - advisor->tf_width * 0.5f < 0.0f) {
            advisor->tf_center = advisor->tf_width * 0.5f;
        }
        if (advisor->tf_center + advisor->tf_width * 0.5f > 1.0f) {
            advisor->tf_center = 1.0f - advisor->tf_width * 0.5f;
        }
        advisor->tf_opacity_scale = voxelpilot_ai_clampf(
            current_opacity_scale < 1.85f ? 1.85f : current_opacity_scale,
            1.0f,
            3.0f);
        advisor->min_intensity = voxelpilot_ai_clampf(
            advisor->tf_center - advisor->tf_width * 0.5f,
            0.0f,
            1.0f);
        advisor->confidence = voxelpilot_ai_clampf(high_ratio, 0.50f, 0.96f);
        snprintf(advisor->action, sizeof(advisor->action),
                 "Focus high-density shell/object detail");
        snprintf(advisor->explanation, sizeof(advisor->explanation),
                 "High-density samples dominate %.1f%% of the histogram%s, so the advisor recommends an upper-band transfer window for shell, bone-like, mineral, or dense industrial structures.",
                 high_ratio * 100.0f,
                 semantic_region_count > 0 ? " and ASD regions are available" : "");
        return 1;
    }

    advisor->confidence = voxelpilot_ai_clampf(
        mid_ratio > low_ratio ? mid_ratio : low_ratio,
        0.35f,
        0.80f);
    snprintf(advisor->action, sizeof(advisor->action),
             "Keep current transfer settings");
    snprintf(advisor->explanation, sizeof(advisor->explanation),
             "The histogram is already balanced across visible intensities, so the advisor does not recommend changing the current transfer window.");
    return 1;
}

static inline int voxelpilot_should_auto_enhance_on_load(
    const float *histogram,
    int          bin_count)
{
    float total_samples = 0.0f;
    float background_ratio;
    int bin_index;

    if (!histogram || bin_count < 2) {
        return 0;
    }

    for (bin_index = 0; bin_index < bin_count; ++bin_index) {
        if (histogram[bin_index] > 0.0f) {
            total_samples += histogram[bin_index];
        }
    }

    if (total_samples <= 0.0f) {
        return 0;
    }

    background_ratio = histogram[0] > 0.0f ? histogram[0] / total_samples : 0.0f;

    return background_ratio < 0.55f;
}

#ifdef __cplusplus
}
#endif

#endif /* VOXELPILOT_DATASET_HEALTH_H */
