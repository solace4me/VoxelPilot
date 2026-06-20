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
    if (previous_width <= 0.0f || previous_height <= 0.0f) {
        return 1;
    }

    if (current_width <= 0.0f || current_height <= 0.0f) {
        return 0;
    }

    if (current_width < previous_width - 0.5f ||
        current_width > previous_width + 0.5f) {
        return 1;
    }

    if (current_height < previous_height - 0.5f ||
        current_height > previous_height + 0.5f) {
        return 1;
    }

    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* UI_STATE_HELPERS_H */
