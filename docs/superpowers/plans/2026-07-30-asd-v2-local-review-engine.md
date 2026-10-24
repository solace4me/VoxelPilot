# ASD v2 Local Review Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build ASD v2 as a fully local, explainable review engine with more accurate region discovery, evidence-rich descriptions, natural AI prompts, Smart Review, richer exports, and a clean model-provider provenance boundary.

**Architecture:** Extend the existing C/C++ helper headers rather than creating a separate subsystem. Keep discovery and prompt parsing unit-testable in `common/`, keep UI orchestration thin in `standalone_main.cu`, and reuse existing label-mask, Guided Review, callout, and review-bundle paths. Model-backed segmentation is represented only as source/provenance metadata in this plan.

**Tech Stack:** C/C++ inline helpers, CUDA app integration, ImGui UI, existing Windows build/test scripts, existing `tests/volume_import_tests.cpp` regression suite.

---

## Scope Check

This plan implements one coherent feature set: ASD v2 local-first review intelligence. It touches discovery, descriptions, prompts, exports, Smart Review wiring, docs, and tests because those pieces must work together for the demo path. It does not implement neural inference, cloud AI, CUDA renderer rewrites, or out-of-core streaming.

## File Structure

- Modify `common/semantic_discovery.h`: add ASD v2 evidence fields, density-band helpers, source/provenance helpers, improved discovery, naming, descriptions, stability, and correction matching.
- Modify `common/ai_prompt.h`: extend `VoxelPilotPromptResult` with workflow intents while preserving existing transfer and quality behavior.
- Modify `common/ai_review_bundle.h`: export ASD v2 evidence/provenance fields in regions JSON and HTML report.
- Modify `standalone_main.cu`: add Smart Review workflow, AI Assist button, prompt workflow handling, richer selected-region summaries, and updated review bundle status.
- Modify `tests/volume_import_tests.cpp`: add focused unit tests before each helper change and register them in `main()`.
- Modify `README.md`: document ASD v2, Smart Review, cautious naming, richer exports, and the model-ready provenance boundary.
- Modify `docs/superpowers/plans/2026-07-30-asd-v2-local-review-engine.md`: check off completed steps as work proceeds.

## Validation Commands

Run these from the repository root:

```powershell
.\run_tests_windows.bat
.\build_windows.ps1
.\run_bonsai_asd_verification_windows.bat
```

For focused compile feedback while editing `tests/volume_import_tests.cpp`, use the same command the project already relies on:

```powershell
.\run_tests_windows.bat
```

---

### Task 1: Add ASD v2 Evidence And Provenance Types

**Files:**
- Modify: `common/semantic_discovery.h`
- Modify: `tests/volume_import_tests.cpp`

- [ ] **Step 1: Write failing tests for ASD v2 metadata defaults**

Add this test near the existing ASD tests in `tests/volume_import_tests.cpp`:

```cpp
static int test_asd_v2_metadata_defaults_and_source_labels(void)
{
    VoxelPilotSemanticRegion region;
    memset(&region, 0, sizeof(region));

    voxelpilot_asd_region_init_v2_metadata(&region);

    if (!expect_true(
            strcmp(region.provider_name, "ASD-v2 local heuristic") == 0,
            "ASD v2 metadata should default to local heuristic provider")) {
        return 0;
    }
    if (!expect_true(
            strcmp(region.name_policy, "cautious_descriptor") == 0,
            "ASD v2 metadata should default to cautious descriptor names")) {
        return 0;
    }
    if (!expect_true(
            strcmp(voxelpilot_semantic_source_label(VOXELPILOT_SEMANTIC_SOURCE_HEURISTIC), "heuristic") == 0,
            "semantic source label should describe heuristic provenance")) {
        return 0;
    }
    return expect_true(
        strcmp(region.density_band, "unknown") == 0 &&
        strcmp(region.evidence_summary, "No ASD-v2 evidence recorded yet.") == 0,
        "ASD v2 metadata should include safe placeholder evidence text");
}
```

Register it in `main()` after `test_asd_rejects_invalid_inputs()`:

```cpp
passed &= test_asd_v2_metadata_defaults_and_source_labels();
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```powershell
.\run_tests_windows.bat
```

Expected: compile fails because `provider_name`, `name_policy`, `density_band`, `evidence_summary`, `voxelpilot_asd_region_init_v2_metadata`, and `voxelpilot_semantic_source_label` do not exist.

- [ ] **Step 3: Add fields and helpers**

In `common/semantic_discovery.h`, increase the description size and add evidence constants after the existing `VOXELPILOT_ASD_*` constants:

```c
#define VOXELPILOT_ASD_DESCRIPTION_MAX 384
#define VOXELPILOT_ASD_PROVIDER_MAX 64
#define VOXELPILOT_ASD_VERSION_MAX 32
#define VOXELPILOT_ASD_NAME_POLICY_MAX 32
#define VOXELPILOT_ASD_DENSITY_BAND_MAX 32
#define VOXELPILOT_ASD_LOCATION_MAX 64
#define VOXELPILOT_ASD_EVIDENCE_MAX 320
```

Replace the existing `#define VOXELPILOT_ASD_DESCRIPTION_MAX 256` line with the new `384` definition rather than keeping both definitions.

Extend `VoxelPilotSemanticRegion` after `VoxelPilotDensityClass density_class;`:

```c
    char provider_name[VOXELPILOT_ASD_PROVIDER_MAX];
    char provider_version[VOXELPILOT_ASD_VERSION_MAX];
    char name_policy[VOXELPILOT_ASD_NAME_POLICY_MAX];
    char density_band[VOXELPILOT_ASD_DENSITY_BAND_MAX];
    char location_descriptor[VOXELPILOT_ASD_LOCATION_MAX];
    char evidence_summary[VOXELPILOT_ASD_EVIDENCE_MAX];
    float intensity_percentile_min;
    float intensity_percentile_max;
    float threshold_min;
    float threshold_max;
    float fill_ratio;
    float overlap_merge_score;
```

Add these helpers after `voxelpilot_asd_clampf()`:

```c
static inline const char *voxelpilot_semantic_source_label(VoxelPilotSemanticSource source)
{
    switch (source) {
        case VOXELPILOT_SEMANTIC_SOURCE_HEURISTIC: return "heuristic";
        case VOXELPILOT_SEMANTIC_SOURCE_USER_CONFIRMED: return "user_confirmed";
        case VOXELPILOT_SEMANTIC_SOURCE_IMPORTED: return "imported_metadata";
        case VOXELPILOT_SEMANTIC_SOURCE_MODEL: return "model";
        default: return "unknown";
    }
}

static inline void voxelpilot_asd_region_init_v2_metadata(VoxelPilotSemanticRegion *region)
{
    if (!region) return;
    snprintf(region->provider_name, sizeof(region->provider_name), "ASD-v2 local heuristic");
    snprintf(region->provider_version, sizeof(region->provider_version), "2.0-local");
    snprintf(region->name_policy, sizeof(region->name_policy), "cautious_descriptor");
    snprintf(region->density_band, sizeof(region->density_band), "unknown");
    snprintf(region->location_descriptor, sizeof(region->location_descriptor), "unknown volume location");
    snprintf(region->evidence_summary, sizeof(region->evidence_summary), "No ASD-v2 evidence recorded yet.");
    region->intensity_percentile_min = 0.0f;
    region->intensity_percentile_max = 0.0f;
    region->threshold_min = 0.0f;
    region->threshold_max = 1.0f;
    region->fill_ratio = 0.0f;
    region->overlap_merge_score = 0.0f;
}
```

