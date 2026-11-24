#!/usr/bin/env python3
"""Detector accuracy against EXACT ground truth (evidence label: MEASURED).

    gt_eval.py run --manifest GT_DIR/manifest.json --probe VSTMTune_WavProbe --out DIR
                   [--setting hard] [--extra "name=value ..."] [--range case|0|1|2] [--jobs 4]
    gt_eval.py compare A/summary.json B/summary.json [--names a,b] [--md out.md] [--csv out.csv]

The manifest comes from gt_corpus.py (resynthesised real recordings and parametric voices whose f0
and voicing are known exactly). Every case is rendered by WavProbe; its hop log (--csv) holds the
engine's detected pitch per analysis hop and, since 1.2, the detector's own analysis delay (the
audio an estimate describes is centred that long before the hop). The estimate is compared with
the ground truth at exactly that time, so no per-file alignment search can hide errors. For older
WavProbes without the column, the delay is recomputed from the detector's frame layout (identical
in 1.0 - 1.2.0: window 256 decimated samples + tauMax).

Frames are scored where the ground truth is voiced or unvoiced for at least 15 ms on both sides
(the 30 ms around a voicing edge are not judged: an analysis window there sees both).
  f0 error         d = engine - truth (semitones), on frames where both are voiced
  fine             |d| <= 1 st: median and p95 in cents
  p95_all_ct       p95 of |d| over every both-voiced frame (gross errors included)
  octave_low/high  -15 < d < -9 / 9 < d < 15
  coarse           |d| > 1 st and not an octave; split 1-2.5 st (wrong / neighbour note),
                   2.5-9 st (fourth, fifth ...), >= 15 st
  oct_runs_20/30/50ms   separate octave-error stretches at least that long
  voiced_recall    engine voiced / truth voiced and loud (within 40 dB of the file's maximum)
  voiced_precision truth voiced / engine voiced
  false_voiced     engine voiced / truth unvoiced
  timing_resid_ms  offset (search -20 ... +40 ms) that would minimise the median |d|: what is left
                   after the engine's own delay compensation (0 = the delay is right)
  vib_depth_ratio  (vibrato cases) rms of the engine's pitch around its 0.4 s moving average divided
                   by the same for the truth: < 1 = the detector smooths the vibrato
"""
import argparse
import csv
import json
import os
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np

SETTINGS = {
    "hard": "key=0 scale=0 robotism=0 retune=0 humanize=0",
    "default": "key=-1 scale=1 robotism=0.5 retune=15 humanize=0.35",
    "chroma50": "key=0 scale=0 robotism=0.5 retune=15 humanize=0.35",
    "natural": "key=0 scale=0 robotism=0.8 retune=15 humanize=0.35",
}
EDGE_S = 0.015
MIN_HZ = {0: 110.0, 1: 72.0, 2: 50.0}


def read_hops(path):
    rows = list(csv.DictReader(open(path)))
    out = {k: np.array([float(r[k]) for r in rows]) for k in ("time_s", "voiced", "in_midi", "out_midi")}
    out["voiced"] = out["voiced"] > 0.5
    out["est_delay_s"] = np.array([float(r["est_delay_s"]) for r in rows]) if rows and "est_delay_s" in rows[0] else None
    return out


def fallback_delay(midi, sr, rng):
    """centreDelay of PitchDetector::analyse (1.0 - 1.2.0 frame layout), in seconds."""
    decim = max(1, int(round(sr / 11025.0)))
    dec_rate = sr / decim
    tau_max = min(1000, int(np.ceil(dec_rate / MIN_HZ[rng])) + 2)
    period = sr / (440.0 * 2.0 ** ((midi - 69.0) / 12.0))
    tau = period / decim
    return ((256 + tau_max) * decim - (256 + tau) * decim * 0.5) / sr


def runs_of(mask):
    m = np.concatenate([[False], mask, [False]]).astype(int)
    d = np.diff(m)
    return np.flatnonzero(d == 1), np.flatnonzero(d == -1)


