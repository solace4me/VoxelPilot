# VoxelPilot

`VoxelPilot` is a standalone CUDA volume exploration workstation for local NVIDIA hardware. It combines real-time 3D rendering, orthogonal slice review, measurement tools, workspace presets, and demo-ready presentation flow in one desktop app.

## Highlights

- Real-time CUDA volume rendering
- Axial, coronal, and sagittal slice review
- Transfer mapping, clipping, histogram, and metadata panels
- Mouse orbit/zoom, preset camera views, and measurements
- PNG snapshot export and `.vpilot` workspace save/load
- Demo splash, guided walkthrough, Help, and About panels

## Quick Start

Build locally:

```powershell
make standalone
```

Clean local build output:

```powershell
make clean
```

Run the packaged Windows demo build:

- `release/voxelpilot-windows-nvidia/Run_VoxelPilot.bat`

## Repo Layout

- `standalone_main.cu` - main application entry point and UI
- `renderer/` - CUDA renderer implementation
- `common/` - shared structs, math helpers, and renderer API
- `imgui/` and `glfw/` - UI dependencies
- `third_party/` - bundled helpers such as tinyfiledialogs and GLEW
- `release/voxelpilot-windows-nvidia/` - packaged NVIDIA laptop demo bundle

## Demo Package

The main handoff folder is:

- `release/voxelpilot-windows-nvidia`

Useful docs inside it:

- `FINAL_RELEASE_CHECKLIST.md`
- `FIRST_RUN_CHECKLIST.md`
- `SAMPLE_WORKFLOW.md`
- `JUDGE_SUMMARY.md`

## Pitch

`VoxelPilot is a presentation-ready CUDA volume exploration workstation, not just a renderer.`

## Notes

- An NVIDIA GPU and working CUDA driver are required to run the app.
- The release folder intentionally keeps the packaged demo binary for MVP/demo use.