Update `voxelpilot_asd_clear_regions()` so metadata defaults are restored after `memset`:

```c
static inline void voxelpilot_asd_clear_regions(
    VoxelPilotSemanticRegion *regions,
    int max_regions,
    int *out_region_count)
{
    int i;
    if (regions && max_regions > 0) {
        memset(regions, 0, sizeof(VoxelPilotSemanticRegion) * (size_t)max_regions);
        for (i = 0; i < max_regions; ++i) {
            voxelpilot_asd_region_init_v2_metadata(&regions[i]);
        }
    }
    if (out_region_count) {
        *out_region_count = 0;
    }
}
```

- [ ] **Step 4: Run tests to verify pass**

Run:

```powershell
.\run_tests_windows.bat
```

Expected: tests pass.

- [ ] **Step 5: Commit**

```powershell
git add common\semantic_discovery.h tests\volume_import_tests.cpp docs\superpowers\plans\2026-07-30-asd-v2-local-review-engine.md
git commit -m "feat: add ASD v2 evidence metadata"
```

---

### Task 2: Add Histogram Density Band Planning

**Files:**
- Modify: `common/semantic_discovery.h`
- Modify: `tests/volume_import_tests.cpp`

- [ ] **Step 1: Write failing tests for density bands**

Add this test near the ASD tests:

```cpp
static int test_asd_v2_plans_density_bands_from_histogram(void)
{
    float histogram[8] = { 80.0f, 10.0f, 20.0f, 30.0f, 20.0f, 15.0f, 12.0f, 18.0f };
    VoxelPilotAsdDensityBand bands[VOXELPILOT_ASD_MAX_DENSITY_BANDS];
    int band_count = 0;

    if (!expect_true(
            voxelpilot_asd_plan_density_bands(
                histogram,
                8,
                0.65f,
                bands,
                VOXELPILOT_ASD_MAX_DENSITY_BANDS,
                &band_count) == 1,
            "ASD v2 should plan bands from a populated histogram")) {
        return 0;
    }

    if (!expect_true(
            band_count >= 3,
            "ASD v2 should produce low, mid, and high density bands when histogram supports them")) {
        return 0;
    }

    if (!expect_true(
            strcmp(bands[0].name, "low-density foreground") == 0 &&
            bands[0].threshold_min >= 0.0f &&
            bands[0].threshold_max < bands[1].threshold_max,
            "first ASD v2 band should cover low-density foreground")) {
        return 0;
    }

    return expect_true(
        strcmp(bands[band_count - 1].name, "upper-tail dense") == 0 &&
        bands[band_count - 1].threshold_min >= 0.60f,
        "last ASD v2 band should focus the dense upper tail");
}

static int test_asd_v2_rejects_flat_histogram_bands(void)
{
    float histogram[8] = { 0.0f };
    VoxelPilotAsdDensityBand bands[VOXELPILOT_ASD_MAX_DENSITY_BANDS];
    int band_count = 99;

    histogram[3] = 100.0f;

    if (!expect_true(
            voxelpilot_asd_plan_density_bands(
                histogram,
                8,
                0.50f,
                bands,
                VOXELPILOT_ASD_MAX_DENSITY_BANDS,
                &band_count) == 0,
            "ASD v2 should reject histograms concentrated in one bin")) {
        return 0;
    }

    return expect_true(band_count == 0, "ASD v2 rejected histogram should clear band count");
}
```

Register both in `main()` after the metadata test:

```cpp
passed &= test_asd_v2_plans_density_bands_from_histogram();
passed &= test_asd_v2_rejects_flat_histogram_bands();
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```powershell
.\run_tests_windows.bat
```

Expected: compile fails because `VoxelPilotAsdDensityBand`, `VOXELPILOT_ASD_MAX_DENSITY_BANDS`, and `voxelpilot_asd_plan_density_bands` do not exist.

- [ ] **Step 3: Add density band type and percentile helpers**

In `common/semantic_discovery.h`, add:

```c
#define VOXELPILOT_ASD_MAX_DENSITY_BANDS 4
```

Add this type after `VoxelPilotDensityClass`:

```c
typedef struct {
    char name[VOXELPILOT_ASD_DENSITY_BAND_MAX];
    float threshold_min;
    float threshold_max;
    float percentile_min;
    float percentile_max;
    VoxelPilotDensityClass density_class;
    float seed_priority;
} VoxelPilotAsdDensityBand;
```

Add these helpers before `voxelpilot_asd_density_class()`:

```c
static inline float voxelpilot_asd_histogram_total(const float *histogram, int histogram_bins)
{
    int i;
    float total = 0.0f;
    if (!histogram || histogram_bins < 2) return 0.0f;
    for (i = 0; i < histogram_bins; ++i) {
        if (histogram[i] > 0.0f) total += histogram[i];
    }
    return total;
}

static inline int voxelpilot_asd_histogram_active_bins(const float *histogram, int histogram_bins)
{
    int i;
    int active = 0;
    if (!histogram || histogram_bins < 2) return 0;
    for (i = 0; i < histogram_bins; ++i) {
        if (histogram[i] > 0.0f) active++;
    }
    return active;
}

static inline float voxelpilot_asd_histogram_percentile(
    const float *histogram,
    int histogram_bins,
    float percentile)
{
    int i;
    float total;
    float target;
    float running = 0.0f;

    if (!histogram || histogram_bins < 2) return voxelpilot_asd_clampf(percentile, 0.0f, 1.0f);
    total = voxelpilot_asd_histogram_total(histogram, histogram_bins);
    if (total <= 0.0f) return voxelpilot_asd_clampf(percentile, 0.0f, 1.0f);
    target = total * voxelpilot_asd_clampf(percentile, 0.0f, 1.0f);
    for (i = 0; i < histogram_bins; ++i) {
        if (histogram[i] > 0.0f) running += histogram[i];
        if (running >= target) {
            return (float)i / (float)(histogram_bins - 1);
        }
    }
    return 1.0f;
}

static inline void voxelpilot_asd_write_band(
    VoxelPilotAsdDensityBand *band,
    const char *name,
    float threshold_min,
    float threshold_max,
    float percentile_min,
    float percentile_max,
    VoxelPilotDensityClass density_class,
    float seed_priority)
{
    if (!band) return;
    memset(band, 0, sizeof(*band));
    snprintf(band->name, sizeof(band->name), "%s", name ? name : "unknown");
    band->threshold_min = voxelpilot_asd_clampf(threshold_min, 0.0f, 1.0f);
    band->threshold_max = voxelpilot_asd_clampf(threshold_max, band->threshold_min, 1.0f);
    band->percentile_min = voxelpilot_asd_clampf(percentile_min, 0.0f, 1.0f);
    band->percentile_max = voxelpilot_asd_clampf(percentile_max, band->percentile_min, 1.0f);
    band->density_class = density_class;
    band->seed_priority = seed_priority;
}

