#!/usr/bin/env python3
"""Ground-truth pitch corpus for VSTM Tune (evidence label of everything measured on it: MEASURED).

    gt_corpus.py resynth --manifest vocals.json --out DIR [--cache REF_CACHE] [--jobs 4] [--only id,...]
    gt_corpus.py synth   --out DIR [--rate 48000]

Real recordings have no exact pitch: a reference tracker (pYIN, 93 ms frames) is itself late and
smooth, so detector errors at note edges and in fast vibrato cannot be told apart from reference
errors (VSTM_TUNE_AUDIT.md 16.1). This tool makes signals whose pitch is known exactly.

resynth   analysis / resynthesis of real recordings (the MDB-stem-synth idea): the pitch contour of
          each recording (pYIN for robustness, refined by short-frame YIN for time resolution) and
          the singer's own harmonic amplitudes, frame by frame, drive a sum of harmonics whose
          frequency IS the contour. Breath / consonant noise is added back as spectrally smoothed,
          random-phase noise (aperiodic by construction). The ground truth is therefore exact:
          voiced where harmonics are synthesised, with exactly that f0; unvoiced elsewhere. The
          contour is the singer's (vibrato, slides, scoops, note changes); the timbre is the
          singer's (a weak fundamental stays weak). What it lacks: cycle-to-cycle jitter /
          shimmer, period doubling, and the stretches where pYIN was unsure (they become noise).
synth     parametric glottal-pulse voices with exact f0 and voicing for the cases the task lists:
          bass / fry (with jitter, shimmer, mild pulse alternation), male register, fast melodic
          runs, soprano / falsetto and register switches, 200 ct 6.5 Hz vibrato, 50-300 ct glides,
          portamento, consonant / transient bursts, noise at several SNRs, breath and silence.

Each case: DIR/<id>.wav (float32) + DIR/<id>.gt.npz (t on a 1 ms grid, f0 Hz with 0 = unvoiced,
voiced, level_db) and DIR/manifest.json ({"files": [{id, path, gt, group, range, tags, note}]}).
Inputs are only read; nothing is written next to them.
"""
import argparse
import json
import math
import os
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import ndimage, signal

GT_STEP = 0.001                       # ground-truth grid (s)


def midi_hz(m):
    return 440.0 * 2.0 ** ((np.asarray(m, dtype=float) - 69.0) / 12.0)


def hz_midi(f):
    return 69.0 + 12.0 * np.log2(np.asarray(f, dtype=float) / 440.0)


def write_case(out, cid, y, sr, t_gt, f0_gt, level_db, group, rng, tags, note):
    y = np.asarray(y, dtype=np.float32)
    peak = float(np.max(np.abs(y))) if y.size else 0.0
    if peak > 0.99:                                    # keep float WAVs inside full scale
        y = y * (0.99 / peak)
    wav = out / f"{cid}.wav"
    gt = out / f"{cid}.gt.npz"
    sf.write(str(wav), y, sr, subtype="FLOAT")
    np.savez_compressed(gt, t=t_gt.astype(np.float64), f0=f0_gt.astype(np.float64),
                        voiced=(f0_gt > 0), level_db=level_db.astype(np.float32))
    return {"id": cid, "path": str(wav), "gt": str(gt), "group": group, "range": int(rng), "tags": tags, "note": note,
            "seconds": round(len(y) / sr, 2), "rate": sr}


#==============================================================================
#  resynthesis of real recordings
#==============================================================================
def _runs(mask):
    """(start, end) index pairs of True runs."""
    m = np.concatenate([[False], mask.astype(bool), [False]])
    d = np.diff(m.astype(int))
    return list(zip(np.flatnonzero(d == 1), np.flatnonzero(d == -1)))


def clean_contour(t, f0, conf, hop_s):
    """Voiced = confident; isolated excursions (> 7 st from a 75 ms running median, shorter than
    ~37 ms) are removed (a reference blip, not singing); gaps <= 15 ms inside a phrase are bridged
    (voicing does not flicker every 6 ms); voiced runs shorter than 40 ms are dropped."""
    v = conf & np.isfinite(f0) & (f0 > 0)
    lm = np.where(v, np.log2(np.where(v, f0, 1.0)), np.nan)
    # running median over voiced frames only (NaN-aware)
    w = max(3, int(round(0.075 / hop_s)) | 1)
    pad = w // 2
    lp = np.pad(lm, pad, constant_values=np.nan)
    from numpy.lib.stride_tricks import sliding_window_view
    med = np.nanmedian(sliding_window_view(lp, w), axis=1) if np.any(v) else lm
    exc = v & (np.abs(lm - med) * 12.0 > 7.0)
    v = v & ~exc
    # bridge short gaps
    gap_max = int(round(0.015 / hop_s))
    for a, b in _runs(~v):
        if a > 0 and b < len(v) and (b - a) <= gap_max:
            la, lb = lm[a - 1], lm[b]
            if np.isfinite(la) and np.isfinite(lb) and abs(la - lb) * 12.0 < 2.0:
                v[a:b] = True
                lm[a:b] = np.linspace(la, lb, b - a + 2)[1:-1]
    # drop short voiced runs
    min_run = int(round(0.040 / hop_s))
    for a, b in _runs(v):
        if b - a < min_run:
            v[a:b] = False
    f = np.where(v, 2.0 ** np.where(np.isfinite(lm), lm, 0.0), 0.0)
    return f, v


