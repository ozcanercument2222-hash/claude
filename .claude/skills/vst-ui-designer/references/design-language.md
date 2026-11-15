# Design language for plugin UIs

Contents: 1 Colour · 2 Palettes · 3 Typography · 4 Spacing and sizes · 5 Depth and material · 6 Icons · 7 Motion · 8 Branding · 9 Dark vs light

A plugin lives in a dark studio, next to a DAW with dozens of other plugin windows. It must be readable at a glance from arm's length, calm during a three-hour session, and instantly recognisable. Those three needs drive nearly every rule below.

## 1. Colour

**Build from tiers, not from a rainbow.**

| Role | Count | Notes |
|---|---|---|
| Surfaces | 3-4 | Same hue, low chroma, small lightness steps: window background, card / module, inset well (displays, value boxes), raised control. Pure `#000` and `#fff` are too harsh; the eye fatigues and glow loses contrast against pure black. |
| Text | 3 tiers | Primary (L about 88-92 %), secondary (about 60 %), disabled / tertiary (about 40 %). Secondary must still pass 4.5:1. |
| Accent | 1 hue, 3 tints | `base` for active arcs and selected states, `hot` (lighter) for the value being dragged and peaks, `deep` (darker, desaturated) for the unfilled track and fills under curves. |
| Semantic | 2-3 | Warning / bypass (amber), clip / error (red), optional info. Keep them out of the accent hue family so they are never confused with "on". |
| Data | 2-4 | For multi-series displays (input / output / added). Vary lightness as well as hue so the series stay distinct in greyscale and for colour-blind users. |

Rules that save time:

- Pick **one** accent. A second chromatic colour is allowed only for a different *meaning* (e.g. odd vs even harmonics, input vs output), never for decoration.
- Large areas get low saturation; small active elements get high saturation. A big saturated panel is exhausting, a small saturated LED is excellent.
- Information must survive greyscale. If a state is communicated only by hue (red vs green), add shape, position or a label.
- Check numbers, not impressions: `python3 scripts/contrast_audit.py <header>`. Text 4.5:1, graphics and control outlines 3:1. Hairline panel borders are decorative and may be lower (about 1.3-1.6:1), but any outline that *defines* a clickable control should reach 3:1 or be backed by a fill change.
- Alpha is your friend for tints (`accent.withAlpha(0.12f)` for a track), but composite-check them: a 10 % tint on a dark surface can be invisible on a dim monitor.

## 2. Ready palettes

All values below were checked with `contrast_audit.py`. Text/dim ratios are against the `bg` surface.

