# Plan: Unity-style workstation layout

## Why

Temitope shared a reference mockup (`docs/superpowers/specs/2026-10-23-unity-
style-workstation-layout.pdf`) for a redesigned main window, modeled on
Unity's editor: left Scene hierarchy, center 3D viewport with a toolbar and
an ortho-slice row underneath, right Inspector, and a tabbed bottom panel
(AI Assist / Histogram / Metadata / Log). The decision to do this after
Tier 3c (see `docs/superpowers/plans/2026-09-30-standalone-main-split.md`
and `docs/superpowers/specs/2026-10-23-unity-style-workstation-layout.md`)
was made so this redesign could be done as a per-file rewrite against
already-split panel modules instead of surgery inside one ~5,900-line file.
Tier 3c is now complete and build-verified (10/10 steps), so this is that
plan.

## What already exists (re-audited against the current split codebase)

This is a smaller lift than it first looks, because several of the
mockup's "new" pieces are already built, just not laid out this way yet:

- **Real dockspace already in place.** `draw_dockspace`/
  `ensure_default_dock_layout` (`ui_ribbon_and_layout.cpp`) already build a
  genuine ImGui dockspace with `ImGuiDockNodeFlags_PassthruCentralNode` -
  the 3D viewport already renders through the empty central dock node
  today; it just isn't treated as a panel with its own toolbar/overlay
  content. There's also an existing `app->dock_layout_variant` (0/1) toggle,
  wired through a button in `ui_controls_panel.cpp` - the natural extension
  point for a new variant rather than replacing the dock system.
- **Ortho slice thumbnails already render, just inside the wrong panel.**
  `draw_slice_card` + `app->axial_tex/coronal_tex/sagittal_tex` (kept
  current every frame by `update_slice_previews` in
  `gl_viewport_and_render.cpp`) already exist and are already drawn today,
  inside `draw_insights_panel` (`standalone_main.cpp`, "Insights" window).
  The mockup's row under the viewport is a relocation, not new plumbing.
- **Floating region callouts already exist.** `draw_cinematic_semantic_
  callouts` (`ui_demo_and_onboarding.cpp`) already draws a leader-line
  callout card anchored to a 3D region position, close to the mockup's
  "R1 - Dense shell - 38% of visible voxels" hover card. Today it's gated
  to the cinematic/demo sequence (`app->semantic_callouts_enabled`); the
  redesign needs an always-on hover/selection-driven variant, which is an
  adaptation of this function, not a rebuild.
- **The data the Scene tree needs already exists.** `app->annotations[]` /
  `annotation_count` and `app->semantic_regions[]` / `semantic_region_count`
  (declared in `standalone_app.h`) already hold everything the "Discovered
  regions" and "Annotations" tree branches need to enumerate. The Scene
  panel is new drawing/selection code over existing state, not a new data
  model (aside from tree selection/expansion/search state, which is new).
- **"Last action / Undo" already exists** in `draw_ai_assist_panel`
  (`ui_ai_assist_panel.cpp`) and matches the mockup's AI Assist tab content
  closely as-is.

Genuinely new, with no existing equivalent:
- The viewport toolbar itself (tool-mode icons, Front/Side/Top/Iso camera
  buttons, Perspective/Cinematic chips) - there's no tool-mode state
  (select/orbit/pan/measure/annotate) in `StandaloneApp` today; mouse
  interaction is a single orbit-drag mode (`process_mouse_camera_controls`,
  `gl_viewport_and_render.cpp`).
- The axis gizmo.
- A Log tab: `app->ui_toast` is a single `char[256]`, not a history ring
  buffer - this needs a small new buffer plus every `set_ui_toast` call
  (and any other status-setting call worth logging) feeding it.
- The Scene panel's own selection/expansion/search UI state.

## Proposed module structure

Mirroring the split's one-file-per-panel pattern. New panels get new
files; panels being relaid out get edited in their existing file rather
than renamed, so cross-file declarations in `standalone_app.h` keep
working without a rename step (unlike `standalone_main.cu`'s step 10,
which had a specific reason - a toolchain change - to rename).

- `ui_scene_panel.cpp` (new) - the left Scene hierarchy: search box,
  dataset root, Discovered regions / Annotations / Clip box / Camera /
  Key light rows, selection state.
- `ui_controls_panel.cpp` (edited in place) - becomes the Inspector:
  same content `draw_controls_panel` already has, regrouped into
  collapsible sections (Rendering, Transfer function, Clipping, Camera &
  light, plus a home for Measurement/Voxel Spacing/Workspace Sessions,
  which the mockup doesn't show but shouldn't be dropped), redocked right
  instead of left.
