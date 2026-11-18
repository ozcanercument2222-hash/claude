#!/usr/bin/env python3
"""Real-vocal regression matrix for VSTM Tune (evidence label: MEASURED).

    run_vocal_matrix.py --manifest vocals.json --probe build/VSTMTune_WavProbe --out DIR
                        [--settings hard,default,...] [--cache DIR] [--jobs 4]
                        [--baseline OLD_DIR/summary.json] [--keep-wav] [--only ID,...]

Every input of the manifest is rendered by VSTMTune_WavProbe with every named setting,
then measured against the dry input:
  detector   pitch_eval.py: voiced recall / false voicing, octave and gross errors
             against a cached pYIN+YIN reference
  artefacts  level difference, sample peak, 20-150 Hz bursts over the dry signal
             (thumps), 10 ms envelope jumps (clicks / dropouts), sub-octave excess
  pitch      out_vs_target: output pitch (YIN on the wet signal, a narrow search around
             the engine's own target) minus the target, where the target is steady
Results: DIR/runs/<id>__<setting>.json, DIR/summary.json and DIR/summary.md (per setting
and voice group). With --baseline, every aggregate is printed next to the old value.

Manifest: {"files": [{"id": "...", "path": "/abs/or/relative/to/manifest.wav",
                      "group": "female", "range": 1, "note": "..."}]}
"range" is the plugin's Vocal Range (0 soprano/alto, 1 tenor/mid, 2 bass/baritone).
This tool never writes next to the inputs; inputs are read only.
"""
import argparse
import json
import os
import subprocess
import sys
import time
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import signal

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pitch_eval  # noqa: E402

# Named settings: WavProbe name=value pairs (see tests/wav_probe.cpp)
SETTINGS = {
    "hard":      "key=0 scale=0 robotism=0 retune=0 humanize=0",
    "default":   "key=-1 scale=1 robotism=0.5 retune=15 humanize=0.35",
    "chroma50":  "key=0 scale=0 robotism=0.5 retune=15 humanize=0.35",
    "natural":   "key=0 scale=0 robotism=0.8 retune=15 humanize=0.35",
    "off":       "key=0 scale=0 amount=0",
    "tr+12":     "key=0 scale=0 robotism=0 retune=0 humanize=0 transpose=12",
    "tr-12":     "key=0 scale=0 robotism=0 retune=0 humanize=0 transpose=-12",
    "fm+12":     "key=0 scale=0 robotism=0 retune=0 humanize=0 formant=12",
    "fm-12":     "key=0 scale=0 robotism=0 retune=0 humanize=0 formant=-12",
    "tr-5fm-3":  "key=0 scale=0 robotism=0.5 retune=15 humanize=0.35 transpose=-5 formant=-3",
}
# Adaptive (engine after 1.0 only; older WavProbes reject the argument)
SETTINGS["default_ad"] = SETTINGS["default"] + " adaptive=1"
SETTINGS["natural_ad"] = SETTINGS["natural"] + " adaptive=1"
SETTINGS["hard_ad"] = SETTINGS["hard"] + " adaptive=1"
for _st in (3, 6, 9):                     # level calibration points (transpose / formant alone)
    for _sg in (1, -1):
        SETTINGS[f"tr{_sg * _st:+d}"] = f"key=0 scale=0 robotism=0 retune=0 humanize=0 transpose={_sg * _st}"
        SETTINGS[f"fm{_sg * _st:+d}"] = f"key=0 scale=0 robotism=0 retune=0 humanize=0 formant={_sg * _st}"


def band_db(y, sr, lo, hi, n=2048, hop=240):
    f, t, z = signal.stft(y, sr, nperseg=n, noverlap=n - hop, boundary=None, padded=False)
    b = (f >= lo) & (f <= hi)
    return t + n / 2 / sr, 10 * np.log10(np.sum(np.abs(z[b] * 2) ** 2, axis=0) + 1e-20)


def env_db(y, sr, win=0.010):
    n = int(win * sr)
    k = len(y) // n
    e = np.sqrt(np.mean(y[:k * n].reshape(k, n) ** 2, axis=1))
    return 20 * np.log10(e + 1e-9)


