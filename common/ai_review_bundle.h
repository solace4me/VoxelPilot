#ifndef VOXELPILOT_AI_REVIEW_BUNDLE_H
#define VOXELPILOT_AI_REVIEW_BUNDLE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "json_utils.h"
#include "semantic_discovery.h"
#include "ui_state_helpers.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int volume_width;
    int volume_height;
    int volume_depth;
    unsigned int selected_label_id;
    const VoxelPilotSemanticRegion *regions;
    int region_count;
    const VoxelPilotRegionRelation *relations;
    int relation_count;
    const unsigned char *label_mask;
    int label_mask_width;
    int label_mask_height;
    int label_mask_depth;
    const char *object_summary_status;
} VoxelPilotAiReviewBundleContent;

static inline void voxelpilot_ai_review_bundle_content_defaults(
    VoxelPilotAiReviewBundleContent *content)
{
    if (!content) return;
    memset(content, 0, sizeof(*content));
    content->object_summary_status = "Not available";
}

static inline void voxelpilot_html_write_escaped(FILE *fp, const char *text)
{
    const unsigned char *cursor;

    if (!fp) return;
    cursor = (const unsigned char *)(text ? text : "");
    while (*cursor) {
        if (*cursor == '&') {
            fputs("&amp;", fp);
        } else if (*cursor == '<') {
            fputs("&lt;", fp);
        } else if (*cursor == '>') {
            fputs("&gt;", fp);
        } else if (*cursor == '"') {
            fputs("&quot;", fp);
        } else {
            fputc((int)*cursor, fp);
        }
        ++cursor;
    }
}

static inline const char *voxelpilot_review_bundle_file_name(const char *path)
{
    const char *slash;
    const char *backslash;

    if (!path) return "";
    slash = strrchr(path, '/');
    backslash = strrchr(path, '\\');
    if (!slash || (backslash && backslash > slash)) slash = backslash;
    return slash ? slash + 1 : path;
}

static inline int voxelpilot_make_ai_review_bundle_stem_from_source(
    const char *source_path,
    char       *out_stem,
    size_t      out_stem_size)
{
    const char *source;
    const char *base;
    const char *dot;
    size_t length;

    if (!out_stem || out_stem_size == 0) return 0;

    source = (source_path && source_path[0]) ? source_path : "voxelpilot_ai_review";
    base = strrchr(source, '\\');
    if (!base) base = strrchr(source, '/');
    base = base ? base + 1 : source;
    dot = strrchr(base, '.');
    length = dot && dot > base ? (size_t)(dot - base) : strlen(base);
    if (length >= out_stem_size) length = out_stem_size - 1;
    memcpy(out_stem, base, length);
    out_stem[length] = '\0';
    if (!out_stem[0]) {
        snprintf(out_stem, out_stem_size, "voxelpilot_ai_review");
    }
    return 1;
}

static inline int voxelpilot_ai_review_write_regions_json(
    const VoxelPilotAiReviewBundleContent *content,
    const char                            *path)
{
    FILE *fp;
    int i;

    if (!content || !path || !path[0] || content->region_count < 0) return 0;
    if (content->region_count > 0 && !content->regions) return 0;

    fp = fopen(path, "w");
    if (!fp) return 0;

    fprintf(fp, "{\n");
    fprintf(fp, "  \"version\": 1,\n");
    fprintf(fp, "  \"source\": \"ASD-Lite heuristic\",\n");
    fprintf(fp, "  \"dimensions\": [%d, %d, %d],\n",
            content->volume_width,
            content->volume_height,
            content->volume_depth);
    fprintf(fp, "  \"selectedLabelId\": %u,\n", content->selected_label_id);
    fprintf(fp, "  \"regions\": [\n");
    for (i = 0; i < content->region_count; ++i) {
        const VoxelPilotSemanticRegion *region = &content->regions[i];
        fprintf(fp, "    {\"labelId\": %u, \"name\": \"", (unsigned int)region->label_id);
        voxelpilot_json_write_escaped(fp, region->name);
        fprintf(fp, "\", \"archetype\": \"");
        voxelpilot_json_write_escaped(fp, region->archetype);
        fprintf(fp, "\", \"category\": \"");
        voxelpilot_json_write_escaped(fp, region->category);
        fprintf(fp,
                "\", \"voxelCount\": %zu, \"confidence\": %.6f, \"stabilityScore\": %.6f, \"importanceScore\": %.6f, \"importanceRank\": %d, \"meanIntensity\": %.6f, \"minIntensity\": %.6f, \"maxIntensity\": %.6f, \"centroid\": [%.6f, %.6f, %.6f], \"bboxMin\": [%d, %d, %d], \"bboxMax\": [%d, %d, %d], \"userConfirmed\": %d, \"rejected\": %d, \"description\": \"",
                region->voxel_count,
                region->confidence,
                region->stability_score,
                region->importance_score,
                region->importance_rank,
                region->mean_intensity,
                region->min_intensity,
                region->max_intensity,
                region->centroid[0], region->centroid[1], region->centroid[2],
                region->bbox_min[0], region->bbox_min[1], region->bbox_min[2],
                region->bbox_max[0], region->bbox_max[1], region->bbox_max[2],
                region->user_confirmed,
                region->rejected);
        voxelpilot_json_write_escaped(fp, region->description);
        fprintf(fp, "\"}%s\n", (i + 1 < content->region_count) ? "," : "");
    }
    fprintf(fp, "  ]\n}\n");
    return fclose(fp) == 0;
}