def score(case, hops, rng, shift_s=0.0):
    g = np.load(case["gt"])
    t_gt, f0_gt, lvl = g["t"], g["f0"], g["level_db"].astype(float)
    step = float(t_gt[1] - t_gt[0])
    gv = f0_gt > 0
    known = f0_gt >= 0                          # a reference (refgt) marks frames it cannot judge with -1
    # frames at least EDGE_S away from a voicing change (or from an unknown stretch)
    k = int(round(EDGE_S / step))
    state = np.where(gv, 1, np.where(known, 0, 2))
    change = np.zeros(len(gv))
    change[1:] = np.diff(state) != 0
    edge = np.convolve(change, np.ones(2 * k + 1), "same") > 0
    loud = lvl >= np.max(lvl[gv]) - 40.0 if gv.any() else np.zeros_like(gv)

    sr = case["rate"]
    ts, ev, emi = hops["time_s"], hops["voiced"], hops["in_midi"]
    hop_s = float(np.median(np.diff(ts))) if len(ts) > 1 else 0.003
    if hops["est_delay_s"] is not None:
        dl = hops["est_delay_s"].copy()
        typical = float(np.median(dl[ev & (dl > 0)])) if np.any(ev & (dl > 0)) else 0.02
        dl = np.where(dl > 0, dl, typical)
    else:
        dl = np.where(ev, fallback_delay(emi, sr, rng), np.nan)
        typical = float(np.nanmedian(dl)) if np.any(ev) else 0.02
        dl = np.where(np.isfinite(dl), dl, typical)

    def at(offset):
        te = ts - dl - shift_s - offset
        gi = np.clip(np.rint(te / step).astype(int), 0, len(t_gt) - 1)
        valid = (te >= 0) & (te <= t_gt[-1])
        return gi, valid

    gi, valid = at(0.0)
    tv = gv[gi] & valid
    judged = valid & ~edge[gi] & known[gi]
    truth_midi = np.where(gv[gi], 69.0 + 12.0 * np.log2(np.maximum(f0_gt[gi], 1.0) / 440.0), np.nan)
    both = judged & tv & ev
    d = np.where(both, emi - truth_midi, np.nan)
    ad = np.abs(d)
    fine = both & (ad <= 1.0)
    olow = both & (d < -9.0) & (d > -15.0)
    ohigh = both & (d > 9.0) & (d < 15.0)
    coarse = both & ~fine & ~olow & ~ohigh
    nb = int(both.sum())
    res = {"id": case["id"], "group": case["group"], "n_both": nb, "hop_ms": round(hop_s * 1000, 3),
           "detector_delay_ms_median": round(typical * 1000.0, 2)}
    rv = judged & tv & loud[gi]
    res["n_truth_voiced_loud"] = int(rv.sum())
    res["n_truth_unvoiced"] = int((judged & ~tv).sum())
    res["voiced_recall"] = round(float(ev[rv].mean()), 5) if rv.any() else None
    jv = judged & ev
    res["voiced_precision"] = round(float(tv[jv].mean()), 5) if jv.any() else None
    ju = judged & ~tv
    res["false_voiced"] = round(float(ev[ju].mean()), 5) if ju.any() else None
    if nb:
        res["fine_median_ct"] = round(float(np.median(ad[fine]) * 100.0), 2) if fine.any() else None
        res["fine_p95_ct"] = round(float(np.percentile(ad[fine], 95) * 100.0), 2) if fine.any() else None
        res["p95_all_ct"] = round(float(np.percentile(ad[both], 95) * 100.0), 1)
        res["octave_low"] = round(float(olow.sum()) / nb, 6)
        res["octave_high"] = round(float(ohigh.sum()) / nb, 6)
        res["coarse"] = round(float(coarse.sum()) / nb, 6)
        res["coarse_1_2p5"] = round(float((coarse & (ad <= 2.5)).sum()) / nb, 6)
        res["coarse_2p5_9"] = round(float((coarse & (ad > 2.5) & (ad < 9.0)).sum()) / nb, 6)
        res["coarse_ge15"] = round(float((coarse & (ad >= 15.0)).sum()) / nb, 6)
        octs = olow | ohigh
        a, b = runs_of(octs)
        lens = (b - a) * hop_s
        for ms in (20, 30, 50):
            res[f"oct_runs_{ms}ms"] = int(np.sum(lens >= ms / 1000.0))
        res["oct_seconds"] = round(float(octs.sum()) * hop_s, 3)
        a, b = runs_of(coarse)
        lens = (b - a) * hop_s
        res["coarse_runs_30ms"] = int(np.sum(lens >= 0.030))
        # where the gross errors are: right after a truth note change (within 60 ms) or elsewhere
        tm = np.where(gv, 69.0 + 12.0 * np.log2(np.maximum(f0_gt, 1.0) / 440.0), np.nan)
        slope = np.abs(np.gradient(np.where(np.isfinite(tm), tm, 0.0), step))          # st / s
        w = int(round(0.06 / step))                                                     # > 20 st/s: a transition
        moving = np.convolve((slope > 20.0).astype(float), np.ones(2 * w + 1), "same") > 0
        near_change = moving[gi]
        res["coarse_near_transition"] = round(float((coarse & near_change).sum()) / nb, 6)
        res["gross_near_transition"] = round(float(((coarse | octs) & near_change).sum()) / nb, 6)
        # timing residual
        best = (0.0, float(np.nanmedian(ad[both])))
        for off in np.arange(-0.020, 0.0401, 0.001):
            gi2, valid2 = at(off)
            b2 = valid2 & gv[gi2] & ev & ~edge[gi2] & known[gi2]
            if b2.sum() < 20:
                continue
            tm2 = 69.0 + 12.0 * np.log2(np.maximum(f0_gt[gi2], 1.0) / 440.0)
            m = float(np.median(np.abs(emi[b2] - tm2[b2])))
            if m < best[1] - 1e-9:
                best = (float(off), m)
        res["timing_resid_ms"] = round(best[0] * 1000.0, 1)
        res["_fine_ct"] = (ad[fine] * 100.0).astype(np.float32).tolist()
        res["_all_ct"] = (ad[both] * 100.0).astype(np.float32).tolist()
        if "vibrato" in case.get("tags", []):
            n_avg = max(3, int(round(0.4 / hop_s)))
            ker = np.ones(n_avg) / n_avg
            sel = both & (ts > 0.6)
            if sel.sum() > 2 * n_avg:
                e = np.where(both, emi, np.nan)
                tr = np.where(both, truth_midi, np.nan)
                # moving averages over both-voiced frames only
                def detrend(x):
                    xm = np.where(np.isfinite(x), x, 0.0)
                    c = np.convolve(np.isfinite(x).astype(float), ker, "same")
                    return x - np.convolve(xm, ker, "same") / np.maximum(c, 1e-9)
                de, dt_ = detrend(e)[sel], detrend(tr)[sel]
                ok = np.isfinite(de) & np.isfinite(dt_) & (np.abs(de) < 3.0)
                if ok.sum() > 10:
                    res["vib_depth_ratio"] = round(float(np.sqrt(np.mean(de[ok] ** 2)) / max(1e-6, np.sqrt(np.mean(dt_[ok] ** 2)))), 3)
    return res


