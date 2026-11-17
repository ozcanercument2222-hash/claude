#!/usr/bin/env python3
"""Measurement and visual inspection for plugin renders.

Evidence label for everything here: MEASURED. Plots are for visual inspection
(an agent can open the PNGs), not listening.

  stats        LUFS, sample/true peak, RMS, crest, DC, clipping, stereo correlation
  align        latency of <test> relative to <ref> (cross-correlation)
  spectrogram  stacked log-frequency spectrograms (+ wet-minus-dry difference panel)
  pitch        pYIN f0 contour vs semitone grid + tuning/warble statistics
  pack         latency-aligned, loudness-matched listening pack with a blind A/B key

Requires numpy, scipy, soundfile, librosa, matplotlib, pyloudnorm.
"""
import argparse
import json
import random
import sys
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import signal

EVIDENCE = "MEASURED"


def load(path, start=0.0, dur=None, mono=False):
    sr = sf.info(str(path)).samplerate
    frames = -1 if dur is None else int(round(dur * sr))
    y, sr = sf.read(str(path), start=int(round(start * sr)), frames=frames,
                    dtype="float64", always_2d=True)
    return (y.mean(axis=1) if mono else y), sr


def db(x, floor=1e-12):
    return float(20.0 * np.log10(max(float(x), floor)))


def lufs(y, sr):
    import pyloudnorm
    if y.shape[0] < int(0.4 * sr):
        return None
    v = pyloudnorm.Meter(sr).integrated_loudness(y)
    return None if not np.isfinite(v) else float(v)


def true_peak(y):
    up = signal.resample_poly(y, 4, 1, axis=0)
    return float(np.max(np.abs(up))) if up.size else 0.0


def latency(ref, test, sr, max_ms=1000.0):
    """Samples by which `test` lags `ref` (positive = test is late)."""
    a, b = ref - ref.mean(), test - test.mean()
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]
    corr = signal.correlate(b, a, mode="full", method="fft")
    lags = signal.correlation_lags(len(b), len(a), mode="full")
    lim = int(max_ms * sr / 1000.0)
    keep = np.abs(lags) <= lim
    corr, lags = corr[keep], lags[keep]
    i = int(np.argmax(np.abs(corr)))
    denom = np.sqrt(np.sum(a * a) * np.sum(b * b)) or 1.0
    return int(lags[i]), float(corr[i] / denom)


def shift(y, lag):
    """Remove `lag` samples of delay (positive lag = y is late)."""
    if lag > 0:
        return np.concatenate([y[lag:], np.zeros((lag,) + y.shape[1:])])
    if lag < 0:
        return np.concatenate([np.zeros((-lag,) + y.shape[1:]), y[:lag]])
    return y


def file_stats(path, start, dur):
    y, sr = load(path, start, dur)
    mono = y.mean(axis=1)
    peak = float(np.max(np.abs(y))) if y.size else 0.0
    rms = float(np.sqrt(np.mean(y * y))) if y.size else 0.0
    out = {
        "path": str(path), "sample_rate": sr, "channels": y.shape[1],
        "duration_s": round(y.shape[0] / sr, 4),
        "lufs_integrated": lufs(y, sr),
        "sample_peak_dbfs": round(db(peak), 2),
        "true_peak_dbtp": round(db(true_peak(y)), 2),
        "rms_dbfs": round(db(rms), 2),
        "crest_db": round(db(peak) - db(rms), 2) if rms > 0 else None,
        "dc_offset": float(mono.mean()) if mono.size else 0.0,
        "clipped_samples": int(np.sum(np.abs(y) >= 0.999)),
        "nan_or_inf": int(np.sum(~np.isfinite(y))),
    }
    if y.shape[1] == 2:
        l, r = y[:, 0], y[:, 1]
        sl, sr_ = np.std(l), np.std(r)
        out["lr_correlation"] = float(np.corrcoef(l, r)[0, 1]) if sl > 0 and sr_ > 0 else None
        mid, side = (l + r) / 2, (l - r) / 2
        out["dual_mono"] = bool(np.max(np.abs(side)) < 1e-9)
        out["side_to_mid_db"] = (round(db(np.std(side)) - db(np.std(mid)), 2)
                                 if np.std(mid) > 0 and not out["dual_mono"] else None)
        out["lr_balance_db"] = round(db(sl) - db(sr_), 2) if sl > 0 and sr_ > 0 else None
    return out


