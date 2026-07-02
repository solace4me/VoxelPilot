#ifndef VOXELPILOT_SEMANTIC_DISCOVERY_H
#define VOXELPILOT_SEMANTIC_DISCOVERY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VOXELPILOT_ASD_MAX_REGIONS 16
#define VOXELPILOT_ASD_MAX_RELATIONS 64
#define VOXELPILOT_ASD_MAX_CORRECTIONS 64
#define VOXELPILOT_ASD_NAME_MAX 64
#define VOXELPILOT_ASD_CATEGORY_MAX 64
#define VOXELPILOT_ASD_ARCHETYPE_MAX 32
#define VOXELPILOT_ASD_DESCRIPTION_MAX 256
#define VOXELPILOT_ASD_RELATION_DESCRIPTION_MAX 160

typedef enum {
    VOXELPILOT_SEMANTIC_SOURCE_HEURISTIC = 1,
    VOXELPILOT_SEMANTIC_SOURCE_USER_CONFIRMED = 2,
    VOXELPILOT_SEMANTIC_SOURCE_IMPORTED = 3,
    VOXELPILOT_SEMANTIC_SOURCE_MODEL = 4
} VoxelPilotSemanticSource;

typedef enum {
    VOXELPILOT_ASD_DENSITY_LOW = 1,
    VOXELPILOT_ASD_DENSITY_MID = 2,
    VOXELPILOT_ASD_DENSITY_HIGH = 3
} VoxelPilotDensityClass;

typedef struct {
    unsigned char label_id;
    char name[VOXELPILOT_ASD_NAME_MAX];
    char description[VOXELPILOT_ASD_DESCRIPTION_MAX];
    char category[VOXELPILOT_ASD_CATEGORY_MAX];
    VoxelPilotSemanticSource source;
    float confidence;
    float stability_score;
    float importance_score;
    char archetype[VOXELPILOT_ASD_ARCHETYPE_MAX];
    int importance_rank;
    size_t voxel_count;
    float centroid[3];
    int bbox_min[3];
    int bbox_max[3];
    float min_intensity;
    float max_intensity;
    float mean_intensity;
    float elongation;
    float compactness;
    float boundary_strength;
    float color[3];
    int user_confirmed;
    int rejected;
    VoxelPilotDensityClass density_class;
} VoxelPilotSemanticRegion;

typedef enum {
    VOXELPILOT_REGION_RELATION_ADJACENT = 1,
    VOXELPILOT_REGION_RELATION_CONTAINS = 2,
    VOXELPILOT_REGION_RELATION_INSIDE = 3,
    VOXELPILOT_REGION_RELATION_ISOLATED = 4
} VoxelPilotRegionRelationType;

typedef struct {
    unsigned char region_a;
    unsigned char region_b;
    VoxelPilotRegionRelationType type;
    float centroid_distance;
    float confidence;
    char description[VOXELPILOT_ASD_RELATION_DESCRIPTION_MAX];
} VoxelPilotRegionRelation;

typedef struct {
    char name[VOXELPILOT_ASD_NAME_MAX];
    char archetype[VOXELPILOT_ASD_ARCHETYPE_MAX];
    VoxelPilotDensityClass density_class;
    float mean_intensity;
    float min_intensity;
    float max_intensity;
    float compactness;
    float elongation;
    float size_ratio;
    int confirmed;
    int rejected;
    unsigned int use_count;
} VoxelPilotSemanticCorrection;

typedef struct {
    int region_index;
    unsigned char label_id;
    int step_number;
    int step_count;
    char title[96];
    char cue[256];
} VoxelPilotGuidedReviewStep;

