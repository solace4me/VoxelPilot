#ifndef UI_STATE_HELPERS_H
#define UI_STATE_HELPERS_H

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline void voxelpilot_set_default_clip_bounds(
    float clip_min[3],
    float clip_max[3])
{
    if (!clip_min || !clip_max) {
        return;
    }

    clip_min[0] = 0.0f;
    clip_min[1] = 0.0f;
    clip_min[2] = 0.0f;
    clip_max[0] = 1.0f;
    clip_max[1] = 1.0f;
    clip_max[2] = 1.0f;
}

static inline int voxelpilot_clip_is_active(
    const float clip_min[3],
    const float clip_max[3])
{
    int i;

    if (!clip_min || !clip_max) {
        return 0;
    }

    for (i = 0; i < 3; ++i) {
        if (clip_min[i] > 0.0f || clip_max[i] < 1.0f) {
            return 1;
        }
    }

    return 0;
}

static inline int voxelpilot_should_rebuild_dock_layout(
    float previous_width,
    float previous_height,
    float current_width,
    float current_height)
{
    (void)current_width;
    (void)current_height;

    if (previous_width <= 0.0f || previous_height <= 0.0f) {
        return 1;
    }

    return 0;
}

static inline int voxelpilot_should_refit_layout_after_maximize(
    int previous_maximized,
    int current_maximized)
{
    return !previous_maximized && current_maximized;
}

static inline int voxelpilot_suggest_transfer_window(
    const float *histogram,
    int          bin_count,
    float        lower_percentile,
    float        upper_percentile,
    float       *out_center,
    float       *out_width)
{
    float total_samples = 0.0f;
    float lower_threshold;
    float upper_threshold;
    float cumulative = 0.0f;
    int lower_bin = 0;
    int upper_bin = 0;
    int lower_found = 0;
    int upper_found = 0;
    int bin_index;
    float lower_norm;
    float upper_norm;
    float window_center;
    float window_width;

    if (!histogram || bin_count < 2 || !out_center || !out_width) {
        return 0;
    }

    if (lower_percentile < 0.0f) lower_percentile = 0.0f;
    if (lower_percentile > 1.0f) lower_percentile = 1.0f;
    if (upper_percentile < 0.0f) upper_percentile = 0.0f;
    if (upper_percentile > 1.0f) upper_percentile = 1.0f;
    if (upper_percentile < lower_percentile) {
        upper_percentile = lower_percentile;
    }

    for (bin_index = 0; bin_index < bin_count; ++bin_index) {
        if (histogram[bin_index] > 0.0f) {
            total_samples += histogram[bin_index];
        }
    }

    if (total_samples <= 0.0f) {
        return 0;
    }

    lower_threshold = total_samples * lower_percentile;
    upper_threshold = total_samples * upper_percentile;
    upper_bin = bin_count - 1;

    for (bin_index = 0; bin_index < bin_count; ++bin_index) {
        if (histogram[bin_index] > 0.0f) {
            cumulative += histogram[bin_index];
        }

        if (!lower_found && cumulative >= lower_threshold) {
            lower_bin = bin_index;
            lower_found = 1;
        }

        if (!upper_found && cumulative >= upper_threshold) {
            upper_bin = bin_index;
            upper_found = 1;
            break;
        }
    }

    if (upper_bin < lower_bin) {
        upper_bin = lower_bin;
    }

    lower_norm = (float)lower_bin / (float)(bin_count - 1);
    upper_norm = (float)upper_bin / (float)(bin_count - 1);
    window_center = (lower_norm + upper_norm) * 0.5f;
    window_width = upper_norm - lower_norm;

    if (window_width < 0.05f) {
        window_width = 0.05f;
    }
    if (window_width > 1.0f) {
        window_width = 1.0f;
    }

    if (window_center - window_width * 0.5f < 0.0f) {
        window_center = window_width * 0.5f;
    }
    if (window_center + window_width * 0.5f > 1.0f) {
        window_center = 1.0f - window_width * 0.5f;
    }

    *out_center = window_center;
    *out_width = window_width;
    return 1;
}

