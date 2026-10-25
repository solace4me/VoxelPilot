# Plan: Unity-style workstation layout

## Why

Temitope shared a reference mockup (`docs/superpowers/specs/2026-10-23-unity-
style-workstation-layout.pdf`) for a redesigned main window, modeled on
Unity's editor. Tier 3c (the standalone_main split) finished first so this
could be done as a per-file rewrite against already-split panel modules.
Steps 1-3 below were built against that mockup.

**2026-10-25 update: superseded as source of truth.** Temitope then shared
a much more detailed, formal spec - "VoxelPilot v2 - UI/UX Interaction &
Flow Spec" (a Claude Docs page, https://claude.ai/artifact/6e4rndE9yZHYkppUDB9spF,
dated 2026-10-24) - with exact region sizes, component states, six end-to-end
user flows, error/safety states, and a keyboard shortcut table. A local
mirror of its structural content (region-by-region spec, layout table, flows,
shortcuts) is saved at
`docs/superpowers/specs/2026-10-24-ui-ux-interaction-and-flow-spec.md` for
this repo's own reference, since the canonical copy lives in that Claude Docs
page. **That spec is now the authority for every step below, including
corrections to steps 1-3's already-shipped work.** Where this plan and that
spec disagree, the spec wins; this doc should stay in sync with it.

## Status

- Step 1 - done, build-verified (commit 6cfe05a): toast history ring buffer
  + a temporary Log panel.
- Step 2 - done, build-verified (commit 4b0c318): `ui_bottom_panels.cpp`
  tab host with Histogram + Log tabs.
- Step 3 - done, build-verified (commit 93c3a9f): AI Assist + Metadata join
  the tab host as tabs (Begin/End stripped out of both).
- Step 4 (corrections) - not started. See below; this is new work the spec
  revealed, not in the original mockup-based plan.
