# VoxelPilot v2 — Cinematic AI Preview

`VoxelPilot v2` is a standalone CUDA volume exploration workstation for local NVIDIA hardware. It combines real-time 3D rendering, orthogonal slice review, measurement tools, workspace presets, and demo-ready presentation flow in one desktop app.

## Highlights

- Real-time CUDA volume rendering
- V2 Cinematic Lite rendering track with GPU rim, glow, and contrast styling
- Headerless raw volume import with arbitrary `width x height x depth`
- Import support for `uint8`, `uint16`, and `float32` voxel data
- Axial, coronal, and sagittal slice review
- Transfer mapping, clipping, histogram, and metadata panels
- Mouse orbit/zoom, preset camera views, and measurements
- PNG snapshot export, AI review bundles, lightweight HTML insight reports, and `.vpilot` workspace save/load
- AI Assist prompts, Adaptive Semantic Discovery, local object identification, slice annotations, cinematic callouts, 3D label overlays, hover explanations, and brick-cache telemetry
- Local Performance Safety Governor that watches render time, estimated GPU memory pressure, labels, callouts, and optional AI workload before recommending user-approved safe actions
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

Run fast unit/regression tests:

```powershell
.\run_tests_windows.bat
```

Run the Phase 0 GPU render safety baseline with the canonical foot volume:

```powershell
.\build_windows.ps1
.\run_render_regression_windows.bat
```

The render baseline writes `build\foot_render_regression.png` and `build\foot_render_regression_metrics.json`, then fails if the snapshot is black, tiny, badly cropped, or unexpectedly slow.

Run the tiny VTK headsq CT verification sample:

```powershell
.\run_headsq_verification_windows.bat
```

This writes `build\vtk_headsq_bone_snapshot.png` and checks CT-appropriate visibility thresholds for the `64 x 64 x 93` dataset.

Run the larger 16 MB Bonsai CT verification sample:

```powershell
.\run_bonsai_verification_windows.bat
```

This checks another `256 x 256 x 256 uint8` public dataset without changing the canonical foot baseline.

Run the Bonsai ASD discovery verifier:

```powershell
.\run_bonsai_asd_verification_windows.bat
```

This writes `build\bonsai_asd_report.json` using the same ASD-Lite region discovery code used by the app, so the 16 MB Bonsai dataset has both render and AI-interpretation checks.

Launch Bonsai directly into the one-click semantic demo workflow:

```powershell
.\volume_renderer_standalone.exe --volume .\sample-data\bonsai_256x256x256_uint8.raw --width 256 --height 256 --depth 256 --type uint8 --semantic-demo
```

Generate an opt-in Cinematic Lite snapshot without changing the default renderer:

```powershell
.\volume_renderer_standalone.exe --volume .\sample-data\foot_256x256x256_uint8.raw --width 256 --height 256 --depth 256 --type uint8 --snapshot .\build\foot_cinematic_lite.png --cinematic-lite
```

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
- Click `Run Semantic Demo` in `AI Assist` for a one-click local AI review: auto-enhanced transfer, object summary, ASD-Lite discovery, cinematic mode, label overlay, and callouts.
- Check `Performance Safety` in `AI Assist`; it explains whether the current dataset is healthy, watch-level, pressure-level, or critical without silently changing render quality.
- Click `Analyze Loaded Volume` in `AI Assist` to generate a local material/object summary if you want to run the steps manually.
- Click `Refresh Quant Metrics`, then `Export Insight Report` to save a lightweight HTML review report. Use `Export AI Review Bundle` to save a PNG snapshot, ASD regions JSON, relationships JSON, label mask, and linked HTML report together.
- For scripted review, run `volume_renderer_standalone.exe --volume sample-data\bonsai_256x256x256_uint8.raw --width 256 --height 256 --depth 256 --type uint8 --semantic-demo --review-bundle build` to export the same bundle without opening the folder dialog.
- Use `AI Assist` prompts such as `show high-density shell` or `hide below 0.3 intensity`.
- Click `Discover Regions` in `AI Assist` to run ASD-Lite, then keep or enable `Cinematic Callouts` for leader-line labels on the top or selected discovered regions. `Selected Region Glow` boosts the selected label directly inside the CUDA label overlay without adding a second render pass. Hover over slices or the 3D render for cautious region descriptions, stability, importance, and relationship notes. Select a discovered region to confirm, rename, reject, save, or reload correction memory.
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

