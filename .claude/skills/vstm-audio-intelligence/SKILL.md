---
name: vstm-audio-intelligence
description: Autonomously test, render, measure, critically evaluate, debug, and iteratively improve JUCE audio plugins and VST3 effects. Use for compressors, autotune, EQ, saturation, dynamics, DSP quality, plugin QA, audio artifacts, and sound comparisons.
---

# VSTM Audio Intelligence — Autonomous R&D

Act as a senior DSP engineer, plugin QA engineer, and critical listening lead. Own the experiment loop, not just the code suggestion.

## Hard evidence rules

- `LISTENED`: only if a real audio-perception model/tool has consumed the audio and returned an assessment, or a human has auditioned it.
- `MEASURED`: numerical signal analysis only.
- `INSPECTED`: source-code/configuration review only.
- `USER-REPORTED`: a human's listening feedback.
- Never convert a spectrum, waveform, LUFS value, or source-code inference into a claim that you heard the audio.
- If no audio-perception capability is configured, continue all measurable QA and explicitly report `MEASURED_ONLY`; prepare a listening pack and request human listening for subjective decisions. Do not fabricate listening.

## Autonomous loop

1. **Map the project**
   - Inspect repository, Git status, build system, JUCE version, plugin formats, parameter IDs, state serialization, tests, and current build artifacts.
   - Identify host/render capability, `pluginval`, and audio-perception capability.
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
   - Run `tools/audio_lab.py` for signal measurements and A/B metrics.
   - Run `tools/run_pluginval.py` for plugin validation if installed.
   - Capture stdout/stderr and exit codes. Never report a test as passed without a successful exit.
5. **Listen**
   - Discover available audio-capable tools. If a genuine audio-perception tool is configured, submit short, level-matched dry/wet excerpts plus context and ask for specific audible artifacts and timestamps.
   - If no such tool exists, do not claim listening. Create a listening pack containing dry, wet, loudness-matched files, excerpts, and a concise evaluation sheet.
   - Keep human or model listening judgments separate from measurements.
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
`juce-host/build/VSTMOfflineHost_artefacts/Release/VSTMOfflineHost <plugin.vst3> <input.wav> <output.wav> [sampleRate] [blockSize] [parameterId=value ...]`

Parameter values are normalized only if the host cannot map the requested real value. Inspect the plugin's parameter range before setting values. The starter host currently expects normalized values 0..1 for parameter assignments; do not pass engineering units unless you convert them to normalized values.

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
