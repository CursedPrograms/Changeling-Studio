"""make_logo.py - draws the Changeling logo (site/logo.svg and site/icon.svg).

After Gemini's sketch in Changeling Imago (logo.jfif): a face split down the middle, one half a plain
face, the other half breaking up into pixels. Redrawn in the Cursed colours: the pixels are the apps'
pink in five shades, the face a warm skin tone, on the windows' dark grey. Solid shapes only (no
gradients, no shadows). Run it again after changing a colour.
"""
import math
import os
import random

HERE = os.path.dirname(os.path.abspath(__file__))
BG = "#1a1a1a"
SKIN, SKIN_SHADE, FEATURE = "#f0c4a8", "#d89a7c", "#1a1a1a"
PINKS = ["#007bb7", "#005184", "#34a9e3", "#004067", "#5bc0f0"]
DARK = "#5e0b35"


def face_outline(cx, cy, w, h, n=80):
    """A face: rounder at the brow, narrower to the chin."""
    pts = []
    for i in range(n):
        t = 2 * math.pi * i / n
        x, y = math.sin(t), -math.cos(t)          # start at the top, clockwise
        narrow = 1 - 0.28 * max(0.0, y) ** 1.6     # the lower half narrows to the chin
        pts.append((cx + x * w / 2 * narrow, cy + y * h / 2))
    return pts


def path(pts):
    return "M" + " L".join(f"{x:.1f},{y:.1f}" for x, y in pts) + " Z"


def logo(size=512, seed=11, detail=True):
    rnd = random.Random(seed)
    cx, cy, w, h = size / 2, size * 0.52, size * 0.50, size * 0.70
    outline = face_outline(cx, cy, w, h)
    face = path(outline)
    s = []
    s.append(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {size} {size}">')
    s.append(f'<defs><clipPath id="face"><path d="{face}"/></clipPath>'
             f'<clipPath id="right"><rect x="{cx}" y="0" width="{size}" height="{size}"/></clipPath></defs>')
    s.append(f'<rect width="{size}" height="{size}" rx="{size * 0.18:.0f}" fill="{BG}"/>')
    # left half: the plain face
    s.append(f'<path d="{face}" fill="{SKIN}"/>')
    if detail:
        u = size / 512
        # brow, eye, nose line, mouth: flat shapes, the left half only
        s.append(f'<path d="M{cx - 92 * u:.1f},{cy - 70 * u:.1f} Q{cx - 50 * u:.1f},{cy - 98 * u:.1f} {cx - 12 * u:.1f},{cy - 78 * u:.1f} '
                 f'L{cx - 14 * u:.1f},{cy - 66 * u:.1f} Q{cx - 52 * u:.1f},{cy - 80 * u:.1f} {cx - 90 * u:.1f},{cy - 60 * u:.1f} Z" fill="{FEATURE}"/>')
        s.append(f'<path d="M{cx - 86 * u:.1f},{cy - 30 * u:.1f} Q{cx - 52 * u:.1f},{cy - 54 * u:.1f} {cx - 18 * u:.1f},{cy - 30 * u:.1f} '
                 f'Q{cx - 52 * u:.1f},{cy - 14 * u:.1f} {cx - 86 * u:.1f},{cy - 30 * u:.1f} Z" fill="{FEATURE}"/>')
        s.append(f'<path d="M{cx - 6 * u:.1f},{cy - 40 * u:.1f} L{cx - 30 * u:.1f},{cy + 46 * u:.1f} L{cx - 4 * u:.1f},{cy + 56 * u:.1f}" '
                 f'fill="none" stroke="{SKIN_SHADE}" stroke-width="{9 * u:.1f}" stroke-linecap="round" stroke-linejoin="round"/>')
        s.append(f'<path d="M{cx - 64 * u:.1f},{cy + 104 * u:.1f} Q{cx - 30 * u:.1f},{cy + 92 * u:.1f} {cx},{cy + 100 * u:.1f} '
                 f'L{cx},{cy + 112 * u:.1f} Q{cx - 30 * u:.1f},{cy + 106 * u:.1f} {cx - 64 * u:.1f},{cy + 104 * u:.1f} Z" fill="{SKIN_SHADE}"/>')
    # right half: the pixels, clipped to the face; a few stray ones break out past its edge
    cell = size / (22 if detail else 8)
    s.append('<g clip-path="url(#right)"><g clip-path="url(#face)">')
    y = cy - h / 2
    while y < cy + h / 2:
        x = cx
        while x < cx + w / 2:
            c = rnd.choice(PINKS + [DARK])
            s.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{cell + 0.5:.1f}" height="{cell + 0.5:.1f}" fill="{c}"/>')
            x += cell
        y += cell
    s.append('</g>')
    for _ in range(9 if detail else 4):            # breaking away from the face
        x = cx + w / 2 * rnd.uniform(0.75, 1.05)
        yy = cy + h / 2 * rnd.uniform(-0.75, 0.6)
        x = cx + round((x - cx) / cell) * cell
        yy = cy - h / 2 + round((yy - (cy - h / 2)) / cell) * cell
        sz = cell * rnd.choice([0.5, 0.75, 1])
        s.append(f'<rect x="{x:.1f}" y="{yy:.1f}" width="{sz:.1f}" height="{sz:.1f}" fill="{rnd.choice(PINKS)}"/>')
    s.append('</g>')
    if detail:
        u = size / 512
        # the right eye and mouth, dark through the pixels, so it still reads as one face
        s.append(f'<path d="M{cx + 86 * u:.1f},{cy - 30 * u:.1f} Q{cx + 52 * u:.1f},{cy - 54 * u:.1f} {cx + 18 * u:.1f},{cy - 30 * u:.1f} '
                 f'Q{cx + 52 * u:.1f},{cy - 14 * u:.1f} {cx + 86 * u:.1f},{cy - 30 * u:.1f} Z" fill="{BG}" opacity="0.85"/>')
        s.append(f'<path d="M{cx + 64 * u:.1f},{cy + 104 * u:.1f} Q{cx + 30 * u:.1f},{cy + 92 * u:.1f} {cx},{cy + 100 * u:.1f} '
                 f'L{cx},{cy + 112 * u:.1f} Q{cx + 30 * u:.1f},{cy + 106 * u:.1f} {cx + 64 * u:.1f},{cy + 104 * u:.1f} Z" fill="{BG}" opacity="0.6"/>')
    # the split
    s.append(f'<line x1="{cx:.1f}" y1="{cy - h / 2 - size * 0.02:.1f}" x2="{cx:.1f}" y2="{cy + h / 2 + size * 0.02:.1f}" '
             f'stroke="{BG}" stroke-width="{size * 0.012:.1f}"/>')
    s.append('</svg>')
    return "\n".join(s)


if __name__ == "__main__":
    for name, size, detail in (("logo.svg", 512, True), ("icon.svg", 64, False)):
        with open(os.path.join(HERE, name), "w", encoding="utf-8", newline="\n") as f:
            f.write(logo(size, detail=detail))
    print("logo.svg, icon.svg")
