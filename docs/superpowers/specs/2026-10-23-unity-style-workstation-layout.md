# Future Work: Unity-style Workstation Layout

Date: 2026-10-23
Status: Deferred until Tier 3c (standalone_main.cpp module split) is complete
Branch: `codex/v2-cinematic-ai`

## What this is

Temitope shared a reference mockup for a redesigned main window, modeled on
Unity's editor layout: left Scene hierarchy panel, center 3D viewport with a
tool toolbar and orthographic slice views underneath, right Inspector panel,
and a tabbed bottom panel for AI Assist / Histogram / Metadata / Log. The
reference file is `docs/superpowers/specs/2026-10-23-unity-style-workstation-
layout.pdf` (both a text description and a rendered screenshot of the target
layout).

## Decision

Do this after Tier 3c finishes, not during it or before it. Reasoning: this
redesign will touch nearly every panel-drawing function in the app, and most
of them still live together in `standalone_main.cpp` today. Doing a large UI
rewrite against that file at the same time as the in-progress, incremental,
build-verified extraction (see
`docs/superpowers/plans/2026-09-30-standalone-main-split.md`) risks the two
efforts colliding - an extraction step landing mid-redesign, or a build
failure with an ambiguous cause. Once the split is done, each panel already
lives in its own small file (`ui_controls_panel.cpp`,
`ui_ribbon_and_layout.cpp`, etc. per the split plan), and the redesign
becomes a per-file rewrite instead of surgery inside a ~4,000-line monolith.
It should also be planned the same way the split was: its own short plan
doc, one panel at a time, each step build-verified before the next starts.

## First-pass inventory (not a committed plan yet)

Already exists in some form, this is mostly a relayout:
- Transfer function controls (center/width/opacity, palette choice)
- Render quality tiers, cinematic look, adaptive quality, step size
- Clipping, camera & light settings
- AI Assist tab content (ask/apply, auto enhance, discover regions,
  health check, guided review, export bundle, last-action/undo)
- Histogram / Metadata / Log tabs
- Annotations and discovered-region data (today surfaced through AI
  Assist/Metadata panels rather than a tree)

Genuinely new widgets needed:
- Left "Scene" hierarchy: searchable tree of the loaded dataset,
  discovered regions, annotations, camera, light, with selection
- Viewport toolbar: select / orbit / pan / measure / annotate tool icons
- Axis gizmo in the viewport corner
- Front / Side / Top / Iso camera-switch buttons
- Floating region-label callout over the 3D view on hover/selection
- Row of three ortho slice thumbnails (Axial / Coronal / Sagittal) under
  the main viewport

## Next step when this is picked back up

Write a proper ordered plan doc (same format as the split plan), scoped
per panel, once Tier 3c's steps 6-10 are committed and build-verified.
