#include <cuda_runtime.h>
#include <device_launch_parameters.h>

#include <stdio.h>
#include <stdlib.h>

#include "../common/volume_structs.h"
#include "../common/renderer_api.h"
#include "../common/math_utils.h"

#ifndef NO_CUDA

/* ============================================================
   Device Texture Symbols
   ============================================================ */

__device__ cudaTextureObject_t devVolumeTex = 0;
__device__ cudaTextureObject_t devGradTex   = 0;


/* ============================================================
   Device Helper Functions
   ============================================================ */

__device__ static int brick_index_3d(
    int bx, int by, int bz,
    const BrickGrid *grid)
{
    if (bx < 0 || by < 0 || bz < 0 ||
        bx >= grid->bricksX ||
        by >= grid->bricksY ||
        bz >= grid->bricksZ)
        return -1;

    return bz * (grid->bricksY * grid->bricksX)
         + by * grid->bricksX
         + bx;
}


__device__ static const BrickInfo* find_brick_for_pos(
    const BrickInfo *bricks,
    const BrickGrid *grid,
    float3 posTex)
{
    int bx = (int)(posTex.x * (float)grid->bricksX);
    int by = (int)(posTex.y * (float)grid->bricksY);
    int bz = (int)(posTex.z * (float)grid->bricksZ);

    int idx = brick_index_3d(bx, by, bz, grid);
    if (idx < 0)
        return 0;

    if (bricks[idx].isEmpty)
        return 0;

    return &bricks[idx];
}


/* ============================================================
   Texture Sampling
   ============================================================ */

__device__ static float sample_volume_tex(
    float fx, float fy, float fz)
{
    if (devVolumeTex == 0)
        return 0.0f;

    return tex3D<float>(
        devVolumeTex,
        fx + 0.5f,
        fy + 0.5f,
        fz + 0.5f);
}


/* The gradient texture holds per-voxel-index differences. For the
   lighting normal it has to be a world-space gradient: by the chain rule
   each component is scaled by voxels-per-world-unit on its axis
   ((n - 1) / box extent). On a cube that scale is uniform and the
   normal is unchanged; on anisotropic data (thin slices, long scans) it
   is what keeps surfaces shaded at their true angle. */
__device__ static float3 sample_gradient_tex(
    float fx, float fy, float fz,
    float3 index_to_world_scale)
{
    float3 out;

    if (devGradTex == 0) {
        out.x = 0.0f;
        out.y = 0.0f;
        out.z = 1.0f;
        return out;
    }

    float4 g = tex3D<float4>(
        devGradTex,
        fx + 0.5f,
        fy + 0.5f,
        fz + 0.5f);

    out.x = g.x * index_to_world_scale.x;
    out.y = g.y * index_to_world_scale.y;
    out.z = g.z * index_to_world_scale.z;

    return f3_normalize(out);
}

/* Stable per-pixel offset in [0, 1). Starting each ray a fraction of a
   step in replaces the "wood-grain" banding of a coarse step with fine
   noise, so laptops can run larger steps before artefacts show. It is a
   fixed hash, not random, so a still frame is identical every time. */
__device__ static float pixel_jitter(int x, int y)
{
    unsigned int h = ((unsigned int)x * 73856093u) ^ ((unsigned int)y * 19349663u);
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return (float)(h & 0xffffu) / 65536.0f;
}

/* Slab test of a ray against an axis-aligned box. On a hit, [*t_enter,
   *t_exit] is the part of the ray inside the box, starting no earlier
   than the camera. */
__device__ static int ray_box_interval(
    float3 origin,
    float3 dir,
    float3 box_min,
    float3 box_max,
    float *t_enter,
    float *t_exit)
{
    const float o[3]  = { origin.x,  origin.y,  origin.z };
    const float d[3]  = { dir.x,     dir.y,     dir.z };
    const float lo[3] = { box_min.x, box_min.y, box_min.z };
    const float hi[3] = { box_max.x, box_max.y, box_max.z };
    float t_near = -1e30f;
    float t_far = 1e30f;
    int axis;

    for (axis = 0; axis < 3; ++axis) {
        if (fabsf(d[axis]) < 1e-8f) {
            if (o[axis] < lo[axis] || o[axis] > hi[axis]) return 0;
        } else {
            float inv = 1.0f / d[axis];
            float a = (lo[axis] - o[axis]) * inv;
            float b = (hi[axis] - o[axis]) * inv;
            if (a > b) { float tmp = a; a = b; b = tmp; }
            t_near = fmaxf(t_near, a);
            t_far = fminf(t_far, b);
        }
    }
    t_near = fmaxf(t_near, 0.0f);
    if (t_far <= t_near) return 0;
    *t_enter = t_near;
    *t_exit = t_far;
    return 1;
}

