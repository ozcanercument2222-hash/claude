# VSTM Tune 1.3.0 – Geri dönüş (rollback)

## 1.2.0 temeli (dokunulmadı)

**Kaynak zip:** `artifacts/release-1.2.0/VSTM_Tune_1.2.0_source.zip`

- SHA-256: `b0651f0e260a8c0a57e1eb98c59fff3010efca60d5119327efb6eb2e2216af9a`
- Açılmış kopya: `artifacts/audio-runs/2026-11-24_q13/baseline-1.2.0/src/`
- 69 dosyanın SHA-256 manifesti: `baseline-1.2.0/source_manifest.sha256`

**Git (`claude/laughing-rubin-45nmp2` dalı):**

| Commit | İçerik |
|---|---|
| `193d2f3` | 1.1.0 |
| `77433e0` | **1.2.0.** Ağaç, 1.2.0 zip'iyle birebir aynı (`git archive` + `diff -r`) |
| `f146f26` | **1.3 aday 1.** Ağaç, aday 1 kaynak zip'iyle birebir aynı. Windows CI derlemesi bundan |
| `fe5ee7a` | VEX Stutter (ayrı proje; VSTM Tune'a dokunmuyor) |
| – | **1.3 aday 2 ve 3:** commit edilmedi (çalışma ağacında aday 3) |

- Önerilen etiket: 1.2.0 için `vstm-tune-1.2.0` (`77433e0`).
- Etiketler henüz konmadı.

**1.2.0 ikili dosyaları:** `artifacts/audio-runs/2026-11-19_q12/bin-s8/`

- İçerik: WavProbe, EngineTest, RtSwitchProbe, BenchProbe, AuditProbe, VST3, Standalone.
- 1.3 başında aynı kaynaktan yeniden derlenen dosyalar bunlarla byte byte aynıydı.

## 1.3 değişiklikleri

| Yama | Dosyalar | Ne | Aday |
|---|---|---|---|
| `patches/01_backdated_switch.patch` | `Source/dsp/PitchCorrector.h`, `Source/dsp/TuneEngine.h` | HQ hard tune: kesinleşen nota değişimi, çalınmamış kayıtlara geri tarihlenir | 1 |
| `patches/02_peak_guard` | `Source/dsp/TuneEngine.h` (`peakGuard`, `kGuardCeiling`) | Çıkış güvenlik sınırlayıcısı, −1 dBFS, MIX'ten önce | 1 |
| `patches/03_rt_fresh_analysis.patch` | `Source/dsp/PitchDetector.h`, `Source/dsp/TuneEngine.h` | REALTIME: en yeni örneklere hizalı algılayıcı penceresi, bitiş koruması, kayıt sırası | 2 |
| `patches/04_vibrato_keep_retune.patch` | `Source/dsp/PitchCorrector.h` | İnsansı taraf: salınan seste nota değişimi düzeltme hızını sıfırlamaz | 3 |
| `patches/X1_vibrato_swing_guard_EXPERIMENTAL.patch` | `Source/dsp/PitchCorrector.h` | Doğal uçta vibrato salınım koruması. **Uygulanmadı** (rapor 8.5) | – |

Test ve araç değişiklikleri:

- **Aday 1:**
  - `tests/engine_test.cpp`: 5 kontrol;
  - `tests/wav_probe.cpp`: `guard=`, `backdate=`, `guard_min`;
  - `tests/bench_probe.cpp`: denormal taşması düzeltildi;
  - yeni araçlar: `transition_decompose.py`, `false_switch.py`, `hot_peaks.py`.
- **Aday 2** (`patches/03_rt_fresh_analysis_tests.patch`):
  - `tests/engine_test.cpp`: 2 kontrol;
  - `tests/wav_probe.cpp`: `fresh=`.
- **Ölçüm araçları** (repo dışı, `artifacts/audio-runs/2026-11-24_q13/tools/`): `decsim.py`, `screen*.py`, `real_tr.py`,
  `vib_exp.py`, `flips.py`. `decsim.py`, düzelticinin Python taklidi.

## Geri dönmenin yolları

1. **Tamamen 1.2.0:** `git checkout 77433e0 -- VSTM_Tune` ya da zip'i aç. `build/` klasörlerini koru ya da sil.
2. **Aday 1'e dön (REALTIME ve vibrato değişikliklerini kaldır):** `git checkout f146f26 -- VSTM_Tune`, ya da
   `04_vibrato_keep_retune.patch` ve `03_rt_fresh_analysis.patch`'i bu sırayla ters uygula (`git apply -R`).
   Yalnız aday 2'ye dönmek için yalnız `04`'ü ters uygula.
3. **Yalnız tek bir değişikliği kapat** (kod değiştirmeden, test için): `TuneEngine::Params` içinde `peakGuard = false`,
   `backdateSwitch = false` ya da `freshAnalysis = false`.
   - Bunlar eklenti parametresi değil.
   - Kalıcı geri dönüş için yamayı geri al.
   - WavProbe'da: `guard=0`, `backdate=0`, `fresh=0`.
4. **Oturum ve preset uyumluluğu:** 1.3 aday 1, 2 ve 3, 1.2.0 ile aynı parametreleri ve durum biçimini (state 2) kullanıyor. 1.3'te
   kaydedilen bir proje 1.2.0'da açılır (PluginTest 73 / 73).
