# VoxelPilot First-Run Checklist

Use this on the NVIDIA laptop during the first real startup test.

## Before Launch

1. Copy the full `voxelpilot-windows-nvidia` folder to the laptop.
2. Make sure the laptop is using the NVIDIA GPU, not only integrated graphics.
3. Open Command Prompt.
4. Run `nvidia-smi`.
5. Confirm the NVIDIA GPU name and driver version appear.

## Launch Test

1. Double-click `Run_VoxelPilot.bat`.
2. Confirm the launcher starts the app without an immediate CUDA error.
3. Confirm the main window title shows `VoxelPilot | NVIDIA Volume Explorer`.
4. Confirm the app reaches the UI and does not close on its own.

## Basic UI Check

1. Confirm the main render view is visible.
2. Confirm the `Controls` window is visible.
3. Confirm the `Insights` window is visible.
4. Confirm the `Metadata` window is visible.
5. Confirm the `Status` window is visible.

## Renderer Check

1. If no volume is loaded, confirm the app still opens using the synthetic fallback volume.
2. Check that the viewport updates while rendering is not paused.
3. Confirm `GPU Render` and `GUI FPS` values appear in the UI.

## Interaction Check

1. Drag in the viewport and confirm the camera orbits.
2. Use the mouse wheel and confirm zoom changes.
3. Click `Front`, `Side`, `Top`, and `Isometric` and confirm the camera updates.
4. Move the `Axial Z`, `Coronal Y`, and `Sagittal X` sliders and confirm slice previews update.

## Feature Check

1. Click `Save Snapshot` and confirm a PNG file can be written.
2. Toggle measurement visibility and confirm the measurement section is usable.
3. Click `Set A From Slices` and `Set B From Slices` and confirm the distance updates.
4. Click `Save Preset` and confirm a `.vpilot` workspace file can be saved.
5. Click `Load Preset` and confirm the saved workspace can be reopened.

## If Something Fails

1. Capture the exact error text from the launcher window.
2. Note whether the failure happened before the UI opened or after the UI appeared.
3. Record whether `nvidia-smi` worked before launch.
4. Record the dataset used, if one was loaded.
