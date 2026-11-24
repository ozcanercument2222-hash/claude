# VSTM Tune 1.2.0 – Kalite raporu (1.1.0 → 1.2.0)

Tarih: 24.11.2026 · Ölçülen yapı: 1.2.0 aday sürümü = 1.1.0 (`193d2f3`) + adım 2–11 (yama ve anlık görüntü 11,
`artifacts/audio-runs/2026-11-19_q12/bin-s8`). **Henüz commit edilmedi** (bölüm 8).

Kanıt etiketleri: **MEASURED** (ölçüm), **INSPECTED** (kod incelemesi), **USER-REPORTED** (insan geri bildirimi: **yok**),
**LISTENED-MODEL** (yerel ses modeli: **kullanılmadı**). **Bu çalışmada hiçbir ses dinlenmedi.** Ses kalitesiyle ilgili her
iyileşme yalnız "teknik test başarılı, dinleme doğrulaması bekliyor" durumundadır.

---

## Sayfa 1 – Özet

### Gerçekte yapılan iş

- **Baseline.** 1.1.0 derlendi, testlendi ve kaydedildi: EngineTest 66 / 66, PluginTest 69 / 69, pluginval 10 geçti, denetim ölçümleri (S1–S17).
  Orijinal ZIP'e dokunulmadı. Yedekler, git paketi ve geri dönüş talimatı: `VSTM_Tune_1.2.0_Rollback.md`.
- **Yeni ölçüm altyapısı.** 1.1'in pYIN referanslı ölçümü, algılayıcının hatasını referansın hatasından ayıramıyordu. Bu yüzden **perdesi
  kesin bilinen** üç set kuruldu:
  - 145 gerçek kaydın perde eğrisinden yeniden sentez,
  - 77 parametrik ses (bas vibratosu, soprano E6, kaymalar, ünsüzler, gürültü),
  - gerçek kayıtların kendisi için kısa çerçeveli referans.

  Puanlama motorun kendi zaman damgasıyla yapılır.
- **Kök neden bulundu.** 1.1'deki kaba perde hatası artışının nedeni şuydu: tam hız ince ayar en yeni ~6 ms'yi ölçüyordu, ama tahmin YIN çerçevesinin
  ortasına damgalanıyordu. Perde ~17 ms kaymış kaydediliyordu (MEASURED: kesin doğruyla zaman artığı −19 ms).
- **9 DSP adımı** (2–9 ve 11). Her biri tek tek ölçüldü (changelog tablosu). Ölçümle reddedilen 7 alternatif de orada listelenmiştir.
  Son ölçümde adım 4'ün yan etkisi bulundu ve adım 11 ile düzeltildi; sonra her şey yeniden ölçüldü.
- **Testler.** EngineTest 66 → 71, PluginTest 69 → 73. 9 yeni ölçüm aracı eklendi (test talimatı bölüm 2).
  ASan + UBSan ve pluginval 10 çalıştırıldı.
- **Teslimler.** 22 paketlik kör dinleme paketi; bu rapor; iki CSV (`…_Regression_Results.csv`: 1.0 / 1.1 / 1.2,
  `…_Benchmark.csv`); changelog, test talimatı, rollback.
- **Yapılmayanlar.** Gerçek DAW testi, insan dinlemesi, Windows / macOS derlemesi (push gerekiyor), commit.

### Önce / sonra (seçilmiş, hepsi MEASURED)

| Ölçüt | 1.0.0 | 1.1.0 | **1.2.0** |
|---|---|---|---|
| Kaba perde hatası, kesin doğru (145 gerçek kaydın yeniden sentezi) | %0,230 | %1,473 | **%0,159** |
| Oktav hatası (aynı set) / > 30 ms süren oktav koşusu | %0,262 / 46 | %0,073 / 14 | **%0,075 / 14** |
| İnce hata p95 (aynı set) | 51,7 ct | 65,3 ct | **3,1 ct** |
| Gerçek kayıtlar, kısa çerçeveli referans: kaba / oktav hata | %0,64 / %0,62 | %2,73 / %0,22 | **%0,83 / %0,23** |
| Bas ±200 ct 6,5 Hz vibrato (Bass aralığı): voiced / > 1 st hata | – | %66,2 / %19,3 | **%94,6 / %0¹** |
| REALTIME ↔ HQ çalarken: tekrar / atlanan içerik (132 durum) / perde sapması | – | 68 / 23,4 ct | **0 / 0,01 ct** |
| Transpoze +12: seviye eşitlenmiş gerçek tepe p95 / 10 ms patlama (145 kayıt) | – | 6,88 dB / 358 | **5,41 dB / 140** |
| Formant −12: aynı | – | 4,59 dB / 712 | **3,69 dB / 473** |
| Hard tune, kaydırmasız: aynı | – | 5,15 dB / 74 | **5,63 dB / 82** (kötü) |
| Hard tune nota değişimi, şarkıcıya göre (sentetik kesin doğru, medyan) | – | +2,9 ms | **+9,2 ms** (kötü) |
| ROBOTISM %50'de vibrato korunumu (sentetik S14) | – | %73,5 | **%96,7** (ses değişti) |
| Auto Key, etiketli sentetik set: doğru / yanlış kilit | – | %94,6 / %1,1 | **%94,6 / %1,1** (değişmedi) |
| CPU, 48 kHz / 256 / stereo, varsayılan (tek çekirdek, paylaşılan VM) | – | %1,65 | **%1,60** |
| `process()` içinde bellek ayırma / NaN-Inf çıkışı | – | 0 / 0 | **0 / 0** |
| EngineTest / PluginTest | – | 66 / 69 | **71 / 73** |

¹ > 1 st hata adım 2 derlemesinde ölçüldü. Son derlemede aynı tür kesin doğru vaka (sy_bassvib_200_6.5_a) %0.

### 9 / 10'a ulaşan kategoriler

**Yok.** Bu raporun kuralı şu: bir kategori 9'u ancak ölçüm hedefteyse **ve** o kategorinin gerektirdiği dış doğrulama varsa alır.
Ses kategorileri için bu kör dinleme, host kategorileri için gerçek DAW testidir. İkisi de yapılmadı. Puanlar benim tahminimdir
(tam tablo ve gerekçeleri: bölüm 7).

**Ölçümde hedefe ulaşan, yalnız dış doğrulama bekleyen kategoriler:**

- REALTIME ↔ HQ geçiş bütünlüğü: 132 durumda 0 sorun.
- Perde algılamada kesin doğruya göre oktav / kaba hata.
- Preset / oturum uyumluluğu (1.0 ve 1.1 oturumları, deterministik durum).
- Gerçek zaman güvenliği: 0 bellek ayırma; olay bloklarında bütçenin ≤ %11'i.

### 9'a ulaşmayanlar ve neden