static inline int voxelpilot_asd_plan_density_bands(
    const float *histogram,
    int histogram_bins,
    float sensitivity,
    VoxelPilotAsdDensityBand *bands,
    int max_bands,
    int *out_band_count)
{
    float p18;
    float p45;
    float p70;
    float p88;
    float p97;
    int count = 0;

    if (out_band_count) *out_band_count = 0;
    if (!histogram || histogram_bins < 4 || !bands || !out_band_count || max_bands < 1) return 0;
    if (voxelpilot_asd_histogram_total(histogram, histogram_bins) <= 0.0f) return 0;
    if (voxelpilot_asd_histogram_active_bins(histogram, histogram_bins) < 3) return 0;

    sensitivity = voxelpilot_asd_clampf(sensitivity, 0.0f, 1.0f);
    p18 = voxelpilot_asd_histogram_percentile(histogram, histogram_bins, 0.18f);
    p45 = voxelpilot_asd_histogram_percentile(histogram, histogram_bins, 0.45f);
    p70 = voxelpilot_asd_histogram_percentile(histogram, histogram_bins, 0.70f);
    p88 = voxelpilot_asd_histogram_percentile(histogram, histogram_bins, 0.88f);
    p97 = voxelpilot_asd_histogram_percentile(histogram, histogram_bins, 0.97f);

    if (count < max_bands && p45 > p18 + 0.03f) {
        voxelpilot_asd_write_band(&bands[count++], "low-density foreground", fmaxf(0.02f, p18), p45, 0.18f, 0.45f, VOXELPILOT_ASD_DENSITY_LOW, 0.35f);
    }
    if (count < max_bands && p70 > p45 + 0.03f) {
        voxelpilot_asd_write_band(&bands[count++], "mid-density material", p45, p70, 0.45f, 0.70f, VOXELPILOT_ASD_DENSITY_MID, 0.55f);
    }
    if (count < max_bands && p88 > p70 + 0.03f) {
        voxelpilot_asd_write_band(&bands[count++], "high-density material", p70, fmaxf(p88, p70 + 0.04f), 0.70f, 0.88f, VOXELPILOT_ASD_DENSITY_HIGH, 0.75f);
    }
    if (count < max_bands) {
        float tail_min = voxelpilot_asd_clampf(p88 - sensitivity * 0.05f, 0.55f, 0.98f);
        voxelpilot_asd_write_band(&bands[count++], "upper-tail dense", tail_min, 1.0f, 0.88f, 0.97f, VOXELPILOT_ASD_DENSITY_HIGH, 0.95f);
    }

    *out_band_count = count;
    return count > 0 ? 1 : 0;
}
```

- [ ] **Step 4: Run tests to verify pass**

Run:

```powershell
.\run_tests_windows.bat
```

Expected: tests pass.

- [ ] **Step 5: Commit**

```powershell
git add common\semantic_discovery.h tests\volume_import_tests.cpp docs\superpowers\plans\2026-07-30-asd-v2-local-review-engine.md
git commit -m "feat: plan ASD v2 density bands from histogram"
```

---

### Task 3: Upgrade Discovery To Use Density Bands And Evidence

**Files:**
- Modify: `common/semantic_discovery.h`
- Modify: `tests/volume_import_tests.cpp`

- [ ] **Step 1: Write failing tests for multi-band discovery and evidence**

Add these tests near the existing ASD discovery tests:

```cpp
static int test_asd_v2_discovers_mid_and_high_density_regions(void)
{
    float volume[64] = { 0.02f };
    float histogram[8] = { 40.0f, 12.0f, 8.0f, 18.0f, 22.0f, 8.0f, 4.0f, 10.0f };
    unsigned char label_mask[64] = { 0 };
    VoxelPilotSemanticRegion regions[VOXELPILOT_ASD_MAX_REGIONS];
    int region_count = 0;
    int i;
    int saw_mid = 0;
    int saw_high = 0;

    volume[1 + 1 * 4 + 1 * 16] = 0.48f;
    volume[2 + 1 * 4 + 1 * 16] = 0.50f;
    volume[1 + 2 * 4 + 1 * 16] = 0.47f;
    volume[2 + 2 * 4 + 1 * 16] = 0.52f;

    volume[1 + 1 * 4 + 2 * 16] = 0.91f;
    volume[2 + 1 * 4 + 2 * 16] = 0.93f;
    volume[1 + 2 * 4 + 2 * 16] = 0.89f;
    volume[2 + 2 * 4 + 2 * 16] = 0.94f;

    if (!expect_true(
            voxelpilot_discover_semantic_regions(
                volume,
                4,
                4,
                4,
                histogram,
                8,
                6,
                0.65f,
                3,
                label_mask,
                regions,
                &region_count) == 1,
            "ASD v2 should discover regions from populated density bands")) {
        return 0;
    }

    for (i = 0; i < region_count; ++i) {
        if (strcmp(regions[i].density_band, "mid-density material") == 0) saw_mid = 1;
        if (strcmp(regions[i].density_band, "upper-tail dense") == 0 ||
            strcmp(regions[i].density_band, "high-density material") == 0) saw_high = 1;
    }

    if (!expect_true(saw_mid == 1, "ASD v2 should keep a mid-density candidate")) return 0;
    if (!expect_true(saw_high == 1, "ASD v2 should keep a high-density candidate")) return 0;
    return expect_true(
        strstr(regions[0].evidence_summary, "band") != NULL &&
        regions[0].threshold_max > regions[0].threshold_min,
        "ASD v2 regions should include evidence and thresholds");
}

static int test_asd_v2_location_descriptor_names_quadrant(void)
{
    VoxelPilotSemanticRegion region;
    memset(&region, 0, sizeof(region));
    voxelpilot_asd_region_init_v2_metadata(&region);
    region.centroid[0] = 7.0f;
    region.centroid[1] = 1.0f;
    region.centroid[2] = 6.0f;

    voxelpilot_asd_update_location_descriptor(&region, 8, 8, 8);

    return expect_true(
        strstr(region.location_descriptor, "right") != NULL &&
        strstr(region.location_descriptor, "upper") != NULL &&
        strstr(region.location_descriptor, "back") != NULL,
        "ASD v2 location descriptor should summarize normalized position");
}
```

Register both after existing ASD discovery tests:

```cpp
passed &= test_asd_v2_discovers_mid_and_high_density_regions();
passed &= test_asd_v2_location_descriptor_names_quadrant();
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```powershell
.\run_tests_windows.bat
```

Expected: compile fails because `voxelpilot_asd_update_location_descriptor` does not exist, and discovery still ignores histogram bands.

- [ ] **Step 3: Add location and evidence helpers**

Add these helpers before `voxelpilot_asd_compute_scores()`:

```c
static inline const char *voxelpilot_asd_axis_word(float value, const char *low, const char *mid, const char *high)
{
    if (value < 0.34f) return low;
    if (value > 0.66f) return high;
    return mid;
}

static inline void voxelpilot_asd_update_location_descriptor(
    VoxelPilotSemanticRegion *region,
    int width,
    int height,
    int depth)
{
    float nx;
    float ny;
    float nz;
    if (!region || width < 1 || height < 1 || depth < 1) return;
    nx = width > 1 ? region->centroid[0] / (float)(width - 1) : 0.5f;
    ny = height > 1 ? region->centroid[1] / (float)(height - 1) : 0.5f;
    nz = depth > 1 ? region->centroid[2] / (float)(depth - 1) : 0.5f;
    snprintf(region->location_descriptor,
             sizeof(region->location_descriptor),
             "%s-%s-%s volume zone",
             voxelpilot_asd_axis_word(nx, "left", "central", "right"),
             voxelpilot_asd_axis_word(ny, "lower", "middle", "upper"),
             voxelpilot_asd_axis_word(nz, "front", "middle", "back"));
}

static inline void voxelpilot_asd_write_evidence_summary(
    VoxelPilotSemanticRegion *region)
{
    if (!region) return;
    snprintf(region->evidence_summary,
             sizeof(region->evidence_summary),
             "%s band %.3f-%.3f, intensity %.3f-%.3f, fill %.0f%%, boundary %.0f%%, stability %.0f%%.",
             region->density_band[0] ? region->density_band : "unknown",
             region->threshold_min,
             region->threshold_max,
             region->min_intensity,
             region->max_intensity,
             region->fill_ratio * 100.0f,
             region->boundary_strength * 100.0f,
             region->stability_score * 100.0f);
}
```

