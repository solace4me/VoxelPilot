# Adaptive Semantic Discovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build Phase 2A/2B of VoxelPilot v2 Adaptive Semantic Discovery: local, bounded, dataset-agnostic region discovery with cautious labels, archetypes, stability, importance ranking, relationships, hover pop-ups, and exportable provenance.

**Architecture:** Add a focused header-only ASD helper in `common/semantic_discovery.h` so algorithm code stays separate from the already-large UI and AI helper files. The helper discovers bounded connected regions from intensity bands, computes descriptors, assigns archetypes/cautious descriptions, ranks importance, computes relationships, and writes label IDs into the existing label-mask path. `standalone_main.cu` integrates the helper into AI Assist, hover pop-ups, existing CUDA label overlays, and export metadata without adding neural inference or extra renderer passes.

**Tech Stack:** C/C++17 header helpers, CUDA renderer label-mask overlay, ImGui UI, Windows batch tests, existing `tests/volume_import_tests.cpp`, existing Phase 0 render regression scripts.

---

## File Structure

- Create `common/semantic_discovery.h`: ASD constants, semantic region structs, discovery helpers, naming, ranking, relationships, and description formatting.
- Modify `tests/volume_import_tests.cpp`: TDD coverage for semantic discovery invalid input, region detection, archetypes, stability, importance ranking, relationships, and cautious descriptions.
- Modify `standalone_main.cu`: app state for semantic regions/relationships, AI Assist controls, hover pop-up upgrades, label overlay color wiring, and semantic JSON export.
- Modify `README.md`: document ASD-Lite behavior and honest-labeling guardrails.
- Keep `renderer/raymarch_kernel.cu` unchanged for Phase 2A/2B unless existing label overlay needs a tiny selected-region tint adjustment. The first implementation reuses current label overlay.

## Scope Slices

Phase 2 is intentionally split:

- **2A Core:** algorithm, descriptors, cautious labels, ranking, relationships, tests.
- **2B UI/Hover:** `Discover Regions`, semantic legend, hover pop-ups, overlay colors.
- **2C Export/Corrections:** richer JSON export, rename/confirm/reject controls.
- **2D Model Bridge:** future ONNX/MONAI/SAM-style plugin interface.

This plan implements 2A and the useful parts of 2B. 2C/2D get clear extension seams but are not required for the first ASD build.

## Task 1: Semantic Types And Invalid Input Tests

**Files:**
- Create: `common/semantic_discovery.h`
- Modify: `tests/volume_import_tests.cpp`

- [ ] **Step 1: Write failing tests**

Add this include near the existing helper includes in `tests/volume_import_tests.cpp`:

```cpp
#include "../common/semantic_discovery.h"
```

Add this test before `main()`:

```cpp
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
```

Call it from `main()`:

```cpp
passed &= test_asd_rejects_invalid_inputs();
```

- [ ] **Step 2: Run test to verify red**

Run:

```powershell
.\run_tests_windows.bat
```

Expected: compile fails because `common/semantic_discovery.h`, `VoxelPilotSemanticRegion`, or `voxelpilot_discover_semantic_regions` is missing.

- [ ] **Step 3: Add minimal header and invalid-input implementation**

Create `common/semantic_discovery.h` with:

```c
#ifndef VOXELPILOT_SEMANTIC_DISCOVERY_H
#define VOXELPILOT_SEMANTIC_DISCOVERY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define VOXELPILOT_ASD_MAX_REGIONS 16
#define VOXELPILOT_ASD_MAX_RELATIONS 64
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
    (void)histogram;
    (void)histogram_bins;
    (void)sensitivity;
    (void)min_region_voxels;

    voxelpilot_asd_clear_regions(regions, max_regions, out_region_count);

    if (!volume_data || !label_mask || !regions || !out_region_count ||
        width < 1 || height < 1 || depth < 1 || max_regions < 1) {
        return 0;
    }

    return 0;
}

#ifdef __cplusplus
}
#endif

#endif
```

- [ ] **Step 4: Run test to verify green for invalid input**

Run:

```powershell
.\run_tests_windows.bat
```

Expected: `volume_import_tests: PASS` and Python render-regression unit tests pass.

## Task 2: Density-Band Connected Region Discovery