| Kategori | Neden |
|---|---|
| Tüm ses kategorileri (perde, bas, transpoze / formant, geçişler, ROBOTISM, Adaptive) | Hiç dinlenmedi |
| Nota geçişleri | Hard tune'da 1.1'e göre ölçülerek **daha geç** (+6 ms medyan). Robotik uçta gerçek kayıtlarda geçiş süresi 41 → 61 ms |
| Transpoze / formant tepeleri | Kaydırmasız ve aşağı kaydırmalı ayarlarda tepe p95 +0,1 … +0,6 dB arttı; limiter yok; 0 dBFS aşımı var |
| Auto Key | Gerçek kayıtta doğruluk **ölçülmedi** (etiketli gerçek veri yok); beat / sidechain ton algılama yok |
| Adaptive | İnsansı uçta vibratonun yarısını siliyor (ölçüldü); varsayılan kapalı kalmalı |
| DAW / host | Hiçbir DAW'da denenmedi |
| CPU | Gerçek donanımda en kötü blok süresi ölçülmedi (VM gürültüsü) |

### Gerçek DAW testleri

**Yapılmadı.** Bu ortam Linux bir bulut makinesi; REAPER, FL Studio, Ableton, Logic, Cubase ve Studio One yok.

Yapılanlar bir DAW testi değildir:

- JUCE'un VST3 host'uyla tarama, yükleme ve ses işletme (PluginTest 5),
- pluginval 10 (Linux VST3).

Windows ve macOS'ta 1.2.0 hiç derlenmedi.

### Dinleme testleri

**Yapılmadı.** Kimse dinlemedi: ben ses dinleyemem, yerel ses modeli de (`tools/ear.py`) kullanılmadı.

Hazır olan: kör dinleme paketi. 22 paket, 77 dosya; −20 LUFS'ye eşitlenmiş, gecikmesi çıkarılmış, rastgele adlı; cevap anahtarı ayrı.
Dinleyici sensin (test talimatı bölüm 4).

### Açık riskler (önem sırasıyla)

1. **Varsayılan ses değişti** (dinlenmeli). ROBOTISM %50 vibratonun çoğunu koruyor (%73,5 → %96,7, sentetik). Hard tune'da hedef nota şarkıcının
   nota değişiminden 1.1'e göre ~6 ms daha geç değişiyor. Robotik uçta geçişler gerçek kayıtlarda daha uzun (41 → 61 ms). Olası açıklama
   (doğrulanmadı): doğru zamanlanmış tahmin şarkıcının kaymasını izliyor ve çıkış ara yarım seslerden basamaklı geçiyor.
2. **Kaydırmasız ve aşağı kaydırmalı ayarlarda tepeler biraz yükseldi** (p95 +0,1…+0,6 dB, 10 ms patlama 74 → 82). Sentetik bir "t" ünsüzü
   kuruya göre 2,0 yerine 3,7 dB kısılıyor. Kaynak adım 2 (bisect ile ölçüldü).
3. **Limiter yok.** Yukarı transpozede çıkış 0 dBFS'i aşabiliyor (145 kayıtta transpoze +12: 3 dosya; 1.1: 5).
4. **DAW'da REALTIME değiştirmek.** Bildirilen gecikme hemen değişiyor, ses 0,4 s (MIX %100) ya da 2 s (kuru ses duyuluyorsa) içinde kayıyor.
   Bu sürede kuru ses en çok ~%1,6 hız (~27 ct) değiştirir. DAW'ın gecikme telafisini (PDC) ne zaman güncellediği test edilmedi.
