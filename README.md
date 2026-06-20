# VoxelPilot

`VoxelPilot` is a standalone CUDA volume exploration workstation for local NVIDIA hardware. It combines real-time 3D rendering, orthogonal slice review, measurement tools, workspace presets, and demo-ready presentation flow in one desktop app.

## Highlights

- Real-time CUDA volume rendering
- Headerless raw volume import with arbitrary `width x height x depth`
- Import support for `uint8`, `uint16`, and `float32` voxel data
- Axial, coronal, and sagittal slice review
- Transfer mapping, clipping, histogram, and metadata panels
- Mouse orbit/zoom, preset camera views, and measurements
- PNG snapshot export and `.vpilot` workspace save/load
- Demo splash, guided walkthrough, Help, and About panels

## Quick Start

Build on Windows with the native PowerShell script:

```powershell
.\build_windows.ps1
```

Launch the app:

```powershell
.\launch_voxelpilot_windows.bat
```

Launch the app and preload a raw dataset at startup:

```powershell
.\launch_voxelpilot_windows.bat --volume .\sample-data\foot_256x256x256_uint8.raw
```

Build with GNU `make` if your environment already provides it:

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
- `build_windows.ps1` - PowerShell build entry point with GPU arch detection
- `build_windows.bat` - native Windows build entry point
- `BUILD_WINDOWS.md` - Windows prerequisites and step-by-step setup
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

## Supported Volume Files

- Headerless raw voxel files such as `.raw`, `.bin`, or `.dat`
- Arbitrary dimensions, as long as the imported size matches `width x height x depth x bytes_per_voxel`
- Voxel data types: `uint8`, `uint16`, and `float32`

You can import a dataset in two ways:

- In the app: click `Browse...`, then set `Width`, `Height`, `Depth`, and `Data Type` before clicking `Load Volume`
- At startup: pass `--volume` and optionally `--dim` or `--width` / `--height` / `--depth` plus `--type`

If the filename contains a pattern such as `foot_256x256x256_uint8.raw`, VoxelPilot auto-detects the dimensions and data type.

## Notes

- An NVIDIA GPU and working CUDA driver are required to run the app.
- The release folder intentionally keeps the packaged demo binary for MVP/demo use.
