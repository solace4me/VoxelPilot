#ifndef RENDERER_API_H
#define RENDERER_API_H

#include "volume_structs.h"

typedef struct {
    float    cam_pos[3];
    float    cam_dir[3];
    float    cam_up[3];
    float    fov_y;

    float    light_pos[3];

    float    step_size;
    float    threshold;
    float    empty_space_skip_mult;
    float    tf_center;
    float    tf_width;
    float    tf_opacity_scale;
    int      tf_palette;
    int      tf_invert;
    float    clip_min[3];
    float    clip_max[3];

    uint32_t img_width;
    uint32_t img_height;
} RendererInput;

typedef struct {
    /* Volume data (managed memory) */
    float          *volume_data;
    size_t          vol_bytes;

    /* Volume dimensions */
    int             W, H, D;

    /* Volume params */
    VolumeParams    vparams;

    /* Brick data */
    BrickInfo      *h_bricks;
    BrickInfo      *d_bricks;
    BrickGrid       h_grid;
    BrickGrid      *d_grid;
    int             brick_dim;

    /* Texture resources */
    VolumeResources volRes;

    /* Device output buffer (grayscale) */
    unsigned char  *d_out;
    int             img_w;
    int             img_h;

    /* Host output buffer (pinned) */
    unsigned char  *h_out;

    /* Camera */
    CameraParams    cam;

    /* Light */
    float3          lightPos;

    /* CUDA stream for rendering */
    cudaStream_t    stream;

    /* Frame counter */
    uint32_t        frame_id;
} RendererState;

void renderer_state_init(
    RendererState *st,
    const char    *volume_path,
    int            dim,
    int            brick_dim);

void renderer_state_cleanup(RendererState *st);

float render_frame_gpu(RendererState *st);

void apply_render_command(
    RendererState       *st,
    const RendererInput *cmd);

int reload_volume(
    RendererState *st,
    const float   *new_data,
    int            new_dim);

#endif /* RENDERER_API_H */
