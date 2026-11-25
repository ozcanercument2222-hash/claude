#!/usr/bin/env python3
"""Natural correction metrics against EXACT ground truth (1.6, evidence label: MEASURED).

    natural_eval.py run --manifest GT_DIR/manifest.json --probe VSTMTune_WavProbe --out DIR
                        [--settings natural,default,hard] [--extra "name=value ..."] [--jobs 4] [--limit N]
    natural_eval.py compare A/summary.json B/summary.json [--names a,b] [--md out.md]

Every case (gt_corpus.py: resynthesised real recordings / parametric voices with exactly known f0) is
rendered by WavProbe with key C chromatic (so the right note is always the nearest semitone of the
truth: the metrics judge HOW the voice is corrected, not which scale was chosen). The engine's hop log
gives the output pitch it produces (out_midi) for the content at time_s - est_delay_s; the truth is read
at that time. Pitch tracks are split zero-phase (filtfilt, per voiced stretch) into
  centre      < 1.5 Hz      (the note centre and slow drift)
  vibrato     3.5 - 9 Hz
  micro       > 12 Hz       (jitter-like fast motion)

Sustained notes: the truth's centre stays within one semitone band for >= 400 ms (100 ms trimmed at
each end). Per sustain:
  centre_err_ct     |median(output centre) - note|                       (correction accuracy)
  sung_err_ct       |median(truth centre) - note|                        (how far the singer was)
  vib_ratio         rms(output vibrato) / rms(truth vibrato)   (sustains with >= 10 ct rms vibrato)
  vib_corr          correlation of output and truth vibrato    (shape / phase kept = 1)
  vib_rate_ratio    output / truth vibrato rate (zero crossings)
  drift_ratio       std(output centre) / std(truth centre)     (sustains with >= 5 ct of drift)
  micro_ratio       rms(output micro) / rms(truth micro)
  switches_per_s    note changes of the engine inside the sustain (the truth stays on one note)
  note_ok_pct       share of the sustain's hops the engine's target is the note of the truth's centre
  flat_pct          share of hops within 3 ct of the note (output) - a fully flattened note -> 100 %
Transitions (sustain A then B, another note, joined by voice within 400 ms):
  trans_offset_ms   output midpoint crossing minus the truth's (> 0 = the output changes note later)
  trans_dur_ratio   output 10-90 % time / truth 10-90 % time   (1 = the singer's portamento kept)
  trans_corr        correlation of output and truth over the transition window (shape)
  overshoot_st      output overshoot past the new note (first 200 ms), and the truth's
  land_ms           output within 15 ct of the new note for 30 ms, from the truth's midpoint crossing
Onsets (voice starts after >= 80 ms of silence, first sustain within 300 ms, the truth starts >= 30 ct
below or above it):
  scoop_keep        (output - note) / (truth - note), 20 ms after the onset (1 = the scoop kept)
Unvoiced / errors:
  unvoiced_corr_pct truth-unvoiced hops the engine voiced and moved >= 30 ct
  wrong_note_pct    judged truth-voiced hops whose output is >= 0.75 st from the truth's note
  octave_pct        ... >= 9 st away (octave errors reaching the output)
"""
import argparse
import csv
import json
import os
import subprocess
import tempfile
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np
from scipy.signal import butter, filtfilt

SETTINGS = {
    "natural": "key=0 scale=0 robotism=0.8 retune=15 humanize=0.35",
    "default": "key=0 scale=0 robotism=0.5 retune=15 humanize=0.35",
    "hard": "key=0 scale=0 robotism=0 retune=0 humanize=0",
}
EDGE_S = 0.015


def read_hops(path):
    rows = list(csv.DictReader(open(path)))
    out = {k: np.array([float(r[k]) for r in rows]) for k in ("time_s", "in_midi", "out_midi", "est_delay_s")}
    out["voiced"] = np.array([int(r["voiced"]) > 0 for r in rows])
    out["note"] = np.array([int(r["note"]) for r in rows])
    return out


def runs_of(mask):
    m = np.concatenate([[False], mask, [False]]).astype(int)
    d = np.diff(m)
    return list(zip(np.flatnonzero(d == 1), np.flatnonzero(d == -1)))


