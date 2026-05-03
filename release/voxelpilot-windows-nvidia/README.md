# VoxelPilot Demo Build for Windows NVIDIA Laptop

This folder contains the polished standalone demo build of `VoxelPilot`.

`VoxelPilot` is a presentation-ready CUDA volume exploration workstation that combines real-time volume rendering, slice review, measurement tools, capture/export, workspace presets, and guided demo flow in one NVIDIA-laptop app.

## Included Files

- `volume_renderer_standalone.exe`
  The compiled VoxelPilot application.
- `Run_VoxelPilot.bat`
  Recommended launcher for the NVIDIA laptop demo.
- `launch_voxelpilot_windows.bat`
  Original launcher script kept for development continuity.
- `RUNTIME_NOTES.txt`
  Runtime requirements and quick troubleshooting notes.
- `FIRST_RUN_CHECKLIST.md`
  Step-by-step checklist for the first laptop startup test.
- `SAMPLE_WORKFLOW.md`
  Short guided workflow for loading a volume, adjusting views, measuring, and saving a snapshot.
- `PRESENTATION_SCRIPT.md`
  Short live-demo script for presenting VoxelPilot.
- `PITCH_DECK_OUTLINE.md`
  Slide-by-slide outline for a short project presentation.
- `DEMO_PRESENTATION.md`
  Polished 8-slide markdown presentation with speaker notes.
- `FINAL_RELEASE_CHECKLIST.md`
  Final handoff checklist for copying, testing, and presenting on the NVIDIA laptop.
- `JUDGE_SUMMARY.md`
  One-page project summary covering the problem, solution, features, selling points, and roadmap.
- `FINAL_SUBMISSION_BUNDLE.md`
  Audience-based guide to which file should be opened first and which materials are for judges versus the live demo.

## How To Run

1. Copy this full folder to the NVIDIA laptop.
2. Double-click `Run_VoxelPilot.bat`.
3. Follow `FIRST_RUN_CHECKLIST.md` for the first startup test.
4. Use `SAMPLE_WORKFLOW.md` for the hands-on demo flow after the app opens successfully.

## Demo Highlights

- Branded startup splash with a short launch sequence.
- Demo mode banner and one-click guided walkthrough overlay.
- Real-time CUDA volume rendering in a merged single-machine app.
- Orthogonal slice review alongside the 3D render.
- Transfer mapping, clipping, histogram insight, and render controls.
- Measurement tools with voxel and world-unit distance readouts.
- PNG snapshot export and workspace session save/load.
- In-app Help and About panels for guided presentation use.

## Top Selling Points

- It is more than a renderer:
  VoxelPilot is a complete interactive review workstation, not just a CUDA visualization prototype.
- It is demo-ready:
  The splash screen, help panel, about panel, walkthrough overlay, launcher flow, and packaged release make it easy to present live.
- It combines 3D and 2D understanding:
  Users can inspect the volume in the main 3D view while cross-checking axial, coronal, and sagittal slices.
- It supports practical review tasks:
  Measurement, clipping, transfer tuning, snapshot export, and workspace presets make the app useful during exploration and repeat demos.
- It is built for the NVIDIA laptop setup:
  The architecture is now merged into a single-machine application that is ready to run and test directly on local NVIDIA hardware.

## Best Short Pitch

`VoxelPilot is a presentation-ready CUDA volume exploration workstation, not just a renderer.`

## Expected Hardware

- Windows laptop with an NVIDIA GPU
- NVIDIA graphics driver installed and working
- OpenGL-capable display path

## Notes

- This build was packaged on April 30, 2026.
- On a non-NVIDIA machine the app will exit early with a CUDA runtime message. That is expected.
