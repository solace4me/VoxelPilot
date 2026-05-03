# VoxelPilot Final Release Checklist

Use this as the final handoff checklist before the NVIDIA laptop demo.

## What To Copy To The NVIDIA Laptop

Copy the full `voxelpilot-windows-nvidia` folder, including:

- `volume_renderer_standalone.exe`
- `Run_VoxelPilot.bat`
- `launch_voxelpilot_windows.bat`
- `README.md`
- `RUNTIME_NOTES.txt`
- `FIRST_RUN_CHECKLIST.md`
- `SAMPLE_WORKFLOW.md`
- `PRESENTATION_SCRIPT.md`
- `PITCH_DECK_OUTLINE.md`
- `DEMO_PRESENTATION.md`

If you already have a demo dataset ready, also copy:

- the raw volume file you want to present
- any prepared `.vpilot` workspace file you want to reopen

## What To Test First

Before launch:

1. Open Command Prompt.
2. Run `nvidia-smi`.
3. Confirm the NVIDIA GPU and driver are listed.

Then test in this order:

1. Launch `Run_VoxelPilot.bat`.
2. Confirm the VoxelPilot splash screen appears.
3. Confirm the main window opens and stays open.
4. Confirm the demo banner and guided walkthrough are available.
5. Confirm the main render view updates.
6. Confirm Controls, Insights, Metadata, Help, About, and Status are visible.

## First Functional Checks

Run these before the live presentation:

1. Orbit in the viewport with mouse drag.
2. Zoom with the mouse wheel.
3. Click `Front` and `Isometric`.
4. Move the sagittal, coronal, and axial slice sliders.
5. Load the actual demo dataset.
6. Confirm metadata and histogram update after load.
7. Place a quick measurement with `Set A From Slices` and `Set B From Slices`.
8. Save one PNG snapshot.
9. Save or load a `.vpilot` workspace if you plan to use a prepared setup.

## What To Say During The Live Demo

Use this simple flow:

1. Opening:
   `VoxelPilot is a presentation-ready CUDA volume exploration workstation built for local NVIDIA hardware.`
2. Positioning:
   `This is not just a renderer. It combines 3D volume rendering, slice inspection, measurements, export, and guided presentation flow in one app.`
3. Main view:
   `Here is the real-time CUDA render view.`
4. Slice review:
   `These axial, coronal, and sagittal slices help validate what we see in 3D.`
5. Practical tools:
   `I can adjust transfer mapping, clipping, camera views, and measurement directly in the workstation.`
6. Capture:
   `I can export a PNG snapshot and save the workspace session for repeat demonstrations.`
7. Close:
   `VoxelPilot turns CUDA volume rendering into a polished exploration and presentation experience.`

## Best Short Selling Point

`VoxelPilot is a presentation-ready CUDA volume exploration workstation, not just a renderer.`

## Backup Plan If Something Goes Wrong

If the dataset does not load:

- use the synthetic fallback volume path
- show navigation, slices, measurements, and workflow first
- keep the explanation focused on the workstation experience

If startup fails:

- check `nvidia-smi` again
- confirm the NVIDIA driver is active
- confirm the app is using the discrete GPU
- keep `RUNTIME_NOTES.txt` available as a quick reference