def bands(x, fs):
    """Zero-phase split of one voiced stretch (hop grid)."""
    n = len(x)
    pad = min(n - 1, 3 * int(fs / 1.5))
    lo = filtfilt(*butter(2, 1.5 / (fs / 2), "low"), x, padlen=pad)
    vib = filtfilt(*butter(2, [3.5 / (fs / 2), 9.0 / (fs / 2)], "band"), x, padlen=pad)
    micro = filtfilt(*butter(2, 12.0 / (fs / 2), "high"), x, padlen=pad)
    return lo, vib, micro


def zero_cross_rate(v, fs):
    s = np.sign(v)
    s[s == 0] = 1
    c = np.count_nonzero(np.diff(s) > 0)
    return c * fs / max(1, len(v))


def crossing(x, level, rising):
    idx = np.flatnonzero((x[:-1] < level) & (x[1:] >= level)) if rising else np.flatnonzero((x[:-1] > level) & (x[1:] <= level))
    return int(idx[0]) + 1 if len(idx) else None


def evaluate(case, probe, setting, extra, workdir):
    rid = case["id"]
    hop_csv = os.path.join(workdir, f"{rid}_{setting}.csv")
    args = [probe, case["path"], os.devnull, "--csv", hop_csv] + SETTINGS[setting].split()
    if case.get("range") is not None:
        args.append(f"range={case['range']}")
    args += extra.split()
    res = subprocess.run(args, capture_output=True, text=True)
    if res.returncode != 0:
        return {"id": rid, "setting": setting, "error": res.stderr[-300:]}
    h = read_hops(hop_csv)
    os.remove(hop_csv)

    g = np.load(case["gt"])
    t_gt, f0, gv_ms, lvl = g["t"], g["f0"], g["voiced"].astype(bool) & (g["f0"] > 0), g["level_db"].astype(float)
    step = float(t_gt[1] - t_gt[0])
    ts = h["time_s"]
    hop_s = float(np.median(np.diff(ts)))
    fs = 1.0 / hop_s
    dl = h["est_delay_s"].copy()
    typical = float(np.median(dl[h["voiced"] & (dl > 0)])) if np.any(h["voiced"] & (dl > 0)) else 0.02
    dl = np.where(dl > 0, dl, typical)
    te = ts - dl
    gi = np.clip(np.rint(te / step).astype(int), 0, len(t_gt) - 1)
    valid = (te >= 0) & (te <= t_gt[-1])
    tv = gv_ms[gi] & valid
    truth = np.where(tv, 69.0 + 12.0 * np.log2(np.maximum(f0[gi], 1.0) / 440.0), np.nan)
    loud = lvl[gi] >= (np.max(lvl[gv_ms]) - 40.0 if gv_ms.any() else 0.0)
    ev = h["voiced"]
    out = h["out_midi"]
    note_col = h["note"]

    # voicing edges (truth) are not judged
    k = max(1, int(round(EDGE_S / hop_s)))
    change = np.zeros(len(tv))
    change[1:] = np.diff(tv.astype(int)) != 0
    edge = np.convolve(change, np.ones(2 * k + 1), "same") > 0

    r = {"id": rid, "group": case.get("group", ""), "tags": case.get("tags", []), "setting": setting,
         "sustains": [], "transitions": [], "onsets": []}

    # ---------------------------------------------------------------- per voiced stretch (truth and engine)
    stretch = tv & ev & valid
    for a, b in runs_of(stretch):
        if (b - a) * hop_s < 0.3:
            continue
        x_in = truth[a:b]
        x_out = out[a:b]
        c_in, v_in, m_in = bands(x_in, fs)
        c_out, v_out, m_out = bands(x_out, fs)
        band = np.rint(c_in)
        # sustains: constant rounded truth centre for >= 400 ms
        sus = []
        for s0, s1 in runs_of(np.ones(len(band), bool)):
            pass
        starts = [0] + [i for i in range(1, len(band)) if band[i] != band[i - 1]]
        ends = starts[1:] + [len(band)]
        for s0, s1 in zip(starts, ends):
            if (s1 - s0) * hop_s >= 0.4:
                sus.append((s0, s1, int(band[s0])))
        trim = int(round(0.1 / hop_s))
        for s0, s1, nt in sus:
            i0, i1 = s0 + trim, s1 - trim
            if i1 - i0 < int(0.2 / hop_s):
                continue
            seg = slice(i0, i1)
            jl = loud[a + i0:a + i1] & ~edge[a + i0:a + i1]
            if jl.mean() < 0.8:
                continue
            d = {"note": nt, "dur_s": round((s1 - s0) * hop_s, 3)}
            d["centre_err_ct"] = round(abs(float(np.median(c_out[seg])) - nt) * 100.0, 2)
            d["sung_err_ct"] = round(abs(float(np.median(c_in[seg])) - nt) * 100.0, 2)
            vin = float(np.sqrt(np.mean(v_in[seg] ** 2)))
            vout = float(np.sqrt(np.mean(v_out[seg] ** 2)))
            d["vib_in_ct"] = round(vin * 100.0, 2)
            if vin >= 0.10:
                d["vib_ratio"] = round(vout / vin, 4)
                d["vib_corr"] = round(float(np.corrcoef(v_in[seg], v_out[seg])[0, 1]), 4) if vout > 1e-6 else 0.0
                rin, rout = zero_cross_rate(v_in[seg], fs), zero_cross_rate(v_out[seg], fs)
                d["vib_rate_ratio"] = round(rout / rin, 4) if rin > 0 else None
            sdi, sdo = float(np.std(c_in[seg])), float(np.std(c_out[seg]))
            d["drift_in_ct"] = round(sdi * 100.0, 2)
            if sdi >= 0.05:
                d["drift_ratio"] = round(sdo / sdi, 4)
            mi, mo = float(np.sqrt(np.mean(m_in[seg] ** 2))), float(np.sqrt(np.mean(m_out[seg] ** 2)))
            d["micro_ratio"] = round(mo / mi, 4) if mi > 1e-4 else None
            nc = note_col[a + i0:a + i1]
            nc = nc[nc >= 0]
            d["switches_per_s"] = round(float(np.count_nonzero(np.diff(nc) != 0)) / ((i1 - i0) * hop_s), 3) if len(nc) > 1 else 0.0
            d["note_ok_pct"] = round(float(np.mean(nc == nt)) * 100.0, 2) if len(nc) else None
            d["flat_pct"] = round(float(np.mean(np.abs(x_out[seg] - nt) < 0.03)) * 100.0, 2)
            d["flat_in_pct"] = round(float(np.mean(np.abs(x_in[seg] - nt) < 0.03)) * 100.0, 2)
            r["sustains"].append(d)

        # transitions between consecutive sustains of different notes
        for (p0, p1, na), (q0, q1, nb) in zip(sus[:-1], sus[1:]):
            if na == nb or (q0 - p1) * hop_s > 0.4:
                continue
            w0 = max(p0, p1 - int(0.15 / hop_s))
            w1 = min(q1, q0 + int(0.25 / hop_s))
            if not loud[a + w0:a + w1].all():
                continue
            xi, xo = x_in[w0:w1], x_out[w0:w1]
            rising = nb > na
            mid = 0.5 * (na + nb)
            ci, co = crossing(xi, mid, rising), crossing(xo, mid, rising)
            if ci is None or co is None:
                continue
            tr = {"from": na, "to": nb, "step": nb - na}
            tr["trans_offset_ms"] = round((co - ci) * hop_s * 1000.0, 2)

            def dur(x):
                lo_l, hi_l = na + 0.1 * (nb - na), na + 0.9 * (nb - na)
                i10 = crossing(x, lo_l, rising)
                i90 = crossing(x, hi_l, rising)
                return None if i10 is None or i90 is None or i90 < i10 else (i90 - i10) * hop_s

            di, do = dur(xi), dur(xo)
            tr["dur_in_ms"] = round(di * 1000.0, 2) if di is not None else None
            tr["dur_out_ms"] = round(do * 1000.0, 2) if do is not None else None
            if di and do is not None and di > 0:
                tr["trans_dur_ratio"] = round(do / di, 4)
            tr["trans_corr"] = round(float(np.corrcoef(xi, xo)[0, 1]), 4) if np.std(xo) > 1e-6 else 0.0
            ncw = note_col[a + w0:a + w1]
            ncw = ncw[ncw >= 0]
            tr["trans_switches"] = int(np.count_nonzero(np.diff(ncw) != 0)) if len(ncw) > 1 else 0
            sgn = 1.0 if rising else -1.0
            k2 = int(0.2 / hop_s)
            tr["overshoot_st"] = round(max(0.0, float(np.max(sgn * (xo[co:co + k2] - nb)))) if co < len(xo) else 0.0, 4)
            tr["overshoot_in_st"] = round(max(0.0, float(np.max(sgn * (xi[ci:ci + k2] - nb)))) if ci < len(xi) else 0.0, 4)
            near = np.abs(xo[ci:] - nb) < 0.15
            hold = int(0.03 / hop_s)
            land = None
            for j in range(len(near) - hold):
                if near[j:j + hold].all():
                    land = j
                    break
            tr["land_ms"] = round(land * hop_s * 1000.0, 2) if land is not None else None
            r["transitions"].append(tr)

        # onset scoop: this stretch starts after >= 80 ms of truth silence
        pre = int(0.08 / hop_s)
        if a >= pre and not tv[a - pre:a].any() and sus and sus[0][0] * hop_s <= 0.3:
            nt = sus[0][2]
            j = int(0.02 / hop_s)
            if j < len(x_in):
                sc_in = float(x_in[j] - nt)
                if abs(sc_in) >= 0.3:
                    r["onsets"].append({"note": nt, "scoop_in_st": round(sc_in, 3),
                                        "scoop_keep": round(float(x_out[j] - nt) / sc_in, 4)})

    # ---------------------------------------------------------------- unvoiced / errors
    unv = valid & ~tv & ~edge & ev
    nunv = int((valid & ~tv & ~edge).sum())
    moved = unv & (np.abs(out - h["in_midi"]) >= 0.3)
    r["unvoiced_corr_pct"] = round(float(moved.sum()) / max(1, nunv) * 100.0, 4)
    judged = valid & tv & ~edge & loud & ev
    if judged.any():
        dn = np.abs(out[judged] - np.rint(truth[judged]))
        r["wrong_note_pct"] = round(float(np.mean(dn >= 0.75)) * 100.0, 4)
        r["octave_pct"] = round(float(np.mean(dn >= 9.0)) * 100.0, 4)
        r["judged_hops"] = int(judged.sum())
    return r


