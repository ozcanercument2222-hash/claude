# VSTM Audio R&D project rules

- This is a commercial-quality audio DSP project. Preserve plugin IDs, parameter IDs, preset/state compatibility, automation behavior, and existing public APIs.
- Never claim to have listened. Output of the local audio model (`tools/ear.py`) is `LISTENED-MODEL (<model>)`: AI perception, valid only for artifact classes it passed in calibration (`docs/vstm-audio-rd/EAR.md`) and only when consistent across both clip orders. Numeric analysis and plots are `MEASURED`, source review is `INSPECTED`, and human feedback is `USER-REPORTED`, which outranks the model.
- Use `VSTMOfflineHost --list <plugin>` before setting parameters. Use `tools/audio_view.py` for LUFS, true peak, spectrogram and pitch evidence, and `tools/audio_view.py pack` for human blind A/B packs.
- For every DSP change: establish a baseline, change one thing at a time, rebuild, rerender identical material, compare level-matched outputs, and run regression checks.
- Use `juce-host/` for offline VST3 rendering when it supports the plugin. Use `pluginval` for format/stability validation.
- Keep all generated renders, logs, and reports under `artifacts/audio-runs/`; never overwrite input WAVs.
- Do not automatically accept a "better" result based only on louder output, larger waveform, or higher spectral energy.
- Ask before destructive edits, broad refactors, long batch tests, installing software, changing system audio settings, or committing.
- Report untested behavior and uncertainty explicitly.
