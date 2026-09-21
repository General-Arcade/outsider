# Copyright (c) 2026 General Arcade (Pte. Ltd.)
# SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
"""Screenshot comparison (Pillow only, no numpy).

compare_images() returns per-pair metrics plus a mask of changed pixels;
diff_regions() groups the mask into rectangles so a report can say where a
scene differs, not just how much.
"""

from PIL import Image, ImageChops, ImageStat

BLOCK = 24              # hotspot grid cell size in pixels


def load_rgb(path):
    return Image.open(path).convert("RGB")


def changed_mask(a, b, tolerance):
    """Binary mask (mode L, 0/255) of pixels whose largest channel
    difference exceeds ``tolerance``."""
    diff = ImageChops.difference(a, b)
    r, g, bl = diff.split()
    strongest = ImageChops.lighter(ImageChops.lighter(r, g), bl)
    lut = [255 if v > tolerance else 0 for v in range(256)]
    return strongest.point(lut), strongest


def compare_images(a, b, tolerance=24):
    """Return (metrics dict, mask image). ``b`` is resized to ``a`` when the
    sizes differ; the metrics record that."""
    metrics = {"width": a.width, "height": a.height, "size_mismatch": None}
    if a.size != b.size:
        metrics["size_mismatch"] = [b.width, b.height]
        b = b.resize(a.size, Image.BILINEAR)
    mask, strongest = changed_mask(a, b, tolerance)
    total = a.width * a.height
    changed = mask.histogram()[255]
    metrics["changed_pixels"] = changed
    metrics["changed_pct"] = round(100.0 * changed / total, 3) if total else 0.0
    metrics["mean_diff"] = round(ImageStat.Stat(strongest).mean[0], 3)
    bbox = mask.getbbox()
    metrics["bbox"] = list(bbox) if bbox else None
    metrics["regions"] = diff_regions(mask)
    return metrics, mask


def diff_regions(mask, block=BLOCK, min_fraction=0.02, max_regions=8):
    """Connected groups of grid cells in which more than ``min_fraction`` of
    the pixels changed, as [x, y, w, h, changed_pct] sorted by size."""
    cols = (mask.width + block - 1) // block
    rows = (mask.height + block - 1) // block
    hot = {}
    for row in range(rows):
        for col in range(cols):
            box = (col * block, row * block,
                   min((col + 1) * block, mask.width), min((row + 1) * block, mask.height))
            cell = mask.crop(box)
            fraction = cell.histogram()[255] / float(cell.width * cell.height)
            if fraction > min_fraction:
                hot[(col, row)] = fraction

    regions = []
    seen = set()
    for start in hot:
        if start in seen:
            continue
        stack = [start]
        seen.add(start)
        cells = []
        while stack:
            cell = stack.pop()
            cells.append(cell)
            c, r = cell
            for n in ((c + 1, r), (c - 1, r), (c, r + 1), (c, r - 1)):
                if n in hot and n not in seen:
                    seen.add(n)
                    stack.append(n)
        c0 = min(c for c, _ in cells)
        c1 = max(c for c, _ in cells)
        r0 = min(r for _, r in cells)
        r1 = max(r for _, r in cells)
        x, y = c0 * block, r0 * block
        w = min((c1 + 1) * block, mask.width) - x
        h = min((r1 + 1) * block, mask.height) - y
        region = mask.crop((x, y, x + w, y + h))
        pct = 100.0 * region.histogram()[255] / float(w * h)
        regions.append([x, y, w, h, round(pct, 1)])
    regions.sort(key=lambda r: -(r[2] * r[3] * r[4]))
    return regions[:max_regions]


def verdict(metrics, minor_pct=0.5, major_pct=5.0):
    pct = metrics["changed_pct"]
    if metrics.get("size_mismatch"):
        return "differ"
    if pct <= minor_pct:
        return "match"
    if pct <= major_pct:
        return "minor"
    return "differ"


def diff_visual(a, b, mask):
    """Side-by-side image: original | outsider | changed pixels in red over a
    dimmed original."""
    if b.size != a.size:
        b = b.resize(a.size, Image.BILINEAR)
    dimmed = Image.eval(a, lambda v: v // 3)
    red = Image.new("RGB", a.size, (255, 40, 40))
    highlight = Image.composite(red, dimmed, mask)
    out = Image.new("RGB", (a.width * 3 + 8, a.height), (32, 32, 32))
    out.paste(a, (0, 0))
    out.paste(b, (a.width + 4, 0))
    out.paste(highlight, (a.width * 2 + 8, 0))
    return out