def run_one(job):
    case, probe, setting, extra, rng_mode, out_dir, keep, shift_s = job
    rng = case["range"] if rng_mode == "case" else int(rng_mode)
    out = Path(out_dir)
    hops = out / "hops" / f"{case['id']}.csv"
    wav = out / "wav" / f"{case['id']}.wav" if keep else Path(tempfile.mkstemp(suffix=".wav", dir=out / "tmp")[1])
    args = [probe, case["path"], str(wav), "--csv", str(hops)] + SETTINGS[setting].split() + extra.split() + [f"range={rng}"]
    t0 = time.time()
    p = subprocess.run(args, capture_output=True, text=True)
    res = {"id": case["id"], "group": case["group"], "probe_exit": p.returncode, "args": " ".join(args[5:])}
    if p.returncode == 0:
        res.update(score(case, read_hops(hops), rng, shift_s))
    else:
        res["probe_err"] = p.stderr[-300:]
    if not keep and wav.exists():
        wav.unlink()
    res["seconds"] = round(time.time() - t0, 2)
    (out / "runs" / f"{case['id']}.json").write_text(json.dumps(res))
    return res


RATE_KEYS = ["octave_low", "octave_high", "coarse", "coarse_1_2p5", "coarse_2p5_9", "coarse_ge15",
             "coarse_near_transition", "gross_near_transition"]
SUM_KEYS = ["oct_runs_20ms", "oct_runs_30ms", "oct_runs_50ms", "oct_seconds", "coarse_runs_30ms"]


