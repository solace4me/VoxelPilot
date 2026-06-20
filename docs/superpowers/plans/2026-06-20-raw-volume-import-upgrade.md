# Raw Volume Import Upgrade Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expand VoxelPilot’s importer to accept non-cubic raw volumes with explicit `width`, `height`, `depth`, and `data type`, while keeping the existing renderer behavior intact and making real medical/scivis datasets easier to load.

**Architecture:** Introduce a focused raw-volume import module responsible for file-size validation, filename hint parsing, and conversion of `uint8`, `uint16`, and `float32` input into the renderer’s internal `float` volume buffer. Then update the standalone UI and renderer API to use explicit dimensions instead of a single cubic `dim`, without changing rendering, slices, histogram, measurement, presets, or snapshot features.

**Tech Stack:** C++/CUDA, ImGui, GLFW, NVCC, MSVC `cl`, Windows batch/PowerShell build scripts

---

### Task 1: Add importer tests for raw-volume parsing and conversion

**Files:**
- Create: `tests/volume_import_tests.cpp`

- [ ] **Step 1: Write the failing test file**

```cpp
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum VolumeDataType {
    VOLUME_DATA_UINT8 = 0,
    VOLUME_DATA_UINT16 = 1,
    VOLUME_DATA_FLOAT32 = 2
} VolumeDataType;

typedef struct VolumeImportSpec {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    VolumeDataType data_type;
} VolumeImportSpec;

extern "C" int volume_import_expected_bytes(
    const VolumeImportSpec *spec,
    size_t *out_expected_bytes);
extern "C" int volume_import_infer_from_filename(
    const char *path,
    VolumeImportSpec *out_spec);
extern "C" int volume_import_load(
    const char *path,
    const VolumeImportSpec *spec,
    float **out_data,
    size_t *out_voxel_count,
    char *error,
    size_t error_size);
extern "C" void volume_import_free(float *data);
```

- [ ] **Step 2: Add concrete tests for expected bytes, filename inference, mismatch handling, and integer conversion**

```cpp
static int test_expected_bytes_non_cubic_uint16(void);
static int test_infer_spec_from_filename(void);
static int test_load_uint8_normalizes_values(void);
static int test_load_uint16_normalizes_values(void);
static int test_rejects_mismatched_file_size(void);
```

- [ ] **Step 3: Run test build to verify it fails before implementation**

Run:

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=amd64 >nul && cl /nologo /EHsc /std:c++17 tests\volume_import_tests.cpp /Fe:build\volume_import_tests.exe"
```

Expected: build fails because the importer functions are declared in the tests but not implemented yet.

### Task 2: Implement a focused raw-volume import module

**Files:**
- Create: `common/volume_import.h`
- Create: `common/volume_import.cpp`
- Test: `tests/volume_import_tests.cpp`

- [ ] **Step 1: Add the importer header with the public API**

```cpp
typedef enum VolumeDataType {
    VOLUME_DATA_UINT8 = 0,
    VOLUME_DATA_UINT16 = 1,
    VOLUME_DATA_FLOAT32 = 2
} VolumeDataType;

typedef struct VolumeImportSpec {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    VolumeDataType data_type;
} VolumeImportSpec;

int volume_import_expected_bytes(
    const VolumeImportSpec *spec,
    size_t *out_expected_bytes);
int volume_import_infer_from_filename(
    const char *path,
    VolumeImportSpec *out_spec);
int volume_import_load(
    const char *path,
    const VolumeImportSpec *spec,
    float **out_data,
    size_t *out_voxel_count,
    char *error,
    size_t error_size);
void volume_import_free(float *data);
const char *volume_import_data_type_label(VolumeDataType data_type);
```

- [ ] **Step 2: Implement exact byte validation and filename hint parsing**

```cpp
expected = (size_t)spec->width * spec->height * spec->depth * bytes_per_voxel;
if (actual_size != expected) {
    snprintf(error, error_size,
        "File size mismatch: got %zu bytes, expected %zu for %ux%ux%u %s",
        actual_size, expected,
        spec->width, spec->height, spec->depth,
        volume_import_data_type_label(spec->data_type));
    return -1;
}
```

- [ ] **Step 3: Implement conversion to renderer-friendly float data**

```cpp
out[i] = (float)src_u8[i] / 255.0f;
out[i] = (float)src_u16[i] / 65535.0f;
out[i] = src_f32[i];
```

- [ ] **Step 4: Run the importer tests and verify they pass**

Run:

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=amd64 >nul && cl /nologo /EHsc /std:c++17 tests\volume_import_tests.cpp common\volume_import.cpp /Fe:build\volume_import_tests.exe && build\volume_import_tests.exe"
```

Expected: all importer tests pass.

### Task 3: Update the renderer API to accept explicit dimensions

**Files:**
- Modify: `common/renderer_api.h`
- Modify: `renderer/renderer.cu`
- Test: `tests/volume_import_tests.cpp`

