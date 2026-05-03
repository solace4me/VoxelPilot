#include <cuda_runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../common/volume_structs.h"

#ifndef NO_CUDA

/* ============================================================
   Local CUDA Error Helper
   ============================================================ */

static void check_cuda_tex(cudaError_t err, const char *msg)
{
    if (err != cudaSuccess) {
        fprintf(stderr,
                "CUDA Error [volume_textures] at %s: %s\n",
                msg, cudaGetErrorString(err));
        exit(-1);
    }
}


/* ============================================================
   Release Volume Resources
   
   Destroys texture objects and frees 3D arrays.
   Safe to call on a zeroed-out struct.
   ============================================================ */

void release_volume_resources(VolumeResources *res)
{
    if (!res) return;

    if (res->volTex) {
        cudaDestroyTextureObject(res->volTex);
        res->volTex = 0;
    }

    if (res->gradTex) {
        cudaDestroyTextureObject(res->gradTex);
        res->gradTex = 0;
    }

    if (res->volArray) {
        cudaFreeArray(res->volArray);
        res->volArray = NULL;
    }

    if (res->gradArray) {
        cudaFreeArray(res->gradArray);
        res->gradArray = NULL;
    }

    res->width  = 0;
    res->height = 0;
    res->depth  = 0;
}


/* ============================================================
   Create Volume and Gradient 3D Textures

   1. Allocates 3D CUDA arrays
   2. Uploads volume data
   3. Computes normalized gradients on host
   4. Uploads gradient data
   5. Creates texture objects for trilinear sampling

   Caller must later call set_device_texture_objects()
   (defined in raymarch_kernel.cu) to bind the texture
   objects to device symbols used by the kernel.
   ============================================================ */