/* Opacities are authored for this step length (the Balanced preset).
   Any other step is corrected so a ray through the same material ends
   with the same opacity - without it, adaptive quality's coarser steps
   while orbiting made the volume visibly fade and brighten. */
#define VOXELPILOT_REFERENCE_STEP 0.0020f

__device__ static unsigned char sample_label_mask(
    const unsigned char *label_mask,
    RendererLabelParams labels,
    float3 posTex)
{
    int x;
    int y;
    int z;
    size_t idx;

    if (!label_mask ||
        !labels.enabled ||
        labels.width < 1 ||
        labels.height < 1 ||
        labels.depth < 1) {
        return 0;
    }

    x = (int)(fminf(fmaxf(posTex.x, 0.0f), 1.0f) * (float)(labels.width - 1) + 0.5f);
    y = (int)(fminf(fmaxf(posTex.y, 0.0f), 1.0f) * (float)(labels.height - 1) + 0.5f);
    z = (int)(fminf(fmaxf(posTex.z, 0.0f), 1.0f) * (float)(labels.depth - 1) + 0.5f);

    idx =
        (size_t)z * (size_t)labels.width * (size_t)labels.height +
        (size_t)y * (size_t)labels.width +
        (size_t)x;

    return label_mask[idx];
}

__device__ static int apply_selected_label_emphasis_device(
    unsigned char label_id,
    unsigned char selected_label_id,
    float strength,
    float *r,
    float *g,
    float *b)
{
    float boost;

    if (!r || !g || !b ||
        label_id == 0 ||
        selected_label_id == 0 ||
        label_id != selected_label_id) {
        return 0;
    }

    strength = fminf(fmaxf(strength, 0.0f), 1.0f);
    if (strength <= 0.0f) {
        return 0;
    }

    boost = 0.20f + 0.45f * strength;
    *r = fminf(fmaxf(*r + (1.0f - *r) * boost, 0.0f), 1.0f);
    *g = fminf(fmaxf(*g + (1.0f - *g) * boost, 0.0f), 1.0f);
    *b = fminf(fmaxf(*b + (1.0f - *b) * boost, 0.0f), 1.0f);
    return 1;
}

__device__ static void apply_label_overlay(
    RendererLabelParams labels,
    unsigned char label_id,
    float *r,
    float *g,
    float *b,
    float *a)
{
    int color_index;
    float alpha;

    if (!labels.enabled ||
        label_id == 0 ||
        label_id > labels.label_count ||
        label_id > RENDERER_MAX_LABELS ||
        !r || !g || !b || !a) {
        return;
    }

    alpha = fminf(fmaxf(labels.alpha, 0.0f), 1.0f);
    color_index = ((int)label_id - 1) * 3;

    *r = *r * (1.0f - alpha) + labels.colors[color_index + 0] * alpha;
    *g = *g * (1.0f - alpha) + labels.colors[color_index + 1] * alpha;
    *b = *b * (1.0f - alpha) + labels.colors[color_index + 2] * alpha;
    if (apply_selected_label_emphasis_device(
            label_id,
            labels.selected_label_id,
            labels.selected_emphasis,
            r, g, b)) {
        *a = fmaxf(*a, 0.28f + alpha * 0.24f);
    } else {
        *a = fmaxf(*a, 0.18f + alpha * 0.18f);
    }
}

