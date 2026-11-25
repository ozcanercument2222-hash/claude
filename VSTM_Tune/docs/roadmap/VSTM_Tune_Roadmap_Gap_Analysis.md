# VSTM Tune – yol haritası boşluk analizi (1.4.0 adayına göre)

- **Kaynak:** kullanıcının "95–98/100 bandına çıkarmak için gerekenler" listesi (98 madde), rakip hedefleri, 2.0 / 3.0
  mimarisi, test matrisi ve başarı kriterleri.
- **Durum işaretleri:** ✅ var, ◐ kısmen, ✗ yok.
- **Kanıt etiketleri:** **INSPECTED** kaynak kodu (commit `370e919`), **MEASURED** önceki ölçüm raporları (`docs/1.2.0`–`1.4.0`).
- **Efor** (bir geliştirici için kaba tahmin): **S** günler, **M** 1–3 hafta, **L** 1–2 ay, **XL** çeyrek ve üstü.
- **Nerede yapılabilir:**
  - **burada:** bu bulut ortamında, Linux derleme ve ölçüm;
  - **CI:** GitHub Actions'ta Windows / macOS derlemesi;
  - **sen:** senin bilgisayarın, DAW'ın, dinlemen ya da hesabın gerekir.

## 1.5 adayından sonra (25.11.2026, commit edilmedi)

| | Var | Kısmen | Yok |
|---|---|---|---|
| 1.4 (aşağıdaki tablolar) | 29 | 37 | 32 |
| 1.5 adayı, ilk paket | 40 | 26 | 32 |
| **1.5 adayı, güncel** | **49** | **25** | **24** |

**İlk paketten sonra eklenen (9):**

- Yok → var:
  - tolerans (22);
  - sentetik vibrato (26);
  - güven eşiği (28);
  - ±24 yarım ses efektleri: oktav kaydırma ve formant izleme (35);
  - MIDI hedef nota (42);
  - MIDI nota girişi, VST3 (46);
  - MIDI hedef melodi (52);
  - MIDI ile bilinçli nota hedefleme (33).
- Kısmen → var: 64 bit işleme (88).

**Kalan yok (24):**

- MIDI CC (47), MIDI Learn (48), MIDI Perform (49);
- nota editörü (54–64), ARA2 (65), geri al / yinele (66), nota başına dinleme (67);
- armoni (68–72);
- AAX (84);
- gerçek DAW doğrulaması (96).

**Kısmen → var (11):** her biri ölçüldü (MEASURED); ayrıntı `docs/1.5.0/VSTM_Tune_1.5.0_Changelog.md`.

| Madde | Ne yapıldı |
|---|---|
| 15, 16, 17 | Ünsüz / nefes koruması ölçüldü ve "Ünsüzleri formanttan koru" seçeneği eklendi |
| 20, 36 | Nota geçişi (Auto / 0–200 ms) eklendi |
| 32, 51 | Note bypass (özel dizide sağ tık) eklendi |
| 34 | Formant transpozu izlesin seçeneği eklendi |
| 80 | Fail-safe: sayı dışı örnek yerine kuru ses |
| 83, 85 | AU (`auval`) ve Apple Silicon (arm64) CI'da doğrulanıyor; 1.4'ün koruşu 27'de geçti |

**Var olanlarda iyileştirme:**

- 23, 24, 25: Vibrato Scale ayara uyuyor.
- 86: Windows derleme düzeltmesi; CI'da henüz doğrulanmadı.
- 87: 96 / 192 kHz doğruluğu.

**Kısmen olanlarda ilerleme:**

- 45: A4 referansı; mikrotonal akort yok.
- 95: +9 motor kontrolü.
- 7: iki varyant denendi, iyileştirme yok.

**Kalan 26 kısmen:**

- algılayıcı ve ses modeli araştırmaları: 1–6, 8–14;
- attack / release: 27;
- dizi transpozu: 41;
- offline nota algılama: 53;
- LIVE ve düşük gecikme: 73, 74;
- CPU koruması ve çoklu örnek: 76, 77;
- 64 bit: 88;
- DAW ve kör dinleme (kullanıcı): 92, 98.

**Olmayan 32:** MIDI, nota editörü / ARA2, armoni, polifonik, AAX ve diğerleri. Her biri ayrı ve büyük bir proje.

## Özet (1.4)

