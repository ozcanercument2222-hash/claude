---
name: vst-ui-designer
description: Designs and builds professional, release-quality user interfaces for audio plugins (VST3 / AU / CLAP / AAX / standalone), JUCE first, also iPlug2, VSTGUI and WebView UIs. Covers visual identity, layout and hierarchy, knobs / sliders / meters / spectrum and EQ displays, LookAndFeel code, resizable HiDPI scaling, animation, UI performance, and screenshot-based design review. Use this skill whenever the user asks for a plugin GUI or "arayüz", wants a redesign, skin, theme or polish, says it should look professional / premium / modern / like FabFilter, UAD or Soundtoys, asks for a knob, meter, visualizer or editor layout, or wants anything visual fixed in a PluginEditor, LookAndFeel or juce::Component, even if the word "design" never appears. Also use it to critique or audit an existing plugin UI.
---

# VST UI Designer

The goal is an interface that looks and feels like it shipped from a serious audio company. What separates professional plugin UIs from amateur ones is rarely decoration. It is hierarchy, restraint, consistency, controls that respond exactly the way a producer expects, and care in small details such as alignment, units and meter ballistics. Work as a product designer who also understands DSP and JUCE, and judge your own output the way a skeptical plugin reviewer would.

Reply to the user in the language they write in (this project's owner writes Turkish). Text *inside* the plugin UI stays English unless they ask otherwise, because that is the industry convention and keeps parameter names identical across DAWs.

## Workflow

Follow these steps in order. Skipping the early ones is the usual reason a redesign comes out generic.

### 1. Recon: read before designing

- Identify the framework (`grep -ril "juce\|iplug\|vstgui" .`) and version. Check `CMakeLists.txt` for the JUCE tag, since APIs differ (JUCE 8 uses `juce::FontOptions`).
- Find the editor, existing LookAndFeel / theme files, colour and font constants, and the parameter layout (APVTS `createParameterLayout`). Every control on screen must map to a real parameter; know each one's range, unit and default.
- Look for a screenshot harness (this repo's `tests/plugin_test.cpp` writes PNGs with `createComponentSnapshot`). If none exists, add one from `assets/ScreenshotHarness.template.cpp` so you can see what you build.
- View current screenshots. Preserve what already works (brand colour, signature widgets, user muscle memory) unless the user asked for a full rebrand. A redesign that throws away a recognisable identity is usually a regression.
- Check what the plugin *does* for the user: which 3 controls get touched most, which are set-and-forget, which display tells them it is working.

### 2. Brief: decide before drawing

Write a short design brief (6-10 lines) and show it to the user. Only stop to ask if a choice is truly ambiguous; otherwise state your assumption and proceed, because the user can redirect cheaply once they see it.

- **Concept**: one sentence for the personality (e.g. "precise studio instrument, warm and dark").
- **Archetype**: pick from `references/archetypes.md` (dynamics, EQ, reverb/delay, saturation, synth, utility) for layout and signature widgets.
- **Hero element**: the one thing the eye lands on first (a big knob, a response graph, a meter).
- **Primary / secondary / tertiary controls**: who gets the large size, who gets the small one, who hides behind an "advanced" view.
- **Palette, type, material**: see `references/design-language.md`.
- **Sizes**: design size, minimum and maximum, aspect ratio.

### 3. Design system first

Create or update one theme header (start from `assets/Theme.template.h`) holding colour tokens, type scale, spacing, radii, stroke widths and shared drawing helpers. Then build components (knob, button, selector, meter, display) on those tokens, and only then compose screens. Consistency across hundreds of small decisions is most of what people read as "professional", and a token file is the only reliable way to get it.

### 4. Layout

- Design on a fixed logical canvas (this repo: 960 x 736) and scale it, rather than re-flowing, unless the plugin needs genuinely different layouts per size. See `references/juce-implementation.md`.
- Use a 4 px base grid; paddings 8 / 12 / 16 / 24. Group by function, let proximity do the grouping, and add borders or cards only where proximity is not enough.
- Follow the audio signal flow (input -> processing -> output, left to right or top to bottom).
- Aim for 5-9 primary controls visible. Put deep editing on a secondary screen or collapsible section (progressive disclosure), as the Character Designer does here.

### 5. Build controls and displays

Use `references/controls.md` for exact anatomy, sizes, interaction rules, value formatting and meter / spectrum behaviour. Use `references/juce-implementation.md` for the code patterns (LookAndFeel, attachments, caching, thread-safe data from the audio thread, fonts). Reusable starting points live in `assets/`.

### 6. Verify visually. This step is mandatory

You cannot judge a UI from code. Render it, then look at it.

1. Build and run the screenshot harness (default size, minimum size, maximum size, and each distinct screen or mode).
2. Open the PNGs with the Read tool. Then zoom into regions with `python3 scripts/zoom_review.py <png> --grid 2x2` (or `--crop x y w h --scale 2`), because full-frame views hide misalignment, clipped text and uneven gaps.
3. Run `python3 scripts/contrast_audit.py <theme-header-or-hex-pairs>` to check text and graphic contrast numerically.
4. Walk through `references/review-checklist.md` honestly and fix what fails. Do at least one critique-and-fix round before presenting. The first render is rarely the best one.

### 7. Report

Tell the user what you changed and why (decisions, not a diff dump), show before / after screenshots when they exist, and say plainly what you could not verify. In a sandbox you usually cannot test inside a real DAW, so list what to check there: HiDPI on Windows, resizing in Ableton / FL Studio / Logic, automation display, the plugin window opened twice.

## Principles (and why)

1. **One hero, three tiers.** If everything is loud, nothing is. Squint at the screenshot: the hierarchy should survive blur. Size, contrast and position (not colour alone) establish rank.
2. **Restraint.** One accent hue plus semantic colours (warning, clip, bypass), at most three surface tones, glow only on things that are live or active. Decoration that carries no information (screws, fake scratches, rainbow gradients) reads as amateur and costs CPU.
3. **Every value is visible, with units.** "37" is not a value; "37 %" or "-12.4 dB" is. Readouts use fixed-width slots so they do not jitter while dragging.
4. **Controls feel right.** Vertical drag, Shift for fine, double-click to reset, scroll wheel, click-to-type. Producers judge quality in the first ten seconds of touching a knob.
5. **States are distinct.** Normal, hover, pressed, focused, disabled, bypassed, automated, modulated each look different, and none relies on colour alone.
6. **Truthful displays.** Meters and graphs use real scales (dB, log frequency), real ballistics and real data. A decorative animation that implies processing that is not happening destroys trust.
7. **Legible everywhere.** Text contrast of at least 4.5:1 (graphics 3:1), body text no smaller than 11 px at design size, no text over busy gradients. Embed your fonts, because system fonts differ across macOS, Windows and Linux and break the layout.
8. **Crisp at any scale.** Draw vectors, not bitmaps, so it is sharp at 1x, 1.5x and 2x. Use physical-pixel-aware hairlines. Cache raster layers at the physical resolution.
9. **The UI never hurts the audio.** No locks or allocation shared with `processBlock`, no per-frame work in `paint()` that can be cached, no timers running while the editor is hidden. A beautiful UI that causes dropouts gets uninstalled.
10. **Polish is the product.** Pixel alignment, consistent radii, matching label text and parameter names, graceful behaviour at the minimum size. These are what a reviewer notices after the first impression fades.

## Common anti-patterns

- Neon glow on every element, or a glow whose colour does not match its control.
- Heavy skeuomorphism (screws, wood, rust) with no functional purpose, or a faux-vintage look painted over a modern workflow.
- Inconsistent knob styles or sizes across screens; three different button styles.
- Tiny grey-on-grey labels. Value text that changes width as it changes number.
- Centre-aligning everything; labels floating away from the control they describe.
- Stretching a bitmap background, or hard-coded pixel sizes that break when resized.
- A meter that jumps (no ballistics), a spectrum with no smoothing, an unlabeled dB scale.
- Animating on a timer forever, even when the window is closed or occluded.
- Copying a competitor's trade dress. Borrow principles (graph-first EQ, big readouts), never their exact look.

## Files in this skill

| File | Read it when |
|---|---|
| `references/design-language.md` | choosing palette, typography, depth, spacing, motion, icons, branding |
| `references/controls.md` | building or reviewing knobs, sliders, buttons, selectors, meters, spectrum / EQ graphs, readouts |
| `references/juce-implementation.md` | writing JUCE code: LookAndFeel, scaling, attachments, caching, threading, fonts, screenshots |
| `references/archetypes.md` | deciding layout and signature elements for a given plugin type |
| `references/review-checklist.md` | the critique pass in step 6 |
| `assets/Theme.template.h` | starting a theme / design-token header |
| `assets/ScreenshotHarness.template.cpp` | no screenshot harness exists yet |
| `scripts/contrast_audit.py` | checking colour contrast (hex pairs, or colours parsed from a C++ header) |
| `scripts/zoom_review.py` | cropping, tiling and magnifying screenshots for close inspection |
