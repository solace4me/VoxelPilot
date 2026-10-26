/* ============================================================
   renderer/renderer.cu

   Shared CUDA renderer implementation used by the standalone
   VoxelPilot desktop application.
   ============================================================ */

#include <cuda_runtime.h>
#include <device_launch_parameters.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "../common/volume_structs.h"
#include "../common/math_utils.h"
#include "../common/renderer_api.h"
#include "../common/cuda_device_policy.h"


/* ============================================================
   Forward Declarations (other compilation units)
   ============================================================ */

/* From raymarch_kernel.cu */
__global__ void volume_render_kernel(
    uchar4          *output,
    const float     *volume,
    VolumeParams     vparams,
    const BrickInfo *bricks,
    const BrickGrid *grid,
    CameraParams     cam,
    float3           lightPos,
    const unsigned char *label_mask,
    RendererLabelParams labels,
    int              img_w,
    int              img_h);

int set_device_texture_objects(
    cudaTextureObject_t volTex,
    cudaTextureObject_t gradTex);

/* From volume_textures.cu */
int create_volume_and_gradient_textures(
    const float     *volume_data,
    int              W,
    int              H,
    int              D,
    VolumeResources *res);

void release_volume_resources(VolumeResources *res);


/* ============================================================
   CUDA Error Helper
   ============================================================ */

static void renderer_clear_error(RendererState *st)
{
    if (!st) {
        return;
    }
    st->last_error_code = 0;
    st->last_error_message[0] = '\0';
}

static int renderer_fail_cuda(RendererState *st, cudaError_t err, const char *msg)
{
    const char *err_name = cudaGetErrorName(err);
    const char *err_text = cudaGetErrorString(err);
    if (!err_name) err_name = "unknown";
    if (!err_text) err_text = "no error string available";

    if (st) {
        st->last_error_code = (int)err;
        snprintf(st->last_error_message,
                 sizeof(st->last_error_message),
                 "CUDA Error at %s: [%d] %s - %s",
                 msg ? msg : "unknown",
                 (int)err,
                 err_name,
                 err_text);
    }

    fprintf(stderr,
            "CUDA Error at %s: [%d] %s - %s\n",
            msg ? msg : "unknown",
            (int)err,
            err_name,
            err_text);
    cudaGetLastError();
    return 0;
}

static int renderer_check_cuda(RendererState *st, cudaError_t err, const char *msg)
{
    if (err == cudaSuccess) {
        return 1;
    }
    return renderer_fail_cuda(st, err, msg);
}

static int renderer_fail_message(RendererState *st, const char *msg)
{
    const char *safe_msg = msg ? msg : "Renderer operation failed";
    if (st) {
        st->last_error_code = -1;
        snprintf(st->last_error_message,
                 sizeof(st->last_error_message),
                 "%s",
                 safe_msg);
    }
    fprintf(stderr, "%s\n", safe_msg);
    return 0;
}

static int resize_output_buffers(RendererState *st)
{
    size_t output_bytes;

    if (!st) {
        return 0;
    }

    if (st->d_out &&
        st->h_out &&
        st->out_w == st->img_w &&
        st->out_h == st->img_h) {
        return 1;
    }

    if (st->stream &&
        !renderer_check_cuda(st,
                             cudaStreamSynchronize(st->stream),
                             "cudaStreamSynchronize before output resize")) {
        return 0;
    }

    if (st->d_out) {
        cudaFree(st->d_out);
        st->d_out = NULL;
    }
    if (st->h_out) {
        cudaFreeHost(st->h_out);
        st->h_out = NULL;
    }

    output_bytes = (size_t)st->img_w * (size_t)st->img_h * sizeof(uchar4);

    if (!renderer_check_cuda(
            st,
            cudaMalloc((void **)&st->d_out, output_bytes),
            "cudaMalloc(d_out resize)")) {
        return 0;
    }
    if (!renderer_check_cuda(
            st,
            cudaMallocHost((void **)&st->h_out, output_bytes),
            "cudaMallocHost(h_out resize)")) {
        cudaFree(st->d_out);
        st->d_out = NULL;
        return 0;
    }

    st->out_w = st->img_w;
    st->out_h = st->img_h;
    renderer_clear_error(st);
    return 1;
}

