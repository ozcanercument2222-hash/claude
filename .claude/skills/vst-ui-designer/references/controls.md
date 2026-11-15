# Controls and displays: anatomy, behaviour, numbers

Contents: 1 Rotary knob · 2 Slider / fader · 3 Buttons, toggles, selectors · 4 Preset bar · 5 Value readouts · 6 Level and gain-reduction meters · 7 Spectrum analyser · 8 EQ / response graphs · 9 Waveform and scope · 10 States · 11 Tooltips, context menus, accessibility

Sizes below are at the design canvas (about 960 px wide); scale proportionally.

## 1. Rotary knob

**Anatomy**: label (above or below, consistent across the plugin) -> knob -> value readout. Inside the knob: unfilled track, value arc, pointer / indicator, optional body. Keep the *value arc* as the primary signal; a tiny pointer on a big dark disc is hard to read at a glance.

- Sweep 270 degrees: from 7:30 to 4:30. In JUCE (angles clockwise from 12 o'clock) that is `setRotaryParameters(1.25f * pi, 2.75f * pi, true)`. JUCE's default is 1.2 pi to 2.8 pi; set yours explicitly so the 7:30 / 4:30 convention is exact.
- **Bipolar parameters** (pan, tone, gain trim, -x to +x): draw the arc from the 12 o'clock centre to the value, and put a centre detent tick. Unipolar: arc from the start.
- Arc thickness 4-8 % of diameter, round caps. Track = accent at 10-14 % alpha or a surface tone; value arc = accent. While dragging, shift the arc to the `hot` tint and brighten the readout; this confirms "you are holding this one".
- Pointer: a line from 45 % to 85 % of the radius, or a small dot on the arc. Skip tick marks unless the scale is meaningful (stepped values, dB marks). A ring of 40 ticks on every knob is noise.
- Size tiers: hero 160-220, primary 72-96, secondary 44-60, mini 28-36 px.
- Value: always shown. Either permanently below the knob (preferred for primary controls) or, on dense panels, on hover / drag in place of the label, or via JUCE's `setPopupDisplayEnabled`.
- Non-linear ranges (frequency, time, threshold in dB): use `NormalisableRange` with a skew so the useful region occupies most of the travel; the arc position should match how the parameter *sounds*.

**Interaction** (JUCE gives some of this for free, the rest you add in a `Slider` subclass; see `assets/Theme.template.h` for `ProKnob`):

| Gesture | Behaviour |
|---|---|
| Drag up / down (right / up increases) | `RotaryVerticalDrag`, about 250-300 px for the full range; `setVelocityBasedMode(false)` so the same distance always gives the same change |
| Shift + drag | Fine adjustment, about 5-10x slower. JUCE does **not** do this natively for a single-value slider; implement it with incremental deltas (a naive sensitivity change mid-drag makes the value jump) |
| Double-click | Reset to default (`setDoubleClickReturnValue(true, defaultValue)`). Alt-click is the JUCE default alternative via the third argument |
| Mouse wheel | 1-2 % per notch, Shift for finer; `setScrollWheelEnabled(true)` |
| Click the value | In-place text entry, select-all, Enter commits, Esc cancels. Parse what you display (see 5) |
| Right-click | Prefer the **host's** parameter menu if available (`getHostContext()->getContextMenuForParameter`), so users get the DAW's automation / MIDI-learn items; fall back to your own Reset / Enter value |
| Hover | Subtle ring or label highlight; optional tooltip |
| Keyboard | Focusable; arrow keys step (JUCE Slider handles this when it has focus) |

Hide the cursor, or let it wrap, while dragging so the user never hits the screen edge (`MouseInputSource::enableUnboundedMouseMovement`; JUCE sliders do this during rotary drags).

## 2. Slider / fader

- Vertical faders for gain / level (matches mixer mental model), horizontal sliders for pan, mix and compact rows.
- Track 4-6 px, thumb 14-20 px wide with a visible grip line; value fill from the 0 position (bipolar) or the minimum.
- Same gestures as knobs (fine drag, double-click reset, wheel, type-in). Fader scale labels (dB marks) when the scale is non-obvious.
- Long, thin hit area: hit target at least 24 px wide even if drawn 6 px.

## 3. Buttons, toggles, selectors

- **Toggle** (on / off): ON must be unmistakable at a glance, not a 5 % colour shift. Use a filled accent background or a lit LED plus accent text; OFF is an outline with secondary text. Label does not change with state (don't flip "Mono" to "Stereo"); state is shown by fill / LED.
- **Momentary / action** (Save, Match): outline style, pressed = darker fill, no latching look.
- **Segmented selector** for 2-5 mutually exclusive options with short labels (mode, slope, ratio steps). Equal widths, the selected segment filled or outlined in accent, all options visible so the user sees what exists. For more than 5, use a dropdown.
- **Dropdown**: show the current value, a chevron, and open a popup menu aligned under it; never replace it with a left / right arrow stepper unless the list is a "preset-like" sequence (see 4).
- Height 28-32 px, horizontal padding 12-16 px, radius from your two-radius set. Min width 48 px.
- **Bypass / power**: always present, top area, same place on every screen. Wire it to the host's bypass parameter (`AudioProcessor::getBypassParameter`) so DAW bypass and plugin bypass stay in sync. When bypassed, dim the processing UI (about 40 % alpha on displays and arcs) but keep it readable and operable.
- Disabled controls remain visible at 35-40 % opacity and ignore hover; hide a control only when its whole group is irrelevant (mode-dependent sections).

## 4. Preset bar

Prev / next chevrons + name field (opens a menu / browser) + optional Save / Save As / A-B. Show a dirty marker (a dot or `*`) once a parameter differs from the loaded preset. Keep factory and user presets separate in the menu, allow rename / delete of user presets, and remember the last-used preset in the session state. Never open a modal dialog that blocks the host: use `launchAsync` for file choosers and alert windows.

## 5. Value readouts

Fixed-width slot, units attached, consistent precision, centred under or beside the control.

| Parameter | Format | Examples |
|---|---|---|
| Frequency | < 100 Hz: 1 decimal; 100-999: integer; 1-9.99 kHz: 2 decimals; >= 10 kHz: 1 decimal | `47.5 Hz`, `245 Hz`, `1.25 kHz`, `12.5 kHz` |
| Gain / level | signed, 1 decimal, `dB`; `-inf` for silence | `+3.5 dB`, `-12.0 dB`, `-inf dB` |
| Time | < 100 ms: 1 decimal; 100-999 ms: integer; >= 1 s: 2 decimals as seconds | `12.5 ms`, `250 ms`, `1.20 s` |
| Ratio | 1 decimal, `:1`; top of range may read `LIMIT` or the infinity sign | `4.0:1`, `LIMIT` |
| Percent | integer, space before `%` (1 decimal only if the control is fine-grained) | `75 %` |
| Pan | `L 30`, `C`, `R 30` | |
| Q / slope | 2 decimals / dB per octave | `0.71`, `24 dB/oct` |
| Pitch | `+7 st`, `-12 ct` | |
| Tempo-synced time | note value, with `T` (triplet) and `.` (dotted) | `1/8`, `1/16T`, `1/4.` |

Text entry must round-trip: accept `1.2k`, `1200`, `1.2 kHz`, `-3`, `-3 dB`. In JUCE set both `textFromValueFunction` and `valueFromTextFunction` on the slider (or `withStringFromValueFunction` / `withValueFromStringFunction` on the parameter attributes so the *host* shows the same text for automation).

## 6. Level and gain-reduction meters

Honest ballistics are what makes a meter feel professional.

- **Peak meter**: instant attack, constant-rate fall (about 20 dB/s), peak-hold line for 1.5-2 s, a **latching clip indicator** at >= 0 dBFS that clears on click. Compute the fall from elapsed time (`dt`), not from frame count, so it behaves the same at 30 and 60 Hz.
- **RMS / VU style**: about 300 ms integration, no peak hold or a thin one.
- **Gain reduction (GR) meter**: grows from the top (vertical) or from the right edge toward the left (horizontal), usually 0 to -20 or -30 dB, in the *warm* colour, fast attack and release to show the compressor's real behaviour. Show a numeric GR readout.
- Scale: dB, log-like mapping, range -60 (or -48) to 0, labelled ticks at 0, -6, -12, -24, -48 in 9-10 px secondary text. Always label the unit once.
- Segmented meters: 2 px gaps, segment height 3-4 px, lit segments in accent / green, amber from about -6 dBFS, red from about -1 dBFS; unlit segments at 8-10 % alpha so the structure stays visible.
- Data path: audio thread writes `std::atomic<float>` (max abs per block, or per-channel); UI timer reads and applies ballistics. No locks, no allocation in `processBlock`.
- Stereo: two bars with a small gap; label L / R only if there is room. Show input *and* output when the processing changes level.
- Update at 30 Hz is enough for bars; 60 Hz for smooth analysers.

## 7. Spectrum analyser

- FFT 4096 (8192 for detail), Hann window, 50-75 % overlap. Correct for window gain so levels read true. Run the FFT on the UI thread (or a worker) from a lock-free FIFO of samples, not in `processBlock`.
- Axes: log frequency 20 Hz - 20 kHz (`x = log(f / 20) / log(1000)`), dB range 60-90 dB selectable, gridlines at 20, 50, 100, 200, 500, 1k, 2k, 5k, 10k, 20k and every 12 dB. Draw grid and labels into a cached `Image` once per resize.
- Smoothing: temporal (fast attack, release 50-200 ms; per-bin exponential) and optional 1/3 or 1/6 octave frequency smoothing. Without smoothing the trace looks like noise, with too much it lies.
- Optional +3 to +4.5 dB/octave tilt around 1 kHz so pink noise reads flat, closer to how people hear music.
- Rendering: decimate to at most one point per 1-2 px, build one `Path` (use `quadraticTo` for smooth curves), fill under the curve with a 15-30 % alpha vertical gradient, stroke 1.5 px with the glow helper if the style uses glow. Reuse the `Path` object; `path.clear()` keeps capacity.
- Multi-series (in / out / added): series differ in lightness as well as hue; input is a quiet filled area, output a bright line, "added" in the second semantic colour. Include a small legend.
- Offer freeze / hold and a range selector if the plugin is analysis-focused.

## 8. EQ and response graphs

- The graph **is** the control: give it 55-70 % of the window in an EQ.
- Nodes: drawn radius 6-8 px, hit radius at least 12 px. Drag X = frequency (log), Y = gain. Scroll wheel or pinch = Q. Double-click empty space = add band, double-click node = reset / remove, Shift = fine, Alt = constrain axis (state your modifier convention in a tooltip).
- Draw the **real** combined magnitude response computed from the filter coefficients, not a drawing approximation. Per-band curves faint, combined curve bold; selected band highlighted.
- Show a live readout near the dragged node (`1.25 kHz  +3.5 dB  Q 0.71`).
- Colour per band from a data palette that stays distinguishable; the same colour must appear on the band's knobs and header.
- Transfer / saturation curves (compressors, shapers): draw the unity diagonal as a dashed reference and a moving dot at the current input level.

## 9. Waveform and scope

- Pre-compute min / max per pixel column from a ring buffer; draw a filled envelope rather than thousands of line segments.
- Scrolling displays move at a constant, labelled time scale; freeze when audio stops rather than animating nothing.
- A compressor scope: input envelope (filled, quiet), gain-reduction trace (accent, on top), threshold line (dashed). Keep the axis implicit but never misleading.

## 10. States

Every interactive element needs each of these to look different from the others, and none may rely on colour alone.

| State | Treatment |
|---|---|
| Default | resting style |
| Hover | +4-8 % lightness or ring, 100 ms ease |
| Pressed / dragging | `hot` accent, readout emphasised |
| Focused (keyboard) | 1.5-2 px outline in accent, distinct from selected |
| Selected / on | filled accent or lit LED |
| Disabled | 35-40 % alpha, no hover |
| Bypassed | processing UI dimmed about 40 %, bypass control in warn colour |
| Automated by host | value arc moves smoothly; optionally a small marker if the host reports automation |
| Mismatch / warning | warn colour plus icon or text, never colour alone |

## 11. Tooltips, context menus, accessibility

- Tooltips after 600-800 ms: parameter name, one-line effect description, range or default. One `TooltipWindow` per editor. Do not duplicate what the label already says.
- Menus: native-looking `PopupMenu` styled via LookAndFeel so they match.
- Contrast 4.5:1 text, 3:1 graphics. Provide `setTitle` / `setDescription` on key components so screen readers can name them (JUCE 7+ accessibility). Enable keyboard focus on controls.
- Do not rely on hover for essential information (touch screens, tablets).
