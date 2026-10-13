"""Locate HUD calibration markers and vanilla bar icons in an adb screenshot.

The mod's calibration mode draws a cross at every icon origin the game reported
(cyan for hunger, magenta for health) at the raw, untransformed coordinates, and
writes the same values to `melonrind-diag.log` next to the overlay's pixel size.
Running this script on the screenshot yields the marker centres in screen pixels,
which together with the log line gives the affine transform between the game's
UI space and launcher-view pixels.

Usage examples:

    python analyze_hud_calibration.py shot.png
    python analyze_hud_calibration.py shot.png --probe 500 2000 700 2200
    python analyze_hud_calibration.py shot.png --runs 2140 190,120,60 40
"""

from __future__ import annotations

import argparse
from collections import Counter, deque

from PIL import Image

# Calibration marker colours as submitted by the mod: the raw crosses land in
# the renderer's own coordinate space, the calibrated ones must land on the
# vanilla icons.  Each entry carries its own tolerance because the calibrated
# colours are close to colours the world itself can contain.
MARKER_COLOURS = {
    "hunger_raw": ((0x00, 0xE5, 0xFF), 40),
    "heart_raw": ((0xFF, 0x4D, 0xD2), 40),
    "hunger_calibrated": ((0x39, 0xFF, 0x14), 12),
    "heart_calibrated": ((0xFF, 0xFF, 0x00), 8),
}


def load(path: str) -> Image.Image:
    image = Image.open(path)
    if image.mode != "RGB":
        image = image.convert("RGB")
    return image


def matches(pixel: tuple[int, int, int], colour: tuple[int, int, int],
            tolerance: int) -> bool:
    return all(abs(a - b) <= tolerance for a, b in zip(pixel, colour))


def clusters(image: Image.Image, colour: tuple[int, int, int], tolerance: int,
             minimum_pixels: int) -> list[dict[str, object]]:
    """Connected components of pixels close to `colour`."""
    width, height = image.size
    pixels = image.load()
    mask = bytearray(width * height)
    for y in range(height):
        for x in range(width):
            if matches(pixels[x, y], colour, tolerance):
                mask[y * width + x] = 1

    seen = bytearray(width * height)
    found: list[dict[str, object]] = []
    for start in range(width * height):
        if not mask[start] or seen[start]:
            continue
        queue = deque([start])
        seen[start] = 1
        points: list[tuple[int, int]] = []
        while queue:
            index = queue.popleft()
            y, x = divmod(index, width)
            points.append((x, y))
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < width and 0 <= ny < height:
                        neighbour = ny * width + nx
                        if mask[neighbour] and not seen[neighbour]:
                            seen[neighbour] = 1
                            queue.append(neighbour)
        if len(points) < minimum_pixels:
            continue
        xs = [p[0] for p in points]
        ys = [p[1] for p in points]
        # A marker is a cross plus a dot, so the geometric centre of the
        # bounding box is the exact origin that was submitted.
        found.append({
            "pixels": len(points),
            "center": ((min(xs) + max(xs)) / 2.0, (min(ys) + max(ys)) / 2.0),
            "bbox": (min(xs), min(ys), max(xs), max(ys)),
        })
    found.sort(key=lambda item: (item["center"][1], item["center"][0]))
    return found


def probe(image: Image.Image, box: tuple[int, int, int, int], top: int) -> None:
    x0, y0, x1, y1 = box
    region = image.crop((x0, y0, x1, y1))
    counts = Counter(region.getdata())
    print(f"top {top} colours in {box}:")
    for colour, count in counts.most_common(top):
        print(f"  {colour}  #{colour[0]:02x}{colour[1]:02x}{colour[2]:02x}  {count}")


def runs(image: Image.Image, y: int, colour: tuple[int, int, int],
         tolerance: int) -> None:
    """Contiguous x ranges on one row that match `colour`."""
    pixels = image.load()
    width, _ = image.size
    result: list[tuple[int, int]] = []
    start: int | None = None
    for x in range(width):
        if matches(pixels[x, y], colour, tolerance):
            if start is None:
                start = x
        elif start is not None:
            result.append((start, x - 1))
            start = None
    if start is not None:
        result.append((start, width - 1))
    print(f"row y={y}: {len(result)} runs matching {colour} +-{tolerance}")
    for begin, end in result:
        print(f"  x {begin}..{end}  (width {end - begin + 1})")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("screenshot")
    parser.add_argument("--minimum-pixels", type=int, default=6)
    parser.add_argument("--probe", type=int, nargs=4,
                        metavar=("X0", "Y0", "X1", "Y1"))
    parser.add_argument("--probe-top", type=int, default=12)
    parser.add_argument("--runs", type=int, nargs=3,
                        metavar=("Y", "R", "G"), help="row, then colour as R G B")
    parser.add_argument("--runs-b", type=int, default=None,
                        help="blue component for --runs")
    parser.add_argument("--runs-tolerance", type=int, default=0)
    args = parser.parse_args()

    image = load(args.screenshot)
    print(f"image {image.size[0]}x{image.size[1]}")

    if args.probe:
        probe(image, tuple(args.probe), args.probe_top)
    if args.runs is not None:
        y, r, g = args.runs
        colour = (r, g, args.runs_b if args.runs_b is not None else 0)
        runs(image, y, colour, args.runs_tolerance)

    for name, (colour, tolerance) in MARKER_COLOURS.items():
        found = clusters(image, colour, tolerance, args.minimum_pixels)
        print(f"{name} markers: {len(found)}")
        for index, item in enumerate(found):
            cx, cy = item["center"]
            print(f"  [{index}] center=({cx:.1f}, {cy:.1f}) "
                  f"bbox={item['bbox']} pixels={item['pixels']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
