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

void set_device_texture_objects(
    cudaTextureObject_t volTex,
    cudaTextureObject_t gradTex);

/* From volume_textures.cu */
void create_volume_and_gradient_textures(
    const float     *volume_data,
    int              W,
    int              H,
    int              D,
    VolumeResources *res);

void release_volume_resources(VolumeResources *res);


/* ============================================================
   CUDA Error Helper
   ============================================================ */

static void check_cuda(cudaError_t err, const char *msg)
{
    if (err != cudaSuccess) {
        const char *err_name = cudaGetErrorName(err);
        const char *err_text = cudaGetErrorString(err);
        if (!err_name) err_name = "unknown";
        if (!err_text) err_text = "no error string available";
        fprintf(stderr,
                "CUDA Error at %s: [%d] %s - %s\n",
                msg, (int)err, err_name, err_text);
        exit(-1);
    }
}

static void warn_cuda_nonfatal(cudaError_t err, const char *msg)
{
    const char *err_name = cudaGetErrorName(err);
    const char *err_text = cudaGetErrorString(err);
    if (!err_name) err_name = "unknown";
    if (!err_text) err_text = "no error string available";
    fprintf(stderr,
            "Warning: CUDA optimization skipped at %s: [%d] %s - %s\n",
            msg, (int)err, err_name, err_text);
    cudaGetLastError();
}

static void resize_output_buffers(RendererState *st)
{
    size_t output_bytes;

    if (!st) {
        return;
    }

    if (st->d_out &&
        st->h_out &&
        st->out_w == st->img_w &&
        st->out_h == st->img_h) {
        return;
    }

    if (st->stream) {
        check_cuda(cudaStreamSynchronize(st->stream),
                   "cudaStreamSynchronize before output resize");
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

    check_cuda(
        cudaMalloc((void **)&st->d_out, output_bytes),
        "cudaMalloc(d_out resize)");
    check_cuda(
        cudaMallocHost((void **)&st->h_out, output_bytes),
        "cudaMallocHost(h_out resize)");

    st->out_w = st->img_w;
    st->out_h = st->img_h;
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
        check_cuda(
            cudaMalloc((void **)&st->d_label_mask, bytes),
            "cudaMalloc(d_label_mask)");
        st->label_mask_bytes = bytes;
        st->label_revision = 0;
    }

    if (st->label_revision != cmd->labels.revision) {
        check_cuda(
            cudaMemcpy(
                st->d_label_mask,
                cmd->label_mask,
                bytes,
                cudaMemcpyHostToDevice),
            "cudaMemcpy(label_mask)");
        st->label_revision = cmd->labels.revision;
    }

    st->labels = cmd->labels;
    st->labels.enabled = 1;
    st->labels.alpha =
        st->labels.alpha < 0.0f ? 0.0f :
        (st->labels.alpha > 1.0f ? 1.0f : st->labels.alpha);
    if (st->labels.label_count > RENDERER_MAX_LABELS) {
        st->labels.label_count = RENDERER_MAX_LABELS;
    }
}

static cudaMemLocation make_device_location(int device)
{
    cudaMemLocation location;
    location.type = cudaMemLocationTypeDevice;
    location.id = device;
    return location;
}

static void apply_managed_memory_hints(
    float *volume_data,
    size_t volume_bytes,
    int device,
    const char *stage)
{
    cudaMemLocation device_location;
    cudaError_t err;
    char advise_label[64];
    char prefetch_label[64];

    device_location = make_device_location(device);

    snprintf(advise_label, sizeof(advise_label),
             "cudaMemAdvise(%s)", stage);
    err = cudaMemAdvise(
        volume_data, volume_bytes,
        cudaMemAdviseSetPreferredLocation, device_location);
    if (err != cudaSuccess) {
        warn_cuda_nonfatal(err, advise_label);
        return;
    }

    snprintf(prefetch_label, sizeof(prefetch_label),
             "cudaMemPrefetchAsync(%s)", stage);
    err = cudaMemPrefetchAsync(
        volume_data, volume_bytes, device_location, 0, NULL);
    if (err != cudaSuccess) {
        warn_cuda_nonfatal(err, prefetch_label);
    }
}


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

