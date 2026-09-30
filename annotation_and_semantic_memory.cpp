/* Annotation regions, label-mask management, and ASD correction-memory
   persistence.

   Extracted from standalone_main.cu as step 2 of the Tier 3c module
   split (see docs/superpowers/plans/2026-09-30-standalone-main-split.md).
   Compiled separately with cl.exe and linked into the final binary
   alongside the nvcc-compiled objects - see build_windows.bat.

   Note: export_annotation_mask has no callers anywhere in the codebase
   as of this extraction - it moved here unchanged rather than being
   removed, since dead-code removal is a separate decision from a file
   split. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "standalone_app.h"

void reset_semantic_discovery(StandaloneApp *app)
{
    if (!app) return;
    memset(app->semantic_regions, 0, sizeof(app->semantic_regions));
    memset(app->semantic_relations, 0, sizeof(app->semantic_relations));
    app->semantic_region_count = 0;
    app->semantic_relation_count = 0;
    app->semantic_selected_region = -1;
    snprintf(app->semantic_status, sizeof(app->semantic_status),
             "Click Discover Regions to run ASD-Lite on the loaded volume.");
}

void reset_annotations(StandaloneApp *app)
{
    if (app->label_mask) {
        memset(
            app->label_mask,
            0,
            (size_t)app->label_mask_W *
            (size_t)app->label_mask_H *
            (size_t)app->label_mask_D);
    }

    memset(app->annotations, 0, sizeof(app->annotations));
    app->annotation_count = 0;
    app->selected_annotation = -1;
    reset_semantic_discovery(app);
    app->label_mask_revision++;
    snprintf(app->annotation_status, sizeof(app->annotation_status),
             "No annotation regions yet.");
}

int ensure_label_mask(StandaloneApp *app)
{
    size_t voxel_count;
    int W = app->renderer.W;
    int H = app->renderer.H;
    int D = app->renderer.D;

    if (!app->renderer.volume_data || W < 1 || H < 1 || D < 1) {
        return 0;
    }

    if (app->label_mask &&
        app->label_mask_W == W &&
        app->label_mask_H == H &&
        app->label_mask_D == D) {
        return 1;
    }

    free(app->label_mask);
    app->label_mask = NULL;
    app->label_mask_W = W;
    app->label_mask_H = H;
    app->label_mask_D = D;

    voxel_count = (size_t)W * (size_t)H * (size_t)D;
    app->label_mask = (unsigned char *)calloc(voxel_count, 1);
    if (!app->label_mask) {
        app->label_mask_W = 0;
        app->label_mask_H = 0;
        app->label_mask_D = 0;
        snprintf(app->annotation_status, sizeof(app->annotation_status),
                 "Could not allocate label mask for this volume.");
        return 0;
    }

    reset_annotations(app);
    return 1;
}

int point_to_voxel_index(
    const StandaloneApp *app,
    const float          point[3],
    int                 *out_x,
    int                 *out_y,
    int                 *out_z,
    size_t              *out_index)
{
    int W = app->renderer.W;
    int H = app->renderer.H;
    int D = app->renderer.D;
    int x;
    int y;
    int z;

    if (!point || W < 1 || H < 1 || D < 1) {
        return 0;
    }

    x = (int)(voxelpilot_ai_clampf(point[0], 0.0f, 1.0f) * (float)(W - 1) + 0.5f);
    y = (int)(voxelpilot_ai_clampf(point[1], 0.0f, 1.0f) * (float)(H - 1) + 0.5f);
    z = (int)(voxelpilot_ai_clampf(point[2], 0.0f, 1.0f) * (float)(D - 1) + 0.5f);

    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
    if (out_z) *out_z = z;
    if (out_index) {
        *out_index =
            (size_t)z * (size_t)W * (size_t)H +
            (size_t)y * (size_t)W +
            (size_t)x;
    }

    return 1;
}

void point_from_slice_uv(
    const StandaloneApp *app,
    int                  plane,
    float                u,
    float                v,
    float                point[3])
{
    point[0] = app->slice_x;
    point[1] = app->slice_y;
    point[2] = app->slice_z;

    if (plane == 0) {
        point[0] = u;
        point[1] = v;
    } else if (plane == 1) {
        point[0] = u;
        point[2] = v;
    } else {
        point[1] = u;
        point[2] = v;
    }
}

static AnnotationRegion *find_annotation_by_label(
    StandaloneApp *app,
    unsigned char  label_id)
{
    int i;

    for (i = 0; i < MAX_ANNOTATIONS; ++i) {
        if (app->annotations[i].active &&
            app->annotations[i].label_id == label_id) {
            return &app->annotations[i];
        }
    }

    return NULL;
}

static VoxelPilotSemanticRegion *find_semantic_region_by_label(
    StandaloneApp *app,
    unsigned char  label_id)
{
    int i;

    if (!app || label_id == 0) {
        return NULL;
    }

    for (i = 0; i < app->semantic_region_count; ++i) {
        if (app->semantic_regions[i].label_id == label_id) {
            return &app->semantic_regions[i];
        }
    }

    return NULL;
}

const char *semantic_relationship_note(
    StandaloneApp *app,
    unsigned char  label_id)
{
    static char empty_note[1] = { '\0' };
    int i;

    if (!app || label_id == 0) {
        return empty_note;
    }

    for (i = 0; i < app->semantic_relation_count; ++i) {
        if (app->semantic_relations[i].region_a == label_id ||
            app->semantic_relations[i].region_b == label_id) {
            return app->semantic_relations[i].description;
        }
    }

    return empty_note;
}

static void clear_label_from_mask(
    StandaloneApp *app,
    unsigned char  label_id)
{
    size_t voxel_count;
    size_t i;

    if (!app->label_mask || label_id == 0) {
        return;
    }

    voxel_count =
        (size_t)app->label_mask_W *
        (size_t)app->label_mask_H *
        (size_t)app->label_mask_D;

    for (i = 0; i < voxel_count; ++i) {
        if (app->label_mask[i] == label_id) {
            app->label_mask[i] = 0;
        }
    }
}

size_t semantic_total_voxels(StandaloneApp *app)
{
    if (!app || app->renderer.W < 1 || app->renderer.H < 1 || app->renderer.D < 1) return 0;
    return (size_t)app->renderer.W * (size_t)app->renderer.H * (size_t)app->renderer.D;
}

void refresh_semantic_annotation_overlays(StandaloneApp *app)
{
    int i;
    int overlay_count = 0;

    if (!app) return;
    memset(app->annotations, 0, sizeof(app->annotations));
    app->annotation_count = 0;
    app->selected_annotation = -1;

    for (i = 0; i < app->semantic_region_count; ++i) {
        VoxelPilotSemanticRegion *semantic = &app->semantic_regions[i];
        AnnotationRegion *region;
        if (semantic->rejected) {
            clear_label_from_mask(app, semantic->label_id);
            continue;
        }
        if (overlay_count >= MAX_ANNOTATIONS) continue;
        region = &app->annotations[overlay_count];
        memset(region, 0, sizeof(*region));
        region->active = 1;
        region->label_id = semantic->label_id;
        snprintf(region->name, sizeof(region->name), "%s", semantic->name);
        region->color[0] = semantic->color[0];
        region->color[1] = semantic->color[1];
        region->color[2] = semantic->color[2];
        region->seed[0] = semantic->centroid[0] / fmaxf((float)(app->renderer.W - 1), 1.0f);
        region->seed[1] = semantic->centroid[1] / fmaxf((float)(app->renderer.H - 1), 1.0f);
        region->seed[2] = semantic->centroid[2] / fmaxf((float)(app->renderer.D - 1), 1.0f);
        region->seed_intensity = semantic->mean_intensity;
        region->tolerance = app->semantic_sensitivity;
        region->voxel_count = semantic->voxel_count;
        region->min_intensity = semantic->min_intensity;
        region->max_intensity = semantic->max_intensity;
        if (i == app->semantic_selected_region) app->selected_annotation = overlay_count;
        overlay_count++;
    }

    app->annotation_count = overlay_count;
    if (app->selected_annotation < 0 && overlay_count > 0) app->selected_annotation = 0;
    app->label_mask_revision++;
    snprintf(app->annotation_status, sizeof(app->annotation_status),
             "Semantic overlays ready: %d ASD region(s).", overlay_count);
}

static int upsert_semantic_correction(
    StandaloneApp *app,
    const VoxelPilotSemanticRegion *region,
    int confirmed,
    int rejected)
{
    VoxelPilotSemanticCorrection correction;
    size_t total_voxels;
    int i;

    if (!app || !region) return 0;
    total_voxels = semantic_total_voxels(app);
    if (!voxelpilot_asd_capture_correction(region, total_voxels, app->semantic_edit_name, confirmed, rejected, &correction)) return 0;

    for (i = 0; i < app->semantic_correction_count; ++i) {
        if (voxelpilot_asd_region_matches_correction(region, total_voxels, &app->semantic_corrections[i])) {
            app->semantic_corrections[i] = correction;
            return 1;
        }
    }

    if (app->semantic_correction_count >= VOXELPILOT_ASD_MAX_CORRECTIONS) {
        snprintf(app->semantic_memory_status, sizeof(app->semantic_memory_status),
                 "ASD memory is full; save or prune before adding more corrections.");
        return 0;
    }
    app->semantic_corrections[app->semantic_correction_count++] = correction;
    return 1;
}

static int save_semantic_memory(StandaloneApp *app, const char *path)
{
    FILE *fp;
    int i;
    if (!app || !path || !path[0]) return 0;
    fp = fopen(path, "w");
    if (!fp) {
        snprintf(app->semantic_memory_status, sizeof(app->semantic_memory_status), "Could not save ASD memory: %s", path);
        return 0;
    }
    fprintf(fp, "{\n  \"version\": 1,\n  \"corrections\": [\n");
    for (i = 0; i < app->semantic_correction_count; ++i) {
        VoxelPilotSemanticCorrection *correction = &app->semantic_corrections[i];
        fprintf(fp, "    {\"name\": \"");
        voxelpilot_json_write_escaped(fp, correction->name);
        fprintf(fp, "\", \"archetype\": \"");
        voxelpilot_json_write_escaped(fp, correction->archetype);
        fprintf(fp, "\", \"densityClass\": %d, \"meanIntensity\": %.6f, \"minIntensity\": %.6f, \"maxIntensity\": %.6f, \"compactness\": %.6f, \"elongation\": %.6f, \"sizeRatio\": %.8f, \"confirmed\": %d, \"rejected\": %d}%s\n",
                (int)correction->density_class,
                correction->mean_intensity,
                correction->min_intensity,
                correction->max_intensity,
                correction->compactness,
                correction->elongation,
                correction->size_ratio,
                correction->confirmed,
                correction->rejected,
                (i + 1 < app->semantic_correction_count) ? "," : "");
    }
    fprintf(fp, "  ]\n}\n");
    fclose(fp);
    snprintf(app->semantic_memory_status, sizeof(app->semantic_memory_status),
             "Saved %d ASD correction(s) to %s.", app->semantic_correction_count, path);
    return 1;
}

static int load_semantic_memory(StandaloneApp *app, const char *path)
{
    FILE *fp;
    char line[1024];
    int loaded = 0;
    if (!app || !path || !path[0]) return 0;
    fp = fopen(path, "r");
    if (!fp) {
        snprintf(app->semantic_memory_status, sizeof(app->semantic_memory_status), "Could not load ASD memory: %s", path);
        return 0;
    }
    memset(app->semantic_corrections, 0, sizeof(app->semantic_corrections));
    app->semantic_correction_count = 0;
    while (fgets(line, sizeof(line), fp) && loaded < VOXELPILOT_ASD_MAX_CORRECTIONS) {
        VoxelPilotSemanticCorrection correction;
        int density_class = 0;
        if (!strstr(line, "\"name\"") || !strstr(line, "\"archetype\"")) continue;
        memset(&correction, 0, sizeof(correction));
        if (!voxelpilot_json_extract_string_field(line, "name", correction.name, sizeof(correction.name))) continue;
        voxelpilot_json_extract_string_field(line, "archetype", correction.archetype, sizeof(correction.archetype));
        voxelpilot_json_extract_int_field(line, "densityClass", &density_class);
        correction.density_class = (VoxelPilotDensityClass)density_class;
        voxelpilot_json_extract_float_field(line, "meanIntensity", &correction.mean_intensity);
        voxelpilot_json_extract_float_field(line, "minIntensity", &correction.min_intensity);
        voxelpilot_json_extract_float_field(line, "maxIntensity", &correction.max_intensity);
        voxelpilot_json_extract_float_field(line, "compactness", &correction.compactness);
        voxelpilot_json_extract_float_field(line, "elongation", &correction.elongation);
        voxelpilot_json_extract_float_field(line, "sizeRatio", &correction.size_ratio);
        voxelpilot_json_extract_int_field(line, "confirmed", &correction.confirmed);
        voxelpilot_json_extract_int_field(line, "rejected", &correction.rejected);
        app->semantic_corrections[loaded++] = correction;
    }
    fclose(fp);
    app->semantic_correction_count = loaded;
    snprintf(app->semantic_memory_status, sizeof(app->semantic_memory_status),
             "Loaded %d ASD correction(s) from %s.", loaded, path);
    if (app->semantic_region_count > 0) {
        voxelpilot_asd_apply_corrections(app->semantic_regions, app->semantic_region_count, semantic_total_voxels(app), app->semantic_corrections, app->semantic_correction_count);
        refresh_semantic_annotation_overlays(app);
    }
    return 1;
}


void create_annotation_region_at_point(
    StandaloneApp *app,
    const float    point[3])
{
    AnnotationRegion *region;
    size_t seed_index = 0;
    size_t count = 0;
    float min_value = 0.0f;
    float max_value = 0.0f;
    int seed_x = 0;
    int seed_y = 0;
    int seed_z = 0;
    int slot;
    unsigned char label_id;

    if (!ensure_label_mask(app)) {
        return;
    }

    if (!point_to_voxel_index(app, point, &seed_x, &seed_y, &seed_z, &seed_index)) {
        snprintf(app->annotation_status, sizeof(app->annotation_status),
                 "Could not place annotation on this volume.");
        return;
    }

    slot = app->selected_annotation;
    if (slot < 0 || slot >= MAX_ANNOTATIONS || !app->annotations[slot].active) {
        if (app->annotation_count >= MAX_ANNOTATIONS) {
            snprintf(app->annotation_status, sizeof(app->annotation_status),
                     "Maximum annotation count reached.");
            return;
        }
        slot = app->annotation_count;
        app->annotation_count++;
    }

    region = &app->annotations[slot];
    label_id = (unsigned char)(slot + 1);
    clear_label_from_mask(app, label_id);

    if (!voxelpilot_flood_fill_label_region(
            app->renderer.volume_data,
            app->renderer.W,
            app->renderer.H,
            app->renderer.D,
            seed_x,
            seed_y,
            seed_z,
            app->annotation_tolerance,
            label_id,
            app->label_mask,
            &count,
            &min_value,
            &max_value)) {
        snprintf(app->annotation_status, sizeof(app->annotation_status),
                 "Flood fill failed for annotation seed.");
        return;
    }

    memset(region, 0, sizeof(*region));
    region->active = 1;
    region->label_id = label_id;
    snprintf(region->name, sizeof(region->name),
             "%s",
             app->annotation_name[0] ? app->annotation_name : "Region");
    region->color[0] = app->annotation_color[0];
    region->color[1] = app->annotation_color[1];
    region->color[2] = app->annotation_color[2];
    region->seed[0] = point[0];
    region->seed[1] = point[1];
    region->seed[2] = point[2];
    region->seed_intensity = app->renderer.volume_data[seed_index];
    region->tolerance = app->annotation_tolerance;
    region->voxel_count = count;
    region->min_intensity = min_value;
    region->max_intensity = max_value;
    app->selected_annotation = slot;
    app->label_mask_revision++;

    snprintf(app->annotation_status, sizeof(app->annotation_status),
             "Annotated %s: %zu voxels around intensity %.3f.",
             region->name,
             region->voxel_count,
             region->seed_intensity);
}

static void export_annotation_mask(StandaloneApp *app)
{
    const char *patterns[] = { "*.raw" };
    const char *selected;
    char json_path[MAX_PATH_BUF + 16];
    FILE *fp;
    FILE *meta;
    size_t voxel_count;
    int i;

    if (!app->label_mask || app->annotation_count == 0) {
        snprintf(app->annotation_status, sizeof(app->annotation_status),
                 "Create at least one annotation before exporting.");
        return;
    }

    selected = tinyfd_saveFileDialog(
        "Export VoxelPilot Annotation Mask",
        "voxelpilot_annotation_mask.raw",
        1,
        patterns,
        "Raw Label Mask (*.raw)");

    if (!selected) {
        snprintf(app->annotation_status, sizeof(app->annotation_status),
                 "Annotation export canceled.");
        return;
    }

    voxel_count =
        (size_t)app->label_mask_W *
        (size_t)app->label_mask_H *
        (size_t)app->label_mask_D;

    fp = fopen(selected, "wb");
    if (!fp) {
        snprintf(app->annotation_status, sizeof(app->annotation_status),
                 "Could not write annotation mask.");
        return;
    }
    {
        size_t written = fwrite(app->label_mask, 1, voxel_count, fp);
        fclose(fp);
        if (written != voxel_count) {
            snprintf(app->annotation_status, sizeof(app->annotation_status),
                     "Export failed: disk write incomplete (%zu of %zu bytes).",
                     written, voxel_count);
            return;
        }
    }

    snprintf(json_path, sizeof(json_path), "%s.json", selected);
    meta = fopen(json_path, "w");
    if (meta) {
        fprintf(meta, "{\n");
        fprintf(meta, "  \"dimensions\": [%d, %d, %d],\n",
                app->label_mask_W,
                app->label_mask_H,
                app->label_mask_D);
        fprintf(meta, "  \"labels\": [\n");
        {
            int emitted = 0;
            for (i = 0; i < app->annotation_count; ++i) {
                AnnotationRegion *region = &app->annotations[i];
                VoxelPilotSemanticRegion *semantic = NULL;
                if (!region->active) continue;
                semantic = find_semantic_region_by_label(app, region->label_id);
                if (emitted > 0) fprintf(meta, ",\n");
                fprintf(meta,
                        "    {\"id\": %u, \"name\": \"",
                        (unsigned int)region->label_id);
                voxelpilot_json_write_escaped(meta, region->name);
                fprintf(meta,
                        "\", \"voxels\": %zu, \"seed\": [%.6f, %.6f, %.6f], \"tolerance\": %.6f, \"intensityRange\": [%.6f, %.6f]",
                        region->voxel_count,
                        region->seed[0],
                        region->seed[1],
                        region->seed[2],
                        region->tolerance,
                        region->min_intensity,
                        region->max_intensity);
                if (semantic) {
                    fprintf(meta,
                            ", \"semantic\": {\"source\": \"ASD-Lite heuristic\", \"confidence\": %.6f, \"stabilityScore\": %.6f, \"importanceScore\": %.6f, \"importanceRank\": %d, \"archetype\": \"",
                            semantic->confidence,
                            semantic->stability_score,
                            semantic->importance_score,
                            semantic->importance_rank);
                    voxelpilot_json_write_escaped(meta, semantic->archetype);
                    fprintf(meta, "\", \"description\": \"");
                    voxelpilot_json_write_escaped(meta, semantic->description);
                    fprintf(meta, "\"}");
                }
                fprintf(meta, "}");
                emitted++;
            }
            fprintf(meta, "\n  ],\n");
        }
        fprintf(meta, "  \"relationships\": [\n");
        {
            int emitted = 0;
            for (i = 0; i < app->semantic_relation_count; ++i) {
                VoxelPilotRegionRelation *relation = &app->semantic_relations[i];
                if (relation->region_a == 0) continue;
                if (emitted > 0) fprintf(meta, ",\n");
                fprintf(meta,
                        "    {\"regionA\": %u, \"regionB\": %u, \"type\": %d, \"centroidDistance\": %.6f, \"confidence\": %.6f, \"description\": \"",
                        (unsigned int)relation->region_a,
                        (unsigned int)relation->region_b,
                        (int)relation->type,
                        relation->centroid_distance,
                        relation->confidence);
                voxelpilot_json_write_escaped(meta, relation->description);
                fprintf(meta, "\"}");
                emitted++;
            }
            fprintf(meta, "\n  ]\n");
        }
        fprintf(meta, "}\n");
        fclose(meta);
    }

    snprintf(app->annotation_status, sizeof(app->annotation_status),
             "Exported annotation mask: %s", selected);
}