| | Var | Kısmen | Yok |
|---|---|---|---|
| 98 madde | 29 | 37 | 32 |

**Güçlü taraflar (MEASURED):**

- gerçek zamanlı düzeltme çekirdeği: algılayıcı, nota kararı, PSOLA;
- güvenilirlik: bellek ayırma yok, kilit yok, bozuk durum ve preset dayanıklılığı, pluginval seviye 10;
- CPU ve gecikme ölçümü; Auto Key; diziler; preset uyumluluğu.

**En büyük boşluklar** (hepsi ✗):

- MIDI;
- nota editörü / grafik mod ve ARA2;
- armoni;
- polifonik analiz;
- AAX;
- gerçek DAW ve kör dinleme doğrulaması.

**Puan üzerine:** "100/100" ölçülebilir bir hedef değil. Listenin son cümlesindeki asıl hedef ölçülebilir hâle getirilmeli:
bağımsız kör testte rakiplerden iyi sonuç, düşük gecikme, düşük CPU, güvenilirlik. Bunun için:

1. Algılama ve geçiş hataları, artefakt sayıları, CPU ve gecikme için sayısal eşikler: araçlar `tests/` altında var.
2. Rakiplerle aynı kuru vokallerde kör tercih testi: rakip çıktıları senin DAW'ında üretilmeli.

## A. Pitch motoru

