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

static int check_cuda_tex(cudaError_t err, const char *msg)
{
    if (err == cudaSuccess) {
        return 1;
    }
    fprintf(stderr,
            "CUDA Error [volume_textures] at %s: %s\n",
            msg, cudaGetErrorString(err));
    cudaGetLastError();
    return 0;
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
   Create the Volume 3D Texture

   The volume lives on the GPU as one 16-bit normalized texture:
   2 bytes per voxel, read back as float in [0, 1] by the texture unit
   (cudaReadModeNormalizedFloat), with hardware trilinear filtering.
   Input data is at most 16-bit (or float already scaled to [0, 1]), so
   no precision that the data had is lost.

   There is no gradient texture any more: the kernel takes central
   differences of this texture where it shades (raymarch_kernel.cu).
   The old float4 gradient texture cost 16 bytes per voxel on the GPU
   plus a 16-byte-per-voxel host buffer while it was built - together
   with the old 4-byte float texture, a 512^3 scan needed about 2.7 GB
   of GPU memory for textures alone; it now needs 256 MB.

   The upload converts and copies a slab of slices at a time, so the
   host staging buffer stays small whatever the volume size.

   Caller must later call set_device_texture_objects()
   (defined in raymarch_kernel.cu) to bind the texture
   object to the device symbol used by the kernel.
   ============================================================ */

#define VOLUME_UPLOAD_SLAB_BYTES (16u * 1024u * 1024u)

int create_volume_and_gradient_textures(
    const float     *volume_data,
    int              W,
    int              H,
    int              D,
    VolumeResources *res)
{
    cudaExtent              extent;
    cudaChannelFormatDesc   volChannelDesc;
    cudaMemcpy3DParms       copyParams;
    cudaResourceDesc        resDesc;
    cudaTextureDesc         texDesc;
    unsigned short         *staging = NULL;
    size_t                  slice_voxels;
    int                     slab_depth;
    int                     z0;

    if (!res || !volume_data || W < 1 || H < 1 || D < 1) return 0;

    release_volume_resources(res);

    res->width  = W;
    res->height = H;
    res->depth  = D;

    extent = make_cudaExtent((size_t)W, (size_t)H, (size_t)D);

    volChannelDesc = cudaCreateChannelDesc(
        16, 0, 0, 0,
        cudaChannelFormatKindUnsigned);

    if (!check_cuda_tex(
            cudaMalloc3DArray(&res->volArray,
                              &volChannelDesc,
                              extent),
            "cudaMalloc3DArray(volume)")) {
        release_volume_resources(res);
        return 0;
    }

    slice_voxels = (size_t)W * (size_t)H;
    slab_depth = (int)(VOLUME_UPLOAD_SLAB_BYTES / (slice_voxels * sizeof(unsigned short)));
    if (slab_depth < 1) slab_depth = 1;
    if (slab_depth > D) slab_depth = D;

    staging = (unsigned short *)malloc(slice_voxels * (size_t)slab_depth * sizeof(unsigned short));
    if (!staging) {
        fprintf(stderr, "Failed to allocate volume upload buffer\n");
        release_volume_resources(res);
        return 0;
    }

    for (z0 = 0; z0 < D; z0 += slab_depth) {
        int    depth = (z0 + slab_depth <= D) ? slab_depth : D - z0;
        size_t count = slice_voxels * (size_t)depth;
        const float *src = volume_data + slice_voxels * (size_t)z0;
        size_t i;

        for (i = 0; i < count; ++i) {
            float v = src[i];
            v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
            staging[i] = (unsigned short)(v * 65535.0f + 0.5f);
        }

        memset(&copyParams, 0, sizeof(copyParams));
        copyParams.srcPtr = make_cudaPitchedPtr(
            (void *)staging,
            (size_t)W * sizeof(unsigned short),
            (size_t)W,
            (size_t)H);
        copyParams.dstArray = res->volArray;
        copyParams.dstPos   = make_cudaPos(0, 0, (size_t)z0);
        copyParams.extent   = make_cudaExtent((size_t)W, (size_t)H, (size_t)depth);
        copyParams.kind     = cudaMemcpyHostToDevice;

        if (!check_cuda_tex(
                cudaMemcpy3D(&copyParams),
                "cudaMemcpy3D(volume slab)")) {
            free(staging);
            release_volume_resources(res);
            return 0;
        }
    }
    free(staging);

    /* normalizedCoords = 0: the kernel passes texel coordinates.
       NormalizedFloat: 0..65535 reads back as 0.0..1.0. */
    memset(&resDesc, 0, sizeof(resDesc));
    resDesc.resType         = cudaResourceTypeArray;
    resDesc.res.array.array = res->volArray;

    memset(&texDesc, 0, sizeof(texDesc));
    texDesc.addressMode[0]  = cudaAddressModeClamp;
    texDesc.addressMode[1]  = cudaAddressModeClamp;
    texDesc.addressMode[2]  = cudaAddressModeClamp;
    texDesc.filterMode      = cudaFilterModeLinear;
    texDesc.readMode        = cudaReadModeNormalizedFloat;
    texDesc.normalizedCoords = 0;

    if (!check_cuda_tex(
            cudaCreateTextureObject(&res->volTex,
                                    &resDesc,
                                    &texDesc,
                                    NULL),
            "cudaCreateTextureObject(volume)")) {
        release_volume_resources(res);
        return 0;
    }

    res->gradTex = 0;
    res->gradArray = NULL;

    printf("Volume texture created: %dx%dx%d, 16-bit (%.1f MB on GPU)\n",
           W, H, D,
           (double)slice_voxels * (double)D * 2.0 / (1024.0 * 1024.0));
    return 1;
}

#endif /* NO_CUDA */