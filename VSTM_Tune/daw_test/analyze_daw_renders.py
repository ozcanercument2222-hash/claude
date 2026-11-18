#!/usr/bin/env python3
"""Checks the WAV renders of the DAW test (README.md) and prints a PASS / FAIL table.

    python3 analyze_daw_renders.py SIGNALS_DIR RENDERS_DIR [--json out.json]

SIGNALS_DIR: the folder make_test_signals.py wrote (t1 ... t6 + manifest.json).
RENDERS_DIR: the DAW's renders, named as in README.md (R01_hard_t1.wav ...). Every render must
cover the whole project from time 0, at the signals' sample rate. Missing renders are listed as
MISSING, not as failures. Evidence label: MEASURED (no listening).
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np
import soundfile as sf


def load(p, mono=True):
    y, sr = sf.read(str(p), always_2d=True, dtype="float64")
    return (y.mean(axis=1) if mono else y), sr


def hz_near(x, sr, start, length, expect):
    """normalised autocorrelation within 0.8 ... 1.25 of the expected period (octave blind on purpose)"""
    T = sr / expect
    lo, hi = max(2, int(T * 0.8)), int(T * 1.25) + 2
    a = x[start:start + length]
    if start < 0 or start + length + hi + 2 > len(x) or np.dot(a, a) < 1e-12:
        return None
    r = np.array([np.dot(a, x[start + l:start + l + length]) / np.sqrt(np.dot(a, a) * np.dot(x[start + l:start + l + length], x[start + l:start + l + length]) + 1e-20)
                  for l in range(lo - 1, hi + 2)])
    b = int(np.argmax(r[1:-1])) + 1
    den = r[b - 1] - 2 * r[b] + r[b + 1]
    lag = lo - 1 + b + (0.5 * (r[b - 1] - r[b + 1]) / den if abs(den) > 1e-12 else 0.0)
    return sr / lag


def cents(f, ref):
    return 1200.0 * np.log2(f / ref) if f else float("nan")


def rms_db(x):
    return 20 * np.log10(np.sqrt(np.mean(x ** 2)) + 1e-12)


def lag_of(render, dry, sr, a, b):
    """samples by which render lags dry in [a, b) seconds (cross-correlation)"""
    seg = dry[int(a * sr):int(b * sr)]
    best, bl = -1e9, 0
    m = int(0.1 * sr)
    for l in range(-m, m + 1, 1):
        s = int(a * sr) + l
        if s < 0 or s + len(seg) > len(render):
            continue
        c = float(np.dot(seg, render[s:s + len(seg)]))
        if c > best:
            best, bl = c, l
    return bl


def check(name, ok, detail):
    return {"render": name, "result": "PASS" if ok else "FAIL", "detail": detail}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("signals")
    ap.add_argument("renders")
    ap.add_argument("--json")
    a = ap.parse_args()
    S, R = Path(a.signals), Path(a.renders)
    rows = []

    def find(name):
        """exact name, or any WAV that contains the R-code (FL Studio: "<project> - R01_hard_t1.wav")"""
        if (R / name).exists():
            return R / name
        hits = sorted(R.glob(f"*{Path(name).stem}*.wav"))
        return hits[0] if hits else R / name

    def have(name):
        p = find(name)
        if not p.exists():
            rows.append({"render": name, "result": "MISSING", "detail": ""})
            return None
        y, sr = load(p)
        if not np.all(np.isfinite(y)):
            rows.append(check(name, False, "non-finite samples"))
            return None
        return y, sr

    t1, sr = load(S / "t1_a3_plus40.wav")
    for name in ("R01_hard_t1.wav", "R11_buffer64_t1.wav", "R12_buffer2048_t1.wav"):
        r = have(name)
        if r:
            y, _ = r
            c = cents(hz_near(y, sr, int(1.5 * sr), int(0.3 * sr), 220.0), 220.0)
            lvl = rms_db(y[int(1.5 * sr):int(3.5 * sr)]) - rms_db(t1[int(1.5 * sr):int(3.5 * sr)])
            rows.append(check(name, abs(c) < 2.0 and abs(lvl) < 1.0, f"pitch {c:+.2f} ct vs A3 (220 Hz), level {lvl:+.2f} dB"))
    base = find("R01_hard_t1.wav")
    for name in ("R11_buffer64_t1.wav", "R12_buffer2048_t1.wav"):
        if base.exists() and find(name).exists():
            y0, _ = load(base)
            y1, _ = load(find(name))
            n = min(len(y0), len(y1))
            d = rms_db(y1[:n] - y0[:n]) - rms_db(y0[:n])
            rows.append(check(name + " = R01", d < -60.0, f"difference {d:.1f} dB (block size must not change the sound)"))

    t3, _ = load(S / "t3_pdc_clicks.wav")
    for name, mode in (("R02_pdc_realtime_t3.wav", "REALTIME 20 ms"), ("R03_pdc_hq_t3.wav", "HQ 40 ms")):
        r = have(name)
        if r:
            y, _ = r
            lag = lag_of(y, t3, sr, 0.95, 5.2)
            rows.append(check(name, abs(lag) <= 1, f"{mode}: render lags the dry file by {lag} samples (0 = the DAW compensates the latency)"))

    t5, _ = load(S / "t5_level.wav")
    for name, expect in (("R04_transpose+12_t5.wav", 440.0), ("R05_formant-12_t5.wav", 220.0), ("R06_formant+12_t5.wav", 220.0),
                         ("R07_transpose-12_t5.wav", 110.0)):
        r = have(name)
        if r:
            y, _ = r
            c = cents(hz_near(y, sr, int(1.5 * sr), int(0.3 * sr), expect), expect)
            lvl = rms_db(y[int(1.5 * sr):int(3.5 * sr)]) - rms_db(t5[int(1.5 * sr):int(3.5 * sr)])
            rows.append(check(name, abs(c) < 5.0 and abs(lvl) <= 1.0, f"pitch {c:+.2f} ct vs {expect:.0f} Hz, level {lvl:+.2f} dB (target +-1 dB)"))

    t4, _ = load(S / "t4_octave_leap.wav")
    r = have("R08_octave_t4.wav")
    if r:
        y, _ = r
        c = cents(hz_near(y, sr, int(1.4 * sr), int(0.3 * sr), 880.0), 880.0)
        seg = y[int(1.3 * sr):int(1.8 * sr)] * np.hanning(int(0.5 * sr))
        spec = np.abs(np.fft.rfft(seg))
        fr = np.fft.rfftfreq(len(seg), 1 / sr)
        band = lambda f: spec[(fr > f * 0.97) & (fr < f * 1.03)].max()
        sub = 20 * np.log10(band(440.0) / band(880.0) + 1e-12)
        rows.append(check("R08_octave_t4.wav", abs(c) < 5.0 and sub < -50.0, f"A5 {c:+.2f} ct vs 880 Hz, f0/2 {sub:.1f} dB under f0"))

    t2, _ = load(S / "t2_melody.wav")
    r7 = have("R09_automation_t2.wav")
    if r7:
        y, _ = r7
        step_dry = np.max(np.abs(np.diff(t2)))
        step = np.max(np.abs(np.diff(y)))
        rows.append(check("R09_automation_t2.wav", step < 3.0 * step_dry and np.max(np.abs(y)) < 0.99,
                          f"largest sample step {step:.4f} (dry {step_dry:.4f}), peak {np.max(np.abs(y)):.3f}"))
    for name, what in (("R10_automation_reopened_t2.wav", "after closing and reopening the project"),
                       ("R13_freeze_t2.wav", "frozen / bounced track")):
        if r7 and find(name).exists():
            y0, _ = load(find("R09_automation_t2.wav"))
            y1, _ = load(find(name))
            n = min(len(y0), len(y1))
            d = rms_db(y1[:n] - y0[:n]) - rms_db(y0[:n])
            rows.append(check(name, d < -60.0, f"{what}: difference to R09 {d:.1f} dB"))
        elif not find(name).exists():
            rows.append({"render": name, "result": "MISSING", "detail": ""})

    r = find("R14_stereo_t6.wav")
    if r.exists():
        st, _ = load(r, mono=False)
        if st.shape[1] >= 2:
            c = cents(hz_near(st[:, 0], sr, int(1.5 * sr), int(0.3 * sr), 220.0), 220.0)
            right = rms_db(st[int(0.5 * sr):, 1])
            rows.append(check("R14_stereo_t6.wav", abs(c) < 2.0 and right < -90.0, f"left {c:+.2f} ct vs 220 Hz, right {right:.1f} dBFS"))
        else:
            rows.append(check("R14_stereo_t6.wav", False, "render is mono"))
    else:
        rows.append({"render": "R14_stereo_t6.wav", "result": "MISSING", "detail": ""})

    print("| render | result | measured |")
    print("|---|---|---|")
    for x in rows:
        print(f"| {x['render']} | {x['result']} | {x['detail']} |")
    if a.json:
        Path(a.json).write_text(json.dumps({"evidence": "MEASURED", "rows": rows}, indent=1))
    return 1 if any(x["result"] == "FAIL" for x in rows) else 0


if __name__ == "__main__":
    sys.exit(main())
