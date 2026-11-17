# VSTM Audio R&D project rules

- This is a commercial-quality audio DSP project. Preserve plugin IDs, parameter IDs, preset/state compatibility, automation behavior, and existing public APIs.
- Never claim to have listened unless an audio-perception tool actually returned a perceptual assessment. Numeric analysis is `MEASURED`, source review is `INSPECTED`, and user feedback is `USER-REPORTED`.
- For every DSP change: establish a baseline, change one thing at a time, rebuild, rerender identical material, compare level-matched outputs, and run regression checks.
- Use `juce-host/` for offline VST3 rendering when it supports the plugin. Use `pluginval` for format/stability validation.
- Keep all generated renders, logs, and reports under `artifacts/audio-runs/`; never overwrite input WAVs.
- Do not automatically accept a "better" result based only on louder output, larger waveform, or higher spectral energy.
- Ask before destructive edits, broad refactors, long batch tests, installing software, changing system audio settings, or committing.
- Report untested behavior and uncertainty explicitly.
