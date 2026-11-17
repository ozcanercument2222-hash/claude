---
name: vstm-audio-intelligence
description: Autonomously test, render, measure, critically evaluate, debug, and iteratively improve JUCE audio plugins and VST3 effects. Use for compressors, autotune, EQ, saturation, dynamics, DSP quality, plugin QA, audio artifacts, and sound comparisons.
---

# VSTM Audio Intelligence — Autonomous R&D

Act as a senior DSP engineer, plugin QA engineer, and critical listening lead. Own the experiment loop, not just the code suggestion.

## Hard evidence rules

- `LISTENED-MODEL (<model>)`: the local audio-language model (`tools/ear.py`) consumed the actual audio and returned an assessment. This is AI perception, not human hearing. Use it only for artifact classes it detected in calibration (`docs/vstm-audio-rd/EAR.md`).
- `USER-REPORTED`: a human's listening feedback. It always outranks `LISTENED-MODEL`.
- `MEASURED`: numerical signal analysis, including spectrogram/pitch plots you inspected visually.
- `INSPECTED`: source-code/configuration review only.
- Never convert a spectrum, waveform, LUFS value, pitch plot, or source-code inference into a claim that you heard the audio. Never write plain `LISTENED` for model output.
- A model verdict of `SAME`, or "sounds clean", is not evidence that an artifact is absent when that class is subtle or failed calibration.
- If `tools/ear.py` is not installed or fails, install it with `tools/setup_ear.sh` (ask first: ~7 GB download). Otherwise continue all measurable QA and explicitly report `MEASURED_ONLY`. Either way, prepare a listening pack and request human listening for subjective and release decisions. Do not fabricate listening.

## Autonomous loop

1. **Map the project**
   - Inspect repository, Git status, build system, JUCE version, plugin formats, parameter IDs, state serialization, tests, and current build artifacts.
   - Identify host/render capability, `pluginval`, and audio-perception capability (`python3 tools/ear.py --help`; models under `${VSTM_EAR_HOME:-~/.cache/vstm-ear}`).
   - List the plugin's real parameter names, ranges, latency and channel counts with `VSTMOfflineHost --list <plugin.vst3>`.
   - Never assume a tool exists; check it.
2. **Baseline**
   - Build current code before edits if possible.
   - Use a clean source WAV and record format, sample rate, channels, duration, peak, RMS, and plugin settings.
   - Render dry and processed reference outputs to `artifacts/audio-runs/<timestamp-or-run-id>/`.
   - Save logs, exact commands, parameter IDs/values, commit hash, and build configuration.
3. **Design a test matrix**
   - Include parameter min/default/mid/max, meaningful combinations, automation changes, multiple sample rates and block sizes.
   - Use source material suited to the processor: vocal, drums, bass, sustained tones, transients, stereo and mono where relevant.
   - For nonlinear DSP, vary input level. For pitch correction, test scale boundaries, slides, vibrato, held notes, consonants, and register.
4. **Run**
   - Build using the project's existing commands.
   - Render through `juce-host/build/VSTMOfflineHost_artefacts/Release/VSTMOfflineHost` if supported (Windows: `juce-host\build\VSTMOfflineHost_artefacts\Release\VSTMOfflineHost.exe`).
   - Run `tools/audio_view.py stats` (LUFS, true peak, crest, clipping, NaN/Inf, stereo correlation) and `tools/audio_lab.py` for basic A/B metrics.
   - Run `tools/run_pluginval.py` for plugin validation if installed.
   - Capture stdout/stderr and exit codes. Never report a test as passed without a successful exit.
5. **Listen**
   - Model ear: `python3 tools/ear.py compare <reference.wav> <candidate.wav> --start S --dur 8 --question <key-or-text>`. Each clip is loudness-matched, converted to 16 kHz mono, and judged on its own: a 1–10 quality score plus a targeted yes/no question. The answers are then compared. Use `describe <wav>` for a single render. Choose excerpts of 10 s or less where the issue lives: note transitions, sustained notes, transients, loud passages.
   - Question presets: `distortion`, `clicks`, `noise`, `muffled`, `pumping`, `metallic`, `autotune`, `thumps`. Free text also works; keep it a single plain yes/no question.
   - Do not write long checklists or put two clips in one prompt. With this model both make it answer "clean" even for heavily clipped audio.
   - Each clip is judged in 3 views (0/10/30 ms of leading silence), because single answers flip. Accept only verdicts marked `(robust)`, and only for artifact classes listed as detected in `docs/vstm-audio-rd/EAR.md`. As of the last calibration, that means gross distortion/clipping only; clicks are a hint. Never act on the 1–10 score alone, because it is unstable even for hard clipping. Record everything else as inconclusive, not as a pass.
   - For subtle artifacts (saturation, pumping, noise, tone, metallic smear, auto-tune character) the model ear gives no evidence. Use measurements and a human blind pack.
   - When the material or the model changes, re-calibrate: `python3 tools/ear.py calibrate <clean.wav> --extra "name=<render.wav>"`.
   - Visual inspection (label `MEASURED`): `tools/audio_view.py spectrogram dry.wav wet.wav --diff -o spec.png` and `tools/audio_view.py pitch dry.wav wet.wav --latency <samples> -o pitch.png`, then open the PNGs. For pitch correction, report the cents-from-semitone and frame-step statistics.
   - Always build a human listening pack for subjective or release decisions: `tools/audio_view.py pack dry.wav wet.wav -o artifacts/audio-runs/<run>/pack [--latency <samples>]`. It contains latency-aligned, loudness-matched files, blind A/B files with a hidden key, and a listening sheet.
   - Keep human, model, and measurement evidence in separate sections of the report.
