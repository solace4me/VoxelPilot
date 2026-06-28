# NiBabel Anatomical NIfTI Sample

Source: `anatomical.nii` downloaded from the NiBabel public test data repository.

Converted for VoxelPilot:

- File: `nibabel_anatomical_33x41x25_uint8.raw`
- Dimensions: `33 x 41 x 25`
- Type: `uint8`
- Source datatype code: `4` (`16` bits)
- Source intensity range before normalization: `-610` to `30393`
- Timepoints in source: `1`; exported first volume only

Use in VoxelPilot:

```powershell
.\launch_voxelpilot_windows.bat --volume .\sample-data\nibabel-anatomical\nibabel_anatomical_33x41x25_uint8.raw --width 33 --height 41 --depth 25 --type uint8
```
