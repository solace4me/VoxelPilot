#ifndef UI_STATE_HELPERS_H
#define UI_STATE_HELPERS_H

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

#ifdef __cplusplus
}
#endif

#endif /* UI_STATE_HELPERS_H */
