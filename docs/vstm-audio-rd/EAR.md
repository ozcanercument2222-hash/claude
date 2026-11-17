# The listening stack ("ear")

Claude cannot hear. This repository gives it three kinds of evidence about sound. Each kind carries its own label and its own limits.

| Tool | What it does | Evidence label |
|---|---|---|
| `tools/ear.py` | Sends the actual audio (16 kHz mono, loudness-matched) to a local audio-language model, **Qwen2.5-Omni-7B** (Q4_K_M, llama.cpp). The model returns a text judgment. | `LISTENED-MODEL (Qwen2.5-Omni-7B Q4_K_M)` |
| `tools/audio_view.py` | Computes LUFS, true peak, crest, clipping, NaN/Inf and stereo stats. Draws log-frequency spectrograms with a wet-minus-dry difference panel. Produces pitch contours and tuning statistics. Claude opens the PNGs. | `MEASURED` |
| `tools/audio_view.py pack` | Builds a latency-aligned, loudness-matched human A/B pack with blind files and a hidden key. | becomes `USER-REPORTED` after a human listens |

Human listening always outranks the model.

## Install

```bash
tools/setup_ear.sh          # idempotent; ~7 GB disk, ~11 GB RAM while the model runs
```

The script installs the Python packages and builds a static `llama-mtmd-cli` from the llama.cpp tree vendored in the `llama-cpp-python` sdist on PyPI. That source works where GitHub downloads are blocked. The script also downloads the model files from `huggingface.co/ggml-org/Qwen2.5-Omni-7B-GGUF` and verifies them by SHA-256.

Everything goes to `${VSTM_EAR_HOME:-~/.cache/vstm-ear}`, outside the repository.

## Use

```bash
# one render, free description
python3 tools/ear.py describe render.wav --start 2 --dur 8

# A/B: each clip judged on its own (score 1-10 + yes/no question), then compared
python3 tools/ear.py compare dry.wav wet.wav --start 2 --dur 8 --question distortion
python3 tools/ear.py compare old_build.wav new_build.wav --question "Does the voice sound robotic on the held notes?"

# what can this ear detect on this kind of material?
python3 tools/ear.py calibrate clean.wav --extra "my plugin hard=render.wav"
```

`compare` never puts both clips in one prompt. Each clip gets the same short questions separately, and `verdict` compares the answers. A score difference under 2 is noise. A yes/no difference counts only if the reference answers "no".

Question presets: `distortion`, `clicks`, `noise`, `muffled`, `pumping`, `metallic`, `autotune`, `thumps`.

On a 4-core CPU one model call on a short clip takes about 30–35 s. Answers are cached in `$VSTM_EAR_HOME/cache` by audio, prompt and model, so a reference clip is only judged once. Keep excerpts short (≤10 s) and aimed at the moment in question.

```bash
python3 tools/audio_view.py stats dry.wav wet.wav
python3 tools/audio_view.py spectrogram dry.wav wet.wav --diff --latency 960 -o spec.png
python3 tools/audio_view.py pitch dry.wav wet.wav --latency 960 -o pitch.png --csv pitch.csv
python3 tools/audio_view.py pack dry.wav wet.wav --latency 960 -o artifacts/audio-runs/<run>/pack
```

Get `--latency` from `VSTMOfflineHost --list`. Without it, cross-correlation is used, which is unreliable after pitch shifting (correlation below 0.5).

## Calibration

_Calibration run in progress; results will be added here._

## Known limits

- The model hears 16 kHz mono. Anything above 8 kHz (air, sibilance detail, aliasing) and anything stereo (image, width, phase between channels) is invisible to it. Use `audio_view.py` for those.
- It was trained to caption and answer questions about everyday audio, speech and music. It was not trained on plugin-artifact grading. Treat its descriptions as a coarse screen. It is not a mastering engineer.
- Free-form `describe` output can be confidently wrong about the source, for example calling singing "talking". Prefer `compare` with a targeted question.
- Timestamps in its answers are rough.
- pYIN can report breath or noise as "voiced". The energy gate (`--gate-db`) removes the quietest frames but not loud breaths.
