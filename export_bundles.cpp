/* Exported AI review bundle + insight report generation.

   Extracted from standalone_main.cu as step 1 of the Tier 3c module
   split (see docs/superpowers/plans/2026-09-30-standalone-main-split.md).
   Compiled separately with cl.exe (like common/volume_import.cpp and
   third_party/glew-2.2.0/src/glew.c) and linked into the final binary
   alongside the nvcc-compiled objects - see build_windows.bat. */

#include <stdio.h>
#include <string.h>

#include "tinyfiledialogs.h"

#include "standalone_app.h"

static void build_ai_review_bundle_content(
    StandaloneApp                    *app,
    VoxelPilotAiReviewBundleContent  *content)
{
    voxelpilot_ai_review_bundle_content_defaults(content);
    if (!app || !content) return;

    content->volume_width = app->renderer.W;
    content->volume_height = app->renderer.H;
    content->volume_depth = app->renderer.D;
    content->selected_label_id =
        (app->selected_annotation >= 0 && app->selected_annotation < app->annotation_count)
            ? (unsigned int)app->annotations[app->selected_annotation].label_id
            : 0u;
    content->regions = app->semantic_regions;
    content->region_count = app->semantic_region_count;
    content->relations = app->semantic_relations;
    content->relation_count = app->semantic_relation_count;
    content->label_mask = app->label_mask;
    content->label_mask_width = app->label_mask_W;
    content->label_mask_height = app->label_mask_H;
    content->label_mask_depth = app->label_mask_D;
    content->object_summary_status = app->object_summary_status;
}

int export_ai_review_bundle_to_folder(StandaloneApp *app, const char *folder)
{
    VoxelPilotReviewBundlePaths paths;
    VoxelPilotAiReviewBundleContent bundle_content;
    char stem[128];
    char folder_error[256];

    if (!app || !app->renderer.volume_data || app->renderer.W < 1 || app->renderer.H < 1 || app->renderer.D < 1) {
        if (app) snprintf(app->report_status, sizeof(app->report_status), "Load a volume before exporting an AI review bundle.");
        return 0;
    }
    if (!folder || !folder[0]) {
        snprintf(app->report_status, sizeof(app->report_status), "Choose a folder for the AI review bundle.");
        return 0;
    }
    if (!voxelpilot_cli_prepare_export_folder(folder, folder_error, sizeof(folder_error))) {
        snprintf(app->report_status,
                 sizeof(app->report_status),
                 "Could not prepare AI review bundle folder: %s",
                 folder_error[0] ? folder_error : folder);
        return 0;
    }

    if (app->semantic_region_count < 1) {
        run_semantic_demo_workflow(app, "AI review bundle");
    }
    if (app->semantic_region_count < 1 || !app->label_mask || app->annotation_count < 1) {
        snprintf(app->report_status, sizeof(app->report_status),
                 "AI review bundle needs ASD regions. Run Semantic Demo or tune ASD settings first.");
        return 0;
    }

    voxelpilot_make_ai_review_bundle_stem_from_source(app->upload_path, stem, sizeof(stem));
    if (!voxelpilot_make_review_bundle_paths(folder, stem, &paths)) {
        snprintf(app->report_status, sizeof(app->report_status), "Could not prepare AI review bundle paths.");
        return 0;
    }

    update_quant_metrics(app);
    update_object_summary(app);
    update_streaming_estimate(app);
    build_ai_review_bundle_content(app, &bundle_content);

    render_frame_gpu_snapshot(app);
    if (!app->renderer.h_out ||
        write_png_file((const unsigned char *)app->renderer.h_out, app->render_w, app->render_h, paths.snapshot_png) != 0) {
        snprintf(app->report_status, sizeof(app->report_status), "Could not write AI review snapshot.");
        return 0;
    }
    if (!voxelpilot_ai_review_write_regions_json(&bundle_content, paths.regions_json)) {
        snprintf(app->report_status, sizeof(app->report_status), "Could not write AI review regions JSON.");
        return 0;
    }
    if (!voxelpilot_ai_review_write_relationships_json(&bundle_content, paths.relationships_json)) {
        snprintf(app->report_status, sizeof(app->report_status), "Could not write AI review relationships JSON.");
        return 0;
    }
    if (!voxelpilot_ai_review_write_label_mask(&bundle_content, paths.label_mask_raw)) {
        snprintf(app->report_status, sizeof(app->report_status), "Could not write AI review label mask.");
        return 0;
    }
    if (!voxelpilot_ai_review_write_bundle_report(&bundle_content, &paths, paths.report_html)) {
        snprintf(app->report_status, sizeof(app->report_status), "Could not write AI review bundle report.");
        return 0;
    }

    strncpy(app->report_path, paths.report_html, sizeof(app->report_path) - 1);
    app->report_path[sizeof(app->report_path) - 1] = '\0';
    snprintf(app->report_status, sizeof(app->report_status),
             "Exported AI review bundle: %s", paths.report_html);
    return 1;
}

