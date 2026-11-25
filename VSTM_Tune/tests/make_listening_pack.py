#!/usr/bin/env python3
"""Blind listening package for VSTM Tune (renders MEASURED for level / alignment; the listening
itself is the human's job: results are USER-REPORTED).

    make_listening_pack.py --plan plan.json --probe-new build/VSTMTune_WavProbe
                           [--probe-old OLD/VSTMTune_WavProbe] [--probe-exp EXP/VSTMTune_WavProbe] --out DIR [--seed 7]

plan.json:
  {"materials": {"<name>": {"path": "...wav", "range": 1, "category": "...", "license": "...",
                            "start": 12.0, "dur": 10.0, "gain_db": 0.0}},
   "packs": [{"id": "A1_robotism_rap", "comparison": "A", "material": "<name>",
              "question": "...", "variants": {"<label>": {"probe": "new", "args": "key=0 scale=0 robotism=0"}}}],
   "missing": [{"category": "...", "why": "..."}]}

Every variant is rendered from the same dry file by VSTMTune_WavProbe (bit-identical to the VST3,
VSTM_TUNE_AUDIT.md 5.2), the engine latency the probe reports is removed (all files of a pack are
aligned to the dry signal), the excerpt is cut, every file of a pack is matched to the same integrated
loudness (-20 LUFS, ITU-R BS.1770 via pyloudnorm) and one common gain keeps the loudest true peak
at -1 dBTP. Files get random 4-letter names; the key is written to KEY_<pack>.json and to
_ANSWER_KEY/answer_key.json (open only after listening). A "dry" variant (label starting with
"dry") is the unprocessed input, aligned and matched like the others.
Writes: <pack>/<code>.wav, packs.json (what is compared, no answers), LISTENING_SHEET.md (Turkish),
feedback_form.csv (one row per file and criterion set), _ANSWER_KEY/.
"""
import argparse
import csv
import json
import random
import string
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
import soundfile as sf

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import audio_view  # noqa: E402  (lufs, true_peak)

TARGET_LUFS = -20.0
CEILING = 10 ** (-1.0 / 20.0)

CRITERIA = ["Tonal doğruluk", "Doğallık", "Artefakt (5 = hiç yok)", "Sibilant / plosive temizliği", "Vibrato koruma",
            "Nota geçişi", "İstenen robotik karaktere ulaşma", "Vokal karakteri / formant", "Mix içinde oturma", "Genel tercih"]


def render(probe, src, args, rng_range):
    out = Path(tempfile.mkstemp(suffix=".wav")[1])
    p = subprocess.run([probe, src, str(out)] + args.split() + [f"range={rng_range}"], capture_output=True, text=True)
    if p.returncode != 0:
        raise RuntimeError(f"{probe} {args}: {p.stderr[-300:]}")
    lat = int(p.stdout.split("latency ")[1].split(" ")[0])
    y, sr = sf.read(str(out), always_2d=True, dtype="float64")
    out.unlink()
    return y[lat:], sr, lat


