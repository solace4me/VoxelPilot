# Phase 3 Design: Performance Safety Governor

Date: 2026-07-01
Status: Draft for user review
Branch: `codex/v2-cinematic-ai`

## Purpose

Phase 3 adds a **Performance Safety Governor** to VoxelPilot v2. The goal is to protect the main selling point: VoxelPilot should remain usable on a normal NVIDIA laptop while still delivering high-quality CUDA rendering, cinematic lighting, ASD-Lite discovery, hover semantics, and review exports.

The governor does not replace the renderer, reduce quality by default, or move the app toward a heavy cloud/model workflow. It is a lightweight local intelligence layer that watches workload pressure and explains safe options before the app becomes slow or unresponsive.

## Research Gap Target

VoxelPilot is addressing a practical research gap in interactive volume visualization: many systems demonstrate high-quality rendering, segmentation, or semantic overlays, but they often assume workstation-class GPUs, known datasets, or offline preprocessing. Users importing unknown raw volumes on laptop hardware need adaptive guidance that balances visual quality, semantic usefulness, memory pressure, and interactivity.

The governor targets this gap by making resource awareness part of the AI-assisted workflow. Instead of silently hanging, degrading quality without explanation, or requiring expert tuning, VoxelPilot should tell the user what is expensive, why it matters, and what safe action preserves interactivity.

## Core Principle

The governor is **advisory first**.

It may recommend:

- `Keep Quality GPU; performance is healthy.`
- `Switch to Interactive GPU while orbiting this large volume.`
- `Delay ASD until rendering stabilizes.`
- `Reduce semantic top-region callouts from 3 to 1.`
- `Use a brick-streaming path when the volume exceeds the safe memory budget.`

It must not silently lower render quality, disable labels, hide regions, or change transfer settings without user action. Automatic changes are allowed only for reversible emergency safety, such as refusing a dangerous allocation or pausing an optional analysis pass before it starts.

## Scope

Phase 3 covers a lightweight local governor integrated into the current single-machine app:

- GPU memory estimate and optional CUDA free-memory telemetry;
- render-time moving average and frame responsiveness status;
- dataset-size and label-mask memory estimate;
- optional ASD and export cost estimate;
- safe recommendations shown in AI Assist and/or Status;
- one-click user actions for safe mode changes;
- scripted verification that existing foot, Bonsai, and headsq render baselines remain intact.

## Non-Goals

- No rewrite of the CUDA raymarcher.
- No automatic quality reduction during normal use.
- No full out-of-core brick streaming implementation in this phase.
- No neural model inference.
- No CPU raymarching fallback.
- No removal of cinematic mode, ASD-Lite, labels, or review bundles.
- No diagnostic/medical performance claims.

## Recommended Approach

Use a small CPU-side heuristic controller that reads already available application state and, when available, CUDA memory telemetry. The GPU continues to do the heavy rendering work. The CPU handles supervision: estimating pressure, deciding whether optional work is safe, and producing explanations.

This is a CPU/GPU split that strengthens the app rather than complicating it:

- GPU: raymarching, gradients, label overlay, cinematic shading, snapshots.
- CPU: histogram review, ASD-Lite orchestration, memory estimates, render-time trend analysis, recommendation text.

The first implementation should be conservative and non-invasive. It can live beside the existing AI Assist and Status systems. Later phases can use the same signals to guide brick streaming or more advanced schedulers.

## Governor Inputs

The governor should evaluate compact signals that are already available or cheap to compute:

### Volume Pressure

- dimensions: `W x H x D`;
- voxel count;
- imported data type;
- estimated normalized volume memory;
- gradient texture memory estimate;
- label mask memory estimate;
- render target memory estimate.

### GPU Pressure

- CUDA runtime available or unavailable;
- optional `cudaMemGetInfo` free/total memory;
- estimated memory budget ratio;
- known `cudaMemAdvise` warnings should remain non-fatal.

### Render Pressure

- latest render time in milliseconds;
- moving average render time;
- rough FPS band;
- current render quality preset;
- match-window render target size;
- cinematic mode enabled/disabled;
- label overlay enabled/disabled.

### AI/ASD Pressure

- ASD region count;
- label mask present/absent;
- semantic callouts enabled/disabled;
- selected region emphasis enabled/disabled;
- requested export bundle includes full mask or metadata only;
- volume size above which ASD should be prompted rather than auto-run.

## Governor Outputs

The governor should produce a compact result struct, for example:

- `status`: `healthy`, `watch`, `pressure`, `critical`;
- `score`: 0 to 100 pressure score;
- `primary_reason`: short explanation;
- `recommendation`: one actionable suggestion;
- `safe_to_run_asd`: boolean;
- `safe_to_export_mask`: boolean;
- `suggest_interactive_gpu`: boolean;
- `suggest_reduce_callouts`: boolean;
- `suggest_brick_streaming`: boolean future-readiness signal for later phases.

The text should be plain and demo-friendly. Example:

`Performance Governor: Watch. Render average is 142 ms at 1024 x 1024 with labels enabled. Keep cinematic mode, but use Interactive GPU while orbiting or reduce callouts to one.`

## User Experience

Add a small `Performance Safety` block inside `AI Assist` or `Metadata`, not a new large docker.

Suggested UI:

- status pill: `Healthy`, `Watch`, `Pressure`, or `Critical`;
- one-line reason;
- one-line recommendation;
- buttons only when useful:
  - `Use Interactive GPU`;
  - `Return to Balanced GPU`;
  - `Limit Callouts to 1`;
  - `Refresh Safety Check`.

The app should keep rendering even when the governor is warning. Warnings should guide, not alarm.

## Data Flow

1. On startup and volume import, compute initial volume and memory estimates.
2. Each frame or every few frames, update render-time moving average from existing `last_render_ms`.
3. When AI Assist is visible, refresh the governor result at a low frequency.
4. Before optional expensive actions, ask the governor whether they are safe:
   - ASD discovery;
   - full label-mask export;
   - semantic demo auto-run;
   - future brick streaming.
5. Display the result and offer user-controlled actions.

## Error Handling

- If CUDA memory telemetry fails, fall back to estimates and show `Telemetry estimate` instead of failing.
- If no volume is loaded, report `Load a volume to estimate safety`.
- If render time spikes briefly, use a moving average so the UI does not flicker.
- If the app is already in a safe mode, avoid repeating recommendations.
- If an optional action is blocked for safety, explain the exact reason and provide a lower-cost alternative.

## Testing Plan

Use focused helper tests before wiring UI:

1. small volume + normal render time returns `healthy`;
2. large volume + high memory estimate returns `pressure` or `critical`;
3. high render average suggests `Interactive GPU` without changing state;
4. callout-heavy semantic state suggests limiting callouts;
5. no CUDA telemetry still returns a usable estimate;
6. existing render regressions remain unchanged for foot, Bonsai, and headsq.

## Acceptance Criteria

- Governor is visible but compact.
- It does not change render quality automatically during normal use.
- It provides at least one useful recommendation under high render time or high memory pressure.
- It remains local and lightweight; no cloud calls or model loading.
- Existing default renderer, cinematic mode, ASD-Lite, selected-region glow, and review bundle exports continue to pass current verification scripts.
- `foot_256x256x256_uint8` remains protected by render regression.
- The design keeps the path open for future brick streaming without implementing streaming in this phase.

## Implementation Order

1. Add a small helper/API test for governor scoring and recommendations.
2. Add a `common/performance_governor.h` helper with pure CPU-side calculations.
3. Add minimal `StandaloneApp` fields for moving average and governor status text.
4. Refresh the governor result from existing render and volume state.
5. Add compact UI block and safe action buttons.
6. Gate only optional analysis actions with explanatory warnings; do not alter the render path automatically.
7. Update README with the local AI concept and performance-safety note.
8. Run build, unit tests, foot render, Bonsai render, headsq render, ASD verification, and one GUI screenshot.

## Risks And Mitigations

- Risk: the governor feels like the app is weaker on laptops. Mitigation: frame it as GPU-aware intelligence that preserves quality and prevents hangs.
- Risk: automatic changes annoy users. Mitigation: advisory first, user-clicked actions only.
- Risk: telemetry differs across NVIDIA laptops. Mitigation: estimates must work without CUDA memory telemetry.
- Risk: too many warnings clutter AI Assist. Mitigation: one status pill, one reason, one recommendation, optional buttons only.
- Risk: governor recommendations become stale. Mitigation: update from moving averages and refresh on volume import, quality change, label change, and semantic runs.

## Future Extensions

- Feed the governor into brick streaming decisions.
- Add a `Demo Safe Mode` preset that users can activate explicitly.
- Add export metadata showing hardware-aware settings used for a screenshot or review bundle.
- Add a benchmark panel that compares current volume pressure against known safe examples like foot, Bonsai, and headsq.