static void clear_label_overlay(RendererState *st)
{
    if (!st) {
        return;
    }

    st->labels.enabled = 0;
    st->labels.label_count = 0;
    st->labels.revision = 0;
}

static void free_label_mask(RendererState *st)
{
    if (!st) {
        return;
    }

    if (st->d_label_mask) {
        cudaFree(st->d_label_mask);
        st->d_label_mask = NULL;
    }
    st->label_mask_bytes = 0;
    st->label_revision = 0;
    clear_label_overlay(st);
}

static void sync_label_overlay(RendererState *st, const RendererInput *cmd)
{
    size_t bytes;

    if (!st || !cmd) {
        return;
    }

    clear_label_overlay(st);

    if (!cmd->labels.enabled ||
        !cmd->label_mask ||
        cmd->labels.width != st->W ||
        cmd->labels.height != st->H ||
        cmd->labels.depth != st->D ||
        cmd->labels.label_count < 1) {
        return;
    }

    bytes =
        (size_t)cmd->labels.width *
        (size_t)cmd->labels.height *
        (size_t)cmd->labels.depth;

    if (bytes == 0) {
        return;
    }

    if (!st->d_label_mask || st->label_mask_bytes != bytes) {
        if (st->d_label_mask) {
            cudaFree(st->d_label_mask);
            st->d_label_mask = NULL;
        }
        if (!renderer_check_cuda(
                st,
                cudaMalloc((void **)&st->d_label_mask, bytes),
                "cudaMalloc(d_label_mask)")) {
            st->label_mask_bytes = 0;
            clear_label_overlay(st);
            return;
        }
        st->label_mask_bytes = bytes;
        st->label_revision = 0;
    }

    if (st->label_revision != cmd->labels.revision) {
        if (!renderer_check_cuda(
                st,
                cudaMemcpy(
                    st->d_label_mask,
                    cmd->label_mask,
                    bytes,
                    cudaMemcpyHostToDevice),
                "cudaMemcpy(label_mask)")) {
            clear_label_overlay(st);
            return;
        }
        st->label_revision = cmd->labels.revision;
    }

    st->labels = cmd->labels;
    st->labels.enabled = 1;
    st->labels.alpha =
        st->labels.alpha < 0.0f ? 0.0f :
        (st->labels.alpha > 1.0f ? 1.0f : st->labels.alpha);
    st->labels.selected_emphasis =
        st->labels.selected_emphasis < 0.0f ? 0.0f :
        (st->labels.selected_emphasis > 1.0f ? 1.0f : st->labels.selected_emphasis);
    if (st->labels.label_count > RENDERER_MAX_LABELS) {
        st->labels.label_count = RENDERER_MAX_LABELS;
    }
    if (st->labels.selected_label_id > st->labels.label_count) {
        st->labels.selected_label_id = 0;
    }
}

/* The renderer keeps its float copy of the volume (st->volume_data) in
   ordinary host memory. The kernel never reads it - it samples the 3D
   texture - while the CPU-side features (bricks, histogram, slices,
   picking, ASD) read it constantly. It used to be cudaMallocManaged
   with a GPU-preferred location and a prefetch to the device: 4 bytes
   per voxel of GPU memory spent on a buffer the GPU never used, and CPU
   reads that could have to migrate pages back. */


/* ============================================================
   Generate Synthetic Volume (sphere fallback)
   ============================================================ */

static void generate_synthetic_volume(float *data, int W, int H, int D)
{
    int x, y, z;
    float cx, cy, cz, radius;

    cx = (float)(W - 1) * 0.5f;
    cy = (float)(H - 1) * 0.5f;
    cz = (float)(D - 1) * 0.5f;
    radius = (float)W * 0.3f;

    for (z = 0; z < D; ++z) {
        for (y = 0; y < H; ++y) {
            for (x = 0; x < W; ++x) {
                float dx = (float)x - cx;
                float dy = (float)y - cy;
                float dz = (float)z - cz;
                float d  = sqrtf(dx*dx + dy*dy + dz*dz);
                float val = (d < radius) ? (1.0f - d / radius) : 0.0f;

                size_t idx = (size_t)z * (size_t)W * (size_t)H
                           + (size_t)y * (size_t)W
                           + (size_t)x;
                data[idx] = val;
            }
        }
    }
}


