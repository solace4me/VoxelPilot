#ifndef VOXELPILOT_HISTOGRAM_PROFILE_H
#define VOXELPILOT_HISTOGRAM_PROFILE_H

#include <stddef.h>
#include <string.h>

typedef struct {
    float  min_value;
    float  max_value;
    float  peak_count;
    size_t voxel_count;
    int    ready;
} VoxelPilotHistogramProfile;

static inline void voxelpilot_histogram_profile_reset(
    float                       *histogram,
    int                          bin_count,
    VoxelPilotHistogramProfile  *profile)
{
    if (histogram && bin_count > 0) {
        memset(histogram, 0, sizeof(float) * (size_t)bin_count);
    }
    if (profile) {
        memset(profile, 0, sizeof(*profile));
    }
}

static inline int voxelpilot_compute_histogram_profile(
    const float                 *volume_data,
    int                          width,
    int                          height,
    int                          depth,
    float                       *histogram,
    int                          bin_count,
    VoxelPilotHistogramProfile  *profile)
{
    size_t voxel_count;
    size_t i;
    int bin_index;
    float min_value;
    float max_value;
    float range;
    float peak_count;

    voxelpilot_histogram_profile_reset(histogram, bin_count, profile);

    if (!volume_data || !histogram || !profile ||
        width < 1 || height < 1 || depth < 1 || bin_count < 2) {
        return 0;
    }

    voxel_count = (size_t)width * (size_t)height * (size_t)depth;
    if (voxel_count == 0) {
        return 0;
    }

    min_value = volume_data[0];
    max_value = volume_data[0];
    for (i = 1; i < voxel_count; ++i) {
        float value = volume_data[i];
        if (value < min_value) min_value = value;
        if (value > max_value) max_value = value;
    }

    range = max_value - min_value;
    if (range < 1e-6f) {
        histogram[0] = (float)voxel_count;
    } else {
        for (i = 0; i < voxel_count; ++i) {
            float normalized = (volume_data[i] - min_value) / range;
            bin_index = (int)(normalized * (float)(bin_count - 1));
            if (bin_index < 0) bin_index = 0;
            if (bin_index >= bin_count) bin_index = bin_count - 1;
            histogram[bin_index] += 1.0f;
        }
    }

    peak_count = 0.0f;
    for (bin_index = 0; bin_index < bin_count; ++bin_index) {
        if (histogram[bin_index] > peak_count) {
            peak_count = histogram[bin_index];
        }
    }

    profile->min_value = min_value;
    profile->max_value = max_value;
    profile->peak_count = peak_count;
    profile->voxel_count = voxel_count;
    profile->ready = 1;
    return 1;
}

#endif