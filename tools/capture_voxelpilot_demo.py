import argparse
import ctypes
import pathlib
import subprocess
import time
from ctypes import wintypes

from PIL import ImageGrab
import imageio_ffmpeg


USER32 = ctypes.windll.user32


def is_voxelpilot_app_title(title: str) -> bool:
    return title.startswith("VoxelPilot v2") or "Cinematic AI Preview" in title


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
            if is_voxelpilot_app_title(title):
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


def clear_owned_frames(frame_dir: pathlib.Path) -> None:
    if frame_dir.name != "voxelpilot_demo_frames":
        raise RuntimeError(f"Refusing to clear unexpected frame directory: {frame_dir}")
    frame_dir.mkdir(parents=True, exist_ok=True)
    for frame in frame_dir.glob("frame_*.png"):
        frame.unlink()


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
    print("Pillow ImageGrab: OK")
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

    clear_owned_frames(frame_dir)
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
        str(args.duration + args.app_extra_duration),
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
    parser.add_argument("--app-extra-duration", type=float, default=12.0)
    parser.add_argument("--check-tools", action="store_true")
    args = parser.parse_args(argv)

    if args.check_tools:
        return check_tools()
    return capture_demo(args)


if __name__ == "__main__":
    raise SystemExit(main())