`VoxelPilot v2 is a presentation-ready CUDA volume exploration workstation with a cinematic AI-assisted interpretation track, not just a renderer.`


## Laptop-Safe Performance Guardrails

- Current AI features are local heuristics and histogram summaries, not large neural-network inference.
- Quantitative metrics reuse the existing histogram and annotation metadata, so they avoid extra volume-wide GPU passes.
- `GPU Quality Preset` keeps the core CUDA renderer intact: `Balanced GPU` is the default, `Quality GPU` uses finer ray steps, and `Interactive GPU` is available for very heavy laptops/datasets.
- `Auto Enhance View` only changes transfer-window and opacity parameters; it does not downsample the viewport or replace the CUDA renderer.
- `Cinematic Mode` is off by default; when enabled, it uses a single CUDA raymarch styling branch with bounded rim, glow, and contrast controls.
- `Cinematic Callouts` are an ImGui foreground overlay fed by ASD metadata; they add no CUDA kernels, model inference, or extra volume scan.
- `Selected Region Glow` reuses the existing CUDA label-mask overlay and only brightens the currently selected label id in the same raymarch pass.
- Report export writes a small HTML file and does not block rendering except during the save operation.
- Sample datasets should stay modest for demo laptops; prefer `uint8` raw volumes around `64^3` to `256^3` unless true out-of-core paging is enabled.

## Adaptive Semantic Discovery

VoxelPilot v2 can run ASD-Lite from `AI Assist` to discover candidate regions in arbitrary imported volumes. ASD-Lite is local and explainable: it groups connected intensity regions, computes shape archetypes such as `rod`, `plate`, `shell`, `blob`, and `cluster`, estimates stability and importance, detects simple region relationships, and exports the provenance beside the label mask.

ASD-Lite deliberately uses cautious names such as `dense rod-like structure` instead of claiming exact anatomy or diagnosis. This keeps the laptop demo honest for unknown medical, industrial, or synthetic datasets while creating a clean bridge to future model-backed recognition. The CUDA renderer remains the main visual engine; ASD reuses the existing label-mask overlay path rather than adding a heavy render pass.

Phase 2C adds human-in-the-loop correction memory. Users can select a discovered region, rename or confirm it, reject false positives, and save/load `voxelpilot_asd_memory.json`. Future ASD passes cautiously reapply those corrections by matching archetype, density class, intensity profile, compactness, elongation, and size ratio, with provenance shown as user-confirmed or rejected rather than hidden model output.

Phase 2D adds cinematic ASD callouts. The overlay projects selected/top region centroids into screen space and draws glow points, leader lines, and compact cards with cautious names, archetype, confidence, stability, and provenance. This improves interpretability for demos without changing the core CUDA renderer.

## AI Concept Note

VoxelPilot currently uses local, explainable heuristic AI rather than a cloud model or trained neural network. The AI Assist workflow combines a flexible local intent parser, histogram/material heuristics, GPU transfer-function tuning, ray-based 3D hover picking, Adaptive Semantic Discovery, flood-fill segmentation, and human-confirmed annotations. Prompts are not limited to one hardcoded list: users can type intent-style requests such as `make it brighter`, `show bone`, `hide below 0.3`, `switch to quality GPU rendering`, or `reset view`, and VoxelPilot maps them to safe local actions. This makes the demo transparent and offline-friendly while leaving a clean path for future model-backed 3D segmentation with ONNX, MONAI, or SAM-style medical models.


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
