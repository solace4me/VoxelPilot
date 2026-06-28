#ifndef AI_FEATURES_H
#define AI_FEATURES_H

#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "volume_structs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VOXELPILOT_PROMPT_STATUS_MAX 256

#define VOXELPILOT_RENDER_QUALITY_INTERACTIVE 0
#define VOXELPILOT_RENDER_QUALITY_BALANCED    1
#define VOXELPILOT_RENDER_QUALITY_QUALITY     2

typedef struct {
    int   recognized;
    int   set_transfer;
    int   set_min_intensity;
    int   reset_view;
    int   auto_enhance;
    int   set_quality;
    int   quality_mode;
    float tf_center;
    float tf_width;
    float tf_opacity_scale;
    float min_intensity;
    char  status[VOXELPILOT_PROMPT_STATUS_MAX];
} VoxelPilotPromptResult;

typedef struct {
    char  primary_material[96];
    char  description[256];
    float confidence;
    float low_density_ratio;
    float mid_density_ratio;
    float high_density_ratio;
    int   annotated_regions;
} VoxelPilotObjectSummary;

typedef struct {
    float total_samples;
    float visible_samples;
    float visible_ratio;
    float low_density_ratio;
    float mid_density_ratio;
    float high_density_ratio;
    float mean_intensity;
    float percentile_50;
    float percentile_95;
    int   nonzero_bins;
    char  summary[256];
} VoxelPilotQuantMetrics;

typedef struct {
    int visible;
    int resident_candidates;
    int budget;
    int stream_now;
    int queued;
    int evictable;
    int over_budget;
} VoxelPilotBrickCachePlan;

typedef struct {
    int    hit;
    int    x;
    int    y;
    int    z;
    size_t index;
    float  point[3];
    float  intensity;
    float  t;
} VoxelPilotRayPickResult;