5. **Kromatik robotik modda geniş vibrato nota titremesine (trill) dönüşüyor.** 1.1'de de aynı (bölüm 5.1).
6. **Adaptive insansı uçta vibratoyu yarıya indiriyor.** README "vibrato korunur" diyordu; düzeltildi.
7. **Auto Key'in gerçek kayıtlardaki doğruluğu bilinmiyor.**
8. **Windows / macOS derlemesi ve gerçek donanımda en kötü blok süresi ölçülmedi.**
9. **Bilinen algılayıcı sınırları.** Tutulan alçak notanın içindeki ünsüzde YIN arada bir yanlış kısa gecikme seçiyor (1.0'dan beri). Bass aralığı
   erkek seslerinde ~%0,24 oktav-alçak hata veriyor (1.1'de de); yalnız ~75 Hz'in altına inen sesler için önerilir.

---

## 1. Yöntem

**Karşılaştırılan yapılar.**
- 1.0.0: `f6e3a46`.
- 1.1.0: `193d2f3`, aynı sesi veren iki derleme. Biri ek olarak algılayıcının zaman damgasını kaydeder.
- 1.2.0: adım 11 yapısı.

Hepsi aynı makinede, aynı girdilerle, aynı araçlarla ölçüldü. Çıktılar `artifacts/audio-runs/2026-11-19_q12/` altında.

**Kayıtlar.** 145 gerçek kayıt. Lisans ve gizlilik nedeniyle depoda değiller.

| Kaynak | Adet | Lisans |
|---|---|---|
| VocalSet (69 kadın + 68 erkek, 14 şarkıcı; arpej, gam, vibrato, düz, nefesli, vocal fry, konuşma) | 137 | CC BY 4.0 |
| RAVDESS | 6 | CC BY-NC-SA 4.0 |
| Kendi kuru rap vokalin | 1 | sana ait |
| Ayrıştırılmış bir stem | 1 | – |

**Kesin doğru setler (MEASURED, ground truth).**

- **Yeniden sentez (145):** her kaydın perde eğrisi ve harmonik genlikleri çıkarılır, sinüs toplamıyla yeniden sentezlenir. Perde birebir bilinir; hareketler
  (vibrato, kayma, geçiş) gerçek şarkıcınınkidir.
- **Sentetik (77):** glottal kaynak + formant. Bas ±100 / ±200 ct vibrato (5,5–7,5 Hz), soprano A4–A5–E6, kaymalar, ünsüzler, gürültü,
  nefes; üç ses aralığı.
- **Kısa çerçeveli referans (refgt):** gerçek kayıtların kendisi. Emin olunmayan kareler (−1) puanlanmaz. Bu referans **kesin değildir**: gerçek
  kayıttaki sayılar referans hatası da içerir.

**Ölçütler.** Oktav hatası (±9–15 st), kaba hata (1–9 st ve ≥ 15 st), > 20 / 30 / 50 ms süren oktav koşuları, ince hata
(50 ct içindeki karelerde medyan / p95), voiced recall / precision, yanlış voiced. Tanımlar: `tests/gt_eval.py`.

---

## 2. Güvenlik ve baseline

- Orijinal yükleme değiştirilmedi. Salt okunur kopyası, SHA-256 değerleri ve geri dönüş yolları rollback belgesinde.
- **Dal.** Görev `vstm-tune-1.2-quality-pass` dalını istiyordu. Bu oturum yalnız `claude/laughing-rubin-45nmp2` dalına itebiliyor; çalışma orada.
- **Commit.** Her adım ayrı yama ve anlık görüntü olarak kaydedildi (01–11). Commit izni otomatik denetimce iki kez reddedildi; adım adım commit planı bölüm 8'de.
- **Uyumluluk.** Parametre kimlikleri, sırası (27), aralıkları, preset ve oturum biçimi (state 2) **değişmedi** (PluginTest 3b / 3d / 3e).

---

## 3. Perde algılama (P0-A oktav ↔ kaba hata, P0-B bas / hızlı vibrato)

### 3.1 Kök neden

1.0 ve 1.1, tam hız ince ayarı (refine) en yeni örnekler üzerinde yapıyordu. Tahmin ise YIN çerçevesinin ortasına damgalanıyordu
(19–26 ms önce). Kesin doğruyla karşılaştırınca 1.1'in tahmini, damgasından **17–19 ms daha yeni** sesi anlatıyordu.

Bunun sonuçları:

- vibratoda ve nota geçişlerinde ölçülen perde "erken" geliyordu,
- ince ayar, kaba gecikmenin ait olduğu çerçeveyle aynı sesi görmüyordu: 1–2,5 st'lik hatalar.

Adım 2 ince ayarı çerçevenin ortasında yapar. Kesin doğruya göre zaman artığı −19 ms'den 0 ms'ye indi.

### 3.2 Sonuçlar (MEASURED; 1.0 / 1.1 / 1.2)

| Set | Kaba hata | Oktav (alçak + yüksek) | Süren oktav > 20 / 30 / 50 ms | İnce hata medyan / p95 | Voiced recall | Yanlış voiced |
|---|---|---|---|---|---|---|
| Yeniden sentez (145, kesin) | %0,230 / %1,473 / **%0,159** | %0,262 / %0,073 / **%0,075** | 70-46-19 / 31-14-2 / **32-14-2** | 9,8 / 13,1 / **0,39** ct · 51,7 / 65,3 / **3,1** ct | %99,01 / %98,99 / **%99,04** | %0 / %0,022 / **%0,031** |
| Sentetik (77, kesin) | %0,474 / %1,880 / **%0,331** | %4,98 / %0,066 / **%0,070** | 24-22-21 / 6-3-0 / **7-4-0** | 6,3 / 8,4 / **4,7** ct · 32,7 / 54,7 / **19,6** ct | %98,75 / %98,72 / **%99,16** | %0,26 / %0,25 / **%0,25** |
| Gerçek kayıt, kısa çerçeveli referans (145) | %0,635 / %2,727 / **%0,834** | %0,619 / %0,224 / **%0,225** | 140-89-50 / 88-31-10 / **86-31-12** | 11,2 / 14,8 / **4,6** ct · 53,9 / 69,9 / **30,3** ct | %96,10 / %96,10 / **%96,92** | %10,15 / %10,11 / **%11,72** |

Okuma:

- **1.0 → 1.1:** oktav hatası ve süren oktav koşuları azaldı, kaba hata arttı. 1.1'in bilinen ödünleşimi buydu.
- **1.2 iki tarafı birlikte düzeltti.** Kesin doğru setlerde 1.2 kaba ve ince hatada her iki sürümden de açıkça iyi. Oktav hatasında 1.1 ile aynı düzeyde
  (fark ≤ 0,004 puan), 1.0'dan çok iyi.
- **Gerçek kayıt referansında:**
  - 1.2'nin kaba hatası 1.1'den 3,3 kat düşük, 1.0'dan biraz yüksek (%0,83'e karşı %0,64).
  - Oktav hatası en iyi düzeyde (%0,23).
  - Yanlış voiced arttı (%10,1 → %11,7); voiced recall de arttı. Bu referansın "unvoiced" kararı gürültülüdür (nefes, ünsüz kıyıları).
    Sentetik kesin doğru sette yanlış voiced değişmedi (%0,25).

### 3.3 Bas, alçak ses, hızlı vibrato (P0-B)

- **1.1'in sentetik bas sesi ("a", ±200 ct, 6,5 Hz, Bass aralığı).**
  - Voiced: %66,2 (1.1) → %95,0 (adım 3) → %94,6 (1.2).
  - > 1 st hata: %19,3 (1.1) → %0 (adım 2).

  Kanıt: `analysis/bass_vibrato_voicing.txt`.
- **Kesin doğru sentetik bas vakaları** (±100 / ±200 ct, 5,5–7,5 Hz, "a" / "o"):
  - Voiced %98,2–99,9.
  - Kaba hata ±200 ct 7,5 Hz "a"da %0,88 → %0 (1.1 → 1.2). Diğer bas vibrato vakalarında üç sürümde de %0.
- **Bas çizgisi (sy_bass_line_o).** Oktav hatası 1.0'da %13,6, 1.1 ve 1.2'de %0. Aralık geçişi (sy_register_switch_bass) 1.0'da %32,5, 1.1 ve 1.2'de %0.
- **Bass aralığında erkek sesleri** (gerçek kayıtlar, range 2):
  - 1.1 ve 1.2'de oktav-alçak hata %0,24 / %0,24 (yeniden sentez) ve %0,27 / %0,27 (referans).
  - Kaba hata %1,53 → %0,26.

  Bass aralığı yalnız ~75 Hz'in altına inen sesler içindir; Tenor aralığı erkek seslerde daha az oktav hatası verir (README).

### 3.4 Soprano üst aralık (adım 4 ve 11)

- Sentetik soprano "i", E5 → E6 legato: ≥ 15 st hata %7,2 → %3,8. E6 notasında hâlâ arada bir 4P okunuyor; tam düzelmedi.
- Adım 4, Tenor aralığında iki tür yan etki yaptı (adım 11 ile kaldırıldı):
  - Kadın seslerinde (f5) oktav yukarı okuma.
  - Tutulan alçak notadaki ünsüzde YIN'in zaten yanlış seçtiği kısa gecikmeyi daha da yukarı taşıma. m11 "dona": 208 Hz'lik seste 525 → ~1050 Hz;
    hard tune'da kuru sesin +9,6 dB üstünde ~10 ms'lik tepe.
- Adım 11 (denetim yalnız Soprano / Alto aralığında) sonrası:
  - gerçek kayıt referansında ≥ 15 st hata %0,282 → %0,279,
  - yeniden sentezde oktav yukarı %0,073 → %0,071,
  - sentetik set değişmedi (77 kaydın hop kaydı birebir aynı).

### 3.5 Sağlamlık (MEASURED, 1.1 → 1.2)

Kesin doğru setlerin değiştirilmiş kopyaları (`gt_variants.py`). Yeniden sentezde 23 kayıtlık alt küme, sentetikte 77 ses
kullanıldı. Hücreler kaba hata / oktav hatası / ince hata p95 / voiced recall değerleridir (MEASURED, `gt_eval/variants/`).

| Değişiklik | 1.1.0 | 1.2.0 |
|---|---|---|
| Yeniden sentez, 44,1 kHz | %0,83 / %0,015 / 58,5 ct / %98,2 | **%0,32 / %0,022 / 3,9 ct / %98,2** |
| … 88,2 / 96 / 176,4 / 192 kHz | %0,82–0,89 / %0,014–0,026 / 59,8–61,2 ct | **%0,29–0,33 / %0,022–0,034 / 4,0–4,1 ct** |
| … kazanç −12 / +6 dB | %0,81 / %0,84 · 58,4 ct | **%0,34 / %0,31 · 3,8–4,0 ct** |
| … kazanç −30 dB (−56 dBFS kapısı) | %0,63 / %0,085 / 52,9 ct / %44,0 | **%0,44 / %0,097 / 4,9 ct / %44,0** |
| … beyaz gürültü SNR 30 / 20 / 10 dB | %1,09 / %1,36 / %4,14 kaba · recall %97,6 / %91,7 / %67,7 | **%0,49 / %0,74 / %3,47 · %97,9 / %92,1 / %70,0** |
| … stereo (aynı / yalnız sol / sağ −6 dB) | %0,82–0,83 | **%0,32** |
| Sentetik, 44,1 – 192 kHz | %2,05–2,36 / %0,058–0,099 / 53–58 ct / %98,4–98,5 | **%0,33–0,41 / %0,055–0,092 / 20–24 ct / %99,1** |
| Sentetik, kazanç / stereo | %1,78–1,88 / %0,066–0,070 | **%0,29–0,33 / %0,070–0,074** |

Okuma:

- 1.2'nin kaba hatası ve ince hatası her değişiklikte düşük. Sonuçlar örnekleme hızından ve kazançtan neredeyse bağımsız.
- Oktav hatası çoğu değişiklikte 0,003–0,03 puan yüksek, bazılarında düşük.
- −30 dB'de iki sürümde de voiced oranı %44'e iniyor: −56 dBFS'lik kapı sessiz bölümleri bırakıyor.
- **Tampon boyutu:** 1–4096 örnek, bit düzeyinde aynı çıktı (6.6).

### 3.6 Üçüncü ölçüm ile çelişki (açıklandı)

1.1'den beri kullanılan pYIN referanslı matriste (93 ms çerçeve) 1.2'nin "gross" hatası %1,21'den %1,42'ye çıkıyor. Kesin doğru setler ise
büyük bir düşüş gösteriyor. Nedeni ölçüldü (`analysis/matrix_gross_alignment_check.txt`):

| Hizalama | 1.1 | 1.2 |
|---|---|---|
| Matrisin sabit hizalaması | %1,202 | %1,414 |
| 1.2'nin kendi hop zaman damgalarıyla | %1,202 | %1,265 |
| İki sürümün de voiced dediği ortak kareler | %1,144 | %1,238 |

Matris her dosyaya tek bir sabit kaydırma uyguluyor. 1.2'nin zaman damgası ise perdeye göre değişiyor; sabit kaydırma bu yüzden 1.2'yi cezalandırıyor.
Kalan ~0,1 puanlık fark, 93 ms'lik düzgünleştirilmiş referansın ayırt edebileceği düzeyin altında. Bu yüzden 1.2 için perde doğruluğu kanıtı
kesin doğru setlerdir.

---

## 4. Ses işleme

### 4.1 REALTIME ↔ HQ geçişi (P0-D)

**Ölçüm.** 132 durum: dürtü dizisi, gürültü ve sesli harf; kuru / bypass / şeffaf / hard tune / MIX %50; 1–4 geçiş; tampon 64–2048.
Düzensiz dürtüler (6–9 ms) her örneğin nereye gittiğini izler. MEASURED, `rtswitch/old.json`, `rtswitch_final2.json`.

| | 1.1.0 | 1.2.0 |
|---|---|---|
| Tekrar eden ya da atlanan içerik | 68 durum | **0** |
| Sırası bozulan içerik | 10 | **0** |
| Geçiş sonunda gecikme ≠ bildirilen | 28 | **0** |
| Kaybolan / fazladan dürtü | 28 | **0** |
| NaN / Inf | 0 | 0 |
| Geçişte perde sapması (sesli harf) | 23,4 ct | **0,01 ct** |
| En büyük örnek adımı / kararlı hâl | 1,60 | 1,59 |
| Sessizlik (S3) | 0,02 ms | **0** |

**Tasarım (INSPECTED).** Bildirilen gecikme hemen değişir: 20 ms ↔ 40 ms. Gerçek gecikme yükseltilmiş kosinüsle kayar:

- yalnız işlenmiş ses duyuluyorsa 0,4 s (işlenmiş yol perde korunarak zaman esnetilir),
- kuru ses duyuluyorsa 2 s (kuru yol Hermite ile hız değiştirir).

**Bilinen sınır.** MIX < %100 iken 2 s boyunca kuru sesin hızı en çok ~%1,6 değişir (~27 ct). DAW'ın PDC'yi ne zaman güncellediği DAW'a bağlıdır;
**hiçbir DAW'da denenmedi.**

### 4.2 Nota geçişleri ve düzeltme müzikalitesi (P0-E)

Hard tune (ROBOTISM 0, RETUNE 0). Sentetik kesin doğru, 47 geçiş. Değerler REALTIME açık ve kapalıda aynı (MEASURED):

| | 1.1.0 | 1.2.0 |
|---|---|---|
| Hedef notanın değişimi − şarkıcının değişimi, medyan (tümü / hızlı koşu / kayma) | +2,9 / −1,7 / +4,7 ms | **+9,2 / +6,3 / +10,8 ms** |
| \|zamanlama\| p95 (tümü / hızlı koşu) | 79,1 / 4,9 ms | 78,9 / 7,1 ms |
| 15 ms'den geç geçiş oranı | %10,6 | **%27,7** |
| Arada üçüncü bir notaya uğrama (100 geçişte) | 6,4 | 6,4 |
| Kararlı bölgede çıkış hatası (medyan) | 1,76 ct | 1,74 ct |

**Neden (MEASURED).** 1.1'de tahmin ~17 ms erken damgalanıyordu ve bu, karar bekleme süresini tesadüfen telafi ediyordu.
Tahmin doğru zamanlanınca geçişler +19,3 ms gecikti (adım 2 sonrası, adım 6 derlemesinde ölçüldü). Adım 7 (yakın varış) bunu +9 ms'ye indirdi, ama 1.1'in değerine getirmedi.
**Bu ölçütte 1.2, 1.1'den kötü.**

**Robotik uçta gerçek kayıtlar** (`robotism_sweep.py`, 91 kayıt; kanıt: `analysis/robotic_transition_bisect.txt`):

- Çıkışın %10–90 geçiş süresi 40,6 ms'den 61,0 ms'ye çıktı. Kaynak adım 2 (bisect).
- Olası açıklama (INSPECTED, doğrulanmadı): doğru zamanlanmış tahmin şarkıcının kaymasını izliyor ve hard tune ara yarım seslerden basamaklı geçiyor.
- Bunun iyi mi kötü mü olduğuna ancak dinleme karar verebilir (paketler A06, A07, A07b).

**Diğer kontroller (EngineTest, MEASURED):**

- **Nota eşleme:** 24 949 297 durumda (perde × dizi × ton × özel dizi) yanlış nota 0.
- **Hızlı otomasyon:** en büyük örnek adımı kararlı hâlin 1,35 katı (formant); düzeltme her 250 ms aç / kapa 0,99 katı.
- **Ton değişimi:** tutulan notada A → A#, %10–90'ı 15 ms (1.1'de robotik tarafta 4 ms'lik ani sıçrama), tık yok.
- **Nota titremesi testi** geçiyor.

### 4.3 Seviye, tepe ve artefaktlar (P0-C transpoze / formant; gerçek kayıtlar)

Seviye ve tepe ayrı ölçülür. Kaynak: `peak_check.py`, 145 kayıt × 21 ayar, gecikme çıkarılmış.

- `tp_rise`: dosyanın seviye farkı çıkarıldıktan sonra kalan gerçek tepe artışı (4× aşırı örnekleme). Yani tepelerin seviyeden ne kadar fazla yükseldiği.
- `b10`: 10 ms'lik pencerede kuru sesin +6 dB üstüne çıkan patlama.

| Ayar | tp_rise p95 (dB) 1.1 → 1.2 | tp_rise en büyük | b10 (dosya) | 0 dBFS aşan dosya | Seviye farkı medyan |
|---|---|---|---|---|---|
| hard (kaydırmasız) | 5,15 → 5,63 | 7,07 → 7,14 | 74 (5) → 82 (8) | 5 → 4 | −0,06 → −0,10 dB |
| transpoze +5 | 6,59 → **5,40** | 8,0 → 7,3 | 80 (19) → 65 (10) | 5 → 2 | |
| transpoze +7 | 4,72 → 4,94 | 10,3 → 8,5 | 97 (23) → **66 (6)** | 4 → 2 | |
| transpoze +12 | 6,88 → **5,41** | 13,0 → 10,7 | 358 (57) → **140 (34)** | 5 → 3 | −0,10 → −0,13 dB |
| transpoze −7 | 4,36 → 4,87 | | 74 (8) → 81 (12) | 9 → 9 | |
| transpoze −12 | 4,35 → 4,49 | | 102 (9) → 106 (5) | 16 → 13 | |
| formant +12 | 7,47 → 7,56 | | 37 (5) → 42 (7) | 17 → 16 | |
| formant −6 | 3,98 → **3,73** | | 188 (32) → 170 (18) | 5 → 5 | |
| formant −12 | 4,59 → **3,69** | 8,1 → 7,4 | 712 (78) → **473 (53)** | 4 → **0** | |
| tr +12 / fm −6 | 5,71 → **5,21** | | 2261 (111) → **1895 (88)** | 5 → 4 | −0,54 → −0,53 dB |
| tr +7 / fm +12 | 8,12 → 8,59 | | 21 (5) → 25 (5) | 8 → 8 | |

21 ayarın tamamı `VSTM_Tune_1.2.0_Regression_Results.csv` ve `peak/full_compare_final2.md` dosyalarında. NaN / Inf: hiçbir dosyada yok.

**Okuma.**

- Adım 8 (tepe sınırlı seviye dengelemesi), seviyenin yükseltildiği yerlerde tepeleri ve patlamaları açıkça azalttı: yukarı transpoze, formant aşağı.
  Seviye eşleşmesi değişmedi.
- Kaydırmasız ve aşağı kaydırmalı ayarlarda tepe p95 0,1–0,6 dB ve patlama sayısı biraz arttı. Dosya bazında medyan değişim ~0 dB; artış birkaç dosyadan geliyor.
- Kaynak çoğunlukla adım 2 (bisect: `vs_m1 vocal fry`, `vs_m6 dona`, `vs_f5 belt`). Pitch mark'lar artık doğru zamandaki periyotla yerleşiyor ve
  düzensiz darbelerde (vocal fry) taneler bazen yapıcı biçimde üst üste biniyor (INSPECTED, doğrulanmadı).

**Gizli limiter yok.** Kazanç hiçbir zaman 1'in altına inmez. Yüksek seviyeli bir vokalde yukarı transpoze 0 dBFS'i aşabilir.

**Gerçek kayıt artefakt sayaçları** (`run_vocal_matrix.py`, kuruya göre; 1.1 → 1.2):

| Ayar | 20–150 Hz gümleme | 10 ms zarf sıçraması | Mutlak seviye farkı |
|---|---|---|---|
| hard | 425 → 457 | 871 → 935 | 0,38 → 0,60 dB |
| varsayılan | 421 → 409 | 782 → 848 | 0,42 → 0,40 dB |
| natural | 442 → 476 | 702 → 725 | 0,61 → 0,52 dB |
| off (düzeltme kapalı) | 253 → 256 | 441 → 467 | 0,12 → 0,12 dB |

Artışın en büyük payı tek dosyada (ayrıştırılmış stem: gümleme +23, sıçrama +37). Hard tune'da en düşük dosya seviye farkı −0,6 dB
(vs_m11 row vibrato; 1.1: −0,34 dB); hiçbir dosyada |seviye farkı| > 1 dB değil.

**Sentetik ünsüz (denetim S10).** Sesli harfin ortasındaki 80 ms'lik "t" patlaması kuruya göre 1.1'de −2,0, 1.2'de **−3,7 dB**. Kaynak adım 2
(bisect: −1,97 → −3,61 dB). "Sibilant / plosive temizliği" ölçütüyle dinlenmeli.

---

## 5. P1

### 5.1 ROBOTISM eğrisi

Düğme: 0 = Robotik, 50 = Dengeli, 100 = İnsansı. Ölçüm 91 gerçek kayıtta (vibratolu, düz, şarkı), kromatik dizide, perde izinden (MEASURED).
Tablo dosya medyanlarıdır.

| ROBOTISM | 0 | 10 | 20 | 25 | 35 | 50 | 65 | 80 | 90 | 100 |
|---|---|---|---|---|---|---|---|---|---|---|
| Notaya yakınlık, ct (1.1) | 1,7 | 2,3 | 5,4 | 7,6 | 11,8 | 17,6 | 23,4 | 23,9 | 21,8 | 23,0 |
| Notaya yakınlık, ct (1.2) | 1,5 | 3,1 | 7,0 | 9,2 | 13,3 | 18,0 | 22,2 | 23,1 | 23,0 | 23,5 |
| "Vibrato korunumu" % (1.1) | 98,8 | 97,0 | 94,0 | 90,8 | 85,6 | 78,1 | 64,9 | 43,3 | 61,4 | 75,8 |
| "Vibrato korunumu" % (1.2) | 97,1 | 94,9 | 92,0 | 89,8 | 87,1 | 84,5 | 70,5 | 51,2 | 74,4 | 91,8 |
| Geçiş süresi, ms (1.1) | 40,6 | 40,6 | 46,4 | 52,2 | 58,0 | 61,0 | 69,7 | 61,0 | 46,4 | 40,6 |
| Geçiş süresi, ms (1.2) | 61,0 | 63,9 | 63,9 | 63,9 | 63,9 | 63,9 | 69,7 | 55,1 | 40,6 | 34,8 |

**Dikkat: "vibrato korunumu" robotik uçta yanıltıcıdır.** Ölçüt, tutulan notada 3,5–9 Hz'lik her perde hareketini sayar. Kromatik robotik modda geniş vibrato
(şarkıcının merkezi iki nota arasındaysa) çıkışı iki yarım ses arasında titretir (trill). Hop kaydında doğrulandı: vs_f8 "dona"da tutulan notada
hedef 74 ↔ 75 arasında vibrato hızında gidip geliyor. %0'daki %97 vibrato değil, bu titremedir. 1.1'de de aynıdır. Gerçek bir ton ve dizi seçilince azalır.

Notaya yakınlık robotikten insansıya düzgün ve tek yönlü artıyor. 1.2'de eğri %10–35 arasında 1–2 ct daha gevşek, %65'te daha sıkı.

**Sentetik (S14 ve S16):**

- **Vibrato korunumu:** %50'de %73,5 → %96,7, %35'te %48,3 → %71,1, %100'de %96,8 → %102,4. Bas, tenor, alto ve sopranoda %50'de %87–97.
- **Geçiş süresi (%10–90):** %0'da 16 → 12 ms, %50'de 22 → 26 ms, %100'de 214 → 230 ms.

**Görevin istediği 0 / 10 / 20 / 35 / 50 / 65 / 80 / 90 / 100 noktaları ölçüldü** (25 fazladan). Kör dinleme paketi B01 iki ses için
0 / 20 / 35 / 50 / 65 / 80 / 100'ü içerir. **"Eşit adımlı, doğal bir eğri" olup olmadığı ölçümle kanıtlanamaz; dinlenmeli.**

### 5.2 Auto Key

Auto Key yalnız vokali duyar. **Beat'in tonunu algılamaz** (sidechain yok, tasarım gereği).

**Ölçüm.** Etiketli sentetik melodi seti (`autokey_eval.py`): 46 vaka × Major ve Minor = 92 render. Her durumda ton bilinir (MEASURED, sentetik).
Gerçek kayıtlar için insan tarafından doğrulanmış ton etiketi yok. Bu yüzden **gerçek kayıtta doğruluk ölçülmedi.**

| Vaka | Render | Doğru | Yanlış kilit | Kilitlenmedi | Kilit süresi (medyan) |
|---|---|---|---|---|---|
| Hepsi | 92 | %94,6 | %1,1 | %4,3 | 9,0 s |
| Majör | 24 | %83,3 | 0 | %16,7 | 8,0 s |
| Minör | 24 | %100 | 0 | 0 | 8,8 s |
| Kromatik geçişli | 8 | %87,5 | %12,5 | 0 | 9,5 s |
| Belirsiz (kilitlenmemeli) | 12 | %100 | 0 | 0 | – |
| Melizma / aralık / modülasyon | 8 / 8 / 8 | %100 | 0 | 0 | 12,5 / 11,8 / 8,8 s |

- 1.1 ile 1.2 aynı. Kilit sonrası değişiklik yok.
- "Ton değişimini izle" açıkken modülasyon %100 izleniyor (medyan 11,7 s), kapalıyken izlenmiyor.
- Karışıklık matrisi `autokey/final2/summary.json` içinde. Tek yanlış kilit kromatik geçişli bir melodide.
- 1.1'in gerçek kayıt gözlemi: 145 kaydın 21'i kilitlendi. Gerçek tonları bilinmediği için bunun doğruluğu da **ölçülmedi.**

### 5.3 Adaptive

**Varsayılan kapalı kalıyor.** Dinlemeyle kanıtlanmadı; ölçüm de bir sorun gösteriyor. Hepsi 1.2.0, kapalı → açık (MEASURED):

**Sentetik geçişler, varsayılan ayar (ROBOTISM %50):**
- Hedef nota değişimi medyanı 44,9 → 30,0 ms (hızlı koşu 19,7 → 17,9 ms).
- Kararlı hata 3,60 → 3,62 ct.

**Gerçek kayıtlar (91), vibrato korunumu:**

| ROBOTISM | Kapalı | Açık |
|---|---|---|
| %20 | %92,0 | %94,2 |
| %50 | %84,5 | %89,2 |
| %80 | %51,2 | %69,0 |
| **%100** | **%91,8** | **%50,5** |

Notaya yakınlık: %0'da 1,5 → 2,6 ct. README "robotik uçta etkisi yok" diyordu; küçük bir etki ölçüldü.

**İnsansı uçta vibratonun yarısı siliniyor.** Olası neden (INSPECTED): Adaptive vibratoyu ~3 Hz'lik "nota merkezi"ne göre koruyor. Bu merkez
5–6 Hz'lik vibratonun bir kısmını içerdiği için o kısım çıkıştan çıkarılıyor. Bu sürümde düzeltilmedi (Adaptive deneysel); README'deki tarif düzeltildi.

**Gerçek kayıt matrisi.** Natural'da gümleme 476 → 391, hard'da zarf sıçraması 935 → 881; seviye ±0,03 dB.

---

## 6. P2

### 6.1 CPU, gecikme, bellek ayırma, gerçek zaman güvenliği

Ölçüm `bench_probe.cpp` ile, paylaşılan 4 çekirdekli bir sanal makinede (Xeon 2,1 GHz). Ölçüm sırasında makinede başka ağır iş çalışmadı (MEASURED).

Ayarlar: "varsayılan" = Auto Key, Major, ROBOTISM %50; "ağır" = hard tune, transpoze +7, formant −3, Adaptive açık, MIX %70.
Tablodaki değerler tek çekirdek CPU yüzdesidir.

| Yapılandırma | 1.1.0 | 1.2.0 |
|---|---|---|
| 48 kHz / 256 / stereo, varsayılan, REALTIME | %1,65 | %1,60 |
| aynı, HQ | %1,62 | %1,68 |
| 48 kHz / 64 / stereo, ağır | %2,03 | %2,03 |
| 96 kHz / 512 / stereo, HQ, ağır | %3,00 | %3,03 |
| 192 kHz / 32 / stereo, ağır | %6,23 | %5,66 |
| 218 yapılandırmanın tamamı (22,05 kHz hariç; 6 hız × 6 blok × mono / stereo × 2 mod × 3 ayar), CPU medyanı | %2,45 | %2,51 |
| CPU oranı 1.2 / 1.1, yapılandırma başına | – | medyan 0,99 (0,66–1,40: VM gürültüsü) |
| p99 blok süresi / blok bütçesi, medyan | 0,075 | 0,076 |
| p99 blok süresi / bütçe, en kötü yapılandırma | 0,995 (192 kHz / 32) | 1,077 (176,4 kHz / 32) |
| 1 / 4 / 8 / 16 örnek, tek iş parçacığı | 1,6 / 6,9 / 14,8 / 28,5 | 1,6 / 6,5 / 12,0 / 30,3 |
| Bellek ayırma (`process()`) / sonlu olmayan çıkış | 0 / 0 | 0 / 0 |

**176,4 / 192 kHz ve 32 örnek blok.** Analiz (YIN) her ~17 blokta bir yapılır. O blokların p99 süresi blok bütçesine eşit (1.1'de de);
ortalama ~9–10 µs. Bu ayarda dropout riski var; README'de sınır olarak yazıldı. 64 ve üstü bloklarda en kötü yapılandırmanın p99'u
bütçenin altında.

**60 dakika (ses zamanı)**, 48 kHz / 256 / stereo, her 2 s'de tüm parametreler, her 60 s Auto Key, her 300 s REALTIME geçişi:
| | 1.2.0 |
|---|---|
| CPU (ortalama blok süresi) | %1,70 (90 µs) |
| Bellek ayırma | 0 |
| Çıktı | sonlu |
| Bellek (RSS) | 17,38 → 17,45 MB (büyüme yok) |
| Dakika başına en kötü blok | medyan 1,6 ms, p95 7,3 ms, en kötü 14,5 ms (bütçe 5,33 ms) |

Bu uç değerler, yukarıdaki olay ölçümüne göre VM kesintisidir: olay bloklarının CPU zamanı ≤ 0,6 ms, en kötü bloklar her
çalıştırmada başka yerde. s7 ile aynı koşu: ortalama 97 µs, en kötü 6,5 ms.

**Gecikme değişmedi:** REALTIME 20 ms, HQ 40 ms. Bu 22,05–192 kHz'in hepsinde örnek sayısı olarak doğru bildiriliyor (S4).

**`process()` içinde bellek ayırma: 0.** Ses iş parçacığında `operator new` sayıldı. Ölçülen her yapılandırmada sıfır
(matris, çoklu örnek, 60 dakika). Kilit yok (INSPECTED: `process()` yolunda mutex yok; durum geri yükleme ayrı iş parçacığında, PluginTest 3e).

**En kötü blok süresi.** Sanal makinede en kötü bloklar her çalıştırmada başka yerde çıkıyor. Aynı tohumlu girdiyle iki koşuda
537,7 s / 798,7 s ve 1027,4 s / 1459,1 s. 1.1'de de aynı: tek bir blok 9,35 ms. Bunlar eklentinin hesabı değil, VM kesintisi.

Olaylara ayrılmış ölçüm (S17: 30 dakika, her 2 s'de tüm parametreler, her 60 s Auto Key, her 300 s REALTIME geçişi; iş parçacığı CPU zamanı;
`analysis/s17_block_cpu/results.txt`):

| Olay | 1.1.0 | 1.2.0 |
|---|---|---|
| Parametre değişimi | 296 µs | 309 µs |
| Auto Key olayı | 198 µs | 170 µs |
| REALTIME geçişi | 301 µs | 176 µs |
| Olaydan sonraki 16 blok | 609 µs | 569 µs |

Bütçe 48 kHz / 256'da 5 333 µs. **Gerçek donanımda, bir DAW içinde ölçülmedi.**

**Uzun koşu ve bellek.** 60 dakikalık koşu (yukarıda) sorunsuz. 30 dakikalık S17 koşusunda bellek iki sürümde de 10,0 → 13,9 MB.
Artış test aracının son 20 s'yi tutan tamponundan (3,84 MB); motorda sızıntı yok.

### 6.2 Denormal, NaN / Inf, kırpılma

- **Denormal.** Motor sessizlikte FTZ / DAZ'a dayanır: FTZ kapalı 135–144 µs, açık 77 µs. Eklenti `processBlock`'ta `ScopedNoDenormals` kullanır;
  DAW içinde sorun beklenmez. Motoru JUCE dışında kullanan biri FTZ'yi kendisi açmalı.
- **NaN / Inf.** Girdide (S2) ve parametrede (S15) NaN / Inf: çıkışta 0 sonlu olmayan örnek. 145 kayıt × 21 ayarda 0. ASan + UBSan temiz (6.5).
- **Kırpılma.** Eklenti kırpmaz ve limiter içermez. Çıkış, yukarı transpozede 0 dBFS'i aşabilir (4.3).

### 6.3 Preset / oturum uyumluluğu (PluginTest, MEASURED; JUCE VST3 host'unda)

- 1.0 parametre kimlikleri ve sırası değişmedi; sonradan eklenen 14 parametre sona eklenmişti (toplam 27).
- 1.0 oturumu açılıyor: özel dizi kapalı, Adaptive kapalı. Kaldırılmış "Hicaz Vocal" preset'i Hicaz dizisiyle geri geliyor.
- 27 parametresi değiştirilmiş bir 1.1 oturumu birebir geri geliyor: değerler, Auto Key kilidi, seçenekler, HQ gecikmesi.
- Kaydedilen durum deterministik: iki kez aynı bayt; geri yükle + kaydet aynı baytı veriyor.
- Bozuk ya da eski oturum: eksik değer varsayılana döner, bilinmeyen kimlik yok sayılır, NaN → varsayılan, aralık dışı → sınır.
- Yinelenen parametre kimliği: değerlerden biri alınıyor, ses sonlu.
- Ses başka bir iş parçacığında çalarken 200 kez oturum geri yükleme: sonlu ses, çökme yok.
- **Gerçek bir DAW'da proje kaydet / kapat / aç testi yapılmadı** (DAW kiti R10).

### 6.4 Host doğrulama

pluginval 1.0.4, strictness 10, `--validate-in-process`, xvfb, Linux VST3 (MEASURED, `final2/pluginval/`):

| Sürüm | Sonuç |
|---|---|
| 1.1.0 | **SUCCESS** (25 test grubu) |
| 1.2.0 | **SUCCESS** (25 test grubu, tohum 0x7ee2a65) |

- Gruplar: tarama, soğuk / sıcak açılış, bilgi, programlar, editör, işlerken editör, ses işleme, bırakmayan işleme,
  durum ve geri yükleme, otomasyon, parametreler, arka plan iş parçacığında durum, parametre iş parçacığı güvenliği, veri yolları, parametre fuzz.
- "vst3 validator" grubu atlandı: Steinberg doğrulayıcısının yolu verilmedi.
- auval Linux'ta yok.

- PluginTest 5: derlenmiş VST3 tarandı, yüklendi; ses işletildi; A3 +35 ct → 220,00 Hz.
- Bunlar bir DAW testi değildir.

### 6.5 Bellek ve tanımsız davranış denetimi (ASan + UBSan)

Son derlemenin kaynağıyla, `-fsanitize=address,undefined -fno-sanitize-recover=undefined` ile (MEASURED, `final2/sanitizers/`):

| Program | Sonuç |
|---|---|
| EngineTest | 71 / 71 geçti |
| WavProbe | 24 çalıştırmanın hepsi çıkış kodu 0 |
| RtSwitchProbe (132 geçiş durumu) | çıkış kodu 0 |
| AddressSanitizer / UBSan / LeakSanitizer uyarısı | **0** |

WavProbe'un 24 çalıştırması: 4 girdi (rap vokali, VocalSet vocal fry, sentetik bas vibratosu, sentetik ünsüzler) × 6 ayar.
Ayarlar Auto Key, hard tune + transpoze + formant + Adaptive, HQ + MIX %50 + Bass aralığı, tampon 1 + özel dizi, Soprano aralığında çalarken
REALTIME geçişi ve Soprano + transpoze / formant +12'dir.

Kapsam: motor ve test programları. JUCE eklenti kabuğu (PluginProcessor / Editor) sanitizer ile derlenmedi.

### 6.6 Determinizm

- Tampon boyutu 1, 7, 32, 64, 128, 256, 1024 ve 4096 örnek, 512'yle **bit düzeyinde aynı** çıktı veriyor. HQ'da 1, 37 ve 4096 da aynı.
- Örnekleme hızları 22,05–192 kHz'de hard tune hatası 0,0 ct; şeffaflık SNR 84,7–93,6 dB (S4).

### 6.7 Regresyon otomasyonu

- **Her push'ta CI** (GitHub Actions, Windows / macOS / Linux) derler ve EngineTest ile PluginTest'i çalıştırır. EngineTest'teki 71 kontrolün 5'i
  1.2'de, 4 yeni bölümle eklendi:
  - içerik sürekliliği,
  - 24,9 milyon durumluk nota eşleme,
  - hızlı otomasyon,
  - ton değişiminde kayma.
- **Ölçüm araçları** (perde, tepe, geçiş, ROBOTISM, Auto Key, CPU) lisanslı kayıtlara ihtiyaç duyduğu için CI'da değil; komutları test talimatında.
- Bu sürümün sayılarının tamamı `run_final2.sh` ile tek seferde yeniden üretilir.

---

## 7. Puan tablosu (tahmin; 9 = ölçüm hedefte + dinleme / DAW doğrulaması var)

| # | Kategori | Teknik kanıt | Eksik | Puan |
|---|---|---|---|---|
| P0-A | Oktav ↔ kaba hata | Kesin doğruda 1.0 ve 1.1'den iyi; gerçek referansta kaba hata 1.0'dan biraz kötü | dinleme | 8 |
| P0-B | Bas / hızlı vibrato | Bas vibrato voiced %66 → %95, kaba %19 → %0 | dinleme; gerçek bas şarkıcısı kaydı yok | 7,5 |
| P0-C | Transpoze / formant tepe ve patlama | Yukarı transpoze ve formant aşağıda açık iyileşme; kaydırmasızda küçük kötüleşme; limiter yok | dinleme | 7 |
| P0-D | REALTIME ↔ HQ geçişi | 132 durumda 0 sorun | DAW PDC davranışı, kuru ses hız değişimi dinlenmedi | 8 |
| P0-E | Nota geçişleri | Zamanlama 1.1'den **kötü** (+6 ms); ton değişimi kayması iyi | dinleme | 6,5 |
| P1 | ROBOTISM eğrisi | Notaya yakınlık tek yönlü ve düzgün; trill (1.1'le aynı) | dinleme (B01) | 7 |
| P1 | Auto Key | Sentetikte %94,6 doğru, %1,1 yanlış kilit; değişmedi | gerçek kayıt doğruluğu ölçülmedi | 6,5 |
| P1 | Adaptive | İnsansı uçta vibratoyu yarıya indiriyor | kapalı kalmalı; dinleme | 5 |
| P2 | CPU / gecikme / bellek ayırma | 0 ayırma, CPU değişmedi, olaylarda bütçenin ≤ %11'i | gerçek donanım, DAW | 8 |
| P2 | Denormal / NaN / kırpılma | NaN / Inf ve denormal güvenli | limiter yok, 0 dBFS aşımı | 7,5 |
| P2 | DAW / host | pluginval 10, JUCE VST3 host | **hiçbir DAW** | 5 |
| P2 | Preset / oturum uyumluluğu | 1.0 / 1.1 oturumları, deterministik, bozuk girdi | DAW'da kaydet / aç | 8,5 |
| P2 | Regresyon otomasyonu | CI'da 144 kontrol; ölçüm komutları belgeli | ölçüm seti CI'da değil | 8 |
| P2 | Belgeler | Rapor, changelog, test talimatı, rollback, README | senin okuman | 8 |

---

## 8. Teslim edilenler ve yerleri

| Dosya | Yer |
|---|---|
| Bu rapor, changelog, test talimatı, rollback | `VSTM_Tune/docs/1.2.0/` |
| `VSTM_Tune_1.2.0_Regression_Results.csv` (808 satır: 1.0 / 1.1 / 1.2), `VSTM_Tune_1.2.0_Benchmark.csv` | `VSTM_Tune/docs/1.2.0/` |
| Güncellenmiş kaynak (adım 2–11), testler, araçlar | `VSTM_Tune/` (commit bekliyor) |
| Linux VST3 + Standalone, kaynak zip'i, dinleme paketi zip'i, SHA-256 | `artifacts/release-1.2.0/` |
| Kör dinleme paketi (22 paket, form, cevap anahtarı ayrı) | `artifacts/audio-runs/2026-11-19_q12/VSTM_Tune_1.2.0_Blind_Listening_Pack/` |
| Tüm ölçüm çıktıları, betikler, adım yamaları, anlık görüntüler, yedekler | `artifacts/audio-runs/2026-11-19_q12/` |

`artifacts/` git dışındadır ve yalnız bu bulut oturumunda durur. Kalıcı olanlar commit edilen dosyalardır.

**Windows VST3.** Bu ortamda derlenemez. CI, push'tan sonra Windows ve macOS paketlerini üretir.

**Commit planı (izin bekliyor).**
1. Adım başına bir commit, `193d2f3` üstüne: 01 araçlar, 02–09 DSP adımları, 10 sürüm ve testler, 11, sonra belgeler.
2. Sonra `claude/laughing-rubin-45nmp2` dalına push.

---

## 9. 9 / 10 için sıradaki adımlar

1. **Kör dinleme** (paket hazır). Özellikle A13 (varsayılan ses), A06 / A07 (hard tune geçişleri), A10 / A11 (transpoze / formant), A12 (REALTIME geçişi),
   B01 (ROBOTISM eğrisi). Sonuçlar USER-REPORTED olarak bu ölçümlerin önüne geçer.
2. **DAW kiti** (R01–R24): en az REAPER ve FL Studio, Windows ve macOS'ta.
3. **Hard tune geçiş zamanlaması.** Dinleme 1.1'in tercih edildiğini gösterirse karar bekleme süresi robotik tarafta kısaltılabilir.
   Bu, adım 2'nin doğruluğunu bozmadan yapılabilir.
4. **Tepe artışı.** Kaydırmasız ayardaki tepe artışını izleyen dosyalarda pitch mark yerleşimi incelenmeli. Gerekirse kullanıcı seçimli bir tepe sınırlayıcı eklenebilir.
5. **Adaptive.** İnsansı uçta vibrato koruma terimi, düz yolun koruduğundan fazlasını silmeyecek biçimde sınırlanmalı.
6. **Auto Key** için tonu insan tarafından etiketlenmiş gerçek kayıt seti.
