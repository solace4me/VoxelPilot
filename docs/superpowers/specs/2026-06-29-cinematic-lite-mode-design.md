# Phase 1 Design: Cinematic Lite Mode

Date: 2026-06-29
Status: Draft for user review

## Purpose

Phase 1 adds a safe, opt-in Cinematic Lite mode to VoxelPilot. The goal is to make imported volumes easier and more impressive to understand in demos without changing the default renderer, weakening the laptop-friendly selling point, or faking semantic/anatomical knowledge.

The Phase 0 foot render baseline remains the guardrail. Any cinematic change must keep the default foot snapshot passing and must only affect output when Cinematic Lite is enabled.

## Research Gap Target

This phase addresses the gap between technically correct volume rendering and human-readable volume interpretation. Many tools can display a volume, but the rendered object often lacks visual depth cues, material separation, and guided readability for non-expert viewers. Cinematic Lite improves perception first; later phases add semantic labels and AI explanations.

## Recommended Approach

Use a GPU-native, single-pass enhancement inside the existing CUDA raymarcher. Cinematic Lite adds controlled lighting style parameters to the render command:

- rim lighting for silhouette readability;
- warm/cool material tinting for high-density structures;
- gentle contrast/gamma shaping after accumulation;
- optional glow/emission boost for highlighted density bands;
- preservation of the current default path when disabled.

This avoids CPU rendering, avoids heavyweight model inference, and keeps the app suitable for a normal NVIDIA laptop.

## User Experience

Add a `Cinematic Mode` control in the rendering/lighting section of the left Controls docker:

- `Off` is the default.
- `Cinematic Lite` enables the safe visual enhancement.
- Small sliders expose intensity-level tuning only after the mode is enabled: `Rim`, `Glow`, and `Contrast`.
- AI Assist may later map prompts such as `make this cinematic` or `improve depth` to the same mode, but Phase 1 does not depend on prompt work.

The status bar should show when cinematic mode is active so screenshots and demos are explainable.

## Architecture

### Renderer API

Extend `RendererInput` and `VolumeParams` with a compact cinematic parameter block or equivalent scalar fields. The fields should be copied through `apply_render_command` into the device-side `VolumeParams`.

Required state:

- `cinematic_enabled`
- `cinematic_rim_strength`
- `cinematic_glow_strength`
- `cinematic_contrast`

### CUDA Raymarcher

Keep the current Phong path as the baseline. When cinematic mode is enabled:

1. compute rim intensity from view vector and gradient normal;
2. add a bounded rim contribution to the lit color;
3. add a bounded glow contribution for high-opacity samples;
4. apply a final contrast curve to accumulated RGB;
5. clamp output to avoid blown-out screenshots.

No additional volume pass, shadow-ray loop, CPU postprocess, or neural model is included in Phase 1.

### UI State

Add cinematic fields to `StandaloneApp`, initialize them to off/defaults, persist them in `.vpilot` workspace sessions, and pass them into both interactive rendering and CLI snapshot rendering.

### CLI/Snapshot Support

Add snapshot flags only if lightweight:

- `--cinematic-lite`
- optional `--cinematic-rim`, `--cinematic-glow`, `--cinematic-contrast`

This lets Phase 0-style image regression compare default vs cinematic snapshots without opening the GUI.

## Testing Plan

Follow TDD with focused tests before implementation:

1. helper/API tests confirm cinematic defaults are disabled and safe;
2. prompt-free state tests confirm enabling mode preserves legal parameter ranges;
3. default Phase 0 render regression must still pass;
4. cinematic snapshot test must produce a visible image and differ measurably from the default snapshot without exceeding a laptop-safe render-time threshold.

## Acceptance Criteria

- Default renderer output remains protected by `run_render_regression_windows.bat`.
- Cinematic Lite is off by default.
- Cinematic Lite can be toggled from the UI.
- Cinematic parameters are passed to CUDA and affect only enabled renders.
- Snapshot CLI can generate a cinematic PNG for evidence.
- Render time remains bounded; first target is below 1.5x the default foot snapshot render time on the test machine.
- No anatomy/object labels are introduced in this phase unless they come from existing annotations.

## Non-Goals

- No automatic object recognition yet.
- No hover pop-up semantics yet.
- No fake tibia/muscle/vessel labels.
- No out-of-core brick streaming changes.
- No CPU raymarching or CPU/GPU split of the main renderer.
- No global renderer retuning that could break the foot baseline.

## Risks And Mitigations

- Risk: cinematic effects make medical data look misleading. Mitigation: name it as demo/interpretation lighting, keep default diagnostic view one click away.
- Risk: performance drops on laptops. Mitigation: single-pass math only, no secondary shadow rays in Phase 1, render-time regression threshold.
- Risk: default renderer regresses. Mitigation: Phase 0 baseline runs before and after implementation.
- Risk: users confuse cinematic lighting with AI identification. Mitigation: keep labels and AI explanations separate until semantic phases.

## Implementation Order

1. Add tests for cinematic parameter defaults and clamps.
2. Add renderer/API state fields with disabled defaults.
3. Wire UI toggle and snapshot CLI flags.
4. Add CUDA cinematic branch behind the enabled flag.
5. Add cinematic snapshot verification.
6. Run build, unit tests, default render baseline, and cinematic snapshot baseline.

## Self-Review

- No incomplete markers remain.
- Scope is limited to Cinematic Lite, not full semantic cinematic rendering.
- Default renderer preservation is explicit.
- Laptop-safe constraints are explicit.
- Label/object identification is intentionally deferred to later phases.
