# VTK Head CT Sample for VoxelPilot

This folder contains an app-ready conversion of Kitware VTK's public `headsq` CT sample.

## Recommended Demo File

- File: `vtk_headsq_bone_64x64x93_uint8.raw`
- Dimensions: `64 x 64 x 93`
- Data type: `uint8`
- Size: `0.36 MB`
- Purpose: high-density/skull-emphasis demo for rendering, ASD-Lite, hover labels, and cinematic callouts

```powershell
.\launch_voxelpilot_windows.bat --volume .\sample-datatk-headsqtk_headsq_bone_64x64x93_uint8.raw --width 64 --height 64 --depth 93 --type uint8
```

## Additional Copies

- `vtk_headsq_64x64x93_uint8.raw`: compact full-range scaled copy.
- `vtk_headsq_64x64x93_uint16.raw`: full-range app-ready CT copy.
- `quarter.nhdr`: source NRRD header downloaded from Kitware ExternalData.
- `source-sha512.txt`: source header/slice hashes from the VTK repository.
- `dataset_metadata.json`: conversion metadata and app-ready checksums.

## Source

- Repository: <https://github.com/Kitware/VTK>
- Source path: `Examples/ImageProcessing/Cxx/Testing/Data/headsq`
- ExternalData store: `https://data.kitware.com/api/v1/file/hashsum/sha512/<hash>/download`

The source header describes a `64 x 64 x 93` little-endian signed-short raw slice stack with spacing `3.2 x 3.2 x 1.5`. The recommended `bone` file is a derived local display transform that emphasizes high-density structures for clearer laptop demos without increasing renderer load.
