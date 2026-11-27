# VSTM Tune – sürüm notları

## 1.7.0 (aday, 27.11.2026) – ara notalarda oktav hataları, daha kısa REALTIME gecikmesi; commit edilmedi

**Dinleme yapılmadı, gerçek DAW'da denenmedi.** Ayrıntılar `docs/1.7.0/VSTM_Tune_1.7.0_Changelog.md`.

- **Oktav hataları:**
  - Çıkıştaki oktav hataları 92 → 39 parça, süre 3,71 → 1,25 s (MEASURED, 145 kayıt, REALTIME).
  - Sesin başında ve nota geçişinde 2–5 kat periyot sınanıyor. Aşağı oktav hatası eklenmedi.
- **REALTIME gecikmesi ses aralığına göre:**
  - Soprano / Alto 13 ms, Tenor 15 ms, Bas 17 ms (1.6: hepsi 20 ms). HQ 40 ms (değişmedi).
  - Her aralık 1.6'nın 20 ms'deki kadar temiz.
  - Çalarken aralık değişirse gecikme kayarak değişir.
- **Bedeller:**
  - Sert düzeltmede medyan sapma 0,3 → 0,6 ct.
  - MIDI note-off'ları sese göre ~5 ms daha geç görünür.
- **macOS CI düzeltmesi:** okunabilir .drp'de Apple Silicon kalıntısı (`7.45e-07`) artık `0` yazılıyor.
- **Testler:** EngineTest ve PluginTest tümü (Linux).

## 1.6.0 (aday, 25.11.2026) – Natural Auto Correction; yayımlanmadı, commit edilmedi

**Dinleme yapılmadı, gerçek DAW'da denenmedi.** Ayrıntılar `docs/1.6.0/VSTM_Tune_1.6.0_Changelog.md`.

- **Doğal perde merkezi:** vibrato yarım ses sınırını geçse de hedef nota değişmez.
  - Uzun notada yanlış nota değişimi: doğal ayarda 6,9 → 1,8 /s, varsayılan ayarda 8,2 → 2,8 /s.
  - Merkez hatası 12 → 7 ct (MEASURED, 145 gerçek kayıt).
- **Hareket koruması:** vibrato sürerken yalnız merkez düzeltilir. Korunan vibrato 0,73 → 0,98 (doğal).
- **Doğal geçişler:** insansı tarafta geçiş şarkıcıyı izler.
  - Geçiş süresi / şarkıcınınki: 1,28 → 1,03.
  - Oturma: 154 → 119 ms.
- **Oktav koruması:** analiz adımında oktav sıçraması 40 ms beklenir; çıkıştaki oktav hataları 105 → 92 parça.
- **Auto Character:** AÇIK / KAPALI; ROBOTISM, Retune / Humanize / Vibrato Scale'i yönetir. Kapatınca değerler devredilir.
- **Davranış değişikliği:** varsayılan ses vibratolu kısımlarda değişti. İnsansı tarafta hızlı atlamalar artık uzatılmıyor.
- **Hard tune** (Robotism %0): 1.5 ile bit bit aynı.
- **Testler:** EngineTest 101/101, PluginTest hepsi, pluginval seviye 10 SUCCESS (Linux).
- **Kör dinleme:** 1.5 ve 1.6 paketleri hazır (16 paket).

## 1.5.0 (aday, 25.11.2026) – yayımlanmadı, commit edilmedi

**Dinleme yapılmadı, gerçek DAW'da denenmedi.** Ayrıntılar `docs/1.5.0/VSTM_Tune_1.5.0_Changelog.md`; yol haritası durumu
`docs/roadmap/`.

**Yeni** (hepsi varsayılan olarak kapalı ya da "Auto"; varsayılan ses 1.4 ile bit bit aynı):

- **Akort (A4):** 432–446 Hz.
- **Nota geçişi:** Auto / 0–200 ms.
- **Ünsüzleri formanttan koru.**
- **Formant transpozu izlesin.**
- **Note bypass:** özel dizide sağ tık.
- **Tolerans, güven eşiği, sentetik vibrato, oktav kaydırma:** ⚙ → Gelişmiş.
- **MIDI hedef notalar:** VST3 / Standalone.
- **64 bit işleme.**

**Düzeltilen:**

- **Vibrato Scale:** %0 vibratoyu gerçekten siler; %200 tril yapmadan iki katına çıkarır.
- **96 / 192 kHz:** perde doğruluğu 48 kHz'e eşit.
- **Windows:** derleme hatası giderildi; CI'da henüz doğrulanmadı.
- **Fail-safe:** motor içinde sayı dışı bir değer oluşursa kuru ses gider.

**Uyumluluk:**

- Eski parametre kimlikleri ve sırası aynı; 35 parametre en sona eklendi (toplam 48).
- Otomasyonlu parametre sayısı aynı (25).
- Eski oturumlar ve presetler yeni ayarlar kapalı / Auto / 440 Hz ile açılır.

**Testler:** EngineTest 92/92, PluginTest 100/100, pluginval seviye 10 SUCCESS, sanitizer temiz (Linux).

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
