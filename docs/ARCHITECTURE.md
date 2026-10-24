# Architecture

This is a map of how VoxelPilot fits together - where state lives, how a
frame gets drawn, and where the AI-assist features plug in. For
feature-level design history (why ASD works the way it does, why the
performance governor has the thresholds it has), see
`docs/superpowers/plans/` (the proposal for a feature) and
`docs/superpowers/specs/` (its design write-up). This document is the
one level up from those: how the pieces relate, not why each one was
built.

## Build model: one translation unit

`standalone_main.cpp` (~5,900 lines) is the entire application - window
setup, ImGui UI, app state, and the glue that calls into the CUDA
renderer - compiled as a single translation unit alongside
`renderer/*.cu`. There's no header/source split for the app logic
itself; everything is `static` functions in one file, defined roughly
top to bottom in the order they're first needed.

This has one direct consequence: **a function called before its
definition needs an explicit forward declaration.** These live in a
block starting around line 431 (`static void sync_orbit_from_camera(StandaloneApp *app);`
and onward). If you add a function and call it from code above its
definition, add its declaration there too, or the build fails at that
call site.

Splitting this file into modules with a real build system (CMake) is
tracked as its own follow-up (Tier 3) rather than something to do
incidentally while fixing something else - it's a large, high-risk
change on its own and shouldn't be bundled with unrelated work.

## Application state: `StandaloneApp`

Everything the running app knows about - loaded volume, camera, transfer
function, ASD results, performance governor state, which panels are
open, dock layout variant, and so on - lives in one struct,
`StandaloneApp` (defined around lines 90-299). `main()` does:

```c
memset(&app, 0, sizeof(app));
```

before touching anything else, so a new `int`/`float`/`bool`-as-int
field defaults to zero without needing explicit init code elsewhere.
Non-zero defaults (and the defaults `reset_workspace()` restores) are
centralized in `common/default_state.h` /
`apply_default_workspace_state()` rather than scattered through
`main()` - see `apply_default_workspace_state(&app, 1)` in `main()` and
`apply_default_workspace_state(app, 0)` in `reset_workspace()`.

## Frame loop and UI surfaces

`main()` -> per-frame `draw_gui(app)` (around line 5333), which in turn
calls, in order: `draw_top_ribbon`, `draw_controls_panel`,
`draw_ai_assist_panel`, plus the Insights and Metadata panels. Docking
layout (which panel sits where) is built by
`ensure_default_dock_layout()`, which only rebuilds when
`app->dock_layout_ready` is `0` - set that flag to force a layout
rebuild next frame (used by the Demo/Deep Review layout toggle and
"Reset Layout").

There are three user-facing UI surfaces, each with a distinct job - if
you're adding a control, it should go in exactly one of these, not be
copied across them:

- **Top ribbon** (`draw_top_ribbon`) - global, always-visible state:
  Home/View tabs, panel visibility checkboxes, workspace presets, the
  Performance Safety badge. Not a place for feature-specific action
  buttons that already live in a panel below.
- **Controls panel** (`draw_controls_panel`) - volume import, camera,
  transfer function, export.
- **AI Assist panel** (`draw_ai_assist_panel`) - everything AI-assist:
  ASD discovery, guided review, transfer advisor, dataset health,
  review bundle export.

A ribbon restructuring earlier in this project's history removed an
AI/Analysis/Export tab set from the ribbon specifically because it had
drifted into duplicating buttons already present in AI Assist and
Controls - see `CHANGELOG.md` under Unreleased. The rule going forward:
check for an existing control with the same label
(`grep -n 'ImGui::Button("..."' standalone_main.cpp`) before adding a
new one.

## AI-assist features

All of the AI-assist functionality is local and heuristic - geometry
and intensity math, not a trained model or a network call. This is a
deliberate fit with the laptop-hardware-floor requirement (see
`CONTRIBUTING.md`), and the UI says so explicitly (the transparency
captions added to AI Assist).

- `common/semantic_discovery.h` - **ASD-Lite** (Adaptive Semantic
  Discovery): connected-component region finding + shape/intensity
  archetyping. Also holds guided review and correction memory.
- `common/ai_features.h` - Transfer Advisor and auto-enhance transfer
  function suggestions, derived from histogram shape.
- `common/dataset_health.h` - heuristic checks for tiny volumes, flat
  histograms, background-dominant imports, oversized working sets.
- `common/performance_governor.h` - the Performance Safety Governor:
  evaluates render time, memory pressure, and callout load into a
  status/score (`VoxelPilotPerformanceResult`), which gates optional
  heavy actions (`performance_allows_optional_action`) and drives the
  ribbon's Performance Safety badge and AI Assist's safety block
  (`draw_performance_safety_block`).
- `common/ai_prompt.h`, `common/insight_report.h`,
  `common/ai_review_bundle.h` - the prompt-driven settings parser and
  the two export formats (human-readable insight report, structured
  review bundle with regions/relationships/label mask).

## Other `common/` modules

- `volume_import.h` / `volume_import.cpp` - raw volume file loading and
  type normalization.
- `histogram_profile.h`, `math_utils.h`, `volume_structs.h` - shared
  data types and histogram/math helpers used across the above.
- `render_command_builder.h`, `renderer_api.h`, `cuda_device_policy.h`
  - the boundary between the ImGui/app layer and the CUDA renderer:
  building a render command from UI state, and picking a safe CUDA
  device.
- `cli_options.h`, `json_utils.h`, `ui_state_helpers.h` - command-line
  parsing (used by the verification tools and headless snapshot mode),
  JSON writing, and small UI-state helpers.

## Renderer

`renderer/renderer.cu`, `renderer/raymarch_kernel.cu`,
`renderer/volume_textures.cu` - the actual CUDA raymarching pipeline:
volume upload into 3D textures and the raymarch kernel itself. The app
layer talks to this only through `render_command_builder.h` /
`renderer_api.h`, so UI and rendering stay separable even though
they're compiled together today.

## Testing

See `CONTRIBUTING.md` for how to run tests. Structurally:

- `tests/volume_import_tests.cpp` - ~75 unit tests, compiled with
  `cl.exe`, covering import, ASD, transfer advisor, dataset health,
  performance governor, adaptive quality, render command building, and
  more. This is where AI-heuristic-threshold coverage lives; check here
  before assuming a heuristic is untested.
- `tests/render_regression_tests.py` - pure-Python structural
  assertions against `standalone_main.cpp` (no compiler needed) plus PNG
  metric unit tests. Cheap to run on its own while iterating on UI
  layout.
- `tools/verify_render_snapshot.py`, `tools/verify_headsq_snapshot.py`,
  `tools/verify_asd_dataset.cpp` - end-to-end checks that actually
  launch the renderer against a sample dataset and validate the output
  (visibility, timing, or discovered ASD regions).
- `run_all_tests_windows.bat` - runs all of the above in one pass with
  a pass/fail/skip summary.