**Files:**
- Modify: `common/semantic_discovery.h`
- Modify: `tests/volume_import_tests.cpp`

- [ ] **Step 1: Write failing discovery test**

Add this test before `main()`:

```cpp
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
```

Call it from `main()`:

```cpp
passed &= test_asd_discovers_high_density_connected_region();
```

- [ ] **Step 2: Run test to verify red**

Run:

```powershell
.\run_tests_windows.bat
```

Expected: test fails because discovery currently returns no regions.

- [ ] **Step 3: Implement bounded high-density flood fill**

In `common/semantic_discovery.h`, update `voxelpilot_discover_semantic_regions` to:

1. Clear `label_mask` for `width * height * depth`.
2. Clamp `max_regions` to `VOXELPILOT_ASD_MAX_REGIONS`.
3. Compute `threshold = 0.70f - sensitivity * 0.25f`, clamped to `[0.35f, 0.85f]`.
4. Iterate voxels; when `volume_data[index] >= threshold` and unlabeled, BFS flood-fill 6-neighbors.
5. Assign label id `region_count + 1` only if `voxel_count >= min_region_voxels`; otherwise clear temporary labels.
6. Compute `voxel_count`, bbox, centroid, min/max/mean intensity.

Use bounded heap arrays sized by voxel count:

```c
int *queue = (int *)malloc(sizeof(int) * voxel_count);
int *visited = (int *)calloc(voxel_count, sizeof(int));
```

Free both before returning. If allocation fails, clear outputs and return `0`.

- [ ] **Step 4: Run test to verify green**

Run:

```powershell
.\run_tests_windows.bat
```

Expected: all tests pass.

## Task 3: Archetypes, Cautious Names, Stability, And Importance

**Files:**
- Modify: `common/semantic_discovery.h`
- Modify: `tests/volume_import_tests.cpp`

- [ ] **Step 1: Write failing descriptor test**

Add this test before `main()`:

```cpp
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
```

Call it from `main()`:

```cpp
passed &= test_asd_assigns_archetype_stability_and_importance();
```

- [ ] **Step 2: Run test to verify red**

Run:

```powershell
.\run_tests_windows.bat
```

Expected: failure because archetype/name/stability/importance are not yet computed.

- [ ] **Step 3: Implement descriptors and naming**

Add helper functions in `common/semantic_discovery.h`:

```c
static inline VoxelPilotDensityClass voxelpilot_asd_density_class(float mean_intensity);
static inline void voxelpilot_asd_assign_archetype(VoxelPilotSemanticRegion *region);
static inline void voxelpilot_asd_assign_color(VoxelPilotSemanticRegion *region);
static inline void voxelpilot_asd_assign_name_and_description(VoxelPilotSemanticRegion *region);
static inline void voxelpilot_asd_compute_scores(VoxelPilotSemanticRegion *region, size_t total_voxels);
```

Behavior:

- Density class: `<0.25 low`, `<0.65 mid`, otherwise high.
- Elongation: max bbox extent / min nonzero bbox extent.
- Compactness: voxel count / bbox volume.
- Archetype:
  - `rod` if elongation >= 2.4 and compactness >= 0.35.
  - `plate` if one extent <= 2 and other two extents >= 3.
  - `shell` if high density and compactness < 0.45 and bbox volume >= 16.
  - `blob` if compactness >= 0.55.
  - `cluster` if compactness >= 0.20.
  - `unclassified` otherwise.
- Stability score for ASD-Lite: deterministic proxy `0.35 + boundary_strength * 0.35 + compactness * 0.20`, clamped `[0,1]`. Add true multi-pass stability later behind the same field.
- Importance score: normalized weighted sum of size ratio, boundary strength, stability, and mean intensity.
- Names must be cautious descriptor names only, e.g. `dense rod-like structure`, `soft-density blob-like region`, `low-density cavity/background region`.

- [ ] **Step 4: Run test to verify green**

Run:

```powershell
.\run_tests_windows.bat
```

Expected: all tests pass.

## Task 4: Region Ranking And Relationships

**Files:**
- Modify: `common/semantic_discovery.h`
- Modify: `tests/volume_import_tests.cpp`

- [ ] **Step 1: Write failing relationship test**

Add this test before `main()`:

```cpp
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
```

Call it from `main()`:

```cpp
passed &= test_asd_ranks_regions_and_reports_relationships();
```

- [ ] **Step 2: Run test to verify red**

Run:

```powershell
.\run_tests_windows.bat
```

Expected: compile failure or test failure because rank/relationship helpers are missing.

- [ ] **Step 3: Implement ranking and relationships**

Add helpers:

```c
static inline void voxelpilot_asd_rank_regions(VoxelPilotSemanticRegion *regions, int region_count);
static inline int voxelpilot_compute_region_relationships(
    const VoxelPilotSemanticRegion *regions,
    int region_count,
    VoxelPilotRegionRelation *relations,
    int max_relations,
    int *out_relation_count);
```

Behavior:

- Rank by descending `importance_score`; assign rank numbers without reordering the array.
- Relationship helper compares all region pairs up to 16 regions.
- Adjacent if bbox gap in one axis is <= 1 and bbox ranges overlap in the other axes.
- Contains if region A bbox fully contains region B bbox.
- Inside if region A bbox is fully inside region B bbox.
- Isolated if no pair relationship exists for a region and relation budget remains.
- Write a short `description` string for each relation.

- [ ] **Step 4: Run test to verify green**

Run:

```powershell
.\run_tests_windows.bat
```

Expected: all tests pass.

## Task 5: Standalone App ASD State And Discovery Button

**Files:**
- Modify: `standalone_main.cu`
- Test: `tests/volume_import_tests.cpp` remains the algorithm safety net.

- [ ] **Step 1: Add app state**

In `StandaloneApp`, add:

```cpp
VoxelPilotSemanticRegion semantic_regions[VOXELPILOT_ASD_MAX_REGIONS];
int semantic_region_count;
VoxelPilotRegionRelation semantic_relations[VOXELPILOT_ASD_MAX_RELATIONS];
int semantic_relation_count;
int semantic_selected_region;
int semantic_max_regions;
int semantic_min_region_voxels;
float semantic_sensitivity;
int semantic_cinematic_emphasis;
char semantic_status[512];
```

Initialize defaults in startup and reset:

```cpp
app.semantic_region_count = 0;
app.semantic_relation_count = 0;
app.semantic_selected_region = -1;
app.semantic_max_regions = 8;
app.semantic_min_region_voxels = 128;
app.semantic_sensitivity = 0.55f;
app.semantic_cinematic_emphasis = 1;
strcpy(app.semantic_status, "Click Discover Regions to run ASD-Lite.");
```

- [ ] **Step 2: Include semantic header**

Add near existing common includes:

```cpp
#include "common/semantic_discovery.h"
```

- [ ] **Step 3: Add discovery runner**

Add a helper in `standalone_main.cu`:

```cpp
static void run_adaptive_semantic_discovery(StandaloneApp *app)
{
    if (!app || !app->renderer.volume_data || app->renderer.W < 1 || app->renderer.H < 1 || app->renderer.D < 1) {
        snprintf(app->semantic_status, sizeof(app->semantic_status), "Load a volume before running ASD-Lite.");
        return;
    }

    if (!ensure_label_mask(app)) {
        snprintf(app->semantic_status, sizeof(app->semantic_status), "Could not allocate semantic label mask.");
        return;
    }

    if (!voxelpilot_discover_semantic_regions(
            app->renderer.volume_data,
            app->renderer.W,
            app->renderer.H,
            app->renderer.D,
            app->histogram,
            HISTOGRAM_BINS,
            app->semantic_max_regions,
            app->semantic_sensitivity,
            (size_t)app->semantic_min_region_voxels,
            app->label_mask,
            app->semantic_regions,
            &app->semantic_region_count)) {
        snprintf(app->semantic_status, sizeof(app->semantic_status), "ASD-Lite found no stable regions for current settings.");
        app->semantic_relation_count = 0;
        app->label_mask_revision++;
        return;
    }

    voxelpilot_compute_region_relationships(
        app->semantic_regions,
        app->semantic_region_count,
        app->semantic_relations,
        VOXELPILOT_ASD_MAX_RELATIONS,
        &app->semantic_relation_count);

    app->annotation_count = app->semantic_region_count;
    for (int i = 0; i < app->semantic_region_count && i < MAX_ANNOTATIONS; ++i) {
        AnnotationRegion *region = &app->annotations[i];
        memset(region, 0, sizeof(*region));
        region->active = 1;
        region->label_id = app->semantic_regions[i].label_id;
        snprintf(region->name, sizeof(region->name), "%s", app->semantic_regions[i].name);
        region->color[0] = app->semantic_regions[i].color[0];
        region->color[1] = app->semantic_regions[i].color[1];
        region->color[2] = app->semantic_regions[i].color[2];
        region->voxel_count = app->semantic_regions[i].voxel_count;
        region->min_intensity = app->semantic_regions[i].min_intensity;
        region->max_intensity = app->semantic_regions[i].max_intensity;
    }

    app->selected_annotation = app->semantic_region_count > 0 ? 0 : -1;
    app->semantic_selected_region = app->selected_annotation;
    app->label_mask_revision++;
    snprintf(app->semantic_status, sizeof(app->semantic_status),
             "ASD-Lite discovered %d region(s) and %d relationship(s).",
             app->semantic_region_count,
             app->semantic_relation_count);
}
```