Update `voxelpilot_asd_compute_scores()` so `fill_ratio` mirrors compactness:

```c
    region->compactness = voxelpilot_asd_clampf((float)region->voxel_count / (float)bbox_volume, 0.0f, 1.0f);
    region->fill_ratio = region->compactness;
```

- [ ] **Step 4: Refactor discovery into band-aware scanning**

Inside `voxelpilot_discover_semantic_regions()`, replace the current single-threshold setup with band planning:

```c
    VoxelPilotAsdDensityBand bands[VOXELPILOT_ASD_MAX_DENSITY_BANDS];
    int band_count = 0;
    int band_index;
```

Remove `(void)histogram;` and `(void)histogram_bins;`.

After `min_region_voxels` is clamped, replace the single `threshold` calculation with:

```c
    if (!voxelpilot_asd_plan_density_bands(
            histogram,
            histogram_bins,
            sensitivity,
            bands,
            VOXELPILOT_ASD_MAX_DENSITY_BANDS,
            &band_count)) {
        bands[0].threshold_min = voxelpilot_asd_clampf(0.70f - sensitivity * 0.25f, 0.35f, 0.85f);
        bands[0].threshold_max = 1.0f;
        bands[0].percentile_min = 0.70f;
        bands[0].percentile_max = 1.0f;
        bands[0].density_class = VOXELPILOT_ASD_DENSITY_HIGH;
        bands[0].seed_priority = 0.70f;
        snprintf(bands[0].name, sizeof(bands[0].name), "upper-tail dense");
        band_count = 1;
    }
```

Wrap the current `for (start_index = 0; ...)` loop in:

```c
    for (band_index = 0; band_index < band_count && region_count < clamped_max_regions; ++band_index) {
        const VoxelPilotAsdDensityBand *band = &bands[band_index];
        memset(visited, 0, voxel_count);
        for (start_index = 0; start_index < voxel_count && region_count < clamped_max_regions; ++start_index) {
            ...
        }
    }
```

Inside the loop, replace the start filter:

```c
        if (visited[start] || volume_data[start] < threshold) {
            continue;
        }
```

with:

```c
        if (visited[start] ||
            volume_data[start] < band->threshold_min ||
            volume_data[start] > band->threshold_max ||
            label_mask[start] != 0) {
            continue;
        }
```

Inside neighbor growth, replace:

```c
                if (!visited[neighbor] && volume_data[neighbor] >= threshold) {
                    visited[neighbor] = 1;
                    queue[tail++] = neighbor;
                }
```

with:

```c
                if (!visited[neighbor] &&
                    label_mask[neighbor] == 0 &&
                    volume_data[neighbor] >= band->threshold_min &&
                    volume_data[neighbor] <= band->threshold_max &&
                    fabsf(volume_data[neighbor] - value) <= fmaxf(0.12f, 0.28f - sensitivity * 0.12f)) {
                    visited[neighbor] = 1;
                    queue[tail++] = neighbor;
                }
```

After `region->mean_intensity` is set, add:

```c
            region->density_class = band->density_class;
            snprintf(region->density_band, sizeof(region->density_band), "%s", band->name);
            region->threshold_min = band->threshold_min;
            region->threshold_max = band->threshold_max;
            region->intensity_percentile_min = band->percentile_min;
            region->intensity_percentile_max = band->percentile_max;
            voxelpilot_asd_update_location_descriptor(region, width, height, depth);
            voxelpilot_asd_region_init_v2_metadata(region);
            region->source = VOXELPILOT_SEMANTIC_SOURCE_HEURISTIC;
            snprintf(region->density_band, sizeof(region->density_band), "%s", band->name);
            region->threshold_min = band->threshold_min;
            region->threshold_max = band->threshold_max;
            region->intensity_percentile_min = band->percentile_min;
            region->intensity_percentile_max = band->percentile_max;
```

Keep `voxelpilot_asd_compute_scores(region, voxel_count);`, then after naming add:

```c
            voxelpilot_asd_write_evidence_summary(region);
```

If the duplicate metadata initialization looks redundant during implementation, keep the order that preserves `label_id`, `voxel_count`, centroid, bbox, intensity, and source. The final code must initialize v2 strings before writing density-band evidence.

- [ ] **Step 5: Run tests to verify pass**

Run:

```powershell
.\run_tests_windows.bat
```

Expected: tests pass and older high-density ASD tests remain green through the fallback band path.

- [ ] **Step 6: Commit**

```powershell
git add common\semantic_discovery.h tests\volume_import_tests.cpp docs\superpowers\plans\2026-07-30-asd-v2-local-review-engine.md
git commit -m "feat: discover ASD v2 regions from density bands"
```

---

### Task 4: Improve Archetypes, Names, Descriptions, And Safety Terms

**Files:**
- Modify: `common/semantic_discovery.h`
- Modify: `tests/volume_import_tests.cpp`

- [ ] **Step 1: Write failing tests for richer descriptions and forbidden terms**

Add:

```cpp
static int test_asd_v2_generates_evidence_rich_safe_description(void)
{
    VoxelPilotSemanticRegion region;
    memset(&region, 0, sizeof(region));
    voxelpilot_asd_region_init_v2_metadata(&region);
    region.label_id = 1;
    region.source = VOXELPILOT_SEMANTIC_SOURCE_HEURISTIC;
    region.density_class = VOXELPILOT_ASD_DENSITY_HIGH;
    region.mean_intensity = 0.88f;
    region.min_intensity = 0.80f;
    region.max_intensity = 0.96f;
    region.compactness = 0.38f;
    region.fill_ratio = 0.38f;
    region.elongation = 1.20f;
    region.boundary_strength = 0.82f;
    region.stability_score = 0.77f;
    region.importance_rank = 2;
    snprintf(region.archetype, sizeof(region.archetype), "curved shell");
    snprintf(region.density_band, sizeof(region.density_band), "upper-tail dense");
    snprintf(region.location_descriptor, sizeof(region.location_descriptor), "right-upper-back volume zone");
    snprintf(region.evidence_summary, sizeof(region.evidence_summary), "upper-tail dense band 0.820-1.000");

    voxelpilot_asd_assign_name_and_description(&region);

    if (!expect_true(
            strstr(region.name, "high-density curved shell candidate") != NULL,
            "ASD v2 should produce a specific cautious shell label")) {
        return 0;
    }
    if (!expect_true(
            strstr(region.description, "right-upper-back") != NULL &&
            strstr(region.description, "upper-tail dense") != NULL &&
            strstr(region.description, "local heuristic") != NULL,
            "ASD v2 description should include location, density band, and provenance")) {
        return 0;
    }
    return expect_true(
        strstr(region.description, "tumor") == NULL &&
        strstr(region.description, "artery") == NULL &&
        strstr(region.description, "tibia") == NULL,
        "heuristic ASD v2 descriptions must not invent diagnostic or anatomy terms");
}
```