def aggregate(results):
    groups = {}
    for r in results:
        if r.get("probe_exit") != 0 or not r.get("n_both"):
            continue
        for gname in ("ALL", r["group"].split("_")[0] + "_*", r["group"]):
            groups.setdefault(gname, []).append(r)
    out = {}
    for gname, rs in sorted(groups.items()):
        nb = sum(r["n_both"] for r in rs)
        row = {"n_cases": len(rs), "n_both_frames": nb}
        for k in RATE_KEYS:
            row[k] = round(sum(r.get(k, 0.0) * r["n_both"] for r in rs) / max(1, nb), 6)
        for k in SUM_KEYS:
            row[k] = round(sum(r.get(k, 0) for r in rs), 3)
        fine = np.concatenate([np.asarray(r.get("_fine_ct", []), dtype=float) for r in rs])
        allc = np.concatenate([np.asarray(r.get("_all_ct", []), dtype=float) for r in rs])
        row["fine_median_ct"] = round(float(np.median(fine)), 2) if fine.size else None
        row["fine_p95_ct"] = round(float(np.percentile(fine, 95)), 2) if fine.size else None
        row["median_all_ct"] = round(float(np.median(allc)), 2) if allc.size else None
        row["p95_all_ct"] = round(float(np.percentile(allc, 95)), 1) if allc.size else None
        # voicing: pooled over frames
        tvl = sum(r.get("n_truth_voiced_loud", 0) for r in rs)
        row["voiced_recall"] = round(sum((r.get("voiced_recall") or 0.0) * r.get("n_truth_voiced_loud", 0) for r in rs) / max(1, tvl), 5)
        tu = sum(r.get("n_truth_unvoiced", 0) for r in rs)
        row["false_voiced"] = round(sum((r.get("false_voiced") or 0.0) * r.get("n_truth_unvoiced", 0) for r in rs) / max(1, tu), 5)
        prec = [r["voiced_precision"] for r in rs if r.get("voiced_precision") is not None]
        row["voiced_precision_mean"] = round(float(np.mean(prec)), 5) if prec else None
        vib = [r["vib_depth_ratio"] for r in rs if r.get("vib_depth_ratio") is not None]
        if vib:
            row["vib_depth_ratio_median"] = round(float(np.median(vib)), 3)
            row["vib_depth_ratio_min"] = round(float(np.min(vib)), 3)
        tr = [r["timing_resid_ms"] for r in rs if r.get("timing_resid_ms") is not None]
        row["timing_resid_ms_median"] = round(float(np.median(tr)), 1) if tr else None
        dl = [r["detector_delay_ms_median"] for r in rs if r.get("detector_delay_ms_median") is not None]
        row["detector_delay_ms_median"] = round(float(np.median(dl)), 2) if dl else None
        out[gname] = row
    return out


def cmd_run(a):
    man = json.loads(Path(a.manifest).read_text())
    cases = [c for c in man["files"] if not a.only or c["id"] in a.only.split(",")]
    if a.group:
        cases = [c for c in cases if any(c["group"].startswith(g) for g in a.group.split(","))]
    out = Path(a.out)
    for sub in ("runs", "hops", "wav", "tmp"):
        (out / sub).mkdir(parents=True, exist_ok=True)
    jobs = [(c, a.probe, a.setting, a.extra, a.range, str(out), a.keep_wav, a.shift_ms / 1000.0) for c in cases]
    t0 = time.time()
    with ProcessPoolExecutor(max_workers=a.jobs) as ex:
        results = list(ex.map(run_one, jobs))
    failed = [r["id"] for r in results if r.get("probe_exit") != 0]
    summ = aggregate(results)
    meta = {"evidence": "MEASURED (exact ground truth)", "probe": a.probe, "manifest": a.manifest, "setting": a.setting,
            "extra": a.extra, "range": a.range, "shift_ms": a.shift_ms, "cases": len(cases), "failed": failed, "seconds": round(time.time() - t0, 1)}
    (out / "summary.json").write_text(json.dumps({"meta": meta, "summary": summ}, indent=1))
    print(json.dumps(meta))
    print(table({"this": summ}, ["this"]))
    return 1 if failed else 0


COLS = ["n_cases", "octave_low", "octave_high", "coarse", "coarse_1_2p5", "coarse_2p5_9", "gross_near_transition",
        "oct_runs_20ms", "oct_runs_30ms", "oct_runs_50ms", "fine_median_ct", "fine_p95_ct", "p95_all_ct",
        "voiced_recall", "false_voiced", "voiced_precision_mean", "vib_depth_ratio_median", "timing_resid_ms_median"]


def fmt(v, k):
    if v is None:
        return "–"
    if k in RATE_KEYS or k in ("voiced_recall", "false_voiced", "voiced_precision_mean"):
        return f"{100.0 * v:.3f}%"
    if isinstance(v, float):
        return f"{v:.4g}"
    return str(v)


