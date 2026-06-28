# VoxelPilot

`VoxelPilot` is a standalone CUDA volume exploration workstation for local NVIDIA hardware. It combines real-time 3D rendering, orthogonal slice review, measurement tools, workspace presets, and demo-ready presentation flow in one desktop app.

## Highlights

- Real-time CUDA volume rendering
- Headerless raw volume import with arbitrary `width x height x depth`
- Import support for `uint8`, `uint16`, and `float32` voxel data
- Axial, coronal, and sagittal slice review
- Transfer mapping, clipping, histogram, and metadata panels
- Mouse orbit/zoom, preset camera views, and measurements
- PNG snapshot export, lightweight HTML insight reports, and `.vpilot` workspace save/load
- AI Assist prompts, local object identification, slice annotations, 3D label overlays, hover explanations, and brick-cache telemetry
- Demo splash, guided walkthrough, and About panel

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

## Demo Launch Guide

- Use a Windows laptop with an NVIDIA GPU and a working display driver.
- Confirm `nvidia-smi` responds before launch.
- Start the packaged demo with `release/voxelpilot-windows-nvidia/Run_VoxelPilot.bat`.
- Confirm `Controls`, `Insights`, `AI Assist`, `Metadata`, `About`, and `Status` are visible.
- If no dataset is loaded yet, the app should still open on the synthetic fallback volume.

## Quick Demo Flow

- Use `Browse...`, confirm `Width`, `Height`, `Depth`, and `Data Type`, then click `Load Volume`.
- Try `Front` and `Isometric`, then orbit and zoom in the main viewport.
- Adjust `Sagittal X`, `Coronal Y`, and `Axial Z` in `Insights` for slice review.
- Click `Analyze Loaded Volume` in `AI Assist` to generate a local material/object summary.
- Click `Refresh Quant Metrics`, then `Export Insight Report` to save a lightweight HTML review report.
- Use `AI Assist` prompts such as `show high-density shell` or `hide below 0.3 intensity`.
- Enable `Slice Click Labels`, keep `Show Labels in 3D Render` on, click a slice, then export the label mask for downstream analysis.
- Use `Set A From Slices` and `Set B From Slices` for a quick measurement pass.
- Click `Save Snapshot` to export a PNG still.
- Save a workspace session if you want to revisit the same setup later.

## Troubleshooting

- If VoxelPilot fails to start on an NVIDIA laptop, check the NVIDIA driver, confirm the app is using the discrete GPU, and verify OpenGL is available on the active display path.

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


## Laptop-Safe Performance Guardrails

- Current AI features are local heuristics and histogram summaries, not large neural-network inference.
- Quantitative metrics reuse the existing histogram and annotation metadata, so they avoid extra volume-wide GPU passes.
- `GPU Quality Preset` keeps the core CUDA renderer intact: `Balanced GPU` is the default, `Quality GPU` uses finer ray steps, and `Interactive GPU` is available for very heavy laptops/datasets.
- `Auto Enhance View` only changes transfer-window and opacity parameters; it does not downsample the viewport or replace the CUDA renderer.
- Report export writes a small HTML file and does not block rendering except during the save operation.
- Sample datasets should stay modest for demo laptops; prefer `uint8` raw volumes around `64^3` to `256^3` unless true out-of-core paging is enabled.

## AI Concept Note

VoxelPilot currently uses local, explainable heuristic AI rather than a cloud model or trained neural network. The AI Assist workflow combines a flexible local intent parser, histogram/material heuristics, GPU transfer-function tuning, ray-based 3D hover picking, flood-fill segmentation, and human-confirmed annotations. Prompts are not limited to one hardcoded list: users can type intent-style requests such as `make it brighter`, `show bone`, `hide below 0.3`, `switch to quality GPU rendering`, or `reset view`, and VoxelPilot maps them to safe local actions. This makes the demo transparent and offline-friendly while leaving a clean path for future model-backed 3D segmentation with ONNX, MONAI, or SAM-style medical models.


## Additional Test Dataset

A small public NiBabel anatomical NIfTI sample is included as an app-ready raw volume for testing the AI Assist, quantitative metrics, report export, and hover explanation workflow.

```powershell
.\launch_voxelpilot_windows.bat --volume .\sample-data\nibabel-anatomical\nibabel_anatomical_33x41x25_uint8.raw --width 33 --height 41 --depth 25 --type uint8
```

For non-interactive verification, the app can render one CUDA frame and write the renderer output directly:

```powershell
.\volume_renderer_standalone.exe --volume .\sample-data\nibabel-anatomical\nibabel_anatomical_33x41x25_uint8.raw --width 33 --height 41 --depth 25 --type uint8 --snapshot .\build\nibabel_snapshot.png
```

See `sample-data/nibabel-anatomical/README.md` for conversion notes and source details.

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
