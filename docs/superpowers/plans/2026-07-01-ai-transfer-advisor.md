# AI Transfer Advisor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a lightweight AI Transfer Advisor that recommends, explains, and user-applies visibility settings from the existing histogram without adding model inference or extra render passes.

**Architecture:** Put the advisor core in `common/ai_features.h` beside existing prompt and auto-enhance helpers so it is unit-testable. Reuse `standalone_main.cu` histogram state and transfer sliders; the UI only previews recommendations until the user clicks Apply.

**Tech Stack:** C/C++ header helper, CUDA/OpenGL app state, ImGui UI, existing Windows batch test/build flow.

---

## File Structure

- Modify `common/ai_features.h`: add `VoxelPilotTransferAdvisorResult` and `voxelpilot_compute_transfer_advisor()`.
- Modify `tests/volume_import_tests.cpp`: add tests for dark/sparse histograms, high-density shells, balanced no-op guidance, and registration in `main()`.
- Modify `standalone_main.cu`: add advisor state, compute/update helpers, an `AI Transfer Advisor` UI block, and an apply button.
- Modify `README.md`: document the advisor in the V2 AI workflow.

## Task 1: Core Advisor Tests

- [ ] **Step 1: Write failing tests**

Add tests that call `voxelpilot_compute_transfer_advisor()` with representative histograms and assert `has_recommendation`, recommended transfer values, `action`, and `explanation`.

- [ ] **Step 2: Verify RED**

Run: `.\run_tests_windows.bat`
Expected: compile failure because `VoxelPilotTransferAdvisorResult` and `voxelpilot_compute_transfer_advisor` do not exist.

## Task 2: Core Advisor Implementation

- [ ] **Step 1: Add advisor types**

Add a fixed-size result struct with recommended `tf_center`, `tf_width`, `tf_opacity_scale`, `min_intensity`, `confidence`, `action`, and `explanation` fields.

- [ ] **Step 2: Add histogram classifier**

Use existing percentile and auto-enhance helpers. Detect three bounded cases: sparse/dark import, high-density shell/object, and balanced/healthy volume.

- [ ] **Step 3: Verify GREEN**

Run: `.\run_tests_windows.bat`
Expected: `volume_import_tests: PASS` and Python render regression tests pass.

## Task 3: UI Integration

- [ ] **Step 1: Add app state**

Store the last advisor result and status string in `StandaloneApp`.

- [ ] **Step 2: Add AI Assist controls**

Render `AI Transfer Advisor`, show the recommendation/explanation, and apply only when the user clicks `Apply Advisor Settings`.

- [ ] **Step 3: Guard with performance safety**

Do not add a model pass. Use existing histogram only; if no histogram exists, show a friendly loaded-volume message.

## Task 4: Docs And Verification

- [ ] **Step 1: Update README**

Document the advisor as local explainable AI, not neural recognition.

- [ ] **Step 2: Run verification**

Run: `git diff --check`, `.\run_tests_windows.bat`, and `.\build_windows.ps1`.

- [ ] **Step 3: Commit**

Run: `git add common/ai_features.h tests/volume_import_tests.cpp standalone_main.cu README.md docs/superpowers/plans/2026-07-01-ai-transfer-advisor.md` then `git commit -m "feat: add AI transfer advisor"`.
