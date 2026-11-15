#!/usr/bin/env python3
"""WCAG contrast audit for plugin UI palettes.

Two ways to use it:

  # 1) Check explicit pairs (foreground background), hex like #c8f72d or 0xffc8f72d
  python3 contrast_audit.py --pair "#dde2dd" "#0c0e0c" --pair "#8c948c" "#111311"

  # 2) Parse named colours out of a C++ theme header (JUCE) or CSS and check
  #    every text / accent colour against every surface colour
  python3 contrast_audit.py Source/UIComponents.h

Why: dark plugin UIs drift toward low-contrast grey-on-grey. Eyeballing hides
it; the numbers do not. Thresholds (WCAG 2.x): body text 4.5:1, large text
(>= 18 px or 14 px bold) 3:1, graphics / control outlines 3:1.

Colours are classified by name, since a header only gives names:
  surface : bg, background, surface, panel, box, card, well, top, bottom, base
  text    : text, label, dim, muted, fg, ink, caption, value
  graphic : everything else that is not a surface (accent, neon, amber, edge...)
Use --text / --graphic / --surface to override with comma-separated names.
Colours with alpha < 255 are composited over each surface before measuring.
"""
import argparse
import re
import sys

SURFACE_RX = re.compile(r"bg|background|surface|panel|box|card|well|top$|bottom$|base|fill", re.I)
TEXT_RX = re.compile(r"text|label|dim|muted|fg|ink|caption|value", re.I)

# juce::Colour kName { 0xAARRGGBB }  /  Colour kName (0xRRGGBB)  /  Colours via fromString
JUCE_RX = re.compile(
    r"Colour\s+(\w+)\s*[\{\(]\s*0x([0-9a-fA-F]{6}|[0-9a-fA-F]{8})\s*[\}\)]")
# CSS custom properties and  constexpr uint32 kName = 0xAARRGGBB;
CSS_RX = re.compile(r"--([\w-]+)\s*:\s*#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})\b")
U32_RX = re.compile(r"(?:uint32|uint32_t|unsigned)\s+(\w+)\s*=\s*0x([0-9a-fA-F]{6}|[0-9a-fA-F]{8})")


def parse_hex(s):
    """Return (r, g, b, a) 0-255. 8-digit input is JUCE-style AARRGGBB."""
    s = s.strip().lower().replace("#", "").replace("0x", "")
    if len(s) == 6:
        s = "ff" + s
    if len(s) != 8:
        raise ValueError(f"bad colour: {s!r}")
    a, r, g, b = (int(s[i:i + 2], 16) for i in (0, 2, 4, 6))
    return r, g, b, a


def blend(fg, bg):
    """Composite fg (with alpha) over opaque bg."""
    a = fg[3] / 255.0
    return tuple(round(fg[i] * a + bg[i] * (1 - a)) for i in range(3))


def lin(c):
    c = c / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def luminance(rgb):
    r, g, b = rgb[:3]
    return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b)


def ratio(fg, bg):
    fg_rgb = blend(fg, bg) if fg[3] < 255 else fg[:3]
    l1, l2 = luminance(fg_rgb), luminance(bg[:3])
    hi, lo = max(l1, l2), min(l1, l2)
    return (hi + 0.05) / (lo + 0.05)


def text_grade(r):
    if r >= 7:
        return "AAA"
    if r >= 4.5:
        return "AA"
    if r >= 3:
        return "large-only"
    return "FAIL"


def graphic_grade(r):
    return "ok" if r >= 3 else "LOW"


def hexstr(c):
    return "#%02x%02x%02x" % c[:3] + ("" if c[3] == 255 else f"@{c[3] / 255:.2f}")


def parse_file(path):
    txt = open(path, encoding="utf-8", errors="replace").read()
    found = {}
    for rx in (JUCE_RX, CSS_RX, U32_RX):
        for name, hx in rx.findall(txt):
            found.setdefault(name, parse_hex(hx))
    return found


def pick(names, override, rx):
    if override:
        wanted = [n.strip() for n in override.split(",") if n.strip()]
        return [n for n in wanted if n in names]
    return [n for n in names if rx.search(n)]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="*", help="C++ headers or CSS files with named colours")
    ap.add_argument("--pair", nargs=2, action="append", metavar=("FG", "BG"), help="explicit pair; repeatable")
    ap.add_argument("--text", help="comma-separated names to treat as text")
    ap.add_argument("--graphic", help="comma-separated names to treat as graphics / accents")
    ap.add_argument("--surface", help="comma-separated names to treat as surfaces")
    ap.add_argument("--strict", action="store_true", help="exit 1 if any text pair is below 4.5:1")
    args = ap.parse_args()

    if not args.files and not args.pair:
        ap.error("give a file to parse or at least one --pair")

    failures = 0

    if args.pair:
        print("Explicit pairs")
        print(f"{'foreground':<16}{'background':<16}{'ratio':>7}   text      graphic")
        for fg_s, bg_s in args.pair:
            fg, bg = parse_hex(fg_s), parse_hex(bg_s)
            r = ratio(fg, bg)
            if r < 4.5:
                failures += 1
            print(f"{hexstr(fg):<16}{hexstr(bg):<16}{r:>6.2f}:1  {text_grade(r):<10}{graphic_grade(r)}")
        print()

    for path in args.files:
        colours = parse_file(path)
        if not colours:
            print(f"{path}: no named colours found (looked for juce::Colour k{{0x..}}, CSS --var: #hex, uint32 k = 0x..)")
            continue
        surfaces = pick(colours, args.surface, SURFACE_RX)
        explicit_text = pick(colours, args.text, TEXT_RX)
        texts = [n for n in explicit_text if n not in surfaces]
        graphics = pick(colours, args.graphic, re.compile(r".")) if args.graphic else \
            [n for n in colours if n not in surfaces and n not in texts]
        print(f"{path}: {len(colours)} colours  |  surfaces: {', '.join(surfaces) or '-'}")
        if not surfaces:
            print("  No surface colours detected by name. Pass --surface name1,name2")
            continue
        print(f"\n  {'colour':<14}{'hex':<14}{'on surface':<14}{'ratio':>7}   verdict")
        for kind, names in (("text", texts), ("graphic", graphics)):
            for n in names:
                for s in surfaces:
                    r = ratio(colours[n], colours[s])
                    if kind == "text":
                        verdict = text_grade(r)
                        if r < 4.5:
                            failures += 1
                    else:
                        verdict = graphic_grade(r)
                    print(f"  {n:<14}{hexstr(colours[n]):<14}{s:<14}{r:>6.2f}:1  {kind}: {verdict}")
        print("\n  Note: names are classified heuristically. Low graphic contrast is fine for")
        print("  deliberately subtle borders (kEdge-style) but not for knob arcs, meters or toggles.")

    if args.strict and failures:
        print(f"\n{failures} text pair(s) below 4.5:1")
        sys.exit(1)


if __name__ == "__main__":
    main()
