# VoxelPilot v2 - UI/UX Interaction & Flow Spec (local mirror)

Local mirror of the structural content of a Claude Docs page Temitope
shared: https://claude.ai/artifact/6e4rndE9yZHYkppUDB9spF, dated
2026-10-24, titled "VoxelPilot v2 - UI/UX Interaction & Flow Spec". The
canonical, editable copy lives in that page; this file exists so the repo
has its own durable reference copy for implementation work, and should be
re-synced here if the page changes materially. It supersedes
`2026-10-23-unity-style-workstation-layout.pdf`/`.md` as the source of
truth for the Unity-style redesign - see
`docs/superpowers/plans/2026-10-24-unity-style-redesign.md`.

## Purpose and principles

This spec defines the VoxelPilot v2 desktop UI: a simple, Unity-style
workstation for exploring 3D volume data (CT/MRI-style RAW files) on a
local NVIDIA GPU. It replaces the current single long Controls panel.

Deliverables expected from design: high-fidelity desktop frames at
1440x900 and 1920x1080, a component library with every state listed here,
and the flows in this doc as clickable prototypes.

Scope: the main workstation screen, its overlays (splash, walkthrough,
guided review, dialogs) and all panel content. Out of scope: the CUDA
render itself, marketing site, installer.

Principles:
- The volume is the hero. The viewport gets the most space; chrome stays
  dark, flat and quiet so the render's colours read true.
- Select, then inspect. Pick something in the Scene tree or viewport; its
  settings appear in the Inspector. No setting lives in two places.
- Every action is reversible and visible. Changes to the transfer
  function, camera or clipping show a short confirmation and offer Undo.
- AI suggests, the user approves. AI and Safety features never change the
  view silently; they propose and the user applies.
- Progressive disclosure. Common controls first; advanced values (raw
  camera vectors, ray step) sit in collapsed sections.
- Built for ImGui. The final UI is implemented in Dear ImGui with
  docking, so designs should use rectangular dockable panels, standard
  widgets and a single icon font. Avoid effects ImGui can't draw cheaply.

## Layout

Eight regions; the three side and bottom panels are dockable, resizable
and can be hidden; the viewport always fills what is left. Sizes below
are defaults at 100% scaling on a 1440x900 window. Scene and Inspector
run full height like Unity's Hierarchy and Inspector; everything under
the viewport belongs to the centre column.

| Region | Default size | Min | Resizable | Can hide |
|---|---|---|---|---|
| Top bar | full width x 48 px | - | No | No |
| Scene panel (left) | 240 px wide | 200 px | Yes, drag right edge | Yes (View menu, Ctrl+1) |
| Viewport toolbar | centre width x 48 px | - | No | No |
| Viewport | fills remaining space | 480 x 320 px | Indirectly | No |
| Slice strip | centre width x 132 px | 96 px | Yes, drag top edge | Yes (View menu, Ctrl+2) |
| Bottom dock | centre width x 240 px | 160 px | Yes, drag top edge | Yes (View menu, Ctrl+3) |
| Inspector (right) | 320 px wide | 280 px | Yes, drag left edge | Yes (View menu, Ctrl+4) |
| Status bar | full width x 28 px | - | No | No |

Docking rules:
- Scene and Inspector run the full height between the top bar and status
  bar, as in Unity. Slice strip and bottom dock sit under the viewport
  only.
- Any panel can be dragged by its header onto another panel to become a
  tab, or torn off as a floating window. Teal drop-zone highlight (accent
  at 20% opacity) while dragging.
- Panel sizes, visibility and arrangement persist between sessions and
  are saved inside .vpilot workspace files.
- View > Reset layout restores the defaults above. **Two named presets:
  Explore (default) and Deep review** (Scene hidden, Inspector 360px,
  bottom dock 320px, Metadata tab active).
- Panel resize handles: 4 px hit area, cursor changes to resize, handle
  line turns accent on hover.
- Focus viewport (F11 or double-click the viewport header): hide all
  panels except the top bar and status bar; press again to restore.

## Design tokens

Use these tokens everywhere; no one-off colours.

Colour (dark theme only for v2):

