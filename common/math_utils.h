#ifndef MATH_UTILS_H
#define MATH_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <math.h>

/* 
   We assume float3 / float4 are defined in volume_structs.h.
   That header must be included before this one.
*/

/* Handle CUDA qualifiers safely */
#ifndef __CUDACC__
#ifndef __host__
#define __host__
#endif
#ifndef __device__
#define __device__
#endif
#endif


/* ============================================================
   Basic float3 operations
   ============================================================ */

__host__ __device__ static inline float3 f3_make(float x, float y, float z)
{
    float3 v; v.x = x; v.y = y; v.z = z; return v;
}

__host__ __device__ static inline float3 f3_add(float3 a, float3 b)
{
    return f3_make(a.x + b.x, a.y + b.y, a.z + b.z);
}

__host__ __device__ static inline float3 f3_sub(float3 a, float3 b)
{
    return f3_make(a.x - b.x, a.y - b.y, a.z - b.z);
}

__host__ __device__ static inline float3 f3_scale(float3 a, float s)
{
    return f3_make(a.x * s, a.y * s, a.z * s);
}

__host__ __device__ static inline float f3_dot(float3 a, float3 b)
{
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

__host__ __device__ static inline float3 f3_cross(float3 a, float3 b)
{
    return f3_make(
        a.y*b.z - a.z*b.y,
        a.z*b.x - a.x*b.z,
        a.x*b.y - a.y*b.x
    );
}

__host__ __device__ static inline float3 f3_normalize(float3 a)
{
    float len = sqrtf(a.x*a.x + a.y*a.y + a.z*a.z) + 1e-6f;
    return f3_make(a.x/len, a.y/len, a.z/len);
}


/* ============================================================
   Transfer Function
   ============================================================ */

__host__ __device__ static inline void tf_lookup(
    VolumeParams params,
    float val,
    float *r,
    float *g,
    float *b,
    float *a)
{
    float width = fmaxf(params.tf_width, 1e-5f);
    float low = params.tf_center - 0.5f * width;
    float v = (val - low) / width;

    v = fminf(fmaxf(v, 0.0f), 1.0f);
    if (params.tf_invert) {
        v = 1.0f - v;
    }

    if (params.tf_palette == 1) {
        *r = fminf(1.0f, v * 1.05f + 0.10f);
        *g = fminf(1.0f, v * 0.95f + 0.08f);
        *b = fminf(1.0f, v * 0.85f + 0.18f);
    } else if (params.tf_palette == 2) {
        *r = fminf(1.0f, v * 1.3f);
        *g = fminf(1.0f, fmaxf(0.0f, (v - 0.25f) * 1.4f));
        *b = fminf(1.0f, fmaxf(0.0f, (v - 0.65f) * 2.2f));
    } else if (params.tf_palette == 3) {
        *r = fminf(1.0f, 0.15f + v * 0.55f);
        *g = fminf(1.0f, 0.25f + v * 0.75f);
        *b = fminf(1.0f, 0.35f + v * 0.95f);
    } else {
        *r = v;
        *g = v;
        *b = v;
    }

    *a = fminf(1.0f, v * 0.6f * fmaxf(params.tf_opacity_scale, 0.0f));
}


/* ============================================================
   Phong Lighting
   ============================================================ */

__host__ __device__ static inline float3 apply_phong(
    float3 baseColor,
    float3 N,
    float3 L,
    float3 V,
    float  ambient,
    float  diffuse,
    float  specular,
    float  shininess)
{
    float NL = fmaxf(f3_dot(N, L), 0.0f);

    float3 R = f3_sub(
        f3_scale(N, 2.0f * NL),
        L
    );

    R = f3_normalize(R);

    float RV = fmaxf(f3_dot(R, V), 0.0f);
    float spec = powf(RV, shininess);

    float3 out;
    out.x = ambient * baseColor.x
          + diffuse * NL * baseColor.x
          + specular * spec;

    out.y = ambient * baseColor.y
          + diffuse * NL * baseColor.y
          + specular * spec;

    out.z = ambient * baseColor.z
          + diffuse * NL * baseColor.z
          + specular * spec;

    return out;
}

#ifdef __cplusplus
}
#endif

#endif /* MATH_UTILS_H */
