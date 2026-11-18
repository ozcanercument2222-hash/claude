#!/usr/bin/env python3
"""Detector accuracy against an independent reference (evidence label: MEASURED).

    pitch_eval.py dry.wav hops.csv [--wet out.wav --latency N] [--cache DIR] [--json]

hops.csv is the analysis log written by VSTMTune_WavProbe (--csv). The reference
pitch of the dry recording is pYIN, kept only where it is confident and agrees with
YIN within 100 cents (both wrong in the same way is possible, but rare). The reference
is cached per input file (it is the slow part), so many settings / builds can be
compared cheaply.

Detector metrics (engine input pitch vs reference, on the reference frame grid)
  voiced_recall          engine voiced where the reference is confidently voiced
  false_voiced_quiet     engine voiced where the reference is unvoiced and quiet
  false_voiced_loud      engine voiced on loud unvoiced frames (consonants, breath noise)
  octave_low / octave_high   both voiced, engine 9..15 st below / above the reference
  gross_other            both voiced, |error| > 1 st and not an octave (fifths etc.)
  octave_runs_30ms       separate octave-error stretches lasting >= 30 ms
  fine_median_ct / fine_p90_ct   |error| where it is below 1 st (includes pYIN's own
                         +1..+4 ct bias, VSTM_TUNE_AUDIT.md 6.1)
Output metrics (with --wet)
  level_diff_db          RMS of wet vs dry (latency removed)
  subharmonic_excess_db  median / p95 over voiced frames of (wet - dry) energy at half
                         the sung pitch relative to the pitch itself: an octave-low
                         detector makes 2-period grains and raises it (VSTM_TUNE_AUDIT.md R6)
"""
import argparse
import csv
import hashlib
import json
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

REF_SR = 22050
REF_HOP = 128          # 5.8 ms
REF_FRAME = 2048


def _sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()[:20]


def load_mono(path):
    y, sr = sf.read(str(path), always_2d=True, dtype="float64")
    return y.mean(axis=1), sr


def reference(path, cache_dir=None, fmin=50.0, fmax=1400.0):
    """pYIN + YIN reference of a dry recording; cached as .npz by content hash."""
    cache = None
    if cache_dir:
        cache = Path(cache_dir) / f"ref_{_sha(path)}_{int(fmin)}_{int(fmax)}.npz"
        if cache.exists():
            d = np.load(cache)
            return {k: d[k] for k in d.files}
    import librosa
    y, sr = load_mono(path)
    y22 = librosa.resample(y, orig_sr=sr, target_sr=REF_SR) if sr != REF_SR else y
    f0, vflag, vprob = librosa.pyin(y22, fmin=fmin, fmax=fmax, sr=REF_SR,
                                    frame_length=REF_FRAME, hop_length=REF_HOP)
    yin = librosa.yin(y22, fmin=fmin, fmax=fmax, sr=REF_SR, frame_length=REF_FRAME, hop_length=REF_HOP)
    rms = librosa.feature.rms(y=y22, frame_length=REF_FRAME, hop_length=REF_HOP)[0]
    n = min(len(f0), len(yin), len(rms))
    t = librosa.times_like(f0[:n], sr=REF_SR, hop_length=REF_HOP)
    out = {"t": t, "f0": f0[:n], "vflag": vflag[:n], "vprob": vprob[:n], "yin": yin[:n], "rms": rms[:n]}
    if cache is not None:
        cache.parent.mkdir(parents=True, exist_ok=True)
        np.savez_compressed(cache, **out)
    return out


def confident(ref, gate_db=40.0):
    f0, yin, rms = ref["f0"], ref["yin"], ref["rms"]
    loud = rms >= rms.max() * 10 ** (-gate_db / 20.0)
    ok = ref["vflag"].astype(bool) & (ref["vprob"] > 0.5) & np.isfinite(f0) & (yin > 0) & loud
    agree = np.zeros_like(ok)
    agree[ok] = np.abs(1200.0 * np.log2(yin[ok] / f0[ok])) < 100.0
    conf = ok & agree
    unv = (~ref["vflag"].astype(bool)) & (ref["vprob"] < 0.1)
    quiet = rms < rms.max() * 10 ** (-50.0 / 20.0)
    return conf, unv & quiet, unv & ~quiet


def read_hops(path):
    rows = list(csv.DictReader(open(path)))
    t = np.array([float(r["time_s"]) for r in rows])
    v = np.array([r["voiced"] == "1" for r in rows])
    mi = np.array([float(r["in_midi"]) for r in rows])
    mo = np.array([float(r["out_midi"]) for r in rows])
    return t, v, mi, mo


def _runs(mask, dt, min_s):
    n, cur = 0, 0
    total = 0.0
    for m in np.append(mask, False):
        if m:
            cur += 1
        else:
            if cur * dt >= min_s:
                n += 1
                total += cur * dt
            cur = 0
    return n, total


