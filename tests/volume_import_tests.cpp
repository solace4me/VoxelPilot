#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/volume_import.h"
#include "../common/ui_state_helpers.h"
#include "../common/ai_features.h"

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
        voxelpilot_should_rebuild_dock_layout(1600.0f, 900.0f, 1920.0f, 1080.0f) == 0,
        "viewport resize should preserve the current dock layout");
}

static int test_layout_refit_only_on_maximize_transition(void)
{
    if (!expect_true(
            voxelpilot_should_refit_layout_after_maximize(0, 1) == 1,
            "restored-to-maximized transition should refit the fallback layout once")) {
        return 0;
    }

    if (!expect_true(
            voxelpilot_should_refit_layout_after_maximize(1, 1) == 0,
            "already-maximized windows should not force fixed panel widths every frame")) {
        return 0;
    }

    return expect_true(
        voxelpilot_should_refit_layout_after_maximize(1, 0) == 0,
        "restore transition should preserve user-adjusted panel sizes");
}

static int test_histogram_transfer_suggestion_centers_dense_region(void)
{
    float histogram[10] = { 0.0f };
    float center = 0.0f;
    float width = 0.0f;

    histogram[4] = 50.0f;
    histogram[5] = 50.0f;

    if (!expect_true(
            voxelpilot_suggest_transfer_window(
                histogram, 10, 0.10f, 0.90f, &center, &width) == 1,
            "transfer suggestion should succeed for a populated histogram")) {
        return 0;
    }

    if (!expect_float_close(center, 0.5f, "transfer suggestion should center the dense region")) {
        return 0;
    }

    return expect_true(
        width >= 0.05f && width <= 0.20f,
        "transfer suggestion should keep a usable narrow window");
}

static int test_focus_slice_scout_selects_high_variance_axial_slice(void)
{
    float volume[] = {
        0.0f, 0.0f,
        0.0f, 0.0f,

        0.0f, 1.0f,
        1.0f, 0.0f,

        0.2f, 0.2f,
        0.2f, 0.2f
    };
    float slice = 0.0f;
    float score = 0.0f;

    if (!expect_true(
            voxelpilot_find_high_variance_axial_slice(
                volume, 2, 2, 3, &slice, &score) == 1,
            "focus slice scout should succeed for a valid volume")) {
        return 0;
    }

    if (!expect_float_close(slice, 0.5f, "focus slice scout should select the middle slice")) {
        return 0;
    }

    return expect_true(
        score > 0.20f,
        "focus slice scout should report the selected slice variance");
}

static int test_render_extent_clamps_to_safe_range(void)
{
    if (!expect_true(
            voxelpilot_clamp_render_extent(12) == 64,
            "render extent should clamp low values")) {
        return 0;
    }

    if (!expect_true(
            voxelpilot_clamp_render_extent(9000) == 4096,
            "render extent should clamp high values")) {
        return 0;
    }

    return expect_true(
        voxelpilot_clamp_render_extent(1920) == 1920,
        "render extent should preserve valid framebuffer sizes");
}

static int test_raymarch_step_limit_reaches_full_volume_at_fine_steps(void)
{
    if (!expect_true(
            voxelpilot_compute_raymarch_step_limit(0.002f, 2.0f, 4096) >= 1000,
            "raymarch step limit should cover the full volume at balanced step size")) {
        return 0;
    }

    if (!expect_true(
            voxelpilot_compute_raymarch_step_limit(0.0005f, 2.0f, 4096) >= 4000,
            "raymarch step limit should cover the full volume at the finest UI step size")) {
        return 0;
    }

    return expect_true(
        voxelpilot_compute_raymarch_step_limit(0.0001f, 2.0f, 2048) == 2048,
        "raymarch step limit should respect the hard cap");
}

