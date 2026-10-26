#ifndef VOLUME_IMPORT_H
#define VOLUME_IMPORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VolumeDataType {
    VOLUME_DATA_UINT8 = 0,
    VOLUME_DATA_UINT16 = 1,
    VOLUME_DATA_FLOAT32 = 2
} VolumeDataType;

typedef struct VolumeImportSpec {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    VolumeDataType data_type;
} VolumeImportSpec;

int volume_import_expected_bytes(
    const VolumeImportSpec *spec,
    size_t *out_expected_bytes);

int volume_import_infer_from_filename(
    const char *path,
    VolumeImportSpec *out_spec);

int volume_import_parse_data_type(
    const char *text,
    VolumeDataType *out_data_type);

int volume_import_load(
    const char *path,
    const VolumeImportSpec *spec,
    float **out_data,
    size_t *out_voxel_count,
    char *error,
    size_t error_size);

void volume_import_free(float *data);

/* Box-filter downsampling by an integer factor: each output voxel is the
   mean of the factor^3 block it covers (partial blocks at odd edges
   average what they have). Output dimensions are ceil(dim / factor).
   Used when a scan does not fit the GPU at full resolution. Free the
   result with volume_import_free. Returns 0 on success. */
int volume_import_downsample(
    const float *src,
    int          width,
    int          height,
    int          depth,
    int          factor,
    float      **out_data,
    int         *out_width,
    int         *out_height,
    int         *out_depth);

/* Copy the sub-block [origin, origin + size) out of a volume (x fastest).
   Used to load a clipped region at full resolution. Free the result with
   volume_import_free. Returns 0 on success, -1 if the block does not lie
   inside the volume or memory runs out (out_data left NULL). */
int volume_import_crop(
    const float *src,
    int          width,
    int          height,
    int          depth,
    const int    origin[3],
    const int    size[3],
    float      **out_data);

const char *volume_import_data_type_label(VolumeDataType data_type);

#ifdef __cplusplus
}
#endif

#endif /* VOLUME_IMPORT_H */