| Token | Hex | Use |
|---|---|---|
| bg/app | #0E1216 | App background, viewport frame, input fields |
| bg/chrome | #0A0D10 | Top bar, status bar |
| bg/panel | #12171C | Scene, Inspector, toolbars, dock |
| bg/raised | #18212A | Secondary buttons, chips |
| bg/viewport | #080B0E | Behind the 3D render and slices |
| bg/selected | #1C3236 | Selected tree row |
| border/default | #222C34 | Panel dividers, card outlines |
| border/strong | #2A3640 | Inputs, secondary buttons |
| text/primary | #E8EEF2 | Body and values |
| text/secondary | #9FB0BC | Labels, captions, inactive tabs |
| text/on-accent | #06201D | Text on accent buttons |
| accent | #4FD1BF | Primary buttons, active tool, slider fill, focus ring |
| accent/hover | #7FE6D6 | Hover on accent elements, active tool icon |
| accent/subtle | #173634 | Active tool / selected segment background |
| highlight | #F2CF5B | Region callouts, opacity curve, warnings in Safety |
| danger | #F07167 | Errors, Safety "Critical", X axis |
| axis/y | #7BD88F | Y axis |
| axis/z | #6EA8FE | Z axis |

Region colours (Scene tree swatches and 3D labels), in assignment order:
#F2CF5B, #F2995B, #6EA8FE, #B48CF2, #7BD88F, #F07167, #4FD1BF, #E9E2CF;
after eight, repeat.

Contrast: all text tokens pass 4.5:1 on bg/panel. Never put
text/secondary on bg/raised below 12 px.

Typography:

| Style | Font | Size / line height | Weight | Use |
|---|---|---|---|---|
| Title | Inter | 15 / 20 | 700 | App name |
| Section | Inter | 13 / 18 | 600 | Inspector section headers, selected item name |
| Body | Inter | 13 / 18 | 400 | Default UI text, buttons, tree rows |
| Label | Inter | 12 / 16 | 400 | Field labels, chips, tabs |
| Overline | Inter | 11 / 14, +1px tracking, uppercase | 600 | Panel titles (SCENE, INSPECTOR) |
| Mono | JetBrains Mono | 12 / 16 | 400 | Numbers, sizes, file names, status bar |
| Mono small | JetBrains Mono | 11 / 14 | 400 | Status bar, tree meta |

All numeric readouts use Mono with tabular figures so values don't
jitter while dragging.

Spacing, radii, elevation:
- Spacing scale: 2, 4, 6, 8, 12, 16, 24 px. Panel padding 16px
  horizontal, 12px vertical. Gap between controls in a section 12px.
- Radii: 6px inputs/chips/tree rows; 8px buttons/cards/toolbars; 10px
  grouped toolbars; full (999) for pills and switches.
- Elevation: none on docked panels (separate with 1px border/default).
  Floating windows, menus, dialogs: 1px border/strong + shadow
  0 8 24 rgba(0,0,0,0.5).

Icons: one outline icon set, 1.8px stroke at 24px, shown at 16px
(buttons) and 18px (viewport tools). Suggested: Lucide (ships as an icon
font ImGui can load). Every icon-only button has a tooltip with its name
and shortcut, e.g. "Measure (M)".

## Components and states

Design every component in all states: default, hover, pressed, focused
(2px accent ring, 2px offset), disabled (40% opacity, no hover), and,
where listed, selected or error.

