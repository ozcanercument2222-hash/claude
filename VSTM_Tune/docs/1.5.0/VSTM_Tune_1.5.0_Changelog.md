# VSTM Tune 1.5.0 – Değişiklik günlüğü (aday, çalışma sürüyor)

**Yayımlanmadı, commit edilmedi.** Temel: 1.4.0 adayı (commit `370e919`). Sürüm numarası henüz 1.4.0.

**Kullanıcının sırası:** önce var olan özellikleri iyileştir, sonra kısmen olanları, en son olmayanları
(`docs/roadmap/VSTM_Tune_Roadmap_Gap_Analysis.md`).

**Kanıt etiketleri:** **MEASURED** ölçüm, **INSPECTED** kaynak incelemesi. Dinleme, DAW ve Windows testi yapılmadı.

## 1. Windows derlemesi düzeltildi (madde 86)

- **Sorun (MEASURED, CI koşusu 27):** 1.4.0 adayı macOS ve Linux'ta derlendi ve testleri geçti; Windows'ta derleme durdu.
- **Neden:** `Source/Drp.cpp` POSIX'e özgü `ssize_t` türünü kullanıyordu. JUCE, bu türü MSVC'de yalnız kendi `juce` ad alanında
  tanımlıyor.
- **Düzeltme:** `juce::pointer_sized_int`; üç platformda aynı tür.
- **Durum:**
  - Linux'ta derleme temiz ve PluginTest geçiyor.
  - Windows'ta derlendiği ancak bir sonraki CI koşusunda görülür (kullanıcı isteğiyle bu turda derleme / push yok).
  - 1.4'te eklenen bütün kod başka POSIX'e özgü ya da C++20'ye özgü yapı için tarandı; bulunmadı (INSPECTED).

## 2. Vibrato Scale ayarına uyuyor (madde 23–25: koru / azalt / güçlendir)

**Sorun (MEASURED, 1.4):**

- **%0 vibratoyu silmiyordu.** Nota merkezi, girişin 3 Hz'lik tek kutuplu alçak geçirgen süzülmüş hâliydi ve 5,5 Hz'lik bir
  vibratonun yaklaşık yarısını taşıyordu. Varsayılan ayarlarda vibratonun %40–46'sı kalıyordu.
- **%150–200'de büyütülmüş vibrato komşu notanın sınırını geçiyordu.** Hedef nota her salınımda değişiyordu.
  - Doğal tarafta %200, %150'den **daha az** vibrato veriyordu; çıkış merkezi +4 sent kayıyordu.
  - Hard tune'da tril oluşuyordu.
  - Gerçek vibratolu kayıtlarda nota değişimi %100'e göre neredeyse iki katına çıkıyordu.

**Değişiklik** (`Source/dsp/PitchCorrector.h`):

- **Nota kararı** büyütülmüş vibratoyu hiç görmüyor: ölçek karar için 1 ile sınırlı. %100 ve altında karar eskisiyle **birebir
  aynı**.
- **Çıkış** yalnız vibrato bandını ölçekliyor: girişin 1,5 Hz'lik iki kutuplu alçak geçirgen süzülmüş hâlinin üstündeki kısım.
  Bu süzgeçte 5,5 Hz'lik bir vibrato −23 dB.
- **%100'de (varsayılan)** iki yol da eski ifade; ses değişmiyor.
- **A/B anahtarı:** `vibratoBand` (motor parametresi; WavProbe `vibband=0` eski davranış). Geliştirici paneline henüz
  eklenmedi.

**Varsayılan ses aynı (MEASURED):**

- 145 gerçek + 77 sentetik kayıt × 3 ayar (hard REALTIME, hard HQ, varsayılan) = 666 render; hepsi 1.4 ile **bit bit aynı**.
- Bu yüzden varsayılan ayarlardaki bütün regresyon kapıları yapı gereği aynı: algılama doğruluğu, geçiş zamanlaması, yanlış
  nota değişimi, mod geçişi.

