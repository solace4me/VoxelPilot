# VoxelPilot Demo Video Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce a short real-app MP4 demo of VoxelPilot loading the foot dataset, rotating the volume, enabling cinematic presentation, applying Auto Enhance/AI assistance, and ship a short LinkedIn post.

**Architecture:** Add an opt-in GUI demo driver to the existing standalone app, then use a Python capture script to record the actual VoxelPilot window and encode frames with the locally available `imageio_ffmpeg` binary. Keep the app behavior unchanged unless `--demo-auto` is passed, and keep final copy in a separate Markdown artifact.

**Tech Stack:** CUDA/OpenGL/GLFW/ImGui app in `standalone_main.cu`, Python 3.12, Pillow `ImageGrab`, `imageio_ffmpeg`, Windows `user32` via `ctypes`.

---

## File Structure

- Modify `standalone_main.cu`: add opt-in `--demo-auto` and `--demo-duration` flags, demo timeline state, an auto-demo update helper, and a small status line in the existing demo banner.
- Create `tools/capture_voxelpilot_demo.py`: launch VoxelPilot with the foot dataset, capture the real app window to PNG frames, encode `build/voxelpilot_foot_demo.mp4`, and provide `--check-tools`.
- Create `build/voxelpilot_linkedin_post.md`: final LinkedIn post text.
- Output `build/voxelpilot_foot_demo.mp4`: generated demo video.

The working tree already contains uncommitted edits in `standalone_main.cu` and related renderer files. Do not commit mixed implementation changes from dirty files unless the user explicitly asks and the staged diff is reviewed line by line.

## Task 1: Add Demo CLI State

**Files:**
- Modify: `standalone_main.cu`

- [ ] **Step 1: Verify current CLI rejects demo automation**

Run:

```powershell
.\volume_renderer_standalone.exe --demo-auto --snapshot .\build\demo_auto_red.png
```

Expected: the executable exits non-zero and prints `Unknown argument: --demo-auto`.

- [ ] **Step 2: Add state fields to `StandaloneApp`**

In `standalone_main.cu`, inside `StandaloneApp`, place these fields after `demo_mode_enabled`:

```cpp
    int           demo_auto_enabled;
    double        demo_auto_start_time;
    double        demo_auto_duration_sec;
    int           demo_auto_enhance_applied;
    char          demo_auto_status[192];
```

- [ ] **Step 3: Add local CLI variables**

In `main`, near the other CLI variables, add:

```cpp
    int           cli_demo_auto = 0;
    double        cli_demo_duration_sec = 24.0;
```

- [ ] **Step 4: Parse `--demo-auto` and `--demo-duration`**

In the argument loop, after `--semantic-demo`, add:

```cpp
        } else if (strcmp(argv[i], "--demo-auto") == 0) {
            cli_demo_auto = 1;
        } else if (strcmp(argv[i], "--demo-duration") == 0 &&
                   i+1 < argc) {
            cli_demo_duration_sec = atof(argv[++i]);
            if (cli_demo_duration_sec < 8.0) cli_demo_duration_sec = 8.0;
            if (cli_demo_duration_sec > 60.0) cli_demo_duration_sec = 60.0;
```

In the missing-value guard, include:

```cpp
                    strcmp(argv[i], "--demo-duration") == 0 ||
```

Update the usage string to include:

```text
[--demo-auto --demo-duration seconds]
```

- [ ] **Step 5: Initialize demo state**

After `app.demo_mode_enabled = 1;`, add:

```cpp
    app.demo_auto_enabled = cli_demo_auto;
    app.demo_auto_start_time = 0.0;
    app.demo_auto_duration_sec = cli_demo_duration_sec;
    app.demo_auto_enhance_applied = 0;
    strcpy(app.demo_auto_status,
           cli_demo_auto
               ? "Auto demo armed: launching VoxelPilot."
               : "Manual demo mode ready.");
```

- [ ] **Step 6: Build smoke**

Run:

```powershell
.\build_windows.ps1
```

Expected: build succeeds and `volume_renderer_standalone.exe` is updated.

## Task 2: Drive The Real App Demo Timeline

**Files:**
- Modify: `standalone_main.cu`

- [ ] **Step 1: Add the auto-demo update helper**

Add this function near `update_startup_splash`:

```cpp
static void update_auto_demo_sequence(StandaloneApp *app, double now)
{
    double t;
    double active_t;
    float phase;

    if (!app || !app->demo_auto_enabled) {
        return;
    }

    if (app->demo_auto_start_time <= 0.0) {
        app->demo_auto_start_time = now;
    }

    t = now - app->demo_auto_start_time;

    if (app->splash_visible) {
        snprintf(app->demo_auto_status,
                 sizeof(app->demo_auto_status),
                 "Launching VoxelPilot and preparing the foot volume.");
        return;
    }

    active_t = t - 3.8;
    if (active_t < 0.0) active_t = 0.0;
    if (active_t > app->demo_auto_duration_sec) {
        active_t = app->demo_auto_duration_sec;
    }

    app->walkthrough_visible = 0;
    app->paused = 0;
    app->orbit_radius = 1.62f;
    app->orbit_target[0] = 0.5f;
    app->orbit_target[1] = 0.5f;
    app->orbit_target[2] = 0.5f;

    if (active_t < 5.0) {
        phase = (float)(active_t / 5.0);
        app->cinematic_enabled = 0;
        app->tf_palette = 0;
        app->orbit_yaw = -0.55f + phase * 0.85f;
        app->orbit_pitch = 0.18f + 0.16f * sinf(phase * 3.14159265f);
        snprintf(app->demo_auto_status,
                 sizeof(app->demo_auto_status),
                 "Loaded foot_256x256x256_uint8.raw | rotating across X/Y.");
    } else if (active_t < 12.0) {
        phase = (float)((active_t - 5.0) / 7.0);
        app->cinematic_enabled = 0;
        app->orbit_yaw = 0.30f + phase * 1.45f;
        app->orbit_pitch = 0.24f + 0.28f * sinf(phase * 3.14159265f);
        snprintf(app->demo_auto_status,
                 sizeof(app->demo_auto_status),
                 "Interactive CUDA orbit: X/Y rotation stays real-time.");
    } else if (active_t < 17.0) {
        phase = (float)((active_t - 12.0) / 5.0);
        app->cinematic_enabled = 1;
        app->cinematic_rim_strength = 0.45f + 0.10f * phase;
        app->cinematic_glow_strength = 0.32f + 0.08f * phase;
        app->cinematic_contrast = 1.16f + 0.08f * phase;
        app->orbit_yaw = 1.75f + phase * 0.65f;
        app->orbit_pitch = 0.32f + 0.16f * sinf(phase * 3.14159265f);
        clamp_cinematic_settings(app);
        snprintf(app->demo_auto_status,
                 sizeof(app->demo_auto_status),
                 "Cinematic Mode enabled: rim, glow, and contrast improve depth.");
    } else {
        phase = (float)((active_t - 17.0) / fmax(1.0, app->demo_auto_duration_sec - 17.0));
        if (!app->demo_auto_enhance_applied) {
            apply_auto_enhance_transfer(app, "Auto demo AI Assist");
            app->tf_palette = 1;
            app->demo_auto_enhance_applied = 1;
        }
        app->cinematic_enabled = 1;
        app->cinematic_rim_strength = fmaxf(app->cinematic_rim_strength, 0.55f);
        app->cinematic_glow_strength = fmaxf(app->cinematic_glow_strength, 0.40f);
        app->cinematic_contrast = fmaxf(app->cinematic_contrast, 1.24f);
        app->orbit_yaw = 2.40f + phase * 0.45f;
        app->orbit_pitch = 0.34f;
        clamp_cinematic_settings(app);
        snprintf(app->demo_auto_status,
                 sizeof(app->demo_auto_status),
                 "AI Assist Auto Enhance applied: transfer and palette tuned for clarity.");
    }

    apply_orbit_camera(app);

    if (t >= app->demo_auto_duration_sec + 5.0) {
        glfwSetWindowShouldClose(app->window, GLFW_TRUE);
    }
}
```

- [ ] **Step 2: Call the helper in the main loop**

In the main loop, immediately after `update_startup_splash(&app);`, add:

```cpp
        update_auto_demo_sequence(&app, now);
```

- [ ] **Step 3: Show auto-demo status in the banner**

In `draw_demo_banner`, after the existing `ImGui::TextWrapped(...)` call, add:

```cpp
    if (app->demo_auto_enabled) {
        ImGui::TextColored(
            ImVec4(0.40f, 0.88f, 0.78f, 1.0f),
            "Auto Demo");
        ImGui::SameLine();
        ImGui::TextWrapped("%s", app->demo_auto_status);
    }
```

- [ ] **Step 4: Build smoke**

Run:

```powershell
.\build_windows.ps1
```

Expected: build succeeds.

- [ ] **Step 5: Launch smoke**