static void build_brick_grid(RendererState *st)
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
        exit(-1);
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
            if (gz < 0 || gz >= st->D) continue;

            for (y = 0; y < br->sizeY; ++y) {
                int gy = br->offsetY + y;
                if (gy < 0 || gy >= st->H) continue;

                for (x = 0; x < br->sizeX; ++x) {
                    int gx = br->offsetX + x;
                    if (gx < 0 || gx >= st->W) continue;

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

static void upload_bricks(RendererState *st)
{
    size_t brick_bytes;

    brick_bytes = (size_t)st->h_grid.numBricks * sizeof(BrickInfo);

    check_cuda(
        cudaMalloc((void **)&st->d_bricks, brick_bytes),
        "cudaMalloc(d_bricks)");

    check_cuda(
        cudaMemcpy(st->d_bricks, st->h_bricks,
                   brick_bytes, cudaMemcpyHostToDevice),
        "cudaMemcpy(d_bricks)");

    check_cuda(
        cudaMalloc((void **)&st->d_grid, sizeof(BrickGrid)),
        "cudaMalloc(d_grid)");

    check_cuda(
        cudaMemcpy(st->d_grid, &st->h_grid,
                   sizeof(BrickGrid), cudaMemcpyHostToDevice),
        "cudaMemcpy(d_grid)");
}


/* ============================================================
   Render One Frame (GPU)
   Returns render time in milliseconds.
   ============================================================ */

float render_frame_gpu(RendererState *st)
{
    float ms;
    dim3 block, grid;

    resize_output_buffers(st);

    block.x = 16; block.y = 16; block.z = 1;
    grid.x = (st->img_w + block.x - 1) / block.x;
    grid.y = (st->img_h + block.y - 1) / block.y;
    grid.z = 1;

    check_cuda(cudaEventRecord(st->start_event, st->stream),
               "cudaEventRecord(start)");

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

    check_cuda(cudaGetLastError(), "volume_render_kernel launch");

    check_cuda(cudaEventRecord(st->stop_event, st->stream),
               "cudaEventRecord(stop)");

    check_cuda(cudaEventSynchronize(st->stop_event),
               "cudaEventSynchronize(stop)");

    check_cuda(cudaEventElapsedTime(&ms, st->start_event, st->stop_event),
               "cudaEventElapsedTime");

    /* Copy result to pinned host buffer */
    check_cuda(
        cudaMemcpyAsync(st->h_out, st->d_out,
                        (size_t)st->img_w * (size_t)st->img_h * sizeof(uchar4),
                        cudaMemcpyDeviceToHost,
                        st->stream),
        "cudaMemcpyAsync(d_out -> h_out)");

    check_cuda(cudaStreamSynchronize(st->stream),
               "cudaStreamSynchronize after memcpy");

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
    memcpy(st->vparams.clip_min, cmd->clip_min, sizeof(st->vparams.clip_min));
    memcpy(st->vparams.clip_max, cmd->clip_max, sizeof(st->vparams.clip_max));
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
    set_device_texture_objects(zeroTex, zeroTex);
    release_volume_resources(&st->volRes);

    /* Free old bricks */
    if (st->d_bricks) { cudaFree(st->d_bricks); st->d_bricks = NULL; }
    if (st->d_grid)   { cudaFree(st->d_grid);   st->d_grid   = NULL; }
    if (st->h_bricks) { free(st->h_bricks);     st->h_bricks = NULL; }
    free_label_mask(st);

    /* Free old volume */
    if (st->volume_data) {
        cudaFree(st->volume_data);
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

    /* Allocate new managed memory */
    check_cuda(
        cudaMallocManaged(
            (void **)&st->volume_data,
            st->vol_bytes,
            cudaMemAttachGlobal),
        "cudaMallocManaged(reload)");

    /* Copy uploaded data */
    memcpy(st->volume_data, new_data, st->vol_bytes);

    /* Recreate textures */
    memset(&st->volRes, 0, sizeof(VolumeResources));
    create_volume_and_gradient_textures(
        st->volume_data, st->W, st->H, st->D,
        &st->volRes);
    set_device_texture_objects(
        st->volRes.volTex, st->volRes.gradTex);

    /* Unified memory hints are optional on some Windows driver setups. */
    apply_managed_memory_hints(
        st->volume_data, st->vol_bytes, 0, "reload");

    /* Rebuild bricks */
    build_brick_grid(st);
    mark_empty_bricks(st, 0.01f);
    upload_bricks(st);

    printf("Volume reloaded from upload: %dx%dx%d\n",
           st->W, st->H, st->D);

    return 1;
}

/* ============================================================
   Initialize Renderer State
   ============================================================ */

void renderer_state_init(
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
    st->vparams.tf_opacity_scale     = 1.0f;
    st->vparams.tf_palette           = 0;
    st->vparams.tf_invert            = 0;
    st->vparams.clip_min[0]          = 0.0f;
    st->vparams.clip_min[1]          = 0.0f;
    st->vparams.clip_min[2]          = 0.0f;
    st->vparams.clip_max[0]          = 1.0f;
    st->vparams.clip_max[1]          = 1.0f;
    st->vparams.clip_max[2]          = 1.0f;

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

    /* Allocate managed memory for volume */
    check_cuda(
        cudaMallocManaged(
            (void **)&st->volume_data,
            st->vol_bytes,
            cudaMemAttachGlobal),
        "cudaMallocManaged(volume_data)");

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
    create_volume_and_gradient_textures(
        st->volume_data, st->W, st->H, st->D, &st->volRes);
    set_device_texture_objects(st->volRes.volTex, st->volRes.gradTex);

    /* Unified memory hints are optional on some Windows driver setups. */
    apply_managed_memory_hints(
        st->volume_data, st->vol_bytes, 0, "init");

    /* Build bricks */
    build_brick_grid(st);
    mark_empty_bricks(st, 0.01f);
    upload_bricks(st);

    /* Allocate output buffers (RGBA) */
    check_cuda(
        cudaMalloc((void **)&st->d_out,
                   (size_t)st->img_w * (size_t)st->img_h * sizeof(uchar4)),
        "cudaMalloc(d_out)");
    check_cuda(
        cudaMallocHost((void **)&st->h_out,
                       (size_t)st->img_w * (size_t)st->img_h * sizeof(uchar4)),
        "cudaMallocHost(h_out)");

    /* Create CUDA stream */
    check_cuda(
        cudaStreamCreate(&st->stream),
        "cudaStreamCreate");

    /* Create timing events once; reused every frame */
    check_cuda(
        cudaEventCreate(&st->start_event),
        "cudaEventCreate(start)");
    check_cuda(
        cudaEventCreate(&st->stop_event),
        "cudaEventCreate(stop)");

    st->frame_id = 0;

    printf("Renderer initialized: %dx%dx%d, brick=%d\n",
           st->W, st->H, st->D, st->brick_dim);
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
    set_device_texture_objects(zeroTex, zeroTex);
    release_volume_resources(&st->volRes);

    if (st->volume_data) {
        cudaFree(st->volume_data);
        st->volume_data = NULL;
    }

    printf("Renderer cleaned up.\n");
}