Register it after archetype tests:

```cpp
passed &= test_asd_v2_generates_evidence_rich_safe_description();
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```powershell
.\run_tests_windows.bat
```

Expected: fails because the current description does not include ASD v2 evidence.

- [ ] **Step 3: Add richer archetype assignment**

Update `voxelpilot_asd_assign_archetype()` with these branches before the existing `rod`/`plate`/`shell` checks:

```c
    if (region->density_class == VOXELPILOT_ASD_DENSITY_LOW &&
        region->compactness < 0.30f &&
        bbox_volume >= 16) {
        snprintf(region->archetype, sizeof(region->archetype), "cavity");
    } else if (region->elongation >= 3.5f && region->compactness < 0.55f) {
        snprintf(region->archetype, sizeof(region->archetype), "branching rod");
    } else if (region->density_class == VOXELPILOT_ASD_DENSITY_HIGH &&
               region->compactness < 0.45f &&
               bbox_volume >= 16 &&
               region->boundary_strength >= 0.55f) {
        snprintf(region->archetype, sizeof(region->archetype), "curved shell");
    } else if (region->compactness >= 0.70f) {
        snprintf(region->archetype, sizeof(region->archetype), "compact core");
```

Keep the existing remaining branches after these checks so old tests that expect `rod`, `plate`, `shell`, `blob`, and `cluster` still have a path.

- [ ] **Step 4: Replace name/description generator**

Replace `voxelpilot_asd_assign_name_and_description()` with:

```c
static inline void voxelpilot_asd_assign_name_and_description(VoxelPilotSemanticRegion *region)
{
    const char *density = "mid-density";
    const char *source_label;

    if (!region) return;

    if (!region->provider_name[0]) {
        voxelpilot_asd_region_init_v2_metadata(region);
    }

    if (region->density_class == VOXELPILOT_ASD_DENSITY_LOW) {
        density = "low-density";
        snprintf(region->category, sizeof(region->category), "low-density candidate");
    } else if (region->density_class == VOXELPILOT_ASD_DENSITY_HIGH) {
        density = "high-density";
        snprintf(region->category, sizeof(region->category), "high-density candidate");
    } else {
        snprintf(region->category, sizeof(region->category), "mid-density candidate");
    }

    if (region->source == VOXELPILOT_SEMANTIC_SOURCE_HEURISTIC) {
        snprintf(region->name,
                 sizeof(region->name),
                 "%s %s candidate",
                 density,
                 region->archetype[0] && strcmp(region->archetype, "unclassified") != 0 ? region->archetype : "connected region");
        snprintf(region->name_policy, sizeof(region->name_policy), "cautious_descriptor");
    }

    source_label = voxelpilot_semantic_source_label(region->source);
    snprintf(region->description,
             sizeof(region->description),
             "ASD-v2 grouped this as a %s in the %s. Evidence: %s Ranked #%d by local size, boundary, density, and stability cues. Source: %s from %s; this is not anatomy or diagnosis.",
             region->name[0] ? region->name : "candidate region",
             region->location_descriptor[0] ? region->location_descriptor : "unknown volume location",
             region->evidence_summary[0] ? region->evidence_summary : "local connected-component evidence unavailable.",
             region->importance_rank > 0 ? region->importance_rank : 0,
             source_label,
             region->provider_name[0] ? region->provider_name : "unknown provider");
}
```

Update `voxelpilot_asd_apply_correction()` so user-confirmed names set the policy:

```c
        snprintf(region->name_policy, sizeof(region->name_policy), "user_label");
```

for confirmed corrections, and:

```c
        snprintf(region->name_policy, sizeof(region->name_policy), "user_rejected");
```

for rejected corrections.

- [ ] **Step 5: Run tests to verify pass**

Run:

```powershell
.\run_tests_windows.bat
```

Expected: tests pass.

- [ ] **Step 6: Commit**

```powershell
git add common\semantic_discovery.h tests\volume_import_tests.cpp docs\superpowers\plans\2026-07-30-asd-v2-local-review-engine.md
git commit -m "feat: add ASD v2 evidence descriptions"
```

---

### Task 5: Export ASD v2 Evidence And Provenance

**Files:**
- Modify: `common/ai_review_bundle.h`
- Modify: `tests/volume_import_tests.cpp`

- [ ] **Step 1: Update failing export test**

In `test_ai_review_bundle_writes_regions_json()`, seed the region with ASD v2 fields before writing:

```cpp
    snprintf(region.provider_name, sizeof(region.provider_name), "ASD-v2 local heuristic");
    snprintf(region.provider_version, sizeof(region.provider_version), "2.0-local");
    snprintf(region.name_policy, sizeof(region.name_policy), "cautious_descriptor");
    snprintf(region.density_band, sizeof(region.density_band), "upper-tail dense");
    snprintf(region.location_descriptor, sizeof(region.location_descriptor), "central-middle-back volume zone");
    snprintf(region.evidence_summary, sizeof(region.evidence_summary), "upper-tail dense band 0.820-1.000");
    region.intensity_percentile_min = 0.88f;
    region.intensity_percentile_max = 0.97f;
    region.threshold_min = 0.82f;
    region.threshold_max = 1.0f;
    region.fill_ratio = 0.42f;
```

Add assertions after reading the JSON:

```cpp
    if (!expect_true(
            strstr(text, "\"providerName\": \"ASD-v2 local heuristic\"") != NULL,
            "regions JSON should include ASD v2 provider name")) {
        return 0;
    }
    if (!expect_true(
            strstr(text, "\"densityBand\": \"upper-tail dense\"") != NULL,
            "regions JSON should include ASD v2 density band")) {
        return 0;
    }
    if (!expect_true(
            strstr(text, "\"evidenceSummary\": \"upper-tail dense band") != NULL,
            "regions JSON should include ASD v2 evidence summary")) {
        return 0;
    }
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```powershell
.\run_tests_windows.bat
```

Expected: export test fails because the JSON writer does not include the new fields.

- [ ] **Step 3: Write new JSON fields**

In `voxelpilot_ai_review_write_regions_json()`, after `"category"` output, add:

```c
        fprintf(fp, "\", \"source\": \"");
        voxelpilot_json_write_escaped(fp, voxelpilot_semantic_source_label(region->source));
        fprintf(fp, "\", \"providerName\": \"");
        voxelpilot_json_write_escaped(fp, region->provider_name);
        fprintf(fp, "\", \"providerVersion\": \"");
        voxelpilot_json_write_escaped(fp, region->provider_version);
        fprintf(fp, "\", \"namePolicy\": \"");
        voxelpilot_json_write_escaped(fp, region->name_policy);
        fprintf(fp, "\", \"densityBand\": \"");
        voxelpilot_json_write_escaped(fp, region->density_band);
        fprintf(fp, "\", \"locationDescriptor\": \"");
        voxelpilot_json_write_escaped(fp, region->location_descriptor);
        fprintf(fp, "\", \"evidenceSummary\": \"");
        voxelpilot_json_write_escaped(fp, region->evidence_summary);
```

Then extend the numeric `fprintf` format so it includes:

```c
                "\", \"intensityPercentileMin\": %.6f, \"intensityPercentileMax\": %.6f, \"thresholdMin\": %.6f, \"thresholdMax\": %.6f, \"fillRatio\": %.6f, \"voxelCount\": %zu, \"confidence\": %.6f, \"stabilityScore\": %.6f, \"importanceScore\": %.6f, \"importanceRank\": %d, \"meanIntensity\": %.6f, \"minIntensity\": %.6f, \"maxIntensity\": %.6f, \"centroid\": [%.6f, %.6f, %.6f], \"bboxMin\": [%d, %d, %d], \"bboxMax\": [%d, %d, %d], \"userConfirmed\": %d, \"rejected\": %d, \"description\": \"",
                region->intensity_percentile_min,
                region->intensity_percentile_max,
                region->threshold_min,
                region->threshold_max,
                region->fill_ratio,
```

Keep the existing remaining arguments after `region->fill_ratio` in their current order.

- [ ] **Step 4: Update HTML bundle report**

In `voxelpilot_ai_review_write_bundle_report()`, add a row to the regions table or region detail output:

```c
        fputs("<tr><th>ASD v2 evidence</th><td>", fp);
        voxelpilot_html_write_escaped(fp, region->evidence_summary);
        fputs("</td></tr>\n", fp);
        fputs("<tr><th>Provenance</th><td>", fp);
        voxelpilot_html_write_escaped(fp, voxelpilot_semantic_source_label(region->source));
        fputs(" / ", fp);
        voxelpilot_html_write_escaped(fp, region->provider_name);
        fputs("</td></tr>\n", fp);
```

Place it inside the existing region output loop so each region carries evidence in the report.

- [ ] **Step 5: Run tests to verify pass**

Run:

```powershell
.\run_tests_windows.bat
```

Expected: tests pass.

- [ ] **Step 6: Commit**

```powershell
git add common\ai_review_bundle.h tests\volume_import_tests.cpp docs\superpowers\plans\2026-07-30-asd-v2-local-review-engine.md
git commit -m "feat: export ASD v2 evidence metadata"
```

---

### Task 6: Add Natural Review Prompt Intents

**Files:**
- Modify: `common/ai_prompt.h`
- Modify: `tests/volume_import_tests.cpp`

- [ ] **Step 1: Write failing prompt intent tests**

Add:

```cpp
static int test_prompt_parser_recognizes_smart_review_intents(void)
{
    VoxelPilotPromptResult result;

    if (!expect_true(
            voxelpilot_parse_prompt_action("find important structures", &result) == 1,
            "prompt parser should recognize Smart Review intent")) {
        return 0;
    }
    if (!expect_true(
            result.workflow_action == VOXELPILOT_PROMPT_WORKFLOW_SMART_REVIEW,
            "find important structures should request Smart Review")) {
        return 0;
    }

    if (!expect_true(
            voxelpilot_parse_prompt_action("explain region 2", &result) == 1,
            "prompt parser should recognize region explanation intent")) {
        return 0;
    }
    if (!expect_true(
            result.workflow_action == VOXELPILOT_PROMPT_WORKFLOW_EXPLAIN_REGION &&
            result.region_number == 2,
            "explain region prompt should store requested region number")) {
        return 0;
    }

    if (!expect_true(
            voxelpilot_parse_prompt_action("show the densest shell", &result) == 1,
            "prompt parser should recognize densest shell intent")) {
        return 0;
    }
    return expect_true(
        result.workflow_action == VOXELPILOT_PROMPT_WORKFLOW_SELECT_DENSEST_SHELL,
        "densest shell prompt should select the matching ASD candidate workflow");
}
```

Register it after existing prompt parser tests:

```cpp
passed &= test_prompt_parser_recognizes_smart_review_intents();
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```powershell
.\run_tests_windows.bat
```

Expected: compile fails because workflow action constants and fields do not exist.

- [ ] **Step 3: Add workflow constants and fields**

In `common/ai_prompt.h`, add after render quality constants:

```c
#define VOXELPILOT_PROMPT_WORKFLOW_NONE                 0
#define VOXELPILOT_PROMPT_WORKFLOW_SMART_REVIEW         1
#define VOXELPILOT_PROMPT_WORKFLOW_DISCOVER_REGIONS     2
#define VOXELPILOT_PROMPT_WORKFLOW_EXPLAIN_REGION       3
#define VOXELPILOT_PROMPT_WORKFLOW_SELECT_DENSEST_SHELL 4
#define VOXELPILOT_PROMPT_WORKFLOW_COMPARE_TOP_REGIONS  5
#define VOXELPILOT_PROMPT_WORKFLOW_HIDE_WEAK_REGIONS    6
#define VOXELPILOT_PROMPT_WORKFLOW_REVIEW_NEXT_REGION   7
#define VOXELPILOT_PROMPT_WORKFLOW_EXPORT_BUNDLE        8
```

Extend `VoxelPilotPromptResult` after `quality_mode`:

```c
    int   workflow_action;
    int   region_number;
```

Update `voxelpilot_init_prompt_result()`:

```c
    result->workflow_action = VOXELPILOT_PROMPT_WORKFLOW_NONE;
    result->region_number = 0;
```

- [ ] **Step 4: Parse workflow intents before transfer-density terms**

In `voxelpilot_parse_prompt_action()`, add term arrays:

```c
    static const char *const smart_review_terms[] = {
        "smart review", "find important structures", "review this volume", "analyze important regions"
    };
    static const char *const discover_region_terms[] = {
        "discover regions", "run asd", "adaptive semantic discovery"
    };
    static const char *const explain_region_terms[] = {
        "explain region", "describe region"
    };
    static const char *const compare_region_terms[] = {
        "compare top regions", "compare regions"
    };
    static const char *const weak_region_terms[] = {
        "hide weak regions", "hide unstable regions", "show only stable regions"
    };
    static const char *const review_next_terms[] = {
        "review next region", "next region"
    };
    static const char *const export_bundle_terms[] = {
        "export ai bundle", "export review bundle"
    };
```

After `mentions_high_cut` is computed and before quality-mode checks, add:

```c
    if (voxelpilot_ai_text_contains_any(text, smart_review_terms, (int)(sizeof(smart_review_terms) / sizeof(smart_review_terms[0])))) {
        result->recognized = 1;
        result->workflow_action = VOXELPILOT_PROMPT_WORKFLOW_SMART_REVIEW;
        snprintf(result->status, sizeof(result->status), "Prompt requested Smart Review.");
        return 1;
    }
    if (voxelpilot_ai_text_contains_any(text, discover_region_terms, (int)(sizeof(discover_region_terms) / sizeof(discover_region_terms[0])))) {
        result->recognized = 1;
        result->workflow_action = VOXELPILOT_PROMPT_WORKFLOW_DISCOVER_REGIONS;
        snprintf(result->status, sizeof(result->status), "Prompt requested ASD-v2 discovery.");
        return 1;
    }
    if (voxelpilot_ai_text_contains_any(text, explain_region_terms, (int)(sizeof(explain_region_terms) / sizeof(explain_region_terms[0])))) {
        result->recognized = 1;
        result->workflow_action = VOXELPILOT_PROMPT_WORKFLOW_EXPLAIN_REGION;
        result->region_number = has_number ? (int)(value + 0.5f) : 1;
        if (result->region_number < 1) result->region_number = 1;
        snprintf(result->status, sizeof(result->status), "Prompt requested evidence for region %d.", result->region_number);
        return 1;
    }
    if (strstr(text, "densest shell") || strstr(text, "dense shell")) {
        result->recognized = 1;
        result->workflow_action = VOXELPILOT_PROMPT_WORKFLOW_SELECT_DENSEST_SHELL;
        snprintf(result->status, sizeof(result->status), "Prompt requested the densest shell candidate.");
        return 1;
    }
    if (voxelpilot_ai_text_contains_any(text, compare_region_terms, (int)(sizeof(compare_region_terms) / sizeof(compare_region_terms[0])))) {
        result->recognized = 1;
        result->workflow_action = VOXELPILOT_PROMPT_WORKFLOW_COMPARE_TOP_REGIONS;
        snprintf(result->status, sizeof(result->status), "Prompt requested a comparison of top ASD regions.");
        return 1;
    }
    if (voxelpilot_ai_text_contains_any(text, weak_region_terms, (int)(sizeof(weak_region_terms) / sizeof(weak_region_terms[0])))) {
        result->recognized = 1;
        result->workflow_action = VOXELPILOT_PROMPT_WORKFLOW_HIDE_WEAK_REGIONS;
        snprintf(result->status, sizeof(result->status), "Prompt requested hiding weak ASD regions.");
        return 1;
    }
    if (voxelpilot_ai_text_contains_any(text, review_next_terms, (int)(sizeof(review_next_terms) / sizeof(review_next_terms[0])))) {
        result->recognized = 1;
        result->workflow_action = VOXELPILOT_PROMPT_WORKFLOW_REVIEW_NEXT_REGION;
        snprintf(result->status, sizeof(result->status), "Prompt requested the next Guided Review region.");
        return 1;
    }
    if (voxelpilot_ai_text_contains_any(text, export_bundle_terms, (int)(sizeof(export_bundle_terms) / sizeof(export_bundle_terms[0])))) {
        result->recognized = 1;
        result->workflow_action = VOXELPILOT_PROMPT_WORKFLOW_EXPORT_BUNDLE;
        snprintf(result->status, sizeof(result->status), "Prompt requested AI review bundle export.");
        return 1;
    }
```

- [ ] **Step 5: Run tests to verify pass**

Run:

```powershell
.\run_tests_windows.bat
```

Expected: tests pass and existing transfer/quality prompts still pass.

- [ ] **Step 6: Commit**

```powershell
git add common\ai_prompt.h tests\volume_import_tests.cpp docs\superpowers\plans\2026-07-30-asd-v2-local-review-engine.md
git commit -m "feat: parse Smart Review prompt intents"
```

---

### Task 7: Wire Smart Review And Prompt Workflows Into AI Assist

**Files:**
- Modify: `standalone_main.cu`

- [ ] **Step 1: Add function declarations**

Near existing declarations for semantic workflow helpers, add:

```c
static void run_smart_review_workflow(StandaloneApp *app, const char *reason);
static void select_region_by_prompt_number(StandaloneApp *app, int region_number);
static void select_densest_shell_candidate(StandaloneApp *app);
static void summarize_top_region_comparison(StandaloneApp *app);
static void hide_weak_asd_regions(StandaloneApp *app);
static int handle_prompt_workflow_action(StandaloneApp *app, const VoxelPilotPromptResult *result);
```

- [ ] **Step 2: Add Smart Review implementation**

Place after `run_semantic_demo_workflow()`:

```c
static void run_smart_review_workflow(StandaloneApp *app, const char *reason)
{
    const char *label = reason && reason[0] ? reason : "Smart Review";

    if (!app || !app->renderer.volume_data) {
        snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                 "%s needs a loaded volume.", label);
        return;
    }

    update_dataset_health(app);
    refresh_performance_governor(app);
    if (!performance_allows_optional_action(app, "Smart Review", 1, 0)) {
        snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                 "%s paused by Performance Safety: %s",
                 label,
                 app->performance_result.recommendation);
        return;
    }

    if (app->histogram_ready &&
        (app->dataset_health.level == VOXELPILOT_DATASET_HEALTH_BACKGROUND_DOMINANT ||
         app->dataset_health.level == VOXELPILOT_DATASET_HEALTH_LOW_CONTRAST)) {
        apply_auto_enhance_transfer(app, label);
    }

    run_adaptive_semantic_discovery(app);
    refresh_performance_governor(app);

    if (app->semantic_region_count < 1) {
        snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                 "%s ran health, transfer review, and ASD-v2, but found no stable reviewable regions. %s",
                 label,
                 app->semantic_status);
        return;
    }

    start_guided_review(app);
    app->semantic_callouts_enabled = 1;
    app->semantic_callout_selected_only = 1;
    if (app->semantic_callout_opacity < 0.90f) app->semantic_callout_opacity = 0.90f;

    if (app->semantic_selected_region >= 0 && app->semantic_selected_region < app->semantic_region_count) {
        VoxelPilotSemanticRegion *region = &app->semantic_regions[app->semantic_selected_region];
        snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                 "%s found %d candidate region(s). Region #%d is %s with %.0f%% stability. Guided Review is focused now.",
                 label,
                 app->semantic_region_count,
                 region->importance_rank,
                 region->name,
                 region->stability_score * 100.0f);
    }
}
```

If the dataset health enum names differ during implementation, use the exact names from `common/dataset_health.h` and keep the behavior: only auto-enhance when the current health result says visibility is poor.

- [ ] **Step 3: Add prompt workflow handlers**

Place after Smart Review:

```c
static void select_region_by_prompt_number(StandaloneApp *app, int region_number)
{
    int index = region_number - 1;
    if (!app || app->semantic_region_count < 1) {
        snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                 "No ASD-v2 regions are available. Run Smart Review or Discover Regions first.");
        return;
    }
    if (index < 0 || index >= app->semantic_region_count) {
        snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                 "Region %d is outside the current ASD-v2 range of 1-%d.",
                 region_number,
                 app->semantic_region_count);
        return;
    }
    app->semantic_selected_region = index;
    snprintf(app->semantic_edit_name, sizeof(app->semantic_edit_name), "%s", app->semantic_regions[index].name);
    snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
             "Region %d: %s",
             region_number,
             app->semantic_regions[index].description);
}