def yin_near(x, sr, start, length, expect_hz):
    """Normalised autocorrelation in 0.8..1.25 x the expected period (octave-blind on purpose)."""
    T = sr / expect_hz
    lo, hi = max(2, int(T * 0.80)), int(T * 1.25) + 2
    if start < 0 or start + length + hi + 2 >= len(x):
        return None
    a = x[start:start + length]
    best, bl, r = -2.0, lo, {}
    ea = float(np.dot(a, a)) + 1e-20
    for lag in range(lo - 1, hi + 2):
        b = x[start + lag:start + lag + length]
        r[lag] = float(np.dot(a, b)) / np.sqrt(ea * (float(np.dot(b, b)) + 1e-20))
    for lag in range(lo, hi + 1):
        if r[lag] > best:
            best, bl = r[lag], lag
    if best < 0.8:
        return None
    a_, b_, c_ = r[bl - 1], r[bl], r[bl + 1]
    den = a_ - 2 * b_ + c_
    p = bl + (0.5 * (a_ - c_) / den if abs(den) > 1e-12 else 0.0)
    return sr / p


def artefacts(dry_path, wet_path, latency, hops_path):
    dry, sr = pitch_eval.load_mono(dry_path)
    wet_full, _ = pitch_eval.load_mono(wet_path)
    wet = wet_full[latency:latency + len(dry)]
    if len(wet) < len(dry):
        wet = np.concatenate([wet, np.zeros(len(dry) - len(wet))])
    out = {}
    out["level_diff_db"] = round(float(20 * np.log10((np.std(wet) + 1e-12) / (np.std(dry) + 1e-12))), 2)
    out["peak_dry"] = round(float(np.max(np.abs(dry))), 4)
    out["peak_wet"] = round(float(np.max(np.abs(wet))), 4)
    t, ld = band_db(dry, sr, 20, 150)
    _, lw = band_db(wet, sr, 20, 150)
    _, l0 = band_db(dry, sr, 150, 4000)
    active = l0 > l0.max() - 45
    peaks, _ = signal.find_peaks(np.where(active, lw - ld, -99), height=15, distance=8)
    out["lf_bursts"] = int(len(peaks))
    out["lf_bursts_per_min"] = round(len(peaks) / (len(dry) / sr / 60.0), 2)
    ex = (lw - ld)[active]
    out["lf_excess_p99_db"] = round(float(np.percentile(ex, 99)), 1) if ex.size else None
    ed, ew = env_db(dry, sr), env_db(wet, sr)
    act = ed > ed.max() - 40
    jumps = (np.abs(np.diff(ew)) - np.abs(np.diff(ed)) > 3.0) & act[1:] & act[:-1]
    out["env_jumps"] = int(np.sum(jumps))
    out["env_jumps_per_min"] = round(float(np.sum(jumps)) / (len(dry) / sr / 60.0), 2)
    # bursts: 10 ms frames (dry loud: within 30 dB of its max) where the wet is > 6 dB louder than the dry
    e1d, e1w = env_db(dry, sr, 0.010), env_db(wet, sr, 0.010)
    loud1 = e1d > e1d.max() - 30
    if loud1.any():
        over = (e1w - e1d)[loud1]
        out["over_dry_6db_frames"] = int(np.sum(over > 6.0))
        out["over_dry_max_db"] = round(float(over.max()), 1)
    # level tracking: 50 ms envelopes of wet vs dry where the dry is loud (within 30 dB of its max);
    # a gain that rides or pumps shows up as spread of the difference around its median
    e5d, e5w = env_db(dry, sr, 0.050), env_db(wet, sr, 0.050)
    loud = e5d > e5d.max() - 30
    if loud.sum() > 10:
        dd = (e5w - e5d)[loud]
        dev = np.abs(dd - np.median(dd))
        out["env_track_sd_db"] = round(float(np.std(dd)), 3)
        out["env_track_p95_db"] = round(float(np.percentile(dev, 95)), 3)

    # output pitch vs the engine's own target where the target is steady (needs the hop log)
    et, ev, emi, emo = pitch_eval.read_hops(hops_path)
    errs = []
    step = max(1, int(round(0.010 / (et[1] - et[0])))) if len(et) > 1 else 1
    for k in range(6, len(et) - 6, step):
        if not ev[k - 6:k + 7].all() or np.ptp(emo[k - 6:k + 7]) > 0.01:
            continue
        # the hop is logged at the end of its analysis window; the grain made from it is output
        # about 20 ms (half the window) earlier on the input timeline -> centre ~ et - 0.02
        c = int((et[k] - 0.02) * sr) + latency
        target_hz = 440.0 * 2 ** ((emo[k] - 69.0) / 12.0)
        hz = yin_near(wet_full, sr, c - int(0.006 * sr), int(0.012 * sr), target_hz)
        if hz:
            errs.append(abs(1200.0 * np.log2(hz / target_hz)))
    if errs:
        out["out_vs_target_median_ct"] = round(float(np.median(errs)), 2)
        out["out_vs_target_p90_ct"] = round(float(np.percentile(errs, 90)), 2)
        out["out_vs_target_frames"] = len(errs)
    return out