static int test_render_target_sync_respects_match_window_mode(void)
{
    if (!expect_true(
            voxelpilot_should_sync_render_target(1, 512, 512, 1920, 1080) == 1,
            "match-window mode should sync changed framebuffer sizes")) {
        return 0;
    }

    if (!expect_true(
            voxelpilot_should_sync_render_target(1, 1920, 1080, 1920, 1080) == 0,
            "match-window mode should ignore unchanged framebuffer sizes")) {
        return 0;
    }

    return expect_true(
        voxelpilot_should_sync_render_target(0, 512, 512, 1920, 1080) == 0,
        "manual render mode should not sync framebuffer sizes");
}

static int test_prompt_parser_maps_density_commands_to_transfer_settings(void)
{
    VoxelPilotPromptResult result;

    if (!expect_true(
            voxelpilot_parse_prompt_action(
                "show only the high-density shell",
                &result) == 1,
            "prompt parser should recognize high-density shell commands")) {
        return 0;
    }

    if (!expect_true(
            result.set_transfer == 1 &&
            result.tf_center > 0.70f &&
            result.tf_width < 0.35f,
            "high-density prompt should produce a narrow high-intensity transfer window")) {
        return 0;
    }

    if (!expect_true(
            voxelpilot_parse_prompt_action(
                "hide everything below 0.3 intensity",
                &result) == 1,
            "prompt parser should recognize numeric hide-below commands")) {
        return 0;
    }

    if (!expect_float_close(result.min_intensity, 0.3f, "hide-below prompt should extract numeric threshold")) {
        return 0;
    }

    return expect_true(
        result.set_min_intensity == 1 &&
        result.tf_center > 0.60f &&
        result.tf_width > 0.60f,
        "hide-below prompt should expose the visible upper intensity range");
}


static int test_prompt_parser_recognizes_visibility_and_quality_intents(void)
{
    VoxelPilotPromptResult result;

    if (!expect_true(
            voxelpilot_parse_prompt_action(
                "please make this scan brighter and easier to see",
                &result) == 1,
            "prompt parser should recognize flexible visibility language")) {
        return 0;
    }

    if (!expect_true(
            result.auto_enhance == 1,
            "visibility prompt should request histogram-based auto enhancement")) {
        return 0;
    }

    if (!expect_true(
            voxelpilot_parse_prompt_action(
                "switch to high quality gpu rendering",
                &result) == 1,
            "prompt parser should recognize quality-mode language")) {
        return 0;
    }

    return expect_true(
        result.set_quality == 1 &&
        result.quality_mode == VOXELPILOT_RENDER_QUALITY_QUALITY,
        "quality prompt should select the high-quality GPU preset");
}

static int test_auto_enhance_transfer_boosts_sparse_visible_histograms(void)
{
    float histogram[8] = { 0.0f };
    float center = 0.0f;
    float width = 0.0f;
    float opacity = 0.0f;

    histogram[0] = 600.0f;
    histogram[2] = 180.0f;
    histogram[3] = 80.0f;
    histogram[5] = 25.0f;
    histogram[6] = 8.0f;

    if (!expect_true(
            voxelpilot_compute_auto_enhance_transfer(
                histogram,
                8,
                &center,
                &width,
                &opacity) == 1,
            "auto-enhance helper should accept a non-empty histogram")) {
        return 0;
    }

    if (!expect_true(
            center > 0.20f &&
            center < 0.60f &&
            width >= 0.20f,
            "auto-enhance helper should frame the informative intensity band")) {
        return 0;
    }

    return expect_true(
        opacity > 1.35f,
        "auto-enhance helper should boost opacity for sparse visible samples");
}



static int test_auto_enhance_load_guard_preserves_high_background_ct_defaults(void)
{
    float histogram[128] = { 0.0f };
    int bin_index;

    histogram[0] = 11994229.0f;
    for (bin_index = 1; bin_index <= 20; ++bin_index) {
        histogram[bin_index] = 60000.0f + (float)bin_index * 8000.0f;
    }
    for (bin_index = 21; bin_index <= 80; ++bin_index) {
        histogram[bin_index] = 180000.0f - (float)(bin_index - 21) * 2500.0f;
    }
    for (bin_index = 81; bin_index <= 126; ++bin_index) {
        histogram[bin_index] = 2500.0f;
    }
    histogram[127] = 50548.0f;

    return expect_true(
        voxelpilot_should_auto_enhance_on_load(histogram, 128) == 0,
        "high-background CT-like volumes should preserve default transfer on load");
}

