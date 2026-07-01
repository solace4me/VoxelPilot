#!/usr/bin/env python3
"""Render and verify the tiny VTK headsq CT sample.

This complements the foot baseline. The CT-head sample is much smaller and
naturally dimmer, so this script uses CT-appropriate visibility thresholds
instead of the foot dataset's brightness thresholds.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import subprocess
import sys
import time

from verify_render_snapshot import read_png_metrics


def parse_render_ms(output: str) -> float | None:
    match = re.search(r"\((\d+)x(\d+),\s*([0-9.]+)\s*ms\)", output)
    if not match:
        return None
    return float(match.group(3))


def validate_headsq(metrics: dict[str, object]) -> list[str]:
    failures: list[str] = []
    width = int(metrics.get("width", 0))
    height = int(metrics.get("height", 0))
    mean_luma = float(metrics.get("mean_luma", 0.0))
    max_luma = int(metrics.get("max_luma", 0))
    visible_pixels = int(metrics.get("visible_pixels", 0))
    visible_ratio = float(metrics.get("visible_ratio", 0.0))
    bbox_ratio = float(metrics.get("visible_bbox_ratio", 0.0))
    render_ms = metrics.get("render_ms")

    if width < 1024 or height < 1024:
        failures.append(f"snapshot is too small: {width}x{height}")
    if mean_luma < 8.0:
        failures.append(f"CT head snapshot mean is too low: {mean_luma:.2f}")
    if max_luma < 120:
        failures.append(f"CT head snapshot peak is too low: {max_luma}")
    if visible_pixels < 120_000:
        failures.append(f"visible CT structure count is too low: {visible_pixels}")
    if visible_ratio < 0.10:
        failures.append(f"visible CT structure ratio is too low: {visible_ratio:.3f}")
    if bbox_ratio < 0.40:
        failures.append(f"visible CT bounding-box coverage is too low: {bbox_ratio:.3f}")
    if render_ms is not None and float(render_ms) > 750.0:
        failures.append(f"snapshot render is too slow: {float(render_ms):.2f} ms")
    return failures


def run_snapshot(exe: pathlib.Path, volume: pathlib.Path, snapshot: pathlib.Path, timeout_seconds: int) -> tuple[str, float]:
    command = [
        str(exe),
        "--volume",
        str(volume),
        "--width",
        "64",
        "--height",
        "64",
        "--depth",
        "93",
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
    parser.add_argument("--volume", type=pathlib.Path, default=pathlib.Path("sample-data/vtk-headsq/vtk_headsq_bone_64x64x93_uint8.raw"))
    parser.add_argument("--snapshot", type=pathlib.Path, default=pathlib.Path("build/vtk_headsq_bone_snapshot.png"))
    parser.add_argument("--metrics", type=pathlib.Path, default=pathlib.Path("build/vtk_headsq_bone_metrics.json"))
    parser.add_argument("--timeout", type=int, default=30)
    parser.add_argument("--no-render", action="store_true", help="verify an existing snapshot without launching CUDA")
    args = parser.parse_args(argv)

    repo_root = pathlib.Path.cwd()
    exe = (repo_root / args.exe).resolve()
    volume = (repo_root / args.volume).resolve()
    snapshot = (repo_root / args.snapshot).resolve()
    metrics_path = (repo_root / args.metrics).resolve()

    metrics_path.parent.mkdir(parents=True, exist_ok=True)
    snapshot.parent.mkdir(parents=True, exist_ok=True)

    render_ms = None
    elapsed_ms = None
    if not args.no_render:
        output, elapsed_ms = run_snapshot(exe, volume, snapshot, args.timeout)
        print(output, end="")
        render_ms = parse_render_ms(output)

    metrics = read_png_metrics(snapshot)
    if render_ms is not None:
        metrics["render_ms"] = render_ms
    if elapsed_ms is not None:
        metrics["process_elapsed_ms"] = elapsed_ms
    metrics_path.write_text(json.dumps(metrics, indent=2, sort_keys=True) + "\n")

    print("VTK headsq render metrics:")
    print(json.dumps(metrics, indent=2, sort_keys=True))

    failures = validate_headsq(metrics)
    if failures:
        print("VTK headsq render verification FAILED:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1

    print("VTK headsq render verification passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