static inline int voxelpilot_find_high_variance_axial_slice(
    const float *volume_data,
    int          width,
    int          height,
    int          depth,
    float       *out_slice,
    float       *out_score)
{
    double best_variance = -1.0;
    int best_slice = 0;
    int slice_index;
    int row_index;
    int column_index;
    size_t slice_voxel_count;

    if (!volume_data || width < 1 || height < 1 || depth < 1 ||
        !out_slice || !out_score) {
        return 0;
    }

    slice_voxel_count = (size_t)width * (size_t)height;
    if (slice_voxel_count == 0) {
        return 0;
    }

    for (slice_index = 0; slice_index < depth; ++slice_index) {
        double mean = 0.0;
        double mean_square = 0.0;
        double variance;

        for (row_index = 0; row_index < height; ++row_index) {
            for (column_index = 0; column_index < width; ++column_index) {
                size_t voxel_index =
                    (size_t)slice_index * slice_voxel_count +
                    (size_t)row_index * (size_t)width +
                    (size_t)column_index;
                double value = (double)volume_data[voxel_index];
                mean += value;
                mean_square += value * value;
            }
        }

        mean /= (double)slice_voxel_count;
        mean_square /= (double)slice_voxel_count;
        variance = mean_square - mean * mean;
        if (variance < 0.0) {
            variance = 0.0;
        }

        if (variance > best_variance) {
            best_variance = variance;
            best_slice = slice_index;
        }
    }

    *out_slice = (depth > 1)
        ? (float)best_slice / (float)(depth - 1)
        : 0.0f;
    *out_score = (float)best_variance;
    return 1;
}

static inline int voxelpilot_clamp_render_extent(int value)
{
    if (value < 64) {
        return 64;
    }

    if (value > 4096) {
        return 4096;
    }

    return value;
}

typedef struct {
    int   enabled;
    float rim_strength;
    float glow_strength;
    float contrast;
} VoxelPilotCinematicParams;

