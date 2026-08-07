#!/usr/bin/env python3
"""Rebuild the 350x350 LDO/Voron bed texture.

The stock texture carves the pocket behind the branding by hand-truncating
individual grid lines, so the pocket stops matching the moment the artwork
moves. Here the grid is generated and clipped against the artwork's real
bounding box, so the two cannot drift apart.

SVG y runs downward and the texture is mapped with the top of the image at the
back of the bed, so bed Y = 350 - svg_y. The two AWD keep-out notches are at
bed (0..40, 0..40) and (310..350, 0..40), i.e. svg y 310..350 along the bottom.
"""

import math
import re

# ---------------------------------------------------------------- parameters

BED = 350.0
FINE_STEP = 10.0
MAJOR_STEP = 50.0

NOTCH = 40.0                      # keep-out square, bed mm
BRANDING_SHIFT_X = -25.0          # clears the front-right notch

C_FINE = "#858585"
C_MAJOR = "#666666"
C_EDGE = "#666666"
C_ZONE_FILL = "#4a4a4a"
C_ZONE_LINE = "#2f2f2f"
C_BRAND_GRAY = "#7a7a7a"          # was #b3b3b3

W_FINE = 0.18
W_MAJOR = 0.50
W_EDGE = 0.70

# keep-out squares in svg space
ZONES = [
    (0.0, BED - NOTCH, NOTCH, BED),
    (BED - NOTCH, BED - NOTCH, BED, BED),
]

# ------------------------------------------------------- branding + its bbox

src = open("350_LDO_Texture_fixed.svg").read()
_s = src.index("<g", src.index('inkscape:label="branding_shifted_clear_of_notch"') - 40)
_e = src.index("</g></g></svg>")
branding = src[_s:_e + 4].replace("#b3b3b3", C_BRAND_GRAY)

_num = re.compile(r"-?\d*\.?\d+(?:[eE][-+]?\d+)?")


def path_bbox(d):
    """Conservative bbox: every on-curve and control point. Curves stay inside
    their control hull, so this never under-reports."""
    xs, ys = [], []
    cx = cy = sx = sy = 0.0
    for m in re.finditer(r"([MmLlHhVvCcSsQqTtAaZz])([^MmLlHhVvCcSsQqTtAaZz]*)", d):
        cmd, a = m.group(1), [float(v) for v in _num.findall(m.group(2))]
        rel, C = cmd.islower(), cmd.upper()
        if C == "Z":
            cx, cy = sx, sy
        elif C == "H":
            for v in a:
                cx = cx + v if rel else v
                xs.append(cx); ys.append(cy)
        elif C == "V":
            for v in a:
                cy = cy + v if rel else v
                xs.append(cx); ys.append(cy)
        elif C == "A":
            for k in range(0, len(a) - 6, 7):
                cx = cx + a[k + 5] if rel else a[k + 5]
                cy = cy + a[k + 6] if rel else a[k + 6]
                xs.append(cx); ys.append(cy)
        else:
            step = {"M": 2, "L": 2, "T": 2, "S": 4, "Q": 4, "C": 6}[C]
            first = True
            for k in range(0, len(a) - step + 1, step):
                seg, bx, by = a[k:k + step], cx, cy
                for p in range(0, step, 2):
                    xs.append(bx + seg[p] if rel else seg[p])
                    ys.append(by + seg[p + 1] if rel else seg[p + 1])
                cx, cy = xs[-1], ys[-1]
                if C == "M" and first:
                    sx, sy = cx, cy
                first = False
    return min(xs), min(ys), max(xs), max(ys)


def cluster_bbox(chunk):
    ts = chunk.rindex("<g", 0, chunk.index('aria-label="350x350mm"'))
    te = chunk.index("</g>", ts) + 4
    box = [1e9, 1e9, -1e9, -1e9]

    def add(b, tx=0.0, ty=0.0):
        box[0] = min(box[0], b[0] + tx); box[1] = min(box[1], b[1] + ty)
        box[2] = max(box[2], b[2] + tx); box[3] = max(box[3], b[3] + ty)

    for d in re.findall(r'\bd="([^"]+)"', chunk[ts:te]):
        add(path_bbox(d), -1.3164625, -6.0126301)     # size_text's own transform
    for d in re.findall(r'\bd="([^"]+)"', chunk[:ts] + chunk[te:]):
        add(path_bbox(d))
    return box


bx0, by0, bx1, by1 = cluster_bbox(branding)
bx0 += BRANDING_SHIFT_X
bx1 += BRANDING_SHIFT_X

# Pocket: the artwork plus a small margin, then snapped outward onto grid lines
# so it fills whole cells. Right and bottom already land on real boundaries (the
# notch edge and the bed edge). Snapping matters visually: a pocket that ends
# part way along a cell leaves the cut grid lines as short dangling stubs,
# whereas one that ends on a line makes clean T-junctions.
MARGIN = 2.0
POCKET = (math.floor((bx0 - MARGIN) / FINE_STEP) * FINE_STEP,
          math.floor((by0 - MARGIN) / FINE_STEP) * FINE_STEP,
          BED - NOTCH,
          BED)

