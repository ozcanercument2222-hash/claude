# VSTM Tune 1.3.0 – Değişiklik günlüğü (aday 3, yayımlanmadı)

Durum: **1.3.0 aday 3** (aday 1 + REALTIME güncel analiz + insansı tarafta vibrato koruması). Bu bir kalite sürümü; yeni özellik, parametre ya da arayüz
değişikliği yok. Yayın kontrol listesi tamamlanmadı (`VSTM_Tune_1.3.0_Release_Checklist.md`). Bu yüzden derleme hâlâ 1.2.0 sürüm
numarasını taşıyor; numara yayında değişecek.

Kanıt etiketleri: **MEASURED** = ölçüm, **INSPECTED** = kaynak incelemesi. Bu adayda **dinleme ve DAW testi yok.**

## Değişenler

### 1. Hard tune: nota değişimi şarkıcının geçtiği yerde duyuluyor (HQ)

- **Dosyalar:** `Source/dsp/PitchCorrector.h`, `Source/dsp/TuneEngine.h`. Yama: `01_backdated_switch.patch`.
- **Sorun:** Yeni nota, histerezis ve bekleme süresi dolunca seçilir. Bu, sesin notalar arasındaki orta noktayı geçmesinden
  ortalama ~8 ms sonradır. Çıkış da notayı o kadar geç değiştiriyordu.
- **Değişiklik:** Karar kesinleşince, ses orta noktayı geçtikten sonraki analiz kayıtları da yeni notayı alır. Bu yalnız henüz
  çalınmamış kayıtlar için yapılır. Kararın kendisi (ne zaman ve hangi notaya geçileceği) değişmedi. Bu yüzden yanlış nota
  değişimi sayısı aynı kaldı.
- **Etki (MEASURED, 43 temiz geçiş, kesin doğru sentetik set):**
  - HQ'da duyulan geçiş medyanı 11,2 ms'den 6,3 ms'ye indi.
  - 15 ms'den geç geçişler %27,9'dan %14,0'a indi.
- **Ne zaman devrede:** Yalnız düzeltme anlık olduğunda (hard tune; retune zaman sabiti < 3 ms, kayma yok) ve yalnız HQ modunda.
- **REALTIME:** Bu değişiklik orada etkisiz; her kayıt analiz edildiğinde zaten çalınmış oluyor. REALTIME için değişiklik 3'e bak.
- **Varsayılan (%50) ve doğal ayarlar:** Çıkış bu değişiklik yüzünden değişmedi.

### 2. Çıkış güvenlik sınırlayıcısı (tepe koruması)

