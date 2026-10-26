#ifndef VOXELPILOT_VOLUME_GEOMETRY_H
#define VOXELPILOT_VOLUME_GEOMETRY_H

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

#endif /* VOXELPILOT_VOLUME_GEOMETRY_H */
