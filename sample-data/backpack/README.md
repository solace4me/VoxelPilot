# Backpack CT (512 x 512 x 373, uint16)

CT scan of a backpack filled with items, from the Open SciVis Datasets
collection (https://klacansky.com/open-scivis-datasets/), downloaded
2026-10-26 from its Hugging Face mirror:
https://huggingface.co/datasets/SciVisAgentBench/SciVisAgentBench-tasks
(`sci_volume_data/backpack/data/`).

| Field | Value |
| --- | --- |
| File | `backpack_512x512x373_uint16.raw` (not committed - download it) |
| Dimensions | 512 x 512 x 373 (97.8 million voxels) |
| Data type | uint16, little endian, headerless |
| Voxel spacing | 0.9766 x 0.9766 x 1.25 |
| Size | 195,559,424 bytes (186.5 MB) |
| SHA-256 | `ead2a3166a4e1e1f1514db41117afd81e0df36fbe5d5108a893e8ff6227d68ce` |

`backpack.txt` is the mirror's metadata file, kept as downloaded.

## Why this dataset

It is the large-scan test: six times the carp, and a non-medical
inspection scan (fabric, straps, metal parts) that matches VoxelPilot's
research / inspection positioning.

## Measured on an RTX 3050 Laptop GPU (4 GB), 2026-10-26

| | Before the laptop memory work (`89da61c`) | Now |
| --- | --- | --- |
| Load + first render + save (`--snapshot`) | 25.9 s | 5.6 s |
| Render, 1024 x 1024 | 140.7 ms | 15.9 ms |
| GPU memory (nvidia-smi delta) | +2,474 MB | +388 MB |
| Peak RAM of the app | 2,352 MB | 877 MB |
| In-app viewport render | - | 5.7 ms |
| Region discovery (optimised tool, incl. file read) | - | 9.8 s, 16 regions |

## Load it

```powershell
.\launch_voxelpilot_windows.bat --volume .\sample-data\backpack\backpack_512x512x373_uint16.raw --width 512 --height 512 --depth 373 --type uint16 --spacing 0.9766 0.9766 1.25
```