static void select_densest_shell_candidate(StandaloneApp *app)
{
    int best = -1;
    float best_score = -1.0f;
    int i;
    if (!app || app->semantic_region_count < 1) {
        snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                 "No ASD-v2 regions are available. Run Smart Review or Discover Regions first.");
        return;
    }
    for (i = 0; i < app->semantic_region_count; ++i) {
        VoxelPilotSemanticRegion *region = &app->semantic_regions[i];
        float score;
        if (region->rejected) continue;
        if (region->density_class != VOXELPILOT_ASD_DENSITY_HIGH) continue;
        if (!strstr(region->archetype, "shell")) continue;
        score = region->mean_intensity + region->stability_score + region->importance_score;
        if (score > best_score) {
            best_score = score;
            best = i;
        }
    }
    if (best < 0) {
        snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                 "No high-density shell candidate is available in current ASD-v2 results.");
        return;
    }
    app->semantic_selected_region = best;
    snprintf(app->semantic_edit_name, sizeof(app->semantic_edit_name), "%s", app->semantic_regions[best].name);
    snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
             "Selected densest shell candidate: %s. %s",
             app->semantic_regions[best].name,
             app->semantic_regions[best].evidence_summary);
}

static void summarize_top_region_comparison(StandaloneApp *app)
{
    if (!app || app->semantic_region_count < 1) {
        snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                 "No ASD-v2 regions are available to compare.");
        return;
    }
    if (app->semantic_region_count == 1) {
        snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                 "Only one ASD-v2 region is available: %s. %s",
                 app->semantic_regions[0].name,
                 app->semantic_regions[0].evidence_summary);
        return;
    }
    snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
             "Top regions: #1 %s (%s, %.0f%% stable); #2 %s (%s, %.0f%% stable).",
             app->semantic_regions[0].name,
             app->semantic_regions[0].density_band,
             app->semantic_regions[0].stability_score * 100.0f,
             app->semantic_regions[1].name,
             app->semantic_regions[1].density_band,
             app->semantic_regions[1].stability_score * 100.0f);
}

