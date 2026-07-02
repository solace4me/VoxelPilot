# Dataset Health Check Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a local Dataset Health Check that explains whether an imported volume is healthy, tiny, flat/constant, background-dominant, low-contrast, or large enough to need performance caution.

**Architecture:** Put the diagnostic core in `common/ai_features.h` beside the existing histogram helpers so it is unit-testable and does not depend on UI state. Integrate it into `standalone_main.cu` as a read-only AI Assist/Metadata status block that recommends existing actions such as AI Transfer Advisor, Semantic Demo, or Performance Safety without silently changing render settings.

**Tech Stack:** C/C++ header helper, existing histogram state, ImGui UI, Windows test/build scripts.

---

## File Structure

- Modify `common/ai_features.h`: add `VoxelPilotDatasetHealthResult`, health flags/levels, and `voxelpilot_compute_dataset_health()`.
- Modify `tests/volume_import_tests.cpp`: add tests for tiny volumes, flat histograms, background-dominant histograms, large-volume pressure, and a healthy foot-sized balanced volume.
- Modify `standalone_main.cu`: add health state, update helper, AI Assist health block, Metadata summary, and refresh after imports/startup volume loads.
- Modify `README.md`: document Dataset Health Check in the V2 demo flow and laptop-safe guardrails.

## Task 1: Health Core Tests

- [ ] **Step 1: Write failing tests**

Add tests that call `voxelpilot_compute_dataset_health()` and assert severity, flags, score, summary, and recommendation text.

- [ ] **Step 2: Verify RED**

Run: `.\run_tests_windows.bat`
Expected: compile failure because `VoxelPilotDatasetHealthResult` and `voxelpilot_compute_dataset_health` do not exist.

## Task 2: Health Core Implementation

- [ ] **Step 1: Add result type and flags**

Add levels `OK`, `WATCH`, `ATTENTION`, plus flags for tiny, flat range, background dominant, low contrast, and large volume.

- [ ] **Step 2: Add histogram/dimension diagnostic**

Compute voxel count, estimated bytes, dynamic range, total samples, active-bin ratio, background ratio, and a bounded score from 0 to 100.

- [ ] **Step 3: Verify GREEN**

Run: `.\run_tests_windows.bat`
Expected: `volume_import_tests: PASS` and Python render regression tests pass.

## Task 3: UI Integration

- [ ] **Step 1: Add app state and update helper**

Store the latest health result and status. Refresh after histogram updates and via AI Assist button.

- [ ] **Step 2: Add AI Assist and Metadata blocks**

Show score, severity, summary, recommendation, and flags. Do not apply settings automatically.

## Task 4: Docs And Verification

- [ ] **Step 1: Update README**

Document the health check as local explainable diagnostics, not model recognition.

- [ ] **Step 2: Run verification**

Run: `git diff --check`, `.\run_tests_windows.bat`, and `.\build_windows.ps1`.

- [ ] **Step 3: Commit**

Run: `git add common/ai_features.h tests/volume_import_tests.cpp standalone_main.cu README.md docs/superpowers/plans/2026-07-01-dataset-health-check.md` then `git commit -m "feat: add dataset health check"`.