def evaluate(dry, hops, wet=None, latency=0, cache_dir=None, fmin=50.0, fmax=1400.0):
    ref = reference(dry, cache_dir, fmin, fmax)
    conf, quiet_unv, loud_unv = confident(ref)
    t = ref["t"]
    # pYIN is quantised to 10-cent bins; YIN (parabolic) refines it where both agree within 50 cents
    f0 = np.where(ref["f0"] > 0, ref["f0"], 1.0)
    yin = np.where(ref["yin"] > 0, ref["yin"], 1.0)
    fine_ref = np.where(np.abs(1200.0 * np.log2(yin / f0)) < 50.0, yin, f0)
    ref_midi = np.where(conf, 69.0 + 12.0 * np.log2(fine_ref / 440.0), np.nan)
    et, ev, emi, emo = read_hops(hops)

    def on_grid(off):
        idx = np.clip(np.searchsorted(et, t + off), 0, len(et) - 1)
        return idx

    # the engine reports each estimate a little after the middle of its analysis window
    best = (0.0, 1e9)
    for off in np.arange(0.0, 0.0801, 0.001):
        idx = on_grid(off)
        sel = conf & ev[idx]
        if sel.sum() < 20:
            continue
        e = float(np.median(np.abs(emi[idx][sel] - ref_midi[sel])))
        if e < best[1]:
            best = (float(off), e)
    off = best[0]
    idx = on_grid(off)
    eng_v = ev[idx]
    eng_m = emi[idx]
    both = conf & eng_v
    d = eng_m - ref_midi
    octave_low = both & (d < -9.0) & (d > -15.0)
    octave_high = both & (d > 9.0) & (d < 15.0)
    fine = both & (np.abs(d) <= 1.0)
    gross = both & ~fine & ~octave_low & ~octave_high
    dt = REF_HOP / REF_SR
    nb = max(1, int(both.sum()))
    runs, run_s = _runs(octave_low | octave_high, dt, 0.030)
    out = {
        "evidence": "MEASURED",
        "align_offset_ms": round(off * 1000.0, 1),
        "ref_confident_s": round(float(conf.sum()) * dt, 2),
        "voiced_recall": round(float(eng_v[conf].mean()) if conf.any() else 0.0, 4),
        "false_voiced_quiet": round(float(eng_v[quiet_unv].mean()) if quiet_unv.any() else 0.0, 4),
        "false_voiced_loud": round(float(eng_v[loud_unv].mean()) if loud_unv.any() else 0.0, 4),
        "octave_low": round(float(octave_low.sum()) / nb, 5),
        "octave_high": round(float(octave_high.sum()) / nb, 5),
        "gross_other": round(float(gross.sum()) / nb, 5),
        "octave_runs_30ms": runs,
        "octave_run_s": round(run_s, 3),
        "fine_median_ct": round(float(np.median(np.abs(d[fine])) * 100.0), 2) if fine.any() else None,
        "fine_p90_ct": round(float(np.percentile(np.abs(d[fine]), 90) * 100.0), 2) if fine.any() else None,
    }

    if wet:
        y, sr = load_mono(dry)
        w, sr2 = load_mono(wet)
        assert sr == sr2, "dry / wet sample rates differ"
        w = w[latency:latency + len(y)]
        if len(w) < len(y):
            w = np.concatenate([w, np.zeros(len(y) - len(w))])
        out["level_diff_db"] = round(float(20 * np.log10((np.std(w) + 1e-12) / (np.std(y) + 1e-12))), 2)
        shift = (emo - emi)[idx]                          # correction + transpose (octave errors cancel)
        win = 4096 if sr > 60000 else 2048
        hann = np.hanning(win)
        fr = np.fft.rfftfreq(win, 1.0 / sr)
        ex = []
        for k in np.flatnonzero(both & (np.arange(len(t)) % 4 == 0)):
            c = int(t[k] * sr)
            a, b = c - win // 2, c + win // 2
            if a < 0 or b > len(y):
                continue
            f_in = 440.0 * 2 ** ((ref_midi[k] - 69.0) / 12.0)
            f_out = f_in * 2 ** (shift[k] / 12.0)
            if f_out / 2 < 40.0 or f_in / 2 < 40.0:
                continue
            sd = np.abs(np.fft.rfft(y[a:b] * hann)) ** 2
            sw = np.abs(np.fft.rfft(w[a:b] * hann)) ** 2

            def rel(spec, f):
                sub = spec[(fr > f * 0.47) & (fr < f * 0.53)].sum()
                fun = spec[(fr > f * 0.94) & (fr < f * 1.06)].sum()
                return max(-60.0, 10 * np.log10((sub + 1e-20) / (fun + 1e-20)))     # floor: band-limited sources
            ex.append(rel(sw, f_out) - rel(sd, f_in))
        if ex:
            ex = np.array(ex)
            out["subharmonic_excess_db_median"] = round(float(np.median(ex)), 2)
            out["subharmonic_excess_db_p95"] = round(float(np.percentile(ex, 95)), 2)
            out["subharmonic_frames_over_6db"] = round(float(np.mean(ex > 6.0)), 4)
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("dry")
    p.add_argument("hops")
    p.add_argument("--wet")
    p.add_argument("--latency", type=int, default=0)
    p.add_argument("--cache")
    p.add_argument("--fmin", type=float, default=50.0)
    p.add_argument("--fmax", type=float, default=1400.0)
    p.add_argument("--json", action="store_true")
    a = p.parse_args()
    out = evaluate(a.dry, a.hops, a.wet, a.latency, a.cache, a.fmin, a.fmax)
    if a.json:
        print(json.dumps(out, indent=2))
    else:
        for k, v in out.items():
            print(f"{k:32s} {v}")


if __name__ == "__main__":
    sys.exit(main())
