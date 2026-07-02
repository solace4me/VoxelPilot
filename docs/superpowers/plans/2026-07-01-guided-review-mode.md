# Guided Review Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Guided Review Mode that walks through top ASD-Lite regions with focused slices, selected-region glow, cinematic callouts, and explainable region cues.

**Architecture:** Add a small testable helper in `common/semantic_discovery.h` to choose the next reviewable region by importance rank while skipping rejected/empty labels. Integrate it into `standalone_main.cu` as a non-modal overlay and AI Assist controls that reuse existing ASD regions, label overlays, and callouts without adding GPU kernels or model inference.

**Tech Stack:** C/C++ header helper, ImGui UI, existing ASD metadata, existing CUDA label overlay and callout paths, Windows test/build scripts.

---

## File Structure

- Modify `common/semantic_discovery.h`: add `VoxelPilotGuidedReviewStep`, region counting, and rank-based navigation helpers.
- Modify `tests/volume_import_tests.cpp`: add RED/GREEN tests for selecting first/top region, skipping rejected regions, and wrapping through reviewable regions.
- Modify `standalone_main.cu`: add guided review app state, start/select helpers, AI Assist buttons, and a floating review overlay.
- Modify `README.md`: document Guided Review Mode in the V2 demo flow and laptop-safe guardrails.

## Task 1: Guided Review Core Tests

- [ ] **Step 1: Write failing tests**

Add tests that call `voxelpilot_guided_review_select_region()` using three synthetic semantic regions with importance ranks and rejected flags.

- [ ] **Step 2: Verify RED**

Run: `.\run_tests_windows.bat`
Expected: compile failure because `VoxelPilotGuidedReviewStep` and `voxelpilot_guided_review_select_region` do not exist.

## Task 2: Guided Review Core

- [ ] **Step 1: Add helper types**

Add a small `VoxelPilotGuidedReviewStep` struct with `region_index`, `label_id`, `step_number`, `step_count`, `title`, and `cue`.

- [ ] **Step 2: Add selection helper**

Select by ascending `importance_rank`, skip rejected regions and label id zero, wrap at the end, and return `0` if no reviewable region exists.

- [ ] **Step 3: Verify GREEN**

Run: `.\run_tests_windows.bat`
Expected: `volume_import_tests: PASS` and Python render regression tests pass.

## Task 3: UI Integration

- [ ] **Step 1: Add app state**

Add `guided_review_visible`, `guided_review_status`, and `guided_review_step` to `StandaloneApp`.

- [ ] **Step 2: Add selection helper**

Selecting a review step updates `semantic_selected_region`, matching annotation selection, slice positions from centroid, selected glow, and selected-only callouts.

- [ ] **Step 3: Add AI Assist controls and overlay**

Add `Start Guided Review`, `Previous Region`, `Next Region`, and `Finish Review` controls. Show name, archetype, confidence, stability, importance, relationship note, and cautious description.

## Task 4: Docs And Verification

- [ ] **Step 1: Update README**

Document Guided Review as local explainable review, not anatomical diagnosis.

- [ ] **Step 2: Run verification**

Run: `git diff --check`, `.\run_tests_windows.bat`, and `.\build_windows.ps1`.

- [ ] **Step 3: Commit**

Run: `git add common/semantic_discovery.h tests/volume_import_tests.cpp standalone_main.cu README.md docs/superpowers/plans/2026-07-01-guided-review-mode.md` then `git commit -m "feat: add guided review mode"`.