- [ ] **Step 1: Change the renderer API signatures**

```cpp
void renderer_state_init(
    RendererState *st,
    const char *volume_path,
    int width,
    int height,
    int depth,
    int brick_dim);

int reload_volume(
    RendererState *st,
    const float *new_data,
    int width,
    int height,
    int depth);
```

- [ ] **Step 2: Update renderer initialization and reload logic to use `W/H/D` directly**

```cpp
st->W = width;
st->H = height;
st->D = depth;
st->vol_bytes = (size_t)width * height * depth * sizeof(float);
```

- [ ] **Step 3: Rebuild the Windows app**

Run:

```powershell
.\build_windows.ps1
```

Expected: `volume_renderer_standalone.exe` is rebuilt successfully.

### Task 4: Replace the cubic importer UI with width/height/depth/data-type controls

**Files:**
- Modify: `standalone_main.cu`
- Modify: `release/voxelpilot-windows-nvidia/SAMPLE_WORKFLOW.md`
- Modify: `README.md`

- [ ] **Step 1: Replace `upload_dim` state with width/height/depth/data-type state**

```cpp
int upload_width;
int upload_height;
int upload_depth;
int upload_data_type;
char upload_hint_status[256];
```

- [ ] **Step 2: Replace the UI combo with three integer inputs plus a data-type selector**

```cpp
ImGui::InputInt("Width", &app->upload_width);
ImGui::InputInt("Height", &app->upload_height);
ImGui::InputInt("Depth", &app->upload_depth);
ImGui::Combo("Data Type", &app->upload_data_type, data_type_labels, 3);
```

- [ ] **Step 3: Add filename-based hints when a file is selected**

```cpp
if (volume_import_infer_from_filename(app->upload_path, &hint_spec) == 0) {
    app->upload_width = (int)hint_spec.width;
    app->upload_height = (int)hint_spec.height;
    app->upload_depth = (int)hint_spec.depth;
    app->upload_data_type = (int)hint_spec.data_type;
}
```

- [ ] **Step 4: Load through the importer module instead of direct `fread`**

```cpp
VolumeImportSpec spec = {
    (uint32_t)app->upload_width,
    (uint32_t)app->upload_height,
    (uint32_t)app->upload_depth,
    (VolumeDataType)app->upload_data_type
};
```

- [ ] **Step 5: Rebuild and verify the app still launches**

Run:

```powershell
.\build_windows.ps1
.\launch_voxelpilot_windows.bat
```

Expected: the app opens and the importer UI shows width/height/depth/data-type controls.

### Task 5: Add a repeatable Windows test runner and refresh docs

**Files:**
- Create: `run_tests_windows.bat`
- Modify: `BUILD_WINDOWS.md`
- Modify: `README.md`

- [ ] **Step 1: Add a Windows test runner for the importer unit tests**

```bat
call "%VS_DEV_CMD%" -arch=amd64 >nul
cl /nologo /EHsc /std:c++17 tests\volume_import_tests.cpp common\volume_import.cpp /Fe:build\volume_import_tests.exe
build\volume_import_tests.exe
```

- [ ] **Step 2: Document the new supported raw formats**

```markdown
- Headerless raw volumes with explicit width, height, depth
- Supported voxel types: uint8, uint16, float32
- Filename hints like `vis_male_128x256x256_uint8.raw` auto-fill importer fields
```

- [ ] **Step 3: Run the unit tests through the new script**

Run:

```powershell
.\run_tests_windows.bat
```

Expected: the importer test executable builds and exits successfully.

### Task 6: Download a public dataset and verify a real import

**Files:**
- Download into: `sample-data\`
- Modify: `README.md`

- [ ] **Step 1: Download a public compatible raw medical/scivis dataset**

Candidate:

```text
vis_male_128x256x256_uint8.raw
```

- [ ] **Step 2: Launch the app and import it with the upgraded UI**

Use:

```text
Width: 128
Height: 256
Depth: 256
Data Type: uint8
```

- [ ] **Step 3: Verify the import updates the status and renders successfully**

Expected:

```text
Loaded: 128x256x256 uint8
```

- [ ] **Step 4: Save a PNG snapshot as proof of a working import**

Use the in-app `Save Snapshot` button and confirm the export succeeds.

### Spec Coverage

- Arbitrary dimensions: covered by Tasks 2, 3, and 4.
- Better usability and friendliness: covered by Task 4 filename hints and clearer validation.
- Compatible public dataset download and real test: covered by Task 6.

### Placeholder Scan

- No `TBD` or `TODO` markers remain.
- Every changed area names an exact file path.

### Type Consistency

- `VolumeImportSpec` and `VolumeDataType` are shared across tests, importer logic, and UI integration.
- Renderer APIs use explicit `width`, `height`, and `depth` consistently after Task 3.