- **Dosya:** `Source/dsp/TuneEngine.h`.
- **Sorun:** 1.2.0'da sınırlayıcı yoktu. Tepesi −1 dBFS olan kayıtlar işlendiğinde 0 dBFS aşılıyordu (145 kayıt):
  - hard tune: 20 dosya (+6,0 dBFS'e kadar),
  - varsayılan: 14 dosya,
  - formant +12: 144 dosya (+7,9 dBFS'e kadar).
  Tepe seviyesi normal olan kayıtlarda bile (tepe −6 dBFS) hard tune'da 5 dosya 0 dBFS'i aşıyordu.
- **Değişiklik:** İşlenmiş sese, MIX'ten önce uygulanan bir sınırlayıcı:
  - Tavan: −1 dBFS (örnek tepesi).
  - Gecikme yok; bildirilen gecikme değişmedi.
  - Anında atak, 10 ms tutma, 80 ms bırakma; stereo bağlı.
  - Tavanın altında kazanç tam 1, yani çıkış bit bit aynı.
  - Bypass ve karışımın kuru kısmı dokunulmadan kalır.
- **Etki (MEASURED, 145 kayıt):** 9 ayarın hiçbirinde 0 dBFS'i aşan örnek kalmadı (aday 2'de REALTIME'da da: tipik ve sıcak
  seviyede 0 dosya).
  - **Sınır:** Örnekler arası tepe (true peak) formant +12'de +1,0 dBTP'ye kadar çıkabiliyor.
- **Normal seviyede (tepe −6 dBFS):**
  - natural: hiç devreye girmedi.
  - varsayılan: 145 dosyanın 2'sinde devreye girdi (en fazla 0,7 dB).
  - hard tune: 8 dosyada devreye girdi (en fazla 2,0 dB).
- **Duyulup duyulmadığı test edilmedi.** Kör dinleme paketi L01–L05 bunun için.

### 3. REALTIME: analiz 10 ms daha güncel (aday 2)

- **Dosyalar:** `Source/dsp/PitchDetector.h`, `Source/dsp/TuneEngine.h`. Yama: `03_rt_fresh_analysis.patch` (+ testler:
  `03_rt_fresh_analysis_tests.patch`).
- **Sorun (MEASURED + INSPECTED):** Algılayıcı her gecikme değerini, en uzun gecikmeye göre boyutlanmış tek bir çerçevenin
  ortasında karşılaştırıyordu. Bulunan perde bu yüzden ~22,5 ms (tenor) ile ~26 ms (bas) eskiydi. HQ'da 40 ms'lik gecikme bunu
  karşılıyor. REALTIME'da (20 ms) ise düzeltme, sesin kendisinden daha eski bir ölçüme göre yapılıyordu. Sonuç: hard tune'da
  duyulan geçiş medyanı 18,7 ms, geçişlerin %60'ı 15 ms'den geç.
- **Değişiklik:** Yalnız REALTIME'da, süregelen bir seste her gecikme en yeni 256 örneği kendisinden `tau` önceki örneklerle
  karşılaştırıyor. Ölçümün yaşı (256 + tau) / 2 oluyor (tenor 220 Hz: 13 ms; önce 22,5 ms). Üç koruma var:
  - Bir sesin başında ve ilk 8 adımında eski düzen kullanılır. En yeni pencere yeni sesteyken uzun gecikmeler öncesindeki
    boşluğa uzanıyor ve oktav yukarı okumayı kolaylaştırıyordu (kısa aradan sonra G4, 100 ms boyunca G5 okunuyordu).
  - En yeni ~5 ms çerçeveden 14 dB'den fazla sessizse (ses kesildi ya da sertçe bitti) o adım voiced sayılmaz. Bu kural olmadan
    sert biten seslerde yanlış nota değişimi oluşuyordu.
  - REALTIME → HQ geçişinde analiz kayıtlarının zaman sırası korunur. HQ daha geride analiz ettiği için "gelecekte" kalan
    REALTIME kayıtları atılır.
- **Etki (MEASURED):**
  - **Hard tune, REALTIME (43 temiz geçiş):**
    - duyulan geçiş medyanı 18,7 ms'den **11,7 ms**'ye indi; bu, 1.2.0 HQ'nun değerine (11,2 ms) denk;
    - |p95| 48,0 ms'den 38,0 ms'ye indi;
    - 15 ms'den geç geçişler %60,5'ten %46,5'e indi;
    - karar zamanı değişmedi (9,7 ms).
  - **Algılayıcı, REALTIME:**
    - gerçek kayıtlarda (145, referans izleyici) 20 ms'den uzun oktav hatası dizileri 86'dan 74'e indi;
    - kesin doğru yeniden sentezde (145) 32'den 26'ya, 30 ms'den uzunlar 14'ten 6'ya indi;
    - voiced isabeti 0,02–0,2 puan düştü;
    - yanlış voiced kesin doğru setlerde arttı (sentetik %0,25 → %0,37, yeniden sentez %0,03 → %0,09), gerçek kayıtlarda azaldı
      (referansa göre %11,7 → %11,2);
    - sentetik sette 5 dB SNR'lik gürültülü kayıtta 50 ms'den uzun bir oktav-aşağı dizisi oluştu (önce 0). O kayıttaki toplam
      oktav hatası süresi 0,224 s'den 0,198 s'ye indi.
  - **Yanlış nota değişimi (hard tune, 77 sentetik):** sabit notada dakikada 29,1'den 28,2'ye indi; vibratoda 664,8'den 666,0'a
    çıktı (+2 olay, %0,06). Ölçülebilir artış yok.
- **HQ:** Çıkış aday 1 ile bit bit aynı (EngineTest ve render karşılaştırması).
- **REALTIME'da çıkış her ayarda değişir**, çünkü ölçümün zamanı değişti. Doğal uçta vibrato aday 2'de %91,8'den %81,0'a
  düşmüştü; aday 3 (değişiklik 4) bunu %91,9'a geri getirdi.

### 4. İnsansı tarafta vibrato korunuyor (aday 3)

- **Dosya:** `Source/dsp/PitchCorrector.h`. Yama: `04_vibrato_keep_retune.patch`.
- **Sorun (MEASURED; 1.2.0'dan beri, her modda):**
  - Geniş vibratoda (VocalSet, ±60–110 ct) vibratonun tepesi komşu notaya 30 ct yaklaşınca hedef nota ona geçiyor ve geri dönüyor:
    tutulan notada saniyede 9–26 değişim, her ayarda.
  - Her değişim, notanın "uzun nota" yavaş düzeltme hızını (humanize) sıfırlıyordu. Düzeltme hızlanıp vibratoyu takip ediyor ve
    onu düzleştiriyordu.
  - ROBOTISM %80'de vibratonun yalnız %47'si kalıyordu.
  - Aday 2'de REALTIME %100'de %91,8 → %81,0 düşüş de buradan geliyordu (HQ 1.2.0'da da %80,3).
- **Değişiklik:** Ses titreşimsiz merkezinin etrafında salınıyorsa (son 400 ms içinde merkezin ±25 ct ötesine en az üç kez yön
  değiştirerek geçtiyse), nota değişimi tutulan notanın düzeltme hızını korur; sıfırlamaz.
  - Nota kararı (hangi nota, ne zaman) ve kayma süresi değişmedi; yanlış nota değişimi sayısı aynı.
  - Sıçramalar (≥ 1,5 yarım ses) eskisi gibi.
  - Etki, insansı tarafın ilk %15'inde yumuşakça devreye giriyor (ROBOTISM %50 → ~%65). Düğme sıçrama yapmaz.
  - %50 ve robotik taraf bit bit aynı.
- **Etki (MEASURED, 91 gerçek kayıt, gerçek ikili):**

  | ROBOTISM | Vibratonun korunan kısmı, aday 2 → aday 3 (REALTIME) | HQ | Çıkış geçiş süresi, REALTIME |
  |---|---|---|---|
  | %65 | %74,7 → %80,2 | %74,5 → %80,5 | 63,9 → 58,0 ms |
  | %80 | %46,9 → **%74,0** | %46,5 → %73,8 | 63,9 → 52,2 ms |
  | %100 | %81,0 → **%91,9** | %80,3 → %91,4 | 40,6 → 40,6 ms |

  - Nota merkezine uzaklık: ±0,9 ct içinde aynı.
  - Kesin doğru sentetik vibrato (her döngüde nota değiştiren ±50 ct): %80'de %32 → %75, %100'de %74 → %94. Komşu notaya
    yaklaşmayan vibratoda çıkış bit bit aynı.
- **Doğrulama (MEASURED):**
  - nota kararları aday 2 ile aynı (6 kayıt × %65 / %80 / %100 × REALTIME / HQ: 36 / 36);
  - %0 / %25 / %50'de çıkış bit bit aynı (24 / 24);
  - EngineTest 78 / 78 (insansı kayma testi 210 ms, değişmedi);
  - PluginTest 73 / 73, pluginval seviye 10 SUCCESS;
  - ASan + UBSan 0 bulgu.

## Değişmeyenler (MEASURED)

- Parametre kimlikleri, sırası (27), aralıkları, preset ve oturum biçimi (PluginTest 73 / 73).
- Gecikme: REALTIME 20 ms, HQ 40 ms.
- ROBOTISM %0–%50'de HQ çıkışı aday 1 ile bit bit aynı. Aday 1'de HQ yalnız hard tune geçişinde ve tavanın üstündeki tepelerde
  1.2.0'dan farklı. İnsansı tarafta aday 3, vibrato değişikliği yüzünden her iki modda da farklı.
- Nota kararının kuralı değişmedi (karar zamanı REALTIME'da da 9,7 ms). Aday 3'ün kararları aday 2 ile birebir aynı.
- REALTIME ↔ HQ geçişi: 132 durumda tekrar ya da atlama yok; 1.2.0 ile aynı.
- CPU: aday 2 / aday 1 oranının medyanı 0,98 (sanal makine, göreli). Ses iş parçacığında bellek ayırma yok.

## Denenip reddedilenler (MEASURED, belgede ayrıntılı)

Nota kararını hızlandıran üç aday yanlış nota değişimini artırdığı için alınmadı. Yol haritası bunu yasaklıyor. Sabit notada
yanlış değişim, 1.2.0'da dakikada 29:

| Aday | Değişiklik | Karar medyanı | Yanlış değişim (dk) |
|---|---|---|---|
| A | Histerezis 6 ct ve iki hop bekleme | 9,7 → 6,0 ms | 75 |
| B | "Kararlı hareket" kısayolu | zamanlama iyileşmedi | 39 |
| C | Bekleme süresi iki hop (HQ'da medyan 5,7 ms, p95 15,6 ms) | – | 53 |

REALTIME analizinin iki ara sürümü de reddedildi:

- **rt1:** Hizalama her zaman açık. Sert biten seslerde yanlış değişim oluşuyordu.
- **rt2:** Bitiş koruması eklenmiş hâli. Kısa aradan sonra oktav yukarı okuma oluşuyordu.

Doğal uçta vibrato için aday 3'ten önce denenip alınmayanlar:

- **nv1:** Düzeltme vibratosuz merkezi çekiyor. İnsansı nota geçişindeki kaymayı bozdu (EngineTest 210 → 18 ms).
- **Salınım koruması (`X1` yaması):** Ses notanın öbür yanına yakın zamanda geçtiyse kararı 90 ms beklet. Vibrato %80'de %72'ye
  çıktı, ama vibratosuz şarkıda da gerçek geçişler gecikti (medyan +15–20 ms).
- **Yakın varışa bekleme (1.1 benzeri):** Vibrato %80'de %42 → %31; kısmi bastırma daha da kötü.
- **Yumuşak değişim, kayma × 2 veya × 3:** Vibrato daha çok korundu, ama geçişlerin uç değerleri uzadı.
- Seçenekler, düzelticinin Python taklidiyle (`tools/decsim.py`) elendi. Bu taklit 210 360 adımın 4'ünde farklıydı. Seçilen
  seçenek C++'ta taklitle uyuştu: adımların %0,036'sı 0,01 yarım sesten fazla farklı, hepsi yuvarlama farkı.