static int test_auto_enhance_boosts_compact_low_contrast_histograms(void)
{
    float histogram[64] = { 0.0f };
    float center = 0.0f;
    float width = 0.0f;
    float opacity = 0.0f;
    int bin_index;

    for (bin_index = 5; bin_index <= 27; ++bin_index) {
        histogram[bin_index] = (float)(28 - bin_index) * 12.0f;
    }
    histogram[50] = 1.0f;
    histogram[63] = 1.0f;

    if (!expect_true(
            voxelpilot_compute_auto_enhance_transfer(
                histogram,
                64,
                &center,
                &width,
                &opacity) == 1,
            "auto-enhance should accept compact low-contrast histograms")) {
        return 0;
    }

    if (!expect_true(
            center > 0.10f &&
            center < 0.40f &&
            width >= 0.20f,
            "compact low-contrast auto-enhance should frame the dense signal band")) {
        return 0;
    }

    if (!expect_true(
            opacity >= 1.75f,
            "compact low-contrast auto-enhance should boost opacity enough to be visibly useful")) {
        return 0;
    }

    return expect_true(
        voxelpilot_should_auto_enhance_on_load(histogram, 64) == 1,
        "compact low-contrast volumes are safe to auto-enhance on load");
}

static int test_flood_fill_labels_connected_intensity_region(void)
{
    float volume[] = {
        0.10f, 0.82f, 0.10f,
        0.80f, 0.81f, 0.20f,
        0.10f, 0.79f, 0.10f
    };
    unsigned char mask[9] = { 0 };
    size_t count = 0;
    float min_value = 0.0f;
    float max_value = 0.0f;

    if (!expect_true(
            voxelpilot_flood_fill_label_region(
                volume, 3, 3, 1,
                1, 1, 0,
                0.035f,
                2,
                mask,
                &count,
                &min_value,
                &max_value) == 1,
            "flood fill should label a valid seed region")) {
        return 0;
    }

    if (!expect_true(count == 4, "flood fill should include connected voxels within tolerance")) {
        return 0;
    }

    if (!expect_true(mask[1] == 2 && mask[3] == 2 && mask[4] == 2 && mask[7] == 2,
                     "flood fill should write the selected label id into the mask")) {
        return 0;
    }

    if (!expect_float_close(min_value, 0.79f, "flood fill should report min region intensity")) {
        return 0;
    }

    return expect_float_close(max_value, 0.82f, "flood fill should report max region intensity");
}

static int test_object_description_prefers_labels_then_density_classes(void)
{
    char description[128];

    voxelpilot_describe_voxel_context(
        0.92f,
        3,
        "Tibia candidate",
        description,
        sizeof(description));

    if (!expect_true(
            strstr(description, "Tibia candidate") != NULL,
            "object description should prefer explicit annotation labels")) {
        return 0;
    }

    voxelpilot_describe_voxel_context(
        0.88f,
        0,
        "",
        description,
        sizeof(description));

    return expect_true(
        strstr(description, "bone-like") != NULL,
        "unlabeled high-density voxels should be described as bone-like material");
}

static int test_ray_pick_hits_first_visible_voxel(void)
{
    float volume[27] = { 0.0f };
    float origin[3] = { 0.5f, 0.5f, -0.5f };
    float direction[3] = { 0.0f, 0.0f, 1.0f };
    float clip_min[3] = { 0.0f, 0.0f, 0.0f };
    float clip_max[3] = { 1.0f, 1.0f, 1.0f };
    VoxelPilotRayPickResult pick;

    volume[1 * 9 + 1 * 3 + 1] = 0.82f;
    memset(&pick, 0, sizeof(pick));

    if (!expect_true(
            voxelpilot_pick_volume_along_ray(
                volume,
                3,
                3,
                3,
                origin,
                direction,
                0.025f,
                0.50f,
                clip_min,
                clip_max,
                &pick) == 1,
            "ray picking should hit the first visible voxel")) {
        return 0;
    }

    if (!expect_true(pick.hit == 1, "ray pick result should be marked as a hit")) {
        return 0;
    }
    if (!expect_true(pick.x == 1 && pick.y == 1 && pick.z == 1,
                     "ray pick should report the expected voxel coordinates")) {
        return 0;
    }

    return expect_float_close(pick.intensity, 0.82f, "ray pick should report sampled intensity");
}