- [ ] **Step 4: Add AI Assist controls**

In `draw_ai_assist_panel`, above `Multi-Material Labels`, add:

```cpp
ImGui::Text("Adaptive Semantic Discovery");
ImGui::SliderInt("ASD Max Regions", &app->semantic_max_regions, 1, VOXELPILOT_ASD_MAX_REGIONS);
ImGui::SliderFloat("ASD Sensitivity", &app->semantic_sensitivity, 0.0f, 1.0f);
ImGui::InputInt("ASD Min Voxels", &app->semantic_min_region_voxels);
if (app->semantic_min_region_voxels < 1) app->semantic_min_region_voxels = 1;
{
    bool emphasis = app->semantic_cinematic_emphasis != 0;
    if (ImGui::Checkbox("ASD Cinematic Emphasis", &emphasis)) {
        app->semantic_cinematic_emphasis = emphasis ? 1 : 0;
    }
}
if (ImGui::Button("Discover Regions")) {
    run_adaptive_semantic_discovery(app);
}
ImGui::TextWrapped("ASD status: %s", app->semantic_status);
```

- [ ] **Step 5: Run build/test smoke**

Run:

```powershell
.\run_tests_windows.bat
.\build_windows.ps1
```

Expected: tests pass and build succeeds with existing third-party warnings only.

## Task 6: Semantic Legend And Hover Pop-Up Upgrade

**Files:**
- Modify: `standalone_main.cu`

- [ ] **Step 1: Add semantic region lookup helper**

Add:

```cpp
static VoxelPilotSemanticRegion *find_semantic_region_by_label(
    StandaloneApp *app,
    unsigned char label_id)
{
    if (!app || label_id == 0) return NULL;
    for (int i = 0; i < app->semantic_region_count; ++i) {
        if (app->semantic_regions[i].label_id == label_id) {
            return &app->semantic_regions[i];
        }
    }
    return NULL;
}
```

- [ ] **Step 2: Display semantic legend**

Below ASD status in `draw_ai_assist_panel`, add:

```cpp
if (app->semantic_region_count > 0) {
    ImGui::Text("Region Legend");
    for (int i = 0; i < app->semantic_region_count; ++i) {
        VoxelPilotSemanticRegion *region = &app->semantic_regions[i];
        char label[192];
        snprintf(label, sizeof(label), "#%d %s | %s | %.0f%% stable | rank %d",
                 (int)region->label_id,
                 region->name,
                 region->archetype,
                 region->stability_score * 100.0f,
                 region->importance_rank);
        if (ImGui::Selectable(label, app->semantic_selected_region == i)) {
            app->semantic_selected_region = i;
            app->selected_annotation = i;
        }
    }
}
```

- [ ] **Step 3: Upgrade hover description**

In slice and main hover paths, after reading `label_id`, prefer `find_semantic_region_by_label`. If present, show:

```cpp
snprintf(app->main_hover_description, sizeof(app->main_hover_description),
         "%s | %s | confidence %.0f%% | stability %.0f%% | source ASD-Lite. %s",
         semantic->name,
         semantic->archetype,
         semantic->confidence * 100.0f,
         semantic->stability_score * 100.0f,
         semantic->description);
```

