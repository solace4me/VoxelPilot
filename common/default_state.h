#ifndef VOXELPILOT_DEFAULT_STATE_H
#define VOXELPILOT_DEFAULT_STATE_H

#include <string.h>

#include "ui_state_helpers.h"

#define VOXELPILOT_DEFAULT_WIN_W 1600
#define VOXELPILOT_DEFAULT_WIN_H 900
#define VOXELPILOT_DEFAULT_RENDER_W 512
#define VOXELPILOT_DEFAULT_RENDER_H 512
#define VOXELPILOT_DEFAULT_MAX_ANNOTATIONS 8
#define VOXELPILOT_DEFAULT_SELECTED_RESOLUTION 1
#define VOXELPILOT_DEFAULT_RIBBON_TAB 0

typedef struct {
    int   window_width;
    int   window_height;
    int   render_width;
    int   render_height;
    int   render_match_window;
    int   layout_refit_next_frame;
    int   window_was_maximized;
    int   selected_resolution;
    int   ribbon_expanded;
    int   ribbon_active_tab;
    int   adaptive_quality_enabled;
    int   paused;
    float camera_pos[3];
    float camera_dir[3];
    float camera_up[3];
    float fov_y_degrees;
    float light_pos[3];
    int   cinematic_enabled;
    float cinematic_rim_strength;
    float cinematic_glow_strength;
    float cinematic_contrast;
    float tf_center;
    float tf_width;
    float tf_opacity_scale;
    int   tf_palette;
    int   tf_invert;
    float clip_min[3];
    float clip_max[3];
    float slice[3];
    float measure_a[3];
    float measure_b[3];
    int   measurement_visible;
    int   measurement_target;
    int   annotation_mode;
    float annotation_tolerance;
    float annotation_color[3];
    int   label_overlay_visible;
    float label_overlay_alpha;
    int   label_selected_emphasis_enabled;
    float label_selected_emphasis_strength;
    int   semantic_max_regions;
    int   semantic_min_region_voxels;
    float semantic_sensitivity;
    int   semantic_cinematic_emphasis;
    int   semantic_callouts_enabled;
    int   semantic_callout_top_regions;
    int   semantic_callout_selected_only;
    float semantic_callout_opacity;
    float voxel_spacing[3];
    float orbit_target[3];
} VoxelPilotDefaultState;

static inline void voxelpilot_default_state_init(VoxelPilotDefaultState *state)
{
    VoxelPilotCinematicParams cinematic;

    if (!state) {
        return;
    }

    memset(state, 0, sizeof(*state));
    voxelpilot_cinematic_defaults(&cinematic);

    state->window_width = VOXELPILOT_DEFAULT_WIN_W;
    state->window_height = VOXELPILOT_DEFAULT_WIN_H;
    state->render_width = VOXELPILOT_DEFAULT_RENDER_W;
    state->render_height = VOXELPILOT_DEFAULT_RENDER_H;
    state->render_match_window = 1;
    state->layout_refit_next_frame = 1;
    state->window_was_maximized = 0;
    state->selected_resolution = VOXELPILOT_DEFAULT_SELECTED_RESOLUTION;
    state->ribbon_expanded = 1;
    state->ribbon_active_tab = VOXELPILOT_DEFAULT_RIBBON_TAB;
    state->adaptive_quality_enabled = 1;
    state->paused = 0;

    state->camera_pos[0] = -0.35f;
    state->camera_pos[1] = 0.95f;
    state->camera_pos[2] = -0.35f;
    state->camera_dir[0] = 0.7f;
    state->camera_dir[1] = -0.35f;
    state->camera_dir[2] = 0.7f;
    state->camera_up[0] = 0.0f;
    state->camera_up[1] = 1.0f;
    state->camera_up[2] = 0.0f;
    state->fov_y_degrees = 45.0f;

    state->light_pos[0] = 1.5f;
    state->light_pos[1] = 1.5f;
    state->light_pos[2] = -1.0f;

    state->cinematic_enabled = cinematic.enabled;
    state->cinematic_rim_strength = cinematic.rim_strength;
    state->cinematic_glow_strength = cinematic.glow_strength;
    state->cinematic_contrast = cinematic.contrast;

    state->tf_center = 0.45f;
    state->tf_width = 0.35f;
    state->tf_opacity_scale = 1.8f;
    state->tf_palette = 0;
    state->tf_invert = 0;

    state->clip_max[0] = 1.0f;
    state->clip_max[1] = 1.0f;
    state->clip_max[2] = 1.0f;

    state->slice[0] = 0.5f;
    state->slice[1] = 0.5f;
    state->slice[2] = 0.5f;

    state->measure_a[0] = 0.25f;
    state->measure_a[1] = 0.50f;
    state->measure_a[2] = 0.50f;
    state->measure_b[0] = 0.75f;
    state->measure_b[1] = 0.50f;
    state->measure_b[2] = 0.50f;
    state->measurement_visible = 1;
    state->measurement_target = 0;

    state->annotation_mode = 0;
    state->annotation_tolerance = 0.035f;
    state->annotation_color[0] = 0.95f;
    state->annotation_color[1] = 0.28f;
    state->annotation_color[2] = 0.16f;

    state->label_overlay_visible = 1;
    state->label_overlay_alpha = 0.55f;
    state->label_selected_emphasis_enabled = 1;
    state->label_selected_emphasis_strength = 0.65f;

    state->semantic_max_regions = VOXELPILOT_DEFAULT_MAX_ANNOTATIONS;
    state->semantic_min_region_voxels = 128;
    state->semantic_sensitivity = 0.55f;
    state->semantic_cinematic_emphasis = 1;
    state->semantic_callouts_enabled = 1;
    state->semantic_callout_top_regions = 3;
    state->semantic_callout_selected_only = 0;
    state->semantic_callout_opacity = 0.88f;

    state->voxel_spacing[0] = 1.0f;
    state->voxel_spacing[1] = 1.0f;
    state->voxel_spacing[2] = 1.0f;

    state->orbit_target[0] = 0.5f;
    state->orbit_target[1] = 0.5f;
    state->orbit_target[2] = 0.5f;
}

#endif