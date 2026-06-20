# Build VoxelPilot on Windows

This project now includes native Windows build scripts that do not require GNU `make`.

## What must be installed

1. Windows 10 or Windows 11, 64-bit.
2. An NVIDIA GPU with a working driver.
3. NVIDIA CUDA Toolkit.
   The build script uses `CUDA_PATH` when available and otherwise scans the default CUDA install folder.
4. Visual Studio 2022 Community or Visual Studio 2022 Build Tools.
   Install the `Desktop development with C++` workload so `cl.exe` and `VsDevCmd.bat` are available.
5. Git, if you want to clone the repository instead of copying it.

## Fast compatibility check

Open PowerShell in the repo root and run:

```powershell
nvidia-smi
$env:CUDA_PATH
```

You should see your NVIDIA GPU and a CUDA install path.

## Build steps

1. Open PowerShell in the repo root.
2. Run:

```powershell
.\build_windows.ps1
```

3. Wait for the script to compile and link `volume_renderer_standalone.exe`.
4. Launch the app:

```powershell
.\launch_voxelpilot_windows.bat
```

5. Optional: verify raw-volume import from the command line with a known-good dataset:

```powershell
.\launch_voxelpilot_windows.bat --volume .\sample-data\foot_256x256x256_uint8.raw
```

## Optional architecture override

The PowerShell build script auto-detects the first GPU compute capability from `nvidia-smi` and turns it into an `sm_XX` architecture. If you need to override it, pass the architecture explicitly:

```powershell
.\build_windows.ps1 -CudaArch sm_86
```

Common examples:

- `sm_75` for Turing
- `sm_80` for Ampere datacenter GPUs
- `sm_86` for many RTX 30-series GPUs
- `sm_89` for many RTX 40-series GPUs

## Troubleshooting

- If the script cannot find CUDA, install the CUDA Toolkit or confirm `CUDA_PATH` exists.
- If the script cannot find Visual Studio tools, install Visual Studio 2022 with `Desktop development with C++`.
- If `nvidia-smi` fails, fix the NVIDIA driver before trying to run the app.
- The app can be built on a system without an active NVIDIA GPU, but runtime still requires NVIDIA CUDA support.
- If a raw import fails, confirm the file is a headerless 3D volume and that `Width x Height x Depth x bytes_per_voxel` matches the file size exactly.
- The import UI and startup loader support `uint8`, `uint16`, and `float32` raw volumes.

## Notes

- `build_windows.bat` remains available as the low-level compiler entry point and accepts the CUDA arch as its first positional argument.
- `make standalone` is still usable on systems that already provide GNU `make`, but Windows users no longer need it.
- The launcher auto-detects `CUDA_PATH` or the newest CUDA folder under the default NVIDIA toolkit location.