def cmd_stats(a):
    print(json.dumps({"evidence": EVIDENCE,
                      "files": [file_stats(p, a.start, a.dur) for p in a.wav]}, indent=2))


def cmd_align(a):
    r, sr = load(a.ref, mono=True)
    t, sr2 = load(a.test, mono=True)
    if sr != sr2:
        sys.exit("sample rates differ; resample first")
    lag, c = latency(r, t, sr, a.max_ms)
    print(json.dumps({"evidence": EVIDENCE, "latency_samples": lag,
                      "latency_ms": round(1000.0 * lag / sr, 3),
                      "normalized_correlation": round(c, 4),
                      "note": "Low correlation (<0.5) means the estimate is unreliable "
                              "(heavy processing, pitch shift, or different material)."}, indent=2))


def stft_db(y, sr, n_fft=4096, hop=512):
    f, t, z = signal.stft(y, sr, window="hann", nperseg=n_fft, noverlap=n_fft - hop,
                          boundary=None, padded=False)
    mag = np.abs(z) * 2.0  # scipy already divides by the window sum; x2 = one-sided sine peak
    return f, t, 20.0 * np.log10(np.maximum(mag, 1e-10))


def cmd_spectrogram(a):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    sigs = []
    for p in a.wav:
        y, sr = load(p, a.start, a.dur, mono=True)
        sigs.append((Path(p).name, y, sr))
    diff = a.diff and len(sigs) == 2
    if diff:
        (_, y0, s0), (_, y1, s1) = sigs
        if s0 != s1:
            sys.exit("--diff needs equal sample rates")
        lag = a.latency if a.latency is not None else latency(y0, y1, s0)[0]
        y1 = shift(y1, lag)
        l0, l1 = lufs(y0, s0), lufs(y1, s1)
        if l0 is not None and l1 is not None:
            y1 = y1 * 10 ** ((l0 - l1) / 20.0)
        sigs[1] = (sigs[1][0] + f" (aligned {lag} smp, loudness-matched)", y1, s1)

    rows = len(sigs) + (1 if diff else 0)
    fig, axes = plt.subplots(rows, 1, figsize=(12, 3.2 * rows), sharex=True, squeeze=False)
    specs = []
    for ax, (name, y, sr) in zip(axes[:, 0], sigs):
        f, t, s = stft_db(y, sr, a.n_fft, a.hop)
        specs.append((f, t, s))
        m = ax.pcolormesh(t + a.start, np.maximum(f, 1), s, shading="auto", cmap="magma",
                          vmin=a.floor, vmax=0)
        ax.set_yscale("log")
        ax.set_ylim(20, sr / 2)
        ax.set_ylabel("Hz")
        ax.set_title(name, fontsize=9)
        fig.colorbar(m, ax=ax, label="dBFS")
    if diff:
        (f, t0, s0), (_, t1, s1) = specs
        n = min(s0.shape[1], s1.shape[1])
        d = s1[:, :n] - s0[:, :n]
        d[(s0[:, :n] < a.floor) & (s1[:, :n] < a.floor)] = 0.0
        ax = axes[-1, 0]
        m = ax.pcolormesh(t0[:n] + a.start, np.maximum(f, 1), d, shading="auto", cmap="RdBu_r",
                          vmin=-a.diff_range, vmax=a.diff_range)
        ax.set_yscale("log")
        ax.set_ylim(20, sigs[0][2] / 2)
        ax.set_ylabel("Hz")
        ax.set_title("difference: second minus first (red = added energy, blue = removed)", fontsize=9)
        fig.colorbar(m, ax=ax, label="dB")
    axes[-1, 0].set_xlabel("seconds")
    fig.tight_layout()
    fig.savefig(a.output, dpi=a.dpi)
    print(json.dumps({"evidence": EVIDENCE, "image": str(a.output)}))


