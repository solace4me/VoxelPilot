#ifndef VOXELPILOT_VOLUME_GEOMETRY_H
#define VOXELPILOT_VOLUME_GEOMETRY_H

#include <stddef.h>

/* Where the volume sits in world space.

   The volume is a box whose sides are proportional to its physical size,
   (dimension - 1) * voxel spacing per axis (the distance between the
   first and last voxel centre), scaled so the longest side is 1 and
   centred on (0.5, 0.5, 0.5). A cube with uniform spacing is therefore
   exactly the old unit cube [0, 1]^3, and camera presets aimed at its
   centre still frame any volume.

   "Volume point" = normalized volume coordinates, (0..1) per axis from
   the first to the last voxel centre - what slices, clip bounds,
   measurements and the label mask use. "World" = the space the camera,
   light and ray marcher live in. The CUDA kernel applies the same
   mapping inline (renderer/raymarch_kernel.cu). */

static inline void voxelpilot_volume_box_extent(
    int         width,
    int         height,
    int         depth,
    const float spacing[3],
    float       extent[3])
{
    const int dims[3] = { width, height, depth };
    float     longest = 0.0f;
    int       axis;

    for (axis = 0; axis < 3; ++axis) {
        float s = (spacing && spacing[axis] > 0.0f) ? spacing[axis] : 1.0f;
        int   n = dims[axis] > 1 ? dims[axis] - 1 : 1;
        extent[axis] = (float)n * s;
        if (extent[axis] > longest) longest = extent[axis];
    }
    for (axis = 0; axis < 3; ++axis) {
        extent[axis] = longest > 0.0f ? extent[axis] / longest : 1.0f;
    }
}

static inline void voxelpilot_volume_point_to_world(
    const float extent[3],
    const float point[3],
    float       world[3])
{
    int axis;
    for (axis = 0; axis < 3; ++axis) {
        world[axis] = 0.5f + (point[axis] - 0.5f) * extent[axis];
    }
}

static inline void voxelpilot_world_to_volume_point(
    const float extent[3],
    const float world[3],
    float       point[3])
{
    int axis;
    for (axis = 0; axis < 3; ++axis) {
        point[axis] = extent[axis] > 0.0f
            ? 0.5f + (world[axis] - 0.5f) / extent[axis]
            : 0.5f;
    }
}

/* ---- GPU memory budget ----

   What a loaded volume costs on the GPU: the 16-bit texture (2 bytes per
   voxel, renderer/volume_textures.cu) plus the label mask (1 byte per
   voxel, allocated once regions or annotations exist). The CUDA/GL
   context and output buffers are a fixed cost on top, covered by the
   headroom below rather than counted here. */

static inline size_t voxelpilot_estimate_volume_gpu_bytes(int width, int height, int depth)
{
    if (width < 1 || height < 1 || depth < 1) return 0;
    return (size_t)width * (size_t)height * (size_t)depth * 3u;
}

/* Use at most this share of the free GPU memory for the volume, leaving
   the rest for output buffers, the driver and other apps. */
#define VOXELPILOT_GPU_BUDGET_SHARE 0.75

/* The smallest downsampling factor (1 = full resolution, then 2, 4) whose
   volume fits the budget, or 0 when even quarter resolution does not. */
static inline int voxelpilot_choose_downsample_factor(
    int    width,
    int    height,
    int    depth,
    size_t free_gpu_bytes)
{
    static const int factors[3] = { 1, 2, 4 };
    double budget = (double)free_gpu_bytes * VOXELPILOT_GPU_BUDGET_SHARE;
    int    i;

    for (i = 0; i < 3; ++i) {
        int f = factors[i];
        size_t need = voxelpilot_estimate_volume_gpu_bytes(
            (width + f - 1) / f, (height + f - 1) / f, (depth + f - 1) / f);
        if ((double)need <= budget) {
            return f;
        }
    }
    return 0;
}

#endif /* VOXELPILOT_VOLUME_GEOMETRY_H */