static inline int voxelpilot_ai_review_write_relationships_json(
    const VoxelPilotAiReviewBundleContent *content,
    const char                            *path)
{
    FILE *fp;
    int i;

    if (!content || !path || !path[0] || content->relation_count < 0) return 0;
    if (content->relation_count > 0 && !content->relations) return 0;

    fp = fopen(path, "w");
    if (!fp) return 0;

    fprintf(fp, "{\n  \"version\": 1,\n  \"relationships\": [\n");
    for (i = 0; i < content->relation_count; ++i) {
        const VoxelPilotRegionRelation *relation = &content->relations[i];
        fprintf(fp,
                "    {\"regionA\": %u, \"regionB\": %u, \"type\": %d, \"centroidDistance\": %.6f, \"confidence\": %.6f, \"description\": \"",
                (unsigned int)relation->region_a,
                (unsigned int)relation->region_b,
                (int)relation->type,
                relation->centroid_distance,
                relation->confidence);
        voxelpilot_json_write_escaped(fp, relation->description);
        fprintf(fp, "\"}%s\n", (i + 1 < content->relation_count) ? "," : "");
    }
    fprintf(fp, "  ]\n}\n");
    return fclose(fp) == 0;
}

static inline int voxelpilot_ai_review_write_label_mask(
    const VoxelPilotAiReviewBundleContent *content,
    const char                            *path)
{
    FILE *fp;
    size_t voxel_count;
    size_t written;
    int close_result;

    if (!content || !path || !path[0] || !content->label_mask ||
        content->label_mask_width < 1 ||
        content->label_mask_height < 1 ||
        content->label_mask_depth < 1) {
        return 0;
    }

    voxel_count =
        (size_t)content->label_mask_width *
        (size_t)content->label_mask_height *
        (size_t)content->label_mask_depth;
    fp = fopen(path, "wb");
    if (!fp) return 0;
    written = fwrite(content->label_mask, 1, voxel_count, fp);
    close_result = fclose(fp);
    return written == voxel_count && close_result == 0;
}