def pitch_track(y, sr, fmin, fmax, gate_db=45.0):
    import librosa
    target = 22050
    if sr != target:
        y = librosa.resample(y, orig_sr=sr, target_sr=target)
    hop = 256
    # pYIN gives robust voicing and a smooth path, but its output is quantized to
    # 10-cent bins (finer bins break its voicing decision). YIN on the same frames
    # is continuous (parabolic interpolation); use it to refine pYIN where both agree
    # within 50 cents, so octave errors fall back to the pYIN value.
    f0, voiced, _ = librosa.pyin(y, fmin=fmin, fmax=fmax, sr=target, frame_length=2048,
                                 hop_length=hop)
    fine = librosa.yin(y, fmin=fmin, fmax=fmax, sr=target, frame_length=2048, hop_length=hop)
    n = min(len(f0), len(fine))
    f0, voiced, fine = f0[:n], voiced[:n], fine[:n]
    # Energy gate: pYIN happily "voices" breath and noise floor far below the vocal.
    rms = librosa.feature.rms(y=y, frame_length=2048, hop_length=hop)[0][:n]
    if rms.size and rms.max() > 0:
        voiced = voiced & (rms >= rms.max() * 10 ** (-gate_db / 20.0))
    ok = voiced & np.isfinite(f0) & (fine > 0)
    agree = np.zeros_like(ok)
    agree[ok] = np.abs(1200.0 * np.log2(fine[ok] / f0[ok])) < 50.0
    f0 = np.where(agree, fine, f0)
    t = librosa.times_like(f0, sr=target, hop_length=hop)
    return t, f0, voiced


def pitch_stats(t, f0, voiced, a4):
    v = voiced & np.isfinite(f0)
    if not np.any(v):
        return {"voiced_fraction": 0.0}
    midi = 69.0 + 12.0 * np.log2(f0[v] / a4)
    cents_off = 100.0 * (midi - np.round(midi))
    # Frame-to-frame motion within continuous voiced runs (warble/jitter indicator).
    steps = []
    idx = np.flatnonzero(v)
    m_all = np.full_like(f0, np.nan)
    m_all[v] = midi
    for i0, i1 in zip(idx[:-1], idx[1:]):
        if i1 == i0 + 1:
            steps.append(100.0 * (m_all[i1] - m_all[i0]))
    steps = np.abs(np.array(steps)) if steps else np.array([0.0])
    return {
        "voiced_fraction": round(float(np.mean(v)), 3),
        "median_abs_cents_from_nearest_semitone": round(float(np.median(np.abs(cents_off))), 2),
        "pct_frames_within_10_cents": round(100.0 * float(np.mean(np.abs(cents_off) <= 10)), 1),
        "pct_frames_within_25_cents": round(100.0 * float(np.mean(np.abs(cents_off) <= 25)), 1),
        "median_frame_step_cents": round(float(np.median(steps)), 2),
        "p95_frame_step_cents": round(float(np.percentile(steps, 95)), 2),
        "jumps_over_80_cents": int(np.sum(steps > 80)),
        "note": "Low cents-from-semitone = tight correction. Large p95 step / many jumps = "
                "abrupt transitions or warble. Pitch graphs cannot prove naturalness.",
    }