| Component | Anatomy and size | States beyond default |
|---|---|---|
| Primary button | Icon 16 + label, 36px tall, 14px side padding, accent fill, text/on-accent 600 | Loading (spinner replaces icon) |
| Secondary button | 36px tall, bg/raised, 1px border/strong | - |
| Ghost button | Top-bar menu items; text only, no border; hover = bg/raised | Open (menu showing) |
| Icon button | 36x36 (toolbar) or 28x28 (panel header), 8/6px radius | Active: accent/subtle fill, 1px #2C6B64 border, accent/hover icon |
| Segmented control | 2-4 options in a bg/app track, 3px inner padding | Selected: #1F3A3D fill, 600 weight |
| Switch | 36x20 track, 16px knob; whole row is the click target | On: accent track, dark knob right |
| Slider row | Label (76px col) - track - value (48px Mono, right-aligned) | Dragging: value turns accent/hover; double-click to type exact number |
| Range slider | Two thumbs on one track, for clip min/max | Thumbs cannot cross |
| Text / search input | 36px tall, bg/app, 1px border/default | Error: 1px danger border + message below in danger 12px |
| Number input | As text input, Mono, with -/+ steppers | Out-of-range: clamps on blur, flashes border once |
| Dropdown | As text input with chevron; list opens below, max 8 rows then scrolls | Open |
| Tree row | 32px tall, 16px indent/level, chevron for groups, 8px colour swatch, label, Mono meta right | Selected (bg/selected), hidden (eye off, label 50% opacity), renaming (inline input) |
| Collapsible section | 44px header: chevron - title (Section style) - optional summary right | Collapsed/expanded; chevron rotates 90 deg in 150ms |
| Tab | Label 13px, 2px bottom indicator in accent when active | Active, has-update dot (6px accent dot after label) |
| Chip | Pill, 28px tall, bg/raised, 12px label | Running (spinner), done (check, then default after 2s) |
| Badge (Safety) | Dot + label + Mono score in a pill | Normal (accent), Elevated (highlight), Critical (danger) |
| Toast | Bottom-centre of viewport, 40px tall, bg/raised, optional "Undo" link | Info/success/warning/error; auto-dismiss 4s, paused on hover |
| Tooltip | bg/raised, 1px border/strong, 12px, max 280px wide; appears after 500ms | - |
| Viewport overlay label | bg/viewport 90%, 1px border in region colour (dimmed), 12px | Hover (full colour border), pinned (pin icon) |
| Dialog | 480px wide, title 15/600, body, right-aligned buttons, Esc closes | - |

## Region by region

### Top bar

Left to right: app logo + "VoxelPilot" + version pill ("v2 Cinematic") -
menus - flexible space - Open volume (primary) - Snapshot (secondary) -
Safety badge.

| Menu | Items |
|---|---|
| File | Open volume... (Ctrl+O) - Recent volumes - Open workspace... - Save workspace (Ctrl+Shift+S) - Save workspace as... - Save snapshot (Ctrl+S) - Export review bundle... - Export insight report... - Quit |
| View | Scene/Slices/Dock/Inspector toggles (Ctrl+1-4) - Focus viewport (F11) - Layout: Explore, Deep review, Reset layout - Camera: Front, Side, Top, Iso |
| Volume | Import settings... - Voxel spacing... - Clip box on/off (C) - Reset transfer function - Dataset health check |
| AI | Ask VoxelPilot (Ctrl+K) - Auto enhance - Discover regions - Scout focus slice - Transfer advisor |
| Review | Start guided review - Next/previous region ([ and ]) - Annotations - Measurements |
| Help | Guided walkthrough - Keyboard shortcuts (?) - About |

