#ifndef VOLUME_STRUCTS_H
#define VOLUME_STRUCTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/* ============================================================
   CUDA Compatibility
   ============================================================ */

#ifdef __CUDACC__
#include <cuda_runtime.h>
#else
/* If not compiling with NVCC, define minimal CUDA-like types */

#ifndef __host__
#define __host__
#endif

#ifndef __device__
#define __device__
#endif

typedef struct { float x, y, z; } float3;
typedef struct { float x, y, z, w; } float4;

typedef void* cudaArray_t;
typedef void* cudaTextureObject_t;

#endif


/* ============================================================
   Volume Rendering Parameters
   ============================================================ */

typedef struct {
    int   width;
    int   height;
    int   depth;

    float step_size;
    float threshold;

    float empty_space_skip_mult;
    float tf_center;
    float tf_width;
    float tf_opacity_scale;
    int   tf_palette;
    int   tf_invert;
    float clip_min[3];
    float clip_max[3];

} VolumeParams;


/* ============================================================
   Brick Structures
   ============================================================ */

typedef struct {
    int bx, by, bz;

    int sizeX;
    int sizeY;
    int sizeZ;

    int offsetX;
    int offsetY;
    int offsetZ;

    int isEmpty;

} BrickInfo;


typedef struct {
    int bricksX;
    int bricksY;
    int bricksZ;

    int numBricks;

} BrickGrid;


/* ============================================================
   Camera Parameters
   ============================================================ */

typedef struct {
    float3 position;
    float3 view_dir;
    float3 up;

    float  fov_y;

} CameraParams;


/* ============================================================
   Volume Texture Resources
   ============================================================ */

typedef struct {
    cudaArray_t         volArray;
    cudaArray_t         gradArray;

    cudaTextureObject_t volTex;
    cudaTextureObject_t gradTex;

    int width;
    int height;
    int depth;

} VolumeResources;


/* ============================================================
   Public Renderer API (implemented in renderer.cu)
   ============================================================ */

int renderer_init(
    const float *volume_data,
    int W, int H, int D,
    VolumeParams *params,
    VolumeResources *resources);

int renderer_render(
    unsigned char *d_output,
    VolumeParams   params,
    CameraParams   cam,
    float3         lightPos,
    int img_w,
    int img_h);

void renderer_cleanup(void);


#ifdef __cplusplus
}
#endif

#endif /* VOLUME_STRUCTS_H */