void export_ai_review_bundle(StandaloneApp *app)
{
    const char *folder = tinyfd_selectFolderDialog("Export VoxelPilot AI Review Bundle", ".");
    if (!folder) {
        if (app) snprintf(app->report_status, sizeof(app->report_status), "AI review bundle export canceled.");
        return;
    }
    export_ai_review_bundle_to_folder(app, folder);
}

static void build_insight_report_content(
    StandaloneApp                    *app,
    VoxelPilotInsightReportContent   *content,
    VoxelPilotInsightAnnotation      *annotations,
    int                               annotation_capacity)
{
    int i;
    int count = 0;

    if (!content) return;
    voxelpilot_insight_report_content_defaults(content);
    if (!app) return;

    content->volume_width = app->renderer.W;
    content->volume_height = app->renderer.H;
    content->volume_depth = app->renderer.D;
    content->hist_min_value = app->hist_min_value;
    content->hist_max_value = app->hist_max_value;
    memcpy(content->voxel_spacing, app->voxel_spacing, sizeof(content->voxel_spacing));
    content->source_path = app->upload_path[0] ? app->upload_path : "startup/synthetic volume";
    content->quant_metrics = &app->quant_metrics;
    content->quant_metrics_status = app->quant_metrics_status;
    content->object_summary = &app->object_summary;
    content->object_summary_status = app->object_summary_status;
    memcpy(content->measure_a, app->measure_a, sizeof(content->measure_a));
    memcpy(content->measure_b, app->measure_b, sizeof(content->measure_b));
    content->measurement_distance_voxels = measurement_distance(app);
    content->measurement_distance_world = measurement_distance_world(app);
    content->last_render_ms = app->last_render_ms;
    content->gui_fps = app->gui_fps;
    content->streaming_visible_bricks = app->streaming_visible_bricks;
    content->streaming_resident_candidates = app->streaming_resident_candidates;
    content->streaming_stream_now = app->streaming_stream_now;
    content->streaming_queue = app->streaming_queue;
    content->streaming_evictable = app->streaming_evictable;

    if (annotations && annotation_capacity > 0) {
        for (i = 0; i < app->annotation_count && count < annotation_capacity; ++i) {
            AnnotationRegion *region = &app->annotations[i];
            annotations[count].active = region->active;
            annotations[count].label_id = (unsigned int)region->label_id;
            annotations[count].name = region->name;
            annotations[count].voxel_count = region->voxel_count;
            annotations[count].min_intensity = region->min_intensity;
            annotations[count].max_intensity = region->max_intensity;
            count++;
        }
    }
    content->annotations = annotations;
    content->annotation_count = count;
}

void export_insight_report(StandaloneApp *app)
{
    const char *patterns[] = { "*.html" };
    const char *selected;
    VoxelPilotInsightReportContent content;
    VoxelPilotInsightAnnotation report_annotations[MAX_ANNOTATIONS];

    if (!app->renderer.volume_data || app->renderer.W < 1 || app->renderer.H < 1 || app->renderer.D < 1) {
        snprintf(app->report_status, sizeof(app->report_status),
                 "Load a volume before exporting a report.");
        return;
    }

    selected = tinyfd_saveFileDialog(
        "Export VoxelPilot Insight Report",
        app->report_path[0] ? app->report_path : "voxelpilot_insight_report.html",
        1,
        patterns,
        "HTML Report (*.html)");

    if (!selected) {
        snprintf(app->report_status, sizeof(app->report_status),
                 "Report export canceled.");
        return;
    }

    update_quant_metrics(app);
    if (!app->object_summary_ready) {
        update_object_summary(app);
    }
    update_streaming_estimate(app);
    build_insight_report_content(app, &content, report_annotations, MAX_ANNOTATIONS);

    if (!voxelpilot_insight_write_report(&content, selected)) {
        snprintf(app->report_status, sizeof(app->report_status),
                 "Could not write report file.");
        return;
    }

    strncpy(app->report_path, selected, sizeof(app->report_path) - 1);
    app->report_path[sizeof(app->report_path) - 1] = '\0';
    snprintf(app->report_status, sizeof(app->report_status),
             "Exported insight report: %s", selected);
}
