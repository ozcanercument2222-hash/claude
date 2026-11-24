#!/usr/bin/env python3
"""Auto Key against LABELLED synthetic melodies (evidence: MEASURED, synthetic; real recordings have no
human-verified key labels, so real-recording accuracy is NOT measured here).

    autokey_eval.py make --out DIR                  # labelled corpus (sung vowels, exact keys)
    autokey_eval.py run  --corpus DIR --probe VSTMTune_WavProbe --out RUN [--jobs 4]
    autokey_eval.py compare A/summary.json B/summary.json [--names a,b]

Auto Key estimates the TONIC for the selected scale type from the vocal alone (it never hears a beat).
For a melody in C major, Scale = Major should give C and Scale = Minor A (same notes); for A natural
minor, Minor -> A and Major -> C. Each case is rendered twice (Scale Major, Scale Minor) with Key = Auto.
Case types (label = expected tonic per scale, or "none" = must not lock):
  major / minor     diatonic phrases with cadences on the tonic, 24 s, all 12 tonics
  chromatic         + 15 % chromatic passing notes                     (must lock, correct)
  melisma           wide vibrato (+-80 ct), scoops, bends               (must lock, correct)
  bass / soprano    the same melodies in other registers (Vocal Range set to match)
  single / two      one note / tonic + fifth only                        (must NOT lock: ambiguous)
  pentatonic        C D E G A only                                       (C, F, G all fit: no lock expected)
  modulation        12 s in one key, 16 s in another, follow off (lock stays) and on (should move)
Metrics: accuracy (locked tonic = label), same-note-set accuracy, false lock (locked, wrong set),
no lock (never locked within the clip), lock time, and for modulation: switch rate with follow on,
false switch with follow off. Confusion matrix over the major / minor cases.
"""
import argparse
import csv
import json
import os
import subprocess
import sys
import tempfile
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np
import soundfile as sf

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gt_corpus as G  # noqa: E402

SR = 48000
MAJOR = [0, 2, 4, 5, 7, 9, 11]
MINOR = [0, 2, 3, 5, 7, 8, 10]
NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def near(tonic, around):
    """The MIDI note of pitch class `tonic` closest to `around`."""
    d = (tonic - around) % 12
    return around + (d - 12 if d > 6 else d)