/* ============================================================
   Load Volume From Raw File
   Returns 1 on success, 0 on failure (caller should
   fall back to synthetic).
   ============================================================ */

static int load_volume_file(
    const char *path,
    float *data,
    size_t expected_bytes)
{
    FILE *fp;
    size_t read_bytes;

    fp = fopen(path, "rb");
    if (!fp)
        return 0;

    read_bytes = fread(data, 1, expected_bytes, fp);
    fclose(fp);

    if (read_bytes != expected_bytes) {
        fprintf(stderr,
                "Warning: read %zu bytes, expected %zu\n",
                read_bytes, expected_bytes);
    }

    return 1;
}


/* ============================================================
   Build Brick Grid
   ============================================================ */

static int build_brick_grid(RendererState *st)
{
    int bx, by, bz;
    int brickSizeX, brickSizeY, brickSizeZ;
    int idx;

    st->h_grid.bricksX = st->W / st->brick_dim;
    st->h_grid.bricksY = st->H / st->brick_dim;
    st->h_grid.bricksZ = st->D / st->brick_dim;

    /* Clamp to at least 1 brick per axis */
    if (st->h_grid.bricksX < 1) st->h_grid.bricksX = 1;
    if (st->h_grid.bricksY < 1) st->h_grid.bricksY = 1;
    if (st->h_grid.bricksZ < 1) st->h_grid.bricksZ = 1;

    st->h_grid.numBricks =
        st->h_grid.bricksX *
        st->h_grid.bricksY *
        st->h_grid.bricksZ;

    brickSizeX = st->W / st->h_grid.bricksX;
    brickSizeY = st->H / st->h_grid.bricksY;
    brickSizeZ = st->D / st->h_grid.bricksZ;

    st->h_bricks = (BrickInfo *)malloc(
        (size_t)st->h_grid.numBricks * sizeof(BrickInfo));

    if (!st->h_bricks) {
        fprintf(stderr, "Failed to allocate brick array\n");
        if (st) {
            st->last_error_code = -1;
            snprintf(st->last_error_message, sizeof(st->last_error_message),
                     "Failed to allocate brick array");
        }
        return 0;
    }

    idx = 0;
    for (bz = 0; bz < st->h_grid.bricksZ; ++bz) {
        for (by = 0; by < st->h_grid.bricksY; ++by) {
            for (bx = 0; bx < st->h_grid.bricksX; ++bx) {
                BrickInfo *b = &st->h_bricks[idx];

                b->bx = bx;
                b->by = by;
                b->bz = bz;

                b->offsetX = bx * brickSizeX;
                b->offsetY = by * brickSizeY;
                b->offsetZ = bz * brickSizeZ;

                /* Last brick on each axis absorbs any remainder voxels */
                b->sizeX = (bx < st->h_grid.bricksX - 1)
                    ? brickSizeX : (st->W - b->offsetX);
                b->sizeY = (by < st->h_grid.bricksY - 1)
                    ? brickSizeY : (st->H - b->offsetY);
                b->sizeZ = (bz < st->h_grid.bricksZ - 1)
                    ? brickSizeZ : (st->D - b->offsetZ);

                b->isEmpty = 0;

                ++idx;
            }
        }
    }

    return 1;
}


/* ============================================================
   Mark Empty Bricks (host pre-pass)
   ============================================================ */