def run_one(job):
    f, setting, probe, out_dir, cache, keep = job
    rid = f"{f['id']}__{setting}"
    res_path = Path(out_dir) / "runs" / f"{rid}.json"
    wav = Path(out_dir) / "wav" / f"{rid}.wav"
    hops = Path(out_dir) / "hops" / f"{rid}.csv"
    args = [probe, f["path"], str(wav), "--csv", str(hops)] + SETTINGS[setting].split() + [f"range={f.get('range', 1)}"]
    t0 = time.time()
    p = subprocess.run(args, capture_output=True, text=True)
    res = {"id": f["id"], "setting": setting, "group": f.get("group", ""), "args": " ".join(args[4:]),
           "probe_exit": p.returncode, "probe_out": p.stdout.strip()[-300:], "probe_err": p.stderr.strip()[-300:]}
    if p.returncode == 0:
        lat = int(p.stdout.split("latency ")[1].split(" ")[0])
        res["latency"] = lat
        res["detector"] = pitch_eval.evaluate(f["path"], str(hops), str(wav), lat, cache)
        res["artefacts"] = artefacts(f["path"], str(wav), lat, str(hops))
    res["seconds"] = round(time.time() - t0, 1)
    res_path.write_text(json.dumps(res, indent=1))
    if not keep and wav.exists():
        wav.unlink()
    return res


def aggregate(results):
    """Per setting and per group: duration-weighted means of rates, sums of counts."""
    keys_rate = ["voiced_recall", "false_voiced_quiet", "false_voiced_loud", "octave_low", "octave_high", "gross_other",
                 "fine_median_ct", "subharmonic_excess_db_median", "subharmonic_excess_db_p95"]
    keys_sum = ["octave_runs_30ms", "octave_run_s"]
    keys_art_mean = ["level_diff_db", "lf_bursts_per_min", "env_jumps_per_min", "out_vs_target_median_ct", "out_vs_target_p90_ct",
                     "env_track_sd_db", "env_track_p95_db"]
    keys_art_sum = ["lf_bursts", "env_jumps", "over_dry_6db_frames"]
    table = {}
    for r in results:
        if "detector" not in r:
            continue
        for grp in ("ALL", r["group"]):
            k = f"{r['setting']}|{grp}"
            e = table.setdefault(k, {"n": 0, "_w": 0.0, "_acc": {}, "_sum": {}, "_amean": {}, "_asum": {}, "_max": {}})
            w = max(0.01, r["detector"]["ref_confident_s"])
            e["n"] += 1
            e["_w"] += w
            for q in keys_rate:
                v = r["detector"].get(q)
                if v is not None:
                    a = e["_acc"].setdefault(q, [0.0, 0.0])
                    a[0] += w * v
                    a[1] += w
            for q in keys_sum:
                e["_sum"][q] = e["_sum"].get(q, 0) + r["detector"].get(q, 0)
            for q in keys_art_mean:
                v = r["artefacts"].get(q)
                if v is not None:
                    a = e["_amean"].setdefault(q, [0.0, 0])
                    a[0] += v
                    a[1] += 1
            for q in keys_art_sum:
                e["_asum"][q] = e["_asum"].get(q, 0) + r["artefacts"].get(q, 0)
            for q in ("level_diff_db",):
                v = r["artefacts"].get(q)
                if v is not None:
                    e["_max"]["abs_" + q] = max(e["_max"].get("abs_" + q, 0.0), abs(v))
            v = r["artefacts"].get("over_dry_max_db")
            if v is not None:
                e["_max"]["over_dry_max_db"] = max(e["_max"].get("over_dry_max_db", -99.0), v)
            pk = r["artefacts"].get("peak_wet", 0.0) / max(1e-9, r["artefacts"].get("peak_dry", 0.0))
            e["_max"]["peak_gain_max_db"] = max(e["_max"].get("peak_gain_max_db", -99.0), 20 * np.log10(max(pk, 1e-9)))
    out = {}
    for k, e in sorted(table.items()):
        row = {"n": e["n"], "confident_voiced_s": round(e["_w"], 1)}
        for q, (s, w) in e["_acc"].items():
            row[q] = round(s / w, 5)
        row.update({q: round(v, 3) for q, v in e["_sum"].items()})
        for q, (s, n) in e["_amean"].items():
            row["mean_" + q] = round(s / n, 3)
        row.update(e["_asum"])
        row.update({q: round(v, 2) for q, v in e["_max"].items()})
        out[k] = row
    return out