| # | Madde | Durum | Bugün ne var | Efor | Nerede |
|---|---|---|---|---|---|
| 1 | Çok sağlam monofonik algılayıcı | ◐ | YIN, ondalıklı; oktav korumaları. Gerçek kayıtta oktav + kaba hata ~%1,0, fry zayıf (MEASURED) | L | burada |
| 2 | Ses aralığı / register farkında izleme | ◐ | 3 elle seçilen aralık (Soprano/Alto, Tenor, Bas). Otomatik register izleme yok | M | burada |
| 3 | Unvoiced / sibilant algılama | ◐ | Voiced/unvoiced kararı; unvoiced bölümler kaydırılmaz (sabit 4 ms grain). Ayrı sibilant sınıflandırıcı yok | M | burada |
| 4 | Nota güven sistemi | ◐ | Kesinlik (clarity) hesaplanıyor, geliştirici panelinde görünüyor; kararlarda kullanılmıyor (1.4'te ölçüldü: zayıf ayırıyor) | M | burada |
| 5 | Uyarlanır yumuşatma | ◐ | Adaptive anahtarı, Humanize, nota yaşına bağlı hız | S–M | burada |
| 6 | Hızlı geçişte overshoot kontrolü | ◐ | Geçiş zamanlaması ölçülüyor; ayrı bir overshoot sınırlayıcı yok | M | burada |
| 7 | Oktav hatası bastırma | ◐ | 2 kat / 3 kat koruması; gerçek kayıtlarda dosyaya göre karışık (1.4) | S–M | burada |
| 8 | Vibrato ile gerçek perde kaymasını ayıran model | ◐ | Vibratosuz merkez izleyici (1.3 aday 3), Vibrato Scale. Ayrık bir kayma modeli yok | M | burada |
| 9 | Alçak nota ve nefesli vokal | ◐ | Bas aralığı; nefesli ses için özel davranış yok; fry bilinen sınır | M–L | burada |
| 10 | CLASSIC / MODERN / HQ / LIVE algoritmaları | ◐ | REALTIME (20 ms) ve HQ (40 ms). CLASSIC ve düşük gecikmeli LIVE yok | M–L | burada |

## B. Doğal ses

| # | Madde | Durum | Bugün ne var | Efor | Nerede |
|---|---|---|---|---|---|
| 11 | Ses farkında formant düzeltme | ◐ | TD-PSOLA perdeyi kaydırırken formantı doğal olarak korur; spektral zarf modeli yok | L | burada |
| 12 | Formant koru / kaydır / throat | ◐ | Koru (doğal) ve ±12 yarım ses kaydır. Throat (gırtlak uzunluğu) yok | M | burada |
| 13 | Büyük transpozda chipmunk / metalik artefakt azaltma | ◐ | ±12 yarım ses, seviye dengeleme. Büyük kaydırmaya özel işlem yok | L | burada |
| 14 | Harmonik koruma | ◐ | PSOLA dalga biçimini korur; ayrıca ölçülmedi | M | burada |
| 15 | Sibilant koruma | ◐ | Unvoiced bölüm kaydırılmaz; nesnel ölçüm yok | S (ölçüm) | burada |
| 16 | Nefes koruma | ◐ | Aynı mekanizma; nefes asla yükseltilmez (INSPECTED) | S (ölçüm) | burada |
| 17 | Ünsüz koruma | ◐ | Aynı mekanizma | S (ölçüm) | burada |
| 18 | Mix / dry-wet | ✅ | MIX parametresi | – | – |

## C. Retune ve ifade

| # | Madde | Durum | Bugün ne var | Efor | Nerede |
|---|---|---|---|---|---|
| 19 | Retune Speed | ✅ | 0–200 ms | – | – |
| 20 | Note Transition | ◐ | Retune ve geçiş mantığının içinde; ayrı parametre yok | S–M | burada |
| 21 | Humanize | ✅ | HUMANIZE | – | – |
| 22 | Flex-Tune benzeri tolerans | ✗ | – | S–M | burada |
| 23 | Vibrato koru | ✅ | Vibrato Scale %100; insansı tarafta vibrato koruması (1.3 aday 3) | – | – |
| 24 | Vibrato azalt | ✅ | Vibrato Scale %0–100 | – | – |
| 25 | Vibrato güçlendir | ✅ | Vibrato Scale %100–200 | – | – |
| 26 | Kontrollü sentetik vibrato | ✗ | – | M | burada |
| 27 | Attack / release | ◐ | Retune ve Humanize; ayrı attack / release yok | S–M | burada |
| 28 | Nota güven eşiği | ✗ | – (madde 4'e bağlı) | S | burada |

## D. Hard tune / yaratıcı

| # | Madde | Durum | Bugün ne var | Efor | Nerede |
|---|---|---|---|---|---|
| 29 | 0–5 ms ultra hızlı düzeltme | ✅ | Retune 0 ms; duyulan geçiş medyanı HQ 6,3 ms, REALTIME 11,7 ms (MEASURED) | – | – |
| 30 | Robot modu | ✅ | ROBOTISM | – | – |
| 31 | Scale lock | ✅ | KEY / SCALE, Auto Key kilidi | – | – |
| 32 | Note bypass | ◐ | Özel dizide nota kapatma = en yakına çek ("remove"). Düzeltmeden geçir ("bypass") yok | S | burada |
| 33 | Bilinçli nota hedefleme | ✗ | – (MIDI ya da editör gerekir) | M | burada |
| 34 | Formant + transpoze yaratıcı zinciri | ◐ | İkisi de var; birlikte tasarlanmış bir zincir yok | S | burada |
| 35 | Aşırı perde efektleri | ✗ | Transpoze ±12 ile sınırlı | M | burada |
| 36 | Glide / portamento kontrolü | ◐ | Retune üzerinden; ayrı glide süresi yok | S–M | burada |

## E. Ton / dizi

| # | Madde | Durum | Bugün ne var | Efor | Nerede |
|---|---|---|---|---|---|
| 37 | Kromatik | ✅ | | – | – |
| 38 | Majör / minör | ✅ | 13 dizi (Hicaz dahil) | – | – |
| 39 | Özel dizi | ✅ | Dizi editörü | – | – |
| 40 | Nota başına aç / kapat | ✅ | 12 otomasyonlu nota | – | – |
| 41 | Dizi transpozu | ◐ | KEY dizinin kökünü taşır; ayrı bir "dizi transpozu" yok | S | burada |
| 42 | MIDI ile hedef nota | ✗ | Eklenti MIDI almıyor (`NEEDS_MIDI_INPUT FALSE`) | M | burada + sen (DAW) |
| 43 | Otomatik ton bulma | ✅ | Auto Key: tahmin, kilit, öneri (1.1; 145 kayıtta ölçüldü) | – | – |
| 44 | Ton değişimi otomasyonu | ✅ | KEY otomasyonlu; Auto Key ton değişimlerini isteğe bağlı izler | – | – |
| 45 | Batı dışı / özel akort | ◐ | Hicaz (12 eşit aralıkla yaklaşık). Mikrotonal akort ve A4 referansı yok | M | burada |

## F. MIDI

| # | Madde | Durum | Bugün ne var | Efor | Nerede |
|---|---|---|---|---|---|
| 46 | MIDI nota girişi | ✗ | – | M | burada + sen |
| 47 | MIDI CC | ✗ | DAW'ın kendi CC → parametre eşlemesi kullanılabilir | S–M | burada + sen |
| 48 | MIDI Learn | ✗ | – | M | burada + sen |
| 49 | MIDI Perform | ✗ | – | M | burada + sen |
| 50 | DAW otomasyonu | ✅ | 25 otomasyonlu parametre (PluginTest; gerçek DAW'da denenmedi) | – | sen (doğrulama) |
| 51 | Nota başına bypass | ◐ | Madde 32 ile aynı | S | burada |
| 52 | MIDI hedef melodi | ✗ | – | M | burada + sen |

## G. Offline / grafik düzenleme

| # | Madde | Durum | Efor |
|---|---|---|---|
| 53 | Sesten nota algılama | ◐: gerçek zamanlı algılama var; kaydedilen, düzenlenebilir analiz yok | L |
| 54–64 | Nota blokları, perde eğrisi, kayma, modülasyon, zamanlama, böl / birleştir / uzunluk, nota başına formant / ses / sibilant | ✗ | XL (toplam) |
| 65 | ARA2 | ✗: ARA SDK (Celemony, açık kaynak) ve JUCE ARA desteği gerekir; ARA'lı host'larda test sende | L (editörün üstüne) |
| 66 | Geri al / yinele | ✗ | M |
| 67 | Nota başına dinleme / bypass | ✗ | M |

Bu bölüm, rakiplerin (Auto-Tune Graph Mode, Waves Tune, Melodyne) en güçlü olduğu yer. VSTM'de **hiç yok**. Zamanlama düzeltme
(58) ayrıca bir zaman germe motoru gerektirir.

## H. Armoni

| # | Madde | Durum | Efor |
|---|---|---|---|
| 68–72 | 2 / 3 / 4 sesli armoni, diziye uyan armoni, MIDI'li armoni, bağımsız formant, ses yürütümü | ✗ | L–XL |

Mevcut PSOLA motoru birden çok kaydırılmış ses üretmeye uygun bir temel. CPU her ses için yaklaşık katlanır (MEASURED değil,
tahmin).

## I. Canlı / performans

| # | Madde | Durum | Bugün ne var | Efor | Nerede |
|---|---|---|---|---|---|
| 73 | Çok düşük gecikme | ◐ | REALTIME 20 ms. Rakiplerin canlı modları daha düşük gecikme iddia ediyor | L | burada |
| 74 | Live mod | ◐ | REALTIME bu işi görüyor; ayrı düşük gecikmeli mod yok | (73 ile) | burada |
| 75 | Buffer boyutundan bağımsız güvenli davranış | ✅ | 32–1024 örnek bloklar, 44,1–192 kHz (MEASURED) | – | – |
| 76 | CPU tepe koruması | ◐ | Blok başına sınırlı iş, en kötü blok ölçülüyor; ayrı koruma yok | S–M | burada |
| 77 | Çoklu örnek optimizasyonu | ◐ | 1 / 4 / 8 / 16 örnek ölçüldü (1.2); paylaşılan kaynak optimizasyonu yok | M | burada |
| 78 | Ses iş parçacığında bellek ayırma yok | ✅ | 0 ayırma (MEASURED, her sürümde) | – | – |
| 79 | Denormal koruması | ✅ | `ScopedNoDenormals`, denormal testi | – | – |
| 80 | Fail-safe bypass | ◐ | Host bypass, sayı dışı değer korumaları; iç hatada kendiliğinden bypass yok | S | burada |
| 81 | Durum kurtarma | ✅ | Bozuk oturum / preset güvenli varsayılana döner (PluginTest) | – | – |

## J. Profesyonel mühendislik

| # | Madde | Durum | Bugün ne var | Efor | Nerede |
|---|---|---|---|---|---|
| 82 | VST3 | ✅ | | – | – |
| 83 | AU | ◐ | macOS'ta derleniyor (CI); `auval` ve DAW doğrulaması yok | S | CI + sen |
| 84 | AAX | ✗ | Avid geliştirici hesabı, AAX SDK ve PACE imzası gerekir | M | sen (hesap) + CI |
| 85 | Apple Silicon yerel | ◐ | arm64 + x86_64 evrensel derleme ayarlı (CI macos-15); donanımda denenmedi | S | sen |
| 86 | Windows x64 | ✅ | CI'dan VST3 (DAW'da denenmedi) | – | sen (doğrulama) |
| 87 | Örnekleme hızı kapsamı | ✅ | 44,1–192 kHz CPU / güvenlik; ses kalitesi çoğunlukla 44,1 / 48 kHz'de ölçüldü | – | – |
| 88 | 32 / 64 bit iç işleme | ◐ | 32 bit ses, 64 bit konumlar; 64 bit ses yolu yok | S | burada |
| 89 | Parametre yumuşatma | ✅ | Otomasyon tık testleri (EngineTest) | – | – |
| 90 | İş parçacığı güvenli UI / ses ayrımı | ✅ | Atomikler, kilit yok (INSPECTED + testler) | – | – |
| 91 | Deterministik DSP | ✅ | Aynı girdi bit bit aynı çıktı; WavProbe = VST3 (MEASURED) | – | – |
| 92 | Host otomasyonu güvenilirliği | ◐ | PluginTest, pluginval; gerçek DAW yok | – | sen |
| 93 | Preset uyumluluğu | ✅ | `.vstmpreset` → `.drp`, oturum 2 | – | – |
| 94 | Çökmeye dayanıklı durum | ✅ | Bulanık (fuzz) testler (1.4) | – | – |
| 95 | Geniş otomatik DSP test takımı | ◐ | EngineTest 78 + PluginTest 97 + ölçüm araçları; ölçümlerin çoğu CI kapısı değil | M | burada + CI |
| 96 | Gerçek DAW doğrulaması | ✗ | REAPER test kiti var (`daw_test/`), çalıştırılmadı | – | sen |
| 97 | CPU benchmark takımı | ✅ | BenchProbe: matris / örnek sayısı / denormal / uzun koşu | – | – |
| 98 | Kör dinleme benchmark'ı | ◐ | Paketler hazır, hiç dinlenmedi; rakip karşılaştırması yok | – | sen |

## Başarı kriterleri (bölüm 6)

| Kriter | Durum |
|---|---|
| Auto-Tune tarzı gerçek zamanlı düzeltme | ✅ var; Auto-Tune ile kör karşılaştırma yok |
| Auto-Tune Advanced düzeyinde vibrato / formant / canlı kontrol | ◐ |
| Waves Tune Real-Time düzeyinde düşük gecikme | ✗ (20 ms) |
| Waves Tune düzeyinde nota düzenleme | ✗ |
| Melodyne'e yaklaşan düzenleme | ✗ |
| ARA2 | ✗ |
| MIDI perform / kontrol | ✗ |
| 4 sesli armoni | ✗ |
| Polifonik analiz motoru | ✗ |
| Gerçek DAW testleri | ✗ |
| Kör dinleme testleri | ✗ (paketler hazır) |
| CPU benchmark | ✅ |
| Çökme / stres testi | ✅ (pluginval 10, bulanık testler, 60 dakikalık koşu) |
| 100+ otomatik DSP regresyon testi | ◐ (EngineTest 78 + PluginTest 97; ölçüm kapılarının çoğu elle) |
| Çoklu örnek testi | ✅ CPU olarak; DAW'da yok |

## Test matrisi (bölüm 5)

**Vokal türleri:**

| Tür | Durum | Malzeme |
|---|---|---|
| Temiz pop | ◐ | kullanıcının stem'i |
| Rap | ✅ | kullanıcının rap kaydı |
| R&B | ✗ | – |
| Rock | ◐ | VocalSet belt |
| Çok vibratolu | ✅ | VocalSet vibrato, sentetik bas vibrato |
| Nefesli | ✅ | VocalSet breathy |
| Falsetto | ✗ | – |
| Bariton / alçak erkek | ✅ | VocalSet erkek, sentetik bas |
| Yüksek kadın | ✅ | VocalSet kadın, sentetik soprano |
| Fry | ✅ | ölçüldü; bilinen sınır |
| Hızlı melisma | ✅ | VocalSet hızlı gam ve arpej, sentetik koşular |
| Büyük sıçrama | ✅ | sentetik register geçişi, arpejler |
| Slide | ◐ | sentetik glide testi |
| Konuşma | ✅ | VocalSet spoken, RAVDESS |
| Koro / backing | ✗ | monofonik motor bunu hedeflemiyor |

**Koşullar:**

- ✅ doğal düzeltme, hard tune, vibrato, düşük / yüksek buffer, mono / stereo, tek / çoklu örnek (CPU);
- ◐ büyük transpoze ve formant (seviye ve tepe ölçüldü, kalite ölçülmedi);
- ◐ 44,1 / 48 / 96 kHz (CPU hepsinde; kalite ölçümü çoğunlukla 44,1 / 48 kHz).

## Mimari (bölüm 4) ile bugünkü kod

| Çekirdek | Bugün |
|---|---|
| 1 Pitch Detection | `dsp/PitchDetector.h` |
| 2 Note Tracker | `dsp/PitchCorrector.h` (karar, histerezis) |
| 3 Correction Engine | `dsp/PitchCorrector.h` + `dsp/TuneEngine.h` (PSOLA) |
| 4 Formant / Vocal Character | `dsp/TuneEngine.h` içinde (grain yeniden örnekleme) |
| 5 Vibrato Engine | `dsp/PitchCorrector.h` içinde (Vibrato Scale, vibrato koruması) |
| 6–10 Creative FX, Graph Editor, Harmony, MIDI, ARA | yok |
| 11 Preset / State | `PresetManager`, `Drp`, oturum 2 |
| 12 Validation / Test | `tests/` (EngineTest, PluginTest, WavProbe, BenchProbe, ölçüm betikleri) |

Ses iş parçacığı kuralları (bellek ayırma yok, kilit yok, UI / disk yok, deterministik, fail-safe) bugün sağlanıyor (MEASURED +
INSPECTED); fail-safe bypass kısmen. Çekirdek 1–5'i ayrı modüllere bölmek **geniş bir yeniden düzenleme**dir. Ancak yeni
çekirdekler (6–10) eklendikçe ve her adımda ses bit bit aynı kalacak şekilde yapılmalı.

## Önerilen sıra

Her fazda kural aynı: temel ölçüm, tek değişiklik, aynı malzemeyle yeniden render, seviyesi eşit karşılaştırma, regresyon
kapıları. Varsayılan ayarlarda ses 1.4 ile bit bit aynı kalmalı; yeni özellikler yeni, eklenmiş parametrelerle gelir. Eski
kimlikler, oturumlar ve presetler bozulmaz.

| Faz | İçerik (madde no) | Efor | Burada yapılabilir mi |
|---|---|---|---|
| **1 – 1.5 "MIDI ve ifade"** | MIDI hedef nota ve melodi (42, 46, 52, 33); note bypass (32, 51); Flex-Tune toleransı (22); güven eşiği (28); sentetik vibrato (26); glide ve geçiş (20, 36) | L | Evet. MIDI'nin DAW'da doğrulaması sende |
| **2 – 1.6 "Ses karakteri ve LIVE"** | Throat ve formant modeli (11–13); aşırı efektler ±24 (35); düşük gecikmeli LIVE modu (10, 73, 74; doğrulukla takas ölçülmeli); sibilant / nefes ölçümleri (15–17) | L | Evet |
| **3 – 2.0 "Nota editörü"** | Eklenti içinde kayıt ve analiz, nota blokları, perde eğrisi, böl / birleştir, nota başına formant / ses, geri al / yinele (53–64, 66, 67). Zamanlama düzeltme (58) için zaman germe | XL | Kodlama burada; kullanılabilirlik sende |
| **4 – 2.x "ARA2, AAX, platform"** | ARA2 (65); AAX (84); AU / Apple Silicon doğrulaması (83, 85) | L | ARA2 kodu burada; AAX hesabı / imzası ve host testleri sende |
| **5 – 3.0 "Armoni ve polifonik"** | 2–4 sesli armoni (68–72); polifonik analiz | XL | Araştırma burada; dinleme sende |
| **Sürekli** | Rakiplerle kör test: aynı kuru vokaller, senin DAW'ında Auto-Tune / Waves Tune / Melodyne çıktıları, benim seviye eşitleme ve körleme | – | Senin render'ların gerekir |

**Neden bu sıra:**

- **Faz 1:** mevcut motoru bozmadan rakiplerle en büyük farkı (MIDI, tolerans, sentetik vibrato) kapatır. Hepsi burada ölçülebilir.
- **Faz 3:** en büyük ürün farkı, ama en pahalısı da o. Yeni bir arayüz ve veri modeli gerektirir. Faz 1'deki nota hedefleme
  altyapısı (dışarıdan verilen hedef nota) editörün de temeli olur.
