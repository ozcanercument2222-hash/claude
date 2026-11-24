#!/usr/bin/env python3
"""REALTIME <-> HQ switch integrity: delay track of rt_switch_probe renders (evidence: MEASURED).

    rt_switch_eval.py DIR [--json out.json] [--md out.md]

For every case in DIR/cases.jsonl the local delay of the output against its input is measured in
20 ms windows every 2 ms (normalised FFT cross-correlation over the lags the two latencies allow,
parabolic peak). A content repeat or skip shows as a step of the full latency difference between two
windows 2 ms apart (960 samples at 48 kHz); a glide shows as a smooth ramp.
  delay_start / delay_end        first / last confident window (samples)
  end_minus_reported             delay at the end minus the latency the engine reports: 0 = aligned
  max_jump_2ms                   largest delay change between neighbouring windows
  monotonic                      (one switch) the delay only moves toward the new latency
  repeat_or_skip                 max_jump_2ms > 50 samples (about 1 ms): content jumped
Windows whose correlation is under 0.5 (silence, the hard-tuned pitch change) are skipped.
The delay track is measured on the two aperiodic signals: impulses (6-9 ms apart, one exact delay
per impulse, each matched once and in order) and noise. Periodic / voiced signals correlate equally
at delays one pitch period apart: they are judged by clicks (largest sample step) and pitch.
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np
import soundfile as sf


def delay_track(x, y, sr, lo, hi, win_s=0.020, step_s=0.002, start_s=0.3):
    win, step = int(win_s * sr), int(step_s * sr)
    span = hi - lo
    n = 1 << int(np.ceil(np.log2(win + span + win)))
    out_t, out_d, out_s = [], [], []
    for c in range(int(start_s * sr), len(y) - win - hi, step):
        a = y[c:c + win]
        ea = float(np.dot(a, a))
        if ea < 1e-10:
            continue
        seg = x[c - hi:c - lo + win]                         # x shifted by every lag in [lo, hi]
        if len(seg) < span + win:
            continue
        # correlation r[k] = sum a[j] * seg[j + (hi - L)] for L = hi - k
        A = np.fft.rfft(a, n)
        S = np.fft.rfft(seg, n)
        r = np.fft.irfft(S * np.conj(A), n)[: span + 1]
        cs = np.concatenate([[0.0], np.cumsum(seg.astype(np.float64) ** 2)])
        eb = cs[np.arange(span + 1) + win] - cs[np.arange(span + 1)]
        rn = r / np.sqrt(ea * eb + 1e-20)
        k = int(np.argmax(rn))
        sc = float(rn[k])
        if sc < 0.5:
            continue
        frac = 0.0
        if 0 < k < span:
            p0, p1, p2 = rn[k - 1], rn[k], rn[k + 1]
            den = p0 - 2 * p1 + p2
            if den < -1e-12:
                frac = float(np.clip(0.5 * (p0 - p2) / den, -0.5, 0.5))
        out_t.append(c / sr)
        out_d.append(hi - (k + frac))
        out_s.append(sc)
    return np.array(out_t), np.array(out_d), np.array(out_s)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dir")
    ap.add_argument("--json")
    ap.add_argument("--md")
    ap.add_argument("--plot", default="", help="case file name to plot (PNG next to --json)")
    a = ap.parse_args()
    d = Path(a.dir)
    cases = [json.loads(l) for l in open(d / "cases.jsonl") if l.strip()]
    inputs = {}
    rows = []
    for c in cases:
        if c["input"] not in inputs:
            inputs[c["input"]] = sf.read(str(d / c["input"]), dtype="float64")
        x, sr = inputs[c["input"]]
        y, _ = sf.read(str(d / c["file"]), dtype="float64")
        lo = min(c["latency_rt"], c["latency_hq"]) - 40
        hi = max(c["latency_rt"], c["latency_hq"]) + 40
        r = dict(c)
        if c["signal"] == "impulses":
            # one delay sample per impulse (sub-sample peak), matched in order: the precise track
            th = 0.25
            ti = np.flatnonzero(np.abs(x) > th)
            ti = ti[np.concatenate([[True], np.diff(ti) > int(0.004 * sr)])]
            ti = ti[ti + hi + 2 < len(x)]
            yo = np.abs(y)
            cand = np.flatnonzero(yo > th * 0.5)
            to = []
            k = 0
            while k < len(cand):
                j = cand[k]
                seg = yo[j:j + int(0.0015 * sr)]
                b = j + int(np.argmax(seg))
                fr = 0.0
                if 0 < b < len(y) - 1:
                    p0, p1, p2 = yo[b - 1], yo[b], yo[b + 1]
                    den = p0 - 2 * p1 + p2
                    if den < -1e-12:
                        fr = float(np.clip(0.5 * (p0 - p2) / den, -0.5, 0.5))
                to.append(b + fr)
                while k < len(cand) and cand[k] < b + int(0.004 * sr):
                    k += 1
            to = np.array(to)
            dl, missing, extra = [], 0, 0
            j = 0
            matched = []
            for a0 in ti:
                while j < len(to) and to[j] - a0 < lo - 2:
                    if matched:
                        extra += 1                     # an output impulse between two matched ones: repeated content
                    j += 1
                if j < len(to) and to[j] - a0 <= hi + 2:
                    dl.append(to[j] - a0)
                    matched.append(j)
                    j += 1
                else:
                    missing += 1                       # an input impulse that never came out: skipped content
            dl = np.array(dl)
            r["impulse_track_n"] = int(len(dl))
            r["impulse_missing"] = int(missing)
            r["impulse_extra"] = int(extra)
            if len(dl) > 1:
                r["delay_start"] = round(float(dl[0]), 2)
                r["delay_end"] = round(float(np.median(dl[-10:])), 2)
                r["end_minus_reported"] = round(float(np.median(dl[-10:])) - c["latency_reported_end"], 2)
                jumps = np.abs(np.diff(dl))
                r["max_jump_per_impulse"] = round(float(jumps.max()), 2)
                r["repeat_or_skip"] = bool(jumps.max() > 50.0 or missing > 0 or extra > 0)
                if len(c["switches"]) == 1:
                    direction = 1.0 if c["start"] == "RT" else -1.0
                    r["monotonic"] = bool(np.all(np.diff(dl) * direction > -2.0))
                if a.plot and c["file"] == a.plot:
                    import matplotlib
                    matplotlib.use("Agg")
                    import matplotlib.pyplot as plt
                    plt.figure(figsize=(10, 3.2))
                    plt.plot(ti[:len(dl)] / sr, dl / sr * 1000.0, ".", ms=2)
                    for s0 in c["switches"]:
                        plt.axvline(s0, color="r", lw=0.7)
                    plt.ylabel("delay (ms)")
                    plt.xlabel("s (input time of each impulse)")
                    plt.title(f"{c['file']}: delay of every impulse (MEASURED)")
                    plt.grid(alpha=0.3)
                    plt.tight_layout()
                    plt.savefig(str(Path(a.json).with_suffix("")) + "_" + Path(c["file"]).stem + ".png", dpi=90)
        elif c["signal"] == "noise":                             # voiced / periodic signals have no unique delay
            t, dl, sc = delay_track(x, y, sr, lo, hi)
            if len(dl) > 1:
                # only neighbouring windows (<= 10 ms apart) count: inside a glide a 20 ms window of noise
                # decorrelates (the delay drifts by several samples within it) and is skipped
                near = np.diff(t) <= 0.0105
                jumps = np.abs(np.diff(dl))[near] if near.any() else np.array([0.0])
                r["delay_windows"] = int(len(dl))
                r["coverage"] = round(float(len(dl)) / max(1, int((len(y) / sr - 0.3) / 0.002)), 3)
                r["delay_start"] = round(float(dl[0]), 2)
                r["delay_end"] = round(float(np.median(dl[-25:])), 2)
                r["end_minus_reported"] = round(float(np.median(dl[-25:])) - c["latency_reported_end"], 2)
                r["max_jump_2ms"] = round(float(jumps.max()), 2)
                r["repeat_or_skip"] = bool(jumps.max() > 50.0)
                if len(c["switches"]) == 1:
                    direction = 1.0 if c["start"] == "RT" else -1.0
                    r["monotonic"] = bool(np.all(np.diff(dl)[near] * direction > -2.0)) if near.any() else True
                if a.plot and c["file"] == a.plot:
                    import matplotlib
                    matplotlib.use("Agg")
                    import matplotlib.pyplot as plt
                    plt.figure(figsize=(10, 3.2))
                    plt.plot(t, dl / sr * 1000.0, ".", ms=2)
                    for s in c["switches"]:
                        plt.axvline(s, color="r", lw=0.7)
                    plt.ylabel("delay (ms)")
                    plt.xlabel("s")
                    plt.title(f"{c['file']}: local delay of the output (MEASURED)")
                    plt.grid(alpha=0.3)
                    plt.tight_layout()
                    plt.savefig(str(Path(a.json).with_suffix("")) + "_" + Path(c["file"]).stem + ".png", dpi=90)
        rows.append(r)
    summary = {
        "cases": len(rows),
        "repeat_or_skip_cases": sum(1 for r in rows if r.get("repeat_or_skip")),
        "not_monotonic_cases": sum(1 for r in rows if r.get("monotonic") is False),
        "misaligned_end_cases": sum(1 for r in rows if abs(r.get("end_minus_reported", 0.0)) > 1.0),
        "impulse_missing_or_extra_cases": sum(1 for r in rows if r.get("impulse_missing", 0) + r.get("impulse_extra", 0) > 0),
        "nonfinite_cases": sum(1 for r in rows if not r.get("finite", True)),
        "max_step_ratio_worst": round(max((r["max_step_near"] / max(1e-9, r["max_step_steady"]) for r in rows if r["max_step_steady"] > 0), default=0.0), 3),
        "pitch_dev_ct_worst": max((r["pitch_dev_ct"] for r in rows if r.get("pitch_dev_ct", -1) >= 0), default=None),
    }
    out = {"evidence": "MEASURED", "summary": summary, "cases": rows}
    if a.json:
        Path(a.json).write_text(json.dumps(out, indent=1))
    lines = ["| signal | path | start | switches | block | delay start | end | end - reported | max jump | repeat/skip | monotonic | impulses tracked/missing/extra | step near / steady | pitch dev ct |",
             "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for r in rows:
        sw = r["switches"]
        sws = f"{len(sw)}x" if len(sw) > 2 else ",".join(f"{v:g}" for v in sw)
        lines.append(f"| {r['signal']} | {r['path']} | {r['start']} | {sws} | {r['block']} | {r.get('delay_start', '–')} | {r.get('delay_end', '–')} | "
                     f"{r.get('end_minus_reported', '–')} | {r.get('max_jump_2ms', r.get('max_jump_per_impulse', '–'))} | {r.get('repeat_or_skip', '–')} | {r.get('monotonic', '–')} | "
                     f"{r.get('impulse_track_n', '–')}/{r.get('impulse_missing', '–')}/{r.get('impulse_extra', '–')} | {r['max_step_near']}/{r['max_step_steady']} | {r['pitch_dev_ct']} |")
    md = "\n".join(lines)
    if a.md:
        Path(a.md).write_text(json.dumps(summary, indent=1) + "\n\n" + md + "\n")
    print(json.dumps(summary, indent=1))


if __name__ == "__main__":
    sys.exit(main())