- `ui_viewport_overlay.cpp` (new) - toolbar, tool-mode state, axis gizmo,
  Front/Side/Top/Iso buttons, and the always-on hover/selection callout
  (adapted from `draw_cinematic_semantic_callouts`). Takes over the ortho
  slice row display, which means `draw_slice_card` moves here too (it's
  `static` in `standalone_main.cpp` today, called only from
  `draw_insights_panel`).
- `ui_bottom_panels.cpp` (new) - one tab bar hosting AI Assist / Histogram
  / Metadata / Log as tab items. AI Assist and Metadata tabs call into
  `draw_ai_assist_panel`/`draw_metadata_panel`'s existing content (those
  functions need their own `ImGui::Begin`/`End` stripped out so they can
  render as tab contents instead of standalone windows - a real but
  mechanical change). Histogram tab is lifted from `draw_insights_panel`'s
  histogram block. Log tab is new (reads the new toast history buffer).
- `ui_ribbon_and_layout.cpp` (edited in place) - add
  `dock_layout_variant == 2` ("Unity") to `ensure_default_dock_layout`,
  docking Scene left, Inspector right, the bottom tab host at the bottom,
  and leaving the central node passthru for the viewport + its overlay
  windows. Variants 0/1 stay as they are; this is additive.
- `standalone_main.cpp` - `draw_insights_panel` is retired once its three
  pieces (slice row, measurement summary, histogram) have new homes;
  `draw_gui`'s call list updates to match.

## Proposed order

Smallest blast radius first, each step build-verified before the next
starts, same discipline as the split:

1. **Toast history buffer + Log tab plumbing only.** Add a small ring
   buffer to `StandaloneApp`, have `set_ui_toast` push into it, write a
   `draw_log_panel` helper. No layout changes yet - lowest-risk way to
   prove a new small panel builds clean before touching the dockspace.
2. **`ui_bottom_panels.cpp`**: tab host + Histogram tab (lifted from
   `draw_insights_panel`) + Log tab (from step 1). AI Assist/Metadata
   tabs deferred to step 3 since they need the `Begin`/`End`-stripping
   change to `ui_ai_assist_panel.cpp`, a separate concern.
3. Strip `ImGui::Begin`/`End` from `draw_ai_assist_panel` and
   `draw_metadata_panel`, wire them into the step 2 tab host as the
   remaining two tabs.
4. **`ui_scene_panel.cpp`**: Scene tree over existing annotation/region
   data, selection state only (no viewport interaction yet).
5. **`ui_viewport_overlay.cpp`** part 1: relocate the ortho slice row
   (`draw_slice_card` + sliders) out of `draw_insights_panel` into this
   new file, docked/positioned under the viewport.
6. **`ui_viewport_overlay.cpp`** part 2: toolbar + tool-mode state +
   Front/Side/Top/Iso buttons. Tool-mode state is new; wiring it into
   `process_mouse_camera_controls`'s behavior (select/pan/measure/annotate
   beyond today's single orbit-drag mode) is the riskiest single step in
   this plan - may need its own sub-plan once reached.
7. **`ui_viewport_overlay.cpp`** part 3: axis gizmo (hand-drawn via
   `ImDrawList`, driven by the current camera orientation).
8. Adapt `draw_cinematic_semantic_callouts` into an always-on
   hover/selection callout (keeping the existing cinematic/demo usage
   intact, likely via a shared helper with a mode flag).
9. Retire `draw_insights_panel` entirely (measurement summary needs a
   final home - likely folded into Metadata or the Inspector - decide
   when reached) and update `draw_gui`'s call list.
10. Wire `dock_layout_variant == 2` in `ensure_default_dock_layout` and
    expose it next to the existing variant toggle in the Inspector, so
    the Unity layout is opt-in alongside the existing two variants rather
    than a forced replacement - lets Temitope compare before it becomes
    default.

## Risks

- **No local compiler, same as the split.** Every step still depends on
  a real `build_windows.ps1` run reporting back the exact error.
- **Step 6 (tool-mode interaction) is qualitatively different from
  everything else here.** The rest of this plan is panel relayout over
  existing data/behavior; step 6 changes actual mouse-input behavior
  (today: one orbit-drag mode everywhere). Budget real design time there,
  not just an extraction-style move.
- **ImGui `Begin`/`End` stripping (step 3) changes window identity.**
  `draw_ai_assist_panel`/`draw_metadata_panel` currently are top-level
  dockable windows with their own docking memory; once their content
  moves inside a shared tab host, anything that currently targets them by
  window name (`set_default_window_layout`, the dock-builder calls docking
  "AI Assist"/"Metadata" by name) needs updating in the same step, not
  left dangling.
- **This spans many sessions**, like the split did - it's listed as one
  ordered sequence, not a single sitting.

## Recommendation

Start with step 1 (toast history buffer) as a trivial, isolated proof
that a new small panel still builds clean post-split, before touching
anything dockspace-related.