def markdown(summary, baseline=None):
    cols = ["n", "voiced_recall", "false_voiced_loud", "octave_low", "octave_high", "gross_other", "octave_runs_30ms",
            "fine_median_ct", "subharmonic_excess_db_p95", "mean_level_diff_db", "abs_level_diff_db", "lf_bursts", "env_jumps",
            "mean_out_vs_target_p90_ct"]
    lines = ["| setting / group | " + " | ".join(cols) + " |", "|---" * (len(cols) + 1) + "|"]
    for k, row in summary.items():
        cells = []
        for c in cols:
            v = row.get(c)
            s = "–" if v is None else (f"{v:.4g}" if isinstance(v, float) else str(v))
            if baseline and k in baseline and baseline[k].get(c) is not None and v is not None and c != "n":
                b = baseline[k][c]
                s += f" ({b:.4g})" if isinstance(b, float) else f" ({b})"
            cells.append(s)
        lines.append(f"| {k.replace('|', ' / ')} | " + " | ".join(cells) + " |")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--probe", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--settings", default="hard,default")
    ap.add_argument("--cache", default=None, help="reference cache folder (default: <out>/../ref_cache)")
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2)))
    ap.add_argument("--baseline")
    ap.add_argument("--keep-wav", action="store_true")
    ap.add_argument("--only", default="")
    a = ap.parse_args()

    man = json.loads(Path(a.manifest).read_text())
    base_dir = Path(a.manifest).resolve().parent
    files = []
    for f in man["files"]:
        f = dict(f)
        p = Path(f["path"])
        f["path"] = str(p if p.is_absolute() else base_dir / p)
        if a.only and f["id"] not in a.only.split(","):
            continue
        files.append(f)
    out = Path(a.out)
    for sub in ("runs", "wav", "hops"):
        (out / sub).mkdir(parents=True, exist_ok=True)
    cache = a.cache or str(out.parent / "ref_cache")
    settings = [s for s in a.settings.split(",") if s]
    for s in settings:
        if s not in SETTINGS:
            sys.exit(f"unknown setting {s}; known: {', '.join(SETTINGS)}")

    # references first (the slow part), one job per file, in parallel
    with ProcessPoolExecutor(max_workers=a.jobs) as ex:
        list(ex.map(pitch_eval.reference, [f["path"] for f in files], [cache] * len(files)))
    jobs = [(f, s, a.probe, str(out), cache, a.keep_wav) for f in files for s in settings]
    t0 = time.time()
    with ProcessPoolExecutor(max_workers=a.jobs) as ex:
        results = list(ex.map(run_one, jobs))
    failed = [r for r in results if r["probe_exit"] != 0]
    summary = aggregate(results)
    meta = {"evidence": "MEASURED", "probe": a.probe, "manifest": a.manifest, "settings": {s: SETTINGS[s] for s in settings},
            "files": len(files), "runs": len(results), "failed_runs": [r["id"] + "__" + r["setting"] for r in failed],
            "seconds": round(time.time() - t0, 1)}
    (out / "summary.json").write_text(json.dumps({"meta": meta, "summary": summary}, indent=1))
    baseline = None
    if a.baseline:
        baseline = json.loads(Path(a.baseline).read_text())["summary"]
    md = markdown(summary, baseline)
    (out / "summary.md").write_text(md + "\n")
    print(md)
    print(json.dumps(meta, indent=1))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
