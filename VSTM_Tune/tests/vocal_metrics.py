#!/usr/bin/env python3
"""Real-vocal regression metrics for VSTM Tune renders (evidence label: MEASURED).

    vocal_metrics.py dry.wav wet.wav [--csv hops.csv] [--latency N] [--json]

wet.wav is the engine/plugin output with its latency still in it (as rendered by
tests/wav_probe or the offline host). hops.csv is the wav_probe analysis log.

Metrics
  lf_bursts           20-150 Hz STFT frames >15 dB above dry (thumps at transitions)
  lf_excess_max_db    worst 20-150 Hz excess over dry
  env_jumps           10 ms RMS-envelope steps of wet that exceed dry's by >3 dB
  note_segments / short_note_segments (<40 ms)    target-note chatter (needs --csv)
  out_vs_target_*     measured output pitch vs engine target, voiced & settled (needs --csv)
  cents_*             output pitch vs nearest semitone (pYIN+YIN)
"""
import argparse
import csv
import json
import sys
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import signal

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import audio_view as av  # noqa: E402


def load(path, lat=0):
    y, sr = sf.read(str(path), always_2d=True, dtype="float64")
    y = y.mean(axis=1)
    if lat:
        y = np.concatenate([y[lat:], np.zeros(lat)])
    return y, sr


def band_db(y, sr, lo, hi, n=2048, hop=240):
    f, t, z = signal.stft(y, sr, nperseg=n, noverlap=n - hop, boundary=None, padded=False)
    b = (f >= lo) & (f <= hi)
    return t + n / 2 / sr, 10 * np.log10(np.sum(np.abs(z[b] * 2) ** 2, axis=0) + 1e-20)


def env_db(y, sr, win=0.010):
    n = int(win * sr)
    k = len(y) // n
    e = np.sqrt(np.mean(y[:k * n].reshape(k, n) ** 2, axis=1))
    return 20 * np.log10(e + 1e-9)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("dry")
    p.add_argument("wet")
    p.add_argument("--csv")
    p.add_argument("--latency", type=int, default=None, help="default: from sample rate (20 ms)")
    p.add_argument("--json", action="store_true")
    a = p.parse_args()

    dry, sr = load(a.dry)
    lat = a.latency if a.latency is not None else int(round(0.020 * sr))
    wet, sr2 = load(a.wet, lat)
    n = min(len(dry), len(wet))
    dry, wet = dry[:n], wet[:n]
    out = {"evidence": "MEASURED", "dry": a.dry, "wet": a.wet, "latency": lat}

    t, ld = band_db(dry, sr, 20, 150)
    _, lw = band_db(wet, sr, 20, 150)
    _, lf0 = band_db(dry, sr, 150, 4000)
    active = lf0 > lf0.max() - 45
    ex = (lw - ld)[active]
    peaks, _ = signal.find_peaks(np.where(active, lw - ld, -99), height=15, distance=8)
    out["lf_bursts"] = int(len(peaks))
    out["lf_burst_times"] = [round(float(t[i]), 3) for i in peaks]
    out["lf_excess_max_db"] = round(float(ex.max()), 1) if ex.size else None
    out["lf_wet_max_dbfs"] = round(float(lw[active].max()), 1) if active.any() else None

    ed, ew = env_db(dry, sr), env_db(wet, sr)
    act = ed > ed.max() - 40
    jd, jw = np.abs(np.diff(ed)), np.abs(np.diff(ew))
    jumps = (jw - jd > 3.0) & act[1:] & act[:-1]
    out["env_jumps"] = int(np.sum(jumps))
    out["level_diff_db"] = round(float(20 * np.log10((np.std(wet) + 1e-12) / (np.std(dry) + 1e-12))), 2)

    tw, fw, vw = av.pitch_track(wet, sr, 65, 1100)
    st = av.pitch_stats(tw, fw, vw, 440.0)
    out["cents_median_from_semitone"] = st.get("median_abs_cents_from_nearest_semitone")
    out["cents_pct_within_10"] = st.get("pct_frames_within_10_cents")
    out["frame_step_p95_cents"] = st.get("p95_frame_step_cents")

    if a.csv:
        rows = list(csv.DictReader(open(a.csv)))
        ct = np.array([float(r["time_s"]) for r in rows])
        cv = np.array([r["voiced"] == "1" for r in rows])
        cn = np.array([int(r["note"]) for r in rows])
        co = np.array([float(r["out_midi"]) for r in rows])
        segs, start = [], None
        for i in range(len(rows)):
            if cv[i] and (i == 0 or not cv[i - 1] or cn[i] != cn[i - 1]):
                start = i
            if cv[i] and (i == len(rows) - 1 or not cv[i + 1] or cn[i + 1] != cn[i]):
                segs.append((ct[i] - ct[start]) * 1000.0)
        out["note_segments"] = len(segs)
        out["short_note_segments"] = int(sum(d < 40 for d in segs))
        # align analysis timeline to the input with the engine's own input-pitch column
        td, fd, vd = av.pitch_track(dry, sr, 65, 1100)
        ci = np.array([float(r["in_midi"]) for r in rows])
        dm = np.where(vd, 69 + 12 * np.log2(np.where(fd > 0, fd, 1) / 440), np.nan)
        best = (0.0, 1e9)
        for off in np.arange(0.0, 0.06, 0.001):
            x = np.interp(td, ct - off, ci)
            e = np.nanmedian(np.abs(x[vd] - dm[vd])) if vd.any() else 1e9
            if e < best[1]:
                best = (off, e)
        off = best[0]
        tgt = np.interp(tw, ct - off, co)
        # only frames where the target note has been constant for >= 20 ms around them
        stable = np.ones_like(tw, dtype=bool)
        for dt in (-0.010, 0.0, 0.010):
            stable &= np.interp(tw + dt, ct - off, cv.astype(float)) > 0.99
            stable &= np.abs(np.interp(tw + dt, ct - off, co) - tgt) < 0.01
        wm = 69 + 12 * np.log2(np.where(fw > 0, fw, 1) / 440)
        ok = stable & vw
        if ok.any():
            err = np.abs(wm[ok] - tgt[ok]) * 100
            out["out_vs_target_median_cents"] = round(float(np.median(err)), 2)
            out["out_vs_target_p90_cents"] = round(float(np.percentile(err, 90)), 2)
            out["out_vs_target_frames"] = int(ok.sum())

    if a.json:
        print(json.dumps(out, indent=2))
    else:
        for k, v in out.items():
            if k not in ("dry", "wet", "evidence"):
                print(f"{k:30s} {v}")


if __name__ == "__main__":
    main()