static void hide_weak_asd_regions(StandaloneApp *app)
{
    int i;
    int hidden = 0;
    if (!app || app->semantic_region_count < 1) {
        snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                 "No ASD-v2 regions are available to filter.");
        return;
    }
    for (i = 0; i < app->semantic_region_count; ++i) {
        VoxelPilotSemanticRegion *region = &app->semantic_regions[i];
        if (!region->rejected &&
            (region->confidence < 0.45f || region->stability_score < 0.45f)) {
            region->rejected = 1;
            hidden++;
        }
    }
    refresh_semantic_annotation_overlays(app);
    snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
             "Hidden %d weak ASD-v2 candidate(s) from callouts and overlays.", hidden);
}

static int handle_prompt_workflow_action(StandaloneApp *app, const VoxelPilotPromptResult *result)
{
    if (!app || !result || result->workflow_action == VOXELPILOT_PROMPT_WORKFLOW_NONE) return 0;
    switch (result->workflow_action) {
        case VOXELPILOT_PROMPT_WORKFLOW_SMART_REVIEW:
            run_smart_review_workflow(app, "Prompt Smart Review");
            return 1;
        case VOXELPILOT_PROMPT_WORKFLOW_DISCOVER_REGIONS:
            run_adaptive_semantic_discovery(app);
            return 1;
        case VOXELPILOT_PROMPT_WORKFLOW_EXPLAIN_REGION:
            select_region_by_prompt_number(app, result->region_number);
            return 1;
        case VOXELPILOT_PROMPT_WORKFLOW_SELECT_DENSEST_SHELL:
            select_densest_shell_candidate(app);
            return 1;
        case VOXELPILOT_PROMPT_WORKFLOW_COMPARE_TOP_REGIONS:
            summarize_top_region_comparison(app);
            return 1;
        case VOXELPILOT_PROMPT_WORKFLOW_HIDE_WEAK_REGIONS:
            hide_weak_asd_regions(app);
            return 1;
        case VOXELPILOT_PROMPT_WORKFLOW_REVIEW_NEXT_REGION:
            start_guided_review(app);
            return 1;
        case VOXELPILOT_PROMPT_WORKFLOW_EXPORT_BUNDLE:
            export_ai_review_bundle(app);
            return 1;
        default:
            return 0;
    }
}
```

- [ ] **Step 4: Call workflow handler from prompt application**

In `apply_ai_prompt_to_app()`, after successful parse and before `result.set_quality`, add:

```c
    if (handle_prompt_workflow_action(app, &result)) {
        return;
    }
