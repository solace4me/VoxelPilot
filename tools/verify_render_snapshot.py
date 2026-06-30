#!/usr/bin/env python3
"""Render and verify VoxelPilot's foot snapshot baseline.

This is intentionally dependency-free so it can run on the same NVIDIA laptop
used for demos. It guards against the renderer silently producing a black,
empty, tiny, or hung snapshot before we add cinematic/AI rendering features.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import struct
import subprocess
import sys
import time
import zlib

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
VISIBLE_LUMA_THRESHOLD = 6


def paeth_predictor(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def decode_png_rgba(path: pathlib.Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError(f"{path} is not a PNG file")

    offset = len(PNG_SIGNATURE)
    width = height = None
    color_type = None
    bit_depth = None
    idat_parts: list[bytes] = []

    while offset + 8 <= len(data):
        length = struct.unpack(">I", data[offset : offset + 4])[0]
        chunk_type = data[offset + 4 : offset + 8]
        chunk_data = data[offset + 8 : offset + 8 + length]
        offset += 12 + length

        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type, compression, filter_method, interlace = struct.unpack(
                ">IIBBBBB", chunk_data
            )
            if bit_depth != 8:
                raise ValueError(f"unsupported PNG bit depth: {bit_depth}")
            if compression != 0 or filter_method != 0 or interlace != 0:
                raise ValueError("unsupported PNG compression/filter/interlace settings")
        elif chunk_type == b"IDAT":
            idat_parts.append(chunk_data)
        elif chunk_type == b"IEND":
            break

    if width is None or height is None or color_type is None:
        raise ValueError("PNG is missing IHDR")

    channels_by_color = {0: 1, 2: 3, 6: 4}
    if color_type not in channels_by_color:
        raise ValueError(f"unsupported PNG color type: {color_type}")

    channels = channels_by_color[color_type]
    row_bytes = width * channels
    raw = zlib.decompress(b"".join(idat_parts))
    expected = height * (1 + row_bytes)
    if len(raw) != expected:
        raise ValueError(f"unexpected PNG payload size: got {len(raw)}, expected {expected}")

    recon = bytearray(height * row_bytes)
    src = 0
    for y in range(height):
        filter_type = raw[src]
        src += 1
        row = bytearray(raw[src : src + row_bytes])
        src += row_bytes
        prior = recon[(y - 1) * row_bytes : y * row_bytes] if y > 0 else bytearray(row_bytes)

        for x in range(row_bytes):
            left = row[x - channels] if x >= channels else 0
            up = prior[x]
            upper_left = prior[x - channels] if x >= channels else 0
            if filter_type == 0:
                value = row[x]
            elif filter_type == 1:
                value = (row[x] + left) & 0xFF
            elif filter_type == 2:
                value = (row[x] + up) & 0xFF
            elif filter_type == 3:
                value = (row[x] + ((left + up) // 2)) & 0xFF
            elif filter_type == 4:
                value = (row[x] + paeth_predictor(left, up, upper_left)) & 0xFF
            else:
                raise ValueError(f"unsupported PNG row filter: {filter_type}")
            row[x] = value

        recon[y * row_bytes : (y + 1) * row_bytes] = row

    if channels == 4:
        return width, height, bytes(recon)

    rgba = bytearray(width * height * 4)
    for index in range(width * height):
        if channels == 1:
            value = recon[index]
            rgba[index * 4 : index * 4 + 4] = bytes((value, value, value, 255))
        else:
            src_index = index * 3
            rgba[index * 4 : index * 4 + 4] = bytes(
                (recon[src_index], recon[src_index + 1], recon[src_index + 2], 255)
            )
    return width, height, bytes(rgba)


def compute_pixel_metrics(width: int, height: int, rgba_pixels: bytes) -> dict[str, object]:
    if len(rgba_pixels) != width * height * 4:
        raise ValueError("RGBA buffer size does not match dimensions")

    total_luma = 0
    max_luma = 0
    dark_pixels = 0
    visible_pixels = 0
    min_x = width
    min_y = height
    max_x = -1
    max_y = -1

    for pixel_index in range(width * height):
        offset = pixel_index * 4
        r = rgba_pixels[offset]
        g = rgba_pixels[offset + 1]
        b = rgba_pixels[offset + 2]
        luma = int(round((r + g + b) / 3.0))
        total_luma += luma
        max_luma = max(max_luma, luma)
        if luma < VISIBLE_LUMA_THRESHOLD:
            dark_pixels += 1
        else:
            visible_pixels += 1
            y, x = divmod(pixel_index, width)
            min_x = min(min_x, x)
            min_y = min(min_y, y)
            max_x = max(max_x, x)
            max_y = max(max_y, y)

    pixel_count = width * height
    bbox = [min_x, min_y, max_x, max_y] if visible_pixels else [-1, -1, -1, -1]
    bbox_area = 0
    if visible_pixels:
        bbox_area = (max_x - min_x + 1) * (max_y - min_y + 1)

    return {
        "width": width,
        "height": height,
        "mean_luma": total_luma / pixel_count,
        "max_luma": max_luma,
        "dark_ratio": dark_pixels / pixel_count,
        "visible_pixels": visible_pixels,
        "visible_ratio": visible_pixels / pixel_count,
        "visible_bbox": bbox,
        "visible_bbox_area": bbox_area,
        "visible_bbox_ratio": bbox_area / pixel_count,
    }


def read_png_metrics(path: pathlib.Path) -> dict[str, object]:
    width, height, rgba_pixels = decode_png_rgba(path)
    return compute_pixel_metrics(width, height, rgba_pixels)


def parse_render_ms(output: str) -> float | None:
    match = re.search(r"\((\d+)x(\d+),\s*([0-9.]+)\s*ms\)", output)
    if not match:
        return None
    return float(match.group(3))


def validate_foot_baseline(metrics: dict[str, object]) -> list[str]:
    failures: list[str] = []
    width = int(metrics.get("width", 0))
    height = int(metrics.get("height", 0))
    mean_luma = float(metrics.get("mean_luma", 0.0))
    max_luma = int(metrics.get("max_luma", 0))
    visible_pixels = int(metrics.get("visible_pixels", 0))
    bbox_ratio = float(metrics.get("visible_bbox_ratio", 0.0))
    if bbox_ratio == 0.0 and "visible_bbox" in metrics and width > 0 and height > 0:
        bbox = metrics["visible_bbox"]
        if isinstance(bbox, list) and len(bbox) == 4 and bbox[2] >= bbox[0] and bbox[3] >= bbox[1]:
            bbox_area = (int(bbox[2]) - int(bbox[0]) + 1) * (int(bbox[3]) - int(bbox[1]) + 1)
            bbox_ratio = bbox_area / float(width * height)
    render_ms = metrics.get("render_ms")

    if width < 1024 or height < 1024:
        failures.append(f"snapshot is too small: {width}x{height}")
    if mean_luma < 5.0:
        failures.append(f"mean luma is too low: {mean_luma:.2f}")
    if max_luma < 80:
        failures.append(f"max luma is too low: {max_luma}")
    if visible_pixels < 80_000:
        failures.append(f"visible pixel count is too low: {visible_pixels}")
    if bbox_ratio < 0.20:
        failures.append(f"visible bounding-box coverage is too low: {bbox_ratio:.3f}")
    if render_ms is not None and float(render_ms) > 750.0:
        failures.append(f"snapshot render is too slow for the baseline: {float(render_ms):.2f} ms")

    return failures


def run_snapshot(exe: pathlib.Path, volume: pathlib.Path, snapshot: pathlib.Path, timeout_seconds: int) -> tuple[str, float]:
    command = [
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
        "--snapshot",
        str(snapshot),
    ]
    start = time.perf_counter()
    completed = subprocess.run(
        command,
        cwd=exe.parent,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout_seconds,
    )
    elapsed_ms = (time.perf_counter() - start) * 1000.0
    if completed.returncode != 0:
        raise RuntimeError(f"snapshot command failed with exit {completed.returncode}:\n{completed.stdout}")
    return completed.stdout, elapsed_ms


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=pathlib.Path, default=pathlib.Path("volume_renderer_standalone.exe"))
    parser.add_argument("--volume", type=pathlib.Path, default=pathlib.Path("sample-data/foot_256x256x256_uint8.raw"))
    parser.add_argument("--snapshot", type=pathlib.Path, default=pathlib.Path("build/foot_render_regression.png"))
    parser.add_argument("--metrics", type=pathlib.Path, default=pathlib.Path("build/foot_render_regression_metrics.json"))
    parser.add_argument("--timeout", type=int, default=30)
    parser.add_argument("--no-render", action="store_true", help="verify an existing snapshot without launching CUDA")
    args = parser.parse_args(argv)

    root = pathlib.Path(__file__).resolve().parents[1]
    exe = (root / args.exe).resolve() if not args.exe.is_absolute() else args.exe
    volume = (root / args.volume).resolve() if not args.volume.is_absolute() else args.volume
    snapshot = (root / args.snapshot).resolve() if not args.snapshot.is_absolute() else args.snapshot
    metrics_path = (root / args.metrics).resolve() if not args.metrics.is_absolute() else args.metrics

    if not args.no_render:
        if not exe.exists():
            print(f"missing executable: {exe}", file=sys.stderr)
            return 2
        if not volume.exists():
            print(f"missing foot baseline volume: {volume}", file=sys.stderr)
            return 2
        snapshot.parent.mkdir(parents=True, exist_ok=True)
        output, elapsed_ms = run_snapshot(exe, volume, snapshot, args.timeout)
        print(output.strip())
    else:
        elapsed_ms = None
        if not snapshot.exists():
            print(f"missing snapshot: {snapshot}", file=sys.stderr)
            return 2

    metrics = read_png_metrics(snapshot)
    render_ms = None
    if not args.no_render:
        render_ms = parse_render_ms(output)
        metrics["process_elapsed_ms"] = elapsed_ms
    if render_ms is not None:
        metrics["render_ms"] = render_ms

    failures = validate_foot_baseline(metrics)
    metrics_path.parent.mkdir(parents=True, exist_ok=True)
    metrics_path.write_text(json.dumps(metrics, indent=2, sort_keys=True), encoding="utf-8")

    print("Foot render metrics:")
    print(json.dumps(metrics, indent=2, sort_keys=True))

    if failures:
        print("Foot render regression FAILED:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1

    print("Foot render regression passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