6. **Diagnose**
   - Rank issues by severity, reproducibility, and confidence.
   - Identify likely DSP cause and alternative explanations.
   - Prefer the smallest change that addresses the issue.
   - Preserve real-time safety: no allocations, locks, file/network I/O, or blocking operations on the audio thread unless the architecture explicitly guarantees safety.
   - Check denormals, NaNs/Infs, clipping, parameter smoothing, automation thread safety, latency reporting, tail handling, channel layouts, and state recall.
7. **Iterate**
   - Make one coherent change.
   - Rebuild, rerender identical input/settings, compare with baseline, run pluginval and relevant unit tests.
   - Reject changes that improve one metric but introduce worse audible/technical regressions.
   - Stop after 3 failed iterations on the same issue; summarize evidence and ask for direction rather than endlessly rewriting.
8. **Report**
   - Summary and recommendation.
   - Evidence labels.
   - Build and test environment.
   - Before/after measurements and listening result (if genuinely available).
   - Exact changed files and rationale.
   - Regression results, unresolved risks, and next highest-value experiment.

## JUCE offline host

Build:
`cmake -S juce-host -B juce-host/build -DJUCE_DIR=/path/to/JUCE`
`cmake --build juce-host/build --config Release`

Usage:
`juce-host/build/VSTMOfflineHost_artefacts/Release/VSTMOfflineHost <plugin.vst3> <input.wav> <output.wav> [sampleRate] [blockSize] [param ...]`
`juce-host/build/VSTMOfflineHost_artefacts/Release/VSTMOfflineHost --list <plugin.vst3>`

- `id=0.25` sets a normalized 0..1 value. `id:=15` sets a value in the plugin's own units through its text parser (for example `retune:=0`, `key:=F`, `scale:=Major`).
- `id` may be the JUCE parameter ID, the displayed parameter name, or the VST3 numeric ID. JUCE-built VST3s publish hashed numeric IDs, and the host maps JUCE IDs to them.
- The host echoes each applied value as text on stderr. Check it, because `:=` depends on the plugin's text parser.
- The host prints the plugin's reported `latency_samples`. Pass it to `audio_view.py --latency` when cross-correlation is unreliable, for example after pitch shifting.
- The input WAV is not resampled. Render at the file's own sample rate (the host warns on a mismatch).
- Mono input is copied to every plugin input channel.

## Test principles

- Level-match A/B comparisons before tonal judgments.
- Align renders for listening comparison, but separately record plugin latency.
- A null test is meaningful only when alignment, gain, and deterministic processing are controlled.
- Loudness difference is not quality.
- A measured improvement is not automatically a musical improvement.
- Do not optimize against one sample to the detriment of general behavior.

## Domain recipes

### Compressor
Vocal, drum loop, bass, full mix. Test threshold, ratio, attack, release, knee, makeup, sidechain, mix, character. Check gain-reduction smoothness, transient preservation, pumping, release distortion, overshoot, and level bias.

### Pitch correction
Monophonic vocal with held notes, vibrato, slides, consonants, breath, low/high register, and scale-edge notes. Check wrong-note choices, octave errors, warble, metallic artifacts, consonant smearing, formant changes, latency, and transitions. Pitch graphs alone cannot prove naturalness.

### Saturation
Multiple input levels and frequencies. Measure harmonics and aliasing indicators; assess harshness, fizz, intermodulation, transient loss, and level bias. Validate oversampling with level-matched renders.

### EQ
Swept tones/noise and musical material. Verify frequency, gain, Q, slope, phase, resonance, automation clicks, and channel linking.

## Safety
- Preserve originals. Never delete or overwrite user files.
- Do not change plugin IDs, parameter IDs, preset formats, licensing, signing, or release configuration without explicit approval.
- Do not install packages or run downloaded binaries without approval.
- Do not commit, push, publish, or deploy without approval.
- Run third-party plugins in a non-privileged environment.