static inline int voxelpilot_ai_review_write_bundle_report(
    const VoxelPilotAiReviewBundleContent *content,
    const VoxelPilotReviewBundlePaths     *paths,
    const char                            *path)
{
    FILE *fp;
    int i;

    if (!content || !paths || !path || !path[0]) return 0;
    if (content->region_count < 0 || content->relation_count < 0) return 0;
    if (content->region_count > 0 && !content->regions) return 0;
    if (content->relation_count > 0 && !content->relations) return 0;

    fp = fopen(path, "w");
    if (!fp) return 0;

    fputs("<!doctype html><html><head><meta charset=\"utf-8\">", fp);
    fputs("<title>VoxelPilot AI Review Bundle</title>", fp);
    fputs("<style>body{font-family:Segoe UI,Arial,sans-serif;background:#101820;color:#eef;line-height:1.45;margin:32px;}section{border:1px solid #345;padding:16px;margin:16px 0;border-radius:10px;background:#16212b;}table{border-collapse:collapse;width:100%;}td,th{border-bottom:1px solid #345;padding:6px;text-align:left;}code{color:#8fe;}img{max-width:100%;border:1px solid #345;border-radius:8px;}</style>", fp);
    fputs("</head><body><h1>VoxelPilot AI Review Bundle</h1>", fp);

    fputs("<section><h2>Summary</h2><table>", fp);
    fprintf(fp, "<tr><th>Dimensions</th><td>%d x %d x %d</td></tr>",
            content->volume_width,
            content->volume_height,
            content->volume_depth);
    fprintf(fp, "<tr><th>Semantic regions</th><td>%d</td></tr>", content->region_count);
    fprintf(fp, "<tr><th>Relationships</th><td>%d</td></tr>", content->relation_count);
    fputs("<tr><th>AI type</th><td>Local explainable heuristic AI using histogram, connected regions, shape archetypes, stability, importance ranking, hover picking, and user-confirmed corrections. No cloud model is used.</td></tr>", fp);
    fputs("<tr><th>Object summary</th><td>", fp);
    voxelpilot_html_write_escaped(fp, content->object_summary_status);
    fputs("</td></tr></table></section>", fp);

    fputs("<section><h2>Snapshot</h2><p><img src=\"", fp);
    voxelpilot_html_write_escaped(fp, voxelpilot_review_bundle_file_name(paths->snapshot_png));
    fputs("\" alt=\"VoxelPilot render snapshot\"></p></section>", fp);

    fputs("<section><h2>Bundle Files</h2><ul>", fp);
    fputs("<li>Snapshot: <code>", fp);
    voxelpilot_html_write_escaped(fp, voxelpilot_review_bundle_file_name(paths->snapshot_png));
    fputs("</code></li>", fp);
    fputs("<li>Regions JSON: <code>", fp);
    voxelpilot_html_write_escaped(fp, voxelpilot_review_bundle_file_name(paths->regions_json));
    fputs("</code></li>", fp);
    fputs("<li>Relationships JSON: <code>", fp);
    voxelpilot_html_write_escaped(fp, voxelpilot_review_bundle_file_name(paths->relationships_json));
    fputs("</code></li>", fp);
    fputs("<li>Label mask RAW: <code>", fp);
    voxelpilot_html_write_escaped(fp, voxelpilot_review_bundle_file_name(paths->label_mask_raw));
    fputs("</code></li>", fp);
    fputs("</ul></section>", fp);

    fputs("<section><h2>Regions</h2><table><tr><th>ID</th><th>Name</th><th>Archetype</th><th>Confidence</th><th>Stability</th><th>Importance</th><th>Voxels</th></tr>", fp);
    for (i = 0; i < content->region_count; ++i) {
        const VoxelPilotSemanticRegion *region = &content->regions[i];
        fprintf(fp, "<tr><td>%u</td><td>", (unsigned int)region->label_id);
        voxelpilot_html_write_escaped(fp, region->name);
        fputs("</td><td>", fp);
        voxelpilot_html_write_escaped(fp, region->archetype);
        fprintf(fp, "</td><td>%.0f%%</td><td>%.0f%%</td><td>rank %d / %.0f%%</td><td>%zu</td></tr>",
                region->confidence * 100.0f,
                region->stability_score * 100.0f,
                region->importance_rank,
                region->importance_score * 100.0f,
                region->voxel_count);
    }
    fputs("</table></section>", fp);

    fputs("<section><h2>Relationship Notes</h2><ul>", fp);
    for (i = 0; i < content->relation_count; ++i) {
        fputs("<li>", fp);
        voxelpilot_html_write_escaped(fp, content->relations[i].description);
        fputs("</li>", fp);
    }
    if (content->relation_count == 0) fputs("<li>No region relationships discovered.</li>", fp);
    fputs("</ul></section>", fp);

    fputs("</body></html>\n", fp);
    return fclose(fp) == 0;
}

#ifdef __cplusplus
}
#endif

#endif /* VOXELPILOT_AI_REVIEW_BUNDLE_H */