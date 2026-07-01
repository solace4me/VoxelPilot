# Sample Volume Data

This folder contains a known-good raw dataset for VoxelPilot:

- `foot_256x256x256_uint8.raw`

Import settings:

- Width: `256`
- Height: `256`
- Depth: `256`
- Data Type: `uint8`

You can load it in either of these ways:

```powershell
.\launch_voxelpilot_windows.bat --volume .\sample-data\foot_256x256x256_uint8.raw
```

Or inside the app:

1. Click `Browse...`
2. Choose `sample-data\foot_256x256x256_uint8.raw`
3. Confirm the detected settings
4. Click `Load Volume`

Source:

- <https://raw.githubusercontent.com/johanna-b/VisWeb/master/foot_256x256x256_uint8.raw>

## VTK Head CT Sample

A second public dataset is included for AI Assist, ASD-Lite, hover labels, and cinematic callout testing:

- `vtk-headsq/vtk_headsq_bone_64x64x93_uint8.raw` — recommended tiny high-density demo copy, `0.36 MB`
- `vtk-headsq/vtk_headsq_64x64x93_uint8.raw` — compact full-range scaled copy, `0.36 MB`
- `vtk-headsq/vtk_headsq_64x64x93_uint16.raw` — full-range app-ready CT copy, `0.73 MB`

If you are choosing one `vtk-headsq` file, use `vtk_headsq_bone_64x64x93_uint8.raw`; it is the visibility-tuned copy for demos.

Import settings for the recommended demo copy:

- Width: `64`
- Height: `64`
- Depth: `93`
- Data Type: `uint8`

Launch it directly:

```powershell
.\launch_voxelpilot_windows.bat --volume .\sample-datatk-headsqtk_headsq_bone_64x64x93_uint8.raw --width 64 --height 64 --depth 93 --type uint8
```

Render a quick verification snapshot:

```powershell
.olume_renderer_standalone.exe --volume .\sample-datatk-headsqtk_headsq_bone_64x64x93_uint8.raw --width 64 --height 64 --depth 93 --type uint8 --snapshot .uildtk_headsq_bone_snapshot.png
```

Source:

- Kitware VTK public sample data, `Examples/ImageProcessing/Cxx/Testing/Data/headsq`
- Header reports `64 x 64 x 93`, spacing `3.2 x 3.2 x 1.5`, signed short, raw encoding
- Downloaded via Kitware's content-addressed ExternalData store; source hashes are recorded in `vtk-headsq/source-sha512.txt`

The `bone` file is a local high-density display transform derived from the same public source stack. It keeps verification laptop-safe while making skull/bone-like regions easier to see and discover with ASD-Lite.

## Bonsai CT Sample

A larger public CT sample is included for testing laptop-scale rendering and ASD/callout behavior at the same size class as the foot dataset:

- `bonsai_256x256x256_uint8.raw` — `256 x 256 x 256`, `uint8`, `16 MB`

Launch it directly:

```powershell
.\launch_voxelpilot_windows.bat --volume .\sample-data\bonsai_256x256x256_uint8.raw
```

Run its render verification:

```powershell
.\run_bonsai_verification_windows.bat
```

Run its ASD-Lite discovery verification:

```powershell
.\run_bonsai_asd_verification_windows.bat
```

In the app, load Bonsai, open `AI Assist`, click `Discover Regions`, and keep `Cinematic Callouts` enabled. The current ASD pass detects a high-density shell-like structure, which is useful for demonstrating cautious AI labels without claiming exact botany or anatomy.

Source:

- Open Scientific Visualization Datasets / Klacansky Bonsai CT dataset
- <http://klacansky.com/open-scivis-datasets/bonsai/bonsai_256x256x256_uint8.raw>