def table(summaries, names, cols=COLS):
    groups = []
    for n in names:
        for gname in summaries[n]:
            if gname not in groups:
                groups.append(gname)
    lines = ["| group | build | " + " | ".join(cols) + " |", "|---|---" + "|---" * len(cols) + "|"]
    for gname in groups:
        for n in names:
            row = summaries[n].get(gname)
            if row is None:
                continue
            lines.append(f"| {gname} | {n} | " + " | ".join(fmt(row.get(c), c) for c in cols) + " |")
    return "\n".join(lines)


def cmd_compare(a):
    names = a.names.split(",") if a.names else [Path(p).parent.name for p in a.summaries]
    summaries = {n: json.loads(Path(p).read_text())["summary"] for n, p in zip(names, a.summaries)}
    md = table(summaries, names)
    print(md)
    if a.md:
        Path(a.md).write_text(md + "\n")
    if a.csv:
        with open(a.csv, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["group", "build"] + COLS)
            for gname in summaries[names[0]]:
                for n in names:
                    row = summaries[n].get(gname)
                    if row:
                        w.writerow([gname, n] + [row.get(c) for c in COLS])


def cmd_plot(a):
    """Engine pitch (at its own time stamps) against the truth for one case; several runs overlaid."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    man = json.loads(Path(a.manifest).read_text())
    case = [c for c in man["files"] if c["id"] == a.id][0]
    g = np.load(case["gt"])
    t0, t1 = (float(x) for x in a.window.split(",")) if a.window else (0.0, float(g["t"][-1]))
    fig, ax = plt.subplots(figsize=(13, 4.5))
    sel = (g["t"] >= t0) & (g["t"] <= t1)
    tm = np.where(g["f0"][sel] > 0, 69.0 + 12.0 * np.log2(np.maximum(g["f0"][sel], 1.0) / 440.0), np.nan)
    ax.plot(g["t"][sel], tm, "k", lw=2.2, label="truth" if man.get("kind") != "refgt" else "reference")
    cols = ["tab:red", "tab:blue", "tab:green", "tab:orange", "tab:purple"]
    for i, run in enumerate(a.runs.split(",")):
        name, d = run.split("=") if "=" in run else (Path(run).name, run)
        h = read_hops(Path(d) / "hops" / f"{a.id}.csv")
        dl = h["est_delay_s"] if h["est_delay_s"] is not None else fallback_delay(h["in_midi"], case["rate"], case["range"])
        te = h["time_s"] - dl
        ss = (te >= t0) & (te <= t1)
        ax.plot(te[ss], np.where(h["voiced"][ss], h["in_midi"][ss], np.nan), color=cols[i % len(cols)], lw=1.0, label=name)
    ax.set_xlabel("s (input timeline)")
    ax.set_ylabel("MIDI")
    ax.grid(alpha=0.3)
    ax.legend(fontsize=8)
    ax.set_title(f"{a.id}: engine pitch at its own time stamps (MEASURED)")
    plt.tight_layout()
    plt.savefig(a.out, dpi=a.dpi)
    print(json.dumps({"evidence": "MEASURED", "image": a.out}))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--manifest", required=True)
    r.add_argument("--probe", required=True)
    r.add_argument("--out", required=True)
    r.add_argument("--setting", default="hard")
    r.add_argument("--extra", default="")
    r.add_argument("--range", default="case")
    r.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2)))
    r.add_argument("--only", default="")
    r.add_argument("--group", default="")
    r.add_argument("--keep-wav", action="store_true")
    r.add_argument("--shift-ms", type=float, default=0.0,
                   help="diagnostic: judge every estimate this much LATER than the engine says (negative = earlier)")
    c = sub.add_parser("compare")
    c.add_argument("summaries", nargs="+")
    c.add_argument("--names", default="")
    c.add_argument("--md")
    c.add_argument("--csv")
    pl = sub.add_parser("plot")
    pl.add_argument("--manifest", required=True)
    pl.add_argument("--id", required=True)
    pl.add_argument("--runs", required=True, help="name=RUN_DIR,name=RUN_DIR (hop logs of gt_eval run)")
    pl.add_argument("--window", default="")
    pl.add_argument("--out", required=True)
    pl.add_argument("--dpi", type=int, default=90)
    a = ap.parse_args()
    return {"run": cmd_run, "compare": cmd_compare, "plot": cmd_plot}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