static void mark_empty_bricks(RendererState *st, float threshold)
{
    int i;

    for (i = 0; i < st->h_grid.numBricks; ++i) {
        BrickInfo *br = &st->h_bricks[i];
        float maxVal = 0.0f;
        int x, y, z;

        for (z = 0; z < br->sizeZ; ++z) {
            int gz = br->offsetZ + z;

            for (y = 0; y < br->sizeY; ++y) {
                int gy = br->offsetY + y;

                for (x = 0; x < br->sizeX; ++x) {
                    int gx = br->offsetX + x;

                    size_t idx =
                        (size_t)gz * (size_t)st->W * (size_t)st->H
                      + (size_t)gy * (size_t)st->W
                      + (size_t)gx;

                    float v = st->volume_data[idx];
                    if (v > maxVal) maxVal = v;

                    if (maxVal > threshold) goto not_empty;
                }
            }
        }

    not_empty:
        br->isEmpty = (maxVal <= threshold) ? 1 : 0;
    }
}


/* ============================================================
   Upload Brick Data to Device
   ============================================================ */

static int upload_bricks(RendererState *st)
{
    size_t brick_bytes;

    if (!st || !st->h_bricks || st->h_grid.numBricks < 1) {
        return 0;
    }

    brick_bytes = (size_t)st->h_grid.numBricks * sizeof(BrickInfo);

    if (!renderer_check_cuda(
            st,
            cudaMalloc((void **)&st->d_bricks, brick_bytes),
            "cudaMalloc(d_bricks)")) {
        return 0;
    }

    if (!renderer_check_cuda(
            st,
            cudaMemcpy(st->d_bricks, st->h_bricks,
                       brick_bytes, cudaMemcpyHostToDevice),
            "cudaMemcpy(d_bricks)")) {
        cudaFree(st->d_bricks);
        st->d_bricks = NULL;
        return 0;
    }

    if (!renderer_check_cuda(
            st,
            cudaMalloc((void **)&st->d_grid, sizeof(BrickGrid)),
            "cudaMalloc(d_grid)")) {
        cudaFree(st->d_bricks);
        st->d_bricks = NULL;
        return 0;
    }

    if (!renderer_check_cuda(
            st,
            cudaMemcpy(st->d_grid, &st->h_grid,
                       sizeof(BrickGrid), cudaMemcpyHostToDevice),
            "cudaMemcpy(d_grid)")) {
        cudaFree(st->d_bricks);
        st->d_bricks = NULL;
        cudaFree(st->d_grid);
        st->d_grid = NULL;
        return 0;
    }

    return 1;
}


/* ============================================================
   Render One Frame (GPU)
   Returns render time in milliseconds.
   ============================================================ */

float render_frame_gpu(RendererState *st)
{
    float ms = -1.0f;
    dim3 block, grid;

    if (!st || !st->volume_data || !st->d_bricks || !st->d_grid ||
        !st->stream || !st->start_event || !st->stop_event) {
        return -1.0f;
    }

    if (!resize_output_buffers(st)) {
        return -1.0f;
    }

    block.x = 16; block.y = 16; block.z = 1;
    grid.x = (st->img_w + block.x - 1) / block.x;
    grid.y = (st->img_h + block.y - 1) / block.y;
    grid.z = 1;

    if (!renderer_check_cuda(st, cudaEventRecord(st->start_event, st->stream),
                             "cudaEventRecord(start)")) {
        return -1.0f;
    }

    volume_render_kernel<<<grid, block, 0, st->stream>>>(
        st->d_out,
        st->volume_data,
        st->vparams,
        st->d_bricks,
        st->d_grid,
        st->cam,
        st->lightPos,
        st->d_label_mask,
        st->labels,
        st->img_w,
        st->img_h);

    if (!renderer_check_cuda(st, cudaGetLastError(), "volume_render_kernel launch")) {
        return -1.0f;
    }

    if (!renderer_check_cuda(st, cudaEventRecord(st->stop_event, st->stream),
                             "cudaEventRecord(stop)")) {
        return -1.0f;
    }

    if (!renderer_check_cuda(st, cudaEventSynchronize(st->stop_event),
                             "cudaEventSynchronize(stop)")) {
        return -1.0f;
    }

    if (!renderer_check_cuda(st, cudaEventElapsedTime(&ms, st->start_event, st->stop_event),
                             "cudaEventElapsedTime")) {
        return -1.0f;
    }

    /* Copy result to pinned host buffer */
    if (!renderer_check_cuda(
            st,
            cudaMemcpyAsync(st->h_out, st->d_out,
                            (size_t)st->img_w * (size_t)st->img_h * sizeof(uchar4),
                            cudaMemcpyDeviceToHost,
                            st->stream),
            "cudaMemcpyAsync(d_out -> h_out)")) {
        return -1.0f;
    }

    if (!renderer_check_cuda(st, cudaStreamSynchronize(st->stream),
                             "cudaStreamSynchronize after memcpy")) {
        return -1.0f;
    }

    renderer_clear_error(st);
    return ms;
}


