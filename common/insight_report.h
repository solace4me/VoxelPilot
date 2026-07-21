#ifndef VOXELPILOT_INSIGHT_REPORT_H
#define VOXELPILOT_INSIGHT_REPORT_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "ai_features.h"
#include "ai_review_bundle.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int          active;
    unsigned int label_id;
    const char  *name;
    size_t       voxel_count;
    float        min_intensity;
    float        max_intensity;
} VoxelPilotInsightAnnotation;

typedef struct {
    int   volume_width;
    int   volume_height;
    int   volume_depth;
    float hist_min_value;
    float hist_max_value;
    float voxel_spacing[3];
    const char *source_path;
    const VoxelPilotQuantMetrics  *quant_metrics;
    const char *quant_metrics_status;
    const VoxelPilotObjectSummary *object_summary;
    const char *object_summary_status;
    float measure_a[3];
    float measure_b[3];
    float measurement_distance_voxels;
    float measurement_distance_world;
    const VoxelPilotInsightAnnotation *annotations;
    int annotation_count;
    float last_render_ms;
    float gui_fps;
    int streaming_visible_bricks;
    int streaming_resident_candidates;
    int streaming_stream_now;
    int streaming_queue;
    int streaming_evictable;
} VoxelPilotInsightReportContent;

static inline void voxelpilot_insight_report_content_defaults(
    VoxelPilotInsightReportContent *content)
{
    if (!content) return;
    memset(content, 0, sizeof(*content));
    content->source_path = "startup/synthetic volume";
    content->quant_metrics_status = "Quantitative metrics are not available.";
    content->object_summary_status = "Object summary is not available.";
    content->voxel_spacing[0] = 1.0f;
    content->voxel_spacing[1] = 1.0f;
    content->voxel_spacing[2] = 1.0f;
}

