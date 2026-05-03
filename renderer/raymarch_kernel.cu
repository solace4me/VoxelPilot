#include <cuda_runtime.h>
#include <device_launch_parameters.h>

#include <stdio.h>
#include <stdlib.h>

#include "../common/volume_structs.h"
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


__device__ static float3 sample_gradient_tex(
    float fx, float fy, float fz)
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

    out.x = g.x;
    out.y = g.y;
    out.z = g.z;

    return f3_normalize(out);
}


/* ============================================================
   Main Volume Raymarch Kernel
   ============================================================ */

__global__ void volume_render_kernel(
    unsigned char   *output,
    const float     *volume,
    VolumeParams     vparams,
    const BrickInfo *bricks,
    const BrickGrid *grid,
    CameraParams     cam,
    float3           lightPos,
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
    float3 u_cam = f3_normalize(f3_cross(w, cam.up));
    float3 v_cam = f3_cross(u_cam, w);

    float3 ray_dir = f3_normalize(
        f3_add(w,
        f3_add(
            f3_scale(u_cam, u * aspect * tan_fov),
            f3_scale(v_cam, v * tan_fov)))
    );

    float t = 0.0f;
    float t_max = 2.0f;

    float accumulated_alpha = 0.0f;
    float accumulated_color = 0.0f;

    unsigned int active_mask = __activemask();

    int step;
    for (step = 0; step < 512 && t < t_max; ++step)
    {
        int still_active =
            (accumulated_alpha < vparams.threshold) &&
            (t < t_max);

        unsigned int any_active =
            __ballot_sync(active_mask, still_active);

        if (any_active == 0u)
            break;

        if (!still_active) {
            t += vparams.step_size;
            continue;
        }

        float3 pos_world =
            f3_add(cam.position,
                   f3_scale(ray_dir, t));

        float3 pos_tex;
        pos_tex.x = fminf(fmaxf(pos_world.x, 0.0f), 1.0f);
        pos_tex.y = fminf(fmaxf(pos_world.y, 0.0f), 1.0f);
        pos_tex.z = fminf(fmaxf(pos_world.z, 0.0f), 1.0f);

        if (pos_tex.x < vparams.clip_min[0] ||
            pos_tex.y < vparams.clip_min[1] ||
            pos_tex.z < vparams.clip_min[2] ||
            pos_tex.x > vparams.clip_max[0] ||
            pos_tex.y > vparams.clip_max[1] ||
            pos_tex.z > vparams.clip_max[2]) {
            t += vparams.step_size;
            continue;
        }

        const BrickInfo *brick =
            find_brick_for_pos(bricks, grid, pos_tex);

        int has_brick = (brick != 0);

        unsigned int any_brick =
            __ballot_sync(active_mask, has_brick);

        if (any_brick == 0u) {
            t += vparams.step_size *
                 vparams.empty_space_skip_mult;
            continue;
        }

        if (!has_brick) {
            t += vparams.step_size;
            continue;
        }

        float fx = pos_tex.x * (float)vparams.width;
        float fy = pos_tex.y * (float)vparams.height;
        float fz = pos_tex.z * (float)vparams.depth;

        float val = sample_volume_tex(fx, fy, fz);

        if (val > 0.01f)
        {
            float r, g, b, a;
            tf_lookup(vparams, val, &r, &g, &b, &a);

            float3 grad =
                sample_gradient_tex(fx, fy, fz);

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
                            0.1f, 0.7f, 0.2f, 16.0f);

            float lum =
                (lit.x + lit.y + lit.z) / 3.0f;

            float one_minus_a =
                1.0f - accumulated_alpha;

            accumulated_color +=
                one_minus_a * lum * a;

            accumulated_alpha +=
                one_minus_a * a;
        }

        t += vparams.step_size;
    }

    output[y * img_w + x] =
        (unsigned char)(
            fminf(fmaxf(accumulated_color, 0.0f), 1.0f)
            * 255.0f);
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

void set_device_texture_objects(
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
        exit(-1);
    }

    e2 = cudaMemcpyToSymbol(
        devGradTex, &gradTex,
        sizeof(cudaTextureObject_t));

    if (e2 != cudaSuccess) {
        fprintf(stderr,
                "cudaMemcpyToSymbol(devGradTex) failed: %s\n",
                cudaGetErrorString(e2));
        exit(-1);
    }
}
#endif
