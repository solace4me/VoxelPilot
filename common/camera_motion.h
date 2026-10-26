#ifndef VOXELPILOT_CAMERA_MOTION_H
#define VOXELPILOT_CAMERA_MOTION_H

#include <math.h>
#include <stddef.h>

/* Camera presets and the eased transition between camera poses used by
   the viewport toolbar (ui_viewport_overlay.cpp), the Inspector's camera
   view buttons and the 1-4 keys. Pure functions, unit-tested. */

enum {
    VOXELPILOT_CAMERA_PRESET_FRONT = 0,
    VOXELPILOT_CAMERA_PRESET_SIDE,
    VOXELPILOT_CAMERA_PRESET_TOP,
    VOXELPILOT_CAMERA_PRESET_ISO,
    VOXELPILOT_CAMERA_PRESET_COUNT
};

/* Every preset sits this far from the volume centre (0.5, 0.5, 0.5),
   which frames the whole volume box (longest side 1, see
   volume_geometry.h) at the default field of view. */
#define VOXELPILOT_CAMERA_PRESET_DISTANCE 1.6f

/* Seconds a preset change takes (spec: 300 ms). */
#define VOXELPILOT_CAMERA_TRANSITION_SECONDS 0.30f

static inline int voxelpilot_camera_preset(int preset, float pos[3], float dir[3], float up[3])
{
    /* Direction from the centre towards the camera, and the up vector. */
    static const float offsets[VOXELPILOT_CAMERA_PRESET_COUNT][3] = {
        {  0.0f, 0.0f, -1.0f },   /* front: looks along +z */
        { -1.0f, 0.0f,  0.0f },   /* side:  looks along +x */
        {  0.0f, 1.0f,  0.0f },   /* top:   looks down -y  */
        { -1.0f, 0.5f, -1.0f }    /* iso:   above, front-left */
    };
    static const float ups[VOXELPILOT_CAMERA_PRESET_COUNT][3] = {
        { 0.0f, 1.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f },
        { 0.0f, 1.0f, 0.0f }
    };
    float len;
    int axis;

    if (preset < 0 || preset >= VOXELPILOT_CAMERA_PRESET_COUNT || !pos || !dir || !up) {
        return 0;
    }
    len = sqrtf(offsets[preset][0] * offsets[preset][0] +
                offsets[preset][1] * offsets[preset][1] +
                offsets[preset][2] * offsets[preset][2]);
    for (axis = 0; axis < 3; ++axis) {
        float o = offsets[preset][axis] / len;
        pos[axis] = 0.5f + o * VOXELPILOT_CAMERA_PRESET_DISTANCE;
        dir[axis] = -o;
        up[axis] = ups[preset][axis];
    }
    return 1;
}

/* Smoothstep, clamped to [0, 1]: starts and ends gently. */
static inline float voxelpilot_camera_ease(float t)
{
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static inline void voxelpilot_camera_normalize(float v[3], const float fallback[3])
{
    float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    int axis;
    if (len < 1e-6f) {
        for (axis = 0; axis < 3; ++axis) v[axis] = fallback[axis];
        return;
    }
    for (axis = 0; axis < 3; ++axis) v[axis] /= len;
}

/* Pose part-way (t in [0, 1], already eased) from one camera to another:
   position linearly, direction and up as normalized blends (falling back
   to the target when the two point in opposite directions). */
static inline void voxelpilot_camera_blend(
    const float from_pos[3], const float from_dir[3], const float from_up[3],
    const float to_pos[3],   const float to_dir[3],   const float to_up[3],
    float t,
    float out_pos[3], float out_dir[3], float out_up[3])
{
    int axis;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    for (axis = 0; axis < 3; ++axis) {
        out_pos[axis] = from_pos[axis] + (to_pos[axis] - from_pos[axis]) * t;
        out_dir[axis] = from_dir[axis] + (to_dir[axis] - from_dir[axis]) * t;
        out_up[axis] = from_up[axis] + (to_up[axis] - from_up[axis]) * t;
    }
    voxelpilot_camera_normalize(out_dir, to_dir);
    voxelpilot_camera_normalize(out_up, to_up);
}

#endif /* VOXELPILOT_CAMERA_MOTION_H */
