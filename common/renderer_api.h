#ifndef RENDERER_API_H
#define RENDERER_API_H

#include "volume_structs.h"

#define RENDERER_MAX_LABELS 8

typedef struct {
    int      enabled;
    int      width;
    int      height;
    int      depth;
    int      label_count;
    float    alpha;
    uint32_t revision;
    float    colors[RENDERER_MAX_LABELS * 3];
} RendererLabelParams;

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

    const unsigned char *label_mask;
    RendererLabelParams labels;
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

    /* Device output buffer (RGBA) */
    uchar4         *d_out;
    int             img_w;
    int             img_h;
    int             out_w;
    int             out_h;

    /* Host output buffer (pinned, RGBA) */
    uchar4         *h_out;

    /* Optional label-mask overlay */
    unsigned char  *d_label_mask;
    size_t          label_mask_bytes;
    uint32_t        label_revision;
    RendererLabelParams labels;

    /* Cached CUDA timing events (created once, reused every frame) */
    cudaEvent_t     start_event;
    cudaEvent_t     stop_event;

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
    int            width,
    int            height,
    int            depth,
    int            brick_dim);

void renderer_state_cleanup(RendererState *st);

float render_frame_gpu(RendererState *st);

void apply_render_command(
    RendererState       *st,
    const RendererInput *cmd);

int reload_volume(
    RendererState *st,
    const float   *new_data,
    int            width,
    int            height,
    int            depth);

#endif /* RENDERER_API_H */
