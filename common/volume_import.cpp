#include "volume_import.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t safe_strnlen_local(const char *text, size_t limit)
{
    size_t i;

    if (!text) return 0;
    for (i = 0; i < limit; ++i) {
        if (text[i] == '\0') {
            return i;
        }
    }
    return limit;
}

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0) {
        return;
    }

    if (!message) {
        error[0] = '\0';
        return;
    }

    snprintf(error, error_size, "%s", message);
}

static int checked_mul_size(size_t a, size_t b, size_t *out)
{
    if (!out) return -1;
    if (a != 0 && b > (SIZE_MAX / a)) {
        return -1;
    }
    *out = a * b;
    return 0;
}

static int bytes_per_voxel(VolumeDataType data_type, size_t *out_bytes)
{
    if (!out_bytes) return -1;

    switch (data_type) {
    case VOLUME_DATA_UINT8:
        *out_bytes = sizeof(uint8_t);
        return 0;
    case VOLUME_DATA_UINT16:
        *out_bytes = sizeof(uint16_t);
        return 0;
    case VOLUME_DATA_FLOAT32:
        *out_bytes = sizeof(float);
        return 0;
    default:
        return -1;
    }
}

static int parse_data_type_token(const char *token, VolumeDataType *out_data_type)
{
    if (!token || !out_data_type) return -1;

    if (strcmp(token, "uint8") == 0 || strcmp(token, "u8") == 0) {
        *out_data_type = VOLUME_DATA_UINT8;
        return 0;
    }
    if (strcmp(token, "uint16") == 0 || strcmp(token, "u16") == 0) {
        *out_data_type = VOLUME_DATA_UINT16;
        return 0;
    }
    if (strcmp(token, "float32") == 0 || strcmp(token, "f32") == 0) {
        *out_data_type = VOLUME_DATA_FLOAT32;
        return 0;
    }

    return -1;
}

int volume_import_parse_data_type(
    const char *text,
    VolumeDataType *out_data_type)
{
    char lower_text[32];
    size_t len;
    size_t i;

    if (!text || !out_data_type) return -1;

    len = safe_strnlen_local(text, sizeof(lower_text) - 1);
    for (i = 0; i < len; ++i) {
        lower_text[i] = (char)tolower((unsigned char)text[i]);
    }
    lower_text[len] = '\0';

    return parse_data_type_token(lower_text, out_data_type);
}

static const char *base_name(const char *path)
{
    const char *slash1;
    const char *slash2;
    const char *base;

    if (!path) return "";

    slash1 = strrchr(path, '\\');
    slash2 = strrchr(path, '/');
    base = path;

    if (slash1 && (!slash2 || slash1 > slash2)) {
        base = slash1 + 1;
    } else if (slash2) {
        base = slash2 + 1;
    }

    return base;
}

int volume_import_expected_bytes(
    const VolumeImportSpec *spec,
    size_t *out_expected_bytes)
{
    size_t voxel_count;
    size_t bytes;

    if (!spec || !out_expected_bytes) return -1;
    if (spec->width == 0 || spec->height == 0 || spec->depth == 0) return -1;
    if (bytes_per_voxel(spec->data_type, &bytes) != 0) return -1;

    if (checked_mul_size((size_t)spec->width, (size_t)spec->height, &voxel_count) != 0) {
        return -1;
    }
    if (checked_mul_size(voxel_count, (size_t)spec->depth, &voxel_count) != 0) {
        return -1;
    }
    if (checked_mul_size(voxel_count, bytes, out_expected_bytes) != 0) {
        return -1;
    }

    return 0;
}

int volume_import_infer_from_filename(
    const char *path,
    VolumeImportSpec *out_spec)
{
    char lower_name[512];
    char type_token[32];
    const char *name;
    size_t len;
    size_t i;

    if (!path || !out_spec) return -1;

    name = base_name(path);
    len = safe_strnlen_local(name, sizeof(lower_name) - 1);
    for (i = 0; i < len; ++i) {
        lower_name[i] = (char)tolower((unsigned char)name[i]);
    }
    lower_name[len] = '\0';

    for (i = 0; i < len; ++i) {
        unsigned int width;
        unsigned int height;
        unsigned int depth;

        if (!isdigit((unsigned char)lower_name[i])) {
            continue;
        }

        width = height = depth = 0;
        type_token[0] = '\0';

        if (sscanf(lower_name + i, "%ux%ux%u_%31[^.]", &width, &height, &depth, type_token) == 4) {
            VolumeDataType data_type;
            if (parse_data_type_token(type_token, &data_type) == 0) {
                out_spec->width = (uint32_t)width;
                out_spec->height = (uint32_t)height;
                out_spec->depth = (uint32_t)depth;
                out_spec->data_type = data_type;
                return 0;
            }
        }
    }

    return -1;
}

