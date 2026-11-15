#!/usr/bin/env python3
"""Close-inspection helper for plugin UI screenshots.

A full-frame screenshot is downscaled when you look at it, which hides the
defects a reviewer notices: text clipped by one pixel, uneven gaps, knobs a
couple of pixels off-axis, muddy edges. This tool lets you look closer.

  # tile the image into 2 columns x 2 rows (native resolution, optional zoom)
  python3 zoom_review.py shot.png --grid 2x2 --scale 1.5

  # magnify one region (x y w h in source pixels)
  python3 zoom_review.py shot.png --crop 40 650 560 200 --scale 2

  # overlay an alignment grid (spacing in source pixels, e.g. 8 for an 8 px grid;
  # use spacing * 2 if the screenshot was rendered at 2x)
  python3 zoom_review.py shot.png --overlay-grid 16

  # before / after side by side with labels
  python3 zoom_review.py new.png --compare old.png

Outputs are written to --out-dir (default: <tmp>/vst-zoom/<name>/) and the
paths are printed; open them with the Read tool.
"""
import argparse
import os
import sys
import tempfile

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("Pillow is required: pip install pillow")


def out_path(out_dir, stem, suffix):
    os.makedirs(out_dir, exist_ok=True)
    return os.path.join(out_dir, f"{stem}_{suffix}.png")


def upscale(img, scale):
    if scale == 1:
        return img
    # NEAREST keeps individual pixels visible, which is the point of zooming in.
    return img.resize((round(img.width * scale), round(img.height * scale)), Image.NEAREST)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image")
    ap.add_argument("--grid", metavar="CxR", help="split into C columns and R rows, e.g. 2x2")
    ap.add_argument("--crop", nargs=4, type=int, metavar=("X", "Y", "W", "H"))
    ap.add_argument("--scale", type=float, default=1.0, help="magnification for tiles / crops (default 1)")
    ap.add_argument("--overlay-grid", type=int, metavar="PX", help="draw alignment grid lines every PX source pixels")
    ap.add_argument("--compare", metavar="OTHER", help="side-by-side with another image (this one on the right = 'after')")
    ap.add_argument("--out-dir", default=None)
    args = ap.parse_args()

    img = Image.open(args.image).convert("RGBA")
    stem = os.path.splitext(os.path.basename(args.image))[0]
    out_dir = args.out_dir or os.path.join(tempfile.gettempdir(), "vst-zoom", stem)
    written = []

    print(f"{args.image}: {img.width} x {img.height} px")

    if args.crop:
        x, y, w, h = args.crop
        box = (max(0, x), max(0, y), min(img.width, x + w), min(img.height, y + h))
        p = out_path(out_dir, stem, f"crop_{x}_{y}_{w}x{h}")
        upscale(img.crop(box), args.scale).save(p)
        written.append(p)

    if args.grid:
        try:
            cols, rows = (int(v) for v in args.grid.lower().split("x"))
        except ValueError:
            sys.exit("--grid must look like 2x2")
        tw, th = img.width / cols, img.height / rows
        for r in range(rows):
            for c in range(cols):
                box = (round(c * tw), round(r * th), round((c + 1) * tw), round((r + 1) * th))
                p = out_path(out_dir, stem, f"tile_r{r + 1}c{c + 1}")
                upscale(img.crop(box), args.scale).save(p)
                written.append(p)

    if args.overlay_grid:
        g = Image.new("RGBA", img.size, (0, 0, 0, 0))
        d = ImageDraw.Draw(g)
        step = args.overlay_grid
        for x in range(0, img.width, step):
            major = (x // step) % 4 == 0
            d.line([(x, 0), (x, img.height)], fill=(255, 0, 255, 90 if major else 40))
        for y in range(0, img.height, step):
            major = (y // step) % 4 == 0
            d.line([(0, y), (img.width, y)], fill=(255, 0, 255, 90 if major else 40))
        p = out_path(out_dir, stem, f"grid{step}")
        Image.alpha_composite(img, g).save(p)
        written.append(p)

    if args.compare:
        other = Image.open(args.compare).convert("RGBA")
        label_h, gap = 22, 12
        h = max(img.height, other.height)
        canvas = Image.new("RGBA", (other.width + img.width + gap, h + label_h), (24, 24, 24, 255))
        d = ImageDraw.Draw(canvas)
        d.text((6, 5), "BEFORE: " + os.path.basename(args.compare), fill=(220, 220, 220, 255))
        d.text((other.width + gap + 6, 5), "AFTER: " + os.path.basename(args.image), fill=(220, 220, 220, 255))
        canvas.paste(other, (0, label_h))
        canvas.paste(img, (other.width + gap, label_h))
        p = out_path(out_dir, stem, "compare")
        canvas.save(p)
        written.append(p)

    if not written:
        sys.exit("nothing to do: pass --grid, --crop, --overlay-grid or --compare")
    for p in written:
        print(p)


if __name__ == "__main__":
    main()