/* ============================================================
   Apply Render Command to State
   ============================================================ */

void apply_render_command(
    RendererState       *st,
    const RendererInput *cmd)
{
    st->cam.position = f3_make(
        cmd->cam_pos[0], cmd->cam_pos[1], cmd->cam_pos[2]);

    st->cam.view_dir = f3_normalize(f3_make(
        cmd->cam_dir[0], cmd->cam_dir[1], cmd->cam_dir[2]));

    st->cam.up = f3_normalize(f3_make(
        cmd->cam_up[0], cmd->cam_up[1], cmd->cam_up[2]));

    st->cam.fov_y = cmd->fov_y;

    st->lightPos = f3_make(
        cmd->light_pos[0], cmd->light_pos[1], cmd->light_pos[2]);

    st->vparams.step_size            = cmd->step_size;
    st->vparams.threshold            = cmd->threshold;
    st->vparams.empty_space_skip_mult = cmd->empty_space_skip_mult;
    st->vparams.tf_center            = cmd->tf_center;
    st->vparams.tf_width             = cmd->tf_width;
    st->vparams.tf_opacity_scale     = cmd->tf_opacity_scale;
    st->vparams.tf_palette           = cmd->tf_palette;
    st->vparams.tf_invert            = cmd->tf_invert;
    st->vparams.cinematic_enabled      = cmd->cinematic_enabled ? 1 : 0;
    st->vparams.cinematic_rim_strength = cmd->cinematic_rim_strength;
    st->vparams.cinematic_glow_strength = cmd->cinematic_glow_strength;
    st->vparams.cinematic_contrast      = cmd->cinematic_contrast;
    memcpy(st->vparams.clip_min, cmd->clip_min, sizeof(st->vparams.clip_min));
    memcpy(st->vparams.clip_max, cmd->clip_max, sizeof(st->vparams.clip_max));
    {
        int axis;
        for (axis = 0; axis < 3; ++axis) {
            float e = cmd->box_extent[axis];
            st->vparams.box_extent[axis] = (e > 0.0f && e <= 1.0f) ? e : 1.0f;
        }
    }
    sync_label_overlay(st, cmd);

    /* Clamp requested resolution */
    st->img_w = (int)cmd->img_width;
    st->img_h = (int)cmd->img_height;

    if (st->img_w < 1)   st->img_w = 1;
    if (st->img_h < 1)   st->img_h = 1;
    if (st->img_w > 4096) st->img_w = 4096;
    if (st->img_h > 4096) st->img_h = 4096;

    resize_output_buffers(st);
}

/* ============================================================
   Reload Volume Data
   
   Tears down old GPU resources and rebuilds everything
   from newly supplied host volume data.
   ============================================================ */