**Sentetik vibrato** (MEASURED; motorun çıkış perdesi, aynı derlemede %100'e oranla):

| Durum | %0 | %50 | %100 | %150 | %200 |
|---|---|---|---|---|---|
| A3, 5,5 Hz, ±35 ct, varsayılan, 1.4 | 0,48 | 0,66 | 1,00 | 1,39 | 0,97 (merkez +3,9 ct) |
| aynı, **1.5** | **0,07** | **0,47** | 1,00 | **1,53** | **2,06** (merkez 0) |
| A3, 4,5 Hz, ±25 ct, varsayılan, 1.4 / **1.5** | 0,55 / **0,10** | 0,70 / **0,46** | 1,00 | 1,36 / **1,54** | 1,74 / **2,08** |
| A3, 6,5 Hz, ±60 ct, insansı %75, 1.4 / **1.5** | 0,42 / **0,05** | 0,62 / **0,48** | 1,00 | 1,14 / **1,52** | 1,53 / **2,04** |
| Hard tune %200 (giriş vibratosu / çıkış) | 1.4: 1,78–1,81 (tril) | | | | **1.5: 0 (düz)** |

**Gerçek kayıtlar** (MEASURED; 57 vibratolu kayıt, VocalSet + kullanıcının iki kaydı; Key Auto, kilitlenene kadar kromatik):

| | %0 | %50 | %100 | %150 | %200 |
|---|---|---|---|---|---|
| Nota değişimi / dakika (medyan), varsayılan, 1.4 | 445 | 631 | 916 | 1364 | 1774 |
| aynı, **1.5** | 445 | 631 | 916 | **916** | **916** |
| Hard tune, 1.4 | 471 | 648 | 964 | 1432 | 1808 |
| Hard tune, **1.5** | 471 | 648 | 964 | **964** | **964** |

**Testler:**

- EngineTest'e 3 kontrol eklendi (78 → 81):
  - %0 vibratoyu siler;
  - %200 nota değişimi olmadan iki katına çıkarır;
  - hard tune %200'de düz kalır.
- Üçü de eski davranışta (`vibratoBand = false`) **kalıyor**: %0'da 25,8 ct p-p kalıyor; %200'de 47 ve 55 nota atlaması.
  Yani kontroller bu kusurun geri dönmesini yakalar.
- PluginTest 97/97.
- Derleme uyarısı 0.
- **CPU:** analiz adımı başına 1 `exp` ve 4 çarpma-toplama (INSPECTED; ölçülmedi).

**Kalan sınır (1.4'ten beri var, bu değişiklik dokunmuyor):**

- %100'ün altında nota kararı hâlâ 3 Hz'lik merkezle süzülmüş perdeyle veriliyor.
- **Geç karar (MEASURED, 12 temiz geçiş, doğal ayar):** %0'da medyan 94 ms, %100'de 41 ms.
- **Atlamalar:** geniş vibratoda nota atlamaları sürüyor (kromatik tonda %0'da dakikada 445).
- **Neden şimdi değil:** kararı daha vibrato-dışı bir referansa bağlamak atlamaları azaltır ama geçişleri ~180 ms geciktirir.
  Dört hızlı karar varyantı da denendi, alınmadı (bölüm 5).

**Dinleme:** `VSTM_Tune_Vibrato_Scale_Blind_Listening_Pack`, 4 paket:

- V01: %200, varsayılan, kadın;
- V02: hard tune %200, erkek;
- V03: %0, varsayılan, erkek;
- V04: %50, varsayılan, kadın.

Dinlenmedi.

## 3. 96 / 192 kHz'de ince perde doğruluğu (madde 87)

**Sorun (MEASURED, 1.4):**

- Algılayıcının tam hızdaki karşılaştırma pencereleri (ince ayar ve periyodiklik denetimleri) en az 256, en çok 2048 örnekle
  sınırlıydı: 48 kHz'de 5,3–43 ms.
- 48 kHz'in üstünde aynı sınırlar süre olarak kısalıyordu: 96 kHz'de yarısı, 192 kHz'de dörtte biri.
- **Etkilenen sesler:**
  - yüksek sesler: pencere alt sınıra takılıyor;
  - 192 kHz'de bas sesler: pencere üst sınıra takılıyor.
- **Sentetik soprano, ince doğruluk medyanı:** 48 kHz'de 3,7 ct, 96 kHz'de 6,6 ct, 192 kHz'de 7,2 ct.

**Değişiklik** (`Source/dsp/PitchDetector.h`):

- Sınırlar 48 kHz'deki sürelerini koruyor: 96 kHz'de 512–4096, 192 kHz'de 1024–8192 örnek.
- 48 kHz ve altında hiçbir şey değişmiyor.

**Ölçümler (MEASURED):**

- 145 gerçek kayıt (44,1 / 48 kHz) ve 77 sentetik kayıt (48 kHz) × 3 ayar = 666 render; hepsi bir önceki derlemeyle bit bit
  aynı.
- **Sentetik 77, 96 / 192 kHz'e yeniden örneklenmiş:**

| | 48 kHz | 96 kHz önce → sonra | 192 kHz önce → sonra |
|---|---|---|---|
| İnce doğruluk medyanı, REALTIME | 4,73 ct | 5,35 → **4,72** | 5,70 → **4,72** |
| p95, REALTIME | 19,8 ct | 21,0 → **19,6** | 23,6 → **19,6** |
| Soprano medyanı / p95 | 3,7 / 13,3 | 6,6 / 26,2 → **3,9 / 13,4** | 7,2 / 30,3 → **3,8 / 13,5** |
| Bas medyanı | 5,15 | 5,25 → 5,25 | 6,58 → **5,20** |
| Kaba hata, REALTIME | %0,306 | %0,326 → %0,319 | %0,386 → **%0,331** |

- HQ'da sonuçlar aynı yönde.
- Oktav hatası, voiced isabeti ve yanlış voiced değişmedi.
- **CPU** (BenchProbe matrisi, yapılandırma başına 3 s, paylaşılan sanal makine; yeni / eski ortalama süre medyanı):

| Örnekleme hızı | REALTIME | HQ | Tek çekirdek payı medyanı, REALTIME |
|---|---|---|---|
| 44,1 / 48 kHz | 0,95 | 0,98 | %1,47 → %1,43 |
| 88,2 / 96 kHz | 0,98 | 1,01 | %2,47 → %2,46 |
| 176,4 / 192 kHz | **1,08** | **1,08** | %4,44 → %4,99 |

  - 48 kHz ve altında kod yolu aynı; oranlar ölçüm gürültüsü.
  - 192 kHz'de bedel ~%8: daha uzun karşılaştırma pencereleri.
  - Bellek ayırma 0, sayı dışı değer yok, gecikme 20 / 40 ms.
- **Testler:** EngineTest 81/81, PluginTest 97/97, derleme uyarısı 0.

## 4. Ölçülüp değişiklik gerekmeyenler (MEASURED, 1.4 derlemesi)

- **Mix** (madde 18):
  - Kuru yol (Mix %0) girişle bit bit aynı (−240 dB) ve gecikme tam bildirilen değer (REALTIME 20 ms, HQ 40 ms).
  - Düzeltmesiz ıslak yol aynı gecikmede −43 dB (PSOLA yeniden sentezi), Mix %50'de −49 dB.
  - İki yol hizalı; tarak süzgeci etkisi yok.
- **Diziler** (madde 37–40, INSPECTED):
  - 13 dizinin aralıkları müzik kuramıyla aynı: majör, doğal / armonik / melodik minör, iki pentatonik, blues, dört mod.
  - Hicaz 12 eşit aralıkla yaklaşık: 1, ♭2, 3, 4, 5, ♭6, ♭7.

- **Auto Key** (etiketli sentetik derlem, 92 render):
  - %94,6 doğru, %1,1 yanlış kilit, %4,3 kilitlenmeme; 1.2 ile aynı.
  - Kilitlenme süresi medyanı 9,0 → 9,5 s.
  - 1.3–1.4 algılayıcı değişiklikleri Auto Key'i bozmamış.
- **Örnekleme hızı, 44,1 kHz:** 48 kHz ile aynı doğruluk (ince medyan 4,6 ct). Kenar yumuşatma süzgeci her hızda indirilmiş
  hıza göre kuruluyor (INSPECTED). 96 / 192 kHz bölüm 3'te düzeltildi.
- **Hard tune geçiş zamanlaması:** REALTIME'da 20 ms gecikme bütçesiyle sınırlı (1.3 raporu, bölüm 2.2). Kararı hızlandıran üç aday
  1.3'te yanlış nota değişimini artırdığı için reddedilmişti. Bu turda değişiklik yok.

## 5. Denenip alınmayan: %100'ün altında hızlı nota kararı (madde 24)

**Sorun (MEASURED, kesin doğru perdeye göre karar gecikmesi; doğal ayar, REALTIME):**

- Vibrato Scale %100'ün altındayken nota kararı 3 Hz'lik merkezle süzülmüş perdeyle veriliyor.
- **Sentetik set** (44 temiz değişim), %0 / %100:
  - karar gecikmesi medyanı 83,0 / 30,5 ms;
  - 400 ms içinde karara bağlanan değişim 28 / 34.
- **Gerçek kayıt** (145 kayıt, 320 değişim), %0 / %100:
  - karar gecikmesi medyanı 89,9 / 36,4 ms;
  - kullanıcının rap kaydında 82,9 / 30,9 ms.

**Denenen ilke:** Ses vibrato yapmıyorken karar, %100'deki gibi doğrudan sesin perdesiyle verilir. Vibrato yaparken eski süzülmüş
perdeyle verilir. Vibrato, işlenmemiş girişin vibratosuz merkez etrafındaki yön değişimleriyle algılanır.

| Varyant | Sentetik %0 karar | Gerçek %0 karar | Gerçek vibratolu kayıt, %0 nota değişimi / dk (445'ten) |
|---|---|---|---|
| fd1: ±25 ct, 3 yön değişimi | 30,5 ms | – | 584 (+%31) |
| fd2: ±25 ct, 2 yön değişimi | 30,5 ms | – | 532 (+%20) |
| fd3: fd2 + 1 s tutma | 30,5 ms | 87,1 ms (rap 82,9) | 456 (+%2,5) |
| fd4: fd3, eşik ±45 ct | 30,5 ms | 82,3 ms (rap 82,5); 11 nota daha karara bağlandı | 483 (+%8,5; en kötü dosya +%33) |

**Karar:** **alınmadı**, kod geri alındı (`PitchCorrector.fastdecide_fd4_rejected.h`).

- fd3 gerçek kayıtlarda neredeyse hiç hızlanmıyor. Gerçek ses vibrato olmadan da merkezin etrafında sık salınıyor; rap
  tonlaması da öyle.
- fd4'te hızlanma geniş vibratoda daha fazla nota atlamasıyla ödeniyor.
- Dinlemeden bu takas seçilemez.

**Sonraki aday:**

- Genliğe değil düzenliliğe bakan bir vibrato algılayıcısı: 3,5–8,5 Hz'de düzenli yön değişimi.
- Adaptive modunda böyle bir algılayıcı zaten var; rap tonlaması düzensiz olduğu için ayrılabilir.
- Geri almadan sonra derleme, kabul edilen durumla 12/12 render'da bit bit aynı; EngineTest 81/81, PluginTest 97/97.

## 6. Var olan 29 maddenin durumu (bu tur)

**İyileştirilen (5):**

| Madde | Ne değişti |
|---|---|
| 23, 24, 25 | Vibrato Scale ayara uyuyor (bölüm 2) |
| 86 | Windows derleme düzeltmesi (bölüm 1; CI'da henüz doğrulanmadı) |
| 87 | 96 / 192 kHz doğruluğu 48 kHz'e eşit (bölüm 3) |

**Ölçülüp ya da testle doğrulanıp sorunsuz çıkan (24):**

| Madde | Kanıt |
|---|---|
| 18 Mix | Kuru yol bit bit, yollar hizalı (bölüm 4) |
| 19 Retune Speed | Zaman sabiti ayara bir analiz adımı (2,7 ms) içinde uyuyor. Örneğin 15 ms'de %63'ü 13,3 ms'de, 200 ms'de 197 ms'de (MEASURED) |
| 21 Humanize | Tutarlı biçimde artıyor: uzun notada korunan vibrato %0'da 0,42 → %20'de 0,72 → %35'te 0,82 → %100'de 0,95 (MEASURED). **Not:** etkinin çoğu ilk %20'de; eğriyi değiştirmek mevcut projelerin sesini değiştirir, dokunulmadı |
| 29 Hard tune hızı | 20 ms gecikme bütçesiyle sınırlı (1.3 raporu); güvenli iyileştirme yok |
| 30 Robot modu | ROBOTISM taraması (1.3), EngineTest 7 (robotik vibratoyu düzleştirir) |
| 31 Scale lock | 13 dizi × 2 ton, 77 792 sesli adım: dizi dışı hedef nota 0 (MEASURED) |
| 37–40 Diziler | Aralıklar doğru (INSPECTED) |
| 43 Auto Key | %94,6 doğru, 1.2 ile aynı (MEASURED) |
| 44 Ton değişimi otomasyonu | Tutulan notada ton / dizi değişince en büyük örnek sıçraması 1,00 (sınır 1,5; PluginTest) |
| 50 DAW otomasyonu | PluginTest (gesture, otomasyon), pluginval; gerçek DAW'da denenmedi |
| 75 Buffer boyutu | 1–1024 örneklik bloklarda çıkış REALTIME'da ve HQ'da bit bit aynı (MEASURED) |
| 78 Bellek ayırma | 218 yapılandırmada 0 (MEASURED) |
| 79 Denormal | Eklentide FTZ açık: sessizlik ortalama 100 µs, en kötü 261 µs (bütçe 5333 µs). FTZ kapalı (yalnız JUCE dışı araçlar) 141 / 1488 µs |
| 81, 94 Durum kurtarma / çökme | PluginTest bulanık testleri. Ek olarak AddressSanitizer + UndefinedBehaviorSanitizer: EngineTest 81/81 ve WavProbe 234 render (13 giriş, uç durumlar dahil) × 9 ayar × 2 mod; 0 hata (MEASURED) |
| 82 VST3 | PluginTest VST3 host yüklemesi, pluginval seviye 10 SUCCESS (güncel derleme) |
| 89 Parametre yumuşatma | EngineTest 14 (tık yok) |
| 90 İş parçacığı güvenliği | INSPECTED (1.2). ThreadSanitizer çalıştırılmadı |
| 91 Deterministik | Blok boyu ve tekrar render'ları bit bit aynı |
| 93 Preset uyumluluğu | PluginTest 3h, 1.0 oturumları |
| 97 CPU benchmark | BenchProbe (bu turda iki kez koşuldu) |

## 7. Kısmen olan maddeler (ikinci aşama, sürüyor)

### 7.1 A4 akort referansı (madde 45, yeni)

- **Ne:** Ayarlar menüsü → **Akort (A4)**: 432 / 435 / 438 / 440 / 442 / 443 / 444 / 446 Hz.
- **Parametre:** yeni parametre `tuningref` (415–466 Hz, varsayılan 440).
  - Otomasyonsuz; vokal aralığı gibi oturuma ait.
  - Presetlere girmez: preset yüklemek akortu değiştirmez, akortu değiştirmek preseti "değişti" yapmaz.
  - 1.0–1.4 oturumları 440 Hz'de açılır.
  - Eski parametre kimlikleri ve sırası aynı; yeni parametre en sona eklendi (PluginTest).
- **Motor:**
  - Düzeltici, Auto Key ve perde göstergesi referansa göre kaydırılmış perdeyle çalışıyor; grain'ler gerçek perdeyle.
  - 440 Hz'de bu yol tamamen atlanıyor.
- **Ölçümler (MEASURED):**
  - 440 Hz'de 666 render 1.4 / önceki derlemeyle bit bit aynı.
  - EngineTest: 441 Hz'lik ses 440 referansında 440,002 Hz'e, 442 referansında 441,999 Hz'e; 433 Hz'lik ses 432 referansında
    432,003 Hz'e gidiyor.
  - pluginval seviye 10 SUCCESS.
- **Kapsam dışı:** mikrotonal özel akort (nota başına sent) yok. Madde 45 kısmen.

### 7.2 Fail-safe (madde 80)

- **Sorun (INSPECTED):** Tepe sınırlayıcı sayı dışı değerleri bilerek atlıyordu. Motor içinde NaN ya da sonsuz bir değer
  oluşsaydı doğrudan host'a gidecekti. Bulanık testlerde ve sanitizer taramasında böyle bir değer hiç oluşmadı.
- **Değişiklik:** sayı dışı her işlenmiş örneğin yerine aynı andaki kuru örnek gidiyor; olay `failSafeSamples` sayacında
  tutuluyor.
- **Ölçümler (MEASURED):**
  - EngineTest'e kontrol eklendi; enjeksiyon kancası yalnız test derlemesinde (`VSTM_TEST_HOOKS`), eklentide yok.
  - Enjekte edilen 64 NaN örneğin yerine tam olarak gecikmeli kuru giriş gitti; geri kalan çıkış bit bit aynı.
  - Normal çıkış önceki derlemeyle 18/18 render'da bit bit aynı.

### 7.3 Oktav hatası bastırma (madde 7): iyileştirme bulunamadı

- **Denenenler** (3 ölçüm seti × 2 mod):
  - og2a: 3 kat koruması yalnız kesintisiz seste;
  - og2b: 3 kat koruması hiç yok.
- **Sonuç (oktav + kaba hata, %):**

| Set | 1.4 | og2a | og2b |
|---|---|---|---|
| Sentetik REALTIME / HQ | 0,350 / 0,379 | aynı | 0,359 / 0,392 (4 dosya kötüleşti) |
| Gerçek kayıt REALTIME | 1,0331 | 1,0333 | 1,0329 (6 iyi / 3 kötü) |
| Gerçek kayıt HQ | 1,0420 | aynı | 1,0520 (3 iyi / 6 kötü) |

- **Karar:** 1.4 koruması kalıyor.
- **Bulgu:** 1.4'teki dosya başına karışık sonuç, sessizlik sonrası 3 kat kontrolünden gelmiyor.

### 7.4 Sibilant / nefes / ünsüz (madde 15–17): ölçüldü

**Yöntem (MEASURED):**

- 145 gerçek kayıt; 98'inde ≥ 50 ms sessiz/ünsüz bölüm var, toplam 146 s.
- Algılayıcının unvoiced dediği, sesli adımlardan 15 ms uzak, sessiz olmayan bölümler.
- Çıkış, gecikme kadar kaydırılmış girişle karşılaştırıldı.

| Ayar | 4 kHz üstü enerji değişimi, medyan (p10 / p90) | Dalga biçimi farkı, medyan |
|---|---|---|
| Varsayılan | −0,25 dB (−0,58 / −0,04) | −7,2 dB |
| Hard tune | −0,24 dB | −7,0 dB |
| Transpoze +12 | 0,00 dB | −30 dB |
| Formant −6 | **−2,85 dB** (−6,57 / −2,12) | +2,1 dB |

**Bulgular:**

- **Enerji:** Sibilant ve nefes enerjisi korunuyor; formant dışındaki ayarlarda en fazla ~0,6 dB.
- **Dalga biçimi farkı:** sesli bölümden devralınan küçük bir zaman kaymasından geliyor (faz sürekliliği için, tasarım
  gereği). Gürültü benzeri seste duyulmamalı; dinlenerek doğrulanmadı.
- **Formant:** formant kaydırma ünsüzlere de uygulanıyor; −6'da sibilantlar 3–6,6 dB boğuklaşıyor. Bu bir tasarım tercihi.
  Ses inceltme/kalınlaştırmada tutarlı bile olabilir.

### 7.5 Ünsüzleri formanttan koru (madde 15–17, yeni seçenek; kullanıcı kararı)

- **Ne:** Ayarlar menüsü → **Ünsüzleri formanttan koru**. Varsayılan kapalı, yani mevcut projelerin sesi aynen kalıyor.
- **Açıkken:** formant kaydırma yalnız sesli bölümlere uygulanıyor; s, ş ve nefes kaydırılmıyor.
- **Parametre:** `protectcons`, en sona eklendi, otomasyonsuz. Ses ayarı olduğu için presetlere giriyor (preset 24 → 25
  parametre); eski presetler kapalı açılıyor.
- **Ölçümler (MEASURED):**
  - Kapalıyken önceki derlemeyle 18/18 render bit bit aynı.
  - EngineTest: iki sesli arasında parlak bir gürültü patlaması ("s"), formant −6. Korumayla seviye değişimi 0,00 dB,
    korumasız −2,13 dB.
  - **145 gerçek kayıt** (98'inde ünsüz bölüm, 146 s), 4 kHz üstü enerji değişimi medyanı:

| Ayar | 4 kHz üstü enerji değişimi, medyan |
|---|---|
| Formant −6, koruma kapalı | −2,85 dB (en kötü %10: −6,6) |
| Formant −6, koruma açık | **−0,28 dB** |
| Formant +6, koruma açık | **−0,28 dB** |
| Referans: formant 0 | −0,25 dB |

- **Testler:** PluginTest 98/98 (159 metin × 7 dil, 16 eklenmiş parametre), EngineTest 84/84, pluginval seviye 10 SUCCESS.

### 7.6 Nota geçişi (madde 20, 36, yeni)

- **Ne:** Ayarlar menüsü → **Nota geçişi**: Auto / 0 / 20 / 50 / 100 / 200 ms.
  - **Auto (varsayılan):** bugünkü gibi ROBOTISM'e bağlı.
  - **Süre seçilince:** ROBOTISM'in iki tarafında da notalar arası S-eğrili kayma o sürede.
- **Parametre:** `transition`, otomasyonsuz; presetlere girer.
- **Ölçümler (MEASURED, EngineTest, robotik hard tune, nota adımı):**
  - Auto'da karardan yeni notaya varış 0 ms (eskisi gibi atlama).
  - 100 ms'de 85 ms'de 10 sent içine giriyor (S-eğrisi).
- **Varsayılan:** önceki derlemeyle 18/18 render bit bit aynı.

### 7.7 Formant transpozu izlesin (madde 34, yeni)

- **Ne:** Ayarlar menüsü → **Formant transpozu izlesin** (varsayılan kapalı).
  - **Açıkken:** formant, transpozla birlikte kayar: ince ya da kalın ses efekti.
  - **Kapalıyken:** transpoz formantı korur, bugünkü davranış.
- **Parametre:** `formantfollow`, otomasyonsuz; presetlere girer.
- **Ölçüm (MEASURED, EngineTest):** transpoze +12'de çıkışın spektral ağırlık merkezi kapalıyken 977 Hz, açıkken 1532 Hz.

### 7.8 Note bypass (madde 32, 51, yeni)

- **Ne:** Özel dizi editöründe **sağ tık** (Mac'te Ctrl-tık) bir notayı **atla** yapar. O notaya yakın söylenen ses
  düzeltilmeden geçer.
  - Sol tık eskisi gibi notayı açar ya da kapatır.
  - Atlanan notalar amber renkte; altta açıklama satırı var.
- **Parametreler:** 12 yeni otomasyonsuz parametre (`scalebypass0`–`11`), tona göre (özel dizi notaları gibi).
  - Yalnız özel dizi açıkken etkili.
  - Presetlere girer; "Presetler ton ve diziyi korur" açıkken korunur.
- **Ölçüm (MEASURED, EngineTest):**
  - Atlanan A'ya +30 sentle söylenen ses 57,300'de kalıyor.
  - Atlanmayan B +30 sent 59,000'e, bypass'sız A 57,000'e düzeltiliyor.
- **Ekran görüntüsü:** `plugintest_shots_15/08b_scale_editor@2x.png`.

### 7.9 AU ve Apple Silicon (madde 83, 85)

- CI'daki macOS işi bunları zaten doğruluyor:
  - kurulum paketini kuruyor, imzayı denetliyor;
  - kurulan AU'yu Apple'ın `auval` aracıyla doğruluyor ("AU VALIDATION SUCCEEDED");
  - plugin testlerini arm64 (Apple Silicon) koşucusunda koşuyor.
- 1.4.0 adayının CI koşusu 27'de bu işin bütün adımları geçti (MEASURED).
- 1.5 değişiklikleri için yeniden doğrulama bir sonraki push'ta.
- Logic gibi gerçek bir host'ta deneme yok.

### 7.10 Tolerans, güven eşiği, sentetik vibrato, oktav, 64 bit (madde 22, 28, 26, 35, 88; yeni)

Hepsi **Ayarlar → Gelişmiş** alt menüsünde. Varsayılanları kapalı (oktav 0); otomasyonsuz; presetlere girer.

| Madde | Ne | Ölçüm (MEASURED, EngineTest / PluginTest) |
|---|---|---|
| 22 Tolerans | Kapalı / 5 / 10 / 15 / 25 / 35 / 50 sent. Notanın vibratosuz merkezi hedefe bu kadar yakınsa söylendiği gibi kalır; ötesi tolerans sınırına çekilir. Vibrato dokunulmaz | Tolerans 25 ct: +15 ct'lik ses 57,150'de kalıyor, +40 ct'lik ses 57,250'ye çekiliyor |
| 28 Güven eşiği | Kapalı / 0,50 / 0,60 / 0,70 / 0,80. Algılayıcının kesinliği eşiğin altındayken düzeltme 20 ms'de söner, ses söylendiği gibi geçer. Nota başı, algılayıcı emin olana kadar düzeltmesiz başlar | Eşik 0,5: 57,000'e düzeltiyor; erişilemeyen eşik (1,0): 57,300'de bırakıyor |
| 26 Sentetik vibrato | Kapalı / Hafif (5 Hz, 15 ct) / Orta (5,5 Hz, 30 ct) / Geniş (6 Hz, 50 ct). Her notada yeniden başlar; 250 ms sonra 200 ms'de girer | 30 ct ayarı düz notada 58,8 ct tepeden tepeye |
| 35 Oktav kaydırma | −2 / −1 / 0 / +1 / +2 oktav; transpozla birlikte en çok ±24 yarım ses | +1: A3 → 68,9996 (A4) |
| 88 64 bit | 64 bit host'lar double blok gönderebilir. Motor 32 bitte çalışır; blok `prepareToPlay`'de ayrılan arabellekten parça parça geçer, ses iş parçacığında bellek ayırma yok | Double yol, 32 bit yolun çıktısını bit bit veriyor (8192 örnek) |

- **Varsayılan:** paketlenmiş 1.5.0 ile 18/18 render bit bit aynı.
- **Testler:** EngineTest 91/91, PluginTest 99/99, pluginval seviye 10 SUCCESS.
- **Menü:** nota geçişi, ünsüz koruması ve formant izleme de "Gelişmiş" altına taşındı.

### 7.11 MIDI hedef notalar (madde 42, 46, 52, 33; yeni)

- **Ne:** Ayarlar → Gelişmiş → **Hedef notalar MIDI'den** (varsayılan kapalı).
  - **Açıkken:** basılı tutulan MIDI notaları hedef notalar olur; oktav fark etmez, canlı çalınan bir dizi gibi.
  - Bir MIDI melodi kanalı da hedef melodiyi sürebilir.
  - Hiç nota basılı değilken normal dizi, özel dizi ve note bypass geçerli.
  - Kapalıyken MIDI yok sayılır.
- **Format:**
  - VST3 ve Standalone MIDI alıyor (`NEEDS_MIDI_INPUT`).
  - AU bilinçli olarak efekt (`aufx`) kaldı; 1.0–1.4 AU kimliği ve mevcut Logic projeleri bozulmasın diye. AU'da MIDI yok.
- **Zamanlama:** Ses, MIDI olaylarının örnek konumlarında bölünerek işleniyor; nota çalındığı yerde etkili oluyor. Motor blok
  boyundan bağımsız (çıkış her bölmede bit bit aynı, bölüm 6).
- **Parametre:** `miditarget`, otomasyonsuz. Oturum ayarı; presetlere girmez.
- **Ölçümler (MEASURED):**
  - EngineTest: B basılıyken A3+40 ct → 59,000 (B3), MIDI yokken → 57,000.
  - PluginTest: gerçek MIDI note-on ile C4 basılıyken A3+35 ct → 261,63 Hz (C4); mod kapalıyken aynı MIDI yok sayılıyor
    (220,00 Hz).
  - pluginval seviye 10 SUCCESS (MIDI girişli VST3).
  - Varsayılan (mod kapalı) paketlenmiş 1.5.0 ile 18/18 render bit bit aynı.
- **Henüz yok:**
  - MIDI CC eşleme (47);
  - MIDI Learn (48);
  - algılanan notaları MIDI olarak gönderme (49).

### Testler (son durum)

- EngineTest 92/92; 1.4'e göre +14 kontrol:
  - Vibrato Scale ×3;
  - fail-safe;
  - A4;
  - ünsüz koruması;
  - nota geçişi;
  - formant izleme;
  - note bypass;
  - tolerans, güven eşiği, sentetik vibrato, oktav;
  - MIDI hedef.
- PluginTest 100/100:
  - 173 metin × 7 dil;
  - 48 parametre: 1.0'ın 13'ü yerinde, 35 eklenmiş;
  - otomasyonlu parametre sayısı değişmedi (25);
  - 64 bit yol 32 bit yolla bit bit aynı;
  - gerçek MIDI olayıyla hedef nota.
- pluginval seviye 10 SUCCESS; AddressSanitizer + UndefinedBehaviorSanitizer temiz; derleme uyarısı 0.
- **Varsayılan ses 1.4 ile aynı (MEASURED):** son 1.5 derlemesi ile 1.4.0 adayı karşılaştırıldı. 145 gerçek + 77 sentetik
  kayıt × 3 ayar (varsayılan, hard REALTIME, hard HQ) = 666 render; hepsi bit bit aynı.
- **CPU, 1.5 / 1.4** (MEASURED; BenchProbe, 218 yapılandırma, yapılandırma başına 3 s, iki derleme art arda, paylaşılan sanal
  makine):
  - ortalama süre medyanı REALTIME 1,02, HQ 1,02 (48 kHz'de 1,02 / 0,98; 96 kHz'de 1,04 / 1,04; 192 kHz'de 1,01 / 1,02);
  - tek çekirdek payı 48 kHz'de ~%1,4, değişmedi;
  - bellek ayırma 0, gecikme 20 / 40 ms.
- **Sürüm numarası:** 1.5.0 (pluginval "VSTM Tune 1.5.0" olarak doğruladı).

## 8. Ölçüm hatası (düzeltildi, sonuçlara girmedi)

- **Hata:** İlk vibrato ölçümünde değerler yüzde olarak verildi (`vibrato=50`). WavProbe ise motor birimini alıyor
  (`vibrato=0.5`, `robotism=0.75`).
- **Belirti:** sonuçlar anlamsız çıktı (%50–200 arası hep aynı).
- **Düzeltme:** fark edildi, doğru birimle yeniden ölçüldü.
- **Etki:** Önceki 1.4 ölçümleri etkilenmedi: o çalışmalar yalnız 0 ve varsayılan değerler kullanıyordu.

## Dosyalar

Hepsi `artifacts/audio-runs/2026-11-25_v15/` altında:

| İçerik | Dosya |
|---|---|
| Vibrato | `vibrato/` (`vib_scale_eval.py`, `vib_real_eval.py`, CSV / JSON) |
| %100 bit eşitliği | `identity/` |
| Geçişler | `transitions/` |
| Auto Key | `autokey_v14/` |
| Örnekleme hızı | `gt_sr/` |
| Test kayıtları | `engine_test_vb2c.log`, `plugintest_vb2.log` |
| Derleme | `bin-vb2/` |
| Geri alma | `PitchCorrector.before_vibband.h`, `TuneEngine.before_vibband.h`, `wav_probe.before_vibband.cpp` |
| Dinleme paketi | `VSTM_Tune_Vibrato_Scale_Blind_Listening_Pack.zip` |
