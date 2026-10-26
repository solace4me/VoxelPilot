#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/volume_import.h"
#include "../common/cli_options.h"
#include "../common/json_utils.h"
#include "../common/cuda_device_policy.h"
#include "../common/ui_state_helpers.h"
#include "../common/default_state.h"
#include "../common/histogram_profile.h"
#include "../common/ai_features.h"
#include "../common/semantic_discovery.h"
#include "../common/performance_governor.h"
#include "../common/render_command_builder.h"
#include "../common/ai_review_bundle.h"
#include "../common/insight_report.h"
#include "../common/volume_geometry.h"

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

static int read_text_file(const char *path, char *buffer, size_t buffer_size)
{
    FILE *fp;
    size_t byte_count;

    if (!path || !buffer || buffer_size == 0) {
        return 0;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "FAIL: could not open text file %s\n", path);
        return 0;
    }

    byte_count = fread(buffer, 1, buffer_size - 1, fp);
    if (ferror(fp)) {
        fclose(fp);
        fprintf(stderr, "FAIL: could not read text file %s\n", path);
        return 0;
    }

    buffer[byte_count] = '\0';
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

/* Real CT/MRI uint16 data rarely uses the full 0..65535 range (the carp
   sample tops out at 2871). Dividing by 65535 squeezed such a scan into
   the bottom few percent of [0, 1], where no transfer window could tell
   tissues apart; uint16 is scaled by the data's own min..max instead. */
static int test_load_uint16_scales_to_data_range(void)
{
    const uint16_t bytes[] = { 100, 1100, 2100, 600 };
    const VolumeImportSpec spec = { 2, 2, 1, VOLUME_DATA_UINT16 };
    char path[260];
    char error[256];
    float *data = NULL;
    size_t voxel_count = 0;

    if (!write_temp_file("volume_import_uint16_range.raw", bytes, sizeof(bytes),
                         path, sizeof(path))) {
        return 0;
    }

    if (!expect_true(
            volume_import_load(path, &spec, &data, &voxel_count,
                               error, sizeof(error)) == 0,
            "uint16 partial-range load should succeed")) {
        return 0;
    }

    if (!expect_float_close(data[0], 0.0f, "uint16 data minimum should map to 0.0")) return 0;
    if (!expect_float_close(data[1], 0.5f, "uint16 mid-range value should map to 0.5")) return 0;
    if (!expect_float_close(data[2], 1.0f, "uint16 data maximum should map to 1.0")) return 0;
    if (!expect_float_close(data[3], 0.25f, "uint16 quarter-range value should map to 0.25")) return 0;

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

static int test_review_bundle_paths_sanitize_stem_and_join_folder(void)
{
    VoxelPilotReviewBundlePaths paths;
    memset(&paths, 0, sizeof(paths));

    if (!expect_true(
            voxelpilot_make_review_bundle_paths(
                "C:\\tmp\\VoxelPilot",
                "Bonsai CT sample",
                &paths) == 1,
            "review bundle path builder should accept folder and stem")) {
        return 0;
    }

    if (!expect_true(
            strcmp(paths.snapshot_png, "C:\\tmp\\VoxelPilot\\Bonsai_CT_sample_snapshot.png") == 0,
            "review bundle should produce deterministic snapshot path")) {
        return 0;
    }
    if (!expect_true(
            strcmp(paths.regions_json, "C:\\tmp\\VoxelPilot\\Bonsai_CT_sample_regions.json") == 0,
            "review bundle should produce deterministic regions JSON path")) {
        return 0;
    }
    if (!expect_true(
            strcmp(paths.relationships_json, "C:\\tmp\\VoxelPilot\\Bonsai_CT_sample_relationships.json") == 0,
            "review bundle should produce deterministic relationships JSON path")) {
        return 0;
    }
    if (!expect_true(
            strcmp(paths.label_mask_raw, "C:\\tmp\\VoxelPilot\\Bonsai_CT_sample_label_mask.raw") == 0,
            "review bundle should produce deterministic label mask path")) {
        return 0;
    }
    return expect_true(
        strcmp(paths.report_html, "C:\\tmp\\VoxelPilot\\Bonsai_CT_sample_report.html") == 0,
        "review bundle should produce deterministic HTML report path");
}

static int test_review_bundle_paths_reject_overlong_output(void)
{
    VoxelPilotReviewBundlePaths paths;
    char long_folder[VOXELPILOT_REVIEW_BUNDLE_PATH_MAX];
    memset(&paths, 0, sizeof(paths));
    memset(long_folder, 'a', sizeof(long_folder));
    long_folder[sizeof(long_folder) - 1] = '\0';

    return expect_true(
        voxelpilot_make_review_bundle_paths(long_folder, "bundle", &paths) == 0,
        "review bundle path builder should reject truncated paths");
}


static int test_ai_review_bundle_stem_uses_source_basename(void)
{
    char stem[128];

    memset(stem, 0, sizeof(stem));
    if (!expect_true(
            voxelpilot_make_ai_review_bundle_stem_from_source(
                "C:\\data\\Bonsai CT.sample.raw",
                stem,
                sizeof(stem)) == 1,
            "AI review bundle stem should build from source path")) {
        return 0;
    }
    if (!expect_true(
            strcmp(stem, "Bonsai CT.sample") == 0,
            "AI review bundle stem should use basename without extension")) {
        return 0;
    }

    memset(stem, 0, sizeof(stem));
    if (!expect_true(
            voxelpilot_make_ai_review_bundle_stem_from_source(
                NULL,
                stem,
                sizeof(stem)) == 1,
            "AI review bundle stem should accept a missing source path")) {
        return 0;
    }
    return expect_true(
        strcmp(stem, "voxelpilot_ai_review") == 0,
        "AI review bundle stem should use a deterministic default");
}

static int test_ai_review_bundle_writes_regions_json(void)
{
    const char *path = "build\\ai_review_regions_test.json";
    char text[4096];
    VoxelPilotAiReviewBundleContent content;
    VoxelPilotSemanticRegion region;

    voxelpilot_ai_review_bundle_content_defaults(&content);
    memset(&region, 0, sizeof(region));
    content.volume_width = 2;
    content.volume_height = 3;
    content.volume_depth = 4;
    content.selected_label_id = 7;
    content.regions = &region;
    content.region_count = 1;
    region.label_id = 7;
    strcpy(region.name, "Dense \"core\"");
    strcpy(region.archetype, "blob");
    strcpy(region.category, "A&B");
    strcpy(region.description, "line\nbreak");
    region.voxel_count = 42;
    region.confidence = 0.75f;
    region.stability_score = 0.5f;
    region.importance_score = 0.25f;

    if (!expect_true(
            voxelpilot_ai_review_write_regions_json(&content, path) == 1,
            "AI review bundle should write regions JSON")) {
        return 0;
    }
    if (!read_text_file(path, text, sizeof(text))) {
        return 0;
    }
    if (!expect_true(
            strstr(text, "\"selectedLabelId\": 7") != NULL,
            "regions JSON should include selected label id")) {
        return 0;
    }
    if (!expect_true(
            strstr(text, "Dense \\\"core\\\"") != NULL,
            "regions JSON should escape quotes")) {
        return 0;
    }
    return expect_true(
        strstr(text, "line\\nbreak") != NULL,
        "regions JSON should escape newlines");
}

static int test_ai_review_bundle_writes_relationships_and_label_mask(void)
{
    const char *relationships_path = "build\\ai_review_relationships_test.json";
    const char *mask_path = "build\\ai_review_mask_test.raw";
    unsigned char mask[] = { 1, 2, 3, 4 };
    unsigned char written_mask[4] = { 0, 0, 0, 0 };
    char text[4096];
    FILE *fp;
    VoxelPilotAiReviewBundleContent content;
    VoxelPilotRegionRelation relation;

    voxelpilot_ai_review_bundle_content_defaults(&content);
    memset(&relation, 0, sizeof(relation));
    content.relations = &relation;
    content.relation_count = 1;
    content.label_mask = mask;
    content.label_mask_width = 2;
    content.label_mask_height = 2;
    content.label_mask_depth = 1;
    relation.region_a = 1;
    relation.region_b = 2;
    relation.type = VOXELPILOT_REGION_RELATION_ADJACENT;
    strcpy(relation.description, "near & stable");
    relation.confidence = 0.875f;

    if (!expect_true(
            voxelpilot_ai_review_write_relationships_json(
                &content,
                relationships_path) == 1,
            "AI review bundle should write relationships JSON")) {
        return 0;
    }
    if (!read_text_file(relationships_path, text, sizeof(text))) {
        return 0;
    }
    if (!expect_true(
            strstr(text, "\"regionA\": 1") != NULL,
            "relationships JSON should include source region")) {
        return 0;
    }
    if (!expect_true(
            strstr(text, "near & stable") != NULL,
            "relationships JSON should include escaped description")) {
        return 0;
    }

    if (!expect_true(
            voxelpilot_ai_review_write_label_mask(&content, mask_path) == 1,
            "AI review bundle should write label mask")) {
        return 0;
    }
    fp = fopen(mask_path, "rb");
    if (!expect_true(fp != NULL, "AI review label mask output should exist")) {
        return 0;
    }
    if (!expect_true(
            fread(written_mask, 1, sizeof(written_mask), fp) == sizeof(written_mask),
            "AI review label mask should contain the full mask")) {
        fclose(fp);
        return 0;
    }
    if (!expect_true(
            fgetc(fp) == EOF,
            "AI review label mask should not write trailing bytes")) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return expect_true(
        memcmp(mask, written_mask, sizeof(mask)) == 0,
        "AI review label mask should preserve byte values");
}

static int test_insight_report_writer_escapes_content_and_annotations(void)
{
    VoxelPilotInsightReportContent content;
    VoxelPilotInsightAnnotation annotations[1];
    VoxelPilotQuantMetrics metrics;
    VoxelPilotObjectSummary summary;
    char path[256];
    char html[8192];

    snprintf(path, sizeof(path), "build\\insight_report_writer_test.html");
    memset(&metrics, 0, sizeof(metrics));
    memset(&summary, 0, sizeof(summary));
    memset(annotations, 0, sizeof(annotations));
    voxelpilot_insight_report_content_defaults(&content);

    metrics.visible_ratio = 0.42f;
    metrics.percentile_50 = 0.50f;
    metrics.percentile_95 = 0.95f;
    metrics.mean_intensity = 0.48f;
    metrics.low_density_ratio = 0.25f;
    metrics.mid_density_ratio = 0.50f;
    metrics.high_density_ratio = 0.25f;
    snprintf(metrics.summary, sizeof(metrics.summary), "metrics <ready>");

    snprintf(summary.primary_material, sizeof(summary.primary_material), "Bone <shell>");
    snprintf(summary.description, sizeof(summary.description), "summary <safe>");

    annotations[0].active = 1;
    annotations[0].label_id = 7;
    annotations[0].name = "ROI <danger>";
    annotations[0].voxel_count = 12;
    annotations[0].min_intensity = 0.10f;
    annotations[0].max_intensity = 0.90f;

    content.volume_width = 2;
    content.volume_height = 3;
    content.volume_depth = 4;
    content.hist_min_value = 0.10f;
    content.hist_max_value = 0.95f;
    content.voxel_spacing[0] = 1.0f;
    content.voxel_spacing[1] = 2.0f;
    content.voxel_spacing[2] = 3.0f;
    content.source_path = "scan <raw>.raw";
    content.quant_metrics = &metrics;
    content.quant_metrics_status = metrics.summary;
    content.object_summary = &summary;
    content.object_summary_status = "object <summary>";
    content.measure_a[0] = 0.1f;
    content.measure_a[1] = 0.2f;
    content.measure_a[2] = 0.3f;
    content.measure_b[0] = 0.4f;
    content.measure_b[1] = 0.5f;
    content.measure_b[2] = 0.6f;
    content.measurement_distance_voxels = 10.0f;
    content.measurement_distance_world = 20.0f;
    content.annotations = annotations;
    content.annotation_count = 1;
    content.last_render_ms = 16.5f;
    content.gui_fps = 60.0f;
    content.streaming_visible_bricks = 2;
    content.streaming_resident_candidates = 3;
    content.streaming_stream_now = 1;
    content.streaming_queue = 4;
    content.streaming_evictable = 5;

    if (!expect_true(
            voxelpilot_insight_write_report(&content, path) == 1,
            "insight report writer should write html")) {
        return 0;
    }
    if (!expect_true(read_text_file(path, html, sizeof(html)),
            "insight report output should be readable")) {
        return 0;
    }
    if (!expect_true(strstr(html, "VoxelPilot Insight Report") != NULL,
            "insight report should include title")) {
        return 0;
    }
    if (!expect_true(strstr(html, "scan &lt;raw&gt;.raw") != NULL,
            "insight report should escape source path")) {
        return 0;
    }
    if (!expect_true(strstr(html, "ROI &lt;danger&gt;") != NULL,
            "insight report should escape annotation names")) {
        return 0;
    }
    return expect_true(strstr(html, "2 x 3 x 4") != NULL,
        "insight report should include dimensions");
}
static int test_ai_review_bundle_report_references_artifacts(void)
{
    const char *path = "build\\ai_review_report_test.html";
    char text[4096];
    VoxelPilotAiReviewBundleContent content;
    VoxelPilotReviewBundlePaths paths;
    VoxelPilotSemanticRegion region;
    VoxelPilotRegionRelation relation;

    voxelpilot_ai_review_bundle_content_defaults(&content);
    memset(&paths, 0, sizeof(paths));
    memset(&region, 0, sizeof(region));
    memset(&relation, 0, sizeof(relation));

    content.volume_width = 8;
    content.volume_height = 9;
    content.volume_depth = 10;
    content.object_summary_status = "Object <summary>";
    content.regions = &region;
    content.region_count = 1;
    content.relations = &relation;
    content.relation_count = 1;
    region.label_id = 4;
    strcpy(region.name, "Region <A>");
    strcpy(region.archetype, "shell");
    relation.region_a = 4;
    relation.region_b = 5;
    strcpy(relation.description, "A <touches> B");
    strcpy(paths.snapshot_png, "C:\\tmp\\snap.png");
    strcpy(paths.regions_json, "C:\\tmp\\regions.json");
    strcpy(paths.relationships_json, "C:\\tmp\\relationships.json");
    strcpy(paths.label_mask_raw, "C:\\tmp\\mask.raw");

    if (!expect_true(
            voxelpilot_ai_review_write_bundle_report(&content, &paths, path) == 1,
            "AI review bundle should write HTML report")) {
        return 0;
    }
    if (!read_text_file(path, text, sizeof(text))) {
        return 0;
    }
    if (!expect_true(
            strstr(text, "VoxelPilot AI Review Bundle") != NULL,
            "AI review report should include title")) {
        return 0;
    }
    if (!expect_true(
            strstr(text, "Object &lt;summary&gt;") != NULL,
            "AI review report should escape object summary")) {
        return 0;
    }
    if (!expect_true(
            strstr(text, "snap.png") != NULL,
            "AI review report should reference snapshot artifact")) {
        return 0;
    }
    return expect_true(
        strstr(text, "A &lt;touches&gt; B") != NULL,
        "AI review report should escape relationship description");
}
static int test_cli_export_plan_runs_snapshot_and_review_bundle(void)
{
    VoxelPilotCliExportPlan plan;
    memset(&plan, 0, sizeof(plan));

    if (!expect_true(
            voxelpilot_cli_make_export_plan(
                "build\\requested_snapshot.png",
                "build\\review-bundle",
                &plan) == 1,
            "CLI export planner should accept snapshot and review bundle together")) {
        return 0;
    }

    if (!expect_true(plan.run_snapshot == 1,
                     "CLI export planner should run explicit snapshot export")) {
        return 0;
    }
    return expect_true(
        plan.run_review_bundle == 1,
        "CLI export planner should run review bundle export");
}


static int test_cli_prepare_export_folder_creates_missing_directory(void)
{
    const char *folder = "build\\voxelpilot_cli_created_bundle_dir";
    const char *probe_path = "build\\voxelpilot_cli_created_bundle_dir\\probe.txt";
    char error[128];
    FILE *fp;

    memset(error, 0, sizeof(error));

    if (!expect_true(
            voxelpilot_cli_prepare_export_folder(folder, error, sizeof(error)) == 1,
            "CLI export folder preparation should create a missing bundle directory")) {
        return 0;
    }

    fp = fopen(probe_path, "wb");
    if (!fp) {
        fprintf(stderr, "FAIL: prepared export folder was not writable: %s\n", error);
        return 0;
    }
    fputs("ok", fp);
    fclose(fp);
    return 1;
}

static int test_json_utils_write_escaped_control_chars(void)
{
    const char *path = "build\\json_escape_test.txt";
    const char *expected = "quote\\\" slash\\\\\\n\\r\\t\\u0001";
    char buffer[128];
    FILE *fp;
    size_t read_count;

    fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "FAIL: could not create JSON escape test file\n");
        return 0;
    }
    voxelpilot_json_write_escaped(fp, "quote\" slash\\\n\r\t\001");
    fclose(fp);

    memset(buffer, 0, sizeof(buffer));
    fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "FAIL: could not reopen JSON escape test file\n");
        return 0;
    }
    read_count = fread(buffer, 1, sizeof(buffer) - 1, fp);
    fclose(fp);
    buffer[read_count] = '\0';

    return expect_true(
        strcmp(buffer, expected) == 0,
        "JSON escape helper should preserve valid text and escape control characters");
}

