# VSTM Tune 1.4.0 – Kalite raporu

- **Durum:** aday, yayımlanmadı. Kontrol listesi: `VSTM_Tune_1.4.0_Release_Checklist.md`.
- **Temel:** 1.3 aday 3. Değişikliklerin listesi: `VSTM_Tune_1.4.0_Changelog.md`.
- **Kanıt etiketleri:** **MEASURED** ölçüm, **INSPECTED** kaynak incelemesi.
- **Yapılmayanlar:** dinleme, DAW testi, Windows / macOS derlemesi. Hiçbir sonuç "dinlendi" anlamına gelmez.

## 1. Temel ve yöntem

| | |
|---|---|
| 1.4 derlemesi | `artifacts/audio-runs/2026-11-25_v14/bin-final` (Linux, Release) |
| Karşılaştırma | 1.3 aday 3 (`2026-11-24_q13/bin-cand3`). CPU'da aday 2'nin BenchProbe'u; aday 3'ün ayrı bir BenchProbe'u yok. |
| Ölçümde kullanılan ara derleme | Algılayıcı ölçümleri `bin-og1` ile yapıldı. `bin-og1` ile `bin-final`, 8 dosya × 2 modda (16 render) çıkış WAV'ı ve perde CSV'si olarak bit bit aynı (MEASURED). |

**Ölçüm setleri** (hepsi önceki sürümlerle aynı; yeniden üretilebilir):

- **Sentetik, 77 kayıt:** perdesi kesin bilinen sentetik ses (melodi, vibrato, bas, fry, gürültü, register geçişi).
- **Yeniden sentez, 145 kayıt:** gerçek kayıtların perde eğrisiyle yeniden sentezlenmiş ses, perdesi kesin bilinen.
- **Gerçek kayıt, 145:** VocalSet 137, RAVDESS 6, kullanıcının iki kaydı. Referans perde, iki ayrı perde algılayıcının
  uyuştuğu yerlerde; diğer yerler ölçülmez.