# ------------------------------------------------------------- grid clipping


def clip(a, b, lo, hi):
    """Remove (lo, hi) from the span a..b."""
    if hi <= a or lo >= b:
        return [(a, b)]
    out = []
    if a < lo:
        out.append((a, lo))
    if hi < b:
        out.append((hi, b))
    return out


def grid_lines(step, skip_major):
    """Horizontal + vertical lines at `step`, clipped against the pocket."""
    px0, py0, px1, py1 = POCKET
    out = []
    n = int(round(BED / step))
    for i in range(n + 1):
        v = i * step
        if skip_major and abs(v / MAJOR_STEP - round(v / MAJOR_STEP)) < 1e-9:
            continue
        for a, b in (clip(0.0, BED, px0, px1) if py0 < v < py1 else [(0.0, BED)]):
            out.append("M %g,%g H %g" % (a, v, b))
        for a, b in (clip(0.0, BED, py0, py1) if px0 < v < px1 else [(0.0, BED)]):
            out.append("M %g,%g V %g" % (v, a, b))
    return out


def hatch(x0, y0, x1, y1, spacing=5.0):
    """45-degree fill lines clipped to the square, computed directly so no
    <pattern> or <clipPath> is needed (the bed texture rasteriser has neither)."""
    out = []
    w, h = x1 - x0, y1 - y0
    c = -h + spacing
    while c < w - 1e-9:
        # the line (x-x0) - (y-y0) = c, kept to where it is inside the square
        ua, ub = max(0.0, c), min(w, c + h)
        out.append("M %.3f,%.3f L %.3f,%.3f"
                   % (x0 + ua, y0 + ua - c, x0 + ub, y0 + ub - c))
        c += spacing
    return out


# ------------------------------------------------------------------- emit

def paths(ds, style):
    return "\n".join('    <path style="%s" d="%s" />' % (style, d) for d in ds)


fine = paths(grid_lines(FINE_STEP, skip_major=True),
             "fill:none;stroke:%s;stroke-width:%g" % (C_FINE, W_FINE))
major = paths(grid_lines(MAJOR_STEP, skip_major=False),
              "fill:none;stroke:%s;stroke-width:%g" % (C_MAJOR, W_MAJOR))

zones = []
for x0, y0, x1, y1 in ZONES:
    zones.append(
        '    <rect x="%g" y="%g" width="%g" height="%g" '
        'style="fill:%s;fill-opacity:0.42;stroke:%s;stroke-width:%g;stroke-opacity:0.9" />'
        % (x0, y0, x1 - x0, y1 - y0, C_ZONE_FILL, C_ZONE_LINE, W_EDGE))
    zones.append(paths(
        hatch(x0, y0, x1, y1),
        "fill:none;stroke:%s;stroke-width:0.35;stroke-opacity:0.5" % C_ZONE_LINE))

svg = '''<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<!-- 350x350 LDO x Voron bed texture, AWD edition.

     Regenerated by make_bed_svg.py. The grid is emitted programmatically and
     clipped against the branding's measured bounding box, so the pocket and
     the artwork cannot fall out of alignment.

     Dark squares are the AWD stepper-mount keep-out zones: bed X/Y 0-40 and
     X 310-350 / Y 0-40. The toolhead cannot enter them at any Z.
-->
<svg width="350mm" height="350mm" viewBox="0 0 350 350" version="1.1"
     xmlns="http://www.w3.org/2000/svg"
     xmlns:svg="http://www.w3.org/2000/svg"
     xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape"
     xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd">
  <g inkscape:label="grid_fine" inkscape:groupmode="layer" id="grid_fine">
{fine}
  </g>
  <g inkscape:label="grid_major" inkscape:groupmode="layer" id="grid_major">
{major}
  </g>
  <g inkscape:label="keepout_zones" inkscape:groupmode="layer" id="keepout_zones">
{zones}
  </g>
  <g inkscape:label="bed_edge" inkscape:groupmode="layer" id="bed_edge">
    <rect x="0" y="0" width="350" height="350"
          style="fill:none;stroke:{edge};stroke-width:{wedge}" />
  </g>
  <g inkscape:label="branding" inkscape:groupmode="layer" id="branding">
{brand}
  </g>
</svg>
'''.format(fine=fine, major=major, zones="\n".join(zones),
           edge=C_EDGE, wedge=W_EDGE, brand=branding)

open("350_LDO_AWD_Texture.svg", "w").write(svg)

print("branding cluster  x %.2f..%.2f   y %.2f..%.2f" % (bx0, bx1, by0, by1))
print("pocket            x %.2f..%.2f   y %.2f..%.2f" % (POCKET[0], POCKET[2], POCKET[1], POCKET[3]))
print("clearances        left %.2f  top %.2f  right %.2f"
      % (bx0 - POCKET[0], by0 - POCKET[1], POCKET[2] - bx1))
print("written 350_LDO_AWD_Texture.svg (%d bytes)" % len(svg))
