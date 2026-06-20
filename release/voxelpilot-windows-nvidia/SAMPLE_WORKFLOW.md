# VoxelPilot Sample Workflow

Use this as a short guided demo once the app starts successfully on the NVIDIA laptop.

## Goal

Load a volume, inspect it from multiple views, place a simple measurement, and save a snapshot.

## Workflow

1. Launch the app with `Run_VoxelPilot.bat`.
2. In `Controls`, click `Browse...` and choose a raw volume file.
3. Confirm `Width`, `Height`, `Depth`, and `Data Type`.
4. Click `Load Volume`.
5. Wait for the import status text to confirm the volume was loaded.

## View Adjustment

1. Click `Front`.
2. Click `Isometric`.
3. Drag in the main viewport to orbit around the volume.
4. Use the mouse wheel to zoom in slightly.
5. Adjust `Step Size` if you want a different render quality/performance balance.

## Slice Review

1. In `Insights`, move `Sagittal X`.
2. Move `Coronal Y`.
3. Move `Axial Z`.
4. Review the `Axial`, `Coronal`, and `Sagittal` slice cards.

## Simple Measurement

1. In `Controls`, enable the measurement tool if needed.
2. Move the slice sliders to the first point of interest.
3. Click `Set A From Slices`.
4. Move the slice sliders to the second point of interest.
5. Click `Set B From Slices`.
6. Read the `Distance` value in voxels and units.

## Snapshot

1. In `Controls`, click `Save Snapshot`.
2. Choose a PNG file location.
3. Confirm the snapshot status updates.

## Optional Workspace Save

1. Click `Save Preset`.
2. Save a `.vpilot` workspace file.
3. Reopen it with `Load Preset` to confirm the session restores correctly.