def agg(values):
    v = np.array([x for x in values if x is not None and np.isfinite(x)], float)
    if len(v) == 0:
        return None
    return {"n": int(len(v)), "median": round(float(np.median(v)), 4), "mean": round(float(np.mean(v)), 4),
            "p10": round(float(np.percentile(v, 10)), 4), "p90": round(float(np.percentile(v, 90)), 4)}


def summarise(results):
    out = {}
    cats = {"all": lambda r: True}
    for tag in ("vibrato", "straight", "messa", "vocal_fry", "breathy", "spoken", "belt", "fast_forte",
                "glide", "portamento", "bass", "soprano", "tenor", "melody", "fry", "creak"):
        cats[tag] = (lambda t: (lambda r: t in r["tags"]))(tag)
    for name, pick in cats.items():
        rs = [r for r in results if pick(r)]
        if not rs:
            continue
        sus = [s for r in rs for s in r["sustains"]]
        trs = [t for r in rs for t in r["transitions"]]
        ons = [o for r in rs for o in r["onsets"]]
        m = {"files": len(rs), "sustains": len(sus), "transitions": len(trs), "onsets": len(ons)}
        for key in ("centre_err_ct", "sung_err_ct", "vib_ratio", "vib_corr", "vib_rate_ratio", "drift_ratio",
                    "micro_ratio", "switches_per_s", "note_ok_pct", "flat_pct", "flat_in_pct"):
            m[key] = agg([s.get(key) for s in sus])
        for key in ("trans_offset_ms", "trans_dur_ratio", "trans_corr", "overshoot_st", "overshoot_in_st", "land_ms", "trans_switches"):
            m[key] = agg([t.get(key) for t in trs])
        m["scoop_keep"] = agg([o.get("scoop_keep") for o in ons])
        for key in ("unvoiced_corr_pct", "wrong_note_pct", "octave_pct"):
            m[key] = agg([r.get(key) for r in rs])
        out[name] = m
    return out


