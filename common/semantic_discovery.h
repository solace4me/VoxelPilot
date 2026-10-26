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
    /* Principal-axis shape (ASD v2), from the voxel coordinates'
       covariance in physical units: flatness = middle / smallest spread,
       fill_ratio = region volume / volume of its equivalent ellipsoid
       (about 1 for a solid lump, low for branching or hollow shapes).
       elongation (above) = largest / middle spread. 0 = not computed. */
    float flatness;
    float fill_ratio;
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

/* spacing: physical voxel size per axis, NULL = isotropic. Elongation is
   measured on the physical bounding box, so a region on thick slices is
   not mistaken for a flat one (or a long one for a cube). Compactness is
   a voxel ratio and needs no spacing. */
static inline void voxelpilot_asd_compute_scores(
    VoxelPilotSemanticRegion *region,
    size_t total_voxels,
    const float *spacing)
{
    int extent_x;
    int extent_y;
    int extent_z;
    float phys[3];
    float max_extent;
    float min_extent;
    int bbox_volume;
    float size_score;
    int axis;

    if (!region || total_voxels == 0 || region->voxel_count == 0) {
        return;
    }

    extent_x = region->bbox_max[0] - region->bbox_min[0] + 1;
    extent_y = region->bbox_max[1] - region->bbox_min[1] + 1;
    extent_z = region->bbox_max[2] - region->bbox_min[2] + 1;
    if (extent_x < 1) extent_x = 1;
    if (extent_y < 1) extent_y = 1;
    if (extent_z < 1) extent_z = 1;

    phys[0] = (float)extent_x;
    phys[1] = (float)extent_y;
    phys[2] = (float)extent_z;
    for (axis = 0; axis < 3; ++axis) {
        if (spacing && spacing[axis] > 0.0f) phys[axis] *= spacing[axis];
    }
    max_extent = phys[0];
    min_extent = phys[0];
    for (axis = 1; axis < 3; ++axis) {
        if (phys[axis] > max_extent) max_extent = phys[axis];
        if (phys[axis] < min_extent) min_extent = phys[axis];
    }
    if (min_extent <= 0.0f) min_extent = 1.0f;

    bbox_volume = extent_x * extent_y * extent_z;
    if (bbox_volume < 1) bbox_volume = 1;

    region->density_class = voxelpilot_asd_density_class(region->mean_intensity);
    region->elongation = max_extent / min_extent;
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

/* Eigenvalues of a symmetric 3x3 matrix (cyclic Jacobi), largest first. */
static inline void voxelpilot_asd_symmetric_eigenvalues(const double m_in[3][3], double out[3])
{
    double m[3][3];
    int sweep, p, q, i, j;

    for (i = 0; i < 3; ++i) for (j = 0; j < 3; ++j) m[i][j] = m_in[i][j];
    for (sweep = 0; sweep < 32; ++sweep) {
        double off = fabs(m[0][1]) + fabs(m[0][2]) + fabs(m[1][2]);
        if (off < 1e-12) break;
        for (p = 0; p < 2; ++p) {
            for (q = p + 1; q < 3; ++q) {
                double theta, t, c, s, tau;
                double mpq = m[p][q];
                if (fabs(mpq) < 1e-15) continue;
                theta = (m[q][q] - m[p][p]) / (2.0 * mpq);
                t = (theta >= 0.0 ? 1.0 : -1.0) / (fabs(theta) + sqrt(theta * theta + 1.0));
                c = 1.0 / sqrt(t * t + 1.0);
                s = t * c;
                tau = s / (1.0 + c);
                m[p][p] -= t * mpq;
                m[q][q] += t * mpq;
                m[p][q] = m[q][p] = 0.0;
                for (i = 0; i < 3; ++i) {
                    if (i != p && i != q) {
                        double mip = m[i][p], miq = m[i][q];
                        m[i][p] = m[p][i] = mip - s * (miq + tau * mip);
                        m[i][q] = m[q][i] = miq + s * (mip - tau * miq);
                    }
                }
            }
        }
    }
    out[0] = m[0][0];
    out[1] = m[1][1];
    out[2] = m[2][2];
    for (i = 0; i < 2; ++i) {
        for (j = i + 1; j < 3; ++j) {
            if (out[j] > out[i]) { double tmp = out[i]; out[i] = out[j]; out[j] = tmp; }
        }
    }
}

/* Principal-axis shape from the sums of voxel coordinates (physical
   units). Each voxel is a box, not a point, so its own spread (size^2/12
   per axis) is added - a one-voxel-thick line is thin, not infinitely
   thin. Orientation-independent, unlike the old bounding-box measures,
   which called any oblique or curved dense object a "shell". */
static inline void voxelpilot_asd_principal_shape(
    VoxelPilotSemanticRegion *region,
    double n,
    const double sum[3],
    const double sum_sq[3][3],
    const float *spacing)
{
    double cov[3][3];
    double ev[3];
    double sigma[3];
    double voxel_volume = 1.0;
    double ellipsoid_volume;
    int i, j;

    if (!region || n < 1.0) return;
    for (i = 0; i < 3; ++i) {
        double s = (spacing && spacing[i] > 0.0f) ? (double)spacing[i] : 1.0;
        voxel_volume *= s;
        for (j = 0; j < 3; ++j) {
            cov[i][j] = sum_sq[i][j] / n - (sum[i] / n) * (sum[j] / n);
        }
        cov[i][i] += s * s / 12.0;
    }
    voxelpilot_asd_symmetric_eigenvalues(cov, ev);
    for (i = 0; i < 3; ++i) sigma[i] = sqrt(ev[i] > 1e-12 ? ev[i] : 1e-12);

    /* A solid ellipsoid with semi-axes r has variance r^2 / 5 along each. */
    ellipsoid_volume = 4.0 / 3.0 * 3.14159265358979 * (sqrt(5.0) * sigma[0]) *
                       (sqrt(5.0) * sigma[1]) * (sqrt(5.0) * sigma[2]);
    region->elongation = (float)(sigma[0] / sigma[1]);
    region->flatness = (float)(sigma[1] / sigma[2]);
    region->fill_ratio = (float)voxelpilot_asd_clampf(
        (float)(n * voxel_volume / (ellipsoid_volume > 1e-12 ? ellipsoid_volume : 1e-12)), 0.0f, 2.0f);
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

    /* Principal-axis rules when the shape was measured (ASD v2). */
    if (region->fill_ratio > 0.0f) {
        if (region->fill_ratio < 0.30f) {
            snprintf(region->archetype, sizeof(region->archetype), "branching");
        } else if (region->elongation >= 2.5f) {
            snprintf(region->archetype, sizeof(region->archetype), "rod");
        } else if (region->flatness >= 2.5f) {
            snprintf(region->archetype, sizeof(region->archetype), "plate");
        } else {
            snprintf(region->archetype, sizeof(region->archetype), "blob");
        }
        return;
    }

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
    } else if (strcmp(region->archetype, "branching") == 0) {
        region->color[0] = 0.95f; region->color[1] = 0.45f; region->color[2] = 0.55f;
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

/* Where a region sits, in the scan's own axes: x = left/right, y =
   top/bottom (image rows), z = front/back (slice order). These are
   directions in the data, not anatomy - the scan's orientation is not
   known. Only axes clearly off-centre (outer thirds) are named. */
static inline void voxelpilot_asd_location_words(
    const VoxelPilotSemanticRegion *region,
    int width,
    int height,
    int depth,
    char *out,
    size_t out_size)
{
    static const char *low_words[3] = { "left", "top", "front" };
    static const char *high_words[3] = { "right", "bottom", "back" };
    const int dims[3] = { width, height, depth };
    int axis;
    size_t used = 0;

    if (!out || out_size == 0) return;
    out[0] = '\0';
    for (axis = 0; axis < 3; ++axis) {
        float span = (float)(dims[axis] > 1 ? dims[axis] - 1 : 1);
        float f = region->centroid[axis] / span;
        const char *word = NULL;
        if (f < 0.33f) word = low_words[axis];
        else if (f > 0.67f) word = high_words[axis];
        if (word) {
            int n = snprintf(out + used, out_size - used, "%s%s", used ? ", " : "", word);
            if (n < 0 || (size_t)n >= out_size - used) break;
            used += (size_t)n;
        }
    }
    if (!out[0]) {
        snprintf(out, out_size, "center");
    }
}

/* "Large dense rod · right, back": size relative to all material found,
   the material class, the shape, and the position - the parts that tell
   regions apart. Category and description stay deliberately cautious:
   ASD-Lite groups voxels; it does not identify anatomy. */
static inline void voxelpilot_asd_name_region(
    VoxelPilotSemanticRegion *region,
    int width,
    int height,
    int depth,
    size_t material_voxels,
    float lower_threshold,
    float upper_threshold)
{
    const char *size_word = "Small";
    const char *material = "mid-density";
    const char *shape;
    char location[48];
    double share = material_voxels ? (double)region->voxel_count / (double)material_voxels : 0.0;

    if (!region) return;

    if (share >= 0.25) size_word = "Large";
    else if (share >= 0.05) size_word = "Medium";

    if (region->density_class == VOXELPILOT_ASD_DENSITY_LOW) {
        material = "low-density";
        snprintf(region->category, sizeof(region->category), "low-density candidate");
    } else if (region->density_class == VOXELPILOT_ASD_DENSITY_HIGH) {
        material = "dense";
        snprintf(region->category, sizeof(region->category), "high-density candidate");
    } else {
        snprintf(region->category, sizeof(region->category), "mid-density candidate");
    }

    shape = strcmp(region->archetype, "unclassified") == 0 ? "region" : region->archetype;
    voxelpilot_asd_location_words(region, width, height, depth, location, sizeof(location));
    snprintf(region->name, sizeof(region->name), "%s %s %s \xC2\xB7 %s", size_word, material, shape, location);

    if (upper_threshold > lower_threshold) {
        snprintf(region->description, sizeof(region->description),
            "ASD-Lite candidate: %zu connected voxels of the %s material (values %.2f-%.2f, "
            "class limits found from this scan). Described by density, shape and position only - "
            "not identified as anatomy.",
            region->voxel_count, material, region->min_intensity, region->max_intensity);
    } else {
        snprintf(region->description, sizeof(region->description),
            "ASD-Lite candidate: %zu connected voxels above %.2f. Described by density, shape and "
            "position only - not identified as anatomy.",
            region->voxel_count, lower_threshold);
    }
}

/* Two regions can still match on every part of the name (same size band,
   material, shape and third of the scan): number the repeats. */
static inline void voxelpilot_asd_make_names_unique(VoxelPilotSemanticRegion *regions, int count)
{
    char original[VOXELPILOT_ASD_MAX_REGIONS][VOXELPILOT_ASD_NAME_MAX];
    int i, j;

    if (!regions || count < 2) return;
    if (count > VOXELPILOT_ASD_MAX_REGIONS) count = VOXELPILOT_ASD_MAX_REGIONS;
    for (i = 0; i < count; ++i) {
        snprintf(original[i], sizeof(original[i]), "%s", regions[i].name);
    }
    /* Compare against the names as generated, so a third repeat becomes
       "#3" rather than matching the already-renamed "#2". */
    for (i = 1; i < count; ++i) {
        int same = 0;
        for (j = 0; j < i; ++j) {
            if (strcmp(original[i], original[j]) == 0) same++;
        }
        if (same > 0) {
            char base[VOXELPILOT_ASD_NAME_MAX];
            snprintf(base, sizeof(base), "%s", original[i]);
            base[VOXELPILOT_ASD_NAME_MAX - 6] = '\0';
            snprintf(regions[i].name, sizeof(regions[i].name), "%s #%d", base, same + 1);
        }
    }
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

/* ============================================================
   ASD-Lite v2: discovery

   1. Material classes. With adaptive thresholds the scan's own value
      distribution is split twice with Otsu's method: background vs
      material, then (inside the material) mid-density vs dense. Regions
      are connected components within one class, so e.g. soft tissue and
      bone come out as separate regions instead of one band above a fixed
      cut-off. Sensitivity moves both limits (higher = lower limits).
      Without adaptive thresholds the old single cut-off is used
      (0.70 - 0.25 * sensitivity), as the original ASD-Lite did.
   2. Most important first. Pass 1 measures every component (keeping the
      best VOXELPILOT_ASD_MAX_CANDIDATES by importance); the top
      max_regions are kept and only they are written to the label mask
      in pass 2. The old version stopped at the first max_regions
      components in voxel scan order, so large structures further into
      the scan were never considered.
   3. Shape on the physical bounding box (spacing), names that tell
      regions apart (size, material, shape, position), numbered if they
      still collide.
   ============================================================ */

#define VOXELPILOT_ASD_MAX_CANDIDATES 512
#define VOXELPILOT_ASD_THRESHOLD_BINS 256

typedef struct {
    VoxelPilotSemanticRegion region;
    int seed;
    int voxel_class;
} VoxelPilotAsdCandidate;

/* Otsu split of hist[lo..hi]: the first bin of the upper class, or -1. */
static inline int voxelpilot_asd_otsu_split(const double *hist, int lo, int hi)
{
    double total = 0.0, sum = 0.0, weight_low = 0.0, sum_low = 0.0, best = -1.0;
    int b, best_bin = -1;

    for (b = lo; b <= hi; ++b) {
        total += hist[b];
        sum += (double)b * hist[b];
    }
    if (total <= 0.0) return -1;

    for (b = lo; b < hi; ++b) {
        double weight_high, mean_low, mean_high, between;
        weight_low += hist[b];
        sum_low += (double)b * hist[b];
        if (weight_low <= 0.0) continue;
        weight_high = total - weight_low;
        if (weight_high <= 0.0) break;
        mean_low = sum_low / weight_low;
        mean_high = (sum - sum_low) / weight_high;
        between = weight_low * weight_high * (mean_low - mean_high) * (mean_low - mean_high);
        if (between > best) {
            best = between;
            best_bin = b + 1;
        }
    }
    return best_bin;
}

/* Lower limit (background | material) and upper limit (mid | dense).
   Returns 0 when the scan has no value spread to split. If the material
   has no second split, upper == lower and all material is one class. */
static inline int voxelpilot_asd_material_thresholds(
    const float *volume_data,
    size_t voxel_count,
    float sensitivity,
    float *out_lower,
    float *out_upper)
{
    double hist[VOXELPILOT_ASD_THRESHOLD_BINS];
    float vmin = volume_data[0], vmax = volume_data[0];
    float bin_width, shift, lower, upper;
    int k1, k2;
    size_t i;

    for (i = 1; i < voxel_count; ++i) {
        if (volume_data[i] < vmin) vmin = volume_data[i];
        if (volume_data[i] > vmax) vmax = volume_data[i];
    }
    if (!(vmax > vmin)) return 0;

    memset(hist, 0, sizeof(hist));
    bin_width = (vmax - vmin) / (float)VOXELPILOT_ASD_THRESHOLD_BINS;
    for (i = 0; i < voxel_count; ++i) {
        int bin = (int)((volume_data[i] - vmin) / bin_width);
        if (bin < 0) bin = 0;
        if (bin >= VOXELPILOT_ASD_THRESHOLD_BINS) bin = VOXELPILOT_ASD_THRESHOLD_BINS - 1;
        hist[bin] += 1.0;
    }

    k1 = voxelpilot_asd_otsu_split(hist, 0, VOXELPILOT_ASD_THRESHOLD_BINS - 1);
    if (k1 < 0) return 0;
    k2 = voxelpilot_asd_otsu_split(hist, k1, VOXELPILOT_ASD_THRESHOLD_BINS - 1);

    lower = vmin + (float)k1 * bin_width;
    upper = (k2 > k1) ? vmin + (float)k2 * bin_width : lower;

    shift = (0.5f - voxelpilot_asd_clampf(sensitivity, 0.0f, 1.0f)) * 0.10f * (vmax - vmin);
    lower += shift;
    upper += shift;
    if (upper < lower) upper = lower;

    *out_lower = lower;
    *out_upper = upper;
    return 1;
}

/* 0 = background; legacy mode: 1 = material; adaptive: 1 = mid, 2 = dense. */
static inline int voxelpilot_asd_voxel_class(float value, float lower, float upper, int adaptive)
{
    if (value < lower) return 0;
    if (!adaptive) return 1;
    return value >= upper ? 2 : 1;
}

static inline int voxelpilot_discover_semantic_regions_ex(
    const float *volume_data,
    int width,
    int height,
    int depth,
    const float *spacing,
    int adaptive_thresholds,
    int max_regions,
    float sensitivity,
    size_t min_region_voxels,
    unsigned char *label_mask,
    VoxelPilotSemanticRegion *regions,
    int *out_region_count)
{
    size_t voxel_count;
    size_t class_voxels[3] = { 0, 0, 0 };
    double sp[3];
    int clamped_max_regions;
    int *queue;
    unsigned char *visited;
    VoxelPilotAsdCandidate *candidates;
    int candidate_count = 0;
    int selected_count;
    size_t start_index;
    int xy_stride;
    float lower = 0.0f;
    float upper = 0.0f;
    int adaptive = adaptive_thresholds ? 1 : 0;
    int i;

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
    if (clamped_max_regions > VOXELPILOT_ASD_MAX_REGIONS) clamped_max_regions = VOXELPILOT_ASD_MAX_REGIONS;
    if (clamped_max_regions > 255) clamped_max_regions = 255;
    if (min_region_voxels < 1) min_region_voxels = 1;

    if (adaptive && !voxelpilot_asd_material_thresholds(volume_data, voxel_count, sensitivity, &lower, &upper)) {
        adaptive = 0;
    }
    if (!adaptive) {
        lower = voxelpilot_asd_clampf(0.70f - sensitivity * 0.25f, 0.35f, 0.85f);
        upper = lower;
    }

    queue = (int *)malloc(sizeof(int) * voxel_count);
    visited = (unsigned char *)calloc(voxel_count, sizeof(unsigned char));
    candidates = (VoxelPilotAsdCandidate *)malloc(sizeof(VoxelPilotAsdCandidate) * VOXELPILOT_ASD_MAX_CANDIDATES);
    if (!queue || !visited || !candidates) {
        free(queue);
        free(visited);
        free(candidates);
        voxelpilot_asd_clear_regions(regions, max_regions, out_region_count);
        return 0;
    }

    xy_stride = width * height;
    for (i = 0; i < 3; ++i) {
        sp[i] = (spacing && spacing[i] > 0.0f) ? (double)spacing[i] : 1.0;
    }

    /* ---- Pass 1: measure every component, keep the best candidates ---- */
    for (start_index = 0; start_index < voxel_count; ++start_index) {
        int start = (int)start_index;
        int voxel_class;
        int head = 0;
        int tail = 0;
        double sum_intensity = 0.0, sum_x = 0.0, sum_y = 0.0, sum_z = 0.0;
        double phys_sum[3] = { 0.0, 0.0, 0.0 };
        double phys_sq[3][3] = { { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 } };
        float min_intensity = 1e30f;
        float max_intensity = -1e30f;
        int bbox_min[3] = { width, height, depth };
        int bbox_max[3] = { 0, 0, 0 };

        if (visited[start]) continue;
        voxel_class = voxelpilot_asd_voxel_class(volume_data[start], lower, upper, adaptive);
        if (voxel_class == 0) continue;

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

            sum_intensity += (double)value;
            sum_x += (double)x;
            sum_y += (double)y;
            sum_z += (double)z;
            {
                const double p[3] = { (double)x * sp[0], (double)y * sp[1], (double)z * sp[2] };
                int a, b;
                for (a = 0; a < 3; ++a) {
                    phys_sum[a] += p[a];
                    for (b = a; b < 3; ++b) phys_sq[a][b] += p[a] * p[b];
                }
            }
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
                if (!visited[neighbor] &&
                    voxelpilot_asd_voxel_class(volume_data[neighbor], lower, upper, adaptive) == voxel_class) {
                    visited[neighbor] = 1;
                    queue[tail++] = neighbor;
                }
            }
        }

        class_voxels[voxel_class] += (size_t)tail;
        if ((size_t)tail < min_region_voxels) continue;

        {
            VoxelPilotSemanticRegion r;
            int slot = -1;

            memset(&r, 0, sizeof(r));
            r.voxel_count = (size_t)tail;
            r.centroid[0] = (float)(sum_x / (double)tail);
            r.centroid[1] = (float)(sum_y / (double)tail);
            r.centroid[2] = (float)(sum_z / (double)tail);
            memcpy(r.bbox_min, bbox_min, sizeof(bbox_min));
            memcpy(r.bbox_max, bbox_max, sizeof(bbox_max));
            r.min_intensity = min_intensity;
            r.max_intensity = max_intensity;
            r.mean_intensity = (float)(sum_intensity / (double)tail);
            voxelpilot_asd_compute_scores(&r, voxel_count, spacing);
            if (adaptive) {
                r.density_class = voxel_class == 2 ? VOXELPILOT_ASD_DENSITY_HIGH : VOXELPILOT_ASD_DENSITY_MID;
            }
            {
                /* Upper triangle accumulated; mirror it for the solver. */
                double sq[3][3];
                int a, b;
                for (a = 0; a < 3; ++a)
                    for (b = 0; b < 3; ++b)
                        sq[a][b] = a <= b ? phys_sq[a][b] : phys_sq[b][a];
                voxelpilot_asd_principal_shape(&r, (double)tail, phys_sum, sq, spacing);
            }
            voxelpilot_asd_assign_archetype(&r);

            if (candidate_count < VOXELPILOT_ASD_MAX_CANDIDATES) {
                slot = candidate_count++;
            } else {
                int weakest = 0;
                for (i = 1; i < candidate_count; ++i) {
                    if (candidates[i].region.importance_score < candidates[weakest].region.importance_score) {
                        weakest = i;
                    }
                }
                if (r.importance_score > candidates[weakest].region.importance_score) slot = weakest;
            }
            if (slot >= 0) {
                candidates[slot].region = r;
                candidates[slot].seed = start;
                candidates[slot].voxel_class = voxel_class;
            }
        }
    }

    /* ---- Choose: most important first (ties: larger first) ---- */
    selected_count = candidate_count < clamped_max_regions ? candidate_count : clamped_max_regions;
    for (i = 0; i < selected_count; ++i) {
        int best = i;
        int j;
        for (j = i + 1; j < candidate_count; ++j) {
            const VoxelPilotSemanticRegion *a = &candidates[j].region;
            const VoxelPilotSemanticRegion *b = &candidates[best].region;
            if (a->importance_score > b->importance_score ||
                (a->importance_score == b->importance_score && a->voxel_count > b->voxel_count)) {
                best = j;
            }
        }
        if (best != i) {
            VoxelPilotAsdCandidate tmp = candidates[i];
            candidates[i] = candidates[best];
            candidates[best] = tmp;
        }
    }

    /* ---- Pass 2: label only the chosen components ---- */
    for (i = 0; i < selected_count; ++i) {
        unsigned char label_id = (unsigned char)(i + 1);
        int voxel_class = candidates[i].voxel_class;
        int head = 0;
        int tail = 0;
        VoxelPilotSemanticRegion *region = &regions[i];

        label_mask[candidates[i].seed] = label_id;
        queue[tail++] = candidates[i].seed;
        while (head < tail) {
            int index = queue[head++];
            int z = index / xy_stride;
            int rem = index - z * xy_stride;
            int y = rem / width;
            int x = rem - y * width;
            int neighbors[6];
            int neighbor_count = 0;
            int n;

            if (x > 0) neighbors[neighbor_count++] = index - 1;
            if (x + 1 < width) neighbors[neighbor_count++] = index + 1;
            if (y > 0) neighbors[neighbor_count++] = index - width;
            if (y + 1 < height) neighbors[neighbor_count++] = index + width;
            if (z > 0) neighbors[neighbor_count++] = index - xy_stride;
            if (z + 1 < depth) neighbors[neighbor_count++] = index + xy_stride;

            for (n = 0; n < neighbor_count; ++n) {
                int neighbor = neighbors[n];
                if (label_mask[neighbor] == 0 &&
                    voxelpilot_asd_voxel_class(volume_data[neighbor], lower, upper, adaptive) == voxel_class) {
                    label_mask[neighbor] = label_id;
                    queue[tail++] = neighbor;
                }
            }
        }

        *region = candidates[i].region;
        region->label_id = label_id;
        region->source = VOXELPILOT_SEMANTIC_SOURCE_HEURISTIC;
        voxelpilot_asd_assign_color(region);
        /* Size words compare a region with its own material: a bone is
           "large" among bones, not small next to all soft tissue. */
        voxelpilot_asd_name_region(region, width, height, depth,
                                   class_voxels[candidates[i].voxel_class],
                                   lower, adaptive ? upper : lower);
    }

    free(queue);
    free(visited);
    free(candidates);

    voxelpilot_asd_make_names_unique(regions, selected_count);
    voxelpilot_asd_rank_regions(regions, selected_count);
    *out_region_count = selected_count;
    return selected_count > 0 ? 1 : 0;
}

/* Original entry point, kept for existing callers: isotropic spacing;
   passing a histogram selects adaptive material thresholds (which are
   computed from the volume itself - the histogram argument only signals
   the intent), NULL keeps the single fixed cut-off. */
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
    (void)histogram_bins;
    return voxelpilot_discover_semantic_regions_ex(
        volume_data, width, height, depth, NULL, histogram != NULL,
        max_regions, sensitivity, min_region_voxels,
        label_mask, regions, out_region_count);
}

#ifdef __cplusplus
}
#endif

#endif
