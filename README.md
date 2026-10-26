# VoxelPilot v2

VoxelPilot is a laptop-first workbench for exploring 3D scans: CT and micro-CT of specimens, industrial parts, packed objects, and teaching datasets. It renders volumes in real time with CUDA, finds and names regions on its own, and lets you measure, clip, annotate and export, all locally on a laptop with an NVIDIA GPU.

It is built for research, non-destructive inspection and teaching. **It is not a medical device** and makes no diagnostic claims: discovered regions get cautious, descriptive names ("small dense rod · center"), never anatomy or pathology.

## Why it exists

Volume tools usually assume a workstation GPU. VoxelPilot is designed around the opposite constraint:

- **Fits the GPU it has.** A scan is checked against free GPU memory before upload. If it does not fit, it is shown at 1/2 or 1/4 resolution with proportions and measurements kept true, and the app says so; nothing is reduced silently.
- **Full detail where it matters.** Clip to the part you care about and load just that box from the file at native resolution.
- **Lean memory.** About 3 bytes per voxel on the GPU (16-bit texture + label mask; gradients computed on the fly).
- **Finds structure for you.** Adaptive Semantic Discovery (ASD) splits materials, finds enclosed cavities, classifies shapes, and names regions by size, material, shape and position, with a human-in-the-loop "Is this right?" memory.
- **Never blocks.** Region discovery runs in the background; the viewport keeps rendering.

Measured on one laptop GPU with the 512 x 512 x 373 backpack CT scan (see `sample-data/backpack/README.md`):

| | Before (89da61c) | Now |
|---|---|---|
| Load, render and save (`--snapshot`) | 25.9 s | 5.6 s |
| Render, 1024 x 1024 | 140.7 ms | 15.9 ms |
| GPU memory | +2,474 MB | +388 MB |
| App RAM | 2,352 MB | 877 MB |

## Quick start

Requirements: Windows, an NVIDIA GPU with a current driver, and (to build) Visual Studio C++ tools plus the CUDA toolkit. See `BUILD_WINDOWS.md`.

```powershell
.\build_windows.ps1                 # build
.\launch_voxelpilot_windows.bat     # run (opens on a synthetic volume)
```

Open a scan from **File > Open volume** (Ctrl+O). Raw files have no header, so set width, height, depth, data type and voxel spacing; names like `carp_256x256x512_uint16.raw` fill in the size and type automatically.

Or load one at startup:

```powershell
.\launch_voxelpilot_windows.bat --volume .\sample-data\carp\carp_256x256x512_uint16.raw --spacing 0.78125 0.390625 1
```

## Using the app

- **Viewport toolbar**: Orbit (O), Pan (P), Measure (M), Clip (C), Annotate (A); Front / Side / Top / Iso views (1–4) and Frame (F). Right-drag pans and middle-drag orbits with any tool. The axis gizmo turns the camera to look down an axis.
- **Scene** (left): the volume, discovered regions (R1, R2...), annotations, measurement, clip box, camera, light. Click a row to inspect it.
- **Inspector** (right): volume file, rendering quality and cinematic look, transfer function with histogram, clipping, workspace.
- **Slices** (under the viewport): axial, coronal and sagittal, kept to physical proportions.
- **Bottom dock**: AI Assist (prompts like `show high-density shell`, Discover regions, Guided review, Export bundle), Histogram, Metadata, Log.
- **Status bar**: size, GPU time, FPS, clip state, and the resolution being shown ("1/2 resolution" or "Full-detail region").

Press `?` in the app for all keyboard shortcuts.

### Large scans: full detail where it matters

1. Open the scan. If it does not fit, it loads at 1/2 or 1/4 resolution and the status bar says so.
2. Use the Clip tool (C) or **Inspector > Clipping** to box the part you care about.
3. Click **Load full detail** (also under the Volume menu). The box is re-read from the file at native resolution, or at least sharper than the overview, using the same transfer function. If the box is still too big, the app says how much memory it needs and asks for a smaller box.
4. **Back to whole scan** returns to the overview.

