#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/volume_import.h"
#include "../common/ui_state_helpers.h"

static int expect_true(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 0;
    }
    return 1;
}

static int expect_float_close(float actual, float expected, const char *message)
{
    if (fabsf(actual - expected) > 0.0001f) {
        fprintf(stderr, "FAIL: %s (actual=%f expected=%f)\n",
                message, actual, expected);
        return 0;
    }
    return 1;
}

static int write_temp_file(
    const char *name,
    const void *bytes,
    size_t byte_count,
    char *out_path,
    size_t out_path_size)
{
    FILE *fp;

    if (snprintf(out_path, out_path_size, "build\\%s", name) < 0) {
        return 0;
    }

    fp = fopen(out_path, "wb");
    if (!fp) {
        fprintf(stderr, "FAIL: could not create temp file %s\n", out_path);
        return 0;
    }

    if (fwrite(bytes, 1, byte_count, fp) != byte_count) {
        fclose(fp);
        fprintf(stderr, "FAIL: could not write temp file %s\n", out_path);
        return 0;
    }

    fclose(fp);
    return 1;
}

static int test_expected_bytes_non_cubic_uint16(void)
{
    VolumeImportSpec spec = { 256, 128, 64, VOLUME_DATA_UINT16 };
    size_t expected = 0;

    if (!expect_true(volume_import_expected_bytes(&spec, &expected) == 0,
                     "volume_import_expected_bytes should succeed")) {
        return 0;
    }

    return expect_true(
        expected == (size_t)256 * 128 * 64 * sizeof(uint16_t),
        "expected bytes should match W*H*D*sizeof(uint16_t)");
}

static int test_infer_spec_from_filename(void)
{
    VolumeImportSpec spec;
    memset(&spec, 0, sizeof(spec));

    if (!expect_true(
            volume_import_infer_from_filename(
                "sample-data\\vis_male_128x256x256_uint8.raw",
                &spec) == 0,
            "filename inference should succeed")) {
        return 0;
    }

    if (!expect_true(spec.width == 128, "width should be inferred")) return 0;
    if (!expect_true(spec.height == 256, "height should be inferred")) return 0;
    if (!expect_true(spec.depth == 256, "depth should be inferred")) return 0;
    return expect_true(
        spec.data_type == VOLUME_DATA_UINT8,
        "data type should be inferred as uint8");
}

static int test_parse_data_type_aliases(void)
{
    VolumeDataType data_type = VOLUME_DATA_UINT8;

    if (!expect_true(
            volume_import_parse_data_type("uint16", &data_type) == 0,
            "uint16 data type should parse")) {
        return 0;
    }
    if (!expect_true(data_type == VOLUME_DATA_UINT16,
                     "uint16 should map to VOLUME_DATA_UINT16")) {
        return 0;
    }

    if (!expect_true(
            volume_import_parse_data_type("F32", &data_type) == 0,
            "F32 alias should parse case-insensitively")) {
        return 0;
    }
    if (!expect_true(data_type == VOLUME_DATA_FLOAT32,
                     "F32 should map to VOLUME_DATA_FLOAT32")) {
        return 0;
    }

    return expect_true(
        volume_import_parse_data_type("bad-type", &data_type) != 0,
        "unknown data type should be rejected");
}

static int test_load_uint8_normalizes_values(void)
{
    const unsigned char bytes[] = { 0, 64, 128, 255 };
    const VolumeImportSpec spec = { 2, 2, 1, VOLUME_DATA_UINT8 };
    char path[260];
    char error[256];
    float *data = NULL;
    size_t voxel_count = 0;

    if (!write_temp_file("volume_import_uint8.raw", bytes, sizeof(bytes),
                         path, sizeof(path))) {
        return 0;
    }

    if (!expect_true(
            volume_import_load(path, &spec, &data, &voxel_count,
                               error, sizeof(error)) == 0,
            "uint8 load should succeed")) {
        return 0;
    }

    if (!expect_true(voxel_count == 4, "uint8 voxel count should match")) return 0;
    if (!expect_float_close(data[0], 0.0f, "uint8 zero should map to 0.0")) return 0;
    if (!expect_float_close(data[1], 64.0f / 255.0f, "uint8 64 should be normalized")) return 0;
    if (!expect_float_close(data[2], 128.0f / 255.0f, "uint8 128 should be normalized")) return 0;
    if (!expect_float_close(data[3], 1.0f, "uint8 255 should map to 1.0")) return 0;

    volume_import_free(data);
    return 1;
}