Run:

```powershell
.\volume_renderer_standalone.exe --volume .\sample-data\foot_256x256x256_uint8.raw --width 256 --height 256 --depth 256 --type uint8 --demo-auto --demo-duration 8
```

Expected: the app opens, shows the splash, transitions into the workspace, updates the demo banner, rotates the volume, and exits after about 13 seconds.

## Task 3: Add Real Window Capture And MP4 Encoding

**Files:**
- Create: `tools/capture_voxelpilot_demo.py`

- [ ] **Step 1: Add the capture script**

Create `tools/capture_voxelpilot_demo.py` with:

```python
import argparse
import ctypes
import os
import pathlib
import subprocess
import sys
import time
from ctypes import wintypes

from PIL import ImageGrab
import imageio_ffmpeg


USER32 = ctypes.windll.user32


def _window_title(hwnd: int) -> str:
    length = USER32.GetWindowTextLengthW(hwnd)
    if length <= 0:
        return ""
    buf = ctypes.create_unicode_buffer(length + 1)
    USER32.GetWindowTextW(hwnd, buf, length + 1)
    return buf.value


def _window_rect(hwnd: int) -> tuple[int, int, int, int]:
    rect = wintypes.RECT()
    if not USER32.GetWindowRect(hwnd, ctypes.byref(rect)):
        raise RuntimeError("GetWindowRect failed")
    return rect.left, rect.top, rect.right, rect.bottom


def find_voxelpilot_window(timeout_sec: float) -> int:
    deadline = time.time() + timeout_sec
    found: list[int] = []

    enum_proc_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    def visit(hwnd, _lparam):
        if USER32.IsWindowVisible(hwnd):
            title = _window_title(hwnd)
            if "VoxelPilot" in title:
                found.append(hwnd)
                return False
        return True

    enum_proc = enum_proc_type(visit)
    while time.time() < deadline:
        found.clear()
        USER32.EnumWindows(enum_proc, 0)
        if found:
            return found[0]
        time.sleep(0.1)
    raise RuntimeError("Timed out waiting for the VoxelPilot window")


def encode_frames(frame_dir: pathlib.Path, output_mp4: pathlib.Path, fps: int) -> None:
    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    pattern = str(frame_dir / "frame_%05d.png")
    cmd = [
        ffmpeg,
        "-y",
        "-framerate",
        str(fps),
        "-i",
        pattern,
        "-vf",
        "scale=trunc(iw/2)*2:trunc(ih/2)*2",
        "-c:v",
        "libx264",
        "-pix_fmt",
        "yuv420p",
        "-movflags",
        "+faststart",
        str(output_mp4),
    ]
    subprocess.run(cmd, check=True)


def check_tools() -> int:
    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    print(f"Pillow ImageGrab: OK")
    print(f"imageio_ffmpeg: {ffmpeg}")
    return 0


def capture_demo(args: argparse.Namespace) -> int:
    root = pathlib.Path(__file__).resolve().parents[1]
    exe = root / "volume_renderer_standalone.exe"
    volume = root / "sample-data" / "foot_256x256x256_uint8.raw"
    frame_dir = root / "build" / "voxelpilot_demo_frames"
    output_mp4 = root / "build" / "voxelpilot_foot_demo.mp4"

    if not exe.exists():
        raise FileNotFoundError(f"Missing executable: {exe}")
    if not volume.exists():
        raise FileNotFoundError(f"Missing volume: {volume}")

    frame_dir.mkdir(parents=True, exist_ok=True)
    output_mp4.parent.mkdir(parents=True, exist_ok=True)

    app_cmd = [
        str(exe),
        "--volume",
        str(volume),
        "--width",
        "256",
        "--height",
        "256",
        "--depth",
        "256",
        "--type",
        "uint8",
        "--demo-auto",
        "--demo-duration",
        str(args.duration),
    ]

    proc = subprocess.Popen(app_cmd, cwd=root)
    try:
        hwnd = find_voxelpilot_window(args.window_timeout)
        USER32.SetForegroundWindow(hwnd)
        time.sleep(args.pre_roll)

        total_frames = int(args.duration * args.fps)
        for index in range(total_frames):
            start = time.perf_counter()
            bbox = _window_rect(hwnd)
            image = ImageGrab.grab(bbox=bbox, all_screens=True)
            image.save(frame_dir / f"frame_{index:05d}.png")
            elapsed = time.perf_counter() - start
            delay = max(0.0, (1.0 / args.fps) - elapsed)
            time.sleep(delay)

        encode_frames(frame_dir, output_mp4, args.fps)
        print(f"MP4 saved: {output_mp4}")
        return 0
    finally:
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--duration", type=float, default=24.0)
    parser.add_argument("--fps", type=int, default=20)
    parser.add_argument("--pre-roll", type=float, default=0.5)
    parser.add_argument("--window-timeout", type=float, default=12.0)
    parser.add_argument("--check-tools", action="store_true")
    args = parser.parse_args(argv)

    if args.check_tools:
        return check_tools()
    return capture_demo(args)


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 2: Verify encoder tooling**

Run:

```powershell
python .\tools\capture_voxelpilot_demo.py --check-tools
```

Expected: output includes `Pillow ImageGrab: OK` and an `imageio_ffmpeg` executable path.

- [ ] **Step 3: Capture the MP4**

Run:

```powershell
python .\tools\capture_voxelpilot_demo.py --duration 24 --fps 20
```

Expected: `build\voxelpilot_foot_demo.mp4` exists and is larger than 1 MB.

## Task 4: Create LinkedIn Post Artifact

**Files:**
- Create: `build/voxelpilot_linkedin_post.md`

- [ ] **Step 1: Write the post**

Create `build/voxelpilot_linkedin_post.md` with:

```markdown
One of the projects I am most proud of from the first half of this year is VoxelPilot, a CUDA-powered 3D volume exploration app I built after being inspired by two winter-quarter classes: GPU Computing and Data Visualization.