static inline float voxelpilot_ai_clampf(float value, float lo, float hi)
{
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static inline void voxelpilot_ai_copy_lower(
    const char *src,
    char       *dst,
    size_t      dst_size)
{
    size_t i;

    if (!dst || dst_size == 0) {
        return;
    }

    if (!src) {
        dst[0] = '\0';
        return;
    }

    for (i = 0; i + 1 < dst_size && src[i]; ++i) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

static inline int voxelpilot_ai_extract_first_number(
    const char *text,
    float      *out_value)
{
    const char *cursor;
    char *end_ptr;
    double value;

    if (!text || !out_value) {
        return 0;
    }

    cursor = text;
    while (*cursor) {
        if ((*cursor >= '0' && *cursor <= '9') || *cursor == '.') {
            value = strtod(cursor, &end_ptr);
            if (end_ptr != cursor) {
                *out_value = (float)value;
                return 1;
            }
        }
        ++cursor;
    }

    return 0;
}

static inline int voxelpilot_ai_text_contains_any(
    const char *text,
    const char *const *terms,
    int         term_count)
{
    int term_index;

    if (!text || !terms || term_count < 1) {
        return 0;
    }

    for (term_index = 0; term_index < term_count; ++term_index) {
        if (terms[term_index] && strstr(text, terms[term_index])) {
            return 1;
        }
    }

    return 0;
}

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
    float upper_percentile = 0.995f;
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

static inline int voxelpilot_blend_label_overlay(
    float base_r,
    float base_g,
    float base_b,
    float label_r,
    float label_g,
    float label_b,
    float alpha,
    float *out_r,
    float *out_g,
    float *out_b)
{
    if (!out_r || !out_g || !out_b) {
        return 0;
    }

    alpha = voxelpilot_ai_clampf(alpha, 0.0f, 1.0f);
    base_r = voxelpilot_ai_clampf(base_r, 0.0f, 1.0f);
    base_g = voxelpilot_ai_clampf(base_g, 0.0f, 1.0f);
    base_b = voxelpilot_ai_clampf(base_b, 0.0f, 1.0f);
    label_r = voxelpilot_ai_clampf(label_r, 0.0f, 1.0f);
    label_g = voxelpilot_ai_clampf(label_g, 0.0f, 1.0f);
    label_b = voxelpilot_ai_clampf(label_b, 0.0f, 1.0f);

    *out_r = base_r * (1.0f - alpha) + label_r * alpha;
    *out_g = base_g * (1.0f - alpha) + label_g * alpha;
    *out_b = base_b * (1.0f - alpha) + label_b * alpha;
    return 1;
}

static inline int voxelpilot_ray_box_intersection(
    const float origin[3],
    const float direction[3],
    float      *out_t_min,
    float      *out_t_max)
{
    float t_min = -3.402823466e+38F;
    float t_max =  3.402823466e+38F;
    int axis;

    if (!origin || !direction || !out_t_min || !out_t_max) {
        return 0;
    }

    for (axis = 0; axis < 3; ++axis) {
        float o = origin[axis];
        float d = direction[axis];

        if (fabsf(d) < 1e-6f) {
            if (o < 0.0f || o > 1.0f) {
                return 0;
            }
        } else {
            float inv_d = 1.0f / d;
            float t0 = (0.0f - o) * inv_d;
            float t1 = (1.0f - o) * inv_d;

            if (t0 > t1) {
                float tmp = t0;
                t0 = t1;
                t1 = tmp;
            }
            if (t0 > t_min) t_min = t0;
            if (t1 < t_max) t_max = t1;
            if (t_max < t_min) {
                return 0;
            }
        }
    }

    *out_t_min = t_min;
    *out_t_max = t_max;
    return t_max >= 0.0f;
}

static inline int voxelpilot_pick_volume_along_ray(
    const float             *volume_data,
    int                      width,
    int                      height,
    int                      depth,
    const float              origin[3],
    const float              direction[3],
    float                    step_size,
    float                    threshold,
    const float              clip_min[3],
    const float              clip_max[3],
    VoxelPilotRayPickResult *result)
{
    float t_min;
    float t_max;
    float t;

    if (result) {
        memset(result, 0, sizeof(*result));
    }

    if (!volume_data || !origin || !direction || !result ||
        width < 1 || height < 1 || depth < 1 ||
        !voxelpilot_ray_box_intersection(origin, direction, &t_min, &t_max)) {
        return 0;
    }

    if (step_size <= 0.0f) {
        step_size = 0.005f;
    }
    if (t_min < 0.0f) {
        t_min = 0.0f;
    }

    for (t = t_min; t <= t_max; t += step_size) {
        float point[3];
        int x;
        int y;
        int z;
        size_t index;
        float intensity;
        int axis;
        int clipped = 0;

        point[0] = origin[0] + direction[0] * t;
        point[1] = origin[1] + direction[1] * t;
        point[2] = origin[2] + direction[2] * t;

        for (axis = 0; axis < 3; ++axis) {
            float lo = clip_min ? clip_min[axis] : 0.0f;
            float hi = clip_max ? clip_max[axis] : 1.0f;
            if (point[axis] < lo || point[axis] > hi) {
                clipped = 1;
                break;
            }
        }
        if (clipped) {
            continue;
        }

        x = (int)(voxelpilot_ai_clampf(point[0], 0.0f, 1.0f) * (float)(width - 1) + 0.5f);
        y = (int)(voxelpilot_ai_clampf(point[1], 0.0f, 1.0f) * (float)(height - 1) + 0.5f);
        z = (int)(voxelpilot_ai_clampf(point[2], 0.0f, 1.0f) * (float)(depth - 1) + 0.5f);
        index =
            (size_t)z * (size_t)width * (size_t)height +
            (size_t)y * (size_t)width +
            (size_t)x;
        intensity = volume_data[index];

        if (intensity < threshold) {
            continue;
        }

        result->hit = 1;
        result->x = x;
        result->y = y;
        result->z = z;
        result->index = index;
        result->point[0] = point[0];
        result->point[1] = point[1];
        result->point[2] = point[2];
        result->intensity = intensity;
        result->t = t;
        return 1;
    }

    return 0;
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

static inline void voxelpilot_init_prompt_result(
    VoxelPilotPromptResult *result)
{
    if (!result) {
        return;
    }

    memset(result, 0, sizeof(*result));
    result->quality_mode = VOXELPILOT_RENDER_QUALITY_BALANCED;
    result->tf_center = 0.45f;
    result->tf_width = 0.35f;
    result->tf_opacity_scale = 1.0f;
    result->min_intensity = 0.0f;
    snprintf(result->status, sizeof(result->status), "No prompt action recognized.");
}

static inline int voxelpilot_parse_prompt_action(
    const char              *prompt,
    VoxelPilotPromptResult *result)
{
    static const char *const visibility_terms[] = {
        "auto enhance", "auto-enhance", "make visible", "more visible",
        "easier to see", "bring out", "brighter", "brighten", "too dark",
        "very dark", "enhance contrast", "more contrast", "clearer", "sharper view",
        "show better", "not showing", "hard to see", "improve visibility"
    };
    static const char *const reset_terms[] = {
        "reset", "full volume", "show all", "restore", "start over"
    };
    static const char *const low_cut_terms[] = {
        "below", "under", "less than", "lower than", "threshold", "cutoff"
    };
    static const char *const high_cut_terms[] = {
        "above", "over", "greater than", "higher than", "at least"
    };
    static const char *const hide_terms[] = {
        "hide", "remove", "exclude", "drop", "suppress", "mask out"
    };
    static const char *const show_terms[] = {
        "show", "keep", "only", "isolate", "segment", "reveal", "focus"
    };
    static const char *const high_density_terms[] = {
        "bone", "cortical", "dense", "high-density", "high density", "shell", "hard tissue"
    };
    static const char *const mid_density_terms[] = {
        "soft tissue", "medium density", "mid density", "muscle", "organ", "tissue"
    };
    static const char *const low_density_terms[] = {
        "low density", "air", "background", "dark material", "low intensity"
    };
    static const char *const quality_terms[] = {
        "quality", "high fidelity", "best detail", "maximum detail", "high detail", "gpu quality"
    };
    static const char *const faster_terms[] = {
        "fast", "faster", "interactive", "responsive", "smooth", "speed"
    };
    char text[512];
    float value = 0.0f;
    int has_number;
    int wants_hide;
    int wants_show;
    int mentions_low_cut;
    int mentions_high_cut;

    if (!result) {
        return 0;
    }

    voxelpilot_init_prompt_result(result);
    voxelpilot_ai_copy_lower(prompt, text, sizeof(text));

    if (!text[0]) {
        return 0;
    }

    has_number = voxelpilot_ai_extract_first_number(text, &value);
    wants_hide = voxelpilot_ai_text_contains_any(text, hide_terms, (int)(sizeof(hide_terms) / sizeof(hide_terms[0])));
    wants_show = voxelpilot_ai_text_contains_any(text, show_terms, (int)(sizeof(show_terms) / sizeof(show_terms[0])));
    mentions_low_cut = voxelpilot_ai_text_contains_any(text, low_cut_terms, (int)(sizeof(low_cut_terms) / sizeof(low_cut_terms[0])));
    mentions_high_cut = voxelpilot_ai_text_contains_any(text, high_cut_terms, (int)(sizeof(high_cut_terms) / sizeof(high_cut_terms[0])));

    if (voxelpilot_ai_text_contains_any(text, quality_terms, (int)(sizeof(quality_terms) / sizeof(quality_terms[0])))) {
        result->recognized = 1;
        result->set_quality = 1;
        result->quality_mode = VOXELPILOT_RENDER_QUALITY_QUALITY;
        snprintf(result->status, sizeof(result->status),
                 "Prompt selected the high-quality GPU rendering preset.");
        return 1;
    }

    if (voxelpilot_ai_text_contains_any(text, faster_terms, (int)(sizeof(faster_terms) / sizeof(faster_terms[0])))) {
        result->recognized = 1;
        result->set_quality = 1;
        result->quality_mode = VOXELPILOT_RENDER_QUALITY_INTERACTIVE;
        snprintf(result->status, sizeof(result->status),
                 "Prompt selected the interactive GPU rendering preset.");
        return 1;
    }

    if (strstr(text, "balanced")) {
        result->recognized = 1;
        result->set_quality = 1;
        result->quality_mode = VOXELPILOT_RENDER_QUALITY_BALANCED;
        snprintf(result->status, sizeof(result->status),
                 "Prompt selected the balanced GPU rendering preset.");
        return 1;
    }

    if (voxelpilot_ai_text_contains_any(text, reset_terms, (int)(sizeof(reset_terms) / sizeof(reset_terms[0])))) {
        result->recognized = 1;
        result->reset_view = 1;
        result->set_transfer = 1;
        result->auto_enhance = 1;
        result->tf_center = 0.45f;
        result->tf_width = 0.35f;
        result->tf_opacity_scale = 1.0f;
        snprintf(result->status, sizeof(result->status),
                 "Prompt reset clipping and requested histogram-based view enhancement.");
        return 1;
    }

    if (voxelpilot_ai_text_contains_any(text, visibility_terms, (int)(sizeof(visibility_terms) / sizeof(visibility_terms[0])))) {
        result->recognized = 1;
        result->auto_enhance = 1;
        snprintf(result->status, sizeof(result->status),
                 "Prompt requested automatic visibility enhancement from the active histogram.");
        return 1;
    }

    if ((wants_hide || wants_show || mentions_low_cut || mentions_high_cut || strstr(text, "intensity")) &&
        has_number) {
        value = voxelpilot_ai_clampf(value, 0.0f, 1.0f);
        result->recognized = 1;
        result->set_transfer = 1;
        result->set_min_intensity = 1;
        result->min_intensity = value;
        result->tf_width = voxelpilot_ai_clampf(1.0f - value, 0.05f, 1.0f);
        result->tf_center = voxelpilot_ai_clampf(value + result->tf_width * 0.5f, 0.0f, 1.0f);
        result->tf_opacity_scale = wants_hide || mentions_low_cut || mentions_high_cut ? 1.55f : 1.35f;
        snprintf(result->status, sizeof(result->status),
                 "Prompt mapped to a flexible intensity cutoff at %.3f.", value);
        return 1;
    }

    if (voxelpilot_ai_text_contains_any(text, high_density_terms, (int)(sizeof(high_density_terms) / sizeof(high_density_terms[0])))) {
        result->recognized = 1;
        result->set_transfer = 1;
        result->set_min_intensity = 1;
        result->min_intensity = 0.62f;
        result->tf_center = 0.78f;
        result->tf_width = 0.24f;
        result->tf_opacity_scale = 1.85f;
        snprintf(result->status, sizeof(result->status),
                 "Prompt mapped to a high-density bone/shell material intent.");
        return 1;
    }

    if (voxelpilot_ai_text_contains_any(text, mid_density_terms, (int)(sizeof(mid_density_terms) / sizeof(mid_density_terms[0])))) {
        result->recognized = 1;
        result->set_transfer = 1;
        result->set_min_intensity = 1;
        result->min_intensity = 0.18f;
        result->tf_center = 0.45f;
        result->tf_width = 0.42f;
        result->tf_opacity_scale = 1.35f;
        snprintf(result->status, sizeof(result->status),
                 "Prompt mapped to a mid-density tissue material intent.");
        return 1;
    }

    if (voxelpilot_ai_text_contains_any(text, low_density_terms, (int)(sizeof(low_density_terms) / sizeof(low_density_terms[0])))) {
        result->recognized = 1;
        result->set_transfer = 1;
        result->set_min_intensity = 1;
        result->min_intensity = 0.02f;
        result->tf_center = 0.20f;
        result->tf_width = 0.32f;
        result->tf_opacity_scale = 1.45f;
        snprintf(result->status, sizeof(result->status),
                 "Prompt mapped to a low-density material intent.");
        return 1;
    }

    return 0;
}

static inline int voxelpilot_flood_fill_label_region(
    const float   *volume_data,
    int            width,
    int            height,
    int            depth,
    int            seed_x,
    int            seed_y,
    int            seed_z,
    float          tolerance,
    unsigned char  label_id,
    unsigned char *label_mask,
    size_t        *out_count,
    float         *out_min_value,
    float         *out_max_value)
{
    size_t voxel_count;
    size_t seed_index;
    unsigned int *queue;
    size_t read_index = 0;
    size_t write_index = 0;
    float seed_value;
    float min_value;
    float max_value;
    size_t count = 0;
    const int dx[6] = { -1, 1, 0, 0, 0, 0 };
    const int dy[6] = { 0, 0, -1, 1, 0, 0 };
    const int dz[6] = { 0, 0, 0, 0, -1, 1 };

    if (!volume_data || !label_mask || width < 1 || height < 1 || depth < 1 ||
        seed_x < 0 || seed_x >= width ||
        seed_y < 0 || seed_y >= height ||
        seed_z < 0 || seed_z >= depth ||
        label_id == 0) {
        return 0;
    }

    voxel_count = (size_t)width * (size_t)height * (size_t)depth;
    seed_index =
        (size_t)seed_z * (size_t)width * (size_t)height +
        (size_t)seed_y * (size_t)width +
        (size_t)seed_x;
    seed_value = volume_data[seed_index];
    min_value = seed_value;
    max_value = seed_value;
    tolerance = fabsf(tolerance);

    queue = (unsigned int *)malloc(voxel_count * sizeof(unsigned int));
    if (!queue) {
        return 0;
    }

    label_mask[seed_index] = label_id;
    queue[write_index++] = (unsigned int)seed_index;

    while (read_index < write_index) {
        unsigned int packed = queue[read_index++];
        int z = (int)(packed / ((unsigned int)width * (unsigned int)height));
        int rem = (int)(packed - (unsigned int)z * (unsigned int)width * (unsigned int)height);
        int y = rem / width;
        int x = rem - y * width;
        float value = volume_data[packed];
        int neighbor_index;

        ++count;
        if (value < min_value) min_value = value;
        if (value > max_value) max_value = value;

        for (neighbor_index = 0; neighbor_index < 6; ++neighbor_index) {
            int nx = x + dx[neighbor_index];
            int ny = y + dy[neighbor_index];
            int nz = z + dz[neighbor_index];
            size_t next_index;
            float next_value;

            if (nx < 0 || nx >= width ||
                ny < 0 || ny >= height ||
                nz < 0 || nz >= depth) {
                continue;
            }

            next_index =
                (size_t)nz * (size_t)width * (size_t)height +
                (size_t)ny * (size_t)width +
                (size_t)nx;

            if (label_mask[next_index] != 0) {
                continue;
            }

            next_value = volume_data[next_index];
            if (fabsf(next_value - seed_value) > tolerance) {
                continue;
            }

            label_mask[next_index] = label_id;
            queue[write_index++] = (unsigned int)next_index;
        }
    }

    free(queue);

    if (out_count) *out_count = count;
    if (out_min_value) *out_min_value = min_value;
    if (out_max_value) *out_max_value = max_value;
    return 1;
}

static inline void voxelpilot_describe_voxel_context(
    float          intensity,
    unsigned char label_id,
    const char   *label_name,
    char         *out_description,
    size_t        out_description_size)
{
    if (!out_description || out_description_size == 0) {
        return;
    }

    if (label_id != 0 && label_name && label_name[0]) {
        snprintf(out_description, out_description_size,
                 "%s - annotated region, label id %u, sampled intensity %.3f.",
                 label_name,
                 (unsigned int)label_id,
                 intensity);
        return;
    }

    if (intensity >= 0.70f) {
        snprintf(out_description, out_description_size,
                 "High-density bone-like material - sampled intensity %.3f.",
                 intensity);
    } else if (intensity >= 0.30f) {
        snprintf(out_description, out_description_size,
                 "Intermediate-density tissue-like material - sampled intensity %.3f.",
                 intensity);
    } else if (intensity >= 0.05f) {
        snprintf(out_description, out_description_size,
                 "Low-density soft material or partial-volume boundary - sampled intensity %.3f.",
                 intensity);
    } else {
        snprintf(out_description, out_description_size,
                 "Background or air-like region - sampled intensity %.3f.",
                 intensity);
    }
}

static inline int voxelpilot_compute_quant_metrics_from_histogram(
    const float              *histogram,
    int                       bin_count,
    float                     visible_threshold,
    VoxelPilotQuantMetrics   *metrics)
{
    float total = 0.0f;
    float visible = 0.0f;
    float low = 0.0f;
    float mid = 0.0f;
    float high = 0.0f;
    float weighted_sum = 0.0f;
    float p50_target;
    float p95_target;
    float cumulative = 0.0f;
    int p50_found = 0;
    int p95_found = 0;
    int i;

    if (!histogram || bin_count < 2 || !metrics) {
        return 0;
    }

    memset(metrics, 0, sizeof(*metrics));
    visible_threshold = voxelpilot_ai_clampf(visible_threshold, 0.0f, 1.0f);

    for (i = 0; i < bin_count; ++i) {
        float count = histogram[i] > 0.0f ? histogram[i] : 0.0f;
        float normalized = (float)i / (float)(bin_count - 1);

        if (count <= 0.0f) {
            continue;
        }

        total += count;
        weighted_sum += count * normalized;
        metrics->nonzero_bins++;

        if (normalized >= visible_threshold) {
            visible += count;
        }
        if (normalized >= 0.65f) {
            high += count;
        } else if (normalized >= 0.20f) {
            mid += count;
        } else {
            low += count;
        }
    }

    if (total <= 0.0f) {
        snprintf(metrics->summary, sizeof(metrics->summary),
                 "No quantitative metrics available because the histogram is empty.");
        return 0;
    }

    p50_target = total * 0.50f;
    p95_target = total * 0.95f;
    for (i = 0; i < bin_count; ++i) {
        float count = histogram[i] > 0.0f ? histogram[i] : 0.0f;
        float normalized = (float)i / (float)(bin_count - 1);

        cumulative += count;
        if (!p50_found && cumulative >= p50_target) {
            metrics->percentile_50 = normalized;
            p50_found = 1;
        }
        if (!p95_found && cumulative >= p95_target) {
            metrics->percentile_95 = normalized;
            p95_found = 1;
            break;
        }
    }

    metrics->total_samples = total;
    metrics->visible_samples = visible;
    metrics->visible_ratio = visible / total;
    metrics->low_density_ratio = low / total;
    metrics->mid_density_ratio = mid / total;
    metrics->high_density_ratio = high / total;
    metrics->mean_intensity = weighted_sum / total;

    snprintf(metrics->summary, sizeof(metrics->summary),
             "%.1f%% visible above %.2f; median %.3f, p95 %.3f, mean %.3f.",
             metrics->visible_ratio * 100.0f,
             visible_threshold,
             metrics->percentile_50,
             metrics->percentile_95,
             metrics->mean_intensity);
    return 1;
}

static inline int voxelpilot_summarize_object_context(
    const float              *histogram,
    int                       bin_count,
    int                       annotated_regions,
    VoxelPilotObjectSummary  *summary)
{
    float total = 0.0f;
    float low = 0.0f;
    float mid = 0.0f;
    float high = 0.0f;
    int i;

    if (!histogram || bin_count < 2 || !summary) {
        return 0;
    }

    memset(summary, 0, sizeof(*summary));
    summary->annotated_regions = annotated_regions;

    for (i = 0; i < bin_count; ++i) {
        float count = histogram[i] > 0.0f ? histogram[i] : 0.0f;
        float normalized = (float)i / (float)(bin_count - 1);

        total += count;
        if (normalized >= 0.65f) {
            high += count;
        } else if (normalized >= 0.20f) {
            mid += count;
        } else {
            low += count;
        }
    }

    if (total <= 0.0f) {
        snprintf(summary->primary_material, sizeof(summary->primary_material),
                 "No dominant material");
        snprintf(summary->description, sizeof(summary->description),
                 "The histogram is empty, so object identification needs a loaded volume.");
        summary->confidence = 0.0f;
        return 0;
    }

    summary->low_density_ratio = low / total;
    summary->mid_density_ratio = mid / total;
    summary->high_density_ratio = high / total;

    if (summary->high_density_ratio >= summary->mid_density_ratio &&
        summary->high_density_ratio >= summary->low_density_ratio) {
        snprintf(summary->primary_material, sizeof(summary->primary_material),
                 "High-density shell / bone-like material");
        summary->confidence = summary->high_density_ratio;
        snprintf(summary->description, sizeof(summary->description),
                 "Local AI heuristic: high-density structures dominate %.1f%% of the histogram. This is a good candidate for bone, shell, implant, or mineralized object review. %d annotated region(s) available for confirmation.",
                 summary->high_density_ratio * 100.0f,
                 annotated_regions);
    } else if (summary->mid_density_ratio >= summary->low_density_ratio) {
        snprintf(summary->primary_material, sizeof(summary->primary_material),
                 "Intermediate-density tissue/object mixture");
        summary->confidence = summary->mid_density_ratio;
        snprintf(summary->description, sizeof(summary->description),
                 "Local AI heuristic: intermediate-density material dominates %.1f%% of the histogram. Prompt segmentation and labels can separate candidate tissue/object regions. %d annotated region(s) available.",
                 summary->mid_density_ratio * 100.0f,
                 annotated_regions);
    } else {
        snprintf(summary->primary_material, sizeof(summary->primary_material),
                 "Low-density/background-dominant volume");
        summary->confidence = summary->low_density_ratio;
        snprintf(summary->description, sizeof(summary->description),
                 "Local AI heuristic: low-density/background samples dominate %.1f%% of the histogram. Tight clipping or a higher transfer threshold may improve object readability. %d annotated region(s) available.",
                 summary->low_density_ratio * 100.0f,
                 annotated_regions);
    }

    return 1;
}

static inline int voxelpilot_estimate_visible_bricks(
    const BrickInfo *bricks,
    int              brick_count,
    int              width,
    int              height,
    int              depth,
    const float      clip_min[3],
    const float      clip_max[3],
    int             *out_visible,
    int             *out_resident_candidates)
{
    int visible = 0;
    int resident = 0;
    int i;
    float min_x;
    float min_y;
    float min_z;
    float max_x;
    float max_y;
    float max_z;

    if (!bricks || brick_count < 1 || width < 1 || height < 1 || depth < 1 ||
        !clip_min || !clip_max || !out_visible || !out_resident_candidates) {
        return 0;
    }

    min_x = clip_min[0] * (float)width;
    min_y = clip_min[1] * (float)height;
    min_z = clip_min[2] * (float)depth;
    max_x = clip_max[0] * (float)width;
    max_y = clip_max[1] * (float)height;
    max_z = clip_max[2] * (float)depth;

    for (i = 0; i < brick_count; ++i) {
        const BrickInfo *brick = &bricks[i];
        float brick_min_x = (float)brick->offsetX;
        float brick_min_y = (float)brick->offsetY;
        float brick_min_z = (float)brick->offsetZ;
        float brick_max_x = (float)(brick->offsetX + brick->sizeX);
        float brick_max_y = (float)(brick->offsetY + brick->sizeY);
        float brick_max_z = (float)(brick->offsetZ + brick->sizeZ);
        int intersects =
            brick_max_x > min_x && brick_min_x <= max_x &&
            brick_max_y > min_y && brick_min_y <= max_y &&
            brick_max_z > min_z && brick_min_z <= max_z;

        if (!intersects) {
            continue;
        }

        ++visible;
        if (!brick->isEmpty) {
            ++resident;
        }
    }

    *out_visible = visible;
    *out_resident_candidates = resident;
    return 1;
}

static inline int voxelpilot_plan_brick_cache(
    int                       visible,
    int                       resident_candidates,
    int                       budget,
    int                       total_bricks,
    VoxelPilotBrickCachePlan *plan)
{
    if (!plan || visible < 0 || resident_candidates < 0 || total_bricks < 1) {
        return 0;
    }

    if (budget < 1) {
        budget = 1;
    }
    if (resident_candidates > total_bricks) {
        resident_candidates = total_bricks;
    }
    if (visible > total_bricks) {
        visible = total_bricks;
    }

    memset(plan, 0, sizeof(*plan));
    plan->visible = visible;
    plan->resident_candidates = resident_candidates;
    plan->budget = budget;
    plan->stream_now =
        resident_candidates < budget ? resident_candidates : budget;
    plan->queued = resident_candidates - plan->stream_now;
    if (plan->queued < 0) plan->queued = 0;
    plan->evictable = total_bricks - resident_candidates;
    if (plan->evictable < 0) plan->evictable = 0;
    plan->over_budget = resident_candidates > budget ? 1 : 0;
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* AI_FEATURES_H */