def harmonic_amps(y, sr, frame_t, f0, fmax):
    """Amplitude of each harmonic k*f0 (k = 1..K) at each frame, from the recording's own spectrum:
    Hann frames centred on the contour frames (93 ms below 150 Hz so close harmonics stay resolved,
    46 ms above), magnitude of the bin nearest to k*f0 (sine amplitude = 2 |X| / sum(w))."""
    kmax = int(fmax / 50.0) + 1
    amps = np.zeros((len(frame_t), kmax), dtype=np.float32)
    base = 2048 if sr <= 50000 else 4096
    for n_fft, sel in ((2 * base, (f0 > 0) & (f0 < 150.0)), (base, f0 >= 150.0)):
        idx = np.flatnonzero(sel)
        if idx.size == 0:
            continue
        win = np.hanning(n_fft)
        half = n_fft // 2
        yp = np.pad(y, (half, half))
        binw = sr / n_fft
        for c0 in range(0, idx.size, 1024):
            ii = idx[c0:c0 + 1024]
            centres = np.clip((frame_t[ii] * sr).astype(int), 0, len(y) - 1)
            fr = np.stack([yp[c:c + n_fft] for c in centres]) * win
            mag = np.abs(np.fft.rfft(fr, axis=1)) * 2.0 / win.sum()
            for r, i in enumerate(ii):
                K = min(kmax, int(fmax / f0[i]))
                if K < 1:
                    continue
                kb = np.clip(np.rint(np.arange(1, K + 1) * f0[i] / binw).astype(int), 0, mag.shape[1] - 1)
                amps[i, :K] = mag[r, kb]
    return amps


def aperiodic(y, sr, frame_t, f0_frames, voiced_frames, n_fft):
    """Breath / consonant noise of the recording, made aperiodic: random phase and a spectrum smoothed
    across frequency (no harmonic comb survives, so it cannot look periodic to any detector)."""
    hop = n_fft // 4
    win = signal.get_window("hann", n_fft)
    f, tt, Z = signal.stft(y, sr, window=win, nperseg=n_fft, noverlap=n_fft - hop, boundary="even", padded=True)
    mag = np.abs(Z)
    binw = sr / n_fft
    fi = np.clip(np.searchsorted(frame_t, tt), 0, len(frame_t) - 1)
    out = np.zeros_like(mag)
    rng = np.random.default_rng(1234)
    for j in range(mag.shape[1]):
        col = mag[:, j]
        if voiced_frames[fi[j]]:
            # the floor between harmonics: low percentile over 3 harmonic spacings, 6 dB down
            wbin = max(9, int(3.0 * f0_frames[fi[j]] / binw) | 1)
            env = ndimage.percentile_filter(col, 20, size=wbin, mode="nearest") * 0.5
        else:
            wbin = max(9, int(1200.0 / binw) | 1)
            env = ndimage.uniform_filter1d(col, wbin, mode="nearest")
        out[:, j] = env
    ph = np.exp(2j * np.pi * rng.random(out.shape))
    _, x = signal.istft(out * ph, sr, window=win, nperseg=n_fft, noverlap=n_fft - hop, boundary=True)
    x = x[:len(y)]
    if len(x) < len(y):
        x = np.concatenate([x, np.zeros(len(y) - len(x))])
    return x