def run(a):
    cases = json.load(open(a.manifest))["files"]
    if a.limit:
        cases = cases[: a.limit]
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    settings = a.settings.split(",")
    jobs = [(c, s) for s in settings for c in cases]
    with tempfile.TemporaryDirectory(dir=out) as tmp:
        with ProcessPoolExecutor(a.jobs) as ex:
            res = list(ex.map(evaluate, [j[0] for j in jobs], [a.probe] * len(jobs), [j[1] for j in jobs],
                              [a.extra] * len(jobs), [tmp] * len(jobs)))
    errs = [x for x in res if "error" in x]
    summary = {"evidence": "MEASURED", "probe": a.probe, "manifest": a.manifest, "extra": a.extra, "errors": len(errs),
               "settings": {}}
    for s in settings:
        summary["settings"][s] = summarise([x for x in res if x.get("setting") == s and "error" not in x])
    json.dump(summary, open(out / "summary.json", "w"), indent=1)
    json.dump({"cases": res}, open(out / "cases.json", "w"))
    print(json.dumps({s: summary["settings"][s].get("all") for s in settings}, indent=1)[:6000])
    if errs:
        print("errors:", errs[:3])


KEYS = [("centre_err_ct", "centre error (ct)"), ("vib_ratio", "vibrato depth kept"), ("vib_corr", "vibrato shape corr"),
        ("vib_rate_ratio", "vibrato rate ratio"), ("drift_ratio", "drift kept"), ("micro_ratio", "micro motion kept"),
        ("switches_per_s", "false switches /s"), ("note_ok_pct", "right target %"), ("flat_pct", "flat hops %"), ("trans_offset_ms", "transition offset ms"),
        ("trans_dur_ratio", "transition time ratio"), ("trans_corr", "transition shape corr"), ("overshoot_st", "overshoot st"),
        ("land_ms", "landing ms"), ("trans_switches", "target switches per transition"), ("scoop_keep", "onset scoop kept"), ("unvoiced_corr_pct", "unvoiced moved %"),
        ("wrong_note_pct", "wrong note %"), ("octave_pct", "octave %")]