static int test_json_utils_extracts_typed_fields(void)
{
    const char *line = "{\"name\": \"dense rod\", \"densityClass\": 3, \"meanIntensity\": 0.875}";
    char name[32];
    int density_class = 0;
    float mean_intensity = 0.0f;

    memset(name, 0, sizeof(name));

    if (!expect_true(
            voxelpilot_json_extract_string_field(line, "name", name, sizeof(name)) == 1 &&
            strcmp(name, "dense rod") == 0,
            "JSON string extractor should return the requested string field")) {
        return 0;
    }
    if (!expect_true(
            voxelpilot_json_extract_int_field(line, "densityClass", &density_class) == 1 &&
            density_class == 3,
            "JSON int extractor should return the requested integer field")) {
        return 0;
    }
    return expect_true(
        voxelpilot_json_extract_float_field(line, "meanIntensity", &mean_intensity) == 1 &&
        mean_intensity > 0.874f && mean_intensity < 0.876f,
        "JSON float extractor should return the requested floating-point field");
}

static int test_cuda_hint_policy_accepts_only_valid_devices(void)
{
    if (!expect_true(
            voxelpilot_cuda_hint_device_is_valid(0, 1) == 1,
            "CUDA hint policy should accept the first available device")) {
        return 0;
    }
    if (!expect_true(
            voxelpilot_cuda_hint_device_is_valid(1, 1) == 0,
            "CUDA hint policy should reject device ids beyond device count")) {
        return 0;
    }
    return expect_true(
        voxelpilot_cuda_hint_device_is_valid(-1, 1) == 0,
        "CUDA hint policy should reject negative active device ids");
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

static int test_cinematic_params_default_disabled(void)
{
    VoxelPilotCinematicParams params;
    voxelpilot_cinematic_defaults(&params);

    if (!expect_true(params.enabled == 0, "cinematic mode should be disabled by default")) return 0;
    if (!expect_float_close(params.rim_strength, 0.35f, "cinematic default rim strength")) return 0;
    if (!expect_float_close(params.glow_strength, 0.20f, "cinematic default glow strength")) return 0;
    return expect_float_close(params.contrast, 1.10f, "cinematic default contrast");
}

static int test_cinematic_params_clamp_safe_ranges(void)
{
    VoxelPilotCinematicParams params;
    params.enabled = 9;
    params.rim_strength = -2.0f;
    params.glow_strength = 9.0f;
    params.contrast = 3.0f;
    voxelpilot_cinematic_clamp(&params);

    if (!expect_true(params.enabled == 1, "cinematic enabled should clamp to one")) return 0;
    if (!expect_float_close(params.rim_strength, 0.0f, "cinematic rim should clamp low")) return 0;
    if (!expect_float_close(params.glow_strength, 1.0f, "cinematic glow should clamp high")) return 0;
    return expect_float_close(params.contrast, 1.6f, "cinematic contrast should clamp high");
}

static int test_semantic_demo_defaults_enable_visible_asd_callouts(void)
{
    VoxelPilotSemanticDemoSettings settings;
    memset(&settings, 0, sizeof(settings));

    voxelpilot_semantic_demo_defaults(&settings);

    if (!expect_true(settings.cinematic_enabled == 1,
            "semantic demo should enable cinematic mode")) {
        return 0;
    }
    if (!expect_true(settings.callouts_enabled == 1,
            "semantic demo should enable ASD callouts")) {
        return 0;
    }
    if (!expect_true(settings.callout_top_regions == 3,
            "semantic demo should show the top three ASD regions")) {
        return 0;
    }
    if (!expect_true(settings.selected_only == 0,
            "semantic demo should not hide unselected top regions")) {
        return 0;
    }
    if (!expect_true(settings.semantic_min_region_voxels >= 512,
            "semantic demo should use a bounded minimum region size")) {
        return 0;
    }
    if (!expect_true(settings.label_overlay_alpha >= 0.70f,
            "semantic demo should make region overlays visible")) {
        return 0;
    }

    return expect_true(settings.semantic_sensitivity > 0.50f,
        "semantic demo should use a discovery-friendly ASD sensitivity");
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

static int test_prompt_parser_clamps_negative_intensity_cutoffs(void)
{
    VoxelPilotPromptResult result;

    if (!expect_true(
            voxelpilot_parse_prompt_action(
                "hide everything below -0.2 intensity",
                &result) == 1,
            "prompt parser should recognize negative numeric cutoffs")) {
        return 0;
    }

    if (!expect_float_close(
            result.min_intensity,
            0.0f,
            "negative cutoff should clamp to the visible lower bound instead of becoming positive")) {
        return 0;
    }

    return expect_true(
        result.set_min_intensity == 1 &&
        result.tf_width > 0.95f,
        "negative hide-below prompt should keep the full visible intensity range");
}

static int test_prompt_reset_uses_visible_default_opacity(void)
{
    VoxelPilotPromptResult result;

    voxelpilot_init_prompt_result(&result);
    if (!expect_float_close(
            result.tf_opacity_scale,
            1.8f,
            "prompt defaults should match the app's visible transfer opacity")) {
        return 0;
    }

    if (!expect_true(
            voxelpilot_parse_prompt_action(
                "reset the view",
                &result) == 1,
            "prompt parser should recognize reset view commands")) {
        return 0;
    }

    return expect_float_close(
        result.tf_opacity_scale,
        1.8f,
        "reset view prompt should restore the visible transfer opacity");
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

static int test_dataset_health_warns_for_tiny_volume(void)
{
    float histogram[8] = { 0.0f };
    VoxelPilotDatasetHealthResult health;

    histogram[1] = 10.0f;
    histogram[2] = 20.0f;
    histogram[3] = 10.0f;

    if (!expect_true(
            voxelpilot_compute_dataset_health(
                histogram,
                8,
                33,
                41,
                25,
                0.0f,
                1.0f,
                (size_t)33 * 41 * 25,
                &health) == 1,
            "dataset health should accept a tiny populated volume")) {
        return 0;
    }

    if (!expect_true(
            health.level == VOXELPILOT_DATASET_HEALTH_WATCH &&
            (health.flags & VOXELPILOT_DATASET_HEALTH_TINY_VOLUME) != 0,
            "tiny app-ready datasets should produce a watch warning, not a failure")) {
        return 0;
    }

    return expect_true(
        strstr(health.recommendation, "semantic") != NULL,
        "tiny dataset health should explain semantic-review limitations");
}

static int test_dataset_health_flags_flat_histogram(void)
{
    float histogram[8] = { 0.0f };
    VoxelPilotDatasetHealthResult health;

    histogram[0] = 1024.0f;

    if (!expect_true(
            voxelpilot_compute_dataset_health(
                histogram,
                8,
                128,
                128,
                64,
                0.42f,
                0.42f,
                (size_t)128 * 128 * 64,
                &health) == 1,
            "dataset health should summarize flat histograms")) {
        return 0;
    }

    if (!expect_true(
            health.level == VOXELPILOT_DATASET_HEALTH_ATTENTION &&
            (health.flags & VOXELPILOT_DATASET_HEALTH_FLAT_RANGE) != 0,
            "flat histograms should require attention")) {
        return 0;
    }

    return expect_true(
        strstr(health.summary, "flat") != NULL || strstr(health.summary, "constant") != NULL,
        "flat dataset health should explain the constant-value issue");
}

static int test_dataset_health_flags_background_dominant_import(void)
{
    float histogram[16] = { 0.0f };
    VoxelPilotDatasetHealthResult health;

    histogram[0] = 9000.0f;
    histogram[3] = 450.0f;
    histogram[5] = 100.0f;
    histogram[9] = 50.0f;

    if (!expect_true(
            voxelpilot_compute_dataset_health(
                histogram,
                16,
                256,
                256,
                128,
                0.0f,
                1.0f,
                (size_t)256 * 256 * 128,
                &health) == 1,
            "dataset health should summarize background-dominant imports")) {
        return 0;
    }

    if (!expect_true(
            health.level == VOXELPILOT_DATASET_HEALTH_ATTENTION &&
            (health.flags & VOXELPILOT_DATASET_HEALTH_BACKGROUND_DOMINANT) != 0,
            "background-dominant imports should request attention")) {
        return 0;
    }

    return expect_true(
        strstr(health.recommendation, "Transfer Advisor") != NULL,
        "background-dominant health should point users to the transfer advisor");
}

static int test_dataset_health_warns_for_large_working_set(void)
{
    float histogram[16] = { 0.0f };
    VoxelPilotDatasetHealthResult health;
    int bin_index;

    for (bin_index = 2; bin_index < 14; ++bin_index) {
        histogram[bin_index] = 100.0f;
    }

    if (!expect_true(
            voxelpilot_compute_dataset_health(
                histogram,
                16,
                768,
                768,
                512,
                0.0f,
                1.0f,
                (size_t)768 * 768 * 512 * sizeof(float),
                &health) == 1,
            "dataset health should summarize large working sets")) {
        return 0;
    }

    return expect_true(
        health.level == VOXELPILOT_DATASET_HEALTH_WATCH &&
        (health.flags & VOXELPILOT_DATASET_HEALTH_LARGE_VOLUME) != 0 &&
        strstr(health.recommendation, "Performance Safety") != NULL,
        "large working sets should warn through Performance Safety guidance");
}

static int test_dataset_health_preserves_healthy_foot_sized_volume(void)
{
    float histogram[32] = { 0.0f };
    VoxelPilotDatasetHealthResult health;
    int bin_index;

    for (bin_index = 4; bin_index <= 24; ++bin_index) {
        histogram[bin_index] = 800.0f + (float)(bin_index % 5) * 40.0f;
    }

    if (!expect_true(
            voxelpilot_compute_dataset_health(
                histogram,
                32,
                256,
                256,
                256,
                0.0f,
                1.0f,
                (size_t)256 * 256 * 256,
                &health) == 1,
            "dataset health should accept healthy foot-sized volumes")) {
        return 0;
    }

    if (!expect_true(
            health.level == VOXELPILOT_DATASET_HEALTH_OK &&
            health.flags == 0,
            "healthy 256^3 volumes should not be degraded by the health check")) {
        return 0;
    }

    return expect_true(
        health.score >= 85.0f && strstr(health.summary, "healthy") != NULL,
        "healthy dataset health should report a high confidence score");
}

static int test_transfer_advisor_recommends_dark_sparse_enhancement(void)
{
    float histogram[16] = { 0.0f };
    VoxelPilotTransferAdvisorResult advisor;

    histogram[0] = 1200.0f;
    histogram[1] = 280.0f;
    histogram[2] = 90.0f;
    histogram[7] = 35.0f;
    histogram[10] = 8.0f;

    if (!expect_true(
            voxelpilot_compute_transfer_advisor(
                histogram,
                16,
                0.45f,
                0.35f,
                1.8f,
                0,
                &advisor) == 1,
            "transfer advisor should accept sparse dark histograms")) {
        return 0;
    }

    if (!expect_true(
            advisor.has_recommendation == 1 &&
            advisor.kind == VOXELPILOT_TRANSFER_ADVICE_BRIGHTEN_SPARSE,
            "sparse dark histogram should produce a brighten recommendation")) {
        return 0;
    }

    if (!expect_true(
            advisor.tf_opacity_scale > 1.8f &&
            advisor.tf_width >= 0.20f,
            "sparse dark advisor should boost opacity while preserving a usable window")) {
        return 0;
    }

    return expect_true(
        strstr(advisor.explanation, "background") != NULL,
        "sparse dark advisor should explain the background-dominant reason");
}

static int test_transfer_advisor_recommends_high_density_shell(void)
{
    float histogram[16] = { 0.0f };
    VoxelPilotTransferAdvisorResult advisor;
    int bin_index;

    for (bin_index = 10; bin_index < 16; ++bin_index) {
        histogram[bin_index] = 120.0f + (float)(bin_index - 10) * 20.0f;
    }
    histogram[0] = 20.0f;
    histogram[4] = 12.0f;

    if (!expect_true(
            voxelpilot_compute_transfer_advisor(
                histogram,
                16,
                0.45f,
                0.35f,
                1.8f,
                4,
                &advisor) == 1,
            "transfer advisor should accept high-density histograms")) {
        return 0;
    }

    if (!expect_true(
            advisor.has_recommendation == 1 &&
            advisor.kind == VOXELPILOT_TRANSFER_ADVICE_HIGH_DENSITY_FOCUS,
            "high-density histogram should produce a shell/object focus recommendation")) {
        return 0;
    }

    if (!expect_true(
            advisor.tf_center > 0.65f &&
            advisor.tf_width <= 0.36f,
            "high-density advisor should focus the upper intensity band")) {
        return 0;
    }

    return expect_true(
        strstr(advisor.action, "high-density") != NULL &&
        advisor.confidence > 0.55f,
        "high-density advisor should provide a cautious explainable action");
}

static int test_transfer_advisor_keeps_balanced_volume_stable(void)
{
    float histogram[10] = { 0.0f };
    VoxelPilotTransferAdvisorResult advisor;
    int bin_index;

    for (bin_index = 1; bin_index < 9; ++bin_index) {
        histogram[bin_index] = 100.0f;
    }

    if (!expect_true(
            voxelpilot_compute_transfer_advisor(
                histogram,
                10,
                0.45f,
                0.35f,
                1.8f,
                2,
                &advisor) == 1,
            "transfer advisor should summarize balanced histograms")) {
        return 0;
    }

    if (!expect_true(
            advisor.kind == VOXELPILOT_TRANSFER_ADVICE_KEEP_CURRENT &&
            advisor.has_recommendation == 0,
            "balanced histogram should not force unnecessary transfer changes")) {
        return 0;
    }

    return expect_true(
        advisor.tf_center == 0.45f &&
        advisor.tf_width == 0.35f &&
        advisor.tf_opacity_scale == 1.8f,
        "balanced advisor should preserve current transfer settings");
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

static int test_auto_enhance_load_frames_headsq_style_histograms(void)
{
    float histogram[128] = { 0.0f };
    float center = 0.0f;
    float width = 0.0f;
    float opacity = 0.0f;
    int bin_index;

    histogram[0] = 73000.0f;
    histogram[1] = 141000.0f;
    for (bin_index = 2; bin_index <= 25; ++bin_index) {
        histogram[bin_index] = 6200.0f;
    }
    for (bin_index = 26; bin_index <= 121; ++bin_index) {
        histogram[bin_index] = 280.0f;
    }
    histogram[127] = 1800.0f;

    if (!expect_true(
            voxelpilot_should_auto_enhance_on_load(histogram, 128) == 1,
            "headsq-style low-background CT should auto-enhance on import")) {
        return 0;
    }

    if (!expect_true(
            voxelpilot_compute_auto_enhance_transfer(
                histogram,
                128,
                &center,
                &width,
                &opacity) == 1,
            "headsq-style histogram should produce an auto-enhance transfer")) {
        return 0;
    }

    if (!expect_true(
            center >= 0.08f && center <= 0.18f &&
            width >= 0.19f && width <= 0.26f,
            "headsq-style import should use a tight high-visibility CT window")) {
        return 0;
    }

    return expect_true(
        opacity >= 1.85f,
        "headsq-style import should boost opacity on load");
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

static int test_selected_label_emphasis_boosts_only_selected_label(void)
{
    float selected_r = 0.35f;
    float selected_g = 0.45f;
    float selected_b = 0.55f;
    float other_r = 0.35f;
    float other_g = 0.45f;
    float other_b = 0.55f;

    if (!expect_true(
            voxelpilot_apply_selected_label_emphasis(
                2, 2, 0.60f,
                &selected_r, &selected_g, &selected_b) == 1,
            "selected label emphasis should report a selected-label boost")) {
        return 0;
    }

    if (!expect_true(selected_r > 0.35f && selected_g > 0.45f && selected_b > 0.55f,
            "selected label emphasis should brighten selected color channels")) {
        return 0;
    }

    if (!expect_true(
            voxelpilot_apply_selected_label_emphasis(
                1, 2, 0.60f,
                &other_r, &other_g, &other_b) == 0,
            "selected label emphasis should skip non-selected labels")) {
        return 0;
    }

    if (!expect_float_close(other_r, 0.35f, "non-selected emphasis should preserve red channel")) return 0;
    if (!expect_float_close(other_g, 0.45f, "non-selected emphasis should preserve green channel")) return 0;
    return expect_float_close(other_b, 0.55f, "non-selected emphasis should preserve blue channel");
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

static int test_histogram_profile_handles_flat_and_ranged_volumes(void)
{
    float flat_volume[5] = { 0.25f, 0.25f, 0.25f, 0.25f, 0.25f };
    float range_volume[4] = { 0.0f, 0.5f, 1.0f, 1.0f };
    float histogram[4];
    VoxelPilotHistogramProfile profile;

    if (!expect_true(
            voxelpilot_compute_histogram_profile(
                flat_volume,
                1,
                1,
                5,
                histogram,
                4,
                &profile) == 1,
            "histogram profile should accept flat volumes")) {
        return 0;
    }

    if (!expect_true(profile.ready == 1, "flat histogram profile should be ready")) return 0;
    if (!expect_float_close(profile.min_value, 0.25f, "flat profile should keep min value")) return 0;
    if (!expect_float_close(profile.max_value, 0.25f, "flat profile should keep max value")) return 0;
    if (!expect_float_close(profile.peak_count, 5.0f, "flat profile should put all samples in one bin")) return 0;
    if (!expect_float_close(histogram[0], 5.0f, "flat histogram should use the first bin")) return 0;

    if (!expect_true(
            voxelpilot_compute_histogram_profile(
                range_volume,
                2,
                1,
                2,
                histogram,
                4,
                &profile) == 1,
            "histogram profile should accept ranged volumes")) {
        return 0;
    }

    if (!expect_float_close(profile.min_value, 0.0f, "range profile should keep min value")) return 0;
    if (!expect_float_close(profile.max_value, 1.0f, "range profile should keep max value")) return 0;
    if (!expect_float_close(profile.peak_count, 2.0f, "range profile should track the busiest bin")) return 0;
    if (!expect_float_close(histogram[0], 1.0f, "range histogram should count low samples")) return 0;
    if (!expect_float_close(histogram[1], 1.0f, "range histogram should count mid samples")) return 0;
    return expect_float_close(histogram[3], 2.0f, "range histogram should count high samples");
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

static int test_render_command_builder_maps_view_state(void)
{
    VoxelPilotRenderCommandConfig config;
    RendererInput cmd;

    voxelpilot_render_command_config_defaults(&config);
    config.cam_pos[0] = 0.25f;
    config.cam_pos[1] = 0.50f;
    config.cam_pos[2] = -1.25f;
    config.cam_dir[0] = 0.10f;
    config.cam_dir[1] = 0.20f;
    config.cam_dir[2] = 0.97f;
    config.cam_up[0] = 0.0f;
    config.cam_up[1] = 1.0f;
    config.cam_up[2] = 0.0f;
    config.fov_y_degrees = 60.0f;
    config.light_pos[0] = 1.5f;
    config.light_pos[1] = 1.0f;
    config.light_pos[2] = -0.5f;
    config.step_size = 0.0027f;
    config.threshold = 0.96f;
    config.empty_space_skip_mult = 1.75f;
    config.tf_center = 0.55f;
    config.tf_width = 0.25f;
    config.tf_opacity_scale = 2.25f;
    config.tf_palette = 2;
    config.tf_invert = 1;
    config.cinematic_enabled = 1;
    config.cinematic_rim_strength = 0.52f;
    config.cinematic_glow_strength = 0.40f;
    config.cinematic_contrast = 1.18f;
    config.clip_min[0] = 0.10f;
    config.clip_min[1] = 0.20f;
    config.clip_min[2] = 0.30f;
    config.clip_max[0] = 0.90f;
    config.clip_max[1] = 0.80f;
    config.clip_max[2] = 0.70f;
    config.render_width = 1280;
    config.render_height = 720;

    if (!expect_true(
            voxelpilot_build_renderer_input(&config, &cmd) == 1,
            "render command builder should accept valid config")) {
        return 0;
    }
    if (!expect_float_close(cmd.cam_pos[2], -1.25f,
            "render command builder should copy camera position")) return 0;
    if (!expect_float_close(cmd.fov_y, 1.0471976f,
            "render command builder should convert fov degrees to radians")) return 0;
    if (!expect_float_close(cmd.step_size, 0.0027f,
            "render command builder should use effective adaptive step size")) return 0;
    if (!expect_float_close(cmd.empty_space_skip_mult, 1.75f,
            "render command builder should use effective adaptive skip multiplier")) return 0;
    if (!expect_float_close(cmd.tf_opacity_scale, 2.25f,
            "render command builder should copy transfer opacity")) return 0;
    if (!expect_true(cmd.tf_palette == 2 && cmd.tf_invert == 1,
            "render command builder should copy transfer palette flags")) return 0;
    if (!expect_float_close(cmd.cinematic_contrast, 1.18f,
            "render command builder should copy cinematic settings")) return 0;
    if (!expect_float_close(cmd.clip_min[1], 0.20f,
            "render command builder should copy clip minimums")) return 0;
    if (!expect_true(cmd.img_width == 1280u && cmd.img_height == 720u,
            "render command builder should copy render target size")) return 0;
    return expect_true(cmd.label_mask == NULL && cmd.labels.enabled == 0,
        "render command builder should leave label overlays for the caller seam");
}

static int test_render_command_builder_attaches_label_overlay(void)
{
    VoxelPilotRenderCommandConfig config;
    VoxelPilotRenderLabelOverlayConfig labels;
    RendererInput cmd;
    unsigned char mask[8] = { 0 };

    voxelpilot_render_command_config_defaults(&config);
    if (!expect_true(voxelpilot_build_renderer_input(&config, &cmd) == 1,
            "render command should build before labels attach")) return 0;

    voxelpilot_render_label_overlay_config_defaults(&labels);
    labels.visible = 1;
    labels.label_mask = mask;
    labels.label_mask_width = 2;
    labels.label_mask_height = 2;
    labels.label_mask_depth = 2;
    labels.renderer_width = 2;
    labels.renderer_height = 2;
    labels.renderer_depth = 2;
    labels.label_count = 2;
    labels.alpha = 0.55f;
    labels.revision = 7;
    labels.selected_enabled = 1;
    labels.selected_label_id = 2;
    labels.selected_emphasis = 0.66f;
    labels.colors[0] = 0.10f;
    labels.colors[1] = 0.20f;
    labels.colors[2] = 0.30f;
    labels.colors[3] = 0.40f;
    labels.colors[4] = 0.50f;
    labels.colors[5] = 0.60f;

    if (!expect_true(
            voxelpilot_attach_label_overlay_to_renderer_input(&labels, &cmd) == 1,
            "render command builder should attach valid label overlay")) {
        return 0;
    }
    if (!expect_true(cmd.label_mask == mask && cmd.labels.enabled == 1,
            "label overlay attach should enable renderer labels with mask")) return 0;
    if (!expect_true(cmd.labels.width == 2 && cmd.labels.height == 2 && cmd.labels.depth == 2,
            "label overlay attach should copy label dimensions")) return 0;
    if (!expect_true(cmd.labels.label_count == 2 && cmd.labels.revision == 7,
            "label overlay attach should copy label count and revision")) return 0;
    if (!expect_float_close(cmd.labels.alpha, 0.55f,
            "label overlay attach should copy alpha")) return 0;
    if (!expect_true(cmd.labels.selected_label_id == 2,
            "label overlay attach should copy selected label id")) return 0;
    if (!expect_float_close(cmd.labels.selected_emphasis, 0.66f,
            "label overlay attach should copy selected emphasis")) return 0;
    return expect_float_close(cmd.labels.colors[5], 0.60f,
        "label overlay attach should copy label colors");
}

static int test_render_command_builder_rejects_mismatched_label_overlay(void)
{
    VoxelPilotRenderCommandConfig config;
    VoxelPilotRenderLabelOverlayConfig labels;
    RendererInput cmd;
    unsigned char mask[8] = { 0 };

    voxelpilot_render_command_config_defaults(&config);
    if (!expect_true(voxelpilot_build_renderer_input(&config, &cmd) == 1,
            "render command should build before rejected label attach")) return 0;

    voxelpilot_render_label_overlay_config_defaults(&labels);
    labels.visible = 1;
    labels.label_mask = mask;
    labels.label_mask_width = 2;
    labels.label_mask_height = 2;
    labels.label_mask_depth = 2;
    labels.renderer_width = 2;
    labels.renderer_height = 3;
    labels.renderer_depth = 2;
    labels.label_count = 1;

    if (!expect_true(
            voxelpilot_attach_label_overlay_to_renderer_input(&labels, &cmd) == 0,
            "render command builder should reject mismatched label dimensions")) {
        return 0;
    }
    return expect_true(cmd.label_mask == NULL && cmd.labels.enabled == 0,
        "rejected label overlay should leave renderer labels disabled");
}
static int test_render_command_builder_rejects_invalid_target_size(void)
{
    VoxelPilotRenderCommandConfig config;
    RendererInput cmd;

    voxelpilot_render_command_config_defaults(&config);
    config.render_width = 0;
    config.render_height = 720;

    return expect_true(
        voxelpilot_build_renderer_input(&config, &cmd) == 0,
        "render command builder should reject invalid render target dimensions");
}
static int test_performance_governor_small_volume_is_healthy(void)
{
    VoxelPilotPerformanceInputs inputs;
    VoxelPilotPerformanceResult result;
    voxelpilot_performance_inputs_defaults(&inputs);
    inputs.volume_width = 256;
    inputs.volume_height = 256;
    inputs.volume_depth = 256;
    inputs.render_width = 1024;
    inputs.render_height = 1024;
    inputs.estimated_volume_bytes = 256u * 256u * 256u * sizeof(float);
    inputs.estimated_gradient_bytes = inputs.estimated_volume_bytes * 3u;
    inputs.estimated_label_mask_bytes = 256u * 256u * 256u;
    inputs.gpu_memory_available = 4ull * 1024ull * 1024ull * 1024ull;
    inputs.gpu_memory_total = 6ull * 1024ull * 1024ull * 1024ull;
    inputs.has_gpu_memory_telemetry = 1;
    inputs.render_ms_average = 35.0f;
    inputs.render_quality_mode = VOXELPILOT_RENDER_QUALITY_BALANCED;
    if (!expect_true(voxelpilot_evaluate_performance(&inputs, &result) == 1,
            "performance governor should evaluate healthy inputs")) return 0;
    if (!expect_true(result.status == VOXELPILOT_GOVERNOR_HEALTHY,
            "small laptop-friendly volume should be healthy")) return 0;
    if (!expect_true(result.safe_to_run_asd == 1,
            "healthy pressure should allow ASD")) return 0;
    return expect_true(result.suggest_interactive_gpu == 0,
        "healthy pressure should not suggest interactive GPU");
}

static int test_performance_governor_high_render_time_suggests_interactive(void)
{
    VoxelPilotPerformanceInputs inputs;
    VoxelPilotPerformanceResult result;
    voxelpilot_performance_inputs_defaults(&inputs);
    inputs.volume_width = 256;
    inputs.volume_height = 256;
    inputs.volume_depth = 256;
    inputs.render_width = 1920;
    inputs.render_height = 1080;
    inputs.estimated_volume_bytes = 256u * 256u * 256u * sizeof(float);
    inputs.estimated_gradient_bytes = inputs.estimated_volume_bytes * 3u;
    inputs.gpu_memory_available = 4ull * 1024ull * 1024ull * 1024ull;
    inputs.gpu_memory_total = 6ull * 1024ull * 1024ull * 1024ull;
    inputs.has_gpu_memory_telemetry = 1;
    inputs.render_ms_average = 155.0f;
    inputs.render_quality_mode = VOXELPILOT_RENDER_QUALITY_QUALITY;
    inputs.cinematic_enabled = 1;
    inputs.label_overlay_enabled = 1;
    if (!expect_true(voxelpilot_evaluate_performance(&inputs, &result) == 1,
            "performance governor should evaluate high render time inputs")) return 0;
    if (!expect_true(result.status >= VOXELPILOT_GOVERNOR_WATCH,
            "high render time should raise at least watch status")) return 0;
    if (!expect_true(result.suggest_interactive_gpu == 1,
            "high render time should suggest interactive GPU")) return 0;
    return expect_true(strstr(result.recommendation, "Interactive GPU") != NULL,
        "recommendation should name the interactive GPU action");
}

static int test_performance_governor_memory_pressure_suggests_streaming(void)
{
    VoxelPilotPerformanceInputs inputs;
    VoxelPilotPerformanceResult result;
    voxelpilot_performance_inputs_defaults(&inputs);
    inputs.volume_width = 1024;
    inputs.volume_height = 1024;
    inputs.volume_depth = 768;
    inputs.render_width = 1920;
    inputs.render_height = 1080;
    inputs.estimated_volume_bytes = 1024ull * 1024ull * 768ull * sizeof(float);
    inputs.estimated_gradient_bytes = inputs.estimated_volume_bytes * 3ull;
    inputs.estimated_label_mask_bytes = 1024ull * 1024ull * 768ull;
    inputs.gpu_memory_available = 4ull * 1024ull * 1024ull * 1024ull;
    inputs.gpu_memory_total = 6ull * 1024ull * 1024ull * 1024ull;
    inputs.has_gpu_memory_telemetry = 1;
    inputs.render_ms_average = 95.0f;
    inputs.render_quality_mode = VOXELPILOT_RENDER_QUALITY_BALANCED;
    if (!expect_true(voxelpilot_evaluate_performance(&inputs, &result) == 1,
            "performance governor should evaluate high memory inputs")) return 0;
    if (!expect_true(result.status >= VOXELPILOT_GOVERNOR_PRESSURE,
            "large working set should raise pressure status")) return 0;
    if (!expect_true(result.suggest_brick_streaming == 1,
            "large working set should suggest brick streaming path")) return 0;
    return expect_true(result.safe_to_export_mask == 0,
        "critical memory pressure should not recommend full mask export");
}

static int test_performance_governor_callout_pressure_suggests_limit(void)
{
    VoxelPilotPerformanceInputs inputs;
    VoxelPilotPerformanceResult result;
    voxelpilot_performance_inputs_defaults(&inputs);
    inputs.volume_width = 512;
    inputs.volume_height = 512;
    inputs.volume_depth = 512;
    inputs.render_width = 1280;
    inputs.render_height = 720;
    inputs.estimated_volume_bytes = 512ull * 512ull * 512ull * sizeof(float);
    inputs.estimated_gradient_bytes = inputs.estimated_volume_bytes * 3ull;
    inputs.estimated_label_mask_bytes = 512ull * 512ull * 512ull;
    inputs.render_ms_average = 118.0f;
    inputs.render_quality_mode = VOXELPILOT_RENDER_QUALITY_BALANCED;
    inputs.label_overlay_enabled = 1;
    inputs.label_mask_present = 1;
    inputs.semantic_callouts_enabled = 1;
    inputs.semantic_callout_top_regions = 6;
    inputs.semantic_region_count = 6;
    if (!expect_true(voxelpilot_evaluate_performance(&inputs, &result) == 1,
            "performance governor should evaluate callout-heavy inputs")) return 0;
    if (!expect_true(result.suggest_reduce_callouts == 1,
            "callout-heavy pressure should suggest limiting callouts")) return 0;
    return expect_true(strstr(result.recommendation, "callouts") != NULL,
        "recommendation should explain callout reduction");
}

static int test_performance_governor_moving_average_is_stable(void)
{
    float average = voxelpilot_update_render_ms_average(0.0f, 100.0f, 0);
    if (!expect_float_close(average, 100.0f,
            "first render sample should initialize moving average")) return 0;
    average = voxelpilot_update_render_ms_average(average, 200.0f, 1);
    return expect_float_close(average, 115.0f,
        "subsequent render sample should use stable 15 percent smoothing");
}


static int test_adaptive_quality_degrades_during_slow_interaction(void)
{
    VoxelPilotAdaptiveQualityInputs inputs;
    VoxelPilotAdaptiveQualityResult result;

    voxelpilot_adaptive_quality_inputs_defaults(&inputs);
    inputs.enabled = 1;
    inputs.base_step_size = 0.0020f;
    inputs.base_skip_mult = 1.50f;
    inputs.current_multiplier = 1.0f;
    inputs.render_ms_average = 72.0f;
    inputs.interaction_active = 1;
    inputs.cinematic_enabled = 1;

    if (!expect_true(
            voxelpilot_update_adaptive_quality(&inputs, &result) == 1,
            "adaptive quality should evaluate valid inputs")) {
        return 0;
    }
    if (!expect_true(result.active == 1,
            "slow interaction should activate adaptive quality")) return 0;
    if (!expect_float_close(result.multiplier, 1.35f,
            "slow interaction should raise the quality multiplier quickly")) return 0;
    if (!expect_float_close(result.step_size, 0.0027f,
            "adaptive step should be the base step multiplied by the quality multiplier")) return 0;
    return expect_true(strstr(result.status, "Adaptive quality") != NULL,
        "adaptive quality status should describe the automatic adjustment");
}

static int test_adaptive_quality_recovers_slowly_when_frame_time_is_low(void)
{
    VoxelPilotAdaptiveQualityInputs inputs;
    VoxelPilotAdaptiveQualityResult result;

    voxelpilot_adaptive_quality_inputs_defaults(&inputs);
    inputs.enabled = 1;
    inputs.base_step_size = 0.0020f;
    inputs.base_skip_mult = 1.50f;
    inputs.current_multiplier = 2.0f;
    inputs.render_ms_average = 24.0f;
    inputs.interaction_active = 0;

    if (!expect_true(
            voxelpilot_update_adaptive_quality(&inputs, &result) == 1,
            "adaptive quality should evaluate recovery inputs")) {
        return 0;
    }
    if (!expect_float_close(result.multiplier, 1.88f,
            "low frame time should recover quality gradually")) return 0;
    if (!expect_float_close(result.step_size, 0.00376f,
            "recovered adaptive step should still derive from the base step")) return 0;
    return expect_true(result.active == 1,
        "quality should remain active until multiplier returns to one");
}

static int test_adaptive_quality_disabled_uses_base_quality(void)
{
    VoxelPilotAdaptiveQualityInputs inputs;
    VoxelPilotAdaptiveQualityResult result;

    voxelpilot_adaptive_quality_inputs_defaults(&inputs);
    inputs.enabled = 0;
    inputs.base_step_size = 0.00125f;
    inputs.base_skip_mult = 1.0f;
    inputs.current_multiplier = 2.5f;
    inputs.render_ms_average = 155.0f;
    inputs.interaction_active = 1;

    if (!expect_true(
            voxelpilot_update_adaptive_quality(&inputs, &result) == 1,
            "adaptive quality should evaluate disabled inputs")) {
        return 0;
    }
    if (!expect_float_close(result.multiplier, 1.0f,
            "disabled adaptive quality should reset the multiplier")) return 0;
    if (!expect_float_close(result.step_size, 0.00125f,
            "disabled adaptive quality should preserve base step size")) return 0;
    return expect_true(result.active == 0,
        "disabled adaptive quality should not report an active adjustment");
}
static int test_asd_rejects_invalid_inputs(void)
{
    unsigned char label_mask[8] = { 0 };
    VoxelPilotSemanticRegion regions[VOXELPILOT_ASD_MAX_REGIONS];
    int region_count = 123;

    if (!expect_true(
            voxelpilot_discover_semantic_regions(
                NULL,
                2,
                2,
                2,
                NULL,
                0,
                4,
                0.5f,
                1,
                label_mask,
                regions,
                &region_count) == 0,
            "ASD should reject null volume data")) {
        return 0;
    }

    if (!expect_true(region_count == 0,
            "ASD should clear output region count on invalid input")) {
        return 0;
    }

    return expect_true(
        voxelpilot_discover_semantic_regions(
            (const float *)label_mask,
            0,
            2,
            2,
            NULL,
            0,
            4,
            0.5f,
            1,
            label_mask,
            regions,
            &region_count) == 0,
        "ASD should reject invalid dimensions");
}

static int test_asd_discovers_high_density_connected_region(void)
{
    float volume[27] = { 0.0f };
    unsigned char label_mask[27] = { 0 };
    VoxelPilotSemanticRegion regions[VOXELPILOT_ASD_MAX_REGIONS];
    int region_count = 0;
    int index;

    volume[13] = 0.92f;
    volume[14] = 0.90f;
    volume[16] = 0.88f;
    volume[22] = 0.91f;

    if (!expect_true(
            voxelpilot_discover_semantic_regions(
                volume,
                3,
                3,
                3,
                NULL,
                0,
                4,
                0.55f,
                2,
                label_mask,
                regions,
                &region_count) == 1,
            "ASD should discover at least one connected region")) {
        return 0;
    }

    if (!expect_true(region_count == 1,
            "ASD should report one high-density connected component")) {
        return 0;
    }

    if (!expect_true(regions[0].label_id == 1,
            "ASD first region should use label id one")) {
        return 0;
    }

    if (!expect_true(regions[0].voxel_count == 4,
            "ASD should count connected region voxels")) {
        return 0;
    }

    for (index = 0; index < 27; ++index) {
        if (volume[index] > 0.80f && label_mask[index] != 1) {
            fprintf(stderr, "FAIL: high-density voxel %d was not labeled\n", index);
            return 0;
        }
    }

    return expect_true(
        regions[0].mean_intensity > 0.88f && regions[0].mean_intensity < 0.93f,
        "ASD should compute region mean intensity");
}


static int test_asd_assigns_archetype_stability_and_importance(void)
{
    float volume[125] = { 0.0f };
    unsigned char label_mask[125] = { 0 };
    VoxelPilotSemanticRegion regions[VOXELPILOT_ASD_MAX_REGIONS];
    int region_count = 0;
    int x;

    for (x = 1; x <= 4; ++x) {
        volume[2 * 25 + 2 * 5 + x] = 0.95f;
    }

    if (!expect_true(
            voxelpilot_discover_semantic_regions(
                volume,
                5,
                5,
                5,
                NULL,
                0,
                4,
                0.50f,
                2,
                label_mask,
                regions,
                &region_count) == 1,
            "ASD should discover rod test region")) {
        return 0;
    }

    if (!expect_true(region_count == 1,
            "ASD rod test should produce one region")) {
        return 0;
    }

    if (!expect_true(strcmp(regions[0].archetype, "rod") == 0,
            "ASD should classify elongated region as rod archetype")) {
        return 0;
    }

    if (!expect_true(strstr(regions[0].name, "rod") != NULL,
            "ASD cautious name should include rod archetype")) {
        return 0;
    }

    if (!expect_true(regions[0].stability_score >= 0.0f && regions[0].stability_score <= 1.0f,
            "ASD stability score should be normalized")) {
        return 0;
    }

    return expect_true(regions[0].importance_score > 0.0f,
        "ASD importance score should be positive for a discovered region");
}


static int test_asd_ranks_regions_and_reports_relationships(void)
{
    VoxelPilotSemanticRegion regions[2];
    VoxelPilotRegionRelation relations[VOXELPILOT_ASD_MAX_RELATIONS];
    int relation_count = 0;

    memset(regions, 0, sizeof(regions));
    regions[0].label_id = 1;
    regions[0].importance_score = 0.25f;
    regions[0].bbox_min[0] = 0; regions[0].bbox_min[1] = 0; regions[0].bbox_min[2] = 0;
    regions[0].bbox_max[0] = 2; regions[0].bbox_max[1] = 2; regions[0].bbox_max[2] = 2;
    regions[0].centroid[0] = 1.0f; regions[0].centroid[1] = 1.0f; regions[0].centroid[2] = 1.0f;

    regions[1].label_id = 2;
    regions[1].importance_score = 0.90f;
    regions[1].bbox_min[0] = 3; regions[1].bbox_min[1] = 0; regions[1].bbox_min[2] = 0;
    regions[1].bbox_max[0] = 4; regions[1].bbox_max[1] = 2; regions[1].bbox_max[2] = 2;
    regions[1].centroid[0] = 3.5f; regions[1].centroid[1] = 1.0f; regions[1].centroid[2] = 1.0f;

    voxelpilot_asd_rank_regions(regions, 2);

    if (!expect_true(regions[1].importance_rank == 1,
            "ASD should rank highest-importance region first")) {
        return 0;
    }

    if (!expect_true(
            voxelpilot_compute_region_relationships(
                regions,
                2,
                relations,
                VOXELPILOT_ASD_MAX_RELATIONS,
                &relation_count) == 1,
            "ASD relationship helper should accept two regions")) {
        return 0;
    }

    if (!expect_true(relation_count >= 1,
            "ASD should report at least one relationship")) {
        return 0;
    }

    return expect_true(
        relations[0].type == VOXELPILOT_REGION_RELATION_ADJACENT,
        "ASD should identify adjacent bounding boxes");
}


static void seed_guided_review_region(
    VoxelPilotSemanticRegion *region,
    unsigned char label_id,
    int importance_rank,
    int rejected,
    const char *name)
{
    memset(region, 0, sizeof(*region));
    region->label_id = label_id;
    region->importance_rank = importance_rank;
    region->rejected = rejected;
    region->confidence = 0.70f;
    region->stability_score = 0.80f;
    region->importance_score = 0.60f;
    region->centroid[0] = 8.0f * (float)label_id;
    region->centroid[1] = 4.0f * (float)label_id;
    region->centroid[2] = 2.0f * (float)label_id;
    snprintf(region->name, sizeof(region->name), "%s", name);
    snprintf(region->archetype, sizeof(region->archetype), "blob");
    snprintf(region->description, sizeof(region->description), "Synthetic guided review region.");
}

static int test_guided_review_selects_top_ranked_region(void)
{
    VoxelPilotSemanticRegion regions[3];
    VoxelPilotGuidedReviewStep step;

    seed_guided_review_region(&regions[0], 1, 3, 0, "third ranked");
    seed_guided_review_region(&regions[1], 2, 1, 0, "first ranked");
    seed_guided_review_region(&regions[2], 3, 2, 0, "second ranked");

    if (!expect_true(
            voxelpilot_guided_review_select_region(
                regions,
                3,
                -1,
                1,
                &step) == 1,
            "guided review should select a first region when none is active")) {
        return 0;
    }

    if (!expect_true(
            step.region_index == 1 &&
            step.label_id == 2 &&
            step.step_number == 1 &&
            step.step_count == 3,
            "guided review should start with the top importance-ranked region")) {
        return 0;
    }

    return expect_true(
        strstr(step.title, "first ranked") != NULL,
        "guided review step should include the selected region name");
}

static int test_guided_review_skips_rejected_regions(void)
{
    VoxelPilotSemanticRegion regions[3];
    VoxelPilotGuidedReviewStep step;

    seed_guided_review_region(&regions[0], 1, 1, 1, "rejected top");
    seed_guided_review_region(&regions[1], 2, 2, 0, "reviewable second");
    seed_guided_review_region(&regions[2], 3, 3, 0, "reviewable third");

    if (!expect_true(
            voxelpilot_guided_review_select_region(
                regions,
                3,
                -1,
                1,
                &step) == 1,
            "guided review should find a reviewable region after rejected entries")) {
        return 0;
    }

    return expect_true(
        step.region_index == 1 &&
        step.label_id == 2 &&
        step.step_count == 2,
        "guided review should skip rejected regions and count only reviewable regions");
}

static int test_guided_review_wraps_through_reviewable_regions(void)
{
    VoxelPilotSemanticRegion regions[3];
    VoxelPilotGuidedReviewStep step;

    seed_guided_review_region(&regions[0], 1, 1, 0, "first ranked");
    seed_guided_review_region(&regions[1], 2, 2, 0, "second ranked");
    seed_guided_review_region(&regions[2], 3, 3, 0, "third ranked");

    if (!expect_true(
            voxelpilot_guided_review_select_region(
                regions,
                3,
                2,
                1,
                &step) == 1,
            "guided review should wrap forward after the final region")) {
        return 0;
    }

    if (!expect_true(
            step.region_index == 0 &&
            step.step_number == 1,
            "guided review should wrap forward to the top-ranked region")) {
        return 0;
    }

    if (!expect_true(
            voxelpilot_guided_review_select_region(
                regions,
                3,
                0,
                -1,
                &step) == 1,
            "guided review should wrap backward before the first region")) {
        return 0;
    }

    return expect_true(
        step.region_index == 2 &&
        step.step_number == 3,
        "guided review should wrap backward to the last-ranked region");
}

static int test_asd_correction_memory_confirms_matching_region(void)
{
    VoxelPilotSemanticRegion region;
    VoxelPilotSemanticRegion candidate;
    VoxelPilotSemanticCorrection correction;
    int applied = 0;

    memset(&region, 0, sizeof(region));
    region.label_id = 1;
    snprintf(region.name, sizeof(region.name), "dense rod-like structure");
    snprintf(region.archetype, sizeof(region.archetype), "rod");
    region.density_class = VOXELPILOT_ASD_DENSITY_HIGH;
    region.mean_intensity = 0.90f;
    region.min_intensity = 0.85f;
    region.max_intensity = 0.96f;
    region.compactness = 0.80f;
    region.elongation = 3.50f;
    region.voxel_count = 12;

    memset(&correction, 0, sizeof(correction));
    voxelpilot_asd_capture_correction(&region, 1000, "Confirmed tibia-like demo region", 1, 0, &correction);

    candidate = region;
    candidate.label_id = 2;
    candidate.user_confirmed = 0;
    candidate.source = VOXELPILOT_SEMANTIC_SOURCE_HEURISTIC;
    snprintf(candidate.name, sizeof(candidate.name), "dense rod-like structure");

    applied = voxelpilot_asd_apply_corrections(&candidate, 1, 1000, &correction, 1);

    if (!expect_true(applied == 1, "ASD correction should apply to matching region")) return 0;
    if (!expect_true(candidate.user_confirmed == 1, "ASD correction should mark region confirmed")) return 0;
    if (!expect_true(candidate.source == VOXELPILOT_SEMANTIC_SOURCE_USER_CONFIRMED, "ASD correction should set user-confirmed provenance")) return 0;
    return expect_true(strcmp(candidate.name, "Confirmed tibia-like demo region") == 0, "ASD correction should rename matching region");
}

static int test_asd_correction_memory_rejects_matching_region(void)
{
    VoxelPilotSemanticRegion region;
    VoxelPilotSemanticCorrection correction;
    int applied = 0;

    memset(&region, 0, sizeof(region));
    region.label_id = 3;
    snprintf(region.name, sizeof(region.name), "dense cluster-like structure");
    snprintf(region.archetype, sizeof(region.archetype), "cluster");
    region.density_class = VOXELPILOT_ASD_DENSITY_HIGH;
    region.mean_intensity = 0.72f;
    region.min_intensity = 0.68f;
    region.max_intensity = 0.88f;
    region.compactness = 0.33f;
    region.elongation = 1.80f;
    region.voxel_count = 30;

    memset(&correction, 0, sizeof(correction));
    voxelpilot_asd_capture_correction(&region, 1000, "Rejected artifact", 0, 1, &correction);
    applied = voxelpilot_asd_apply_corrections(&region, 1, 1000, &correction, 1);

    if (!expect_true(applied == 1, "ASD rejection should apply to matching region")) return 0;
    if (!expect_true(region.rejected == 1, "ASD rejection should mark region rejected")) return 0;
    return expect_true(strstr(region.description, "rejected") != NULL, "ASD rejection should explain provenance");
}


static int test_callout_projection_centers_visible_point(void)
{
    float point[3] = { 0.5f, 0.5f, 0.5f };
    float cam_pos[3] = { 0.5f, 0.5f, -1.0f };
    float cam_dir[3] = { 0.0f, 0.0f, 1.0f };
    float cam_up[3] = { 0.0f, 1.0f, 0.0f };
    float screen_x = 0.0f;
    float screen_y = 0.0f;
    float depth = 0.0f;

    if (!expect_true(
            voxelpilot_project_point_to_screen(
                point,
                cam_pos,
                cam_dir,
                cam_up,
                45.0f,
                1000.0f,
                800.0f,
                &screen_x,
                &screen_y,
                &depth) == 1,
            "callout projection should accept visible center point")) {
        return 0;
    }

    if (!expect_float_close(screen_x, 500.0f, "callout center point should project to viewport center X")) {
        return 0;
    }
    if (!expect_float_close(screen_y, 400.0f, "callout center point should project to viewport center Y")) {
        return 0;
    }
    return expect_true(depth > 0.0f, "callout projection should report positive depth");
}

static int test_callout_projection_rejects_point_behind_camera(void)
{
    float point[3] = { 0.5f, 0.5f, -2.0f };
    float cam_pos[3] = { 0.5f, 0.5f, -1.0f };
    float cam_dir[3] = { 0.0f, 0.0f, 1.0f };
    float cam_up[3] = { 0.0f, 1.0f, 0.0f };
    float screen_x = 0.0f;
    float screen_y = 0.0f;
    float depth = 0.0f;

    return expect_true(
        voxelpilot_project_point_to_screen(
            point,
            cam_pos,
            cam_dir,
            cam_up,
            45.0f,
            1000.0f,
            800.0f,
            &screen_x,
            &screen_y,
            &depth) == 0,
        "callout projection should reject point behind camera");
}

static int test_callout_visibility_respects_top_count_and_selection(void)
{
    if (!expect_true(
            voxelpilot_callout_should_show_rank(2, 3, 0, 0) == 1,
            "callout visibility should include top-ranked regions")) {
        return 0;
    }
    if (!expect_true(
            voxelpilot_callout_should_show_rank(5, 3, 0, 0) == 0,
            "callout visibility should hide low-ranked unselected regions")) {
        return 0;
    }
    if (!expect_true(
            voxelpilot_callout_should_show_rank(5, 3, 0, 1) == 1,
            "callout visibility should always include selected region")) {
        return 0;
    }
    return expect_true(
        voxelpilot_callout_should_show_rank(2, 3, 1, 0) == 0,
        "selected-only callout mode should hide unselected regions");
}

/* ---- volume_geometry.h: the volume's world-space box ---- */

static int test_volume_box_cube_stays_unit(void)
{
    const float spacing[3] = { 1.0f, 1.0f, 1.0f };
    float extent[3];

    voxelpilot_volume_box_extent(256, 256, 256, spacing, extent);
    if (!expect_float_close(extent[0], 1.0f, "cube extent x should be 1")) return 0;
    if (!expect_float_close(extent[1], 1.0f, "cube extent y should be 1")) return 0;
    if (!expect_float_close(extent[2], 1.0f, "cube extent z should be 1")) return 0;
    return 1;
}

static int test_volume_box_uses_dimensions_and_spacing(void)
{
    /* The carp sample: 256x256x512 at 0.78125 x 0.390625 x 1. Physical
       lengths span the voxel centres: (n - 1) * spacing. */
    const float spacing[3] = { 0.78125f, 0.390625f, 1.0f };
    float extent[3];

    voxelpilot_volume_box_extent(256, 256, 512, spacing, extent);
    if (!expect_float_close(extent[0], 255.0f * 0.78125f / 511.0f, "carp extent x")) return 0;
    if (!expect_float_close(extent[1], 255.0f * 0.390625f / 511.0f, "carp extent y")) return 0;
    if (!expect_float_close(extent[2], 1.0f, "carp longest axis should be 1")) return 0;
    return 1;
}

static int test_volume_box_rejects_bad_spacing(void)
{
    const float spacing[3] = { 0.0f, -2.0f, 1.0f };
    float extent[3];

    /* Non-positive spacing falls back to 1 on that axis. */
    voxelpilot_volume_box_extent(64, 64, 64, spacing, extent);
    if (!expect_float_close(extent[0], 1.0f, "zero spacing should fall back to 1")) return 0;
    if (!expect_float_close(extent[1], 1.0f, "negative spacing should fall back to 1")) return 0;
    return 1;
}

static int test_volume_point_world_round_trip(void)
{
    const float extent[3] = { 0.4f, 0.2f, 1.0f };
    const float corner[3] = { 0.0f, 1.0f, 0.25f };
    const float centre[3] = { 0.5f, 0.5f, 0.5f };
    float world[3];
    float back[3];

    /* The box is centred on (0.5, 0.5, 0.5) so camera presets aimed at
       the old unit cube's centre still frame any volume. */
    voxelpilot_volume_point_to_world(extent, centre, world);
    if (!expect_float_close(world[0], 0.5f, "centre maps to 0.5 x")) return 0;
    if (!expect_float_close(world[2], 0.5f, "centre maps to 0.5 z")) return 0;

    voxelpilot_volume_point_to_world(extent, corner, world);
    if (!expect_float_close(world[0], 0.3f, "corner x maps to box min")) return 0;
    if (!expect_float_close(world[1], 0.6f, "corner y maps to box max")) return 0;
    if (!expect_float_close(world[2], 0.25f, "unit-extent axis is unchanged")) return 0;

    voxelpilot_world_to_volume_point(extent, world, back);
    if (!expect_float_close(back[0], corner[0], "round trip x")) return 0;
    if (!expect_float_close(back[1], corner[1], "round trip y")) return 0;
    if (!expect_float_close(back[2], corner[2], "round trip z")) return 0;
    return 1;
}

static int test_render_command_carries_box_extent(void)
{
    VoxelPilotRenderCommandConfig config;
    RendererInput cmd;

    voxelpilot_render_command_config_defaults(&config);
    if (!expect_true(voxelpilot_build_renderer_input(&config, &cmd) == 1, "default command should build")) return 0;
    if (!expect_float_close(cmd.box_extent[0], 1.0f, "default box extent is the unit cube")) return 0;

    config.box_extent[0] = 0.4f;
    config.box_extent[1] = 0.2f;
    config.box_extent[2] = 1.0f;
    if (!expect_true(voxelpilot_build_renderer_input(&config, &cmd) == 1, "anisotropic command should build")) return 0;
    if (!expect_float_close(cmd.box_extent[0], 0.4f, "box extent x reaches the renderer")) return 0;
    if (!expect_float_close(cmd.box_extent[1], 0.2f, "box extent y reaches the renderer")) return 0;
    return 1;
}

int main(void)
{
    int passed = 1;

    passed &= test_expected_bytes_non_cubic_uint16();
    passed &= test_infer_spec_from_filename();
    passed &= test_parse_data_type_aliases();
    passed &= test_load_uint8_normalizes_values();
    passed &= test_load_uint16_normalizes_values();
    passed &= test_load_uint16_scales_to_data_range();
    passed &= test_rejects_mismatched_file_size();
    passed &= test_default_clip_bounds_start_unclipped();
    passed &= test_dock_layout_rebuild_detection();
    passed &= test_layout_refit_only_on_maximize_transition();
    passed &= test_histogram_transfer_suggestion_centers_dense_region();
    passed &= test_focus_slice_scout_selects_high_variance_axial_slice();
    passed &= test_render_extent_clamps_to_safe_range();
    passed &= test_render_target_sync_respects_match_window_mode();
    passed &= test_review_bundle_paths_sanitize_stem_and_join_folder();
    passed &= test_review_bundle_paths_reject_overlong_output();
    passed &= test_ai_review_bundle_stem_uses_source_basename();
    passed &= test_ai_review_bundle_writes_regions_json();
    passed &= test_ai_review_bundle_writes_relationships_and_label_mask();
    passed &= test_ai_review_bundle_report_references_artifacts();
    passed &= test_insight_report_writer_escapes_content_and_annotations();
    passed &= test_cli_export_plan_runs_snapshot_and_review_bundle();
    passed &= test_cli_prepare_export_folder_creates_missing_directory();
    passed &= test_json_utils_write_escaped_control_chars();
    passed &= test_json_utils_extracts_typed_fields();
    passed &= test_cuda_hint_policy_accepts_only_valid_devices();
    passed &= test_cinematic_params_default_disabled();
    passed &= test_cinematic_params_clamp_safe_ranges();
    passed &= test_semantic_demo_defaults_enable_visible_asd_callouts();
    passed &= test_raymarch_step_limit_reaches_full_volume_at_fine_steps();
    passed &= test_prompt_parser_maps_density_commands_to_transfer_settings();
    passed &= test_prompt_parser_clamps_negative_intensity_cutoffs();
    passed &= test_prompt_reset_uses_visible_default_opacity();
    passed &= test_prompt_parser_recognizes_visibility_and_quality_intents();
    passed &= test_auto_enhance_transfer_boosts_sparse_visible_histograms();
    passed &= test_dataset_health_warns_for_tiny_volume();
    passed &= test_dataset_health_flags_flat_histogram();
    passed &= test_dataset_health_flags_background_dominant_import();
    passed &= test_dataset_health_warns_for_large_working_set();
    passed &= test_dataset_health_preserves_healthy_foot_sized_volume();
    passed &= test_transfer_advisor_recommends_dark_sparse_enhancement();
    passed &= test_transfer_advisor_recommends_high_density_shell();
    passed &= test_transfer_advisor_keeps_balanced_volume_stable();
    passed &= test_auto_enhance_load_guard_preserves_high_background_ct_defaults();
    passed &= test_auto_enhance_boosts_compact_low_contrast_histograms();
    passed &= test_auto_enhance_load_frames_headsq_style_histograms();
    passed &= test_flood_fill_labels_connected_intensity_region();
    passed &= test_object_description_prefers_labels_then_density_classes();
    passed &= test_ray_pick_hits_first_visible_voxel();
    passed &= test_ray_pick_respects_clip_bounds();
    passed &= test_label_overlay_blends_annotation_color();
    passed &= test_selected_label_emphasis_boosts_only_selected_label();
    passed &= test_object_summary_identifies_high_density_dominance();
    passed &= test_quant_metrics_summarize_histogram_without_volume_scan();
    passed &= test_streaming_estimate_counts_visible_nonempty_bricks();
    passed &= test_brick_cache_plan_reports_queue_and_eviction_pressure();
    passed &= test_render_command_builder_maps_view_state();
    passed &= test_render_command_builder_attaches_label_overlay();
    passed &= test_render_command_builder_rejects_mismatched_label_overlay();
    passed &= test_render_command_builder_rejects_invalid_target_size();
    passed &= test_performance_governor_small_volume_is_healthy();
    passed &= test_performance_governor_high_render_time_suggests_interactive();
    passed &= test_performance_governor_memory_pressure_suggests_streaming();
    passed &= test_performance_governor_callout_pressure_suggests_limit();
    passed &= test_performance_governor_moving_average_is_stable();
    passed &= test_adaptive_quality_degrades_during_slow_interaction();
    passed &= test_adaptive_quality_recovers_slowly_when_frame_time_is_low();
    passed &= test_adaptive_quality_disabled_uses_base_quality();
    passed &= test_asd_rejects_invalid_inputs();
    passed &= test_asd_discovers_high_density_connected_region();
    passed &= test_asd_assigns_archetype_stability_and_importance();
    passed &= test_asd_ranks_regions_and_reports_relationships();
    passed &= test_guided_review_selects_top_ranked_region();
    passed &= test_guided_review_skips_rejected_regions();
    passed &= test_guided_review_wraps_through_reviewable_regions();
    passed &= test_asd_correction_memory_confirms_matching_region();
    passed &= test_asd_correction_memory_rejects_matching_region();
    passed &= test_callout_projection_centers_visible_point();
    passed &= test_callout_projection_rejects_point_behind_camera();
    passed &= test_callout_visibility_respects_top_count_and_selection();
    passed &= test_volume_box_cube_stays_unit();
    passed &= test_volume_box_uses_dimensions_and_spacing();
    passed &= test_volume_box_rejects_bad_spacing();
    passed &= test_volume_point_world_round_trip();
    passed &= test_render_command_carries_box_extent();

    if (!passed) {
        return 1;
    }

    printf("volume_import_tests: PASS\n");
    return 0;
}