static int test_load_uint16_normalizes_values(void)
{
    const uint16_t bytes[] = { 0, 32768, 65535, 16384 };
    const VolumeImportSpec spec = { 2, 2, 1, VOLUME_DATA_UINT16 };
    char path[260];
    char error[256];
    float *data = NULL;
    size_t voxel_count = 0;

    if (!write_temp_file("volume_import_uint16.raw", bytes, sizeof(bytes),
                         path, sizeof(path))) {
        return 0;
    }

    if (!expect_true(
            volume_import_load(path, &spec, &data, &voxel_count,
                               error, sizeof(error)) == 0,
            "uint16 load should succeed")) {
        return 0;
    }

    if (!expect_true(voxel_count == 4, "uint16 voxel count should match")) return 0;
    if (!expect_float_close(data[0], 0.0f, "uint16 zero should map to 0.0")) return 0;
    if (!expect_float_close(data[1], 32768.0f / 65535.0f, "uint16 mid should be normalized")) return 0;
    if (!expect_float_close(data[2], 1.0f, "uint16 max should map to 1.0")) return 0;
    if (!expect_float_close(data[3], 16384.0f / 65535.0f, "uint16 low value should be normalized")) return 0;

    volume_import_free(data);
    return 1;
}

static int test_rejects_mismatched_file_size(void)
{
    const unsigned char bytes[] = { 1, 2, 3 };
    const VolumeImportSpec spec = { 2, 2, 1, VOLUME_DATA_UINT8 };
    char path[260];
    char error[256];
    float *data = NULL;
    size_t voxel_count = 0;

    if (!write_temp_file("volume_import_bad_size.raw", bytes, sizeof(bytes),
                         path, sizeof(path))) {
        return 0;
    }

    if (!expect_true(
            volume_import_load(path, &spec, &data, &voxel_count,
                               error, sizeof(error)) != 0,
            "mismatched file size should fail")) {
        return 0;
    }

    return expect_true(
        strstr(error, "expected") != NULL,
        "error message should explain expected file size");
}

static int test_default_clip_bounds_start_unclipped(void)
{
    float clip_min[3] = { 1.0f, 1.0f, 1.0f };
    float clip_max[3] = { 0.0f, 0.0f, 0.0f };

    voxelpilot_set_default_clip_bounds(clip_min, clip_max);

    if (!expect_float_close(clip_min[0], 0.0f, "clip_min x should reset to 0")) return 0;
    if (!expect_float_close(clip_min[1], 0.0f, "clip_min y should reset to 0")) return 0;
    if (!expect_float_close(clip_min[2], 0.0f, "clip_min z should reset to 0")) return 0;
    if (!expect_float_close(clip_max[0], 1.0f, "clip_max x should reset to 1")) return 0;
    if (!expect_float_close(clip_max[1], 1.0f, "clip_max y should reset to 1")) return 0;
    if (!expect_float_close(clip_max[2], 1.0f, "clip_max z should reset to 1")) return 0;

    return expect_true(
        voxelpilot_clip_is_active(clip_min, clip_max) == 0,
        "default clip bounds should leave clipping disabled");
}

static int test_dock_layout_rebuild_detection(void)
{
    if (!expect_true(
            voxelpilot_should_rebuild_dock_layout(0.0f, 0.0f, 1600.0f, 900.0f) == 1,
            "first layout should require a dock build")) {
        return 0;
    }

    if (!expect_true(
            voxelpilot_should_rebuild_dock_layout(1600.0f, 900.0f, 1600.0f, 900.0f) == 0,
            "unchanged viewport size should not rebuild the dock layout")) {
        return 0;
    }

    return expect_true(
        voxelpilot_should_rebuild_dock_layout(1600.0f, 900.0f, 1920.0f, 1080.0f) == 1,
        "viewport resize should trigger dock layout refresh");
}

int main(void)
{
    int passed = 1;

    passed &= test_expected_bytes_non_cubic_uint16();
    passed &= test_infer_spec_from_filename();
    passed &= test_parse_data_type_aliases();
    passed &= test_load_uint8_normalizes_values();
    passed &= test_load_uint16_normalizes_values();
    passed &= test_rejects_mismatched_file_size();
    passed &= test_default_clip_bounds_start_unclipped();
    passed &= test_dock_layout_rebuild_detection();

    if (!passed) {
        return 1;
    }

    printf("volume_import_tests: PASS\n");
    return 0;
}