static inline float voxelpilot_clampf(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static inline void voxelpilot_cinematic_defaults(VoxelPilotCinematicParams *params)
{
    if (!params) {
        return;
    }

    params->enabled = 0;
    params->rim_strength = 0.35f;
    params->glow_strength = 0.20f;
    params->contrast = 1.10f;
}

static inline void voxelpilot_cinematic_clamp(VoxelPilotCinematicParams *params)
{
    if (!params) {
        return;
    }

    params->enabled = params->enabled ? 1 : 0;
    params->rim_strength = voxelpilot_clampf(params->rim_strength, 0.0f, 1.0f);
    params->glow_strength = voxelpilot_clampf(params->glow_strength, 0.0f, 1.0f);
    params->contrast = voxelpilot_clampf(params->contrast, 0.8f, 1.6f);
}

typedef struct {
    int   cinematic_enabled;
    float cinematic_rim_strength;
    float cinematic_glow_strength;
    float cinematic_contrast;
    int   callouts_enabled;
    int   callout_top_regions;
    int   selected_only;
    float callout_opacity;
    int   label_overlay_visible;
    float label_overlay_alpha;
    int   semantic_max_regions;
    int   semantic_min_region_voxels;
    float semantic_sensitivity;
} VoxelPilotSemanticDemoSettings;

static inline void voxelpilot_semantic_demo_defaults(VoxelPilotSemanticDemoSettings *settings)
{
    if (!settings) {
        return;
    }

    settings->cinematic_enabled = 1;
    settings->cinematic_rim_strength = 0.55f;
    settings->cinematic_glow_strength = 0.40f;
    settings->cinematic_contrast = 1.24f;
    settings->callouts_enabled = 1;
    settings->callout_top_regions = 3;
    settings->selected_only = 0;
    settings->callout_opacity = 0.94f;
    settings->label_overlay_visible = 1;
    settings->label_overlay_alpha = 0.78f;
    settings->semantic_max_regions = 8;
    settings->semantic_min_region_voxels = 2048;
    settings->semantic_sensitivity = 0.72f;
}

static inline int voxelpilot_compute_raymarch_step_limit(
    float step_size,
    float t_max,
    int   hard_cap)
{
    int step_limit;

    if (step_size <= 0.0f) {
        step_size = 0.0025f;
    }
    if (t_max <= 0.0f) {
        t_max = 2.0f;
    }
    if (hard_cap < 512) {
        hard_cap = 512;
    }

    step_limit = (int)(t_max / step_size) + 2;
    if (step_limit < 512) {
        step_limit = 512;
    }
    if (step_limit > hard_cap) {
        step_limit = hard_cap;
    }

    return step_limit;
}

static inline int voxelpilot_should_sync_render_target(
    int match_window,
    int current_width,
    int current_height,
    int framebuffer_width,
    int framebuffer_height)
{
    if (!match_window) {
        return 0;
    }

    if (framebuffer_width <= 0 || framebuffer_height <= 0) {
        return 0;
    }

    framebuffer_width = voxelpilot_clamp_render_extent(framebuffer_width);
    framebuffer_height = voxelpilot_clamp_render_extent(framebuffer_height);

    return current_width != framebuffer_width ||
           current_height != framebuffer_height;
}

#define VOXELPILOT_REVIEW_BUNDLE_PATH_MAX 512

typedef struct {
    char snapshot_png[VOXELPILOT_REVIEW_BUNDLE_PATH_MAX];
    char regions_json[VOXELPILOT_REVIEW_BUNDLE_PATH_MAX];
    char relationships_json[VOXELPILOT_REVIEW_BUNDLE_PATH_MAX];
    char label_mask_raw[VOXELPILOT_REVIEW_BUNDLE_PATH_MAX];
    char report_html[VOXELPILOT_REVIEW_BUNDLE_PATH_MAX];
} VoxelPilotReviewBundlePaths;

static inline void voxelpilot_sanitize_bundle_stem(
    const char *stem,
    char *out_stem,
    size_t out_stem_size)
{
    size_t write_index = 0;
    size_t read_index;

    if (!out_stem || out_stem_size == 0) {
        return;
    }

    if (!stem || !stem[0]) {
        snprintf(out_stem, out_stem_size, "voxelpilot_ai_review");
        return;
    }

    for (read_index = 0; stem[read_index] && write_index + 1 < out_stem_size; ++read_index) {
        unsigned char c = (unsigned char)stem[read_index];
        if (isalnum(c) || c == '-' || c == '_') {
            out_stem[write_index++] = (char)c;
        } else {
            if (write_index == 0 || out_stem[write_index - 1] != '_') {
                out_stem[write_index++] = '_';
            }
        }
    }

    while (write_index > 0 && out_stem[write_index - 1] == '_') {
        --write_index;
    }
    out_stem[write_index] = '\0';

    if (!out_stem[0]) {
        snprintf(out_stem, out_stem_size, "voxelpilot_ai_review");
    }
}

static inline int voxelpilot_write_bundle_path(
    char *out_path,
    size_t out_path_size,
    const char *folder,
    const char *separator,
    const char *safe_stem,
    const char *suffix)
{
    int written;

    if (!out_path || out_path_size == 0) return 0;
    written = snprintf(out_path, out_path_size,
                       "%s%s%s%s", folder, separator, safe_stem, suffix);
    return written > 0 && (size_t)written < out_path_size;
}

static inline int voxelpilot_make_review_bundle_paths(
    const char *folder,
    const char *stem,
    VoxelPilotReviewBundlePaths *paths)
{
    char safe_stem[128];
    const char *separator = "\\";
    size_t folder_len;

    if (!folder || !folder[0] || !paths) {
        return 0;
    }

    memset(paths, 0, sizeof(*paths));
    voxelpilot_sanitize_bundle_stem(stem, safe_stem, sizeof(safe_stem));

    folder_len = strlen(folder);
    if (folder_len > 0 &&
        (folder[folder_len - 1] == '\\' || folder[folder_len - 1] == '/')) {
        separator = "";
    }

    return voxelpilot_write_bundle_path(paths->snapshot_png, sizeof(paths->snapshot_png),
                                        folder, separator, safe_stem, "_snapshot.png") &&
           voxelpilot_write_bundle_path(paths->regions_json, sizeof(paths->regions_json),
                                        folder, separator, safe_stem, "_regions.json") &&
           voxelpilot_write_bundle_path(paths->relationships_json, sizeof(paths->relationships_json),
                                        folder, separator, safe_stem, "_relationships.json") &&
           voxelpilot_write_bundle_path(paths->label_mask_raw, sizeof(paths->label_mask_raw),
                                        folder, separator, safe_stem, "_label_mask.raw") &&
           voxelpilot_write_bundle_path(paths->report_html, sizeof(paths->report_html),
                                        folder, separator, safe_stem, "_report.html");
}

static inline float voxelpilot_dot3(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static inline void voxelpilot_cross3(const float a[3], const float b[3], float out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static inline int voxelpilot_normalize3(float v[3])
{
    float length = sqrtf(voxelpilot_dot3(v, v));
    if (length <= 0.000001f) {
        return 0;
    }
    v[0] /= length;
    v[1] /= length;
    v[2] /= length;
    return 1;
}

static inline int voxelpilot_project_point_to_screen(
    const float point[3],
    const float cam_pos[3],
    const float cam_dir[3],
    const float cam_up[3],
    float       fov_y_deg,
    float       viewport_width,
    float       viewport_height,
    float      *out_x,
    float      *out_y,
    float      *out_depth)
{
    float forward[3];
    float up[3];
    float right[3];
    float true_up[3];
    float to_point[3];
    float view_x;
    float view_y;
    float view_z;
    float tan_half_fov;
    float aspect;
    float ndc_x;
    float ndc_y;

    if (!point || !cam_pos || !cam_dir || !cam_up || !out_x || !out_y || !out_depth ||
        viewport_width <= 1.0f || viewport_height <= 1.0f) {
        return 0;
    }

    forward[0] = cam_dir[0];
    forward[1] = cam_dir[1];
    forward[2] = cam_dir[2];
    up[0] = cam_up[0];
    up[1] = cam_up[1];
    up[2] = cam_up[2];
    if (!voxelpilot_normalize3(forward) || !voxelpilot_normalize3(up)) {
        return 0;
    }

    voxelpilot_cross3(forward, up, right);
    if (!voxelpilot_normalize3(right)) {
        return 0;
    }
    voxelpilot_cross3(right, forward, true_up);
    if (!voxelpilot_normalize3(true_up)) {
        return 0;
    }

    to_point[0] = point[0] - cam_pos[0];
    to_point[1] = point[1] - cam_pos[1];
    to_point[2] = point[2] - cam_pos[2];
    view_z = voxelpilot_dot3(to_point, forward);
    if (view_z <= 0.0001f) {
        return 0;
    }

    if (fov_y_deg < 10.0f) fov_y_deg = 10.0f;
    if (fov_y_deg > 120.0f) fov_y_deg = 120.0f;
    tan_half_fov = tanf(fov_y_deg * 0.5f * 3.14159265f / 180.0f);
    if (tan_half_fov <= 0.0001f) {
        return 0;
    }

    aspect = viewport_width / viewport_height;
    view_x = voxelpilot_dot3(to_point, right);
    view_y = voxelpilot_dot3(to_point, true_up);
    ndc_x = view_x / (view_z * tan_half_fov * aspect);
    ndc_y = view_y / (view_z * tan_half_fov);

    if (ndc_x < -1.25f || ndc_x > 1.25f || ndc_y < -1.25f || ndc_y > 1.25f) {
        return 0;
    }

    *out_x = (ndc_x * 0.5f + 0.5f) * viewport_width;
    *out_y = (0.5f - ndc_y * 0.5f) * viewport_height;
    *out_depth = view_z;
    return 1;
}

static inline int voxelpilot_callout_should_show_rank(
    int rank,
    int top_count,
    int selected_only,
    int is_selected)
{
    if (selected_only) {
        return is_selected ? 1 : 0;
    }
    if (top_count < 1) top_count = 1;
    if (rank < 1) return is_selected ? 1 : 0;
    return rank <= top_count || is_selected;
}

#ifdef __cplusplus
}
#endif

#endif /* UI_STATE_HELPERS_H */
