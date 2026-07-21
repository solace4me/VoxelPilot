#ifndef VOXELPILOT_CUDA_DEVICE_POLICY_H
#define VOXELPILOT_CUDA_DEVICE_POLICY_H

#ifdef __cplusplus
extern "C" {
#endif

static inline int voxelpilot_cuda_hint_device_is_valid(
    int active_device,
    int device_count)
{
    return active_device >= 0 && device_count > 0 && active_device < device_count;
}

#ifdef __cplusplus
}
#endif

#endif /* VOXELPILOT_CUDA_DEVICE_POLICY_H */