def real_contour(path, cache):
    """Pitch contour of a real recording: pYIN's Viterbi voicing (its posterior is often low in wide
    vibrato, so it is not required), loud, two independent YIN estimates (93 ms and 46 ms frames)
    agreeing with pYIN within 100 ct; the value is the 46 ms YIN (time resolution). Also returns
    the frames that are confidently unvoiced (pYIN unvoiced and its voicing probability < 0.1)."""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import pitch_eval
    import librosa
    y, sr = pitch_eval.load_mono(path)
    ref = pitch_eval.reference(path, cache)                            # pYIN + YIN, 93 ms, cached
    rms = ref["rms"]
    loud = rms >= rms.max() * 10 ** (-40.0 / 20.0)
    pf = np.where(ref["f0"] > 0, ref["f0"], 1.0)
    agree_long = np.abs(1200.0 * np.log2(np.maximum(ref["yin"], 1.0) / pf)) < 100.0
    conf = ref["vflag"].astype(bool) & np.isfinite(ref["f0"]) & (ref["f0"] > 0) & loud & agree_long
    unv = (~ref["vflag"].astype(bool)) & (ref["vprob"] < 0.1)
    t_ref = ref["t"]
    hop_ref = float(t_ref[1] - t_ref[0])
    y22 = librosa.resample(y, orig_sr=sr, target_sr=pitch_eval.REF_SR) if sr != pitch_eval.REF_SR else y
    yin_s = librosa.yin(y22, fmin=50.0, fmax=1400.0, sr=pitch_eval.REF_SR, frame_length=1024,
                        hop_length=pitch_eval.REF_HOP)
    n = min(len(t_ref), len(yin_s))
    t_ref, conf, unv, p_f0 = t_ref[:n], conf[:n], unv[:n], np.where(ref["f0"][:n] > 0, ref["f0"][:n], 1.0)
    yin_s = yin_s[:n]
    near = np.abs(1200.0 * np.log2(np.maximum(yin_s, 1.0) / p_f0)) < 100.0
    f_fine = np.where(near, yin_s, p_f0)
    f0c, vc = clean_contour(t_ref, f_fine, conf & near, hop_ref)
    return y, sr, t_ref, f0c, vc, unv & ~vc


