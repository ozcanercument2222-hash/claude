# VSTM Tune – sürüm notları

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