static int test_ray_pick_respects_clip_bounds(void)
{
    float volume[27] = { 0.0f };
    float origin[3] = { 0.5f, 0.5f, -0.5f };
    float direction[3] = { 0.0f, 0.0f, 1.0f };
    float clip_min[3] = { 0.0f, 0.0f, 0.75f };
    float clip_max[3] = { 1.0f, 1.0f, 1.0f };
    VoxelPilotRayPickResult pick;

    volume[1 * 9 + 1 * 3 + 1] = 0.82f;
    memset(&pick, 0, sizeof(pick));

    if (!expect_true(
            voxelpilot_pick_volume_along_ray(
                volume,
                3,
                3,
                3,
                origin,
                direction,
                0.025f,
                0.50f,
                clip_min,
                clip_max,
                &pick) == 0,
            "ray picking should ignore voxels outside clipping bounds")) {
        return 0;
    }

    return expect_true(pick.hit == 0, "clipped ray pick should not report a hit");
}

static int test_label_overlay_blends_annotation_color(void)
{
    float out_r = 0.0f;
    float out_g = 0.0f;
    float out_b = 0.0f;

    if (!expect_true(
            voxelpilot_blend_label_overlay(
                0.2f, 0.2f, 0.2f,
                1.0f, 0.0f, 0.0f,
                0.50f,
                &out_r,
                &out_g,
                &out_b) == 1,
            "label overlay blend should accept valid color channels")) {
        return 0;
    }

    if (!expect_float_close(out_r, 0.6f, "label overlay should lift red channel")) {
        return 0;
    }
    if (!expect_float_close(out_g, 0.1f, "label overlay should reduce green channel")) {
        return 0;
    }

    return expect_float_close(out_b, 0.1f, "label overlay should reduce blue channel");
}

static int test_object_summary_identifies_high_density_dominance(void)
{
    float histogram[8] = { 1.0f, 1.0f, 2.0f, 3.0f, 5.0f, 20.0f, 30.0f, 38.0f };
    VoxelPilotObjectSummary summary;

    if (!expect_true(
            voxelpilot_summarize_object_context(
                histogram,
                8,
                0,
                &summary) == 1,
            "object summary should accept a populated histogram")) {
        return 0;
    }

    if (!expect_true(
            strstr(summary.primary_material, "High-density") != NULL,
            "object summary should identify high-density dominant volumes")) {
        return 0;
    }

    return expect_true(
        summary.confidence > 0.50f,
        "object summary should report useful confidence for dominant material");
}

static int test_quant_metrics_summarize_histogram_without_volume_scan(void)
{
    float histogram[10] = { 0.0f };
    VoxelPilotQuantMetrics metrics;

    histogram[0] = 5.0f;
    histogram[4] = 10.0f;
    histogram[9] = 5.0f;
    memset(&metrics, 0, sizeof(metrics));

    if (!expect_true(
            voxelpilot_compute_quant_metrics_from_histogram(
                histogram,
                10,
                0.30f,
                &metrics) == 1,
            "quant metrics should summarize a populated histogram")) {
        return 0;
    }

    if (!expect_float_close(metrics.total_samples, 20.0f, "quant metrics should count total samples")) {
        return 0;
    }

    if (!expect_float_close(metrics.visible_ratio, 0.75f, "quant metrics should estimate visible ratio above threshold")) {
        return 0;
    }

    if (!expect_true(
            metrics.percentile_50 > 0.40f &&
            metrics.percentile_50 < 0.50f,
            "quant metrics should estimate median intensity")) {
        return 0;
    }

    return expect_true(
        strstr(metrics.summary, "visible") != NULL,
        "quant metrics should produce a report-ready summary");
}