- Steps 5-11 (renumbered from the original plan's 4-10) - not started,
  scope revised against the spec; see below.

## Corrections needed to steps 1-3's already-shipped work (new step 4)

The spec's bottom-dock tab *names and order* ("AI Assist - Histogram -
Metadata - Log") match what we built exactly. Their *contents* mostly don't:

- **Log tab** (direct contradiction, not just a gap): spec wants newest
  entries at the bottom, auto-scrolling unless the user has scrolled up -
  we built newest-first at the top. Also needs: a timestamp per line,
  warning/error severity with highlight/danger coloring (today's ring
  buffer stores only the raw message string, no severity), and a "Copy
  all" button.
- **Histogram tab**: spec wants 140px tall (we built 180px), bars inside
  the current transfer-function window shown in accent color and others
  in `#2E3B45` (today: uniform color), and dragging on the histogram sets
  Center/Width directly (today: static display only).
- **Metadata tab**: spec wants a 4-column label/value grid - Dimensions,
  Data type, Voxels, Size, Spacing, Value range, Palette, Clipping, Health
  score. We built a plain vertical `ImGui::Text` list missing Data type
  and Size.
- **AI Assist tab**: spec wants a simple layout - a prompt input ("Ask
  VoxelPilot", Enter or Apply), a row of quick-action chips (Auto enhance,
  Discover regions, Health check, Guided review, Export bundle), and a
  "Last action" card on the right showing what changed (before -> after
  in Mono) with Undo and the Safety status for the next AI action (chips
  disabled + tooltip when Safety blocks). We moved the existing
  collapsing-header content (Discovery / Transfer & Health / More AI
  Tools) into the tab unchanged - a relocation, not this redesign. This is
  the biggest of the four corrections; the existing functions it calls
  (run_adaptive_semantic_discovery, apply_auto_enhance_transfer,
  update_dataset_health, start_guided_review, export_ai_review_bundle,
  apply_ai_prompt_to_app, undo_transfer_settings, and so on) all stay -
  only the panel's own layout and which of them get a visible control
  changes.

## What already exists (re-audited against the current split codebase)

- **Real dockspace already in place.** `draw_dockspace`/
  `ensure_default_dock_layout` (`ui_ribbon_and_layout.cpp`) already build a
  genuine ImGui dockspace with `ImGuiDockNodeFlags_PassthruCentralNode`.
  `app->dock_layout_variant` (0/1) already toggles two arrangements, though
  its naming and meaning both need to change in step 11 to match the
  spec's named presets.
- **Ortho slice thumbnails already render**, just inside the wrong panel
  and with none of the interactivity step 6 below needs
  (`draw_slice_card` + `app->axial_tex/coronal_tex/sagittal_tex`,
  `update_slice_previews` in `gl_viewport_and_render.cpp`, called today
  from `draw_insights_panel` in `standalone_main.cpp`).
- **Floating region callouts already exist**
  (`draw_cinematic_semantic_callouts`, `ui_demo_and_onboarding.cpp`),
  gated to the cinematic/demo sequence - step 9 adapts this into an
  always-on hover/selection variant per the spec, keeping the existing
  cinematic use intact.
- **The data the Scene tree needs mostly already exists**:
  `app->annotations[]`/`annotation_count`,
  `app->semantic_regions[]`/`semantic_region_count` (the spec's
  "Discovered regions"/"Annotations" branches). But the spec's tree also
  lists Measurements and Clip box as rows, and today's data model holds
  only a single global measurement (`measure_a`/`measure_b`,
  `measurement_visible`, `measurement_target`) and a single global clip
  box (`clip_min`/`clip_max`) - not collections. The spec's "+" add menu
  (Annotation, Measurement, Clip box) implies you could add more than one
  measurement; step 5 surfaces the existing single instances rather than
  building multi-measurement support, and flags that as an open question
  rather than silently assuming it.
- **"Last action / Undo" already exists** in `draw_ai_assist_panel`
  (`ui_ai_assist_panel.cpp`) and is most of what step 4's AI Assist
  correction needs - it needs a new home (the right-side card) and a
  Safety-status readout next to it, not a rebuild from scratch.

Genuinely new, with no existing equivalent:
- The viewport toolbar itself and tool-mode state (Orbit/Pan/Measure/
  Clip/Annotate - note: Clip is its own tool in the spec, "select" is
  not; today there is a single orbit-drag mode,
  `process_mouse_camera_controls` in `gl_viewport_and_render.cpp`).
- Animated camera-preset transitions (300ms) - not in the original plan
  at all.
- The axis gizmo, and its click-to-look-down-axis interactivity.
- A real classic menu bar (File/View/Volume/AI/Review/Help) - today's
  `draw_top_ribbon` is a row of ribbon-tab buttons, a different paradigm,
  not a relayout of the same thing.
- The Scene panel's own selection/expansion/search UI state, its add(+)
  menu, and camera-focus-on-double-click (brought forward into step 5
  per the spec, rather than deferred the way the original plan had it).
- Slice-strip interactivity: crosshair drag, wheel-scroll, double-click
  to enlarge/fill viewport (Esc returns), Measure-mode point placement.

## Proposed module structure

- `ui_bottom_panels.cpp` (exists) - step 4's corrections land here and in
  `ui_ai_assist_panel.cpp`.
- `ui_scene_panel.cpp` (new, step 5) - the left Scene hierarchy per the
  spec's region-by-region description.
- `ui_viewport_overlay.cpp` (new, steps 6-8) - slice strip, toolbar +
  tool-mode state, axis gizmo. Takes over `draw_slice_card` (`static` in
  `standalone_main.cpp` today, called only from `draw_insights_panel`).
- `ui_controls_panel.cpp` (edited in place, step 10) - becomes the
  spec's selection-driven Inspector: sections shown depend on what's
  selected (Volume / Region / Annotation / Measurement / Clip box /
  Camera / Key light), each with the exact sections and default-open
  state the spec lists.
- `ui_ribbon_and_layout.cpp` (edited in place, step 11) - `draw_top_ribbon`
  is rebuilt into the spec's File/View/Volume/AI/Review/Help menu bar;
  `dock_layout_variant`'s two values are renamed/redefined to the spec's
  "Explore" (default) and "Deep review" (Scene hidden, Inspector 360px,
  bottom dock 320px, Metadata tab active) presets.
- `standalone_main.cpp` - `draw_insights_panel` is retired once its three
  pieces (slice row, measurement summary, histogram) have new homes
  (step 10); `draw_gui`'s call list updates to match.

## Proposed order

Smallest blast radius first, each step build-verified before the next
starts, same discipline as the split and as steps 1-3:

4. **Bottom dock content corrections** (see above): Log reorder +
   timestamps + severity + Copy all; Histogram resize + window coloring +
   drag-to-set; Metadata 4-column grid; AI Assist redesign to prompt +
   chips + Last-action card. Lowest risk of what's left - isolated to
   `ui_bottom_panels.cpp`/`ui_ai_assist_panel.cpp`, which steps 2-3 just
   proved build clean.
5. **`ui_scene_panel.cpp`**: Scene tree over existing annotation/region/
   measurement/clip-box data, search filter, add(+) menu (flagging the
   single-measurement/single-clip-box data-model limit above), row hover
   (eye icon + Rename/Focus camera/Change colour/Delete), click-select
   into the Inspector, double-click-to-focus-camera, F2 rename,
   Delete-with-Undo-toast, empty-state drop zone.
6. **`ui_viewport_overlay.cpp`** part 1: slice strip, relocated under the
   viewport *and* made interactive per the spec - drag-crosshair (updates
   the other two slices and the 3D slice planes), wheel-scroll through
   slices, double-click to enlarge to fill the viewport (Esc returns),
   Measure-mode clicks place point A then B. Bigger than the original
   plan's "just relocate it" framing for this step.
7. **`ui_viewport_overlay.cpp`** part 2: toolbar + tool-mode state
   (Orbit/Pan/Measure/Clip/Annotate, exactly one active, hint text) +
   camera-preset segmented control (Front/Side/Top/Iso) with a 300ms
   animated transition. Tool-mode wiring into
   `process_mouse_camera_controls`'s behavior is still the riskiest single
   step in this plan.
8. **`ui_viewport_overlay.cpp`** part 3: axis gizmo (X red/Y green/Z blue
   with letter labels, ImDrawList, driven by the current camera
   orientation) - now also clickable, to look down the clicked axis.
9. Adapt `draw_cinematic_semantic_callouts` into an always-on
   hover/selection callout with a one-line explanation and click-to-select
   in the Scene tree (keeping the existing cinematic/demo usage intact).
10. Retire `draw_insights_panel` (measurement summary/slice row/histogram
    all have new homes by now) and turn `draw_controls_panel`
    (`ui_controls_panel.cpp`) into the spec's selection-driven Inspector.
    Update `draw_gui`'s call list.
11. Rebuild `draw_top_ribbon` into the spec's classic menu bar, wire the
    keyboard-shortcut table (O/P/M/C/A tool keys, 1-4 camera presets,
    Ctrl+K AI prompt focus, F11 focus-viewport, F2/Delete, [ and ], and so
    on - Ctrl+T/Ctrl+O/Ctrl+S already exist and stay), and redefine
    `dock_layout_variant`'s two values as "Explore"/"Deep review" with the
    spec's exact preset semantics.

## Risks

- **No local compiler, same as always.** Every step still depends on a
  real `build_windows.ps1` run reporting back the exact error.
- **Measurement/clip-box data model** supports only one of each today;
  the spec's "+" add menu in the Scene panel implies more might be
  addable later. Step 5 does not build multi-measurement support - it
  surfaces what exists and leaves "add another" as an open question
  rather than silently overscoping or silently dropping the "+" menu
  item.
- **Step 7 (tool-mode interaction)** is still qualitatively different
  from everything else here - it changes real mouse-input behavior
  (today: one orbit-drag mode everywhere) and now also needs animated
  camera transitions. Budget real design time there.
- **Step 6 (slice-strip interactivity)** now carries similar risk to
  step 7 for the same reason - it's a mouse-interaction build, not a
  layout move, even though it was framed as the latter in the original
  mockup-based plan.
- **Step 11 (menu bar rebuild)** is broad rather than deep - many small
  wires to existing functions - which is why it's last: by then the
  Inspector/Scene/Viewport pieces it links to already exist. Still a
  real risk from the sheer number of menu items and shortcuts to get
  right in one pass.
- **This spans many sessions**, like the split did.

## Recommendation

Start with step 4 (bottom dock content corrections) - it's the cheapest
and most isolated of what's left, it fixes direct contradictions in
already-shipped work before more gets built on top of it, and it touches
only files steps 2-3 already proved build clean.
