# VEX Stutter – Architecture plan

Based on `VEX_STUTTER_Technical_Specification.txt` (v1.0) and the UI mockup. This plan follows section 23 of the
specification. Each phase is implemented on its own and keeps a working build. Every phase ends with automated
tests and a test-report entry (`docs/TEST_REPORT.md`).

## 1. Build and formats

- JUCE 8.0.6, CMake ≥ 3.22, C++17.
- Formats: VST3 first, plus Standalone for quick testing. AU only after the VST3 has been validated
  (`-DVEX_BUILD_AU=ON`, macOS).
- JUCE source, in this order:
  1. `-DVEX_JUCE_PATH=/path/JUCE`,
  2. a `./JUCE` checkout,
  3. download of the pinned tag.
- Tests: `-DVEX_BUILD_TESTS=ON`.
  - `VEXStutter_Tests` builds the plug-in sources into a console app.
  - With `--vst3 <path>` it also loads the built VST3 the way a host does.
  - From Phase 2 on, the DSP modules are JUCE-free headers, tested by a separate engine test. That test can
    also be built with sanitizers.

## 2. Modules (specification section 2)

| Module | Phase | Thread | Notes |
|---|---|---|---|
| `VEXStutterAudioProcessor` | 1 | audio + message | Entry point. Owns parameters, state and all DSP. Contains no drawing code. |
| `VEXStutterAudioProcessorEditor` | 1 → 7 | message only | Talks to the processor only through APVTS attachments and lock-free snapshots. |
| `Parameters/` (IDs, layout, divisions) | 1 | – | Stable IDs and ranges. The division table is JUCE-free and shared with the DSP. |
| `DSP/CaptureBuffer` | 2 | audio writes, UI reads a snapshot | Preallocated rolling stereo buffer, 8 s at the session sample rate. Single writer. The UI never touches it directly. |
| `DSP/WaveformSnapshot` | 2 | audio → UI | Peak pairs per pixel column, published lock-free. The UI polls at about 30 Hz. |
| `DSP/TimingEngine` | 3 | audio | Reads tempo, transport and PPQ from the playhead, validated. Falls back to free time and 120 BPM. |
| `DSP/StutterEngine` (slice reader + scheduler) | 3 | audio | Fractional-position reads, every index clamped. Crossfade length is never more than half the slice. |
| `DSP/ModeEngine` | 4 | audio | Stutter, Repeat, Gate and Glitch. Glitch randomness uses a deterministic seed. |
| `DSP/EnvelopeEngine` | 3 / 5 | audio | Edge fades, Shape curves, Smoothness. A minimal protective fade is always applied. |
| `DSP/FXChain` | 6 | audio | Wet-only delay (feedback capped) and reverb. Tails preserved; nothing is fed back into the capture buffer. |
| `PresetManager` | 8 | message | Versioned files that are validated before they are applied. |

Threading rules for every phase:

- No allocation, lock, file I/O or UI call in `processBlock()`.
- Every buffer is allocated in `prepareToPlay()`.
- Ownership of each shared buffer is documented in the header of the module that owns it.

## 3. Parameters (Phase 1; IDs are permanent once a build is released)

| ID | Name | Range / choices | Default | Connected in |
|---|---|---|---|---|
| `bypass` | Bypass | off / on (host bypass parameter) | off | 3 (click-free) |
| `mode` | Mode | Stutter, Repeat, Gate, Glitch | Stutter | 4 |
| `amount` | Amount | 0–100 % | 75 % | 3 |
| `sync` | Sync | free / host | host | 3 |
| `rate` | Rate | 15 musical divisions, longest first: 1/2D, 1/2, 1/4D, 1/2T, 1/4, 1/8D, 1/4T, 1/8, 1/16D, 1/8T, 1/16, 1/32D, 1/16T, 1/32, 1/32T | 1/8 | 3 |
| `rate_ms` | Rate (Free) | 10–2000 ms | 250 ms | 3 |
| `length` | Length | 0–100 % | 50 % | 3 |
| `style` | Style | Sharp, Smooth, Tight, Loose, Tape, Random | Sharp | 5 |
| `shape` | Shape | Hard, Ramp Up, Ramp Down, Smooth | Hard | 5 |
| `smoothness` | Smoothness | 0–100 % | 40 % | 3 (fade) / 5 |
| `offset_ms` | Offset | −100…+100 ms | 0 ms | 5 |
| `mix` | Mix | 0–100 % | 100 % | 3 |
| `fx_enabled` | FX Chain | off / on | on | 6 |
| `delay_sync` | Delay Sync | free / host | host | 6 |
| `delay_rate` | Delay Rate | same 15 divisions | 1/8 | 6 |
| `delay_time_ms` | Delay Time | 1–2000 ms | 250 ms | 6 |
| `delay_feedback` | Delay Feedback | 0–85 % (hard safety cap) | 30 % | 6 |
| `delay_mix` | Delay Mix | 0–100 % | 0 % | 6 |
| `reverb_size` | Reverb Size | 0–100 % | 50 % | 6 |
| `reverb_damping` | Reverb Damping | 0–100 % | 50 % | 6 |
| `reverb_mix` | Reverb Mix | 0–100 % | 0 % | 6 |

## 4. Decisions waiting for the owner (defaults chosen for the most reliable plug-in, all still reversible)

1. **One bypass state.** `bypass` is both the host's bypass and the plug-in's Bypass button. The power icon shows
   the same state inverted, so Power and Bypass can never disagree (section 12). The specification's "Enabled" is
   this state seen from the other side. A separate "Enabled" would create two overlapping states.
2. **Reverse (mockup) is not a mode.** Section 21 postpones the reverse engine. If it comes later, it will be a
   separate `reverse` on/off parameter. Adding an entry to the Mode list after release would shift saved
   automation, a separate parameter does not.
3. **Glitch Type and Intensity (mockup) and the "Digital Glitch" style** are not in the specification.
   Glitch variations are decided in Phase 4 (section 7: "two or three high-quality variations"). The styles are
   the specification's six. Adding parameters with new IDs before release is safe.
4. **Rate list order.** The list is ordered by note length, longest first. An automation sweep and the
   previous / next arrows then always move monotonically faster. 1/64 is left out (section 4).
5. **Capture / Freeze and the selection region** are decided in Phase 2: automatable parameters versus state
   stored with the session.
6. **Output gain and reverb pre-delay / width** are optional in the specification and left out until
   measurement or listening shows they are needed.
7. **Defaults** follow section 13 where it gives one; the others are provisional until listening tests.
