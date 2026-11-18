#!/usr/bin/env python3
"""Test signals for the DAW tests of VSTM Tune (REAPER, FL Studio, ...).

    python3 make_test_signals.py [OUT_DIR] [--rate 48000]

Writes 24-bit WAV files with known content (synthetic sung vowels, no recordings of anyone):
  t1_a3_plus40.wav      A3 sung 40 cents sharp, 4 s          -> hard tune must give 220.00 Hz
  t2_melody.wav         C major melody, slightly out of tune, vibrato, 12 s (Auto key, automation)
  t3_pdc_clicks.wav     short clicks every second + A3 bursts (latency compensation, PDC)
  t4_octave_leap.wav    soprano "i": A4 -> A5 -> A4 legato, 3 s (octave tracking)
  t5_level.wav          A3 steady, 4 s (transpose / formant level)
  t6_stereo_lr.wav      stereo: voice left, silence right, 3 s (channel layout)
manifest.json lists each file with what the analysis expects of it.
"""
import argparse
import json
import math
from pathlib import Path

import numpy as np
import soundfile as sf


def midi_hz(m):
    return 440.0 * 2.0 ** ((m - 69.0) / 12.0)


def resonator(x, sr, f, bw):
    from scipy import signal
    r = math.exp(-math.pi * bw / sr)
    return signal.lfilter([(1.0 - r) * 2.0], [1.0, -2.0 * r * math.cos(2.0 * math.pi * f / sr), r * r], x)


def voice(sr, secs, midi_at, vowel="a", level=0.3):
    n = int(sr * secs)
    t = np.arange(n) / sr
    f0 = midi_hz(midi_at(t))
    ph = np.cumsum(f0) / sr
    src = np.zeros(n)
    for h in range(1, 41):
        ok = h * f0 < 0.45 * sr
        src += np.where(ok, np.sin(2 * np.pi * h * ph) / h ** 2 * (1.0 if h == 1 else 1.6), 0.0)
    p = np.diff(src, prepend=0.0)
    F = {"a": (730, 1090, 2440, 90, 110, 170, 0.7, 0.35), "i": (300, 2300, 3000, 70, 120, 180, 0.5, 0.4)}[vowel]
    y = resonator(p, sr, F[0], F[3]) + F[6] * resonator(p, sr, F[1], F[4]) + F[7] * resonator(p, sr, F[2], F[5])
    rng = np.random.default_rng(7)
    y += 0.0005 * rng.standard_normal(n) * np.max(np.abs(y))
    return level * y / np.max(np.abs(y))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", nargs="?", default="vstm_daw_test_signals")
    ap.add_argument("--rate", type=int, default=48000)
    a = ap.parse_args()
    sr = a.rate
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    files = {}

    def write(name, y, info):
        sf.write(str(out / name), y.astype(np.float32), sr, subtype="PCM_24")
        files[name] = info

    write("t1_a3_plus40.wav", voice(sr, 4.0, lambda t: np.full_like(t, 57.40)),
          {"expect": "hard tune: 220.00 Hz (A3) from 1.0 s on", "pitch_hz": 220.0})
    notes = [60, 62, 64, 65, 67, 69, 71, 72, 67, 64, 65, 62, 60, 64, 67, 72, 71, 69, 67, 65, 64, 62, 60, 60]

    def melody(t):
        i = np.minimum((t / 0.5).astype(int), len(notes) - 1)
        m = np.array(notes, dtype=float)[i]
        return m + 0.18 * np.sin(i * 1.7) + 0.25 * np.sin(2 * np.pi * 5.5 * t)
    write("t2_melody.wav", voice(sr, 12.0, melody), {"expect": "Auto key finds C (major); automation renders clean"})

    clicks = voice(sr, 6.0, lambda t: np.full_like(t, 57.0)) * 0.0
    for k in range(1, 6):
        i = int(k * sr)
        clicks[i:i + 24] = np.hanning(24) * 0.5 * (1 if k % 2 else -1)
        burst = voice(sr, 0.4, lambda t: np.full_like(t, 57.0), level=0.2)
        clicks[i + int(0.2 * sr):i + int(0.2 * sr) + len(burst)] += burst
    write("t3_pdc_clicks.wav", clicks, {"expect": "rendered through the plugin, aligned with the dry file to +-1 sample (PDC)"})

    def leap(t):
        m = np.where(t < 1.0, 69.3, np.where(t < 2.0, 81.3, 69.3))
        s1 = (t >= 1.0) & (t < 1.025)
        s2 = (t >= 2.0) & (t < 2.025)
        m = np.where(s1, 69.3 + 12 * (t - 1.0) / 0.025, m)
        m = np.where(s2, 81.3 - 12 * (t - 2.0) / 0.025, m)
        return m
    write("t4_octave_leap.wav", voice(sr, 3.0, leap, vowel="i"), {"expect": "hard tune: 880 Hz in 1.2-1.9 s, no f0/2 in the output"})
    write("t5_level.wav", voice(sr, 4.0, lambda t: np.full_like(t, 57.40)), {"expect": "transpose / formant +-12: level within 1 dB of the dry file"})
    v = voice(sr, 3.0, lambda t: np.full_like(t, 57.40))
    sf.write(str(out / "t6_stereo_lr.wav"), np.stack([v, np.zeros_like(v)], axis=1).astype(np.float32), sr, subtype="PCM_24")
    files["t6_stereo_lr.wav"] = {"expect": "left tuned to 220 Hz, right stays silent"}
    (out / "manifest.json").write_text(json.dumps({"sample_rate": sr, "files": files}, indent=1))
    print(f"wrote {len(files)} files to {out} @ {sr} Hz")


if __name__ == "__main__":
    main()