Safety badge: hover shows a tooltip with the main reason (e.g. "Label
budget 70% - consider hiding callouts"); click opens a popover with the
score breakdown (render time, GPU memory, labels, AI workload) and
recommended actions, each with an Apply button.

### Scene panel

- Header: overline "SCENE" + add (+) icon button. The + menu: Annotation,
  Measurement, Clip box.
- Search field filters rows as you type; matching text is bold; parents
  of matches stay visible.
- Tree structure: Volume (file name) > Discovered regions (count) > one
  row per region; Annotations (count); Measurements; Clip box; then
  Camera and Key light at the root.
- Row hover reveals an eye icon (show/hide in the viewport) and, for
  regions and annotations, a (...) menu: Rename, Focus camera, Change
  colour, Delete.
- Click selects and updates the Inspector; double-click a region focuses
  the camera on it; F2 renames; Delete removes (with Undo toast).
- Empty state (no volume): tree hidden, a dashed drop zone fills the
  panel: "Drop a .raw or .vpilot file here, or Open volume."

### Viewport toolbar

- Left: tool group - Orbit (O), Pan (P), Measure (M), Clip (C), Annotate
  (A). Exactly one is active. To the right of the group, one line of hint
  text for the active tool, e.g. "Drag to orbit - wheel to zoom".
- Right: camera presets segmented control - Front, Side, Top, Iso.
  Changing preset animates the camera over 300ms. After any manual
  orbit, no preset is selected.

### Viewport

- Shows the CUDA render scaled to fit; letterbox in bg/viewport.
- Overlays (all 12px, semi-opaque bg/viewport chips): top-left projection
  ("Perspective") and Cinematic state; bottom-left axis gizmo (X red, Y
  green, Z blue - click an axis to look down it); bottom-right render
  resolution + quality preset.
- Region callouts: a label chip with a 1px leader line to the region's
  anchor point. Hover a region in 3D to show its callout plus a one-line
  explanation; click to select it in the Scene tree.
- Mouse: left-drag = active tool, right-drag = pan (any tool), wheel =
  zoom, middle-drag = orbit (any tool), double-click = focus at point.
- Loading state: centred spinner + "Uploading to GPU... 64%"; the
  previous image stays visible at 40% opacity.
- Empty state: centred card with Open volume, "or drop a file anywhere",
  and the three most recent files.

### Slice strip

- Three equal cards: Axial (Z), Coronal (Y), Sagittal (X). Header: name +
  Mono position ("Z 0.50").
- Each card shows the slice with teal crosshairs where the other two
  planes cross it.
- Drag in a card to move the crosshair (updates the other two slices and
  the 3D slice planes); wheel scrolls through slices one voxel at a
  time; double-click enlarges the card to fill the viewport (Esc
  returns).
- In Measure mode, clicks in a slice place points A then B.

### Bottom dock

- Tabs: AI Assist - Histogram - Metadata - Log. The last-used tab is
  remembered.
- **AI Assist**: prompt input ("Ask VoxelPilot", Enter or Apply) -
  quick-action chips: Auto enhance, Discover regions, Health check,
  Guided review, Export bundle - a "Last action" card on the right
  showing what changed (the before -> after values in Mono) with Undo,
  and the Safety status for the next AI action. If Safety blocks an
  action, its chip is disabled with a tooltip explaining why.
- **Histogram**: intensity histogram, full width, 140px tall; bars inside
  the current transfer window are accent, others #2E3B45. Drag on the
  histogram to set the window directly (updates Center and Width).
- **Metadata**: 4-column grid of label/value pairs - Dimensions, Data
  type, Voxels, Size, Spacing, Value range, Palette, Clipping, Health
  score.
- **Log**: timestamped Mono lines, newest at the bottom, auto-scrolls
  unless the user scrolled up; warnings in highlight, errors in danger;
  Copy all button.

### Inspector

Header: overline "INSPECTOR", then the selected item's name + type pill.
Sections shown depend on the selection:

| Selected | Sections (default open in bold) |
|---|---|
| Volume | Rendering, Transfer function, Clipping, Import & spacing |
| Region | Region (name, colour, visible, voxel share, archetype), Relations, Notes |
| Annotation | Annotation (text, slice, colour), Position |
| Measurement | Points (A and B XYZ, Set from slices), Distance (voxels and world units) |
| Clip box | Bounds (X, Y, Z range sliders), Enabled switch, Reset |
| Camera | View (preset, FOV), Advanced (position/direction/up vectors, collapsed) |
| Key light | Light (position XYZ, intensity) |
| Nothing | Short hint: "Select something in the Scene panel or click the volume." |

Volume sections in detail:
- **Rendering**: GPU quality segmented control (Fast/Balanced/Quality) -
  Cinematic look switch -> when on, reveals Rim, Glow, Contrast sliders -
  Adaptive quality switch - Advanced (collapsed): step size, opacity
  threshold, skip multiplier, render resolution.
- **Transfer function**: 72px preview showing the histogram with the
  opacity curve in highlight; drag the curve's peak to move
  Center/Opacity and its sides to change Width - Center, Width, Opacity
  sliders - palette picker (Bone, Thermal, Gray, Teal - four gradient
  swatches) - Invert switch - Reset link.
- **Clipping**: summary "Full volume" or "Active" in the header - X/Y/Z
  range sliders in axis colours - Enabled switch.
- **Import & spacing**: read-only dimensions and type, voxel spacing XYZ
  inputs, Re-import... button.

### Status bar

Mono 11px, left to right: volume size and type - GPU ms - FPS - Clip
on/off - Cinematic on/off - flexible space - workspace save state
("Saved - foot.vpilot" or "Unsaved changes" in highlight). Clicking a
segment jumps to its setting. Toasts never appear here.

## Key user flows

Prototype each flow end to end. Each step names the exact control, what
the user sees, and where focus goes.

### 1. First launch

Splash (1.5s max): logo, "VoxelPilot v2", GPU name detected (e.g.
"NVIDIA RTX..."). If no CUDA GPU is found, the splash shows an error
dialog instead.

Main screen in its empty state; a one-time coach card in the viewport:
"New here? Take the 60-second tour" with Start tour and Skip.

Tour = 5 numbered spotlight steps (dim everything except the highlighted
region): Open volume -> Viewport tools -> Inspector -> AI Assist ->
Safety badge. Each step has Back, Next, step count ("2 of 5") and Esc to
exit. Tour can be restarted from Help.

### 2. Open a RAW volume

RAW files have no header, so the user must confirm dimensions and data
type before loading.

User clicks Open volume (or Ctrl+O, or drops a file). System file dialog
filters *.raw, *.bin, *.dat.

Import dialog opens (560px): file name and size at top; Width, Height,
Depth number inputs; Data type dropdown (uint8, uint16, float32); voxel
spacing XYZ (default 1.0).

Fields pre-filled from the file name when it matches a pattern like
256x256x256_uint8; a hint line says "Detected from file name".

A live check row compares expected size (WxHxDxbytes) with the file
size: green check "Matches file size (16.0 MB)", or highlight warning
"Expected 32.0 MB, file is 16.0 MB - check data type or dimensions".
Load stays enabled on a mismatch but asks for confirmation.

Load -> dialog closes, viewport loading state with progress, previous
volume (if any) dims.

On success: render appears, auto enhance runs if the histogram allows
it, toast "Volume loaded - auto enhance applied. Undo". Scene tree
fills; the Volume row is selected so the Inspector shows Rendering and
Transfer.

On failure: dialog stays open with the error under the relevant field
(e.g. "File is smaller than these dimensions need").

### 3. Tune the view

Volume selected -> Inspector > Transfer function. User drags Center;
render updates live (target under 50ms per frame), the histogram
highlight in the dock follows.

User picks a palette swatch; render updates instantly.

User toggles Cinematic look; Rim, Glow, Contrast sliders slide open
beneath it (150ms).

Every committed change (on mouse-up) adds one entry to the undo stack.
Ctrl+Z/Ctrl+Y step through them; a toast names what was undone ("Undid:
Transfer center").

### 4. Measure a distance

Press M or click Measure. Hint: "Click the first point".

Click in the viewport or a slice -> point A (teal ring). Hint: "Click
the second point".

Click again -> point B; dashed line and a Mono label "42.6 vox - 42.6
units" appear. The Measurement row is added to the Scene tree and
selected; the Inspector shows Points and Distance.

Drag either point to adjust. Esc cancels an unfinished measurement.

### 5. Discover regions and guided review

AI Assist > Discover regions (or AI menu). If Safety is Elevated, a
confirmation shows the cost first ("This may slow rendering while it
runs").

Chip shows Running; the Log streams progress. On finish, regions appear
in the Scene tree with colour swatches and as callouts in the viewport;
toast "4 regions found".

Guided review opens a floating card (420px, top-right of the viewport):
region name, colour, archetype, voxel share, relationship note, Notes
field; Previous, Next, Finish. The camera animates to frame each region.

The user can correct a region (rename, change archetype); the card says
"Correction saved to local memory".

### 6. Save and share

Ctrl+Shift+S -> save .vpilot workspace (volume path, camera, transfer,
clip, regions, annotations, layout). Status bar changes to "Saved -
name.vpilot".

Ctrl+S -> snapshot PNG of the viewport only; toast with Show in folder.

File > Export review bundle / insight report -> save dialog, then toast
with Open.

## Feedback, safety and error states

The user always knows what the app is doing: every action gets feedback
within 100ms, and anything over 1s shows progress.

| Situation | What the user sees | Where |
|---|---|---|
| Action applied (transfer, preset, AI suggestion) | Toast "<Action> applied" + Undo | Bottom-centre of viewport |
| Long task (load, discover regions, export) | Progress in place (viewport spinner or chip spinner) + Log lines; Cancel where possible | Where the action started |
| Safety Normal | Badge in accent, no interruption | Top bar |
| Safety Elevated | Badge turns highlight; heavy actions ask to confirm; popover lists suggested fixes (lower quality, hide callouts) each with Apply | Top bar + popover |
| Safety Critical | Badge turns danger; AI actions disabled with tooltip; one-time banner across viewport top: "Rendering is over budget - switch to Fast?" Switch/Dismiss | Viewport top |
| Import size mismatch | Inline warning in Import dialog | Import dialog |
| Load failed | Inline error under the field at fault; dialog stays open | Import dialog |
| No CUDA GPU / driver too old | Blocking dialog: what was found, what is needed, Open driver page and Quit | Over splash |
| Out of GPU memory on load | Dialog: "This volume needs X MB; Y MB free" with Load at half resolution/Cancel | Over viewport |
| Unsaved changes on quit or open | Dialog: Save/Don't save/Cancel | Centred |
| Empty panels | One line of guidance + the action that fills them (e.g. Histogram: "Load a volume to see its histogram") | In the panel |

Rules: never more than one toast at a time (a new one replaces the old);
errors never auto-dismiss; AI and Safety never apply changes without a
user click.

## Keyboard shortcuts and accessibility

Single-key shortcuts work only when focus is not in a text field. Show
them in menus, tooltips and the ? overlay (a two-column cheat sheet
dialog).

| Keys | Action |
|---|---|
| Ctrl+O | Open volume |
| Ctrl+S | Save snapshot |
| Ctrl+Shift+S | Save workspace |
| Ctrl+Z / Ctrl+Y | Undo/redo |
| Ctrl+K | Focus the AI prompt |
| Ctrl+T | Toggle Cinematic look |
| Ctrl+1...Ctrl+4 | Toggle Scene, Slices, Dock, Inspector |
| O, P, M, C, A | Orbit, Pan, Measure, Clip, Annotate tools |
| 1, 2, 3, 4 | Camera Front, Side, Top, Iso |
| F | Frame the selection (or the whole volume) |
| [ and ] | Previous/next region in review |
| F2 / Delete | Rename/delete selected item |
| F11 | Focus viewport |
| Esc | Cancel current tool action, close dialog or popover |
| ? | Shortcut cheat sheet |

Ctrl+T, Ctrl+O and Ctrl+S already exist in the current build; keep them.

Accessibility:
- Tab moves through panels in reading order (top bar -> Scene -> viewport
  toolbar -> slices -> dock -> Inspector -> status bar); arrow keys move
  within a tree, segmented control or tab row.
- Visible focus ring on every interactive element.
- Text contrast at least 4.5:1; colour is never the only signal (Safety
  badge always has a word; axis colours always have X/Y/Z letters;
  region swatches have names).
- Minimum interactive size 28x28px in dense panels, 36px for primary
  actions.
- UI scale setting in Help > Preferences: 100%, 125%, 150%, 200%.

## Window sizes, DPI and handoff

VoxelPilot is a desktop app; design for windows from 1280x720 up to
2560x1440, at Windows scaling of 100-200%.

- 1920x1080 and up: all panels open at default sizes; extra width goes
  to the viewport.
- 1440x900 (primary frame): as specified in Layout.
- 1280x720: Slice strip collapses to a 32px header row (click to
  expand); bottom dock defaults to 200px; Inspector 280px.
- Below 1280 wide: Scene panel auto-hides into a 40px icon rail on the
  left (click to fly out over the viewport).
- DPI: every size in this doc is at 100%; all sizes, including fixed
  ones like the 420px guided review card, scale with the UI scale
  factor. Provide icons as vector or at 1x and 2x.

Open questions (from design, unresolved as of this mirror):
- Light theme: needed for v2, or dark only?
- Does the Scene tree need drag-to-reorder or grouping of regions?
- Should the slice strip be its own dockable panel (movable to the
  right side) or stay fixed under the viewport?
- Which icon set: Lucide (suggested) or a custom set?