void create_volume_and_gradient_textures(
    const float     *volume_data,
    int              W,
    int              H,
    int              D,
    VolumeResources *res)
{
    cudaExtent              extent;
    cudaChannelFormatDesc   volChannelDesc;
    cudaChannelFormatDesc   gradChannelDesc;
    cudaMemcpy3DParms       copyParams;
    cudaResourceDesc        resDesc;
    cudaTextureDesc         texDesc;
    float4                 *gradHost;
    size_t                  voxels;
    int                     x, y, z;

    /* ---- Release any previously held resources ---- */

    release_volume_resources(res);

    if (!res) return;

    res->width  = W;
    res->height = H;
    res->depth  = D;

    extent = make_cudaExtent((size_t)W, (size_t)H, (size_t)D);

    /* ============================================================
       Volume 3D Array + Texture Object
       ============================================================ */

    /* C-compatible channel descriptor (no C++ template) */
    volChannelDesc = cudaCreateChannelDesc(
        32, 0, 0, 0,
        cudaChannelFormatKindFloat);

    check_cuda_tex(
        cudaMalloc3DArray(&res->volArray,
                          &volChannelDesc,
                          extent),
        "cudaMalloc3DArray(volume)");

    /* Copy volume data into 3D array */

    memset(&copyParams, 0, sizeof(copyParams));

    copyParams.srcPtr = make_cudaPitchedPtr(
        (void *)volume_data,
        (size_t)W * sizeof(float),
        (size_t)W,
        (size_t)H);

    copyParams.dstArray = res->volArray;
    copyParams.extent   = extent;
    copyParams.kind     = cudaMemcpyHostToDevice;

    check_cuda_tex(
        cudaMemcpy3D(&copyParams),
        "cudaMemcpy3D(volume)");

    /* Volume texture object
       normalizedCoords = 0 because kernel passes texel
       coordinates (pos_tex * dim).
       readMode = ElementType because data is already float. */

    memset(&resDesc, 0, sizeof(resDesc));
    resDesc.resType         = cudaResourceTypeArray;
    resDesc.res.array.array = res->volArray;

    memset(&texDesc, 0, sizeof(texDesc));
    texDesc.addressMode[0]  = cudaAddressModeClamp;
    texDesc.addressMode[1]  = cudaAddressModeClamp;
    texDesc.addressMode[2]  = cudaAddressModeClamp;
    texDesc.filterMode      = cudaFilterModeLinear;
    texDesc.readMode        = cudaReadModeElementType;
    texDesc.normalizedCoords = 0;

    check_cuda_tex(
        cudaCreateTextureObject(&res->volTex,
                                &resDesc,
                                &texDesc,
                                NULL),
        "cudaCreateTextureObject(volume)");

    /* ============================================================
       Compute Normalized Gradients on Host
       Central differences with boundary clamping
       ============================================================ */

    voxels = (size_t)W * (size_t)H * (size_t)D;

    gradHost = (float4 *)malloc(voxels * sizeof(float4));
    if (!gradHost) {
        fprintf(stderr,
                "Failed to allocate gradient buffer (%zu bytes)\n",
                voxels * sizeof(float4));
        exit(-1);
    }

    for (z = 0; z < D; ++z) {
        for (y = 0; y < H; ++y) {
            for (x = 0; x < W; ++x) {

                int idx = z * H * W + y * W + x;

                /* Clamped neighbor indices */
                int xm = (x > 0)     ? x - 1 : 0;
                int xp = (x < W - 1) ? x + 1 : W - 1;
                int ym = (y > 0)     ? y - 1 : 0;
                int yp = (y < H - 1) ? y + 1 : H - 1;
                int zm = (z > 0)     ? z - 1 : 0;
                int zp = (z < D - 1) ? z + 1 : D - 1;

                /* Central differences */
                float gx = volume_data[z  * W * H + y  * W + xp]
                         - volume_data[z  * W * H + y  * W + xm];

                float gy = volume_data[z  * W * H + yp * W + x]
                         - volume_data[z  * W * H + ym * W + x];

                float gz = volume_data[zp * W * H + y  * W + x]
                         - volume_data[zm * W * H + y  * W + x];

                /* Normalize */
                float len = sqrtf(gx*gx + gy*gy + gz*gz) + 1e-6f;

                gradHost[idx].x = gx / len;
                gradHost[idx].y = gy / len;
                gradHost[idx].z = gz / len;
                gradHost[idx].w = 0.0f;
            }
        }
    }

    /* ============================================================
       Gradient 3D Array + Texture Object
       ============================================================ */

    /* float4 channel: 32 bits per component x 4 */
    gradChannelDesc = cudaCreateChannelDesc(
        32, 32, 32, 32,
        cudaChannelFormatKindFloat);

    check_cuda_tex(
        cudaMalloc3DArray(&res->gradArray,
                          &gradChannelDesc,
                          extent),
        "cudaMalloc3DArray(gradient)");

    /* Copy gradient data into 3D array */

    memset(&copyParams, 0, sizeof(copyParams));

    copyParams.srcPtr = make_cudaPitchedPtr(
        (void *)gradHost,
        (size_t)W * sizeof(float4),
        (size_t)W,
        (size_t)H);

    copyParams.dstArray = res->gradArray;
    copyParams.extent   = extent;
    copyParams.kind     = cudaMemcpyHostToDevice;

    check_cuda_tex(
        cudaMemcpy3D(&copyParams),
        "cudaMemcpy3D(gradient)");

    /* Gradient texture object (same settings as volume) */

    memset(&resDesc, 0, sizeof(resDesc));
    resDesc.resType         = cudaResourceTypeArray;
    resDesc.res.array.array = res->gradArray;

    memset(&texDesc, 0, sizeof(texDesc));
    texDesc.addressMode[0]  = cudaAddressModeClamp;
    texDesc.addressMode[1]  = cudaAddressModeClamp;
    texDesc.addressMode[2]  = cudaAddressModeClamp;
    texDesc.filterMode      = cudaFilterModeLinear;
    texDesc.readMode        = cudaReadModeElementType;
    texDesc.normalizedCoords = 0;

    check_cuda_tex(
        cudaCreateTextureObject(&res->gradTex,
                                &resDesc,
                                &texDesc,
                                NULL),
        "cudaCreateTextureObject(gradient)");

    /* ---- Free host gradient buffer ---- */

    free(gradHost);

    printf("Volume textures created: %dx%dx%d\n", W, H, D);
    printf("  Volume  tex obj: %llu\n",
           (unsigned long long)res->volTex);
    printf("  Gradient tex obj: %llu\n",
           (unsigned long long)res->gradTex);
}

#endif /* NO_CUDA */