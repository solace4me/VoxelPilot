# Cinematic Label Callouts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Add polished cinematic callout labels for ASD regions so viewers immediately understand what the AI layer discovered.

**Architecture:** Add a small testable projection helper in `common/ui_state_helpers.h` that maps normalized 3D points to screen pixels using the existing camera. Extend `standalone_main.cu` with toggleable callout state and an ImGui foreground overlay that draws leader lines and translucent cards for the selected/top ASD regions. Keep rendering untouched: callouts are CPU/UI overlay only, so the CUDA raymarch path and laptop performance remain stable.

**Tech Stack:** C/C++ inline helpers, ImGui draw lists, existing ASD metadata, existing Windows test/build/render scripts.

---

## File Structure

- Modify `common/ui_state_helpers.h`: projection helper and callout visibility predicate.
- Modify `tests/volume_import_tests.cpp`: projection and visibility tests.
- Modify `standalone_main.cu`: app state, AI Assist toggles, foreground callout drawing.
- Modify `README.md`: document cinematic callouts as a lightweight UI overlay.

## Task 1: Projection Helper Tests

**Files:**
- Modify `tests/volume_import_tests.cpp`

- [x] Add a failing test that projects a point straight ahead to screen center.
- [x] Add a failing test that rejects points behind the camera.
- [x] Add a test that selected/top-region callout visibility behaves correctly.
- [x] Run `./run_tests_windows.bat` during verification.

## Task 2: Projection Helper Implementation

**Files:**
- Modify `common/ui_state_helpers.h`

- [x] Add `voxelpilot_project_point_to_screen()`.
- [x] Add `voxelpilot_callout_should_show_rank()`.
- [x] Run `./run_tests_windows.bat` and verify all tests pass.

## Task 3: UI State And Controls

**Files:**
- Modify `standalone_main.cu`

- [x] Add callout fields: enabled, top count, selected-only mode, opacity.
- [x] Initialize defaults: enabled, top 3, not selected-only, 0.88 opacity.
- [x] Add AI Assist controls under ASD: `Cinematic Callouts`, `Callout Top Regions`, `Selected Region Only`, `Callout Opacity`.

## Task 4: Foreground Overlay Drawing

**Files:**
- Modify `standalone_main.cu`

- [x] Add `draw_cinematic_semantic_callouts()`.
- [x] Project each non-rejected top/selected ASD region centroid into screen space.
- [x] Draw a small glow dot, leader line, and card with name, archetype, confidence, stability, and source.
- [x] Call it from `draw_gui()` after `draw_main_render_hover_tooltip()`.

## Task 5: Docs, Verification, Commit

**Files:**
- Modify `README.md`

- [x] Document callouts as a lightweight UI overlay.
- [x] Run `./run_tests_windows.bat`, `./build_windows.ps1`, and `./run_render_regression_windows.bat`.
- [x] Commit locally as `feat: add cinematic ASD callouts`.

## Self-Review

- Scope is intentionally UI-only; no CUDA kernels, model inference, or render quality reduction.
- The helper is independently tested; ImGui drawing is smoke-verified through the full build.
- Callouts derive labels from ASD/correction provenance and do not hardcode anatomy.
