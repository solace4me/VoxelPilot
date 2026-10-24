# Contributing to VoxelPilot

VoxelPilot is a CUDA volume-rendering research/visualization workbench
with local, heuristic AI-assist features. This doc covers how to build,
test, and make changes safely. For what the app does, see `README.md`;
for how the codebase fits together, see `docs/ARCHITECTURE.md`.

## Hard requirement: laptop-friendly hardware floor

VoxelPilot must run well on a normal, mid-range NVIDIA laptop GPU, not
just a high-end workstation card. This is a cross-cutting constraint on
every change, not a nice-to-have:

- Don't add a feature whose default path requires a high-end/workstation
  GPU. If something needs more headroom, it must be optional and gated
  behind the Performance Safety Governor (`common/performance_governor.h`),
  the same way ASD discovery and AI review bundle export are gated today.
- Prefer local/heuristic approaches over anything that assumes a large
  GPU or a network round-trip. The AI-assist features (ASD-Lite,
  Transfer Advisor, Dataset Health Check) are deliberately local and
  non-neural for this reason.
- If you're unsure whether a change respects the hardware floor, say so
  in the PR/commit description rather than assuming it's fine.

## Building

Windows + an NVIDIA GPU is required (CUDA, single-TU `.cu` build). See
`BUILD_WINDOWS.md` for prerequisites. Day to day:

```
.\build_windows.ps1
```

This detects your GPU architecture and produces
`volume_renderer_standalone.exe` in the repo root.

## Testing

Run everything with one command:

```
.\run_all_tests_windows.bat
```

This wraps the five existing test/verification entry points and prints
a pass/fail/skip summary (also logged to `build\test_results.log`):

1. `run_tests_windows.bat` - compiles and runs `tests/volume_import_tests.cpp`
   (unit tests for import, AI heuristics, performance governor, render
   command building, etc.), then runs the structural regression suite
   `tests/render_regression_tests.py`.
2. `run_render_regression_windows.bat` - renders the foot baseline
   dataset and checks the snapshot isn't black/empty/hung/too slow.
3. `run_headsq_verification_windows.bat` - same idea, tiny CT sample
   with CT-appropriate thresholds.
4. `run_bonsai_verification_windows.bat` - same idea, Bonsai CT sample.
5. `run_bonsai_asd_verification_windows.bat` - compiles and runs
   `tools/verify_asd_dataset.cpp` against the Bonsai sample to check ASD
   discovers the expected regions.

A stage prints `SKIP` (not a failure) when its executable or sample
dataset isn't present yet - for example on a fresh clone before the
first build, or if you don't have every sample dataset checked out.
Each script also still works standalone if you only want to run one
check while iterating.

`tests/render_regression_tests.py` is pure Python and only reads
`standalone_main.cpp` as text (no compiler needed), so it can be run on
its own for a quick structural check:

```
python tests\render_regression_tests.py
```

## Before you change UI code

This project has one specific, hard-won rule: **before adding a button,
panel, tab, or section, check whether the control already exists
somewhere else in the app.** `standalone_main.cpp` has several UI
surfaces (top ribbon, Controls panel, AI Assist panel, Insights panel),
and it's easy to "fix" one of them by copy-pasting a control that
already lives in another - which just adds duplication without adding
value. Grep for the button/checkbox label first
(`grep -n 'ImGui::Button("Your Label"' standalone_main.cpp`); if it's
already there, either wire the existing one up differently or find a
genuinely new thing for that space to do.

## Code structure notes

See `docs/ARCHITECTURE.md` for the full picture. The two things most
likely to bite you on a first change:

- **Forward declarations.** `standalone_main.cpp` is one large
  top-to-bottom translation unit. A function called earlier in the file
  than it's defined needs an explicit forward declaration in the block
  starting around line 431. Forgetting this is a compile error, not a
  runtime one, so it's caught immediately.
- **`StandaloneApp` zero-init.** `main()` does
  `memset(&app, 0, sizeof(app));` before anything else, so a new
  `int`/`float` field in the `StandaloneApp` struct defaults to
  `0`/`0.0f` for free - you don't need to hand-initialize it, but you
  do need to add it to the struct definition itself (around line
  90-299).

## Making a change

1. Make the smallest change that accomplishes the goal.
2. Run `.\run_all_tests_windows.bat` (or at least the stages your
   change could affect) before considering it done.
3. If the change is user-visible, add a line under `## [Unreleased]`
   in `CHANGELOG.md`.
4. If the change is a new AI-assist or rendering feature of any size,
   consider a short design note under `docs/superpowers/specs/` the
   way earlier features (ASD, performance governor, cinematic mode)
   were documented - it's cheap now and useful later, including for
   the eventual paper on this project.