def resynth_one(job):
    f, out_dir, cache = job
    out = Path(out_dir)
    y, sr, t_ref, f0c, vc, _ = real_contour(f["path"], cache)

    # per-sample f0 (log-linear interpolation inside voiced runs) and voicing
    ns = len(y)
    ts = np.arange(ns) / sr
    lf = np.where(vc, np.log2(np.maximum(f0c, 1.0)), np.nan)
    vs = np.interp(ts, t_ref, vc.astype(float)) > 0.999            # voiced only between two voiced frames
    lfs = np.interp(ts, t_ref, np.where(vc, lf, 0.0))
    f0s = np.where(vs, 2.0 ** lfs, 0.0)

    # harmonic amplitudes at the reference frames, then per sample. Each harmonic keeps one random
    # phase for the whole file: a voice-like crest factor, not the 0-phase pulse train of equal phases
    n_fft = 2048 if sr <= 50000 else 4096
    fmax = min(0.45 * sr, 8000.0)
    amps = harmonic_amps(y, sr, t_ref, np.where(vc, f0c, 0.0), fmax)
    theta = np.random.default_rng(4321).uniform(0.0, 2.0 * np.pi, amps.shape[1] + 1)
    ymix = np.zeros(ns)
    for a, b in _runs(vs):
        seg = slice(a, b)
        fseg = f0s[seg]
        phase = 2.0 * np.pi * np.cumsum(fseg) / sr
        L = b - a
        fade = np.ones(L)
        nf = min(L // 2, int(0.005 * sr))
        if nf > 0:
            ramp = 0.5 - 0.5 * np.cos(np.pi * np.arange(nf) / nf)
            fade[:nf] = ramp
            fade[L - nf:] = ramp[::-1]
        K = min(amps.shape[1], int(fmax / max(1.0, fseg.min())))
        acc = np.zeros(L)
        for k in range(1, K + 1):
            ak = np.interp(ts[seg], t_ref, amps[:, k - 1])
            ak = np.where(k * fseg < fmax, ak, 0.0)
            if not np.any(ak > 0):
                continue
            acc += ak * np.sin(k * phase + theta[k])
        ymix[seg] = acc * fade
    noise = aperiodic(y, sr, t_ref, np.where(vc, f0c, 0.0), vc, n_fft)
    yres = ymix + noise

    # ground truth on a 1 ms grid: f0 where harmonics are synthesised, harmonic level in dB
    t_gt = np.arange(0.0, ns / sr, GT_STEP)
    gi = np.clip((t_gt * sr).astype(int), 0, ns - 1)
    f0_gt = f0s[gi]
    env = np.sqrt(np.maximum(ndimage.uniform_filter1d(ymix ** 2, int(0.010 * sr)), 0.0) + 1e-20)
    level = 20.0 * np.log10(env[gi])
    cid = "rs_" + f["id"]
    tags = ["resynth", f.get("group", ""), f.get("technique", "")]
    note = f"resynthesis of {Path(f['path']).name}: real contour and harmonic spectrum, exact f0; aperiodic noise"
    return write_case(out, cid, yres, sr, t_gt, f0_gt, level, "rs_" + f.get("group", ""), f.get("range", 1),
                      [x for x in tags if x], note)


def cmd_resynth(a):
    man = json.loads(Path(a.manifest).read_text())
    files = [f for f in man["files"] if not a.only or f["id"] in a.only.split(",")]
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    jobs = [(f, str(out), a.cache) for f in files]
    with ProcessPoolExecutor(max_workers=a.jobs) as ex:
        entries = list(ex.map(resynth_one, jobs))
    write_manifest(out, entries, "resynth")


#==============================================================================
#  parametric synthetic voices
#==============================================================================
VOWELS = {
    "a": ((730, 90, 1.0), (1090, 110, 0.7), (2440, 170, 0.35), (3300, 250, 0.15)),
    "o": ((500, 80, 1.0), (800, 100, 0.6), (2600, 160, 0.2), (3300, 250, 0.1)),
    "u": ((320, 70, 1.0), (800, 100, 0.3), (2300, 160, 0.12), (3300, 250, 0.06)),
    "i": ((300, 70, 1.0), (2300, 120, 0.5), (3000, 180, 0.4), (3700, 250, 0.15)),
    "e": ((450, 80, 1.0), (1900, 110, 0.55), (2600, 170, 0.35), (3400, 250, 0.15)),
}


def resonator(x, sr, f, bw):
    r = math.exp(-math.pi * bw / sr)
    return signal.lfilter([(1.0 - r) * 2.0], [1.0, -2.0 * r * math.cos(2.0 * math.pi * f / sr), r * r], x)


def glottal_voice(sr, f0, voiced, vowel="a", tilt=1.0, jitter=0.0, shimmer=0.0, alternation=0.0, breath=0.0,
                  seed=7):
    """Pulse-by-pulse glottal source (Rosenberg-like pulses, one per period, so jitter / shimmer /
    alternation are real cycle-to-cycle changes) through 4 formants. f0 (Hz per sample) is the
    ground truth: each period starts where the phase of the nominal f0 crosses an integer, then
    jitter moves the pulse by a fraction of its period (the label stays the nominal f0).
    tilt > 1 steepens the source (falsetto ~2.6: few harmonics, strong fundamental)."""
    n = len(f0)
    rng = np.random.default_rng(seed)
    src = np.zeros(n)
    ph = np.cumsum(np.where(voiced, f0, 0.0)) / sr
    pulse_idx = 0
    starts = np.flatnonzero(np.diff(np.floor(ph)) > 0) + 1
    for s in starts:
        if not voiced[s]:
            continue
        T = sr / max(f0[s], 20.0)
        amp = max(0.05, 1.0 + shimmer * rng.standard_normal())
        if alternation > 0.0:
            amp *= 1.0 - alternation if pulse_idx % 2 else 1.0
        pulse_idx += 1
        L = max(4, int(T * 0.6))                       # open phase 60 % of the period
        tt = np.arange(L) / L
        g = np.where(tt < 0.66, 0.5 - 0.5 * np.cos(np.pi * tt / 0.66), np.cos(np.pi * (tt - 0.66) / (2 * 0.34)))
        # jitter: each pulse moves by a random fraction of its period (the label stays the nominal f0)
        a = max(0, int(round(s + T * jitter * float(np.clip(rng.standard_normal(), -3.0, 3.0)))))
        b = min(n, a + L)
        if b > a:
            src[a:b] += amp * g[:b - a]
    d = np.diff(src, prepend=0.0)                      # flow derivative
    # spectral tilt: one-pole low-pass, steeper for falsetto-like sources
    pole = min(0.98, max(0.0, 1.0 - 2.0 * math.pi * 900.0 / (tilt * sr)))
    d = signal.lfilter([1.0 - pole], [1.0, -pole], d) if tilt > 1.0 else d
    y = np.zeros(n)
    for fr, bw, g in VOWELS[vowel]:
        if fr < 0.45 * sr:
            y += g * resonator(d, sr, fr, bw)
    if breath > 0.0:
        nz = rng.standard_normal(n)
        nz = signal.lfilter(*signal.butter(2, [300.0 / (sr / 2), min(0.95, 6000.0 / (sr / 2))], "band"), nz)
        env = ndimage.uniform_filter1d(np.abs(y), int(0.01 * sr)) + 1e-9
        y += breath * nz * env * 2.0
    return y


def noise_burst(n, sr, kind, rng):
    """Unvoiced material: plosive burst, fricative, breath."""
    x = rng.standard_normal(n)
    if kind == "plosive":
        env = np.exp(-np.arange(n) / (0.012 * sr))
        b, a = signal.butter(2, [1500.0 / (sr / 2), min(0.95, 8000.0 / (sr / 2))], "band")
        return signal.lfilter(b, a, x) * env
    if kind == "fricative":                            # "s" / "sh"
        b, a = signal.butter(4, 3500.0 / (sr / 2), "high")
        env = np.minimum(1.0, np.minimum(np.arange(n), n - np.arange(n)) / (0.015 * sr))
        return signal.lfilter(b, a, x) * env
    b, a = signal.butter(2, [400.0 / (sr / 2), min(0.95, 3000.0 / (sr / 2))], "band")   # breath
    env = np.minimum(1.0, np.minimum(np.arange(n), n - np.arange(n)) / (0.05 * sr))
    return signal.lfilter(b, a, x) * env


def notes_traj(t, notes, durs, glide=0.03, vib_ct=0.0, vib_hz=5.5, detune_ct=0.0, scoop_ct=0.0):
    """MIDI trajectory: raised-cosine glides of `glide` s between notes, optional vibrato (fading in
    over 0.25 s of each note), detuning and an onset scoop."""
    edges = np.concatenate([[0.0], np.cumsum(durs)])
    m = np.full_like(t, float(notes[0]))
    for i in range(1, len(notes)):
        a = np.clip((t - edges[i] + glide / 2) / max(glide, 1e-4), 0.0, 1.0)
        m += (notes[i] - notes[i - 1]) * (0.5 - 0.5 * np.cos(np.pi * a))
    idx = np.clip(np.searchsorted(edges, t, side="right") - 1, 0, len(notes) - 1)
    age = t - edges[idx]
    if vib_ct:
        m += vib_ct / 100.0 * np.sin(2 * np.pi * vib_hz * t) * np.clip((age - 0.1) / 0.25, 0.0, 1.0)
    if scoop_ct:
        m -= scoop_ct / 100.0 * np.exp(-age / 0.05)
    return m + detune_ct / 100.0


def synth_cases(sr):
    """(id, group, range, tags, note, builder) for every parametric case."""
    C = []

    def add(cid, group, rng, tags, note, fn):
        C.append((cid, group, rng, tags, note, fn))

    # --- bass / fry, 80-120 Hz and below (weak fundamental vowels o / u / a)
    for vow in ("a", "o", "u"):
        add(f"sy_bass_line_{vow}", "sy_bass", 2, ["bass", "melody"], "bass melody E2-A2-C3-G2, light vibrato",
            lambda t, v=vow: (notes_traj(t, [40, 45, 48, 43, 41, 45, 40], [0.6] * 7, 0.04, 30, 5.5, 20), v, dict(jitter=0.005, shimmer=0.03)))
    add("sy_bass_fry", "sy_bass", 2, ["bass", "fry"], "vocal fry 48-70 Hz: jitter 3 %, shimmer 12 %, pulse alternation 15 %",
        lambda t: (notes_traj(t, [31, 34, 32, 36, 33], [0.8] * 5, 0.08), "a", dict(jitter=0.03, shimmer=0.12, alternation=0.15)))
    add("sy_bass_creak", "sy_bass", 2, ["bass", "creak"], "creaky voice ~60-90 Hz with strong alternation 30 %",
        lambda t: (notes_traj(t, [36, 38, 41, 37], [0.9] * 4, 0.06), "o", dict(jitter=0.02, shimmer=0.08, alternation=0.30)))
    add("sy_bass_low_default_range", "sy_bass", 1, ["bass", "default-range"], "bass line 82-131 Hz, default Tenor/Mid range",
        lambda t: (notes_traj(t, [40, 43, 47, 45, 48, 43], [0.6] * 6, 0.04, 30, 5.5), "a", dict(jitter=0.005, shimmer=0.03)))
    # --- bass fast vibrato (P0-B): 200 ct at 6.5 Hz, also 150 / 100 ct
    for depth, rate in ((200, 6.5), (150, 6.0), (100, 5.5), (200, 7.5)):
        for vow in ("a", "o"):
            add(f"sy_bassvib_{depth}_{rate}_{vow}", "sy_bassvib", 2, ["bass", "vibrato"], f"bass G2 +-{depth} ct {rate} Hz",
                lambda t, d=depth, r=rate, v=vow: (43 + d / 100.0 * np.sin(2 * np.pi * r * t) * np.minimum(1.0, t / 0.3), v,
                                                   dict(jitter=0.004, shimmer=0.02)))
            add(f"sy_bassvib_{depth}_{rate}_{vow}_r1", "sy_bassvib", 1, ["bass", "vibrato", "default-range"],
                f"bass A2 +-{depth} ct {rate} Hz, default range",
                lambda t, d=depth, r=rate, v=vow: (45 + d / 100.0 * np.sin(2 * np.pi * r * t) * np.minimum(1.0, t / 0.3), v,
                                                   dict(jitter=0.004, shimmer=0.02)))
    # --- normal male 100-250 Hz
    for vow in ("a", "e", "o"):
        add(f"sy_male_melody_{vow}", "sy_male", 1, ["male", "melody"], "male melody 110-220 Hz, vibrato 40 ct",
            lambda t, v=vow: (notes_traj(t, [45, 50, 52, 53, 55, 57, 52, 48, 50, 45], [0.45] * 10, 0.05, 40, 5.8, 15, 40), v,
                              dict(jitter=0.006, shimmer=0.03, breath=0.02)))
    # --- fast melodic passages 200-500 Hz (runs of 90-150 ms notes)
    run = [60, 62, 64, 65, 67, 69, 71, 72, 71, 69, 67, 65, 64, 62, 60, 64, 67, 72, 67, 64, 60]
    for vow, nd in (("a", 0.09), ("i", 0.12), ("e", 0.15)):
        add(f"sy_fast_run_{vow}_{int(nd * 1000)}", "sy_fast", 1, ["fast", "melody"], f"fast run C4-C5, {int(nd * 1000)} ms notes",
            lambda t, v=vow, d=nd: (notes_traj(t, run, [d] * len(run), 0.02), v, dict(jitter=0.005, shimmer=0.03)))
    # --- soprano / falsetto and register switches (octave leaps)
    add("sy_soprano_melody_i", "sy_soprano", 0, ["soprano", "melody"], "soprano 520-1050 Hz, vibrato 60 ct",
        lambda t: (notes_traj(t, [72, 76, 79, 84, 81, 77, 74, 72], [0.5] * 8, 0.04, 60, 6.0), "i", dict(jitter=0.004, shimmer=0.02)))
    add("sy_falsetto", "sy_soprano", 0, ["falsetto"], "male falsetto 330-660 Hz: steep source, breathy",
        lambda t: (notes_traj(t, [64, 67, 71, 76, 72, 69], [0.55] * 6, 0.05, 30, 5.5), "u", dict(tilt=2.6, breath=0.06, jitter=0.004)))
    add("sy_register_switch_tenor", "sy_register", 1, ["register", "octave-leap"], "chest A3 <-> falsetto A4 octave leaps",
        lambda t: (notes_traj(t, [57, 69, 57, 69, 64, 76, 64], [0.6] * 7, 0.04, 25, 5.5), "a", dict(jitter=0.005, shimmer=0.03)))
    add("sy_register_switch_sop", "sy_register", 0, ["register", "octave-leap"], "soprano A4 <-> A5 octave leaps, vowel i",
        lambda t: (notes_traj(t, [69, 81, 69, 81, 76, 88, 76], [0.6] * 7, 0.03, 25, 6.0), "i", dict(jitter=0.004)))
    add("sy_register_switch_bass", "sy_register", 2, ["register", "octave-leap", "bass"], "bass G2 <-> G3 octave leaps",
        lambda t: (notes_traj(t, [43, 55, 43, 55, 48, 60, 48], [0.6] * 7, 0.04, 25, 5.5), "o", dict(jitter=0.005, shimmer=0.03)))
    # --- vibrato tenor / alto / soprano
    for name, base, rng_ in (("tenor", 55, 1), ("alto", 64, 0), ("soprano", 74, 0)):
        for depth, rate in ((200, 6.5), (100, 5.5), (50, 5.5)):
            add(f"sy_vib_{name}_{depth}_{rate}", "sy_vibrato", rng_, ["vibrato", name], f"{name} +-{depth} ct {rate} Hz",
                lambda t, b=base, d=depth, r=rate: (b + d / 100.0 * np.sin(2 * np.pi * r * t) * np.minimum(1.0, t / 0.3), "a",
                                                    dict(jitter=0.004, shimmer=0.02)))
    # --- glides 50-300 ct (and portamento 5-12 st)
    for name, base, rng_ in (("bass", 45, 2), ("tenor", 55, 1), ("soprano", 72, 0)):
        for size in (50, 100, 200, 300):
            for dur in (0.06, 0.2):
                steps = [size, -size, size, -size]
                add(f"sy_glide_{name}_{size}ct_{int(dur * 1000)}ms", "sy_glide", rng_, ["glide", name],
                    f"{name} {size} ct glides of {int(dur * 1000)} ms",
                    lambda t, b=base, s=steps, d=dur: (notes_traj(t, np.cumsum([b * 100] + s) / 100.0, [0.6] * 5, d, 0, 5.5), "a",
                                                        dict(jitter=0.004, shimmer=0.02)))
        add(f"sy_portamento_{name}", "sy_portamento", rng_, ["portamento", name], f"{name} portamento 5/7/12 st, 80-250 ms",
            lambda t, b=base: (notes_traj(t, [b, b + 5, b - 2, b + 10, b - 2, b + 7], [0.7] * 6, 0.15, 25, 5.5), "a",
                               dict(jitter=0.004, shimmer=0.02)))
    return C


def build_synth(sr, cid, fn, secs):
    t = np.arange(int(secs * sr)) / sr
    midi, vowel, kw = fn(t)
    f0 = midi_hz(midi)
    voiced = np.ones_like(t, dtype=bool)
    voiced[: int(0.05 * sr)] = False                   # 50 ms of silence, then the voice
    y = glottal_voice(sr, f0, voiced, vowel=vowel, **kw)
    return t, f0, voiced, y


def cmd_synth(a):
    sr = a.rate
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    entries = []
    rng = np.random.default_rng(99)
    for cid, group, rng_, tags, note, fn in synth_cases(sr):
        # duration from the trajectory builder: every case is <= 4.5 s
        secs = 4.4
        t, f0, voiced, y = build_synth(sr, cid, fn, secs)
        y = 0.3 * y / (np.max(np.abs(y)) + 1e-12)
        entries.append(finish(out, cid, y, sr, t, f0, voiced, group, rng_, tags, note))

    # --- consonant / transient heavy: syllables = noise burst + vowel, unvoiced labelled
    for name, base, rng_ in (("male", 50, 1), ("female", 64, 0), ("bass", 43, 2)):
        n = int(5.0 * sr)
        t = np.arange(n) / sr
        f0 = np.zeros(n)
        voiced = np.zeros(n, dtype=bool)
        y = np.zeros(n)
        pos = int(0.1 * sr)
        notes = [base, base + 2, base + 4, base + 5, base + 7, base + 4, base + 2, base]
        kinds = ["plosive", "fricative", "plosive", "breath", "fricative", "plosive", "fricative", "plosive"]
        for i, (mn, kd) in enumerate(zip(notes, kinds)):
            cl = int((0.03 if kd == "plosive" else 0.12) * sr)
            y[pos:pos + cl] += 0.08 * noise_burst(cl, sr, kd, rng)[: max(0, min(cl, n - pos))]   # ~10 dB under the vowels
            pos += cl + int(0.005 * sr)
            vl = int(0.22 * sr)
            if pos + vl >= n:
                break
            tt = t[pos:pos + vl]
            mid = notes_traj(tt - tt[0], [mn], [1.0], 0.02, 20, 5.5, 0, 60)
            fv = midi_hz(mid)
            vv = np.ones(vl, dtype=bool)
            seg = glottal_voice(sr, fv, vv, vowel="a" if i % 2 else "e", jitter=0.006, shimmer=0.03, seed=i)
            ramp = np.minimum(1.0, np.minimum(np.arange(vl), vl - np.arange(vl)) / (0.01 * sr))
            y[pos:pos + vl] += 0.3 * seg / (np.max(np.abs(seg)) + 1e-12) * ramp
            f0[pos:pos + vl] = fv
            voiced[pos:pos + vl] = ramp > 0.5           # voiced label where the vowel is up
            pos += vl + int(0.06 * sr)
        entries.append(finish(out, f"sy_consonants_{name}", y, sr, t, np.where(voiced, f0, 0.0), voiced,
                              "sy_consonant", rng_, ["consonant", "transient"], "syllables: plosive / fricative / breath + vowel"))
    # --- noise / low SNR: the male melody with pink-ish noise at several SNRs
    t = np.arange(int(4.4 * sr)) / sr
    midi = notes_traj(t, [45, 50, 52, 53, 55, 57, 52, 48, 50, 45], [0.45] * 10, 0.05, 40, 5.8, 15, 40)
    f0 = midi_hz(midi)
    voiced = np.ones_like(t, dtype=bool)
    voiced[: int(0.05 * sr)] = False
    clean = glottal_voice(sr, f0, voiced, vowel="a", jitter=0.006, shimmer=0.03)
    clean = 0.3 * clean / np.max(np.abs(clean))
    nz = signal.lfilter([1.0], [1.0, -0.95], rng.standard_normal(len(t)))
    for snr in (30, 20, 10, 5):
        p_s = np.mean(clean[voiced] ** 2)
        p_n = np.mean(nz ** 2)
        y = clean + nz * math.sqrt(p_s / p_n / 10 ** (snr / 10.0))
        entries.append(finish(out, f"sy_noise_snr{snr}", y, sr, t, f0, voiced, "sy_noise", 1, ["noise", f"snr{snr}"],
                              f"male melody + red noise at {snr} dB SNR"))
    # --- breath and silence between phrases (unvoiced must stay unvoiced)
    n = int(4.4 * sr)
    t = np.arange(n) / sr
    y = np.zeros(n)
    f0 = np.zeros(n)
    voiced = np.zeros(n, dtype=bool)
    for k, (a0, a1) in enumerate(((0.3, 1.2), (2.0, 2.9), (3.4, 4.2))):
        i0, i1 = int(a0 * sr), int(a1 * sr)
        tt = t[i0:i1] - a0
        fv = midi_hz(notes_traj(tt, [57 + 2 * k, 60 + 2 * k], [0.45, 0.45], 0.05, 30))
        seg = glottal_voice(sr, fv, np.ones(i1 - i0, dtype=bool), vowel="a", jitter=0.006, shimmer=0.03, seed=k)
        ramp = np.minimum(1.0, np.minimum(np.arange(i1 - i0), i1 - i0 - np.arange(i1 - i0)) / (0.02 * sr))
        y[i0:i1] = 0.3 * seg / np.max(np.abs(seg)) * ramp
        f0[i0:i1] = fv
        voiced[i0:i1] = ramp > 0.5
    for a0, a1 in ((1.3, 1.9), (2.95, 3.35)):          # audible inhales between phrases
        i0, i1 = int(a0 * sr), int(a1 * sr)
        y[i0:i1] += 0.03 * noise_burst(i1 - i0, sr, "breath", rng)
    entries.append(finish(out, "sy_breath_silence", y, sr, t, np.where(voiced, f0, 0.0), voiced, "sy_breath", 1,
                          ["breath", "silence"], "three phrases with silences and inhales"))
    write_manifest(out, entries, "synth")


def finish(out, cid, y, sr, t, f0, voiced, group, rng_, tags, note):
    t_gt = np.arange(0.0, len(y) / sr, GT_STEP)
    gi = np.clip((t_gt * sr).astype(int), 0, len(y) - 1)
    f0_gt = np.where(voiced[gi], f0[gi], 0.0)
    env = np.sqrt(np.maximum(ndimage.uniform_filter1d(np.asarray(y, float) ** 2, int(0.010 * sr)), 0.0) + 1e-20)
    return write_case(out, cid, y, sr, t_gt, f0_gt, 20.0 * np.log10(env[gi]), group, rng_, tags, note)


def write_manifest(out, entries, kind):
    man = {"kind": kind, "evidence": "MEASURED (signals with exact pitch ground truth)", "files": entries}
    (out / "manifest.json").write_text(json.dumps(man, indent=1))
    print(f"{len(entries)} cases -> {out / 'manifest.json'}")


def refgt_one(job):
    """Short-frame reference of a REAL recording (not ground truth: an independent estimate with
    better time resolution than the 93 ms pYIN). f0 > 0 voiced reference, 0 confidently unvoiced,
    -1 unknown (not judged)."""
    f, out_dir, cache = job
    out = Path(out_dir)
    y, sr, t_ref, f0c, vc, unv = real_contour(f["path"], cache)
    t_gt = np.arange(0.0, len(y) / sr, GT_STEP)
    v = np.interp(t_gt, t_ref, vc.astype(float)) > 0.999
    lf = np.interp(t_gt, t_ref, np.where(vc, np.log2(np.maximum(f0c, 1.0)), 0.0))
    u = np.interp(t_gt, t_ref, unv.astype(float)) > 0.999
    f0 = np.where(v, 2.0 ** lf, np.where(u, 0.0, -1.0))
    env = np.sqrt(np.maximum(ndimage.uniform_filter1d(y ** 2, int(0.010 * sr)), 0.0) + 1e-20)
    gi = np.clip((t_gt * sr).astype(int), 0, len(y) - 1)
    gt = out / f"ref_{f['id']}.gt.npz"
    np.savez_compressed(gt, t=t_gt, f0=f0, voiced=f0 > 0, level_db=(20.0 * np.log10(env[gi])).astype(np.float32))
    return {"id": f["id"], "path": f["path"], "gt": str(gt), "group": f.get("group", ""), "range": int(f.get("range", 1)),
            "tags": ["real", "reference"] + ([f["technique"]] if f.get("technique") else []),
            "note": "real recording; short-frame reference (46 ms YIN where pYIN and the 93 ms YIN agree), -1 = unknown",
            "seconds": round(len(y) / sr, 2), "rate": sr}


def cmd_refgt(a):
    man = json.loads(Path(a.manifest).read_text())
    files = [f for f in man["files"] if not a.only or f["id"] in a.only.split(",")]
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    with ProcessPoolExecutor(max_workers=a.jobs) as ex:
        entries = list(ex.map(refgt_one, [(f, str(out), a.cache) for f in files]))
    man_out = {"kind": "refgt", "evidence": "MEASURED vs an independent short-frame reference (NOT ground truth)",
               "files": entries}
    (out / "manifest.json").write_text(json.dumps(man_out, indent=1))
    print(f"{len(entries)} references -> {out / 'manifest.json'}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("resynth")
    r.add_argument("--manifest", required=True)
    r.add_argument("--out", required=True)
    r.add_argument("--cache", default=None)
    r.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2)))
    r.add_argument("--only", default="")
    g = sub.add_parser("refgt")
    g.add_argument("--manifest", required=True)
    g.add_argument("--out", required=True)
    g.add_argument("--cache", default=None)
    g.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2)))
    g.add_argument("--only", default="")
    s = sub.add_parser("synth")
    s.add_argument("--out", required=True)
    s.add_argument("--rate", type=int, default=48000)
    a = ap.parse_args()
    {"resynth": cmd_resynth, "refgt": cmd_refgt, "synth": cmd_synth}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