static inline int voxelpilot_insight_write_report(
    const VoxelPilotInsightReportContent *content,
    const char                           *path)
{
    FILE *fp;
    size_t voxel_count;
    int i;
    int emitted_annotations = 0;

    if (!content || !path || !path[0] ||
        content->volume_width < 1 ||
        content->volume_height < 1 ||
        content->volume_depth < 1 ||
        content->annotation_count < 0) {
        return 0;
    }
    if (content->annotation_count > 0 && !content->annotations) {
        return 0;
    }

    fp = fopen(path, "w");
    if (!fp) return 0;

    voxel_count =
        (size_t)content->volume_width *
        (size_t)content->volume_height *
        (size_t)content->volume_depth;

    fputs("<!doctype html><html><head><meta charset=\"utf-8\">", fp);
    fputs("<title>VoxelPilot Insight Report</title>", fp);
    fputs("<style>body{font-family:Segoe UI,Arial,sans-serif;background:#101820;color:#eef;line-height:1.45;margin:32px;}section{border:1px solid #345;padding:16px;margin:16px 0;border-radius:10px;background:#16212b;}table{border-collapse:collapse;width:100%;}td,th{border-bottom:1px solid #345;padding:6px;text-align:left;}code{color:#8fe;}</style>", fp);
    fputs("</head><body><h1>VoxelPilot Insight Report</h1>", fp);

    fputs("<section><h2>Dataset</h2><table>", fp);
    fprintf(fp, "<tr><th>Dimensions</th><td>%d x %d x %d</td></tr>",
            content->volume_width,
            content->volume_height,
            content->volume_depth);
    fprintf(fp, "<tr><th>Voxel count</th><td>%zu</td></tr>", voxel_count);
    fprintf(fp, "<tr><th>Value range</th><td>%.4f to %.4f</td></tr>",
            content->hist_min_value,
            content->hist_max_value);
    fprintf(fp, "<tr><th>Voxel spacing</th><td>%.3f / %.3f / %.3f</td></tr>",
            content->voxel_spacing[0],
            content->voxel_spacing[1],
            content->voxel_spacing[2]);
    fputs("<tr><th>Source path</th><td><code>", fp);
    voxelpilot_html_write_escaped(
        fp,
        content->source_path && content->source_path[0]
            ? content->source_path
            : "startup/synthetic volume");
    fputs("</code></td></tr></table></section>", fp);

    fputs("<section><h2>Quantitative Metrics</h2><table>", fp);
    if (content->quant_metrics) {
        fprintf(fp, "<tr><th>Visible ratio</th><td>%.1f%%</td></tr>",
                content->quant_metrics->visible_ratio * 100.0f);
        fprintf(fp, "<tr><th>Median / P95 / Mean</th><td>%.3f / %.3f / %.3f</td></tr>",
                content->quant_metrics->percentile_50,
                content->quant_metrics->percentile_95,
                content->quant_metrics->mean_intensity);
        fprintf(fp, "<tr><th>Low / Mid / High density</th><td>%.1f%% / %.1f%% / %.1f%%</td></tr>",
                content->quant_metrics->low_density_ratio * 100.0f,
                content->quant_metrics->mid_density_ratio * 100.0f,
                content->quant_metrics->high_density_ratio * 100.0f);
    }
    fputs("<tr><th>Summary</th><td>", fp);
    voxelpilot_html_write_escaped(fp, content->quant_metrics_status);
    fputs("</td></tr></table></section>", fp);

    fputs("<section><h2>AI/Object Review</h2><table>", fp);
    fputs("<tr><th>AI type</th><td>Local explainable heuristic AI: rule prompts, histogram/material heuristics, ray picking, flood-fill segmentation, and human-confirmed annotations. No cloud model or trained neural network is used in this demo slice.</td></tr>", fp);
    fputs("<tr><th>Primary material</th><td>", fp);
    voxelpilot_html_write_escaped(
        fp,
        content->object_summary ? content->object_summary->primary_material : "Not available");
    fputs("</td></tr><tr><th>Object summary</th><td>", fp);
    voxelpilot_html_write_escaped(fp, content->object_summary_status);
    fputs("</td></tr></table></section>", fp);

    fputs("<section><h2>Measurements</h2><table>", fp);
    fprintf(fp, "<tr><th>Point A</th><td>%.3f, %.3f, %.3f</td></tr>",
            content->measure_a[0], content->measure_a[1], content->measure_a[2]);
    fprintf(fp, "<tr><th>Point B</th><td>%.3f, %.3f, %.3f</td></tr>",
            content->measure_b[0], content->measure_b[1], content->measure_b[2]);
    fprintf(fp, "<tr><th>Distance</th><td>%.3f voxels / %.3f world units</td></tr>",
            content->measurement_distance_voxels,
            content->measurement_distance_world);
    fputs("</table></section>", fp);

    fputs("<section><h2>Annotations</h2><table><tr><th>ID</th><th>Name</th><th>Voxels</th><th>Intensity Range</th></tr>", fp);
    for (i = 0; i < content->annotation_count; ++i) {
        const VoxelPilotInsightAnnotation *annotation = &content->annotations[i];
        if (!annotation->active) continue;
        fprintf(fp, "<tr><td>%u</td><td>", annotation->label_id);
        voxelpilot_html_write_escaped(fp, annotation->name);
        fprintf(fp, "</td><td>%zu</td><td>%.3f - %.3f</td></tr>",
                annotation->voxel_count,
                annotation->min_intensity,
                annotation->max_intensity);
        emitted_annotations++;
    }
    if (emitted_annotations == 0) {
        fputs("<tr><td colspan=\"4\">No annotations yet.</td></tr>", fp);
    }
    fputs("</table></section>", fp);

    fputs("<section><h2>Performance / Streaming</h2><table>", fp);
    fprintf(fp, "<tr><th>Render time</th><td>%.2f ms</td></tr>", content->last_render_ms);
    fprintf(fp, "<tr><th>FPS</th><td>%.1f</td></tr>", content->gui_fps);
    fprintf(fp, "<tr><th>Brick cache plan</th><td>%d visible, %d candidates, stream %d now, queue %d, evictable %d</td></tr>",
            content->streaming_visible_bricks,
            content->streaming_resident_candidates,
            content->streaming_stream_now,
            content->streaming_queue,
            content->streaming_evictable);
    fputs("</table></section>", fp);

    fputs("</body></html>\n", fp);
    return fclose(fp) == 0;
}

#ifdef __cplusplus
}
#endif

#endif