# ASD Correction Memory Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Phase 2C human-in-the-loop correction memory so users can rename, confirm, reject, save, load, and reapply ASD-Lite semantic corrections without adding heavy model inference.

**Architecture:** Keep the matching/correction logic in `common/semantic_discovery.h` beside ASD descriptors so it is testable outside the UI. Extend `standalone_main.cu` with small app-state arrays, AI Assist controls, simple local JSON save/load, and overlay refresh logic. The CUDA renderer remains unchanged; confirmed/rejected semantics only affect metadata, label masks, and existing overlay colors.

**Tech Stack:** C/C++ header helpers, ImGui UI, existing Windows test/build scripts, simple JSON writer/reader for VoxelPilot-owned memory files.

---

## File Structure

- Modify `common/semantic_discovery.h`: add `VoxelPilotSemanticCorrection`, capture/match/apply helpers.
- Modify `tests/volume_import_tests.cpp`: add red/green tests for confirmation and rejection matching.
- Modify `standalone_main.cu`: add correction memory app state, selected-region controls, save/load helpers, and overlay refresh after corrections.
- Modify `README.md`: document correction memory and provenance labels.

## Task 1: Correction Core Tests

**Files:**
- Modify: `tests/volume_import_tests.cpp`

- [ ] **Step 1: Write failing tests**

Add tests before `main()`:

```cpp
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
```

Call them from `main()`.

- [ ] **Step 2: Run red**

Run `./run_tests_windows.bat`. Expected compile failure for missing correction type/functions.

## Task 2: Correction Core Implementation

**Files:**
- Modify: `common/semantic_discovery.h`

- [ ] **Step 1: Add types and helpers**

Add `VOXELPILOT_ASD_MAX_CORRECTIONS`, `VoxelPilotSemanticCorrection`, `voxelpilot_asd_capture_correction`, `voxelpilot_asd_region_matches_correction`, `voxelpilot_asd_apply_correction`, and `voxelpilot_asd_apply_corrections`.

- [ ] **Step 2: Run green**

Run `./run_tests_windows.bat`. Expected all tests pass.

## Task 3: UI State and Overlay Refresh

**Files:**
- Modify: `standalone_main.cu`

- [ ] Add correction memory arrays/status/path/edit name to `StandaloneApp`.
- [ ] Initialize them at startup without clearing them during `reset_workspace`.
- [ ] Add `refresh_semantic_annotation_overlays()` and call it after discovery and correction actions.
- [ ] Rejected regions should have their label cleared from `label_mask` and not be copied to annotation overlays.

## Task 4: AI Assist Correction Controls

**Files:**
- Modify: `standalone_main.cu`

- [ ] Under the ASD legend, add selected-region controls: correction name input, `Confirm / Rename`, `Reject`, `Save Memory`, `Load Memory`.
- [ ] Confirm/Rename captures or updates a correction and reapplies overlays.
- [ ] Reject captures a rejected correction, clears the region label, and updates overlays.
- [ ] Save/Load use `voxelpilot_asd_memory.json` by default.

## Task 5: Docs, Verification, Commit

**Files:**
- Modify: `README.md`

- [ ] Document correction memory in the ASD section.
- [ ] Run `./run_tests_windows.bat`, `./build_windows.ps1`, and `./run_render_regression_windows.bat`.
- [ ] Commit locally on `codex/v2-cinematic-ai` with `feat: add ASD correction memory`.

## Self-Review

- Spec coverage: confirms, renames, rejects, saves, loads, reapplies, and preserves local explainable provenance.
- Scope check: no neural model inference, no renderer kernel changes, no global architecture rewrite.
- Type consistency: region/correction functions use existing ASD naming and `VoxelPilotSemanticRegion` fields.
