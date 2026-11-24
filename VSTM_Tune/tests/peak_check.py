#!/usr/bin/env python3
"""Peak, burst and clipping-risk check of transpose / formant (evidence label: MEASURED).

    peak_check.py run --manifest vocals.json --probe VSTMTune_WavProbe --out DIR
                      [--settings tr+12,fm-12,...] [--jobs 4] [--only id,...]
    peak_check.py compare A/summary.json B/summary.json [--names a,b] [--md out.md] [--csv out.csv]

Level compensation (the average level of a shifted voice) and peak safety (what short peaks do) are
judged separately. Every file is rendered by WavProbe, the latency is removed and the wet signal
is compared with the dry input:
  off_db          the file's level offset: median over loud 50 ms frames of (wet - dry) dB
  tp_rise_db      true peak (4x oversampled) of wet minus dry, MINUS off_db: how much more the peaks
                  rise than the level (0 = the peaks follow the level; level changes are not counted)
  tp_raw_db       true peak rise without level matching (headroom the effect consumes)
  crest_rise_db   (peak / rms) of wet minus the same for dry, on the whole file
  b10             10 ms frames (dry within 30 dB of its maximum) where wet - dry - off > +6 dB
  b10_max_db      the largest of those excesses; worst_t_s = where it is (input timeline, s)
  rms10_p99_db    p99 of the 10 ms level-matched excess
  lf_bursts       20-150 Hz events where the wet exceeds the dry by > 15 dB (thumps), on active frames
  over_0dbfs      samples of the wet above full scale (only meaningful for loud inputs: see tp_raw_db)
  dc              mean of the wet
Aggregates per setting: median / p95 / max over files, sums of counts, and the 5 worst files.
Settings are WavProbe arguments; the names below are fixed so builds can be compared.
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
import soundfile as sf
from scipy import signal

HARD = "key=0 scale=0 robotism=0 retune=0 humanize=0"
SETTINGS = {"hard": HARD, "off": "key=0 scale=0 amount=0"}
for st in (1, 2, 5, 7, 12):
    SETTINGS[f"tr+{st}"] = f"{HARD} transpose={st}"
    SETTINGS[f"tr-{st}"] = f"{HARD} transpose={-st}"
for st in (3, 6, 12):
    SETTINGS[f"fm+{st}"] = f"{HARD} formant={st}"
    SETTINGS[f"fm-{st}"] = f"{HARD} formant={-st}"
SETTINGS["tr+12fm-6"] = f"{HARD} transpose=12 formant=-6"
SETTINGS["tr-12fm+6"] = f"{HARD} transpose=-12 formant=6"
SETTINGS["tr+7fm+12"] = f"{HARD} transpose=7 formant=12"
SETTINGS["tr-7fm-12"] = f"{HARD} transpose=-7 formant=-12"
SETTINGS["def_tr+12"] = "key=-1 scale=1 robotism=0.5 retune=15 humanize=0.35 transpose=12"
SETTINGS["def_tr-12"] = "key=-1 scale=1 robotism=0.5 retune=15 humanize=0.35 transpose=-12"
DEFAULT_SET = "hard,tr+1,tr-1,tr+2,tr-2,tr+5,tr-5,tr+7,tr-7,tr+12,tr-12,fm+3,fm-3,fm+6,fm-6,fm+12,fm-12,tr+12fm-6,tr-12fm+6,tr+7fm+12,tr-7fm-12"


def mono(path):
    y, sr = sf.read(str(path), always_2d=True, dtype="float64")
    return y.mean(axis=1), sr


def env_db(y, sr, win):
    n = max(1, int(win * sr))
    k = len(y) // n
    e = np.sqrt(np.mean(y[: k * n].reshape(k, n) ** 2, axis=1))
    return 20.0 * np.log10(e + 1e-9)


def true_peak(y):
    up = signal.resample_poly(y, 4, 1)
    return float(np.max(np.abs(up))) if up.size else 0.0


def band_db(y, sr, lo, hi, n=2048, hop=240):
    f, t, z = signal.stft(y, sr, nperseg=n, noverlap=n - hop, boundary=None, padded=False)
    b = (f >= lo) & (f <= hi)
    return 10.0 * np.log10(np.sum(np.abs(z[b] * 2) ** 2, axis=0) + 1e-20)


def measure(dry, wet, sr):
    e5d, e5w = env_db(dry, sr, 0.05), env_db(wet, sr, 0.05)
    loud = e5d > e5d.max() - 30
    off = float(np.median((e5w - e5d)[loud])) if loud.any() else 0.0
    e1d, e1w = env_db(dry, sr, 0.01), env_db(wet, sr, 0.01)
    l1 = e1d > e1d.max() - 30
    over = (e1w - e1d) - off
    ov = np.where(l1, over, -99.0)
    tpd, tpw = true_peak(dry), true_peak(wet)
    rd, rw = np.sqrt(np.mean(dry ** 2)) + 1e-12, np.sqrt(np.mean(wet ** 2)) + 1e-12
    ld, lw = band_db(dry, sr, 20, 150), band_db(wet, sr, 20, 150)
    l0 = band_db(dry, sr, 150, 4000)
    active = l0 > l0.max() - 45
    peaks, _ = signal.find_peaks(np.where(active, lw - ld, -99.0), height=15, distance=8)
    k = int(np.argmax(ov))
    return {
        "off_db": round(off, 2),
        "tp_dry_dbfs": round(20 * np.log10(tpd + 1e-12), 2),
        "tp_wet_dbfs": round(20 * np.log10(tpw + 1e-12), 2),
        "tp_raw_db": round(20 * np.log10((tpw + 1e-12) / (tpd + 1e-12)), 2),
        "tp_rise_db": round(20 * np.log10((tpw + 1e-12) / (tpd + 1e-12)) - off, 2),
        "crest_rise_db": round(20 * np.log10(tpw / rw) - 20 * np.log10(tpd / rd), 2),
        "b10": int(np.sum(ov > 6.0)),
        "b10_max_db": round(float(ov.max()), 1),
        "worst_t_s": round(k * 0.01, 2),
        "rms10_p99_db": round(float(np.percentile(over[l1], 99)), 2) if l1.any() else None,
        "lf_bursts": int(len(peaks)),
        "over_0dbfs": int(np.sum(np.abs(wet) > 1.0)),
        "dc": float(np.mean(wet)),
        "finite": bool(np.all(np.isfinite(wet))),
    }


def job(a):
    f, setting, probe, out = a
    fd, tmp = tempfile.mkstemp(suffix=".wav", dir=out)
    os.close(fd)
    args = [probe, f["path"], tmp] + SETTINGS[setting].split() + [f"range={f.get('range', 1)}"]
    p = subprocess.run(args, capture_output=True, text=True)
    if p.returncode:
        os.unlink(tmp)
        return f["id"], setting, {"error": p.stderr[-200:]}
    lat = int(p.stdout.split("latency ")[1].split(" ")[0])
    dry, sr = mono(f["path"])
    w, _ = mono(tmp)
    os.unlink(tmp)
    w = w[lat:lat + len(dry)]
    if len(w) < len(dry):
        w = np.concatenate([w, np.zeros(len(dry) - len(w))])
    return f["id"], setting, measure(dry, w, sr)


def aggregate(res):
    out = {}
    for s, files in res.items():
        ok = {k: v for k, v in files.items() if v and "error" not in v}
        if not ok:
            continue
        row = {"n": len(ok)}
        for q in ("off_db", "tp_rise_db", "tp_raw_db", "crest_rise_db", "b10_max_db", "rms10_p99_db"):
            vals = np.array([v[q] for v in ok.values() if v.get(q) is not None], dtype=float)
            row[q + "_median"] = round(float(np.median(vals)), 2)
            row[q + "_p95"] = round(float(np.percentile(vals, 95)), 2)
            row[q + "_max"] = round(float(np.max(vals)), 2)
        row["off_db_abs_max"] = round(float(max(abs(v["off_db"]) for v in ok.values())), 2)
        for q in ("b10", "lf_bursts", "over_0dbfs"):
            row[q] = int(sum(v[q] for v in ok.values()))
        row["files_b10"] = int(sum(1 for v in ok.values() if v["b10"] > 0))
        row["files_over_0dbfs"] = int(sum(1 for v in ok.values() if v["over_0dbfs"] > 0))
        row["nonfinite_files"] = int(sum(1 for v in ok.values() if not v["finite"]))
        row["dc_abs_max"] = float(max(abs(v["dc"]) for v in ok.values()))
        worst = sorted(ok.items(), key=lambda kv: -kv[1]["tp_rise_db"])[:5]
        row["worst_tp_rise"] = [{"id": k, "tp_rise_db": v["tp_rise_db"], "tp_raw_db": v["tp_raw_db"]} for k, v in worst]
        worst = sorted(ok.items(), key=lambda kv: -kv[1]["b10_max_db"])[:5]
        row["worst_burst"] = [{"id": k, "b10_max_db": v["b10_max_db"], "t_s": v["worst_t_s"]} for k, v in worst]
        out[s] = row
    return out


def cmd_run(a):
    man = json.loads(Path(a.manifest).read_text())
    files = [f for f in man["files"] if not a.only or f["id"] in a.only.split(",")]
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    settings = a.settings.split(",")
    t0 = time.time()
    res = {}
    with ProcessPoolExecutor(a.jobs) as ex:
        for fid, s, r in ex.map(job, [(f, s, a.probe, str(out)) for f in files for s in settings]):
            res.setdefault(s, {})[fid] = r
    summ = aggregate(res)
    meta = {"evidence": "MEASURED", "probe": a.probe, "manifest": a.manifest, "settings": {s: SETTINGS[s] for s in settings},
            "files": len(files), "seconds": round(time.time() - t0, 1)}
    (out / "per_file.json").write_text(json.dumps(res))
    (out / "summary.json").write_text(json.dumps({"meta": meta, "summary": summ}, indent=1))
    print(json.dumps(meta))
    print(table({"this": summ}, ["this"]))


COLS = ["n", "off_db_median", "off_db_abs_max", "tp_rise_db_median", "tp_rise_db_p95", "tp_rise_db_max", "tp_raw_db_p95",
        "tp_raw_db_max", "crest_rise_db_p95", "b10", "files_b10", "b10_max_db_max", "rms10_p99_db_p95", "lf_bursts",
        "files_over_0dbfs", "nonfinite_files"]


def table(summaries, names, cols=COLS):
    settings = []
    for n in names:
        for s in summaries[n]:
            if s not in settings:
                settings.append(s)
    lines = ["| setting | build | " + " | ".join(cols) + " |", "|---|---" + "|---" * len(cols) + "|"]
    for s in settings:
        for n in names:
            row = summaries[n].get(s)
            if row:
                lines.append(f"| {s} | {n} | " + " | ".join(str(row.get(c, "–")) for c in cols) + " |")
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
            w.writerow(["setting", "build"] + COLS)
            for s in summaries[names[0]]:
                for n in names:
                    row = summaries[n].get(s)
                    if row:
                        w.writerow([s, n] + [row.get(c) for c in COLS])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--manifest", required=True)
    r.add_argument("--probe", required=True)
    r.add_argument("--out", required=True)
    r.add_argument("--settings", default=DEFAULT_SET)
    r.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2)))
    r.add_argument("--only", default="")
    c = sub.add_parser("compare")
    c.add_argument("summaries", nargs="+")
    c.add_argument("--names", default="")
    c.add_argument("--md")
    c.add_argument("--csv")
    a = ap.parse_args()
    return cmd_run(a) if a.cmd == "run" else cmd_compare(a)


if __name__ == "__main__":
    sys.exit(main())
