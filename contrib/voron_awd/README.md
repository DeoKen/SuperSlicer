# Voron 2.4 350 + LDO CNC AWD — bed assets

Bed texture and bed model for a 350 Voron whose AWD stepper mounts sit at
gantry level in the two front corners. The toolhead contacts a mount 40 mm out
on both axes, so bed X/Y 0–40 and X 310–350 / Y 0–40 are unreachable at any Z.

| file | use |
| --- | --- |
| `350_LDO_AWD_Texture.svg` | Printer Settings → General → Bed shape → **Texture** |
| `voron350_awd_bed.stl` | Printer Settings → General → Bed shape → **Model** |
| `make_bed_svg.py` | regenerates the texture |

Leave **Size** as a plain rectangular 350 × 350. A concave `bed_shape` makes the
build volume `Type::Custom`, which switches the outside-of-bed test and arrange
onto per-vertex geometry for the whole session and makes the plater noticeably
slower. The notches are shown by the texture and enforced at slice time by
**Print Settings → Output options → Avoid front corner keep-out zones**; the
bed polygon itself does not need to know about them.

## Regenerating the texture

```
python3 make_bed_svg.py        # reads 350_LDO_Texture_fixed.svg, writes 350_LDO_AWD_Texture.svg
```

The stock LDO texture carves the pocket behind the branding by hand-truncating
individual grid lines, so the pocket stops matching the artwork the moment the
artwork moves — which is exactly what happened when the branding was shifted
25 mm left to clear the front-right notch. Here the grid is emitted
programmatically and clipped against the artwork's measured bounding box, then
snapped outward onto grid lines, so the two cannot drift apart.

SVG y runs downward and the texture is mapped with the top of the image at the
back of the bed, so bed Y = 350 − svg_y and the front corners are along the
bottom edge of the image.

Only `<path>` and `<rect>` are used. The bed-texture rasteriser has no
`<text>`, `<pattern>`, `<mask>` or `<clipPath>`, so the keep-out hatching is
emitted as explicit line segments and the size caption stays as outlines.
