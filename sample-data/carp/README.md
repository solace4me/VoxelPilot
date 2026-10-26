# Carp CT (256 x 256 x 512, uint16)

CT scan of a carp fish, from the Open SciVis Datasets collection
(https://klacansky.com/open-scivis-datasets/), downloaded 2026-10-25 from its
Hugging Face mirror:
https://huggingface.co/datasets/SciVisAgentBench/SciVisAgentBench-tasks
(`sci_volume_data/carp/data/`).

| Field | Value |
| --- | --- |
| File | `carp_256x256x512_uint16.raw` |
| Dimensions | 256 x 256 x 512 |
| Data type | uint16, little endian, headerless |
| Voxel spacing | 0.78125 x 0.390625 x 1 |
| Size | 67,108,864 bytes (64 MB) |
| SHA-256 | `0744e565584393e8e2e561af60d128d68426832faade48d70c2a49df23907f38` |

`carp.txt` is the mirror's metadata file, kept as downloaded.

## Why this dataset

It covers what the other samples in `sample-data/` do not: 16-bit data, a
non-cubic volume (twice as long as it is wide), real non-uniform voxel
spacing, and several clearly separated densities (skeleton, swim bladder,
soft tissue) for the transfer function and region discovery to work on.

## Load it

```powershell
.\launch_voxelpilot_windows.bat --volume .\sample-data\carp\carp_256x256x512_uint16.raw --width 256 --height 256 --depth 512 --type uint16
```

Then set Voxel spacing in Inspector > Volume file to 0.78125 / 0.390625 / 1
for true-to-scale measurements.
