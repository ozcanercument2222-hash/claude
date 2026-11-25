# VSTM Tune – sürüm notları

## 1.4.0 (aday, 25.11.2026) – yayımlanmadı

**Dinleme yapılmadı, DAW'da denenmedi, Windows / macOS derlemesi yapılmadı.** Ayrıntılar `docs/1.4.0/`:

- değişiklik günlüğü,
- kalite raporu,
- `.drp` biçimi,
- yetki tasarımı,
- yayın kontrol listesi.

1.3.0 aday 3'ün üzerine kuruldu. 1.3 değişiklikleri `docs/1.3.0/` altında.

- **Yeni:**
  - `.drp` preset biçimi (sürümlü, doğrulanan, ikili); preset içe aktarma.
  - Ed25519 imzalı admin yetki dosyası: geliştirici paneli ve uzman motor anahtarları.
- **Ses:** algılayıcının oktav-aşağı koruması, kısa bir boşluktan sonra da ve 3 kat periyotta çalışıyor.
  - Toplamda oktav / kaba hata azaldı.
  - Gerçek kayıtlarda dosya başına sonuç karışık: REALTIME'da 8 iyileşen, 7 kötüleşen.
- **Uyumluluk:**
  - Eklenti ve parametre kimlikleri, oturum biçimi (2) ve gecikmeler aynı.
  - `.vstmpreset` okunuyor.
  - 1.3, 1.4'ün `.drp` presetlerini okuyamaz.
- **Testler:** EngineTest 78 / 78, PluginTest 97 / 97, pluginval seviye 10 SUCCESS (Linux).

## 1.2.0 (24.11.2026) – Kalite / güvenilirlik sürümü

**Dinleme yapılmadı, DAW'da denenmedi.** Ses ifadeleri ölçüm (MEASURED) veya kod incelemesine (INSPECTED) dayanır.
Ayrıntılar `docs/1.2.0/`:

- kalite raporu,
- değişiklik günlüğü (adım adım),
- test talimatı,
- geri dönüş (rollback),
- regresyon ve benchmark CSV'leri.

Yeni özellik, parametre ya da arayüz öğesi yoktur.

### Değişen davranış

- **Perde algılama doğru zamanlandı.** İnce ayar YIN çerçevesinin ortasında yapılır; 1.0 / 1.1'de tahmin ~17 ms kaymıştı.
  Perdesi kesin bilinen yeniden sentez setinde (145 gerçek kaydın perde eğrisi): kaba hata %1,47 → %0,16, ince hata p95 65 → 3 ct.
  Oktav hatası 1.1 düzeyinde kaldı.
- **Süren ses voiced kalır.** Bass aralığında ±200 ct 6,5 Hz vibratoda voiced %66 → %95.
- **Kısa periyot denetimi yalnız Soprano / Alto'da çalışır** (E6 gibi notalarda 2P / 4P hatası).
- **REALTIME ↔ HQ çalarken:** ses kısılmaz, içerik tekrarlanmaz ya da atlanmaz (132 durumda 68 → 0). Gecikme 0,4–2 s içinde kayar.
- **Hard tune:** yeni notaya 30 cent yaklaşan ses beklemeden varır. Tutulan notada KEY / SCALE değişiminde 25 ms kayma.
- **Seviye dengelemesine tepe sınırı.** Transpoze +12'de seviye eşitlenmiş tepe p95 6,9 → 5,4 dB, 10 ms patlama 358 → 140.
  Gizli limiter yoktur.
- **Varsayılan ses biraz değişti:**
  - ROBOTISM %50 vibratonun daha çoğunu korur (sentetik %74 → %97).
  - Hard tune'da nota değişimi şarkıcıya göre ~6 ms daha geç.
  - Kaydırmasız ayarda tepe p95 +0,5 dB.

  Hepsi rapor bölüm 4–5'te.

### Uyumluluk

- Parametre kimlikleri, sırası (27), aralıkları, otomasyon, preset ve oturum biçimi (state 2) **değişmedi**.
- 1.0 ve 1.1 projeleri açılır.
- Gecikme aynı: REALTIME 20 ms, HQ 40 ms.

### Testler

- EngineTest 66 → 71, PluginTest 69 → 73. Hepsi geçiyor; derlenmiş VST3 JUCE host'unda yüklenip sınandı.
- pluginval 10 (Linux VST3) geçti; 1.1.0 da aynı testten geçti.
- ASan + UBSan temiz.
- Yeni ölçüm araçları: `gt_corpus.py`, `gt_eval.py`, `gt_variants.py`, `peak_check.py`, `rt_switch_probe.cpp`,
  `rt_switch_eval.py`, `transition_eval.py`, `autokey_eval.py`, `bench_probe.cpp`.

### Test edilmeyenler

