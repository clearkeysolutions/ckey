# CKey Design System — Profile 0 (draft 0.1)

Like QR codes, a CKey can be branded: colours, materials, mark shapes and a logo. The rule that keeps it scannable is simple: **styling changes how a symbol is painted, never where a reader samples.** Everything here applies to Profile 0 (Format 0.27). Profiles 1–3 render as before.

## Fixed in every style

These elements are what the reader measures:

- **Frame:** the rounded square, stroke 0.65.
- **Finders:** the four corner arcs and the top-left split.
- **Keys:** the Grid, Ring and Ray Keys at the version's pitch.
- **Positions:** every site's position.
- **Quiet margin:** the one-unit margin outside the frame.

Each element can still take any colour or material below.

## Free within checked limits

| Element | Options | Why it still scans |
|---|---|---|
| Payload modules | rounded square (default), square, circle, diamond | Each shape covers the module's centre, where the reader samples, and stays inside its cell |
| Ring marks | joined arcs (default) or separate dots | The reader samples dot centres; joining is visual only |
| Dial and key rays | round or flat ends | The reader samples along the ray, never at its tips |
| Ink | flat, or a metallic sheen (ink → shade → ink across the symbol) | Every ink colour is checked against every background colour |
| Background | flat, or a radial vignette (centre → edge) | Checked the same way |
| Accents | up to 4 colours for a share of payload marks | Each accent must pass the same contrast test; keys, frame and finders always use the main ink |
| Materials | bevel (embossed highlight), depth (drop shadow), grain (background texture) | Bounded strengths; stricter limits for dark-on-light |
| Logo | any image (PNG, JPEG, SVG) in the centre, with an optional plate | The empty centre costs nothing; a larger logo is budgeted against error correction |

## The checks (`ckey_check_scalable_style`)

Under the default **strict** policy, the renderers refuse any style that fails, and the check names the reason. These rules are a conservative pre-filter. They do not certify a style; see [Policies and certification](#policies-and-certification).

1. **Contrast:**
   - **Pairs:** every ink colour (ink, shade, each accent in use) against every background colour (centre and edge) needs a WCAG contrast ratio of at least **3:1**.
   - **One polarity:** inks must be all lighter or all darker than the background. Either polarity is allowed.
2. **Effects:**
   - **Range:** bevel, depth and grain are each 0–1.
   - **Dark-on-light:** depth is limited to 0.4 and bevel to 0.5, because shadows darken the gaps and highlights lighten the marks.
3. **Logo:**
   - **Free size:** within the empty centre (radius about 3.2 units, roughly 22% of the symbol's width), a logo needs no plate and hides nothing.
   - **Ring Key:** never beyond `ckey_scalable_logo_max_radius(v)`, which keeps the outermost ring and the Ring Key fully visible.
   - **Plate:** beyond the empty centre the logo needs a plate.
   - **Error budget:** the marks it hides may use at most **50%** of the worst Reed–Solomon block's correction capacity (`ckey_scalable_logo_load`). The other half stays available for real damage.

**Known limit on logo size.** Profile 0 scatters each byte's 8 bits across the whole symbol, which protects against stripes and smudges. The cost is that a logo covering one ring corrupts roughly one byte per hidden mark, not one per eight. So today a logo can fill the empty centre, plus a little at level H, but not much more. A future format could place each byte's bits in neighbouring sites, as QR does, and roughly triple the allowed logo size. That would be a wire-format change.

## Policies and certification

`ckey_style.policy` (ABI 4) selects one of three policies (specification §4):

| Policy | Renderers | `ckey_certify_scalable_style` |
|---|---|---|
| `CKEY_POLICY_STRICT` (default) | refuse a style that fails the checks above | `CKEY_OK` only if certified |
| `CKEY_POLICY_ADAPTIVE` | apply fixed repairs, then the strict checks | repairs, then strips materials if the modelled scan still fails; returns the repaired style and every change (`CKEY_ADJUST_*`) |
| `CKEY_POLICY_OVERRIDE` | draw the style as given | always `CKEY_OK`; `certified` says whether it would scan |

The adaptive repairs are:

- the ink is pushed to black or white;
- if no ink can work, the symbol gets its own background;
- accents that fail are dropped;
- depth is limited on dark-on-light styles;
- a logo is plated, or shrunk until it fits its error budget.

Values a renderer cannot draw, such as an unknown shape or NaN, are refused under every policy.

**Certification is a modelled scan, not a contrast ratio:**

1. The symbol is rendered with the native raster.
2. It is composited over every backdrop.
3. The scan model degrades it: about 8 pixels per module, Gaussian defocus 0.9 px, sensor noise 3%, and 20% glare compression. Each setting can be changed in `ckey_scan_model`.
4. The real decoder decodes it on 3 seeded trials per backdrop.
5. The style is `certified = 1` only if every trial decodes exactly and the rules pass.

The result also reports, for information:

- the WCAG-style minimum contrast;
- the luma separation between mark and gap pixels after compositing (5th/95th percentile);
- the logo load.

**Transparent output:** the renderer cannot see what is behind a transparent symbol. Certification therefore needs a `ckey_backdrop`, given as solid colours and/or the host's own pixels. Without one it reports "background unknown" and `certified = 0`. A host image's darkest and lightest pixels also join the contrast rules, so a busy photo is not certified by its average.

The model is a model. Real-camera captures remain the evidence (specification §11.6). The logo image's own pixels are not modelled, only the marks it hides.

## Verification

- **Geometry, in the test suite:** all 16 mark-shape combinations decode from rasterized renders at v2 and v20. At every level, the largest logo the checker allows decodes, the hidden marks corrected by Reed–Solomon. The checker refuses low contrast, mixed polarity, and too-strong depth on light backgrounds.
- **Materials, in a browser:** the concept look rendered by Chromium decodes exactly at v6, v11 and v20, sharp and blurred, with 1–5 bytes corrected. That look is the champagne sheen, navy vignette, sage and grey accents, bevel 0.6, depth 0.5, grain 0.4 and a PNG logo on a plate. The CKey Renderer's read check runs the same decoder on its own WebKit raster for every design.

## Using it

- **CKey Renderer:** Profile 0 → **Design**: modules, rings, rays, metal sheen, vignette, accents, bevel, depth, grain, logo, and a "Concept preset". The status line shows the style check.
- **CLI:** `ckey encode-scalable --utf8 "…" --preset concept --modules circle --logo logo.png --out symbol.svg`
- **C ABI:** `ckey_style_default`, `ckey_style_preset_fill`, `ckey_check_scalable_style`, `ckey_certify_scalable_style`, `ckey_scan_model_default`, `ckey_render_scalable_styled_svg`, `ckey_scalable_logo_max_radius`, `ckey_scalable_logo_load`.