def code(rng, used):
    while True:
        c = "".join(rng.choice(string.ascii_uppercase) for _ in range(4))
        if c not in used:
            used.add(c)
            return c


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--plan", required=True)
    ap.add_argument("--probe-new", required=True)
    ap.add_argument("--probe-old")
    ap.add_argument("--probe-exp", help="a third build (variants with \"probe\": \"exp\"), e.g. an experimental patch")
    ap.add_argument("--out", required=True)
    ap.add_argument("--seed", type=int, default=7)
    a = ap.parse_args()
    plan = json.loads(Path(a.plan).read_text())
    probes = {"new": a.probe_new, "old": a.probe_old, "exp": a.probe_exp}
    out = Path(a.out)
    (out / "_ANSWER_KEY").mkdir(parents=True, exist_ok=True)
    rng = random.Random(a.seed)
    used, answer, listing, rows = set(), {}, [], []

    for pack in plan["packs"]:
        m = plan["materials"][pack["material"]]
        dry, sr = sf.read(m["path"], always_2d=True, dtype="float64")
        g = 10 ** (m.get("gain_db", 0.0) / 20.0)
        s0, n = int(m.get("start", 0.0) * sr), int(m.get("dur", 10.0) * sr)
        files = {}
        for label, v in pack["variants"].items():
            if label.startswith("dry"):
                y, lat = dry * g, 0
            else:
                tmp = Path(tempfile.mkstemp(suffix=".wav")[1])
                sf.write(str(tmp), dry * g, sr, subtype="FLOAT")
                y, sr2, lat = render(probes[v.get("probe", "new")], str(tmp), v["args"], m.get("range", 1))
                tmp.unlink()
                assert sr2 == sr
            files[label] = (y[s0:s0 + n], lat)
        # one loudness for every file of the pack, then one common gain for the true peak
        gains, peaks = {}, []
        for label, (y, _) in files.items():
            lu = audio_view.lufs(y, sr)
            gains[label] = 10 ** ((TARGET_LUFS - lu) / 20.0) if lu is not None else 1.0
            peaks.append(audio_view.true_peak(y * gains[label]))
        common = min(1.0, CEILING / max(max(peaks), 1e-9))
        pdir = out / pack["id"]
        pdir.mkdir(parents=True, exist_ok=True)
        labels = list(files)
        rng.shuffle(labels)
        key = {}
        fade = min(int(0.01 * sr), n // 4)
        ramp = np.linspace(0.0, 1.0, fade)[:, None]
        for label in labels:
            y, lat = files[label]
            y = y * gains[label] * common
            y[:fade] *= ramp
            y[-fade:] *= ramp[::-1]
            c = code(rng, used)
            sf.write(str(pdir / f"{c}.wav"), y.astype(np.float32), sr, subtype="PCM_16")
            key[c] = {"label": label, "settings": pack["variants"][label].get("args", "dry (unprocessed)"),
                      "build": pack["variants"][label].get("probe", "-") if not label.startswith("dry") else "-",
                      "latency_removed_samples": lat, "gain_db": round(20 * np.log10(gains[label] * common), 2)}
            rows.append({"paket": pack["id"], "dosya": c})
        answer[pack["id"]] = key
        (out / "_ANSWER_KEY" / f"KEY_{pack['id']}.json").write_text(json.dumps(key, indent=1, ensure_ascii=False))
        listing.append({"pack": pack["id"], "comparison": pack["comparison"], "question": pack["question"],
                        "material": pack["material"], "category": m.get("category", ""), "license": m.get("license", ""),
                        "excerpt_s": [m.get("start", 0.0), m.get("dur", 10.0)], "files": sorted(key),
                        "loudness_lufs": TARGET_LUFS, "true_peak_ceiling_dbtp": -1.0})
        print(f"{pack['id']}: {len(key)} files", flush=True)

    (out / "_ANSWER_KEY" / "answer_key.json").write_text(json.dumps(answer, indent=1, ensure_ascii=False))
    (out / "packs.json").write_text(json.dumps({"evidence": "renders MEASURED (level, alignment); listening = USER-REPORTED",
                                                "packs": listing, "missing_material": plan.get("missing", [])},
                                               indent=1, ensure_ascii=False))
    with open(out / "feedback_form.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["paket", "dosya"] + [f"{c} (1-5)" for c in CRITERIA] + ["Zaman (s) ve sorun: hangi kelime / nota, ne duyuldu?"])
        for r in rows:
            w.writerow([r["paket"], r["dosya"]] + [""] * (len(CRITERIA) + 1))
    sheet = ["# VSTM Tune – kör dinleme formu", "",
             "Bu klasördeki her paket aynı kuru vokalin farklı işlenmiş hâllerini içerir. Dosyalar **aynı yüksekliğe**",
             "(-20 LUFS) ve **aynı zamana** (eklenti gecikmesi çıkarılmış) getirilmiştir; adları rastgeledir.",
             "`_ANSWER_KEY` klasörünü **dinleme bitene kadar açmayın**.", "",
             "**Nasıl:** kulaklık veya monitör, sabit ses seviyesi. Her pakette dosyaları istediğiniz sırayla, istediğiniz kadar",
             "dinleyin. `feedback_form.csv` dosyasında her dosya için ölçütleri 1–5 arasında puanlayın (5 = en iyi; artefakt",
             "için 5 = hiç yok). Bir sorun duyduğunuzda zamanı ve ne olduğunu yazın: \"3,2 s 'gece' kelimesinde metalik ses\".", "",
             "Ölçütler: " + ", ".join(CRITERIA) + ".", "", "## Paketler", ""]
    for p in listing:
        sheet += [f"### {p['pack']}  (karşılaştırma {p['comparison']})", "", p["question"], "",
                  f"Malzeme: {p['category']} — lisans: {p['license']}. Dosyalar: " + ", ".join(f"`{c}`" for c in p["files"]), ""]
    if plan.get("missing"):
        sheet += ["## Eksik malzeme (paket üretilemedi)", ""]
        sheet += [f"- **{m['category']}**: {m['why']}" for m in plan["missing"]]
        sheet += [""]
    (out / "LISTENING_SHEET.md").write_text("\n".join(sheet) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