def compare(a):
    sums = [json.load(open(p)) for p in a.summaries]
    names = a.names.split(",") if a.names else [Path(p).parent.name for p in a.summaries]
    lines = []
    for s in sums[0]["settings"]:
        for cat in sums[0]["settings"][s]:
            lines.append(f"\n### {s} / {cat}\n")
            lines.append("| metric | " + " | ".join(names) + " |")
            lines.append("|---|" + "---|" * len(names))
            for key, label in KEYS:
                cells = []
                for sm in sums:
                    m = sm["settings"].get(s, {}).get(cat, {}).get(key)
                    cells.append("-" if not m else f"{m['median']:.3g} ({m['p10']:.3g}..{m['p90']:.3g}, n={m['n']})")
                lines.append(f"| {label} | " + " | ".join(cells) + " |")
    text = "\n".join(lines)
    if a.md:
        open(a.md, "w").write(text)
    print(text[:20000])


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--manifest", required=True)
    r.add_argument("--probe", required=True)
    r.add_argument("--out", required=True)
    r.add_argument("--settings", default="natural,default,hard")
    r.add_argument("--extra", default="")
    r.add_argument("--jobs", type=int, default=4)
    r.add_argument("--limit", type=int, default=0)
    c = sub.add_parser("compare")
    c.add_argument("summaries", nargs="+")
    c.add_argument("--names", default="")
    c.add_argument("--md", default="")
    a = ap.parse_args()
    run(a) if a.cmd == "run" else compare(a)


if __name__ == "__main__":
    main()