int volume_import_load(
    const char *path,
    const VolumeImportSpec *spec,
    float **out_data,
    size_t *out_voxel_count,
    char *error,
    size_t error_size)
{
    FILE *fp;
    __int64 file_size_64;
    size_t file_size;
    size_t expected_bytes;
    size_t voxel_count;
    size_t raw_bytes_per_voxel;
    unsigned char *raw_data;
    float *float_data;
    size_t i;

    if (out_data) *out_data = NULL;
    if (out_voxel_count) *out_voxel_count = 0;

    if (!path || !spec || !out_data || !out_voxel_count) {
        set_error(error, error_size, "Invalid volume import arguments.");
        return -1;
    }

    if (volume_import_expected_bytes(spec, &expected_bytes) != 0) {
        set_error(error, error_size, "Invalid volume import specification.");
        return -1;
    }

    if (bytes_per_voxel(spec->data_type, &raw_bytes_per_voxel) != 0) {
        set_error(error, error_size, "Unsupported volume data type.");
        return -1;
    }

    voxel_count = expected_bytes / raw_bytes_per_voxel;

    fp = fopen(path, "rb");
    if (!fp) {
        if (error && error_size > 0) {
            snprintf(error, error_size, "Cannot open file: %s", path);
        }
        return -1;
    }

#if defined(_WIN32)
    if (_fseeki64(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        set_error(error, error_size, "Could not inspect file size.");
        return -1;
    }
    file_size_64 = _ftelli64(fp);
#else
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        set_error(error, error_size, "Could not inspect file size.");
        return -1;
    }
    file_size_64 = ftell(fp);
#endif
    if (file_size_64 < 0) {
        fclose(fp);
        set_error(error, error_size, "Could not inspect file size.");
        return -1;
    }

#if defined(_WIN32)
    if ((unsigned __int64)file_size_64 > (unsigned __int64)SIZE_MAX) {
        fclose(fp);
        set_error(error, error_size, "Raw volume file is too large for this build.");
        return -1;
    }
#endif

    file_size = (size_t)file_size_64;
    rewind(fp);

    if (file_size != expected_bytes) {
        fclose(fp);
        if (error && error_size > 0) {
            snprintf(
                error, error_size,
                "File size mismatch: got %zu bytes, expected %zu for %ux%ux%u %s",
                file_size, expected_bytes,
                spec->width, spec->height, spec->depth,
                volume_import_data_type_label(spec->data_type));
        }
        return -1;
    }

    raw_data = (unsigned char *)malloc(expected_bytes);
    if (!raw_data) {
        fclose(fp);
        set_error(error, error_size, "Could not allocate raw file buffer.");
        return -1;
    }

    if (fread(raw_data, 1, expected_bytes, fp) != expected_bytes) {
        free(raw_data);
        fclose(fp);
        set_error(error, error_size, "Could not read the complete raw volume.");
        return -1;
    }

    fclose(fp);

    float_data = (float *)malloc(voxel_count * sizeof(float));
    if (!float_data) {
        free(raw_data);
        set_error(error, error_size, "Could not allocate float volume buffer.");
        return -1;
    }

    switch (spec->data_type) {
    case VOLUME_DATA_UINT8:
        for (i = 0; i < voxel_count; ++i) {
            float_data[i] = (float)raw_data[i] / 255.0f;
        }
        break;

    case VOLUME_DATA_UINT16:
    {
        const uint16_t *src = (const uint16_t *)raw_data;
        for (i = 0; i < voxel_count; ++i) {
            float_data[i] = (float)src[i] / 65535.0f;
        }
        break;
    }

    case VOLUME_DATA_FLOAT32:
    {
        const float *src = (const float *)raw_data;
        for (i = 0; i < voxel_count; ++i) {
            float_data[i] = src[i];
        }
        break;
    }

    default:
        free(raw_data);
        free(float_data);
        set_error(error, error_size, "Unsupported volume data type.");
        return -1;
    }

    free(raw_data);
    *out_data = float_data;
    *out_voxel_count = voxel_count;
    set_error(error, error_size, "");
    return 0;
}

void volume_import_free(float *data)
{
    free(data);
}

const char *volume_import_data_type_label(VolumeDataType data_type)
{
    switch (data_type) {
    case VOLUME_DATA_UINT8:
        return "uint8";
    case VOLUME_DATA_UINT16:
        return "uint16";
    case VOLUME_DATA_FLOAT32:
        return "float32";
    default:
        return "unknown";
    }
}
