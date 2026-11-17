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

`compare` never puts both clips in one prompt. Each clip gets the same short questions separately, in 3 views (0/10/30 ms of leading silence; change with `--views N` before the subcommand). `verdict` is `(robust)` only when every view agrees. Otherwise it reads `inconclusive`, which is not a pass.

Question presets: `distortion`, `clicks`, `noise`, `muffled`, `pumping`, `metallic`, `autotune`, `thumps`.

On a 4-core CPU one model call on a short clip takes about 30–35 s, so a `compare` with a question takes about 12 calls (~6 min) the first time. Answers are cached in `$VSTM_EAR_HOME/cache` by audio, prompt and model, so a reference clip is only judged once. Keep excerpts short (≤10 s) and aimed at the moment in question.

```bash
python3 tools/audio_view.py stats dry.wav wet.wav
python3 tools/audio_view.py spectrogram dry.wav wet.wav --diff --latency 960 -o spec.png
python3 tools/audio_view.py pitch dry.wav wet.wav --latency 960 -o pitch.png --csv pitch.csv
python3 tools/audio_view.py pack dry.wav wet.wav --latency 960 -o artifacts/audio-runs/<run>/pack
```

Get `--latency` from `VSTMOfflineHost --list`. Without it, cross-correlation is used, which is unreliable after pitch shifting (correlation below 0.5).

## Calibration

Run on 2026-11-17 with a sung vocal: a RAVDESS song clip, 4.3 s long, recorded at 16 kHz and upsampled to 48 kHz. The clip is not included in the repository (CC BY-NC-SA). The calibration applies known degradations to it, plus two real VSTM Tune renders.

### Bottom line: what this ear can be trusted for

| Artifact class | Status | Use |
|---|---|---|
| Gross distortion / hard clipping | **Detected, robust.** `--question distortion` said "yes" in every view, and "no" on clean in every view. | OK as `LISTENED-MODEL` evidence |
| Clicks / pops | **Partial.** `--question clicks` said "yes" in 2 of 3 views. | Hint only; confirm by measurement |
| Mild saturation (~1–3% THD), 6-bit quantization | Missed | Measure (THD, spectrum) |
| Pumping (10 dB at 4 Hz) | Missed | Measure (gain-reduction curve, short-term LUFS) |
| Hiss, muffled tone, metallic/phasey smear, low thumps | **Question unusable:** the model also says "yes" on the clean clip | Measure; human pack |
| Auto-tune / robotic character (VSTM Tune, hard settings) | Missed: the model calls both dry and wet "natural" | Pitch statistics; human pack |
| 1–10 quality score | **Unstable.** The score flips between 10 and 1 when 10–30 ms of silence is added, even for hard clipping. | Never use a score change on its own |

In short, the model ear is a coarse screen for gross failures. For the subtle differences plugin QA is usually about, it gives no evidence, and an answer of "clean" from it means nothing. Rely on `audio_view.py` measurements and human blind packs there.

### How the method was chosen

| Prompting tried | Result on a +18 dB hard-clipped vocal |
|---|---|
| Long checklist: "describe artifacts: distortion, clicks, noise, …" | "No audible artifacts … the sound is clean" ✗ |
| Two clips in one prompt: "which has more artifacts?" | Picked the clean clip in one order and "SAME" in the other ✗ |
| One-line scorecard: `distortion=N noise=N … quality=N` | All zeros and 10/10 for clean, clipped and hissy clips alike ✗ |
| One plain question per call: "Is the sound in this recording distorted?" | "Yes … distorted" (clean: "No") ✓ |

### Single-view results (0 ms)

| Case | Score clean → case | Targeted question | Question outcome |
|---|---|---|---|
| near-identical (+0.2 dB, dither) | 10 → 10 | — | stable |
| hard clip (+18 dB into clipper) | 10 → 1 | distortion | detected |
| mild saturation (tanh) | 10 → 10 | distortion | missed |
| bit crush (6-bit) | 10 → 10 | distortion | missed |
| clicks (12 impulses) | 10 → 10 | clicks | detected |
| hiss (−30 dB re RMS) | 10 → 10 | noise | unusable (yes on clean) |
| muffled (low-pass 2.5 kHz) | 10 → 10 | muffled | unusable (yes on clean) |
| pumping (4 Hz, 10 dB) | 10 → 10 | pumping | missed |
| phase-vocoder smear (±3 st) | 10 → 10 | metallic | unusable (yes on clean) |
| VSTM Tune default (Key Auto) | 10 → 1 | autotune | missed |
| VSTM Tune hard (F major, retune 0, robotism max) | 10 → 1 | autotune | missed |
| VSTM Tune hard | — | thumps | unusable (yes on clean) |

### Stability across views (0 / 10 / 30 ms of leading silence)

| Clip | Scores | Targeted answers |
|---|---|---|
| clean | 10, 10, 10 | distortion: no ×3; clicks: no ×3 |
| hard clip | 1, 1, 1 (silence added at 48 kHz); 1, 1, 10 (added at 16 kHz) | distortion: yes ×3 (both runs) |
| clicks | 10, 10, 10 | clicks: yes, no, yes |
| VSTM Tune default | 1, 1, 10 | autotune: no ×3 |

The VSTM Tune "1/10" was checked against measurement and found to be noise. The silent-section noise floor is identical to dry, and the between-harmonic floor rises only 1.3 dB. A 20 ms shift of the same render scored 10. Removing everything below 150 Hz scored 1 again. Clean plus only the plugin's sub-150 Hz band also scored 1, while the high-passed clean control scored 10. These results point in no consistent direction, so the score drop is not evidence of anything.

Re-run the calibration when the model, the quantization, or the kind of material changes:
`python3 tools/ear.py calibrate clean.wav --extra "name=render.wav@autotune" --log calib.jsonl` (default 3 views; budget about 30 s per model call).

## Known limits

- The model hears 16 kHz mono. Anything above 8 kHz (air, sibilance detail, aliasing) and anything stereo (image, width, phase between channels) is invisible to it. Use `audio_view.py` for those.
- It was trained to caption and answer questions about everyday audio, speech and music. It was not trained on plugin-artifact grading. Treat its descriptions as a coarse screen. It is not a mastering engineer.
- Free-form `describe` output can be confidently wrong about the source, for example calling singing "talking". Prefer `compare` with a targeted question.
- Timestamps in its answers are rough.
- pYIN can report breath or noise as "voiced". The energy gate (`--gate-db`) removes the quietest frames but not loud breaths.