/* ============================================================
   Main Volume Raymarch Kernel
   ============================================================ */

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
    int              img_h)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= img_w || y >= img_h)
        return;

    float u = ((float)x + 0.5f) / (float)img_w * 2.0f - 1.0f;
    float v = ((float)y + 0.5f) / (float)img_h * 2.0f - 1.0f;

    float aspect  = (float)img_w / (float)img_h;
    float tan_fov = tanf(0.5f * cam.fov_y);

    float3 w = f3_normalize(cam.view_dir);
    /* If view direction is nearly parallel to up, fall back to world X axis
       to avoid a zero cross product that would produce NaN basis vectors. */
    float3 cross_test = f3_cross(w, cam.up);
    float3 ref_up = (f3_dot(cross_test, cross_test) < 1e-6f)
        ? f3_make(1.0f, 0.0f, 0.0f)
        : cam.up;
    float3 u_cam = f3_normalize(f3_cross(w, ref_up));
    float3 v_cam = f3_cross(u_cam, w);

    float3 ray_dir = f3_normalize(
        f3_add(w,
        f3_add(
            f3_scale(u_cam, u * aspect * tan_fov),
            f3_scale(v_cam, v * tan_fov)))
    );

    /* The volume is a box of size box_extent centred on (0.5, 0.5, 0.5)
       (common/volume_geometry.h): a cube is the old unit cube, anything
       else keeps its true proportions. The clip bounds (normalized
       volume coordinates) shrink that box, and each ray is marched only
       through the part of it that it actually crosses. */
    float3 extent = f3_make(
        vparams.box_extent[0], vparams.box_extent[1], vparams.box_extent[2]);
    float3 clip_lo = f3_make(
        0.5f + (fminf(fmaxf(vparams.clip_min[0], 0.0f), 1.0f) - 0.5f) * extent.x,
        0.5f + (fminf(fmaxf(vparams.clip_min[1], 0.0f), 1.0f) - 0.5f) * extent.y,
        0.5f + (fminf(fmaxf(vparams.clip_min[2], 0.0f), 1.0f) - 0.5f) * extent.z);
    float3 clip_hi = f3_make(
        0.5f + (fminf(fmaxf(vparams.clip_max[0], 0.0f), 1.0f) - 0.5f) * extent.x,
        0.5f + (fminf(fmaxf(vparams.clip_max[1], 0.0f), 1.0f) - 0.5f) * extent.y,
        0.5f + (fminf(fmaxf(vparams.clip_max[2], 0.0f), 1.0f) - 0.5f) * extent.z);
    float3 gradient_scale = f3_make(
        (float)(vparams.width  > 1 ? vparams.width  - 1 : 1) / extent.x,
        (float)(vparams.height > 1 ? vparams.height - 1 : 1) / extent.y,
        (float)(vparams.depth  > 1 ? vparams.depth  - 1 : 1) / extent.z);
    float step_len = fmaxf(vparams.step_size, 1e-5f);
    float opacity_exponent = step_len / VOXELPILOT_REFERENCE_STEP;

    float t = 0.0f;
    float t_end = 0.0f;
    int   hits_volume = ray_box_interval(cam.position, ray_dir, clip_lo, clip_hi, &t, &t_end);

    if (hits_volume) {
        t += step_len * pixel_jitter(x, y);
    }

    float accumulated_alpha = 0.0f;
    float accumulated_r     = 0.0f;
    float accumulated_g     = 0.0f;
    float accumulated_b     = 0.0f;

    unsigned int active_mask = __activemask();

    /* The iteration count must be the same for every thread in a warp
       (the __ballot_sync calls below), so it is bounded by the longest
       possible chord (the box diagonal, at most sqrt(3)) rather than by
       each ray's own interval; rays that are done simply go inactive,
       and the warp leaves the loop as soon as all of its rays are. */
    int step;
    int max_steps = (int)(1.7321f / step_len) + 2;
    if (max_steps > 4096) max_steps = 4096;

    for (step = 0; step < max_steps; ++step)
    {
        int still_active =
            hits_volume &&
            (accumulated_alpha < vparams.threshold) &&
            (t < t_end);

        unsigned int any_active =
            __ballot_sync(active_mask, still_active);

        if (any_active == 0u)
            break;

        if (!still_active) {
            t += step_len;
            continue;
        }

        float3 pos_world =
            f3_add(cam.position,
                   f3_scale(ray_dir, t));

        /* World -> normalized volume coordinates (inverse of the box
           placement above), clamped against float round-off at faces. */
        float3 pos_tex;
        pos_tex.x = fminf(fmaxf(0.5f + (pos_world.x - 0.5f) / extent.x, 0.0f), 1.0f);
        pos_tex.y = fminf(fmaxf(0.5f + (pos_world.y - 0.5f) / extent.y, 0.0f), 1.0f);
        pos_tex.z = fminf(fmaxf(0.5f + (pos_world.z - 0.5f) / extent.z, 0.0f), 1.0f);

        const BrickInfo *brick =
            find_brick_for_pos(bricks, grid, pos_tex);

        int has_brick = (brick != 0);

        unsigned int any_brick =
            __ballot_sync(active_mask, has_brick);

        if (any_brick == 0u) {
            t += step_len *
                 vparams.empty_space_skip_mult;
            continue;
        }

        if (!has_brick) {
            t += step_len;
            continue;
        }

        /* Normalized coordinate 0..1 spans the first to the last voxel
           centre - the same convention as the slices, the label mask and
           CPU picking. sample_volume_tex adds the half-texel offset. The
           old "* width" mapping put the last voxel a texel beyond 1.0 and
           shifted the render half a voxel against everything else. */
        float fx = pos_tex.x * (float)(vparams.width  > 1 ? vparams.width  - 1 : 1);
        float fy = pos_tex.y * (float)(vparams.height > 1 ? vparams.height - 1 : 1);
        float fz = pos_tex.z * (float)(vparams.depth  > 1 ? vparams.depth  - 1 : 1);

        float val = sample_volume_tex(fx, fy, fz);

        if (val > 0.01f)
        {
            float r, g, b, a;
            unsigned char label_id;
            tf_lookup(vparams, val, &r, &g, &b, &a);
            label_id = sample_label_mask(label_mask, labels, pos_tex);
            apply_label_overlay(labels, label_id, &r, &g, &b, &a);

            /* Opacity correction for the actual step length. */
            a = 1.0f - __powf(1.0f - fminf(fmaxf(a, 0.0f), 0.9999f), opacity_exponent);

            float3 grad =
                sample_gradient_tex(fx, fy, fz, gradient_scale);

            float3 L =
                f3_normalize(f3_sub(lightPos, pos_world));
            float3 V =
                f3_normalize(f3_sub(cam.position, pos_world));

            float3 base;
            base.x = r;
            base.y = g;
            base.z = b;

            float3 lit =
                apply_phong(base, grad, L, V,
                            0.25f, 0.65f, 0.10f, 16.0f);

            if (vparams.cinematic_enabled) {
                float rim_strength = fminf(fmaxf(vparams.cinematic_rim_strength, 0.0f), 1.0f);
                float glow_strength = fminf(fmaxf(vparams.cinematic_glow_strength, 0.0f), 1.0f);
                float rim = 1.0f - fabsf(f3_dot(grad, V));
                float glow = a * glow_strength;

                rim = rim * rim * rim_strength;

                lit.x = fminf(1.0f, lit.x + rim * 0.72f + glow * 0.30f + val * 0.035f);
                lit.y = fminf(1.0f, lit.y + rim * 0.55f + glow * 0.22f + val * 0.020f);
                lit.z = fminf(1.0f, lit.z + rim * 0.32f + glow * 0.14f);
            }

            float one_minus_a =
                1.0f - accumulated_alpha;

            accumulated_r += one_minus_a * lit.x * a;
            accumulated_g += one_minus_a * lit.y * a;
            accumulated_b += one_minus_a * lit.z * a;

            accumulated_alpha +=
                one_minus_a * a;
        }

        t += step_len;
    }

    if (vparams.cinematic_enabled) {
        float contrast = fminf(fmaxf(vparams.cinematic_contrast, 0.8f), 1.6f);
        accumulated_r = (accumulated_r - 0.5f) * contrast + 0.5f;
        accumulated_g = (accumulated_g - 0.5f) * contrast + 0.5f;
        accumulated_b = (accumulated_b - 0.5f) * contrast + 0.5f;
    }

    output[y * img_w + x] = make_uchar4(
        (unsigned char)(fminf(fmaxf(accumulated_r, 0.0f), 1.0f) * 255.0f),
        (unsigned char)(fminf(fmaxf(accumulated_g, 0.0f), 1.0f) * 255.0f),
        (unsigned char)(fminf(fmaxf(accumulated_b, 0.0f), 1.0f) * 255.0f),
        255);
}