int reload_volume(
    RendererState *st,
    const float   *new_data,
    int            width,
    int            height,
    int            depth)
{
    cudaTextureObject_t zeroTex;

    /* Free old textures */
    zeroTex = 0;
    (void)set_device_texture_objects(zeroTex, zeroTex);
    release_volume_resources(&st->volRes);

    /* Free old bricks */
    if (st->d_bricks) { cudaFree(st->d_bricks); st->d_bricks = NULL; }
    if (st->d_grid)   { cudaFree(st->d_grid);   st->d_grid   = NULL; }
    if (st->h_bricks) { free(st->h_bricks);     st->h_bricks = NULL; }
    free_label_mask(st);

    /* Free old volume */
    if (st->volume_data) {
        free(st->volume_data);
        st->volume_data = NULL;
    }

    /* Update dimensions */
    st->W = width;
    st->H = height;
    st->D = depth;

    st->vol_bytes = (size_t)width
                  * (size_t)height
                  * (size_t)depth
                  * sizeof(float);

    st->vparams.width  = width;
    st->vparams.height = height;
    st->vparams.depth  = depth;

    /* Host copy for the CPU-side features (see the note above). */
    st->volume_data = (float *)malloc(st->vol_bytes);
    if (!st->volume_data) {
        return renderer_fail_message(st, "Not enough system memory for this volume.");
    }

    /* Copy uploaded data */
    memcpy(st->volume_data, new_data, st->vol_bytes);

    /* Recreate textures */
    memset(&st->volRes, 0, sizeof(VolumeResources));
    if (!create_volume_and_gradient_textures(
            st->volume_data, st->W, st->H, st->D,
            &st->volRes)) {
        return 0;
    }
    if (!set_device_texture_objects(
            st->volRes.volTex, st->volRes.gradTex)) {
        return renderer_fail_message(st, "Could not bind CUDA texture objects for reloaded volume.");
    }

    /* Rebuild bricks */
    if (!build_brick_grid(st)) {
        return 0;
    }
    mark_empty_bricks(st, 0.01f);
    if (!upload_bricks(st)) {
        return 0;
    }

    printf("Volume reloaded from upload: %dx%dx%d\n",
           st->W, st->H, st->D);

    return 1;
}

int renderer_query_gpu_memory(size_t *free_bytes, size_t *total_bytes)
{
    size_t free_b = 0;
    size_t total_b = 0;

    if (cudaMemGetInfo(&free_b, &total_b) != cudaSuccess) {
        cudaGetLastError();
        return 0;
    }
    if (free_bytes) *free_bytes = free_b;
    if (total_bytes) *total_bytes = total_b;
    return 1;
}

/* ============================================================
   Initialize Renderer State
   ============================================================ */