def melody(rng, tonic_midi, steps, secs, chromatic=0.0, cadence=True):
    """Phrases of 4-8 notes (0.25-0.6 s), a breath between phrases, cadences on degree 1 / 5."""
    notes, durs = [], []
    t = 0.0
    deg = 0
    while t < secs:
        n = int(rng.integers(4, 9))
        for k in range(n):
            if k == n - 1 and cadence:
                deg = int(rng.choice([0, 0, 0, 4, 7]))       # end on the tonic (or the octave / fifth)
            else:
                deg = int(np.clip(deg + rng.choice([-2, -1, -1, 1, 1, 2, 3, -3]), -3, 9))
            pc = steps[deg % 7] + 12 * (deg // 7)
            m = tonic_midi + pc
            if chromatic > 0.0 and k < n - 1 and rng.random() < chromatic:
                m += int(rng.choice([-1, 1]))
            d = float(rng.uniform(0.25, 0.6))
            notes.append(m)
            durs.append(d)
            t += d
        notes.append(None)                                  # breath
        d = float(rng.uniform(0.2, 0.4))
        durs.append(d)
        t += d
    return notes, durs


def render_voice(notes, durs, vib_ct=25.0, scoop_ct=30.0, detune_sd=12.0, seed=0, vowel="a"):
    rng = np.random.default_rng(seed)
    n = int(sum(durs) * SR)
    t = np.arange(n) / SR
    f0 = np.zeros(n)
    voiced = np.zeros(n, dtype=bool)
    pos = 0
    for m, d in zip(notes, durs):
        L = int(d * SR)
        if m is not None and L > 0:
            tt = np.arange(L) / SR
            det = rng.normal(0.0, detune_sd) / 100.0
            mm = m + det - scoop_ct / 100.0 * np.exp(-tt / 0.04) + vib_ct / 100.0 * np.sin(2 * np.pi * 5.6 * tt) * np.clip((tt - 0.1) / 0.2, 0, 1)
            f0[pos:pos + L] = G.midi_hz(mm)
            voiced[pos:pos + L] = True
        pos += L
    y = G.glottal_voice(SR, np.where(voiced, f0, 100.0), voiced, vowel=vowel, jitter=0.005, shimmer=0.03, seed=seed)
    # 10 ms fades at phrase edges (no clicks)
    env = np.convolve(voiced.astype(float), np.ones(int(0.01 * SR)) / int(0.01 * SR), "same")
    y = y * env
    return 0.3 * y / (np.max(np.abs(y)) + 1e-12)


def cmd_make(a):
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    cases = []
    seed = 100

    def add(cid, kind, y, label_major, label_minor, rng_=1, follow_case=None, note=""):
        sf.write(str(out / f"{cid}.wav"), y.astype(np.float32), SR, subtype="FLOAT")
        cases.append({"id": cid, "kind": kind, "path": str(out / f"{cid}.wav"), "range": rng_,
                      "label": {"1": label_major, "2": label_minor}, "modulation": follow_case, "note": note,
                      "seconds": round(len(y) / SR, 2)})

    for tonic in range(12):
        for mode, steps, rel in (("major", MAJOR, 9), ("minor", MINOR, 3)):
            seed += 1
            rng = np.random.default_rng(seed)
            notes, durs = melody(rng, near(tonic, 57), steps, 24.0)                  # tonic near A3
            y = render_voice(notes, durs, seed=seed)
            lab_major = tonic if mode == "major" else (tonic + rel) % 12
            lab_minor = (tonic + rel) % 12 if mode == "major" else tonic
            add(f"{mode}_{NAMES[tonic]}", mode, y, lab_major, lab_minor, note=f"{NAMES[tonic]} {mode}, diatonic")
    for tonic, mode in ((0, "major"), (6, "major"), (9, "minor"), (2, "minor")):
        seed += 1
        rng = np.random.default_rng(seed)
        steps, rel = (MAJOR, 9) if mode == "major" else (MINOR, 3)
        notes, durs = melody(rng, near(tonic, 55), steps, 24.0, chromatic=0.15)
        y = render_voice(notes, durs, seed=seed)
        lm = tonic if mode == "major" else (tonic + rel) % 12
        ln = (tonic + rel) % 12 if mode == "major" else tonic
        add(f"chromatic_{NAMES[tonic]}{mode[:3]}", "chromatic", y, lm, ln, note="15 % chromatic passing notes")
        seed += 1
        notes, durs = melody(np.random.default_rng(seed), near(tonic, 55), steps, 24.0)
        y = render_voice(notes, durs, vib_ct=80.0, scoop_ct=90.0, detune_sd=20.0, seed=seed, vowel="o")
        add(f"melisma_{NAMES[tonic]}{mode[:3]}", "melisma", y, lm, ln, note="+-80 ct vibrato, 90 ct scoops, 20 ct detune")
    for tonic, mode, reg, rg in ((4, "major", -12, 2), (7, "minor", -12, 2), (0, "major", 12, 0), (5, "minor", 12, 0)):
        seed += 1
        steps, rel = (MAJOR, 9) if mode == "major" else (MINOR, 3)
        notes, durs = melody(np.random.default_rng(seed), near(tonic, 57 + reg), steps, 24.0)
        y = render_voice(notes, durs, seed=seed)
        lm = tonic if mode == "major" else (tonic + rel) % 12
        ln = (tonic + rel) % 12 if mode == "major" else tonic
        add(f"{'bass' if reg < 0 else 'soprano'}_{NAMES[tonic]}{mode[:3]}", "register", y, lm, ln, rg,
            note="bass register, Bass range" if reg < 0 else "soprano register, Soprano range")
    # ambiguous material: must not lock
    for name, notes in (("single_C4", [60]), ("single_A3", [57]), ("two_C4G4", [60, 67]), ("two_D4A4", [62, 69])):
        seq = [notes[i % len(notes)] for i in range(40)]
        y = render_voice(seq, [0.45] * len(seq), seed=seed)
        seed += 1
        add(name, "ambiguous", y, None, None, note="one / two notes: no key")
    for k, tonic in enumerate((0, 7)):
        rng = np.random.default_rng(seed + k)
        penta = [0, 2, 4, 7, 9]
        seq = [57 + tonic + penta[int(rng.integers(0, 5))] for _ in range(50)]
        y = render_voice(seq, [0.4] * len(seq), seed=seed + k)
        add(f"pentatonic_{NAMES[tonic]}", "ambiguous", y, None, None, note="major pentatonic: C, F and G (etc.) all fit")
    seed += 10
    # modulation: 12 s key A then 16 s key B
    for k, (ta, tb, mode) in enumerate(((0, 7, "major"), (9, 4, "minor"), (0, 3, "major"), (2, 9, "major"))):
        steps, rel = (MAJOR, 9) if mode == "major" else (MINOR, 3)
        rng = np.random.default_rng(seed + k)
        n1, d1 = melody(rng, near(ta, 57), steps, 12.0)
        n2, d2 = melody(rng, near(tb, 57), steps, 16.0)
        y = render_voice(n1 + n2, d1 + d2, seed=seed + k)
        la = ta if mode == "major" else (ta + rel) % 12
        lb = tb if mode == "major" else (tb + rel) % 12
        lna = (ta + rel) % 12 if mode == "major" else ta
        lnb = (tb + rel) % 12 if mode == "major" else tb
        add(f"modulation_{NAMES[ta]}to{NAMES[tb]}{mode[:3]}", "modulation", y, la, lna,
            follow_case={"after": {"1": lb, "2": lnb}, "at_s": round(sum(d1), 2)},
            note=f"{NAMES[ta]} -> {NAMES[tb]} {mode} at {sum(d1):.1f} s")
    (out / "manifest.json").write_text(json.dumps({"evidence": "synthetic, labelled by construction", "cases": cases}, indent=1))
    print(f"{len(cases)} cases -> {out / 'manifest.json'}")


def same_set(a, b, scale):
    if a is None or b is None:
        return False
    steps = MAJOR if scale == "1" else MINOR
    return {(a + s) % 12 for s in steps} == {(b + s) % 12 for s in steps}


def job(arg):
    case, probe, scale, follow = arg
    fd, tmp = tempfile.mkstemp(suffix=".wav")
    os.close(fd)
    hops = tmp + ".csv"
    args = [probe, case["path"], tmp, "--csv", hops, "key=-1", f"scale={scale}", f"range={case['range']}",
            f"follow={1 if follow else 0}", "robotism=0.5", "retune=15", "humanize=0.35"]
    p = subprocess.run(args, capture_output=True, text=True)
    os.unlink(tmp)
    if p.returncode:
        return case["id"], scale, follow, {"error": p.stderr[-200:]}
    rows = list(csv.DictReader(open(hops)))
    os.unlink(hops)
    t = np.array([float(r["time_s"]) for r in rows])
    key = np.array([int(r["auto_key"]) for r in rows])
    st = np.array([int(r["key_state"]) for r in rows])
    locked = np.flatnonzero((st == 2) & (key >= 0))
    res = {"lock_time_s": round(float(t[locked[0]]), 2) if locked.size else None,
           "first_lock": int(key[locked[0]]) if locked.size else None,
           "end_key": int(key[-1]) if key[-1] >= 0 else None,
           "changes_after_lock": int(np.sum(np.diff(key[locked]) != 0)) if locked.size > 1 else 0}
    if case.get("modulation"):
        at = case["modulation"]["at_s"]
        after = np.flatnonzero((t > at) & (st == 2))
        res["key_at_end"] = int(key[-1]) if key[-1] >= 0 else None
        sw = [i for i in after if key[i] == case["modulation"]["after"][scale]]
        res["switch_time_s"] = round(float(t[sw[0]] - at), 2) if sw else None
    return case["id"], scale, follow, res


def cmd_run(a):
    man = json.loads((Path(a.corpus) / "manifest.json").read_text())
    jobs = []
    for c in man["cases"]:
        for scale in ("1", "2"):
            jobs.append((c, a.probe, scale, False))
            if c["kind"] == "modulation":
                jobs.append((c, a.probe, scale, True))
    with ProcessPoolExecutor(a.jobs) as ex:
        out = list(ex.map(job, jobs))
    byid = {c["id"]: c for c in man["cases"]}
    per = []
    for cid, scale, follow, r in out:
        c = byid[cid]
        lab = c["label"][scale]
        # a modulation case that first locks after the key change should lock to the key sung then
        if c.get("modulation") and r.get("lock_time_s") is not None and r["lock_time_s"] > c["modulation"]["at_s"]:
            lab = c["modulation"]["after"][scale]
        row = {"id": cid, "kind": c["kind"], "scale": "Major" if scale == "1" else "Minor", "follow": follow, "label": lab}
        row.update(r)
        lk = r.get("first_lock")
        if lab is None:
            row["outcome"] = "correct_no_lock" if lk is None else "false_lock"
        elif lk is None:
            row["outcome"] = "no_lock"
        elif lk == lab:
            row["outcome"] = "correct"
        elif same_set(lk, lab, scale):
            row["outcome"] = "same_set_other_tonic"
        else:
            row["outcome"] = "false_lock"
        per.append(row)
    summ = {}
    for kind in ["ALL"] + sorted({r["kind"] for r in per}):
        rs = [r for r in per if (kind == "ALL" or r["kind"] == kind) and not r["follow"]]
        if not rs:
            continue
        n = len(rs)
        lt = [r["lock_time_s"] for r in rs if r.get("lock_time_s") is not None and r["outcome"] == "correct"]
        summ[kind] = {"renders": n,
                      "correct": round(sum(r["outcome"] in ("correct", "correct_no_lock") for r in rs) / n, 3),
                      "false_lock": round(sum(r["outcome"] == "false_lock" for r in rs) / n, 3),
                      "same_set_other_tonic": round(sum(r["outcome"] == "same_set_other_tonic" for r in rs) / n, 3),
                      "no_lock": round(sum(r["outcome"] == "no_lock" for r in rs) / n, 3),
                      "lock_time_median_s": round(float(np.median(lt)), 2) if lt else None,
                      "changes_after_lock": int(sum(r.get("changes_after_lock", 0) for r in rs))}
    mods = [r for r in per if r["kind"] == "modulation"]
    if mods:
        on = [r for r in mods if r["follow"]]
        off = [r for r in mods if not r["follow"]]
        summ["modulation_follow"] = {
            "follow_on_switched": round(sum(r.get("switch_time_s") is not None for r in on) / max(1, len(on)), 3),
            "follow_on_switch_time_median_s": (round(float(np.median([r["switch_time_s"] for r in on if r.get("switch_time_s") is not None])), 2)
                                               if any(r.get("switch_time_s") is not None for r in on) else None),
            "follow_off_switched": round(sum(r.get("changes_after_lock", 0) > 0 for r in off) / max(1, len(off)), 3)}
    # confusion matrix on the major / minor cases (label -> first lock), per scale
    conf = {}
    for scale in ("Major", "Minor"):
        mtx = [[0] * 13 for _ in range(12)]
        for r in per:
            if r["kind"] in ("major", "minor") and r["scale"] == scale and r["label"] is not None:
                col = r["first_lock"] if r.get("first_lock") is not None else 12
                mtx[r["label"]][col] += 1
        conf[scale] = mtx
    o = Path(a.out)
    o.mkdir(parents=True, exist_ok=True)
    (o / "per_case.json").write_text(json.dumps(per, indent=0))
    (o / "summary.json").write_text(json.dumps({"meta": {"evidence": "MEASURED, synthetic labelled corpus", "probe": a.probe},
                                                "summary": summ, "confusion": conf}, indent=1))
    print(json.dumps(summ, indent=1))


def cmd_compare(a):
    names = a.names.split(",") if a.names else [Path(p).parent.name for p in a.summaries]
    sums = {n: json.loads(Path(p).read_text())["summary"] for n, p in zip(names, a.summaries)}
    keys = []
    for n in names:
        for k in sums[n]:
            if k not in keys:
                keys.append(k)
    for k in keys:
        for n in names:
            if k in sums[n]:
                print(f"{k:20s} {n:10s} {json.dumps(sums[n][k])}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("make")
    m.add_argument("--out", required=True)
    r = sub.add_parser("run")
    r.add_argument("--corpus", required=True)
    r.add_argument("--probe", required=True)
    r.add_argument("--out", required=True)
    r.add_argument("--jobs", type=int, default=3)
    c = sub.add_parser("compare")
    c.add_argument("summaries", nargs="+")
    c.add_argument("--names", default="")
    a = ap.parse_args()
    return {"make": cmd_make, "run": cmd_run, "compare": cmd_compare}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