def cmd_pitch(a):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(12, 4.5))
    result = {"evidence": EVIDENCE, "a4_hz": a.a4, "files": []}
    rows = []
    for k, p in enumerate(a.wav):
        y, sr = load(p, a.start, a.dur, mono=True)
        t, f0, voiced = pitch_track(y, sr, a.fmin, a.fmax, a.gate_db)
        t = t + a.start
        if k > 0 and a.latency:
            t = t - a.latency / sr  # plot processed files on the dry file's timeline
        st = pitch_stats(t, f0, voiced, a.a4)
        st["path"] = str(p)
        result["files"].append(st)
        midi = np.where(voiced, 69.0 + 12.0 * np.log2(np.where(f0 > 0, f0, 1) / a.a4), np.nan)
        ax.plot(t, midi, lw=1.2, label=Path(p).name)
        for ti, mi in zip(t, midi):
            rows.append((Path(p).name, ti, mi))
    lo, hi = ax.get_ylim()
    for n in range(int(np.floor(lo)), int(np.ceil(hi)) + 1):
        ax.axhline(n, color="0.85", lw=0.6, zorder=0)
    ax.set_xlabel("seconds")
    ax.set_ylabel("MIDI note (grid = semitones)")
    ax.legend(fontsize=8)
    ax.set_title("pYIN f0 contour")
    fig.tight_layout()
    fig.savefig(a.output, dpi=a.dpi)
    result["image"] = str(a.output)
    if a.csv:
        with open(a.csv, "w") as fh:
            fh.write("file,time_s,midi\n")
            for name, ti, mi in rows:
                fh.write(f"{name},{ti:.4f},{'' if np.isnan(mi) else f'{mi:.3f}'}\n")
        result["csv"] = str(a.csv)
    print(json.dumps(result, indent=2))