- **Nota geçişi:** sentetik setteki 47 geçiş (43'ü temiz).
- **Yanlış nota değişimi:** sentetik 77, hard tune.
- **REALTIME ↔ HQ geçişi:** 132 durum.
- **CPU:** 218 yapılandırma (örnekleme hızı, blok, kanal, mod, ayar, örnek sayısı).

**Araçlar:** `tests/gt_eval.py`, `transition_decompose.py`, `false_switch.py`, `rt_switch_eval.py`, BenchProbe,
EngineTest, PluginTest, pluginval 1.0.4. Yeni: `render_diff/render_diff.py` (bu rapor, bölüm 3.5).

## 2. Şartname durumu

| Madde | Durum | Kanıt |
|---|---|---|
| P0 oktav sağlamlığı | Değişiklik yapıldı. Toplamda iyileşme; gerçek kayıtlarda dosyaya göre karışık. | Bölüm 3 |
| P0 fry sağlamlığı | Ölçüldü; düzeltme bulunamadı. Bilinen sınır. | Bölüm 4 |
| P0 PSOLA geçiş sürekliliği | PSOLA yolu değişmedi. Mevcut kapılar geçti. **Yeni tık ölçüm kampanyası yapılmadı.** | Bölüm 6 |
| P1 hedef nota kararı | Karar kuralı değişmedi (vibrato sınırı 1.3 aday 3'te). Yanlış değişim ve geçiş zamanlaması ölçüldü. | Bölüm 6 |
| P1 sıçrama bastırma | Oktav dizileri ve ≥ 15 yarım seslik sıçramalar ölçüldü; oktav koruması bunları etkiliyor. Ayrı bir bastırıcı eklenmedi. | Bölüm 3.3–3.4 |
| P1 güvene duyarlı işleme | Ölçüldü; uygulanmadı. Kesinlik hataları yeterince ayırmıyor. | Bölüm 5 |
| P2 regresyon seti | Mevcut setler kullanıldı ve genişletildi: fry seti, kesinlik analizi, 145 kayıtta çıkış farkı. Ayrı bir paket hâline getirilmedi. | Bölüm 1, 3.5, 4 |
| P3 performans / güvenlik | CPU, bellek ayırma, gecikme, pluginval, `.drp` ve yetki dosyası dayanıklılığı ölçüldü. | Bölüm 6–8 |
| `.drp` preset biçimi | Yapıldı. | Bölüm 7 |
| İmzalı admin yetkisi | Yapıldı. | Bölüm 8 |

## 3. Oktav koruması (P0)

### 3.1 Kök neden

- **MEASURED (sentetik, 5 dB SNR gürültü):** Ses kısa bir an unvoiced sayılıyor, sonra bir oktav aşağıda yeniden yakalanıyordu.
  Aynı kayıtta 3 kat periyot (−19 yarım ses) hatası da vardı.
- **INSPECTED:** Oktav-aşağı koruması yalnız önceki adım voiced iken çalışıyordu ve yalnız 2 katı denetliyordu.
  - Kısa bir unvoiced boşluk korumayı sıfırlıyordu.
  - 3 kat periyot hiç denetlenmiyordu.

### 3.2 Değişiklik (`Source/dsp/PitchDetector.h`)

- **Boşluktan sonra da:** aşağı yönlü koruma, kısa bir boşluktan sonra da (≤ 36 analiz adımı, ~100 ms) son voiced periyoda göre
  çalışır.
- **3 kat:** oran 2,7–3,3 ise periyodun üçte biri de denenir.
- **Kural aynı:** Kısa periyot, ancak yeterince periyodikse ve uzun periyot ezici biçimde daha periyodik değilse seçilir. Yukarı
  yönlü kontrol değişmedi.
- **Maliyet:** analiz adımı başına birkaç karşılaştırma. Bellek ayırma yok.

### 3.3 Algılayıcı doğruluğu (MEASURED, hard tune, aralık kayda göre)

Her hücrede: aday 3 → 1.4.

| Set | Oktav-aşağı | Oktav-yukarı | Kaba (> 1 yarım ses) | Oktav dizileri ≥ 20 / 30 / 50 ms | Voiced isabeti |
|---|---|---|---|---|---|
| Sentetik, REALTIME | %0,062 → %0,044 | %0 → %0 | %0,315 → %0,306 | 5/2/1 → 4/1/0 | %99,05 → %99,03 |
| Sentetik, HQ | %0,070 → %0,059 | %0 → %0 | %0,333 → %0,320 | 7/4/0 → 6/3/0 | %99,16 → %99,13 |
| Yeniden sentez, REALTIME | %0,004 → %0 | %0,057 → %0,057 | %0,161 → %0,163 | 26/6/1 → 23/6/1 | %99,02 → %99,03 |
| Yeniden sentez, HQ | %0,004 → %0 | %0,071 → %0,071 | %0,159 → %0,160 | 32/14/2 → 29/14/2 | %99,04 → %99,05 |
| Gerçek kayıt, REALTIME | %0,144 → %0,135 | %0,075 → %0,076 | %0,821 → %0,822 | 74/29/11 → 72/28/10 | %96,72 → %96,72 |
| Gerçek kayıt, HQ | %0,141 → %0,133 | %0,083 → %0,084 | %0,834 → %0,825 | 86/31/12 → 82/30/12 | %96,92 → %96,92 |

**Oktav + kaba hata toplamı** (adımlara göre ağırlıklı):

- 6 set × mod birleşiminin hepsinde azaldı.
- Gerçek kayıtta değişim küçük: REALTIME −0,006, HQ −0,017 puan.

**Gürültülü sentetik kayıt** (değişikliğin hedefi):

- REALTIME: oktav-aşağı %7,26 → %5,33, kaba %0,98 → %0, ≥ 50 ms oktav dizisi 1 → 0.
- HQ: oktav-aşağı %8,12 → %7,17, kaba %1,74 → %0,20.
- Voiced isabeti 1,5–2,6 puan düştü: yanlış oktavda voiced sayılan adımlar artık unvoiced.

### 3.4 Dosya başına (MEASURED)

**Oktav + kaba hatanın değiştiği dosya sayısı:**

| Set | İyileşen | Kötüleşen | Değişmeyen |
|---|---|---|---|
| Sentetik, REALTIME / HQ | 4 / 4 | 0 / 0 | 73 / 73 |
| Yeniden sentez, REALTIME / HQ | 8 / 7 | 4 / 1 | 133 / 137 |
| Gerçek kayıt, REALTIME / HQ | 8 / 10 | 7 / 4 | 130 / 131 |

**Gerçek kayıtlarda en büyük değişimler** (puan, oktav + kaba):

- **İyileşen:**
  - kullanıcının stem'i: REALTIME −0,27, HQ −0,38. Oktav-aşağı hata REALTIME'da %1,60 → %1,37.
  - VocalSet f8 konuşur gibi: REALTIME −0,23, HQ −0,35.
  - VocalSet f8 fry arpej: HQ −0,66. Oktav-aşağı hata %1,73 → %1,07.
- **Kötüleşen:**
  - VocalSet f3 konuşur gibi: REALTIME +0,32 (%0,52 → %0,84), HQ +0,19.
  - RAVDESS 1, REALTIME: +0,20.
  - VocalSet m2 fry arpej, REALTIME: +0,19.
  - VocalSet m6 Caro vibrato, REALTIME: +0,12.

**Kötüleşenlerde hata türü:**

- Kötüleşen dosyalarda artan hata oktav değil; 15 yarım sesten büyük kısa sıçramalar.
- Gerçek kayıtlarda ≥ 15 yarım seslik hata: REALTIME %0,275 → %0,276, HQ %0,279 → %0,272.
- Bu hatalar dosyalar arasında yer değiştiriyor. Örneğin REALTIME'da stem'de 16 adım azaldı, m6 Caro vibrato'da 12, f3 konuşur gibi
  söylemede 11 adım arttı.
- Bu dosyalarda 1.3 ile 1.4'ün çıkış perdesi farkı 17–20 yarım ses. Bu, 3 kat periyot değişimiyle tutarlı (bölüm 3.5).
- **Hipotez, doğrulanmadı:** koruma, boşluktan önceki periyot zaten yanlışsa o hatayı boşluktan sonra da sürdürüyor olabilir.

### 3.5 Çıkış sesi farkı (MEASURED, 145 gerçek kayıt)

Kaynak: `artifacts/audio-runs/2026-11-25_v14/render_diff/` (betik, dosya başına CSV, özet JSON). Aynı kuru dosya, aynı ayarlar,
iki derleme.

| Ayar | Bit bit aynı | Farklı dosyalarda fark sinyali, medyan / en yüksek | Çıkış perdesi farkı ≥ 1 / ≥ 6 yarım ses (iki tarafta da ses olan adımlar) | En çok fark |
|---|---|---|---|---|
| Hard, REALTIME | 109 / 145 | −27,0 / +2,1 dB | %0,073 / %0,053 | stem %0,83 |
| Hard, HQ | 106 / 145 | −28,6 / −1,5 dB | %0,073 / %0,054 | RAVDESS 5 %1,33 |
| Varsayılan | 104 / 145 | −25,4 / +2,8 dB | %0,074 / %0,055 | stem %0,85 |

- **Perde farkları kısa olaylar:** bir dosyada genelde 8–16 analiz adımı (~20–40 ms). Stem'de 168 s içinde 350 adım.
- **Fark sinyali:**
  - Bir perde olayından sonra grain fazı kaydığı için birkaç dosyada fark sinyali sinyal düzeyine çıkıyor (+2,8 dB'e kadar).
  - Perde aynıyken de böyle bir fark oluşabilir.
  - Bu sayı **duyulan farkın ölçüsü değildir**.
- **Dinleme:** `VSTM_Tune_1.4.0_Blind_Listening_Pack` bu olayları içerir. Her pakette 1.3 aday 3, 1.4 ve kuru ses; seviyeler
  eşitlenmiş, adlar rastgele.

  | Paket | Malzeme | Mod | Referansa göre 1.4 |
  |---|---|---|---|
  | O01 | kullanıcının stem'i, 10 s | REALTIME | iyileşen |
  | O02 | gürültülü sentetik | REALTIME | iyileşen (kesin doğru) |
  | O03 | f3 konuşur gibi söyleme | REALTIME | kötüleşen |
  | O04 | m2 fry arpej | REALTIME | kötüleşen |
  | O05 | f8 fry arpej | HQ | iyileşen |

  Hangisinin daha iyi duyulduğu **bilinmiyor**. Sonuç USER-REPORTED olarak bu ölçümlerin önüne geçer.

### 3.6 Karar

Koruma **alındı**. Gerekçeler:

- Oktav + kaba hata toplamı 6 set × mod birleşiminin hepsinde azaldı.
- 50 ms'den uzun oktav dizileri azaldı.
- 1.3'ün bilinen sorunu 9 (gürültüde ≥ 50 ms oktav dizisi) kapandı.
- Geçiş zamanlaması, yanlış değişim, CPU ve gecikme etkilenmedi.

**Çekince:**

- REALTIME'da gerçek kayıtlarda dosya başına sonuç neredeyse yarı yarıya (8 iyileşen, 7 kötüleşen).
- Dinleme sonucu kötüleşen paketlerde (O03, O04) 1.3 tercih edilirse, sonraki adımda şu varyant ölçülmeli: 3 kat
  korumasını yalnız kesintisiz seste çalıştırmak, boşluktan sonra yalnız 2 katı denetlemek.
- Geri alma: `artifacts/audio-runs/2026-11-25_v14/PitchDetector.before_octguard.h`.

## 4. Fry (P0): ölçüldü, düzeltilmedi

### 4.1 Hard tune'da hedef nota değişimi (MEASURED, saniyede, 1.4)

| Kayıt | REALTIME | HQ |
|---|---|---|
| Sentetik fry | 27,0 | 26,8 |
| Sentetik creak | 14,8 | 15,4 |
| Sentetik bas cümleleri (3 kayıt) | 3,7–4,4 | 4,4–4,6 |
| Sentetik bas, varsayılan aralık | 2,8 | 3,7 |
| VocalSet fry arpejleri (14 kayıt) | 7,7–75,8 | – |

Fry'da algılayıcı periyodu düzensiz buluyor. Hard tune her sıçramada hedef notayı değiştirip "titreyen" bir çıkış veriyor.

### 4.2 Denenen fry kapısı (alınmadı)

Fry algılanınca düzeltmeyi tutmak ya da yumuşatmak için iki ölçüt denendi:

1. **85 Hz altı perde ve adımdan adıma sıçrama** (`TuneEngine.frygate_rejected.h`):
   - Fry kayıtlarında adımların yalnız %0–0,1'inde devreye girdi.
   - Nota değişim sayısı değişmedi.
   - Çoğu fry kaydında algılanan perde 85 Hz'in üstünde (medyan MIDI 40–75).
2. **~110 ms'lik pencerede 4 ya da daha çok büyük sıçrama** (analiz):
   - 14 fry kaydında bu tür adımların payı: 2 kayıtta %52 ve %75; diğer 12'sinde %0–7,8.
   - 91 normal kayıtta medyan %0,45, en yüksek %12 (stem) ve %10 (RAVDESS 5).
   - Normal kayıtların en kötüleri, fry kayıtlarının çoğundan yüksek. Bu ölçüt normal vokalde yanlış devreye girer.

**Sonuç:** fry'ı normal şarkıdan güvenle ayıran bir ölçüt bulunamadı. Kapı geri alındı. Fry'da hard tune titremesi **bilinen
sınır**.

## 5. Güven (kesinlik) analizi (P1, MEASURED)

**Yöntem:**

- Sentetik 77, REALTIME, aday 3.
- İki tarafta da ses olan 120 278 adım; > 1 yarım ses hata 468 (%0,39), oktav hatası 74.

| Kesinlik eşiği | İşaretlenen adımlar | Yakalanan hatalar | İşaretlenenlerde hata oranı |
|---|---|---|---|
| < 0,50 | %0,03 | %6,4 | %73,2 |
| < 0,60 | %0,18 | %14,5 | %30,6 |
| < 0,70 | %0,81 | %28,8 | %13,8 |
| < 0,80 | %3,31 | %47,0 | %5,5 |
| < 0,90 | %10,66 | %75,0 | %2,7 |

**Hatanın en çok olduğu kayıtlarda** doğru ve hatalı adımların medyan kesinliği:

| Kayıt | Doğru | Hatalı |
|---|---|---|
| Fry | 0,936 | 0,887 |
| 5 dB gürültü | 0,797 | 0,791 |
| Soprano register geçişi | 0,986 | 0,858 |
| Creak | 0,891 | 0,675 |

**Sonuç:**

- Hataların çoğunu yakalayan bir eşik, doğru adımların da büyük kısmını işaretliyor. Örneğin < 0,90: hataların %75'i, ama
  adımların %10,7'si.
- Kesinliğe göre düzeltmeyi kısmak, doğru notalarda düzeltmeyi zayıflatır.
- Fry ve gürültüde zaten işe yaramaz.
- Bu yüzden 1.4'te uygulanmadı. Kesinlik, geliştirici panelinde canlı gösteriliyor.

## 6. Regresyon (MEASURED, 1.4 ile 1.3 aday 3)

### Nota geçişi (hard tune, 43 temiz geçiş)

| | Duyulan geçiş medyan | \|p95\| | > 15 ms payı | Karar medyanı |
|---|---|---|---|---|
| REALTIME | 11,65 ms | 38,02 ms | %46,5 | 7,97 ms |
| HQ | 6,33 ms | 29,72 ms | %14,0 | 8,01 ms |

- Özet sayıları 1.3 ile aynı.
- HQ'da 47 geçişin hepsi aynı.
- REALTIME'da 47 geçişin biri farklı: duyulan geçiş 0,5 ms erken (6,15 → 5,65 ms).

### Diğer kapılar

| Kapı | 1.3 aday 3 | 1.4 |
|---|---|---|
| Yanlış nota değişimi, sabit notada (dakikada) | 28,18 | 27,62 |
| Yanlış nota değişimi, vibratoda (dakikada) | 665,98 | 666,08 |
| Tüm nota değişimleri (dakikada) | 854,3 | 853,8 |
| REALTIME ↔ HQ geçişi: 132 durumda tekrar / atlama / sıra bozulması / hizalama / dürtü / sayı dışı değer | 0 | 0 (perde sapması en çok 0,01 sent) |
| CPU, 1.4 / aday 2, ortalama süre medyanı | – | REALTIME 0,989 (110 yapılandırma), HQ 1,001 (108) |
| CPU, p95 süre oranı medyanı | – | REALTIME 0,996, HQ 0,998 |
| Tek çekirdek payı, medyan (en çok) | – | REALTIME %2,4 (%6,7), HQ %2,6 (%7,1) |
| Ses iş parçacığında bellek ayırma | 0 | 0 |
| Gecikme | 20 / 40 ms | 20 / 40 ms |
| EngineTest | 78 / 78 | 78 / 78 |
| PluginTest | 73 / 73 | 97 / 97 |
| pluginval (Linux VST3, seviye 10) | SUCCESS | SUCCESS (VSTM Tune 1.4.0) |
| Derleme uyarısı | 0 | 0 |

**Notlar:**

- Yanlış değişimin çoğu sentetik bas (fry / creak) ve geniş bas vibratosunda. Bölüm 4'teki bilinen sınırla aynı kaynak.
- CPU ölçümü paylaşılan bir sanal makinede yapıldı; oranlar tek tek yapılandırmalarda ±%20 oynuyor (p10–p90: 0,83–1,22). Gerçek
  donanımda ölçülmedi.

## 7. `.drp` preset biçimi (MEASURED, PluginTest 3h)

- **Kayıt / okuma:** 24 parametre geri geldi, en büyük normalize fark 0.
  - Dosya 247 bayt, sihirli bayt dizisiyle başlıyor.
  - Parametre adları ya da değerleri metin olarak görünmüyor.
- **Bozuk dosyalar:**
  - 247 kesilmiş hâl, 247 tek-bayt değişikliği ve 2000 rastgele dosya: kabul edilen 0.
  - 2 MB'a açılan 2087 baytlık dosya, başka eklentinin dosyası, geçersiz parametre kimliği: reddedildi.
- **Sınırlar:**
  - `nan` / `inf` / dev değerler varsayılana döndü ya da kırpıldı; ses sonlu kaldı.
  - 65 620 bayttan büyük dosya okunmadı.
- **Geçiş:**
  - 1.0–1.3 `.vstmpreset` yükleniyor.
  - Aynı adda `.drp` varsa liste tek giriş gösteriyor.
  - Yeniden adlandırma `.drp`'ye geçiriyor.
- **İçe aktarma:** boş bir adla kopyalıyor (`Ad (2)`, `Ad (3)`), hiçbir dosyanın üzerine yazmıyor. Yetki dosyasını reddediyor
  ve hiçbir şey yazmıyor.

Biçimin ayrıntısı: `VSTM_Tune_1.4.0_DRP_Format.md`.

## 8. İmzalı admin yetkisi (MEASURED, PluginTest 3i ve çapraz kontrol)

- **Geçerli dosya:** doğrulandı. İçe aktarılınca bayt bayt saklandı; yeni bir örnekte yeniden doğrulandı.
- **Bozulma:** 147 baytlık dosyanın 1176 tek-bit değişikliği ve 147 kesilmiş hâli: kabul edilen 0.
- **Reddedilenler:**
  - imzasız, güvenilmeyen anahtar, sahte imza, bilinmeyen rol;
  - eksik yetki, süresi dolmuş, henüz geçerli değil, iptal edilmiş;
  - başka eklentinin dosyası, preset.
- **Saklanan kopya:**
  - Reddedilen içe aktarma önceki yetkiyi değiştirmedi.
  - Saklanan kopya süresi dolunca ya da diskte değişince çalışmayı bıraktı.
  - "Yetkiyi kaldır" dosyayı sildi.
- **Anahtarlar:**
  - Dağıtılan derleme yalnız kendi açık anahtarına güveniyor; test anahtarı reddedildi.
  - Gerçek anahtarla `tools/drp_auth.py` ile verilen dosya (`VSTM_Tune_Admin.drp`) eklentide doğrulandı: anahtar
    `a2f22028b35ac70d`.
- **Uzman anahtarlar:**
  - Yetkisiz reddedildi; yetkiyle çalıştı.
  - Oturuma kaydedilmedi; yetki bitince açık konuma döndü.
- **Geliştirici paneli:** yetkisiz görünmüyor, yetkiyle açılıyor (ekran görüntüsü `09_developer_panel.png`).
- **Çapraz kontrol:**
  - RFC 8032 test vektörü kabul edildi; değiştirilmiş hâli reddedildi.
  - Python `cryptography` ile atılan 200 imzanın 200'ü kabul edildi; değiştirilmiş mesajların 0'ı kabul edildi.

Tasarım ve sınırlar: `VSTM_Tune_1.4.0_Authorization.md`.

## 9. Test edilmeyenler (NOT TESTED)

- **Dinleme:** hiçbir 1.4 sonucu dinlenmedi; dinleme paketi hazır.
- **Gerçek DAW:** REAPER, FL Studio, Ableton, Logic; otomasyon, preset menüsü ve dosya seçici gerçek host'ta denenmedi.
- **Platformlar:** Windows ve macOS derlemesi (kullanıcı isteğiyle bu turda yapılmadı). `.drp` ve yetki kodu platformdan bağımsız
  yazıldı (little-endian okuma, JUCE dosya API'si). Windows'ta saklama yolu `%APPDATA%\VSTM Audio\VSTM Tune\` olmalı
  (INSPECTED, denenmedi).
- **Diğer:**
  - Gerçek donanımda CPU.
  - Ana dili konuşanlarca çeviri kontrolü (de / fr / it / ru / zh).
  - Ekran okuyucu ile geliştirici paneli.

## 10. Bilinen sorunlar

1. **Fry'da hard tune titremesi:** saniyede 7–76 nota değişimi (bölüm 4). 1.4 değiştirmedi.
2. **Oktav koruması gerçek kayıtlarda karışık:** REALTIME'da 8 iyileşen, 7 kötüleşen dosya (bölüm 3.4). Dinlemeyle
   doğrulanmalı.
3. **Gürültüde voiced isabeti 1,5–2,6 puan düştü:** gürültülü seste bazı anlar artık düzeltilmiyor (bölüm 3.3).
4. **Yüksek soprano:** E6 civarında alt harmonik hataları 1.2.0'dan beri var (sentetik soprano register geçişi). 1.4 değiştirmedi.
5. **Taşınabilirlik ve süre denetimi:**
   - Yetki dosyası kopyalanırsa başka bilgisayarda da çalışır.
   - Sistem saatini geri almak süreyi uzatır (Yetki belgesi, "Sınırlar").
6. **Özel anahtarın kökeni:** bu bulut oturumunda üretildi. Daha sıkı güvenlik için çevrimdışı yeni bir anahtar önerilir
   (Yetki belgesi).

## 11. Dosyalar

Hepsi `artifacts/audio-runs/2026-11-25_v14/` altında:

| Konu | Dosya |
|---|---|
| Algılayıcı ölçümleri | `gt_eval/` (`*_og1_*` = 1.4, `*_cand3_hq` = aday 3; aday 3 REALTIME `2026-11-24_q13/gt_eval/*_rt3`) |
| Çıkış farkı | `render_diff/` |
| Fry | `fry/fry_eval.txt`, `conf/` |
| Geçiş, yanlış değişim, mod geçişi | `transitions/`, `false_switch/`, `rtswitch_final.*` |
| CPU | `bench/v14_matrix_b.csv`, `bench/cand3ref_matrix_b.csv` |
| Testler | `engine_test_final.log`, `plugintest_final.log`, `plugintest_bitflip.log` (1176 bitlik yetki testi), `pluginval_final/` |
| Geri alma | `PitchDetector.before_octguard.h`, `TuneEngine.before_frygate.h` |
| Dinleme paketi | `VSTM_Tune_1.4.0_Blind_Listening_Pack/` (`listening_plan_1.4.0.json`) |