static int test_streaming_estimate_counts_visible_nonempty_bricks(void)
{
    BrickInfo bricks[2];
    float clip_min[3] = { 0.0f, 0.0f, 0.0f };
    float clip_max[3] = { 0.49f, 1.0f, 1.0f };
    int visible = 0;
    int resident = 0;

    memset(bricks, 0, sizeof(bricks));
    bricks[0].sizeX = 4;
    bricks[0].sizeY = 4;
    bricks[0].sizeZ = 4;
    bricks[0].offsetX = 0;
    bricks[0].offsetY = 0;
    bricks[0].offsetZ = 0;
    bricks[0].isEmpty = 0;

    bricks[1].sizeX = 4;
    bricks[1].sizeY = 4;
    bricks[1].sizeZ = 4;
    bricks[1].offsetX = 4;
    bricks[1].offsetY = 0;
    bricks[1].offsetZ = 0;
    bricks[1].isEmpty = 0;

    if (!expect_true(
            voxelpilot_estimate_visible_bricks(
                bricks, 2, 8, 4, 4, clip_min, clip_max, &visible, &resident) == 1,
            "streaming estimate should accept valid brick metadata")) {
        return 0;
    }

    if (!expect_true(visible == 1, "streaming estimate should count bricks intersecting the clip box")) {
        return 0;
    }

    return expect_true(
        resident == 1,
        "streaming estimate should count visible non-empty bricks as resident candidates");
}

static int test_brick_cache_plan_reports_queue_and_eviction_pressure(void)
{
    VoxelPilotBrickCachePlan plan;

    if (!expect_true(
            voxelpilot_plan_brick_cache(
                10,
                7,
                4,
                16,
                &plan) == 1,
            "brick cache planner should accept valid visible/resident counts")) {
        return 0;
    }

    if (!expect_true(plan.stream_now == 4, "cache planner should cap streamed bricks by budget")) {
        return 0;
    }

    if (!expect_true(plan.queued == 3, "cache planner should queue resident candidates over budget")) {
        return 0;
    }

    return expect_true(
        plan.evictable == 9,
        "cache planner should identify non-resident bricks as evictable");
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
    passed &= test_layout_refit_only_on_maximize_transition();
    passed &= test_histogram_transfer_suggestion_centers_dense_region();
    passed &= test_focus_slice_scout_selects_high_variance_axial_slice();
    passed &= test_render_extent_clamps_to_safe_range();
    passed &= test_render_target_sync_respects_match_window_mode();
    passed &= test_raymarch_step_limit_reaches_full_volume_at_fine_steps();
    passed &= test_prompt_parser_maps_density_commands_to_transfer_settings();
    passed &= test_prompt_parser_recognizes_visibility_and_quality_intents();
    passed &= test_auto_enhance_transfer_boosts_sparse_visible_histograms();
    passed &= test_auto_enhance_load_guard_preserves_high_background_ct_defaults();
    passed &= test_auto_enhance_boosts_compact_low_contrast_histograms();
    passed &= test_flood_fill_labels_connected_intensity_region();
    passed &= test_object_description_prefers_labels_then_density_classes();
    passed &= test_ray_pick_hits_first_visible_voxel();
    passed &= test_ray_pick_respects_clip_bounds();
    passed &= test_label_overlay_blends_annotation_color();
    passed &= test_object_summary_identifies_high_density_dominance();
    passed &= test_quant_metrics_summarize_histogram_without_volume_scan();
    passed &= test_streaming_estimate_counts_visible_nonempty_bricks();
    passed &= test_brick_cache_plan_reports_queue_and_eviction_pressure();

    if (!passed) {
        return 1;
    }

    printf("volume_import_tests: PASS\n");
    return 0;
}