def cmd_pack(a):
    dry, sr = load(a.dry, a.start, a.dur)
    wet, sr2 = load(a.wet, a.start, a.dur)
    if sr != sr2:
        sys.exit("sample rates differ; resample first")
    lag, corr = latency(dry.mean(axis=1), wet.mean(axis=1), sr)
    if a.latency is not None:
        lag = a.latency
    elif a.no_align:
        lag = 0
    wet = shift(wet, lag)
    n = min(len(dry), len(wet))
    dry, wet = dry[:n], wet[:n]
    ch = max(dry.shape[1], wet.shape[1])
    dry = np.repeat(dry, ch, axis=1) if dry.shape[1] == 1 and ch > 1 else dry
    wet = np.repeat(wet, ch, axis=1) if wet.shape[1] == 1 and ch > 1 else wet
    ld, lw = lufs(dry, sr), lufs(wet, sr)
    target = a.target_lufs if a.target_lufs is not None else ld
    gd = 10 ** ((target - ld) / 20.0) if ld is not None else 1.0
    gw = 10 ** ((target - lw) / 20.0) if lw is not None else 1.0
    dry, wet = dry * gd, wet * gw
    tp = max(true_peak(dry), true_peak(wet))
    ceiling = 10 ** (-1.0 / 20.0)
    safety = ceiling / tp if tp > ceiling else 1.0
    dry, wet = dry * safety, wet * safety
    fade = min(int(0.01 * sr), n // 4)
    if fade > 0:
        ramp = np.linspace(0.0, 1.0, fade)[:, None]
        for x in (dry, wet):
            x[:fade] *= ramp
            x[-fade:] *= ramp[::-1]

    out = Path(a.output)
    out.mkdir(parents=True, exist_ok=True)
    sf.write(out / "dry_matched.wav", dry, sr, subtype="PCM_24")
    sf.write(out / "wet_matched.wav", wet, sr, subtype="PCM_24")
    order = ["dry", "wet"]
    random.Random(a.seed).shuffle(order)
    for label, which in zip("AB", order):
        sf.write(out / f"blind_{label}.wav", dry if which == "dry" else wet, sr, subtype="PCM_24")
    key = {"A": order[0], "B": order[1]}
    meta = {
        "evidence": EVIDENCE, "dry": str(a.dry), "wet": str(a.wet), "start_s": a.start,
        "duration_s": round(n / sr, 4), "latency_compensated_samples": lag,
        "latency_correlation": round(corr, 4), "lufs_before": {"dry": ld, "wet": lw},
        "lufs_target": target, "gain_db": {"dry": round(db(gd * safety), 2),
                                           "wet": round(db(gw * safety), 2)},
        "peak_safety_db": round(db(safety), 2),
    }
    (out / "pack.json").write_text(json.dumps(meta, indent=2))
    (out / "blind_key.json").write_text(json.dumps(key, indent=2))
    (out / "LISTENING_SHEET.md").write_text(
        "# Listening sheet\n\n"
        "Both blind files are latency-aligned and loudness-matched. Do not open blind_key.json "
        "until you have written your answers.\n\n"
        "1. Which file (A/B) do you prefer, and why?\n"
        "2. Artifacts heard (where, in seconds): distortion, clicks, warble/metallic pitch, "
        "robotic steps, pumping, noise, harshness, dullness, stereo image change.\n"
        "3. Is the difference obvious, subtle, or not audible?\n"
        "4. Playback system (headphones/monitors) and level.\n")
    print(json.dumps({**meta, "output_dir": str(out),
                      "files": sorted(p.name for p in out.iterdir())}, indent=2))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    def window(sp):
        sp.add_argument("--start", type=float, default=0.0, help="excerpt start (s)")
        sp.add_argument("--dur", type=float, default=None, help="excerpt length (s)")

    s = sub.add_parser("stats")
    s.add_argument("wav", nargs="+")
    window(s)
    s.set_defaults(fn=cmd_stats)

    s = sub.add_parser("align")
    s.add_argument("ref")
    s.add_argument("test")
    s.add_argument("--max-ms", type=float, default=1000.0)
    s.set_defaults(fn=cmd_align)

    s = sub.add_parser("spectrogram")
    s.add_argument("wav", nargs="+")
    s.add_argument("-o", "--output", required=True)
    s.add_argument("--diff", action="store_true", help="with two files: add second-minus-first panel")
    s.add_argument("--n-fft", type=int, default=4096)
    s.add_argument("--hop", type=int, default=512)
    s.add_argument("--floor", type=float, default=-110.0)
    s.add_argument("--diff-range", type=float, default=12.0)
    s.add_argument("--latency", type=int, default=None,
                   help="known plugin latency in samples for --diff (default: cross-correlation)")
    s.add_argument("--dpi", type=int, default=110)
    window(s)
    s.set_defaults(fn=cmd_spectrogram)

    s = sub.add_parser("pitch")
    s.add_argument("wav", nargs="+")
    s.add_argument("-o", "--output", required=True)
    s.add_argument("--csv")
    s.add_argument("--fmin", type=float, default=65.0)
    s.add_argument("--fmax", type=float, default=1100.0)
    s.add_argument("--a4", type=float, default=440.0)
    s.add_argument("--gate-db", type=float, default=45.0,
                   help="ignore frames this far below the loudest frame (breath/noise)")
    s.add_argument("--latency", type=int, default=0,
                   help="plugin latency in samples, removed from every file after the first")
    s.add_argument("--dpi", type=int, default=110)
    window(s)
    s.set_defaults(fn=cmd_pitch)

    s = sub.add_parser("pack")
    s.add_argument("dry")
    s.add_argument("wet")
    s.add_argument("-o", "--output", required=True)
    s.add_argument("--target-lufs", type=float, default=None, help="default: dry's loudness")
    s.add_argument("--no-align", action="store_true")
    s.add_argument("--latency", type=int, default=None,
                   help="known plugin latency in samples (use instead of cross-correlation, "
                        "e.g. for pitch shifters where correlation is low)")
    s.add_argument("--seed", type=int, default=None)
    window(s)
    s.set_defaults(fn=cmd_pack)

    a = p.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
