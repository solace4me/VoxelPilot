# VoxelPilot Real-App Demo Video Design

Date: 2026-07-03
Status: Approved design

## Purpose

Create a short, authentic MP4 demo of VoxelPilot and a matching LinkedIn post. The demo should show the actual app running, not a mockup: launch VoxelPilot, load `sample-data/foot_256x256x256_uint8.raw`, rotate the volume across X and Y, switch to Cinematic Mode, then trigger Auto Enhance or the nearest existing AI Assist enhancement path.

The LinkedIn post should present VoxelPilot as a first-half-of-the-year project inspired by winter-quarter GPU Computing and Data Visualization classes.

## Selected Approach

Use a real app capture. This was selected over a polished composite because the strongest proof for a portfolio/social post is seeing VoxelPilot operate as a live CUDA/OpenGL workstation.

The target video length is 20 to 30 seconds, with no voiceover required. If captions are added, they should be minimal and factual.

## Demo Storyboard

1. Launch VoxelPilot and show the workspace opening.
2. Load `foot_256x256x256_uint8.raw` as a `256 x 256 x 256 uint8` volume.
3. Rotate the view across X and Y so the dataset reads as an interactive 3D object.
4. Enable Cinematic Mode to show rim/glow/contrast styling.
5. Apply Auto Enhance or AI Assist enhancement and hold on the improved final view.

## Capture Strategy

Prefer existing app paths first:

- launch with CLI volume arguments if available;
- use existing Cinematic Mode and Auto Enhance UI behavior;
- use any existing snapshot or scripted review path for supporting verification.

If the app lacks enough automation to produce a smooth capture, add the smallest demo-only automation path needed. Any new automation should be opt-in through CLI flags and should not affect normal interactive behavior.

Acceptable implementation options:

- a scripted demo mode inside `standalone_main.cu` that drives camera orbit, cinematic state, and enhancement over time;
- frame export to PNGs followed by MP4 assembly;
- screen capture of the real app if frame export is not practical in the current environment.

## App Behavior Constraints

- Do not change the default renderer behavior.
- Do not rename or move the sample dataset.
- Do not overwrite unrelated local edits.
- Do not claim diagnostic or anatomical certainty.
- Keep AI language honest: local explainable assistance, histogram/material heuristics, transfer tuning, and future model-backed possibilities.

## Output Files

Expected final deliverables:

- `build/voxelpilot_foot_demo.mp4`
- `build/voxelpilot_linkedin_post.md`

If environment limits prevent direct MP4 capture, provide the best verified intermediate artifact, such as rendered frames or a PNG sequence, and explain the blocker.

## LinkedIn Post Direction

The post should be short, first-person, and polished without sounding overproduced. It should cover:

- VoxelPilot as a major first-half-of-the-year accomplishment;
- inspiration from winter-quarter GPU Computing and Data Visualization classes;
- what the app does: real-time CUDA volume rendering, interactive 3D exploration, cinematic visualization, and AI-assisted enhancement;
- selling point: a compact workstation that combines rendering, review, presentation, and explainable AI assistance in one local NVIDIA app;
- future direction: continuing to improve it, especially using AI to do more in visualization, interpretation, segmentation, and scientific/medical/engineering data exploration.

## Acceptance Criteria

- The MP4 is short and viewable in common players.
- The video clearly shows VoxelPilot launching or already launched into the real app workspace.
- The foot dataset is visibly loaded.
- The view rotates across two axes.
- Cinematic Mode or cinematic styling is visibly enabled.
- Auto Enhance or the nearest existing AI enhancement path is shown or reflected in the final view.
- The LinkedIn post uses "winter quarter" throughout.
- The post is suitable to paste into LinkedIn with only minor personal edits.

## Risks And Mitigations

- Risk: GUI automation is unreliable on Windows. Mitigation: prefer CLI/demo-frame export where possible.
- Risk: MP4 creation depends on local video tooling. Mitigation: inspect available tools first and fall back to frame sequence if needed.
- Risk: AI wording could overclaim. Mitigation: describe current features as local and explainable, while framing neural/model-backed AI as future improvement.
- Risk: existing uncommitted renderer work may be in progress. Mitigation: keep edits scoped and avoid reverting any existing changes.

## Self-Review

- No incomplete requirements remain.
- The winter-quarter correction is included.
- The design is scoped to one short demo video and one LinkedIn post.
- The capture path is flexible enough for the current Windows/CUDA environment while preserving normal app behavior.