static inline float voxelpilot_asd_clampf(float value, float min_value, float max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static inline void voxelpilot_asd_clear_regions(
    VoxelPilotSemanticRegion *regions,
    int max_regions,
    int *out_region_count)
{
    if (regions && max_regions > 0) {
        memset(regions, 0, sizeof(VoxelPilotSemanticRegion) * (size_t)max_regions);
    }
    if (out_region_count) {
        *out_region_count = 0;
    }
}

static inline VoxelPilotDensityClass voxelpilot_asd_density_class(float mean_intensity)
{
    if (mean_intensity < 0.25f) return VOXELPILOT_ASD_DENSITY_LOW;
    if (mean_intensity < 0.65f) return VOXELPILOT_ASD_DENSITY_MID;
    return VOXELPILOT_ASD_DENSITY_HIGH;
}

static inline void voxelpilot_asd_compute_scores(
    VoxelPilotSemanticRegion *region,
    size_t total_voxels)
{
    int extent_x;
    int extent_y;
    int extent_z;
    int max_extent;
    int min_extent;
    int bbox_volume;
    float size_score;

    if (!region || total_voxels == 0 || region->voxel_count == 0) {
        return;
    }

    extent_x = region->bbox_max[0] - region->bbox_min[0] + 1;
    extent_y = region->bbox_max[1] - region->bbox_min[1] + 1;
    extent_z = region->bbox_max[2] - region->bbox_min[2] + 1;
    if (extent_x < 1) extent_x = 1;
    if (extent_y < 1) extent_y = 1;
    if (extent_z < 1) extent_z = 1;

    max_extent = extent_x;
    if (extent_y > max_extent) max_extent = extent_y;
    if (extent_z > max_extent) max_extent = extent_z;

    min_extent = extent_x;
    if (extent_y < min_extent) min_extent = extent_y;
    if (extent_z < min_extent) min_extent = extent_z;
    if (min_extent < 1) min_extent = 1;

    bbox_volume = extent_x * extent_y * extent_z;
    if (bbox_volume < 1) bbox_volume = 1;

    region->density_class = voxelpilot_asd_density_class(region->mean_intensity);
    region->elongation = (float)max_extent / (float)min_extent;
    region->compactness = voxelpilot_asd_clampf((float)region->voxel_count / (float)bbox_volume, 0.0f, 1.0f);
    region->boundary_strength = voxelpilot_asd_clampf((region->mean_intensity - 0.35f) * 1.25f +
        (region->max_intensity - region->min_intensity) * 0.25f, 0.0f, 1.0f);
    region->stability_score = voxelpilot_asd_clampf(
        0.35f + region->boundary_strength * 0.35f + region->compactness * 0.20f,
        0.0f,
        1.0f);
    size_score = voxelpilot_asd_clampf(((float)region->voxel_count / (float)total_voxels) * 8.0f,
        0.0f,
        1.0f);
    region->importance_score = voxelpilot_asd_clampf(
        size_score * 0.25f + region->boundary_strength * 0.25f +
        region->stability_score * 0.25f + region->mean_intensity * 0.25f,
        0.0f,
        1.0f);
    region->confidence = voxelpilot_asd_clampf(
        0.30f + region->stability_score * 0.35f + region->compactness * 0.20f +
        region->boundary_strength * 0.10f,
        0.0f,
        0.95f);
}

static inline void voxelpilot_asd_assign_archetype(VoxelPilotSemanticRegion *region)
{
    int extent_x;
    int extent_y;
    int extent_z;
    int thin_axes = 0;
    int broad_axes = 0;
    int bbox_volume;

    if (!region) return;

    extent_x = region->bbox_max[0] - region->bbox_min[0] + 1;
    extent_y = region->bbox_max[1] - region->bbox_min[1] + 1;
    extent_z = region->bbox_max[2] - region->bbox_min[2] + 1;
    if (extent_x < 1) extent_x = 1;
    if (extent_y < 1) extent_y = 1;
    if (extent_z < 1) extent_z = 1;

    if (extent_x <= 2) thin_axes++; else if (extent_x >= 3) broad_axes++;
    if (extent_y <= 2) thin_axes++; else if (extent_y >= 3) broad_axes++;
    if (extent_z <= 2) thin_axes++; else if (extent_z >= 3) broad_axes++;

    bbox_volume = extent_x * extent_y * extent_z;

    if (region->elongation >= 2.4f && region->compactness >= 0.35f) {
        snprintf(region->archetype, sizeof(region->archetype), "rod");
    } else if (thin_axes == 1 && broad_axes >= 2) {
        snprintf(region->archetype, sizeof(region->archetype), "plate");
    } else if (region->density_class == VOXELPILOT_ASD_DENSITY_HIGH &&
            region->compactness < 0.45f && bbox_volume >= 16) {
        snprintf(region->archetype, sizeof(region->archetype), "shell");
    } else if (region->compactness >= 0.55f) {
        snprintf(region->archetype, sizeof(region->archetype), "blob");
    } else if (region->compactness >= 0.20f) {
        snprintf(region->archetype, sizeof(region->archetype), "cluster");
    } else {
        snprintf(region->archetype, sizeof(region->archetype), "unclassified");
    }
}

static inline void voxelpilot_asd_assign_color(VoxelPilotSemanticRegion *region)
{
    if (!region) return;

    if (strcmp(region->archetype, "rod") == 0) {
        region->color[0] = 0.95f; region->color[1] = 0.55f; region->color[2] = 0.15f;
    } else if (strcmp(region->archetype, "plate") == 0) {
        region->color[0] = 0.25f; region->color[1] = 0.75f; region->color[2] = 1.00f;
    } else if (strcmp(region->archetype, "shell") == 0) {
        region->color[0] = 0.95f; region->color[1] = 0.90f; region->color[2] = 0.20f;
    } else if (strcmp(region->archetype, "blob") == 0) {
        region->color[0] = 0.30f; region->color[1] = 1.00f; region->color[2] = 0.65f;
    } else if (strcmp(region->archetype, "cluster") == 0) {
        region->color[0] = 0.70f; region->color[1] = 0.50f; region->color[2] = 1.00f;
    } else {
        region->color[0] = 0.70f; region->color[1] = 0.70f; region->color[2] = 0.70f;
    }
}

static inline void voxelpilot_asd_assign_name_and_description(VoxelPilotSemanticRegion *region)
{
    const char *density = "mid-density";

    if (!region) return;

    if (region->density_class == VOXELPILOT_ASD_DENSITY_LOW) {
        density = "low-density";
        snprintf(region->category, sizeof(region->category), "low-density candidate");
    } else if (region->density_class == VOXELPILOT_ASD_DENSITY_HIGH) {
        density = "dense";
        snprintf(region->category, sizeof(region->category), "high-density candidate");
    } else {
        snprintf(region->category, sizeof(region->category), "mid-density candidate");
    }

    if (strcmp(region->archetype, "unclassified") == 0) {
        snprintf(region->name, sizeof(region->name), "%s candidate region", density);
    } else {
        snprintf(region->name, sizeof(region->name), "%s %s-like structure", density, region->archetype);
    }

    snprintf(region->description, sizeof(region->description),
        "ASD-Lite heuristic candidate grouped by connected voxels. It is described by density and shape only, not asserted as anatomy or diagnosis.");
}

static inline int voxelpilot_guided_review_is_reviewable(
    const VoxelPilotSemanticRegion *region)
{
    return region && region->label_id != 0 && !region->rejected;
}

static inline int voxelpilot_guided_review_rank_key(
    const VoxelPilotSemanticRegion *region,
    int fallback_index)
{
    if (!region) return fallback_index + 1;
    return region->importance_rank > 0 ? region->importance_rank : fallback_index + 1;
}

static inline int voxelpilot_guided_review_count_regions(
    const VoxelPilotSemanticRegion *regions,
    int region_count)
{
    int count = 0;
    int i;

    if (!regions || region_count < 1) return 0;
    for (i = 0; i < region_count; ++i) {
        if (voxelpilot_guided_review_is_reviewable(&regions[i])) {
            count++;
        }
    }
    return count;
}

static inline int voxelpilot_guided_review_select_region(
    const VoxelPilotSemanticRegion *regions,
    int region_count,
    int current_index,
    int direction,
    VoxelPilotGuidedReviewStep *step)
{
    int reviewable_count;
    int current_rank = 0;
    int best_index = -1;
    int best_rank = 0;
    int best_step_number = 0;
    int fallback_index = -1;
    int fallback_rank = 0;
    int fallback_step_number = 0;
    int i;

    if (!regions || region_count < 1 || !step) return 0;
    memset(step, 0, sizeof(*step));
    reviewable_count = voxelpilot_guided_review_count_regions(regions, region_count);
    if (reviewable_count < 1) return 0;

    if (current_index >= 0 && current_index < region_count &&
        voxelpilot_guided_review_is_reviewable(&regions[current_index])) {
        current_rank = voxelpilot_guided_review_rank_key(&regions[current_index], current_index);
    }

    if (direction < 0) {
        int step_number = 0;
        for (i = 0; i < region_count; ++i) {
            int rank;
            if (!voxelpilot_guided_review_is_reviewable(&regions[i])) continue;
            rank = voxelpilot_guided_review_rank_key(&regions[i], i);
            step_number++;
            if (fallback_index < 0 || rank > fallback_rank) {
                fallback_index = i;
                fallback_rank = rank;
                fallback_step_number = step_number;
            }
            if (current_rank > 0 && rank < current_rank &&
                (best_index < 0 || rank > best_rank)) {
                best_index = i;
                best_rank = rank;
                best_step_number = step_number;
            }
        }
    } else {
        int step_number = 0;
        for (i = 0; i < region_count; ++i) {
            int rank;
            if (!voxelpilot_guided_review_is_reviewable(&regions[i])) continue;
            rank = voxelpilot_guided_review_rank_key(&regions[i], i);
            step_number++;
            if (fallback_index < 0 || rank < fallback_rank) {
                fallback_index = i;
                fallback_rank = rank;
                fallback_step_number = step_number;
            }
            if (current_rank > 0 && rank > current_rank &&
                (best_index < 0 || rank < best_rank)) {
                best_index = i;
                best_rank = rank;
                best_step_number = step_number;
            }
        }
    }

    if (best_index < 0) {
        best_index = fallback_index;
        best_rank = fallback_rank;
        best_step_number = fallback_step_number;
    }

    if (best_index < 0) return 0;

    best_step_number = 0;
    for (i = 0; i < region_count; ++i) {
        int rank;
        if (!voxelpilot_guided_review_is_reviewable(&regions[i])) continue;
        rank = voxelpilot_guided_review_rank_key(&regions[i], i);
        if (rank <= best_rank) {
            best_step_number++;
        }
    }
    if (best_step_number < 1) best_step_number = 1;
    if (best_step_number > reviewable_count) best_step_number = reviewable_count;

    step->region_index = best_index;
    step->label_id = regions[best_index].label_id;
    step->step_count = reviewable_count;
    step->step_number = best_step_number;
    snprintf(step->title, sizeof(step->title),
             "Region %d/%d: %s",
             step->step_number,
             step->step_count,
             regions[best_index].name[0] ? regions[best_index].name : "ASD candidate region");
    snprintf(step->cue, sizeof(step->cue),
             "Review %s %s with %.0f%% confidence, %.0f%% stability, and importance rank %d. Treat this as a cautious local AI cue, not a diagnosis.",
             regions[best_index].archetype[0] ? regions[best_index].archetype : "unclassified",
             regions[best_index].category[0] ? regions[best_index].category : "candidate region",
             regions[best_index].confidence * 100.0f,
             regions[best_index].stability_score * 100.0f,
             best_rank);
    return 1;
}

static inline float voxelpilot_asd_size_ratio(
    const VoxelPilotSemanticRegion *region,
    size_t total_voxels)
{
    if (!region || total_voxels == 0) return 0.0f;
    return voxelpilot_asd_clampf((float)region->voxel_count / (float)total_voxels, 0.0f, 1.0f);
}

static inline int voxelpilot_asd_capture_correction(
    const VoxelPilotSemanticRegion *region,
    size_t total_voxels,
    const char *custom_name,
    int confirmed,
    int rejected,
    VoxelPilotSemanticCorrection *out_correction)
{
    const char *name;

    if (!region || !out_correction) return 0;

    memset(out_correction, 0, sizeof(*out_correction));
    name = (custom_name && custom_name[0]) ? custom_name : region->name;
    snprintf(out_correction->name, sizeof(out_correction->name), "%s", name && name[0] ? name : "Confirmed region");
    snprintf(out_correction->archetype, sizeof(out_correction->archetype), "%s", region->archetype);
    out_correction->density_class = region->density_class;
    out_correction->mean_intensity = region->mean_intensity;
    out_correction->min_intensity = region->min_intensity;
    out_correction->max_intensity = region->max_intensity;
    out_correction->compactness = region->compactness;
    out_correction->elongation = region->elongation;
    out_correction->size_ratio = voxelpilot_asd_size_ratio(region, total_voxels);
    out_correction->confirmed = confirmed ? 1 : 0;
    out_correction->rejected = rejected ? 1 : 0;
    out_correction->use_count = 0;
    if (!out_correction->confirmed && !out_correction->rejected) {
        out_correction->confirmed = 1;
    }
    return 1;
}

static inline int voxelpilot_asd_region_matches_correction(
    const VoxelPilotSemanticRegion *region,
    size_t total_voxels,
    const VoxelPilotSemanticCorrection *correction)
{
    float region_size_ratio;
    float size_tolerance;
    float elongation_delta;

    if (!region || !correction || !correction->name[0]) return 0;
    if (correction->archetype[0] && strcmp(region->archetype, correction->archetype) != 0) return 0;
    if (correction->density_class != 0 && region->density_class != correction->density_class) return 0;
    if (fabsf(region->mean_intensity - correction->mean_intensity) > 0.12f) return 0;
    if (fabsf(region->compactness - correction->compactness) > 0.35f) return 0;

    elongation_delta = fabsf(region->elongation - correction->elongation);
    if (elongation_delta > fmaxf(1.25f, correction->elongation * 0.45f)) return 0;

    region_size_ratio = voxelpilot_asd_size_ratio(region, total_voxels);
    size_tolerance = fmaxf(0.02f, correction->size_ratio * 1.50f);
    if (fabsf(region_size_ratio - correction->size_ratio) > size_tolerance) return 0;

    return 1;
}

static inline void voxelpilot_asd_apply_correction(
    VoxelPilotSemanticRegion *region,
    VoxelPilotSemanticCorrection *correction)
{
    if (!region || !correction) return;

    if (correction->rejected) {
        region->rejected = 1;
        region->user_confirmed = 0;
        region->source = VOXELPILOT_SEMANTIC_SOURCE_USER_CONFIRMED;
        region->confidence = 0.0f;
        snprintf(region->name, sizeof(region->name), "%s", correction->name[0] ? correction->name : "Rejected candidate region");
        snprintf(region->description, sizeof(region->description),
            "User rejected this ASD-Lite match from local correction memory; it is hidden from semantic overlays.");
    } else {
        region->rejected = 0;
        region->user_confirmed = 1;
        region->source = VOXELPILOT_SEMANTIC_SOURCE_USER_CONFIRMED;
        if (region->confidence < 0.95f) region->confidence = 0.95f;
        snprintf(region->name, sizeof(region->name), "%s", correction->name[0] ? correction->name : region->name);
        snprintf(region->description, sizeof(region->description),
            "User-confirmed ASD-Lite correction from local memory. Original match used density, archetype, intensity, compactness, elongation, and size ratio.");
    }
    correction->use_count++;
}

static inline int voxelpilot_asd_apply_corrections(
    VoxelPilotSemanticRegion *regions,
    int region_count,
    size_t total_voxels,
    VoxelPilotSemanticCorrection *corrections,
    int correction_count)
{
    int applied = 0;
    int i;

    if (!regions || region_count < 1 || !corrections || correction_count < 1) return 0;
    if (correction_count > VOXELPILOT_ASD_MAX_CORRECTIONS) correction_count = VOXELPILOT_ASD_MAX_CORRECTIONS;

    for (i = 0; i < region_count; ++i) {
        int j;
        for (j = 0; j < correction_count; ++j) {
            if (voxelpilot_asd_region_matches_correction(&regions[i], total_voxels, &corrections[j])) {
                voxelpilot_asd_apply_correction(&regions[i], &corrections[j]);
                applied++;
                break;
            }
        }
    }

    return applied;
}

static inline void voxelpilot_asd_rank_regions(VoxelPilotSemanticRegion *regions, int region_count)
{
    int rank;
    int i;

    if (!regions || region_count < 1) return;

    for (i = 0; i < region_count; ++i) {
        regions[i].importance_rank = 0;
    }

    for (rank = 1; rank <= region_count; ++rank) {
        int best_index = -1;
        float best_score = -1.0f;
        for (i = 0; i < region_count; ++i) {
            if (regions[i].importance_rank == 0 && regions[i].importance_score > best_score) {
                best_score = regions[i].importance_score;
                best_index = i;
            }
        }
        if (best_index >= 0) {
            regions[best_index].importance_rank = rank;
        }
    }
}

static inline int voxelpilot_asd_range_overlaps(int min_a, int max_a, int min_b, int max_b)
{
    return min_a <= max_b && min_b <= max_a;
}

static inline int voxelpilot_asd_bbox_contains(
    const VoxelPilotSemanticRegion *outer,
    const VoxelPilotSemanticRegion *inner)
{
    return outer->bbox_min[0] <= inner->bbox_min[0] && outer->bbox_max[0] >= inner->bbox_max[0] &&
           outer->bbox_min[1] <= inner->bbox_min[1] && outer->bbox_max[1] >= inner->bbox_max[1] &&
           outer->bbox_min[2] <= inner->bbox_min[2] && outer->bbox_max[2] >= inner->bbox_max[2];
}

static inline int voxelpilot_asd_axis_gap(int min_a, int max_a, int min_b, int max_b)
{
    if (max_a < min_b) return min_b - max_a;
    if (max_b < min_a) return min_a - max_b;
    return 0;
}

static inline int voxelpilot_asd_bboxes_adjacent(
    const VoxelPilotSemanticRegion *a,
    const VoxelPilotSemanticRegion *b)
{
    int gap_x = voxelpilot_asd_axis_gap(a->bbox_min[0], a->bbox_max[0], b->bbox_min[0], b->bbox_max[0]);
    int gap_y = voxelpilot_asd_axis_gap(a->bbox_min[1], a->bbox_max[1], b->bbox_min[1], b->bbox_max[1]);
    int gap_z = voxelpilot_asd_axis_gap(a->bbox_min[2], a->bbox_max[2], b->bbox_min[2], b->bbox_max[2]);
    int overlap_x = voxelpilot_asd_range_overlaps(a->bbox_min[0], a->bbox_max[0], b->bbox_min[0], b->bbox_max[0]);
    int overlap_y = voxelpilot_asd_range_overlaps(a->bbox_min[1], a->bbox_max[1], b->bbox_min[1], b->bbox_max[1]);
    int overlap_z = voxelpilot_asd_range_overlaps(a->bbox_min[2], a->bbox_max[2], b->bbox_min[2], b->bbox_max[2]);

    return (gap_x > 0 && gap_x <= 1 && overlap_y && overlap_z) ||
           (gap_y > 0 && gap_y <= 1 && overlap_x && overlap_z) ||
           (gap_z > 0 && gap_z <= 1 && overlap_x && overlap_y);
}

static inline float voxelpilot_asd_centroid_distance(
    const VoxelPilotSemanticRegion *a,
    const VoxelPilotSemanticRegion *b)
{
    float dx = a->centroid[0] - b->centroid[0];
    float dy = a->centroid[1] - b->centroid[1];
    float dz = a->centroid[2] - b->centroid[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static inline void voxelpilot_asd_write_relation(
    VoxelPilotRegionRelation *relation,
    const VoxelPilotSemanticRegion *a,
    const VoxelPilotSemanticRegion *b,
    VoxelPilotRegionRelationType type,
    const char *verb)
{
    if (!relation || !a || !b || !verb) return;
    memset(relation, 0, sizeof(*relation));
    relation->region_a = a->label_id;
    relation->region_b = b->label_id;
    relation->type = type;
    relation->centroid_distance = voxelpilot_asd_centroid_distance(a, b);
    relation->confidence = 0.65f;
    snprintf(relation->description, sizeof(relation->description),
        "Region %u %s region %u; centroid distance %.2f voxels.",
        (unsigned int)a->label_id,
        verb,
        (unsigned int)b->label_id,
        relation->centroid_distance);
}

static inline int voxelpilot_compute_region_relationships(
    const VoxelPilotSemanticRegion *regions,
    int region_count,
    VoxelPilotRegionRelation *relations,
    int max_relations,
    int *out_relation_count)
{
    int relation_count = 0;
    int i;
    int has_relation[VOXELPILOT_ASD_MAX_REGIONS];

    if (out_relation_count) {
        *out_relation_count = 0;
    }
    if (!regions || !relations || !out_relation_count || region_count < 1 || max_relations < 1) {
        return 0;
    }

    if (region_count > VOXELPILOT_ASD_MAX_REGIONS) {
        region_count = VOXELPILOT_ASD_MAX_REGIONS;
    }
    if (max_relations > VOXELPILOT_ASD_MAX_RELATIONS) {
        max_relations = VOXELPILOT_ASD_MAX_RELATIONS;
    }

    memset(relations, 0, sizeof(VoxelPilotRegionRelation) * (size_t)max_relations);
    memset(has_relation, 0, sizeof(has_relation));

    for (i = 0; i < region_count && relation_count < max_relations; ++i) {
        int j;
        for (j = i + 1; j < region_count && relation_count < max_relations; ++j) {
            VoxelPilotRegionRelationType type = VOXELPILOT_REGION_RELATION_ISOLATED;
            const char *verb = "is separated from";
            int related = 0;

            if (voxelpilot_asd_bbox_contains(&regions[i], &regions[j])) {
                type = VOXELPILOT_REGION_RELATION_CONTAINS;
                verb = "contains";
                related = 1;
            } else if (voxelpilot_asd_bbox_contains(&regions[j], &regions[i])) {
                type = VOXELPILOT_REGION_RELATION_INSIDE;
                verb = "is inside";
                related = 1;
            } else if (voxelpilot_asd_bboxes_adjacent(&regions[i], &regions[j])) {
                type = VOXELPILOT_REGION_RELATION_ADJACENT;
                verb = "is adjacent to";
                related = 1;
            }

            if (related) {
                voxelpilot_asd_write_relation(&relations[relation_count], &regions[i], &regions[j], type, verb);
                has_relation[i] = 1;
                has_relation[j] = 1;
                relation_count++;
            }
        }
    }

    for (i = 0; i < region_count && relation_count < max_relations; ++i) {
        if (!has_relation[i]) {
            memset(&relations[relation_count], 0, sizeof(relations[relation_count]));
            relations[relation_count].region_a = regions[i].label_id;
            relations[relation_count].type = VOXELPILOT_REGION_RELATION_ISOLATED;
            relations[relation_count].confidence = 0.50f;
            snprintf(relations[relation_count].description, sizeof(relations[relation_count].description),
                "Region %u is isolated from other discovered regions in the current ASD pass.",
                (unsigned int)regions[i].label_id);
            relation_count++;
        }
    }

    *out_relation_count = relation_count;
    return 1;
}

static inline int voxelpilot_discover_semantic_regions(
    const float *volume_data,
    int width,
    int height,
    int depth,
    const float *histogram,
    int histogram_bins,
    int max_regions,
    float sensitivity,
    size_t min_region_voxels,
    unsigned char *label_mask,
    VoxelPilotSemanticRegion *regions,
    int *out_region_count)
{
    size_t voxel_count;
    int region_count = 0;
    float threshold;
    int clamped_max_regions;
    int *queue;
    unsigned char *visited;
    size_t start_index;
    int xy_stride;

    (void)histogram;
    (void)histogram_bins;

    voxelpilot_asd_clear_regions(regions, max_regions, out_region_count);

    if (!volume_data || !label_mask || !regions || !out_region_count ||
        width < 1 || height < 1 || depth < 1 || max_regions < 1) {
        return 0;
    }

    voxel_count = (size_t)width * (size_t)height * (size_t)depth;
    if (voxel_count == 0 || voxel_count > (size_t)2147483647) {
        return 0;
    }

    memset(label_mask, 0, voxel_count);

    clamped_max_regions = max_regions;
    if (clamped_max_regions > VOXELPILOT_ASD_MAX_REGIONS) {
        clamped_max_regions = VOXELPILOT_ASD_MAX_REGIONS;
    }
    if (clamped_max_regions > 255) {
        clamped_max_regions = 255;
    }

    if (min_region_voxels < 1) {
        min_region_voxels = 1;
    }

    threshold = voxelpilot_asd_clampf(0.70f - sensitivity * 0.25f, 0.35f, 0.85f);
    queue = (int *)malloc(sizeof(int) * voxel_count);
    visited = (unsigned char *)calloc(voxel_count, sizeof(unsigned char));
    if (!queue || !visited) {
        free(queue);
        free(visited);
        memset(label_mask, 0, voxel_count);
        voxelpilot_asd_clear_regions(regions, max_regions, out_region_count);
        return 0;
    }

    xy_stride = width * height;

    for (start_index = 0; start_index < voxel_count && region_count < clamped_max_regions; ++start_index) {
        int start = (int)start_index;
        int head = 0;
        int tail = 0;
        size_t component_voxels = 0;
        double sum_intensity = 0.0;
        double sum_x = 0.0;
        double sum_y = 0.0;
        double sum_z = 0.0;
        float min_intensity = 1.0f;
        float max_intensity = 0.0f;
        int bbox_min[3] = { width, height, depth };
        int bbox_max[3] = { 0, 0, 0 };

        if (visited[start] || volume_data[start] < threshold) {
            continue;
        }

        visited[start] = 1;
        queue[tail++] = start;

        while (head < tail) {
            int index = queue[head++];
            int z = index / xy_stride;
            int rem = index - z * xy_stride;
            int y = rem / width;
            int x = rem - y * width;
            float value = volume_data[index];
            int neighbors[6];
            int neighbor_count = 0;
            int n;

            component_voxels++;
            sum_intensity += (double)value;
            sum_x += (double)x;
            sum_y += (double)y;
            sum_z += (double)z;
            if (value < min_intensity) min_intensity = value;
            if (value > max_intensity) max_intensity = value;
            if (x < bbox_min[0]) bbox_min[0] = x;
            if (y < bbox_min[1]) bbox_min[1] = y;
            if (z < bbox_min[2]) bbox_min[2] = z;
            if (x > bbox_max[0]) bbox_max[0] = x;
            if (y > bbox_max[1]) bbox_max[1] = y;
            if (z > bbox_max[2]) bbox_max[2] = z;

            if (x > 0) neighbors[neighbor_count++] = index - 1;
            if (x + 1 < width) neighbors[neighbor_count++] = index + 1;
            if (y > 0) neighbors[neighbor_count++] = index - width;
            if (y + 1 < height) neighbors[neighbor_count++] = index + width;
            if (z > 0) neighbors[neighbor_count++] = index - xy_stride;
            if (z + 1 < depth) neighbors[neighbor_count++] = index + xy_stride;

            for (n = 0; n < neighbor_count; ++n) {
                int neighbor = neighbors[n];
                if (!visited[neighbor] && volume_data[neighbor] >= threshold) {
                    visited[neighbor] = 1;
                    queue[tail++] = neighbor;
                }
            }
        }

        if (component_voxels >= min_region_voxels) {
            unsigned char label_id = (unsigned char)(region_count + 1);
            VoxelPilotSemanticRegion *region = &regions[region_count];
            int i;

            for (i = 0; i < tail; ++i) {
                label_mask[queue[i]] = label_id;
            }

            memset(region, 0, sizeof(*region));
            region->label_id = label_id;
            region->source = VOXELPILOT_SEMANTIC_SOURCE_HEURISTIC;
            region->confidence = 0.50f;
            region->voxel_count = component_voxels;
            region->centroid[0] = (float)(sum_x / (double)component_voxels);
            region->centroid[1] = (float)(sum_y / (double)component_voxels);
            region->centroid[2] = (float)(sum_z / (double)component_voxels);
            region->bbox_min[0] = bbox_min[0];
            region->bbox_min[1] = bbox_min[1];
            region->bbox_min[2] = bbox_min[2];
            region->bbox_max[0] = bbox_max[0];
            region->bbox_max[1] = bbox_max[1];
            region->bbox_max[2] = bbox_max[2];
            region->min_intensity = min_intensity;
            region->max_intensity = max_intensity;
            region->mean_intensity = (float)(sum_intensity / (double)component_voxels);
            voxelpilot_asd_compute_scores(region, voxel_count);
            voxelpilot_asd_assign_archetype(region);
            voxelpilot_asd_assign_color(region);
            voxelpilot_asd_assign_name_and_description(region);
            region_count++;
        }
    }

    free(queue);
    free(visited);
    voxelpilot_asd_rank_regions(regions, region_count);
    *out_region_count = region_count;
    return region_count > 0 ? 1 : 0;
}

#ifdef __cplusplus
}
#endif

#endif