Measurements and spacing stay physical throughout.

### Region discovery

**Discover regions** (AI Assist, or the AI menu) runs ASD in the background and lists regions in the Scene tree with callouts in the viewport. Each region has a shape (rod, plate, blob, branching), a match score and a stability score. Use **Is this right?** in the Inspector to confirm, rename or reject; corrections are remembered in `voxelpilot_asd_memory.json` and reapplied to similar regions later. **Guided review** (`[` / `]`) walks the regions in order of importance.

Discovery and the 3D hover use the same idea of "what you see": hover reports the first surface that is actually visible under the cursor, not faint haze in front of it.

## Command line

| Flag | Meaning |
|---|---|
| `--volume PATH` | Raw volume to load at startup |
| `--width W --height H --depth D` or `--dim N` | Dimensions (inferred from the file name when possible) |
| `--type uint8\|uint16\|float32` | Voxel data type |
| `--spacing X Y Z` | Physical voxel spacing (e.g. mm) |
| `--gpu-budget-mb N` | Pretend only N MB of GPU memory is free (test laptop limits) |
| `--detail-box x0 y0 z0 x1 y1 z1` | Clip to a box (0–1 per axis); on a downsampled scan, load it at full detail |
| `--discover` | Start region discovery at launch (in the background) |
| `--demo`, `--demo-auto`, `--demo-duration S` | Guided demo mode |
| `--semantic-demo` | One-click discovery + callouts workflow |
| `--tf-center C --tf-width W --tf-opacity O` | Transfer function (overrides auto-enhance) |
| `--cinematic-lite` | Start with the cinematic look on |
| `--snapshot PATH.png` | Render one 1024² frame, save it, exit |
| `--review-bundle FOLDER` | Export snapshot, regions, label mask and report, exit |

## Sample data

| Dataset | Size | Notes |
|---|---|---|
| `sample-data/foot_256x256x256_uint8.raw` | 16 MB | Render regression baseline |
| `sample-data/bonsai_256x256x256_uint8.raw` | 16 MB | Render + ASD verification |
| `sample-data/vtk-headsq/` | 64 x 64 x 93 | Tiny CT check, spacing 3.2 x 3.2 x 1.5 |
| `sample-data/carp/` | 256 x 256 x 512 uint16, 64 MB | Fish CT: skeleton, swim bladder, soft tissue |
| `sample-data/backpack/` | 512 x 512 x 373 uint16, 187 MB | Large-scan benchmark |
| `sample-data/nibabel-anatomical/` | 33 x 41 x 25 | Small anatomical sample |

The carp and backpack `.raw` files are not in git; their READMEs say where to download them.

## Tests

```powershell
.\run_all_tests_windows.bat
```

Runs unit and structural tests, the foot render regression, the VTK headsq and Bonsai render checks, and the Bonsai ASD check. Each writes its snapshot or report to `build\`. `run_tests_windows.bat` runs just the fast unit tests.

## Repo layout

- `standalone_main.cpp`: app entry, startup, loading, GPU budget, background discovery
- `ui_*.cpp`: menu bar and dock layout, Scene, Inspector, viewport overlays, AI Assist, bottom panels, status bar
- `gl_viewport_and_render.cpp`: viewport, picking, tools, camera
- `renderer/`: CUDA ray marcher and volume textures
- `common/`: geometry, import, ASD (`semantic_discovery.h`), picking, camera motion and other testable headers
- `tests/`, `tools/`: unit tests and dataset verifiers
- `docs/`: design plans
- `release/voxelpilot-windows-nvidia/`: packaged build

## Limitations

- Raw volumes only (no DICOM or NIfTI reader yet).
- Windows + NVIDIA CUDA only.
- A scan must fit in system RAM once while loading; GPU memory is what the app manages.
- ASD is explainable heuristics, not a trained model; treat its names as hints to review.
