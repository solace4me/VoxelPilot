#ifndef VOXELPILOT_RENDER_COMMAND_BUILDER_H
#define VOXELPILOT_RENDER_COMMAND_BUILDER_H

#include <string.h>

#include "renderer_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float cam_pos[3];
    float cam_dir[3];
    float cam_up[3];
    float fov_y_degrees;
    float light_pos[3];

    float step_size;
    float threshold;
    float empty_space_skip_mult;
    float tf_center;
    float tf_width;
    float tf_opacity_scale;
    int   tf_palette;
    int   tf_invert;

    int   cinematic_enabled;
    float cinematic_rim_strength;
    float cinematic_glow_strength;
    float cinematic_contrast;
    float clip_min[3];
    float clip_max[3];

    int   render_width;
    int   render_height;
} VoxelPilotRenderCommandConfig;
typedef struct {
    int                  visible;
    const unsigned char *label_mask;
    int                  label_mask_width;
    int                  label_mask_height;
    int                  label_mask_depth;
    int                  renderer_width;
    int                  renderer_height;
    int                  renderer_depth;
    int                  label_count;
    float                alpha;
    uint32_t             revision;
    int                  selected_enabled;
    unsigned char        selected_label_id;
    float                selected_emphasis;
    float                colors[RENDERER_MAX_LABELS * 3];
} VoxelPilotRenderLabelOverlayConfig;

static inline void voxelpilot_render_command_config_defaults(
    VoxelPilotRenderCommandConfig *config)
{
    if (!config) return;
    memset(config, 0, sizeof(*config));

    config->cam_pos[0] = 0.5f;
    config->cam_pos[1] = 0.5f;
    config->cam_pos[2] = -1.0f;
    config->cam_dir[0] = 0.0f;
    config->cam_dir[1] = 0.0f;
    config->cam_dir[2] = 1.0f;
    config->cam_up[0] = 0.0f;
    config->cam_up[1] = 1.0f;
    config->cam_up[2] = 0.0f;
    config->fov_y_degrees = 45.0f;

    config->light_pos[0] = 1.5f;
    config->light_pos[1] = 1.5f;
    config->light_pos[2] = -1.0f;

    config->step_size = 0.0020f;
    config->threshold = 0.96f;
    config->empty_space_skip_mult = 1.50f;
    config->tf_center = 0.45f;
    config->tf_width = 0.35f;
    config->tf_opacity_scale = 1.8f;

    config->cinematic_enabled = 0;
    config->cinematic_rim_strength = 0.35f;
    config->cinematic_glow_strength = 0.20f;
    config->cinematic_contrast = 1.10f;

    config->clip_min[0] = 0.0f;
    config->clip_min[1] = 0.0f;
    config->clip_min[2] = 0.0f;
    config->clip_max[0] = 1.0f;
    config->clip_max[1] = 1.0f;
    config->clip_max[2] = 1.0f;

    config->render_width = 1024;
    config->render_height = 1024;
}

static inline void voxelpilot_render_label_overlay_config_defaults(
    VoxelPilotRenderLabelOverlayConfig *config)
{
    if (!config) return;
    memset(config, 0, sizeof(*config));
    config->alpha = 0.55f;
    config->selected_emphasis = 0.0f;
}
static inline float voxelpilot_render_command_fov_radians(float fov_y_degrees)
{
    if (fov_y_degrees <= 0.0f || fov_y_degrees >= 179.0f) {
        fov_y_degrees = 45.0f;
    }
    return fov_y_degrees * 3.14159265f / 180.0f;
}

static inline int voxelpilot_build_renderer_input(
    const VoxelPilotRenderCommandConfig *config,
    RendererInput                       *cmd)
{
    if (!config || !cmd ||
        config->render_width < 1 ||
        config->render_height < 1) {
        return 0;
    }

    memset(cmd, 0, sizeof(*cmd));
    memcpy(cmd->cam_pos, config->cam_pos, sizeof(cmd->cam_pos));
    memcpy(cmd->cam_dir, config->cam_dir, sizeof(cmd->cam_dir));
    memcpy(cmd->cam_up, config->cam_up, sizeof(cmd->cam_up));
    memcpy(cmd->light_pos, config->light_pos, sizeof(cmd->light_pos));
    memcpy(cmd->clip_min, config->clip_min, sizeof(cmd->clip_min));
    memcpy(cmd->clip_max, config->clip_max, sizeof(cmd->clip_max));

    cmd->fov_y = voxelpilot_render_command_fov_radians(config->fov_y_degrees);
    cmd->step_size = config->step_size;
    cmd->threshold = config->threshold;
    cmd->empty_space_skip_mult = config->empty_space_skip_mult;
    cmd->tf_center = config->tf_center;
    cmd->tf_width = config->tf_width;
    cmd->tf_opacity_scale = config->tf_opacity_scale;
    cmd->tf_palette = config->tf_palette;
    cmd->tf_invert = config->tf_invert;
    cmd->cinematic_enabled = config->cinematic_enabled ? 1 : 0;
    cmd->cinematic_rim_strength = config->cinematic_rim_strength;
    cmd->cinematic_glow_strength = config->cinematic_glow_strength;
    cmd->cinematic_contrast = config->cinematic_contrast;
    cmd->img_width = (uint32_t)config->render_width;
    cmd->img_height = (uint32_t)config->render_height;
    return 1;
}

static inline int voxelpilot_attach_label_overlay_to_renderer_input(
    const VoxelPilotRenderLabelOverlayConfig *config,
    RendererInput                            *cmd)
{
    int i;
    int label_count;

    if (!config || !cmd ||
        !config->visible ||
        !config->label_mask ||
        config->label_count <= 0 ||
        config->label_mask_width < 1 ||
        config->label_mask_height < 1 ||
        config->label_mask_depth < 1 ||
        config->label_mask_width != config->renderer_width ||
        config->label_mask_height != config->renderer_height ||
        config->label_mask_depth != config->renderer_depth) {
        return 0;
    }

    label_count = config->label_count;
    if (label_count > RENDERER_MAX_LABELS) {
        label_count = RENDERER_MAX_LABELS;
    }

    cmd->label_mask = config->label_mask;
    cmd->labels.enabled = 1;
    cmd->labels.width = config->label_mask_width;
    cmd->labels.height = config->label_mask_height;
    cmd->labels.depth = config->label_mask_depth;
    cmd->labels.label_count = label_count;
    cmd->labels.alpha = config->alpha;
    cmd->labels.revision = config->revision;

    if (config->selected_enabled) {
        cmd->labels.selected_label_id = config->selected_label_id;
        cmd->labels.selected_emphasis = config->selected_emphasis;
    }

    for (i = 0; i < RENDERER_MAX_LABELS * 3; ++i) {
        cmd->labels.colors[i] = config->colors[i];
    }

    return 1;
}
#ifdef __cplusplus
}
#endif

#endif