/* ============================================================
   Gray → RGBA Kernel
   ============================================================ */

__global__ void expand_gray_to_rgba(
    uchar4 *out,
    const unsigned char *in,
    int w,
    int h)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= w || y >= h)
        return;

    int idx = y * w + x;

    unsigned char v = in[idx];

    out[idx] = make_uchar4(v, v, v, 255);
}

/* ============================================================
   Host Setter for Device Texture Symbols

   Must live in this file because cudaMemcpyToSymbol
   requires symbols in the same compilation unit.

   Called by renderer.cu after creating texture objects.
   ============================================================ */

int set_device_texture_objects(
    cudaTextureObject_t volTex,
    cudaTextureObject_t gradTex)
{
    cudaError_t e1;
    cudaError_t e2;

    e1 = cudaMemcpyToSymbol(
        devVolumeTex, &volTex,
        sizeof(cudaTextureObject_t));

    if (e1 != cudaSuccess) {
        fprintf(stderr,
                "cudaMemcpyToSymbol(devVolumeTex) failed: %s\n",
                cudaGetErrorString(e1));
        cudaGetLastError();
        return 0;
    }

    e2 = cudaMemcpyToSymbol(
        devGradTex, &gradTex,
        sizeof(cudaTextureObject_t));

    if (e2 != cudaSuccess) {
        fprintf(stderr,
                "cudaMemcpyToSymbol(devGradTex) failed: %s\n",
                cudaGetErrorString(e2));
        cudaGetLastError();
        return 0;
    }

    return 1;
}
#endif