VoxelPilot started as a way to bring those ideas together in a practical tool: real-time volume rendering, interactive 3D exploration, slice review, cinematic viewing, and AI-assisted enhancement in one local desktop app for NVIDIA hardware.

What makes it exciting to me is that it is not just a renderer. The goal is to make complex volumetric data easier to inspect, present, and understand. Features like Cinematic Mode and Auto Enhance help turn raw 3D data into something more readable, while the AI Assist direction opens the door for smarter workflows around segmentation, interpretation, and guided exploration.

This project reminded me how powerful the intersection of GPU computing, visualization, and AI can be. I am looking forward to improving VoxelPilot even more, especially in this era where AI can help us accomplish more in scientific, medical, engineering, and visual data workflows.

#GPUComputing #DataVisualization #CUDA #AI #ComputerGraphics #VolumeRendering
```

- [ ] **Step 2: Check the quarter wording**

Run:

```powershell
Select-String -Path .\build\voxelpilot_linkedin_post.md -Pattern "spring quarter"
```

Expected: no matches.

## Task 5: Final Verification

**Files:**
- Verify: `build/voxelpilot_foot_demo.mp4`
- Verify: `build/voxelpilot_linkedin_post.md`

- [ ] **Step 1: Inspect output files**

Run:

```powershell
Get-Item .\build\voxelpilot_foot_demo.mp4, .\build\voxelpilot_linkedin_post.md | Select-Object Name,Length,LastWriteTime
```

Expected: both files exist; the MP4 size is above 1 MB and the Markdown file is non-empty.

- [ ] **Step 2: Verify MP4 metadata with bundled ffmpeg**

Run:

```powershell
python -c "import imageio_ffmpeg, subprocess; ff=imageio_ffmpeg.get_ffmpeg_exe(); subprocess.run([ff,'-i','build/voxelpilot_foot_demo.mp4'], stderr=subprocess.STDOUT, stdout=subprocess.PIPE, text=True, check=False)"
```

Expected: output includes `Video:` and `h264`.

- [ ] **Step 3: Run a renderer snapshot sanity check**

Run:

```powershell
.\volume_renderer_standalone.exe --volume .\sample-data\foot_256x256x256_uint8.raw --width 256 --height 256 --depth 256 --type uint8 --snapshot .\build\foot_demo_sanity.png --cinematic-lite
```

Expected: command exits 0 and prints `Snapshot saved`.

- [ ] **Step 4: Report deliverables**

Final response must include:

- `build\voxelpilot_foot_demo.mp4`
- `build\voxelpilot_linkedin_post.md`
- the verification commands that passed;
- any capture limitations if GUI capture fails.

## Self-Review

- Spec coverage: MP4, foot dataset, X/Y rotation, Cinematic Mode, Auto Enhance/AI Assist, and LinkedIn post are all covered by Tasks 1-5.
- Placeholder scan: no incomplete markers or deferred requirements are present.
- Type consistency: `demo_auto_*` field names are consistent across struct fields, initialization, banner drawing, and timeline update.
