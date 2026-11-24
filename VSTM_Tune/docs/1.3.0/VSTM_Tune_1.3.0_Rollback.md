# VSTM Tune 1.3.0 – Geri dönüş (rollback)

## 1.2.0 temeli (dokunulmadı)

**Kaynak zip:** `artifacts/release-1.2.0/VSTM_Tune_1.2.0_source.zip`

- SHA-256: `b0651f0e260a8c0a57e1eb98c59fff3010efca60d5119327efb6eb2e2216af9a`
- Açılmış kopya: `artifacts/audio-runs/2026-11-24_q13/baseline-1.2.0/src/`
- 69 dosyanın SHA-256 manifesti: `baseline-1.2.0/source_manifest.sha256`

**Git:**

- Temel commit: `193d2f35fa9cbcfa3d91db1308adc6432a9670c1` (1.1.0).
- 1.2.0 ve 1.3 aday 1 değişiklikleri **henüz commit edilmedi**; commit için senin onayın gerekiyor. Commit edilince 1.2.0 ayrı bir
  commit olarak kaydedilmeli. Önerilen etiket `vstm-tune-1.2.0`; 1.3 değişiklikleri onun üstünde.
- 1.3 işine başlarken çalışma ağacının 1.2.0 zip'iyle aynı olduğu ölçüldü (`diff -r`: fark yok).

**1.2.0 ikili dosyaları:** `artifacts/audio-runs/2026-11-19_q12/bin-s8/`

- İçerik: WavProbe, EngineTest, RtSwitchProbe, BenchProbe, AuditProbe, VST3, Standalone.
- 1.3 başında aynı kaynaktan yeniden derlenen dosyalar bunlarla byte byte aynıydı.

## 1.3 aday 1'in değişiklikleri

| Yama | Dosyalar | Ne |
|---|---|---|
| `patches/01_backdated_switch.patch` | `Source/dsp/PitchCorrector.h`, `Source/dsp/TuneEngine.h` | HQ hard tune: kesinleşen nota değişimi, çalınmamış kayıtlara geri tarihlenir |
| `patches/02_peak_guard` | `Source/dsp/TuneEngine.h` (`peakGuard`, `kGuardCeiling`) | Çıkış güvenlik sınırlayıcısı, −1 dBFS, MIX'ten önce |

Test ve araç değişiklikleri:

- `tests/engine_test.cpp`: 5 kontrol eklendi.
- `tests/wav_probe.cpp`: `guard=` ve `backdate=` seçenekleri, `guard_min` çıktısı.
- `tests/bench_probe.cpp`: denormal taşması düzeltildi.
- Yeni araçlar: `tests/transition_decompose.py`, `tests/false_switch.py`, `tests/hot_peaks.py`.

## Geri dönmenin yolları

1. **Tamamen 1.2.0:** zip'i aç ya da `baseline-1.2.0/src/VSTM_Tune` klasörünü `VSTM_Tune` yerine kopyala.
   `build/` klasörlerini koru ya da sil.
2. **Yalnız tek bir değişikliği kapat** (kod değiştirmeden, test için): `TuneEngine::Params` içinde
   `peakGuard = false` ya da `backdateSwitch = false`. Bunlar eklenti parametresi değil; kalıcı geri dönüş için yamayı geri al.
3. **Oturum ve preset uyumluluğu:** 1.3 aday 1, 1.2.0 ile aynı parametreleri ve durum biçimini (state 2) kullanıyor. 1.3'te
   kaydedilen bir proje 1.2.0'da açılır (PluginTest 73 / 73).