```

Update the unrecognized prompt message:

```c
        snprintf(app->ai_assist_status, sizeof(app->ai_assist_status),
                 "Prompt not recognized. Try: Smart Review, find important structures, explain region 2, show densest shell, make it brighter, hide below 0.3, quality mode, or reset view.");
```

- [ ] **Step 5: Add Smart Review button**

In `draw_ai_assist_panel()`, near the ASD buttons, add a primary button before `Discover Regions`:

```c
        if (ImGui::Button("Smart Review")) {
            run_smart_review_workflow(app, "Smart Review");
            refresh_performance_governor(app);
        }
        ImGui::SameLine();
```

Keep the existing `Discover Regions`, `Start Guided Review`, and `Run Semantic Demo` buttons after it.

- [ ] **Step 6: Build to verify app integration**

Run:

```powershell
.\run_tests_windows.bat
.\build_windows.ps1
```

Expected: tests and Windows build pass.

- [ ] **Step 7: Commit**

```powershell
git add standalone_main.cu docs\superpowers\plans\2026-07-30-asd-v2-local-review-engine.md
git commit -m "feat: add Smart Review workflow"
```

---

### Task 8: Update Docs And Demo Verification

**Files:**
- Modify: `README.md`
- Modify: `docs/superpowers/plans/2026-07-30-asd-v2-local-review-engine.md`

- [ ] **Step 1: Update README highlights and demo flow**

In `README.md`, update the Highlights AI bullet to include ASD v2 and Smart Review:

```markdown
- AI Assist prompts, Smart Review, Adaptive Semantic Discovery v2, evidence-rich local region descriptions, local object identification, slice annotations, cinematic callouts, 3D label overlays, hover explanations, and brick-cache telemetry
```

In Quick Demo Flow, add a Smart Review bullet before manual ASD controls:

```markdown
- Click `Smart Review` in `AI Assist` for the preferred local review path: dataset health, transfer advice, ASD v2 discovery, evidence-rich descriptions, selected-region glow, Guided Review, and export-ready metadata.
```

Update the Adaptive Semantic Discovery section with:

```markdown
ASD v2 uses histogram-derived density bands, bounded connected-component discovery, local contrast cues, stability scoring, and source-aware evidence summaries. Labels are more specific than ASD-Lite, such as `high-density curved shell candidate`, but remain cautious unless the name comes from user correction, imported metadata, or a future opt-in local model provider.
```

Update the AI Concept Note with:

```markdown
Smart Review orchestrates local health, transfer, ASD v2, Guided Review, callouts, and export steps without cloud inference. Future model-backed segmentation is represented by explicit provider/provenance fields so model-suggested labels remain separate from heuristic ASD descriptors.
```

- [ ] **Step 2: Run final verification**

Run:

```powershell
.\run_tests_windows.bat
.\build_windows.ps1
.\run_bonsai_asd_verification_windows.bat
```

Expected:

- tests pass;
- Windows build succeeds;
- Bonsai ASD verifier writes `build\bonsai_asd_report.json`;
- Bonsai report contains ASD v2 evidence/provenance fields in region output.

- [ ] **Step 3: Inspect git status**

Run:

```powershell
git status --short
```

Expected: only intended files are modified.

- [ ] **Step 4: Commit docs and final verification updates**

```powershell
git add README.md docs\superpowers\plans\2026-07-30-asd-v2-local-review-engine.md
git commit -m "docs: document ASD v2 Smart Review workflow"
```

---

## Final Review Checklist

- [ ] `common/semantic_discovery.h` contains ASD v2 metadata fields and initializes them safely.
- [ ] `voxelpilot_discover_semantic_regions()` uses histogram-derived density bands when a histogram is available.
- [ ] Existing fallback behavior still discovers high-density connected components when no histogram is provided.
- [ ] Region names are more specific but remain heuristic descriptors.
- [ ] Heuristic descriptions do not contain anatomy, diagnosis, or unsupported identity claims.
- [ ] AI review bundle JSON includes evidence and provenance fields.
- [ ] Prompt parser recognizes Smart Review and ASD review intents.
- [ ] AI Assist exposes Smart Review as the preferred demo action.
- [ ] Smart Review respects Performance Safety.
- [ ] README describes Smart Review, ASD v2, cautious naming, and future model-provider provenance.
- [ ] `.\run_tests_windows.bat` passes.
- [ ] `.\build_windows.ps1` passes.
- [ ] `.\run_bonsai_asd_verification_windows.bat` passes.