For slice tooltip, use the same semantic text in `app->hover_description`.

- [ ] **Step 4: Add relationship note to hover**

Add helper:

```cpp
static const char *semantic_relationship_note(StandaloneApp *app, unsigned char label_id)
{
    static char empty_note[1] = { '\0' };
    if (!app || label_id == 0) return empty_note;
    for (int i = 0; i < app->semantic_relation_count; ++i) {
        if (app->semantic_relations[i].region_a == label_id || app->semantic_relations[i].region_b == label_id) {
            return app->semantic_relations[i].description;
        }
    }
    return empty_note;
}
```

Append the note in tooltip only if it is non-empty.

- [ ] **Step 5: Build smoke**

Run:

```powershell
.\build_windows.ps1
```

Expected: build succeeds.

## Task 7: Semantic Export Metadata

**Files:**
- Modify: `standalone_main.cu`
- Modify: `README.md`

- [ ] **Step 1: Extend JSON export**

In `export_annotation_mask`, when writing the `.json`, include semantic fields if a matching semantic region exists:

```json
"semantic": {
  "source": "ASD-Lite heuristic",
  "confidence": 0.72,
  "stability_score": 0.81,
  "importance_score": 0.66,
  "importance_rank": 1,
  "archetype": "rod",
  "description": "AI-suggested dense rod-like structure grouped by connected high-intensity voxels."
}
```

Also add a top-level `relationships` array using `semantic_relations`.

- [ ] **Step 2: Document ASD in README**

Add a short section:

```markdown
## Adaptive Semantic Discovery

VoxelPilot v2 can run ASD-Lite from AI Assist to discover candidate regions in arbitrary volumes. ASD labels are cautious and local: they describe density, shape archetype, stability, importance, and relationships without claiming exact anatomy or diagnosis unless a user or future model plugin provides provenance.
```

- [ ] **Step 3: Build smoke**

Run:

```powershell
.\build_windows.ps1
```

Expected: build succeeds.

## Task 8: Final Verification And Commit

**Files:**
- All files changed above.

- [ ] **Step 1: Run full verification**

Run:

```powershell
.\run_tests_windows.bat
.\build_windows.ps1
.\run_render_regression_windows.bat
```

Expected:

- `volume_import_tests: PASS`
- Python render regression tests pass
- build completes with only existing third-party warnings
- foot render regression passes

- [ ] **Step 2: Generate a visual ASD-ready smoke snapshot**

Run:

```powershell
.\volume_renderer_standalone.exe --volume .\sample-data\foot_256x256x256_uint8.raw --width 256 --height 256 --depth 256 --type uint8 --snapshot .\build\foot_asd_phase2_smoke.png --cinematic-lite
```

Expected: snapshot writes successfully. ASD is UI-driven, so this does not prove discovery; it proves V2 cinematic rendering remains intact after ASD integration.

- [ ] **Step 3: Review changed files**

Run:

```powershell
git status --short
git diff --stat
```

Expected: only intentional source, test, docs, and plan/spec files changed.

- [ ] **Step 4: Commit locally**

Run:

```powershell
git add common/semantic_discovery.h tests/volume_import_tests.cpp standalone_main.cu README.md docs/superpowers/specs/2026-06-29-adaptive-semantic-discovery-design.md docs/superpowers/plans/2026-06-29-adaptive-semantic-discovery.md
git commit -m "feat: add adaptive semantic discovery plan and core"
```

Expected: local commit on `codex/v2-cinematic-ai`. Do not push to GitHub unless the user explicitly asks.

## Self-Review

- Spec coverage: ASD region discovery, cautious names, archetypes, stability, relationships, importance ranking, hover pop-ups, cinematic emphasis, export metadata, performance guardrails, and no hardcoded anatomy are represented.
- Scope control: this plan implements ASD-Lite, UI/hover integration, and export metadata; it does not implement neural models, heavy shadows, out-of-core streaming, or a persistent knowledge base.
- Type consistency: `VoxelPilotSemanticRegion`, `VoxelPilotRegionRelation`, `voxelpilot_discover_semantic_regions`, `voxelpilot_compute_region_relationships`, and `voxelpilot_asd_rank_regions` are used consistently.
- Verification includes unit tests, CUDA build, Phase 0 foot render baseline, and smoke snapshot.
