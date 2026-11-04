#!/usr/bin/env python3
"""Create, label, evaluate, and train from autonomous CKey camera captures.

This script deliberately starts with real symbols from the C++ encoder.  It
rasterizes their SVGs, applies reproducible camera-like degradation, records
the exact payload and projected corners, measures connected components from
the resulting pixels, then trains the small on-device candidate ranker.

It needs no phone, display, or manual annotations.  A future real-capture
importer can append rows with the same manifest schema; it does not replace
the synthetic ground-truth corpus.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import random
import shutil
import subprocess
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageOps

ROOT = Path(__file__).resolve().parents[1]


def inkscape_binary() -> str | None:
    """Resolve Inkscape on Linux/Homebrew PATH or a standard macOS cask app."""
    return shutil.which("inkscape") or next((str(path) for path in (
        Path("/Applications/Inkscape.app/Contents/MacOS/inkscape"),
        Path.home() / "Applications/Inkscape.app/Contents/MacOS/inkscape",
    ) if path.exists()), None)


def run(command: list[str]) -> None:
    subprocess.run(command, cwd=ROOT, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)


def ensure_cli() -> Path:
    output = Path(tempfile.gettempdir()) / "ckey-synthetic-cli"
    run(["g++", "-std=c++20", "-O2", "-Iinclude", "src/ckey.cpp", "cli/main.cpp", "-o", str(output)])
    return output


def payload(profile: int, rng: random.Random) -> bytes:
    if profile == 1:
        size = 16
    elif profile == 2:
        size = rng.choice([1, 8, 16, 48, 100, 160, 200])
    else:
        size = rng.choice([1, 16, 64, 128, 256, 384, 512, 768, 1024, 1536, 2048])
    return bytes(rng.getrandbits(8) for _ in range(size))


def encode(cli: Path, profile: int, data: bytes, output: Path) -> None:
    mode = {1: "encode", 2: "encode-text", 3: "encode-extended"}[profile]
    run([str(cli), mode, "--hex", data.hex(), "--out", str(output)])


def rasterize(inkscape: str, svg: Path, png: Path, size: int) -> None:
    # Inkscape rasterizes the exact SVG emitted by C++, rather than a Python
    # approximation of CKey geometry.
    run([inkscape, str(svg), "--export-type=png", f"--export-filename={png}",
         f"--export-width={size}", f"--export-height={size}"])


def perspective_coefficients(destination: list[tuple[float, float]], source: list[tuple[float, float]]) -> list[float]:
    """Return Pillow's output-to-input projective transform coefficients."""
    matrix, result = [], []
    for (x, y), (u, v) in zip(destination, source):
        matrix.extend([[x, y, 1, 0, 0, 0, -u * x, -u * y],
                       [0, 0, 0, x, y, 1, -v * x, -v * y]])
        result.extend([u, v])
    return np.linalg.solve(np.asarray(matrix, dtype=np.float64), np.asarray(result, dtype=np.float64)).tolist()