| Name | bg | panel (inset) | edge | text | dim | accent | warn | Mood |
|---|---|---|---|---|---|---|---|---|
| **Graphite & Lime** (this repo's SCC) | `#111311` | `#0c0e0c` | `#2c312c` | `#dde2dd` 14:1 | `#8c948c` 6.0:1 | `#c8f72d` | `#ffa62b` | Modern, energetic, "character" tools |
| **Warm Studio** | `#1a1816` | `#12100e` | `#36312b` | `#ece6dc` 14:1 | `#9a9186` 5.7:1 | `#ff9f3d` | `#ffd23d` | Analogue, tape, tube, vocal tools |
| **Cold Lab** | `#0f1520` | `#0a0f18` | `#243044` | `#dbe4f0` 14:1 | `#8393a8` 5.8:1 | `#38d4ff` | `#ffb02e` | Precision: EQ, analysers, mastering |
| **Midnight Violet** | `#14121c` | `#0e0c14` | `#2b2640` | `#e4e0f0` 14:1 | `#8d87a6` 5.4:1 | `#b388ff` | `#ffb02e` | Reverb, delay, spatial, synths |
| **Clean Light** | `#eceae6` | `#f7f6f3` | `#cfccc5` | `#1b1d20` 14:1 | `#5d6168` 5.2:1 | `#2f6bff` | `#c25e00` | Utility, educational, bright rooms. Accent is 3.7:1: fine for graphics, not for small text. |
| **Vintage Cream** | `#d9d2c0` | `#cdc5b0` | `#a89f88` | `#2a2723` 9.9:1 | `#4d483c` 6.0:1 | `#a02a23` | `#7a5000` | Hardware emulations (black knobs `#1b1a18` on cream) |

Starting from one of these and adjusting the accent hue is far safer than inventing a palette from scratch. If you do generate your own, derive surfaces by holding hue and dropping chroma to 4-8 %, stepping lightness 4-6 points per tier.

Meter colours (all palettes): safe = accent-family green or the accent itself, then amber from about -6 dBFS, red from about -1 dBFS. Compressor gain-reduction meters use a *different* colour from level meters (usually the warm accent) so they are never mistaken for level.

## 3. Typography

- **One family, two weights** (regular, semibold). A second face is allowed only for the wordmark. More than that looks like a ransom note.
- **Embed the font** (see `juce-implementation.md`). System fonts differ per OS (this repo's README already notes Linux vs macOS vs Windows look different), and layout that fits one will clip on another. Use open-licence (OFL) fonts that ship with their licence file:
  - Inter, IBM Plex Sans, DM Sans, Manrope: neutral, excellent at small sizes.
  - IBM Plex Mono, JetBrains Mono: values and tables where fixed-width digits matter.
  - Barlow / Barlow Condensed, Roboto Condensed: compact technical look, good when labels are long and space is tight.
  - Space Grotesk, Sora: more personality for titles; keep to titles.
- **Scale at the 960 px design width** (scale proportionally for other widths):

| Use | Size | Style |
|---|---|---|
| Section label, knob label | 10.5-12 px | Semibold, UPPERCASE, tracking +6 to +10 %, secondary text colour |
| Value readout | 12-14 px | Regular or mono, primary text colour, **fixed-width slot** |
| Panel title / preset name | 14-16 px | Semibold |
| Hero readout (e.g. the main knob's value) | 26-36 px | Regular, accent or primary |
| Wordmark | free | The only place for expressive type |

- Nothing below 10 px at design size. If it does not fit at 10 px the layout is wrong, not the font.
- Tabular digits: JUCE does not expose OpenType `tnum`. Use a font whose digits are already tabular (IBM Plex, JetBrains Mono, Barlow, Roboto) or place values in fixed-width boxes and centre or right-align them, so "-9.9 dB" to "-10.1 dB" does not shift neighbours.
- Use real minus signs for negative numbers (`-` is fine, `−` U+2212 is nicer; check your font has it) and a thin space before the unit.
- Shrink-to-fit long text with `drawFittedText` (as `sccui::drawLabel` does), but fix the layout if it shrinks below 10 px.
- Truncate with an ellipsis (`...`) in preset names and file names rather than clipping.

## 4. Spacing and sizes

- **4 px base grid.** Gaps: 4 (tight, label to its control), 8 (within a group), 12-16 (between groups), 24 (between sections). Card inner padding 12-16.
- Keep gaps *consistent*: the single biggest tell of amateur layouts is gaps that are 10, 13 and 17 px for no reason.
- Align on shared edges and baselines. Align label baselines across a row, not label tops. Align knob centres on a row axis even if the labels differ in length.
- **Control sizes come in a small set** (design size):
  - Hero knob: 160-220 px. Primary knob: 72-96. Secondary: 44-60. Mini: 28-36.
  - Button height 28-32; toggle LED 8-10; segmented selector height 28-32.
  - Minimum hit target 24 x 24 even if the drawing is smaller.
- Corner radii: pick two (e.g. 4 for controls, 8 for cards) and use them everywhere.
- Stroke widths: hairline = 1 physical pixel, outline = 1-1.5 px, arcs = 4-8 % of knob diameter.
- Leave air. Plugins that fill every pixel look cheap; whitespace is what makes the hero element read as the hero.

## 5. Depth and material

Pick **one** material language and apply it everywhere:

- **Flat-modern** (FabFilter / Valhalla school): solid fills, 1 px borders, almost no gradients. Hierarchy by size, contrast and position. Cheapest, ages well, scales perfectly.
- **Soft-dimensional**: subtle vertical gradient on surfaces (2-4 % lightness), 1 px top highlight at 8-12 % white, inner shadow on wells. Light comes from the top, always. This is where the SCC design sits.
- **Hardware-faithful** (UAD / Arturia school): textured metal and paint, real bezels, shadows. Only worth it when the product *is* an emulation; budget the raster caching it needs.

Rules: at most two elevation levels (card and raised control). Wells (displays, value boxes) are darker than their surroundings, raised controls lighter. A drop shadow is a soft, low-alpha, offset-down blob, never a hard black one. Do not mix light directions.

Glow: reserve for what is live (the active arc, a lit LED, the audio trace). A 3-layer stroke (wide at 7 % alpha, medium at 18 %, core at 100 %) reads as glow without blur. Never glow static text or panel borders. Cache or limit it: each glowing stroke is three strokes per repaint.

Textures: a faint noise (1-3 % opacity) on large surfaces removes gradient banding and adds tactility. Generate once into an `Image` on resize; never per frame.

## 6. Icons

- Draw icons as `juce::Path` (arrows, chevrons, power, link, lock, play) or load SVG with `juce::Drawable::createFromSVG`. Never use emoji or glyph characters, which vary per OS.
- Design on a 16 / 20 / 24 px grid, 1.5-2 px strokes, round caps, one visual weight across the set.
- Pair an icon with a label wherever space allows. Unlabeled icons are acceptable only for universal symbols (bypass power, prev / next chevrons, A/B).

## 7. Motion

Motion in a plugin is feedback, not entertainment.

- Value changes and hover: 80-150 ms ease-out. Press: instant. Nothing in the UI chrome should take longer than 250 ms.
- Meters and analysers have *ballistics* (see `controls.md`); those are audio-domain, not UI easing.
- Screen / mode transitions (e.g. open Designer): 150-220 ms crossfade or slide, interruptible, never blocking input.
- Do not animate anything idle. Constant ambient motion burns CPU across 50 plugin instances and distracts from the music. If audio is silent, displays should settle and the timer should be able to stop.
- Respect a "reduce motion" option if you add one: snap values, keep meters.

## 8. Branding

- Wordmark top-left, modest in size (it is a signature, not the hero). Product name, then a quiet descriptor, then version in tertiary colour.
- One signature element makes the plugin recognisable in a thumbnail: a distinctive knob, a characteristic display style, a colour pairing. Decide it deliberately.
- No marketing clutter in the editor (no "Buy now" banners, no social icons). A small, tidy "About / settings" popup carries version, licence and links.
- Window chrome: resize handle bottom-right, clearly visible; remembered size; sensible default (about 900-1100 px wide for a full-featured effect, 500-700 for a utility).

## 9. Dark vs light

Default to dark: it matches DAW chrome, is easier in dim rooms, and makes glow and meters pop. Offer light only if the audience wants it (education, bright broadcast rooms) or as a user theme. If both are offered, drive them from the same token header so a switch is one swap, not two UIs.
