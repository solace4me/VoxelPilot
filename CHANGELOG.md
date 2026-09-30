# Changelog

All notable changes to VoxelPilot are recorded here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Dates are commit
dates (UTC); entries above the most recent tagged/committed history are
work in progress on top of the checked-out tree.

## [Unreleased]

### Added
- Unified test runner (`run_all_tests_windows.bat`) that wraps the five
  existing test/verification scripts (unit + structural tests, foot
  baseline render regression, VTK headsq CT verification, Bonsai CT
  verification, Bonsai ASD verification) into a single pass/fail/skip
  summary with a log at `build/test_results.log`. Stages skip cleanly
  (instead of failing) when their executable or sample dataset isn't
  present yet.
- `CONTRIBUTING.md` and `docs/ARCHITECTURE.md` describing the build
  model, contributor workflow, and codebase map.
- Persistent Performance Safety badge in the top ribbon's brand row
  (status + score, with a tooltip explaining the current limiting
  factor).
- AI transparency captions and hover tooltips on ASD region
  confidence/stability readouts and the Transfer Advisor, making clear
  these are local heuristic scores, not a trained model.
- "Show Me: Volume Import Step" button and reworded empty/first-run
  copy in AI Assist for users who haven't loaded a volume yet.
- Saved Demo vs. Deep Review docking layouts, toggled from the
  Controls panel.

### Changed
- AI Assist panel reorganized into collapsible "Discovery" and
  "Transfer & Health" sections for clearer scanning.
- Region confidence/stability and Performance Safety pressure readouts
  changed from plain numeric text to color-coded progress bars.
- Button rows in AI Assist / Controls now wrap based on available
  width instead of overflowing at lower laptop resolutions.
- Top ribbon restructured: removed the AI/Analysis/Export tabs, which
  duplicated controls already present in the AI Assist, Insights, and
  Controls panels, and replaced that space with the Performance Safety
  badge described above. The ribbon now only carries Home and View,
  each doing something not available elsewhere.
- Wired up the previously-unused `draw_performance_safety_block`, so a
  blocked/limited Performance Safety state now surfaces its reason and
  one-click fixes in AI Assist instead of only a "Ready"/"Blocked"
  label.

### Fixed
- Two stale assertions in `tests/render_regression_tests.py` that
  referenced UI labels the app no longer uses (`"Advanced AI"` /
  `"Undo Transfer"`) were checking against text that predates this
  changelog's history; updated to match the current labels
  (`"More AI Tools"` / `"Restore Previous View"`).

## [2026-07-30]
- docs: add ASD v2 local review engine design

## [2026-07-21]
- version two with ASD

## [2026-07-03]
- docs: add VoxelPilot demo video design

## [2026-07-01]
- feat: add dataset health check
- feat: add guided review mode
- feat: add AI transfer advisor
- fix: preserve visible prompt reset behavior
- docs: describe performance safety governor
- feat: guard optional AI actions with performance safety
- feat: add performance safety UI
- feat: integrate performance safety telemetry
- feat: add performance safety governor core
- docs: plan performance safety governor
- docs: design performance safety governor

## [2026-06-30]
- feat: add AI review bundle export
- feat: add selected region GPU emphasis
- feat: add one-click semantic demo
- test: add Bonsai ASD verification
- fix: improve default volume render visibility
- feat: brighten CT imports and add bonsai sample
- data: add tiny VTK headsq verification sample
- feat: add cinematic ASD callouts
- feat: add ASD correction memory
- feat: add adaptive semantic discovery

## [2026-06-29]
- feat: start VoxelPilot v2 cinematic AI track

## [2026-06-20] - [2026-06-28]
- Little fix
- working with ai
- working point
- fix in rendering

## [2026-05-03]
- VoxelPilot for Github (initial import)