int renderer_state_init(
    RendererState  *st,
    const char     *volume_path,
    int             width,
    int             height,
    int             depth,
    int             brick_dim)
{
    memset(st, 0, sizeof(RendererState));

    st->W = width;
    st->H = height;
    st->D = depth;
    st->brick_dim = brick_dim;

    st->vol_bytes = (size_t)width * (size_t)height * (size_t)depth * sizeof(float);

    /* Default rendering params */
    st->vparams.width                = width;
    st->vparams.height               = height;
    st->vparams.depth                = depth;
    st->vparams.step_size            = 0.0025f;
    st->vparams.threshold            = 0.95f;
    st->vparams.empty_space_skip_mult = 2.0f;
    st->vparams.tf_center            = 0.45f;
    st->vparams.tf_width             = 0.35f;
    st->vparams.tf_opacity_scale     = 1.8f;
    st->vparams.tf_palette           = 0;
    st->vparams.tf_invert            = 0;
    st->vparams.cinematic_enabled    = 0;
    st->vparams.cinematic_rim_strength = 0.35f;
    st->vparams.cinematic_glow_strength = 0.20f;
    st->vparams.cinematic_contrast   = 1.10f;
    st->vparams.clip_min[0]          = 0.0f;
    st->vparams.clip_min[1]          = 0.0f;
    st->vparams.clip_min[2]          = 0.0f;
    st->vparams.clip_max[0]          = 1.0f;
    st->vparams.clip_max[1]          = 1.0f;
    st->vparams.clip_max[2]          = 1.0f;
    st->vparams.box_extent[0]        = 1.0f;
    st->vparams.box_extent[1]        = 1.0f;
    st->vparams.box_extent[2]        = 1.0f;

    /* Default camera */
    st->cam.position = f3_make(0.5f, 0.5f, -1.0f);
    st->cam.view_dir = f3_make(0.0f, 0.0f, 1.0f);
    st->cam.up       = f3_make(0.0f, 1.0f, 0.0f);
    st->cam.fov_y    = 45.0f * 3.14159265f / 180.0f;

    /* Default light */
    st->lightPos = f3_make(1.5f, 1.5f, -1.0f);

    /* Default output resolution */
    st->img_w = 512;
    st->img_h = 512;

    /* Host copy for the CPU-side features (see the note above
       apply_render_command's neighbours). */
    st->volume_data = (float *)malloc(st->vol_bytes);
    if (!st->volume_data) {
        renderer_fail_message(st, "Not enough system memory for this volume.");
        return 0;
    }

    /* Load or generate volume */
    if (volume_path && load_volume_file(
            volume_path, st->volume_data, st->vol_bytes)) {
        printf("Loaded volume from: %s\n", volume_path);
    } else {
        if (volume_path) {
            printf("Could not open %s, generating synthetic volume\n",
                   volume_path);
        } else {
            printf("No volume file specified, generating synthetic volume\n");
        }
        generate_synthetic_volume(
            st->volume_data, st->W, st->H, st->D);
    }

    /* Create textures */
    memset(&st->volRes, 0, sizeof(VolumeResources));
    if (!create_volume_and_gradient_textures(
            st->volume_data, st->W, st->H, st->D, &st->volRes)) {
        renderer_fail_message(st, "Could not create CUDA volume textures.");
        return 0;
    }
    if (!set_device_texture_objects(st->volRes.volTex, st->volRes.gradTex)) {
        renderer_fail_message(st, "Could not bind CUDA texture objects.");
        return 0;
    }

    /* Build bricks */
    if (!build_brick_grid(st)) {
        return 0;
    }
    mark_empty_bricks(st, 0.01f);
    if (!upload_bricks(st)) {
        return 0;
    }

    /* Allocate output buffers (RGBA) */
    if (!resize_output_buffers(st)) {
        return 0;
    }

    /* Create CUDA stream */
    if (!renderer_check_cuda(
            st,
            cudaStreamCreate(&st->stream),
            "cudaStreamCreate")) {
        return 0;
    }

    /* Create timing events once; reused every frame */
    if (!renderer_check_cuda(
            st,
            cudaEventCreate(&st->start_event),
            "cudaEventCreate(start)")) {
        return 0;
    }
    if (!renderer_check_cuda(
            st,
            cudaEventCreate(&st->stop_event),
            "cudaEventCreate(stop)")) {
        return 0;
    }

    st->frame_id = 0;

    printf("Renderer initialized: %dx%dx%d, brick=%d\n",
           st->W, st->H, st->D, st->brick_dim);
    return 1;
}


/* ============================================================
   Cleanup Renderer State
   ============================================================ */

void renderer_state_cleanup(RendererState *st)
{
    cudaTextureObject_t zeroTex;

    if (!st) return;

    if (st->stream) {
        cudaStreamSynchronize(st->stream);
        cudaStreamDestroy(st->stream);
        st->stream = NULL;
    }

    if (st->start_event) {
        cudaEventDestroy(st->start_event);
        st->start_event = NULL;
    }
    if (st->stop_event) {
        cudaEventDestroy(st->stop_event);
        st->stop_event = NULL;
    }

    if (st->d_out)  { cudaFree(st->d_out);  st->d_out = NULL; }
    if (st->h_out)  { cudaFreeHost(st->h_out); st->h_out = NULL; }
    free_label_mask(st);

    if (st->d_bricks) { cudaFree(st->d_bricks); st->d_bricks = NULL; }
    if (st->d_grid)   { cudaFree(st->d_grid);   st->d_grid = NULL; }
    if (st->h_bricks) { free(st->h_bricks);     st->h_bricks = NULL; }

    zeroTex = 0;
    (void)set_device_texture_objects(zeroTex, zeroTex);
    release_volume_resources(&st->volRes);

    if (st->volume_data) {
        free(st->volume_data);
        st->volume_data = NULL;
    }

    printf("Renderer cleaned up.\n");
}