- Gerçek DAW: **NOT TESTED.**
- Windows / macOS derlemesi: 1.2.0 için yalnız CI'da yapılabilir; henüz yapılmadı.
- İnsan dinlemesi: **yok.** Kör dinleme paketi hazır.
- Auto Key'in gerçek kayıtlardaki doğruluğu ölçülmedi.

## 1.1.0 (19.11.2026) – Final quality pass

**Dinleme yapılmadı.** Bu sürümdeki her ses ifadesi ölçüm (MEASURED) veya kod incelemesine (INSPECTED) dayanır.
Dinleme ile doğrulanması gerekenler: `VSTM_TUNE_AUDIT.md` 17.9. Geri dönüş noktası: etiket `vstm-tune-pre-final-pass` (`5d2e4e3`).

### Değişen davranış

- **Auto Key artık bir kez kilitlenir.** Tonu yalnız vokalden tahmin eder (beat'i duymaz; arayüzde "VOKALDEN TAHMİN").
  Dinliyor → aday (`A? 41%`, kilitsiz, en yakın yarım sese düzeltir) → kilitli (`A` + kilit simgesi). Kilitten sonra söylenen
  notalar tonu değiştirmez. Kilit yeterli ve ayırt edici kanıt ister (tipik olarak 10–20 s şarkı); belirsiz ezgide kilitlemez.
  - 145 gerçek kayıtta (varsayılan ayar): kilitten sonra ton değişimi 35 → 0; kaydın tamamının gösterdiği tonla uyum 47/55 → 20/20;
    kilitlenen kayıt 99 → 21; medyan kilit 5,5 → 17,5 s.
  - **Varsayılan çıkış değişti:** Key = Auto iken, kilitlenmeyen kısımlarda düzeltme dizi yerine en yakın yarım sese gider.
    145 kayıttan 98'inde çıkış farklı (bit düzeyinde); artefakt metrikleri pratikte aynı (17.6).
- **KEY menüsü (Auto):** Şimdi kilitle, Önerilen tona geç, Yeniden analiz et, Ton değişimlerini izle (modülasyon; isteğe bağlı, varsayılan kapalı).
- **Kilit kalıcı:** host'un yeniden hazırlaması (`prepareToPlay`) kilidi silmez; kilit projeyle kaydedilir.
- **Presetler ton ve diziyi korur** (varsayılan açık): preset yüklemek KEY / SCALE / özel diziyi değiştirmez. Kapatılırsa 1.0 davranışı.
  "Tüm parametreleri sıfırla" her şeyi sıfırlar.
- **Fabrika presetleri:** 14 → 12. "Hicaz Vocal" ve "Chromatic Fix" kaldırıldı (tek farkları diziydi; Hicaz ve Kromatik SCALE
  menüsünde). "Robot Voice" ve "Trap Melodic" diziyi değiştirmiyor. Menüde Robotik / Doğal / Efekt grupları.
- **Kullanıcı presetini yeniden adlandırma.**
- **Klavye:** knob, kaydırıcı, seçici ve anahtarlar klavyeyle kullanılabilir (ok %1, Shift + ok bir adım, PageUp / PageDown %10,
  Home / End, Delete = varsayılan, Enter / Boşluk). Her tuş DAW'a tek bir otomasyon hareketi olarak gider.
- **ROBOTISM "i" balonu** editöre bağlandı: eklenti penceresi açıkken kapatılırsa yetim kalmaz (1.0'da bilinen sınır).
- Hakkında penceresi sürümü derlemeden alır.

### Uyumluluk

- Parametre kimlikleri, sırası (27), aralıkları ve otomasyon **değişmedi**. Preset dosya biçimi aynı.
- Oturum (state) sürümü 1 → 2: yeni özellikler `autoKeyLock`, `autoKeyConf`, `followKeyChanges`, `presetsKeepKey`.
  1.0 projeleri açılır (kilit yok, izleme kapalı, presetler tonu korur). Bozuk / elle düzenlenmiş değerler güvenli varsayılana döner.
- 1.0 projesinde kayıtlı fabrika preset adı ("Hicaz Vocal" gibi) korunur ve gösterilir; değerler aynen geri gelir.
- Sabit tonlu her ayarda ve Adaptive açık / kapalı DSP çıkışı 1.0 ile **bit düzeyinde aynı** (145 kayıt × 9 ayar).

### Testler

EngineTest 58 → 66, PluginTest 50 → 69 (hepsi geçiyor; VST3 JUCE host'unda dahil). AuditProbe'a S17 (30 dk, bellek, otomasyon,
ton istekleri, REALTIME geçişleri); ROBOTISM taraması belgedeki 9 noktada. Yeni araç: `tests/make_listening_pack.py`.

### Test edilmeyenler

Gerçek DAW (REAPER, FL Studio, diğerleri): **NOT TESTED**. pluginval: **NOT TESTED** (17.7). macOS / Windows yalnız CI'da.
İnsan dinlemesi: yok.
