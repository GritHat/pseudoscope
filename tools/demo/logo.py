#!/usr/bin/env python3
"""The mark of Pseudoscope: a lens over three lines of pseudocode, the one that
is running lit up, with the arrow that says "here". It is described once, on
a square of 100, and drawn from that both as SVG and on a picture (Pillow).

Usage: logo.py OUTPUT.svg
"""
import sys

BLUE, YELLOW, PAPER, INK = "#5fafff", "#ffd75f", "#d4d6dc", "#0d0e12"

RING = (42, 42, 30, 9)                 # centre, radius, width of the line
HANDLE = (65, 65, 88, 88, 11)          # from, to, width
LINES = ((27, 30, 50, PAPER), (36, 42, 60, YELLOW), (36, 54, 53, PAPER))  # from x, at y, to x
ARROW = ((23, 37.5), (23, 46.5), (31, 42))
THICK = 6.5
INSET = 0.84                           # how much of its tile the mark fills


def svg(tile=True):
    """As SVG; on a dark tile, so that it reads on any background."""
    cx, cy, r, w = RING
    x0, y0, x1, y1, hw = HANDLE
    out = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" role="img" aria-label="Pseudoscope">']
    if tile:
        move = 50 * (1 - INSET)
        out.append(f'  <rect width="100" height="100" rx="22" fill="{INK}"/>')
        out.append(f'  <g transform="translate({move:g} {move:g}) scale({INSET})">')
    out.append(f'    <circle cx="{cx}" cy="{cy}" r="{r}" fill="none" stroke="{BLUE}" stroke-width="{w}"/>')
    out.append(f'    <line x1="{x0}" y1="{y0}" x2="{x1}" y2="{y1}" stroke="{BLUE}" stroke-width="{hw}" stroke-linecap="round"/>')
    for a, y, b, colour in LINES:
        out.append(f'    <rect x="{a}" y="{y - THICK / 2:g}" width="{b - a}" height="{THICK}" rx="{THICK / 2:g}" fill="{colour}"/>')
    out.append(f'    <polygon points="{" ".join(f"{x:g},{y:g}" for x, y in ARROW)}" fill="{YELLOW}"/>')
    if tile:
        out.append("  </g>")
    return "\n".join(out + ["</svg>", ""])


def draw(image, x, y, size, tile=False):
    """On a Pillow picture, in a square of `size` whose corner is (x, y)."""
    from PIL import Image, ImageDraw

    fine = 4  # drawn larger and reduced, for smooth edges
    layer = Image.new("RGBA", (size * fine, size * fine), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    unit = size * fine / 100
    inset = INSET if tile else 1.0

    def at(*numbers):
        return [(50 + (n - 50) * inset) * unit for n in numbers]

    if tile:
        d.rounded_rectangle([0, 0, 100 * unit - 1, 100 * unit - 1], 22 * unit, fill=INK)
    cx, cy, r, w = RING
    half = r + w / 2
    d.ellipse(at(cx - half, cy - half, cx + half, cy + half), outline=BLUE, width=round(w * inset * unit))
    x0, y0, x1, y1, hw = HANDLE
    d.line(at(x0, y0, x1, y1), fill=BLUE, width=round(hw * inset * unit))
    for px, py in ((x0, y0), (x1, y1)):
        d.ellipse(at(px - hw / 2, py - hw / 2, px + hw / 2, py + hw / 2), fill=BLUE)
    for a, line, b, colour in LINES:
        d.rounded_rectangle(at(a, line - THICK / 2, b, line + THICK / 2), THICK / 2 * inset * unit, fill=colour)
    d.polygon([tuple(at(px, py)) for px, py in ARROW], fill=YELLOW)
    layer = layer.resize((size, size), Image.LANCZOS)
    image.paste(layer, (round(x), round(y)), layer)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    with open(sys.argv[1], "w", encoding="utf-8") as out:
        out.write(svg())