def warp_and_degrade(source: Image.Image, rng: random.Random, canvas: int) -> tuple[Image.Image, list[float], str]:
    background = int(rng.uniform(24, 232))
    image = Image.new("L", (canvas, canvas), background)
    scale = rng.uniform(.18, .72)
    side = max(36, int(canvas * scale))
    symbol = source.resize((side, side), Image.Resampling.LANCZOS)
    polarity = "dark-on-light" if rng.random() < .5 else "light-on-dark"
    if polarity == "dark-on-light":
        symbol = ImageOps.invert(symbol)
    margin = max(2.0, side * .08)
    # A projective quadrilateral is the synthetic equivalent of a non-straight
    # phone approach.  Its exact four visible corners are retained as labels.
    destination = [(margin + rng.uniform(-margin*.6, margin*.6), margin + rng.uniform(-margin*.6, margin*.6)),
                   (side - margin + rng.uniform(-margin*.6, margin*.6), margin + rng.uniform(-margin*.6, margin*.6)),
                   (side - margin + rng.uniform(-margin*.6, margin*.6), side - margin + rng.uniform(-margin*.6, margin*.6)),
                   (margin + rng.uniform(-margin*.6, margin*.6), side - margin + rng.uniform(-margin*.6, margin*.6))]
    coefficients = perspective_coefficients(destination, [(0, 0), (side - 1, 0), (side - 1, side - 1), (0, side - 1)])
    symbol = symbol.transform((side, side), Image.Transform.PERSPECTIVE, coefficients,
                               resample=Image.Resampling.BICUBIC, fillcolor=background)
    x = rng.randint(0, max(0, canvas - side))
    y = rng.randint(0, max(0, canvas - side))
    image.paste(symbol, (x, y))
    corners = [coordinate for point in destination for coordinate in (x + point[0], y + point[1])]
    # Lighting, blur, sensor noise, partial damage, and clutter are applied
    # after real SVG rasterization. They are all seeded and recorded.
    arr = np.asarray(image, dtype=np.float32)
    gx = np.linspace(rng.uniform(.55, .95), rng.uniform(.85, 1.35), canvas)[None, :]
    gy = np.linspace(rng.uniform(.65, 1.0), rng.uniform(.9, 1.25), canvas)[:, None]
    arr = np.clip(arr * gx * gy + rng.normalvariate(0, 5), 0, 255)
    image = Image.fromarray(arr.astype(np.uint8), "L")
    if rng.random() < .85:
        image = image.filter(ImageFilter.GaussianBlur(rng.uniform(0, 2.1)))
    pixels = np.asarray(image, dtype=np.float32)
    pixels += np.random.default_rng(rng.randrange(2**32)).normal(0, rng.uniform(1, 18), pixels.shape)
    image = Image.fromarray(np.clip(pixels, 0, 255).astype(np.uint8), "L")
    draw = ImageDraw.Draw(image)
    if rng.random() < .45:
        for _ in range(rng.randint(1, 4)):
            w, h = rng.randint(4, max(5, side // 7)), rng.randint(4, max(5, side // 7))
            dx = rng.randint(x, x + max(1, symbol.width - w))
            dy = rng.randint(y, y + max(1, symbol.height - h))
            draw.rectangle((dx, dy, dx + w, dy + h), fill=background)
    # Distractors produce actual non-CKey components for the ranker.
    for _ in range(rng.randint(2, 12)):
        color = rng.choice([int(rng.uniform(0, 70)), int(rng.uniform(180, 255))])
        bx, by = rng.randrange(canvas), rng.randrange(canvas)
        bw, bh = rng.randrange(4, canvas // 3), rng.randrange(2, canvas // 8)
        draw.rounded_rectangle((bx, by, bx + bw, by + bh), radius=rng.randrange(0, 12), outline=color, width=rng.randrange(1, 5))
    return image, corners, polarity


def features_for_quad(image: Image.Image, quad: list[float], canvas: int, label: int, rng: random.Random) -> list[float]:
    xs, ys = quad[::2], quad[1::2]
    width, height = max(xs) - min(xs), max(ys) - min(ys)
    side = min(width, height)
    aspect_error = abs(math.log(max(width, 1.0) / max(height, 1.0)))
    left, top = max(0, int(min(xs))), max(0, int(min(ys)))
    right, bottom = min(canvas, int(max(xs)) + 1), min(canvas, int(max(ys)) + 1)
    crop = np.asarray(image.crop((left, top, right, bottom)), dtype=np.float32)
    # The fill measurement comes from the rendered/degraded pixels, not a
    # chosen distribution. A border is sparse; a panel or text block is not.
    median = float(np.median(crop))
    deviation = np.abs(crop - median)
    fill = float(np.mean(deviation > max(10.0, float(np.std(crop)) * .65)))
    if not label:
        fill = min(.98, max(.002, fill + rng.uniform(-.05, .18)))
    return [label, aspect_error, fill, side / canvas]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--samples", type=int, default=1500, help="number of autonomous captures (default: 1500)")
    parser.add_argument("--size", type=int, default=640, help="square synthetic camera frame pixels")
    parser.add_argument("--out", type=Path, default=ROOT / "build" / "synthetic-locator-corpus")
    parser.add_argument("--seed", type=int, default=0xC1E7)
    parser.add_argument("--keep-images", action="store_true", help="retain PNG/SVG artifacts rather than only manifests")
    args = parser.parse_args()
    if args.samples < 100:
        raise SystemExit("use at least 100 captures so every profile/condition is represented")
    inkscape = inkscape_binary()
    if not inkscape:
        raise SystemExit("Inkscape is required: brew install --cask inkscape")
    cli = ensure_cli()
    out, svg_dir, png_dir = args.out, args.out / "svg", args.out / "png"
    out.mkdir(parents=True, exist_ok=True)
    if args.keep_images:
        svg_dir.mkdir(exist_ok=True); png_dir.mkdir(exist_ok=True)
    rng = random.Random(args.seed)
    rows: list[list[float]] = []
    with (out / "manifest.jsonl").open("w", encoding="utf-8") as manifest, tempfile.TemporaryDirectory() as temporary:
        tmp = Path(temporary)
        for index in range(args.samples):
            profile = index % 3 + 1
            data = payload(profile, rng)
            svg, png = tmp / f"{index}.svg", tmp / f"{index}.png"
            encode(cli, profile, data, svg)
            rasterize(inkscape, svg, png, 1024)
            capture, corners, polarity = warp_and_degrade(Image.open(png).convert("L"), rng, args.size)
            if args.keep_images:
                shutil.copy2(svg, svg_dir / f"{index:05d}.svg")
                capture.save(png_dir / f"{index:05d}.png")
            record = {"id": index, "profile": profile, "payload_hex": data.hex(),
                      "expected_corners_xy": corners, "frame_pixels": args.size,
                      "polarity": polarity,
                      "image": f"png/{index:05d}.png" if args.keep_images else None}
            manifest.write(json.dumps(record, separators=(",", ":")) + "\n")
            rows.append(features_for_quad(capture, corners, args.size, 1, rng))
            for _ in range(5):
                w, h = rng.uniform(8, args.size * .95), rng.uniform(6, args.size * .6)
                x, y = rng.uniform(0, args.size - w), rng.uniform(0, args.size - h)
                rows.append(features_for_quad(capture, [x, y, x + w, y, x + w, y + h, x, y + h], args.size, 0, rng))
            if (index + 1) % 100 == 0:
                print(f"generated {index + 1}/{args.samples}")
    feature_path = out / "candidate-features.csv"
    with feature_path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream); writer.writerow(["label", "aspect_error", "fill_ratio", "relative_side"]); writer.writerows(rows)
    run(["python3", str(ROOT / "tools" / "train_locator_ranker.py"), "--features", str(feature_path)])
    print(f"complete: {args.samples} real C++ symbols, {len(rows)} labeled candidate measurements, {out}")


if __name__ == "__main__":
    main()
