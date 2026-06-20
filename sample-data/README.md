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
