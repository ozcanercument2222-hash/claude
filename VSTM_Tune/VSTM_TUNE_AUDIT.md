# VSTM Tune – Denetim Raporu (VSTM_TUNE_AUDIT.md)

| | |
|---|---|
| Tarih | 17.11.2026 |
| Denetlenen kod | `VSTM-Tune-Kaynak-Kod.zip` (SHA-256 `712d03fb…2b4`). Depodaki `VSTM_Tune/` + `.github/workflows/vstm-tune.yml` ile **birebir aynı** (`diff -rq`), commit `21ff3f6` |
| Esas alınan belge | `VSTM_Tune_Gelistirme_Yol_Haritasi.txt` v1.0 |
| Kaynak kodda değişiklik | Denetimde **yok** (bu dosya `8124b64`'te eklendi). Sonrasında aynı gün P0-1 (kısmi), P0-2 ve P0-3 uygulandı: **Bölüm 15** |
| Satır numaraları | Denetlenen sürüme (`21ff3f6`) göredir; sonraki commit'lerde kayabilir |

## Kanıt etiketleri

| Etiket | Anlamı |
|---|---|
| **MEASURED** | Bu denetimde çalıştırılan sayısal ölçüm veya incelenen grafik / ekran görüntüsü |
| **INSPECTED** | Yalnızca kaynak kod ya da yapılandırma incelemesi (çalıştırılmadı) |
| **CI** | GitHub Actions çalışma kaydından okunan sonuç (bu makinede çalıştırılmadı) |
| **USER-REPORTED** | İnsan dinleme geri bildirimi. Bu denetimde **yok** |
| **LISTENED-MODEL** | Yerel ses modeli (`tools/ear.py`). Bu denetimde **kullanılmadı**: kalibrasyona göre (`docs/vstm-audio-rd/EAR.md`) yalnızca kaba distorsiyonu güvenilir buluyor; autotune artefaktları için kanıt vermiyor |
| **NOT TESTED** | Hiç sınanmadı |

> **Bu denetimde hiçbir ses dinlenmedi.** Ses kalitesine dair her ifade ölçüme ya da kod incelemesine dayanır. "Sentetik" yazan ölçümler yapay vokal sinyaliyle yapıldı ve gerçek vokal davranışını kanıtlamaz.

---

## 0. Yönetici özeti

**Durum:** Kod derleniyor ve mevcut otomatik testlerin hepsi geçiyor. Ses motoru blok boyutundan bağımsız, şeffaf, sentetik sinyalde çok doğru ve CPU'su düşük. Ticari kalite için eksikler şunlar: **sayısal sağlamlık** (NaN/Inf), **gerçek vokal test seti**, **insan dinlemesi** ve **DAW / macOS doğrulaması**.

**Ölçülen güçlü yanlar (MEASURED):**
- **Derleme:** Linux, GCC 13.3, JUCE 8.0.6 Release; 3 dk 23 sn, proje kaynaklarında **0 uyarı**.
- **Mevcut testler:** EngineTest **37/37**, PluginTest **32/32** (derlenmiş VST3'ün JUCE host'unda taranması, yüklenmesi ve ses işlemesi dahil).
- **VST3 ile motor aynı:** VST3 eklentisi (JUCE host üzerinden) ve motor seviyesindeki `WavProbe`, aynı gerçek vokalde en çok **1,8·10⁻⁷** farkla aynı çıktıyı veriyor.
- **Blok boyutu:** 1, 2, 7, 32 … 8192 ve rastgele blok boyutlarında çıktı **bit düzeyinde aynı**.
- **Örnekleme hızları:** 22,05 – 192 kHz'in hepsinde hard-tune hatası ~0,00 ct. Düzeltme kapalıyken şeffaflık SNR 84–94 dB (sabit ton); EngineTest'te vibratolu girişle 61 dB.
- **CPU:** Ortalama tek çekirdeğin %1,5–3,2'si. 10 dakikalık sürekli çalışma kararlı.
- **Gürültü:** Beyaz gürültü, nefes ve sessizlikte yanlış "voiced" kararı yok.

**En önemli riskler (öncelik sırasıyla):**
1. **Tek bir NaN veya Inf giriş örneği perde düzeltmesini kalıcı olarak kapatıyor.** Motor sıfırlanana kadar düzeltme dönmüyor ve 5 NaN örnek çıkışa geçiyor (MEASURED).
2. **Bozuk oturum ya da preset'teki `nan` değeri eklentiyi kalıcı olarak bozuyor.**
   - Bozuk dosya NaN parametreye, o da NaN çıkışa yol açıyor (MEASURED, gerçek eklenti kodu).
   - Motor seviyesinde yarım saniyelik bir NaN parametre, değer düzelse bile çıkışı kalıcı olarak susturuyor. `mix` için çıkış kalıcı olarak NaN oluyor (MEASURED).
3. **Gerçek vokal test seti yetersiz.**
   - Mevcut "gerçek vokal" kanıtı 6 kısa RAVDESS klibine dayanıyor (4,2–5,2 s).
   - Bu klipler 16 kHz kaynaklı: 8,5 kHz üstündeki enerji −110…−130 dB.
   - Lisansları ticari olmayan (CC BY-NC-SA); dosyalar depoda değil, metadata yok.
4. **Hiç insan dinlemesi kaydı yok, hiçbir DAW'da test edilmedi.** macOS/AU derlemesi CI'da **hiç çalışmadı**: 5 çalıştırmanın hepsinde (6 deneme) macOS işi hiçbir adım çalıştırmadan iptal oldu (CI).
5. **REALTIME aç/kapa geçişi sesi kesiyor.** 40 ms tam sessizlik oluşuyor, örnek adımı sabit duruma göre ~5 kat artıyor (tık) ve otomatik bulunan ton unutuluyor (MEASURED).
6. **Yüksek seste legato oktav sıçramasında algılayıcı yanlış oktavda kalıyor** (A4→A5, sentetik).
   - Motorun algıladığı perde nota boyunca bir oktav aşağıda kalıyor.
   - Çıkış perdesi doğru, ancak alt oktav (f0/2) enerjisi kuru sese göre **+31 dB** artıyor (−45 dB) ve perde ekranı yanlış oktavı gösteriyor (MEASURED).
7. **Auto Key sentetik testlerde zayıf** (MEASURED).
   - Yalnızca tek bir nota duyulduğunda bile 2 saniye sonra kesin ton (C) bildiriyor.
   - C majörden G majöre modülasyonu 8 saniyede fark etmiyor.
8. **Büyük transpoze ve formant ayarlarında seviye kayıyor** (MEASURED, sentetik).
   - RMS seviye −7,4 ile +2,6 dB arasında değişiyor; tepe değer +5,9 dB artıyor.
   - Çıkışta koruma (limiter) yok.

**Öneri:** Bölüm 11'deki P0 sırasıyla ilerlenmeli. Önce test altyapısı ve iki küçük sağlamlık düzeltmesi (NaN/Inf ve REALTIME geçişi) gelmeli; bunlar küçük, ölçülebilir ve geri alınabilir. Gerçek vokal seti kullanıcıdan gelmeli ve bu iş hemen, paralel başlamalı.

> **Güncelleme (17.11.2026, aynı gün):** Risk 1, 2 ve 5 düzeltildi ve testle kanıtlandı (`00bde73`, `154eddc`). Ölçüm aracı depoya alındı ve WavProbe sağlamlaştırıldı (`76f8594`). Normal girişte çıktı bit düzeyinde değişmedi. Testler: EngineTest 41/41, PluginTest 43/43. Ayrıntılar ve tamamlanma raporları **Bölüm 15**'te. Risk 3, 4, 6, 7 ve 8 açık.

---

## 1. Kapsam, ortam ve yöntem

**Yapılanlar:**
1. Tüm kaynak kod ve testler okundu: `Source/**`, `tests/**`, `CMakeLists.txt`, `README.md` ve CI dosyası.
2. Proje ayrı bir klasörde (`VSTM_Tune/build-audit/`, `.gitignore` kapsamında) sıfırdan derlendi. Var olan `build/` klasörüne dokunulmadı.
3. Mevcut testler çalıştırıldı: EngineTest, PluginTest (xvfb, `--vst3`), WavProbe + `vocal_metrics.py` (6 vokal × 2 ayar) ve `VSTMOfflineHost --list` ile VST3 render.
4. Koddan çıkan riskleri ölçmek için iki ölçüm programı yazıldı. **Repo koduna dokunmuyorlar**, başlıkları ve kaynakları olduğu gibi kullanıyorlar:
   - `harness/audit_engine.cpp`: JUCE gerektirmez; S1–S15 senaryoları.
   - `harness/state_test/`: gerçek `VSTMTuneProcessor` ve `PresetManager` ile oturum / preset sağlamlığı.
5. GitHub Actions çalışma geçmişi okundu (5 çalıştırma).

**Ortam (MEASURED):**

| | |
|---|---|
| OS / CPU | Linux 6.18 x86_64, Intel Xeon @ 2,1 GHz, 4 çekirdek (paylaşımlı sanal makine: zaman ölçümleri gürültülü) |
| Derleyici | GCC 13.3 (Release, `juce_recommended_warning_flags`), CMake 3.28.3, Ninja 1.11.1 |
| JUCE | 8.0.6, yerel kopya `/home/user/juce-framework/juce` (CMake'teki `VSTM_JUCE_TAG` ile aynı sürüm) |
| Python | numpy 2.5.3, scipy 1.18.1, soundfile 0.14, librosa 1.0.1 |
| Kanıt klasörü | `artifacts/audio-runs/2026-11-17_vstm-tune-audit/` (depo kökünde; **git'e girmez**, `.gitignore`) |

pluginval **çalıştırılmadı**. Sistemde kurulu değil; eski bir oturumdan indirilmiş bir zip var, ancak indirilmiş ikili dosyayı izinsiz çalıştırmamak için kullanılmadı.

---

## 2. Mevcut mimari

### 2.1 Dosyalar ve sorumluluklar (INSPECTED)

| Dosya | Sorumluluk |
|---|---|
| `Source/dsp/DspCommon.h` | Biquad alçak geçiren, Hermite enterpolasyon, kilitsiz SPSC kuyruk, MIDI ↔ Hz dönüşümü (A4 = 440 Hz sabit) |
| `Source/dsp/Scales.h` | 13 dizi maskesi, en yakın izinli nota araması (±7 yarım ses) |
| `Source/dsp/PitchDetector.h` | `PitchDetector`: seyreltilmiş YIN + tam hızda ince ayar + oktav süreklilik kuralı. `PitchTracker`: başlangıç, büyük sıçrama ve kopma kuralları |
| `Source/dsp/KeyDetector.h` | Perde sınıfı histogramı (25 s unutma), 0,5 s'de bir değerlendirme; ton seçimi |
| `Source/dsp/PitchCorrector.h` | Hedef nota, histerezis, bekleme süresi (dwell), S-eğrisi glide, retune alçak geçireni, humanize, ROBOTISM makrosu |
| `Source/dsp/TuneEngine.h` | Bütün akış: halka tamponlar, analiz adımı, perde işaretleri, TD-PSOLA tanecikleri, voiced/unvoiced geçişleri, MIX / bypass, gecikme |
| `Source/Parameters.h` | Kalıcı parametre kimlikleri, aralıklar, varsayılanlar, metin ↔ değer dönüşümleri |
| `Source/PluginProcessor.*` | `AudioProcessor` yaşam döngüsü, `processBlock`, durum kaydetme/yükleme, gecikme bildirimi |
| `Source/PresetManager.*` | 14 fabrika + kullanıcı preseti (XML `.vstmpreset`), değişmiş (dirty) bilgisi |
| `Source/PluginEditor.*`, `Source/ui/*` | 1536×1024 tasarım tuvali, ROBOTISM düğmesi, perde ekranı, kontroller, menüler |
| `Source/Lang.*` | 7 dil (92 metin), işletim sistemi diline göre seçim, `VSTM_TUNE_LANG` ile zorlama |

### 2.2 Ses akışı (INSPECTED, sayılar koddan hesaplandı)

```
giriş (L,R) ─► halka tampon in[ch] (65 536 örnek)
     └─ mono toplam ─► 2× biquad LP (fc = 0,30·sr/decim ≈ 3,2–3,6 kHz) ─► seyreltme (decim = round(sr/11025))
          └─ her hop = 32·decim örnekte (2,7–3,0 ms) analiz:
               PitchDetector (YIN, pencere 256 seyreltilmiş örnek, eşik 0,15; voiced 0,22 / kalış 0,34; kapı RMS 0,0015 ≈ −56 dBFS)
               ─► PitchTracker (3 adımda başlangıç; >4,5 st sıçrama ancak 7 adım ≈ 19–21 ms sürerse; 2 adımlık kopma köprüsü)
               ─► KeyDetector (Auto)  ─► PitchCorrector (hedef çıkış perdesi + "passThrough")
               ─► kayıt (zaman damgalı)  +  perde işaretleri (periyot başına bir, dalga benzerliğiyle ±T/6 hizalama)
     TD-PSOLA sentez (Hann tanecik, hIn = min(periyot, 0,55·gecikme), formant: hOut = hIn/2^(formant/12), kazanç = aralık/hOut ∈ [0,25; 4])
     unvoiced: 4 ms'lik zamana hizalı tanecikler (%50 bindirme), U→V çapraz geçiş, V→U ofset sönümü
          ─► wet ─► MIX (doğrusal, 20 ms tek kutuplu yumuşatma) ve bypass (20 ms) ◄── gecikmeyle hizalı dry
```

**Zamanlama ve stereo:**
- **Gecikme:** REALTIME 20 ms, HQ 40 ms; her örnekleme hızında sabit, `setLatencySamples` ile host'a bildiriliyor.
- **REALTIME modunda analiz geç kalıyor (INSPECTED, hesap):**
  - Analiz penceresinin merkezi, ses aralığına ve perdeye göre ~18–27 ms geride kalıyor.
  - Bu yüzden o anki çıkış örneğinin analizi 0–6 ms (artı en çok bir hop) geç gelebiliyor.
  - Perde işaretleri o anki, geride kalan algıya göre konuyor.
- **Stereo bağlı işleniyor:** Analiz mono toplam üzerinde yapılıyor ve iki kanala aynı tanecik planı uygulanıyor.

### 2.3 İş parçacıkları ve veri akışı (INSPECTED)

- **Ses iş parçacığı:**
  - `processBlock` önce parametreleri atomiklerden okuyor (`readParams`), sonra `engine.setParams` ve `engine.process` çağrılıyor.
  - Ses yolunda **bellek ayırma ve kilit yok**.
  - Tek istisna: REALTIME değişince `setRealtime()` → `reset()` çalışıyor ve ses iş parçacığında ~2 MB tamponu sıfırlıyor (`memset`; bellek ayırma değil).
- **UI iş parçacığı:**
  - Editör zamanlayıcısı 15 Hz'de preset değişmiş bilgisini, bypass solukluğunu ve bulunan tonu güncelliyor.
  - `PitchDisplay` 60 Hz'de `SpscQueue<DisplayPoint, 4096>` kuyruğundan okuyor. Kuyruk dolarsa yeni öğe atılıyor; ses iş parçacığı beklemiyor.
- **Atomik değerler:** `detectedKey`, `currentNote`, `currentVoiced`, `currentInMidi`, `currentOutMidi`.
- **Bellek:** Örnek başına ≈ 2,1 MB (8 × 65 536 float halka tampon + küçük tamponlar). Boyut sabit, çalışma sırasında büyümüyor.
- **Denormal:** `juce::ScopedNoDenormals` var. **NaN/Inf koruması yok** (bkz. 6.2).

### 2.4 Durum (state) ve presetler (INSPECTED)

- **`getStateInformation`:** APVTS XML'ine `presetName`, `uiScale` ve `version=1` ekliyor. `version` yüklemede **okunmuyor**.
- **`setStateInformation`:** `replaceState` çağırıyor, `uiScale`'i 0,5–1,0 arasına sıkıştırıyor ve `presets.restoreCurrent` çağırıyor. Bu son çağrı klasör taraması yapıyor ve snapshot'ı yeniden kuruyor.
- **Presetler:**
  - Yer: `<kullanıcı veri klasörü>/VSTM Audio/VSTM Tune/Presets/*.vstmpreset` (XML `PARAM id/value`).
  - Bypass, Realtime ve Vocal Range preset'e dahil değil. **Key ve Scale dahil.**
- **Geri al:** UndoManager yok; APVTS `nullptr` ile kuruluyor.

### 2.5 Arayüz (INSPECTED + MEASURED ekran görüntüleri)

- **Pencere:** Sabit oranlı (1,5), 768×512 – 1536×1024. Arka plan ve düğme gövdesi önbellekli görüntü.
- **Yazı tipi:** Inter alt kümesi, 211 glif. Türkçe, Almanca ve Fransızca karakterler var; **Kiril ve Çince yok**, bunlar için işletim sistemi yazı tipine düşülüyor (MEASURED).
- **Kontroller:** Sürükleme, Shift ile ince ayar, tekerlek, çift tık ile varsayılan (`SliderParameterAttachment`), değere tıklayıp yazma, sağ tık ile host menüsü, ipuçları.

---

## 3. Mevcut parametreler

Kaynak: `Source/Parameters.h:107-154` (INSPECTED). Host görünümü `VSTMOfflineHost --list` ile doğrulandı (MEASURED): 13 parametre var, 11'i otomasyona açık. VST3 sayısal kimlikleri metin kimliğinin hash'i; adlar arayüz diline göre çevriliyor, kimlikler değişmiyor.

| ID | Host adı (EN) | Tür | Aralık | Varsayılan | Adım / eğri | Otomasyon | Presette | Motor eşlemesi |
|---|---|---|---|---|---|---|---|---|
| `key` | Key | Choice (13) | Auto, C … B | Auto | – | evet | evet | `key = index − 1` (−1 = Auto) |
| `scale` | Scale | Choice (13) | Chromatic … Hicaz | Major | – | evet | evet | dizi indeksi |
| `correction` | Pitch Correction | Float | 0–100 % | 100 | 0,1 | evet | evet | `amount = v/100` |
| `retune` | Retune Speed | Float | 0–200 ms | 15 | 0,1; skew merkez 20 ms | evet | evet | `retuneMs` (tek kutuplu zaman sabiti) |
| `humanize` | Humanize | Float | 0–100 % | 35 | 0,1 | evet | evet | `humanize = v/100` |
| `robotism` | Robotism | Float | 0–100 | 50 | 0,1 | evet | evet | `0` Robotik … `0,5` Dengeli … `1` İnsansı |
| `vibscale` | Vibrato Scale | Float | 0–200 % | 100 | 0,1 | evet | evet | `vibratoScale = v/100` |
| `formant` | Formant | Float | −12…+12 st | 0 | 0,1 | evet | evet | yarım ses |
| `transpose` | Pitch Range | Float | −12…+12 st | 0 | 1,0 | evet | evet | tam sayıya yuvarlanır |
| `mix` | Mix | Float | 0–100 % | 100 | 0,1 | evet | evet | `mix = v/100`, 20 ms yumuşatma |
| `bypass` | Bypass | Bool | Off / On | Off | – | evet (host bypass) | hayır | `active = !bypass`, 20 ms geçiş |
| `realtime` | Realtime | Bool | Off / On | On | – | **hayır** | hayır | 20 ms / 40 ms gecikme |
| `range` | Vocal Range | Choice (3) | Soprano/Alto, Tenor/Mid, Bass/Baritone | Tenor/Mid | – | **hayır** | hayır | algılayıcı 110–1400 / 72–1050 / 50–620 Hz |

**Notlar:**
- Bütün parametrelerin version hint değeri 1.
- Sınır dışı değerleri JUCE kırpıyor, ancak **NaN'ı kırpmıyor** (`jlimit` NaN'ı olduğu gibi geçiriyor; JUCE `"nan"` ve `"inf"` metnini sayıya çeviriyor, INSPECTED). Sonuçları 6.2'de.

---

## 4. Mevcut testler

| Test | Kapsam | Süre | Bu denetimde sonuç |
|---|---|---|---|
| `tests/engine_test.cpp` (VSTMTune_EngineTest) | 44,1 / 48 / 96 kHz'de hard-tune doğruluğu, seviye, şeffaflık ve gecikme hizası, ±12 / +7 transpoze, formant +3, blok bağımsızlığı (64 / 1013), ünlü başlangıcında seviye çukuru. 48 kHz'de Robotik/İnsansı geçiş süresi, nota titremesi, vibrato, Auto key (C majör, D minör), PitchTracker kuralları, sessizlik | 3,7 s | **37/37 geçti** (MEASURED) |
| `tests/plugin_test.cpp` (VSTMTune_PluginTest) | Varsayılanlar ve metinler, 7 dil, gecikme (960 / 1920), tuning, bypass hizası, oturum gidiş-dönüşü, fabrika preseti, sonraki/önceki, editör, 15 ekran görüntüsü, `--vst3` ile tarama, yükleme, gecikme, otomasyon sayısı, ses işleme ve editör | 10 s | **32/32 geçti** (MEASURED) |
| `tests/wav_probe.cpp` (VSTMTune_WavProbe) | Gerçek WAV'ı motordan geçirir, her analiz adımını CSV'ye yazar | – | 12/12 çalıştırma çıkış kodu 0 (MEASURED) |
| `tests/vocal_metrics.py` | LF patlamaları, 10 ms zarf sıçramaları, seviye farkı, yarım sese uzaklık (pYIN+YIN), nota segmentleri, çıkış ile hedef farkı | ~10 s/dosya | 12/12 çalıştı (MEASURED) |
| CI `.github/workflows/vstm-tune.yml` | Windows / macOS / Linux derleme + EngineTest + PluginTest | – | Windows ✅, Linux ✅. **macOS 6/6 denemede iptal** (5 çalıştırma, hiçbir adım çalışmadı) (CI) |

**Mevcut testlerin boşlukları (INSPECTED + MEASURED):**
- **Yalnızca sentetik vokal:** Otomatik testlerin hepsi tek bir ünlü ("aaa") modeliyle çalışıyor; gerçek vokal otomatik testte yok.
- **Bağımsız perde ölçümü oktav hatasını göremiyor:** engine/plugin testlerindeki ölçüm beklenen frekansın **±%20–25** çevresinde arıyor.
- **Bazı hata sınıfları hiç sınanmıyor:**
  - Sağlamlık: NaN/Inf, REALTIME geçişi, otomasyon stresi, uzun süre.
  - Kapsam: 88,2 / 176,4 / 192 kHz, 1 örneklik ve değişken bloklar, farklı içerikli stereo.
  - Durum ve preset: bozuk preset, eski/eksik oturum, kullanıcı preseti kaydet/sil.
  - Bu denetimde ölçülenler 6. bölümde.
- **Kalite ve performans:** CPU benchmark, sanitizer, pluginval ve DAW testi yok.
- **`vocal_metrics.py`'nin zayıf yanları:**
  - Depo kökündeki `tools/audio_view.py`'ye bağımlı (`tests/vocal_metrics.py:27`). Bu dosya zip'te yok, yani **zip tek başına bu hattı çalıştıramaz**.
  - Varsayılan gecikme 20 ms (`:62`); HQ render'lar `--latency` verilmezse kayık ölçülür.
  - `out_vs_target` motorun kendi hedefini referans alıyor ve yalnızca hard ayarda anlamlı (varsayılan ayarda 1–5 kare).
  - pYIN analizinin kendi sapması **+1…+4,3 ct**; çerçevesi 93 ms (MEASURED, 6.1).
- **`wav_probe.cpp`:**
  - WAV chunk boyutları denetlenmiyor (`:50-68`); bozuk dosyada okuma sınırı aşılabilir.
  - Çıkış, girişle aynı uzunlukta; son 20/40 ms (gecikme) kesiliyor.

---

## 5. Bu denetimde çalıştırılan testler (başlangıç / baseline)

### 5.1 Derleme (MEASURED)

```
cmake -S VSTM_Tune -B VSTM_Tune/build-audit -G Ninja -DCMAKE_BUILD_TYPE=Release -DVSTM_BUILD_TESTS=ON \
      -DVSTM_COPY_AFTER_BUILD=OFF -DVSTM_JUCE_PATH=/home/user/juce-framework/juce
cmake --build VSTM_Tune/build-audit --parallel 4      # 3 dk 23 sn, EXIT 0, uyarı: 0
```
Üretilenler: `VST3/VSTM Tune.vst3`, `Standalone/VSTM Tune`, `VSTMTune_EngineTest`, `VSTMTune_WavProbe`, `VSTMTune_PluginTest`. AU yalnızca macOS'ta üretilir; burada **NOT TESTED**.

### 5.2 Test özetleri (MEASURED)

**EngineTest (37/37):**
- **Doğruluk ve seviye:** Hard-tune hatası 0,0002–0,002 ct; seviye farkı ≤ 0,005 dB; şeffaflık SNR 61,3 dB.
- **Transpoze ve formant:** ±12 / +7 st hatası ≤ 0,04 ct; formant +3 perdeyi koruyor.
- **Blok ve geçiş:** Blok farkı 0; ünlü başlangıcında seviye çukuru ≤ 0,19 dB.
- **ROBOTISM:** Robotik geçiş 24 ms, İnsansı 254 ms. Vibrato: Robotik 1,0 ct, İnsansı 98,2 ct tepe-tepe.
- **Diğer:** Titreme testinde 2 s'de 6 nota değişimi; Auto key C ve D'yi buluyor.

**PluginTest (32/32):**
- Gecikme 960 / 1920 örnek; A3 +35 ct girişi 220,00 Hz'e düzeltiliyor; bypass dry sinyali SNR > 60 dB.
- Oturum gidiş-dönüşü çalışıyor.
- VST3 tarama, yükleme, kimlik (VSTM Tune / VSTM Audio), 11 otomasyon parametresi ve editör açılışı (1152×768) çalışıyor.

**VST3 ile WavProbe eşdeğerliği:** Aynı vokal ve ayarla `max|host − probe|` varsayılanda 1,79·10⁻⁷, hard'da 1,19·10⁻⁷.

**Ekran görüntüleri:** 15 dosya (`goruntuler/`). Türkçe, Rusça ve Çince doğru çiziliyor; Rusça ve Çince sistem yazı tipine düşerek (Linux). 768×512'de bazı yazılar çok küçük (≈ 8–9 px).

### 5.3 Gerçek vokal başlangıç ölçümü (MEASURED, 6 RAVDESS klibi)

**Malzeme (MEASURED):**
- 6 RAVDESS şarkı klibi: 48 kHz / 24 bit stereo, 4,2–5,2 s.
- **Kaynak 16 kHz:** 8,5 kHz üstü −110…−130 dB.
- Lisans ticari olmayan; dosyalar depo dışında, `~/.cache/vstm-ear/testdata`. SHA-256 değerleri `vocals/input_sha256.txt`'de.

**Ayarlar:**
- *default:* Plugin varsayılanları (Auto key, Major, ROBOTISM 50, retune 15 ms, humanize 35 %).
- *hard:* Chromatic, ROBOTISM 0, retune 0, humanize 0.

| Metrik (6 vokal) | default (min–max) | hard (min–max) |
|---|---|---|
| Seviye farkı (dB) | −0,19 … −0,03 | −0,26 … −0,08 |
| LF patlaması (20–150 Hz, kuru sese göre >15 dB kare sayısı) | 0 … 8 | 1 … 7 |
| LF fazlası en büyük (dB) | 13,1 … 30,5 | 16,1 … 27,6 |
| 10 ms zarf sıçraması | 0 … 3 | 0 … 4 |
| Yarım sese uzaklık medyanı (ct, pYIN) | 9,1 … 20,8 | 0,8 … 16,4 |
| ±10 ct içinde kare oranı (%) | 23,9 … 52,2 | 37,9 … 73,0 |
| <40 ms nota segmenti / toplam | 69 / 168 | 76 / 178 |
| Çıkış–hedef farkı p90 (ct, yalnız hard) | – | 18 … 135 |

**Yorum ve sınırlar:**
- *vocal5:* Konuşmaya yakın, sürekli kaymalarla (glide) dolu bir klip. Sert ayarda "16 ct" uzaklık büyük ölçüde kayma bölümlerinden ve pYIN'in 93 ms'lik penceresinin basamakları yumuşatmasından geliyor (grafik: `vocals/vocal5_hard_pitch.png`).
- **Bu tablo ses kalitesini kanıtlamaz.** Bant genişliği 8 kHz ve dinleme yok.

---

## 6. Hedefli ölçümler (bu denetimde, S1–S15)

Ham sonuçlar `out/measurements.jsonl` dosyasında; analiz `analysis/independent_pitch.json`'da. Sinyal sentetik: formantlı glottal darbe dizisi, 48 kHz (aksi yazmadıkça).

### 6.1 Ölçüm aracının doğrulanması (yol haritası 1.3) – MEASURED

`tools/audio_view.py pitch_track` (pYIN + YIN), frekansı bilinen harmonik tonlarla sınandı:

| Hz | 82,4 | 110 | 196 | 220 | 440 | 659 | 880 | 1046 |
|---|---|---|---|---|---|---|---|---|
| Medyan hata (ct) | +4,28 | +3,25 | +1,78 | +1,68 | +1,15 | +0,99 | +1,84 | +2,43 |
| En büyük hata (ct) | 6,68 | 4,65 | 2,55 | 2,32 | 1,48 | 1,23 | 2,03 | 2,57 |

Bu yüzden `vocal_metrics.py`'nin "cents" metriklerinde **+1…+4 ct'lik bir taban** var. Ayrıca 93 ms'lik pencere kısa notaları ve geçişleri yumuşatıyor.

### 6.2 Sağlamlık (yol haritası 1.4 / 10.1)

| # | Senaryo | Sonuç | Kanıt |
|---|---|---|---|
| S2 | Hard-tune'da 1,0 s'de **tek bir NaN** (ya da +Inf) giriş örneği | Çıkışta 5 non-finite örnek oluşuyor (1,020 s). **Sonrasında düzeltme hiç dönmüyor:** 2,5 s sonrasında 563 adımın 0'ı voiced, 3,2 s'de çıkış +40 ct (düzeltilmemiş giriş). Nedeni: anti-alias biquad durumu NaN'da kalıyor (`TuneEngine.h:178`, `DspCommon.h:43-49`) | MEASURED |
| S15 | Motor parametresi 0,5 s boyunca NaN, sonra geçerli değer | `amount` / `transpose`: 427 NaN örnek, sonra **kalıcı sessizlik** (−281 dB). `formant`: **kalıcı sessizlik**. `mix`: **kalıcı NaN çıkış**. Neden: `sNext` NaN oluyor ve yeniden eşitleme koşulu NaN'ı yakalamıyor (`TuneEngine.h:652`); `mixSmooth` NaN'da kalıyor (`:192`) | MEASURED |
| State | Oturumda `correction="nan"`, `mix="inf"`, `transpose="99"`, `formant="-1e9"` | inf, 99 ve −1e9 doğru kırpılıyor; **`correction` raw NaN kalıyor ve ses non-finite oluyor (FAIL)** | MEASURED (`state_preset_test.log`) |
| Preset | `B_nan.vstmpreset` (`correction` = nan) yükleme | **non-finite ses (FAIL)** | MEASURED |
| Preset | Kesik XML, boş dosya, aralık dışı (transpose 99, formant −99, key 57) | Kesik ve boş dosya yok sayılıyor; aralık dışı değerler kırpılıyor; ses sonlu ✓ | MEASURED |
| State | 4 KB rastgele blob, boş blob, yabancı kök etiketi, eksik/eski oturum (bilinmeyen kimlik, eksik `retune`) | Hepsi güvenli: eksik parametre varsayılana dönüyor, bilinmeyen kimlik yok sayılıyor ✓ | MEASURED |
| Preset | Kullanıcı preseti kaydet → üzerine yaz → yükle → sil | ✓. Geçerli preset silinince adı hâlâ gösteriliyor (küçük UX) | MEASURED |
| S3 | REALTIME → HQ (1,0 s) → REALTIME (2,0 s) | **40 ms tam sessizlik**; geçişte örnek adımı 0,155, sabit durumda 0,029 (≈ 5×, tık). `reset()` ses iş parçacığında çalışıyor ve Auto key'i siliyor (`TuneEngine.h:98-122`) | MEASURED |
| S4 | 22,05 / 32 / 44,1 / 48 / 88,2 / 96 / 176,4 / 192 kHz | Hepsinde hard-tune 0,00 ct, şeffaflık SNR 84,7–93,6 dB, gecikme 20,0 ms, hop 2,67–3,0 ms, çıkış sonlu ✓ | MEASURED |
| S5 | Blok 1, 2, 7, 32, 64, 128, 333, 1024, 4096, 8192 ve rastgele 1–2048, 512'ye karşı | **Hepsinde fark 0** (bit düzeyinde aynı) ✓ | MEASURED |
| S6 | Stereo: L = ses, R = sessizlik | R tam sessiz kalıyor, L 0,00 ct ✓ | MEASURED |
| S6 | Stereo: L = A3 +40 ct, R = E4 +10 ct (farklı içerik) | L +16,7 ct, R −13,0 ct sapmalı: bağlı stereo ve mono toplam analizi. **Kanallar farklı içerik taşıyorsa doğru çalışmıyor** (sınırlama olarak belgelenmeli) | MEASURED |
| S7 | Her 256 örnekte rastgele parametre sıçraması (tüm parametreler, bypass dahil) | Çıkış sonlu; >0,2 örnek adımı yok; tepe 0,589 (otomasyonsuz 0,305: aşırı formant ve transpoze kazancı) | MEASURED |
| S9 | 10 dakika sürekli (48 kHz, hard) | Çıkış sonlu; tutulan notalarda 20–40 s ve 580–600 s bölümlerinde en kötü 0,00 ct; render 12 s | MEASURED |

### 6.3 Perde algılama ve geçişler (yol haritası 2.x / 3.x) – MEASURED, sentetik

**S1, legato büyük sıçramalar.** Notalar 1 s, 25 ms kaymayla, hepsi +30 ct detone. Hard ve varsayılan ayar, REALTIME:

| Ses | Notalar | Motorun algıladığı perde | Çıkış perdesi (pYIN, geniş aralık) | Alt oktav (f0/2) enerjisi |
|---|---|---|---|---|
| Tenor "a" | A3 → A4 → A3 → E4 → G3 | her notada doğru (0 oktav hatası) | doğru oktav, hedefe ±0,01 ct | kuru sesle aynı (−74…−83 dB) |
| Bas "o" (zayıf temel, Tenor ve Bass aralığı) | G2 → G3 → G2 → D3 → E2 | doğru | doğru | kuru sesle aynı |
| Soprano "i" (Tenor ve Soprano aralığı) | A4 → **A5** → A4 → E5 → G4 | **A5 notası boyunca bir oktav aşağıda** (311 adım, iki aralıkta da) | doğru oktav (medyan 81,02 MIDI; hedef 81) | **A5'te −45,4 dB; kuru seste −76,6 dB (+31 dB)** |

Nota başlangıcından itibaren çıkışın ±20 ct'ye oturma süresi 30–70 ms (25 ms kayma dahil).

**Kök neden (INSPECTED):** `PitchDetector.h:130-135`. Yukarı oktavda ölçülen periyodun yarıya düşmesi, "alt oktav cmnd'si +0,04'e kadar daha kötü olsa bile eski oktavı tut" kuralına takılıyor.

**Sonuç:** Çıkış perdesi doğru kalıyor, çünkü tanecikler iki periyot içeriyor. Ancak alt oktav pürüzü olası, perde ekranı yanlış oktavı gösteriyor ve gerçek vokalde etkisi bilinmiyor. **Dinleme paketi hazır** (bkz. 12).

**S14, ROBOTISM taraması.** Retune 15 ms, humanize %35. A3 → C4 adımı (20 ms kayma), ±50 ct vibrato (5,5 Hz), A3 +40 ct sabit ton:

| ROBOTISM | Geçiş %10→%90 | Kalan vibrato (100 ct'den) | 0,5 s'de hata | Histerezis | Bekleme | Glide | Başlangıç kayışı | τ kısa / uzun nota |
|---|---|---|---|---|---|---|---|---|
| 0 | 24 ms | 1,0 ct | 0,00 ct | 15 ct | 14 ms | 0 | 0 | 0 / 0 ms |
| 10 | 24 ms | 0,9 ct | 0,00 | 15 | 16 | 0 | 0 | 0,6 / 0,9 |
| 20 | 38 ms | 9,1 ct | 0,00 | 15 | 17 | 0 | 0 | 2,4 / 4,4 |
| 35 | 52 ms | 41,3 ct | 0,00 | 15 | 20 | 0 | 0 | 7,3 / 18,2 |
| 50 | 62 ms | 73,5 ct | 0,00 | 15 | 22 | 0 | 0 | 15 / 46,5 |
| 65 | 108 ms | 89,1 ct | 0,01 | 21 | 31 | 33 | 21 | 28,5 / 101,7 |
| 80 | 162 ms | 94,1 ct | 0,07 | 27 | 40 | 66 | 42 | 42 / 169,5 |
| 90 | 206 ms | 95,7 ct | 0,24 | 31 | 46 | 88 | 56 | 51 / 221,7 |
| 100 | 262 ms | 96,8 ct | 0,58 | 35 | 52 | 110 | 70 | 60 / 279,6 |

**Yorum:**
- Eğri sürekli ve monoton; **0,5 çevresinde ani değişim yok.**
- **%0–10 arası pratikte aynı** (ölü bölge).
- %50→65 arasında geçiş süresi hızla artıyor: glide, başlangıç kayışı ve histerezis aynı anda devreye giriyor.
- Bütün değerler 1,5 s'de ~0 ct'ye oturuyor.
- Algısal kalibrasyon **yapılmadı** (dinleme yok).

### 6.4 Gürültü, ton, seviye, MIX (MEASURED, sentetik)

**S10, unvoiced:**
- Voiced oranı beyaz gürültüde ("s" benzeri, −30 dBFS), nefeste (−48 dBFS) ve sessizlikte **0,000**; ünlüde 1,000.
- Gürültü ve nefeste seviye 0,00 dB; "t" patlamasında −1,63 dB.

**S11, Auto Key** (Scale = Major, aksi belirtilmedikçe):
- **Tek bir nota (C4) 5 s:** 2,01 s'de **"C"** bildiriyor. Tek nota tonu belirlemez; yanlış kesinlik.
- **C4/G4 dönüşümlü:** 2,01 s'de "C". C ve G'yi içeren beş majör ton var (C, F, G, B♭, E♭); seçim belirsiz ama kesin bildiriliyor.
- **0–8 s C majör, 8–16 s G majör:** 16 s boyunca **"C"**; modülasyon 8 s içinde algılanmıyor (25 s histogram belleği).
- **A minör melodi, Scale = Major:** 2,01 s'de **"G"**, 3,02 s'de "C" (ilk tahmin 1 s yanlış). Scale = Minor seçilince: "A" ✓.

**S12, büyük kaydırma** (A3 +40 ct, hard). Perde her durumda doğru (≤ 0,01 ct); seviye farkları:

| Ayar | formant −12 | −6 | +6 | +12 | transpoze −12 | −5 | +7 | +12 | −12 / formant −4 |
|---|---|---|---|---|---|---|---|---|---|
| RMS farkı (dB) | −7,36 | −1,39 | +1,05 | +2,59 | +2,59 | +0,83 | −1,08 | −7,36 | +1,59 |
| Tepe (giriş 0,300) | 0,288 | 0,287 | 0,420 | **0,593** | **0,593** | 0,396 | 0,300 | 0,300 | 0,471 |

**S13, MIX** (A3 +40 ct, hard). MIX %50'de kuru ve düzeltilmiş ses 5,1 Hz'de birbirini döverek **6,3 dB tepe-tepe zarf dalgalanması** ve −3,0 dB seviye üretiyor. MIX %0 ve %100'de bu değer 0,6 dB. Doğrusal geçişin doğal sonucu; dinleme ile değerlendirilmeli.

### 6.5 CPU (MEASURED; paylaşımlı sanal makine, gürültülü)

Varsayılan ayar, 20 s melodi, 64 örneklik bloklar:

| sr | Kanal | Mod | Formant | Ortalama CPU (tek çekirdeğin %'si) | Blok p99 (µs) | 64 örnek bütçesi (µs) |
|---|---|---|---|---|---|---|
| 44,1 k | 1 / 2 | RT / HQ | 0 / 3 | 1,51 – 2,01 | 58 – 115 | 1451 |
| 48 k | 1 / 2 | RT / HQ | 0 / 3 | 1,80 – 2,20 | 65 – 88 | 1333 |
| 96 k | 1 / 2 | RT / HQ | 0 / 3 | 2,87 – 3,19 | 105 – 156 | 667 |

- En uzun blok süresi koşudan koşuya 0,1–3,3 ms arasında değişti; bazıları 64 örneklik bütçeyi aştı. Tutarsız oldukları için sanal makine kaynaklı olabilirler; **gerçek donanımda profiler ile yeniden ölçülmeli.**
- Formant açıkken maliyet en çok +0,4 puan arttı.
- Ekran (UI) CPU'su ölçülmedi.

---

## 7. Gerçekten çalışan özellikler (kanıtla)

| Özellik | Kanıt |
|---|---|
| Derleme (Linux), VST3 + Standalone + testler, 0 uyarı | MEASURED |
| Windows derlemesi, EngineTest ve PluginTest (VST3 yükleme dahil) | CI (çalıştırma #1–#5) |
| Hard-tune doğruluğu (sentetik, tüm örnekleme hızları) ≤ 0,01 ct | MEASURED |
| Düzeltme 0 iken şeffaflık ve dry ile örneğine kadar hiza (SNR 61–94 dB) | MEASURED |
| Blok boyutundan bağımsızlık (1…8192 ve rastgele, bit düzeyinde aynı) | MEASURED |
| Bildirilen gecikme = ölçülen hiza (960 / 1920 @ 48 kHz) | MEASURED |
| Bypass: gecikmesi korunmuş temiz dry (SNR > 60 dB), 20 ms geçiş | MEASURED |
| ROBOTISM makrosu sürekli ve monoton; README'deki 24 / 254 ms ve 1 / 98 ct değerleri doğru | MEASURED (sentetik) |
| Nota titremesi önleme (histerezis + bekleme) | MEASURED (sentetik, EngineTest) |
| Gürültü ve nefeste yanlış voiced yok; seviye korunuyor | MEASURED (sentetik) |
| Tenor ve bas legato oktav / beşli sıçramalarında doğru takip | MEASURED (sentetik) |
| Auto key, örnek C majör ve D minör melodilerde doğru | MEASURED (sentetik) |
| 10 dakika kararlılık, otomasyon stresinde sonlu çıkış | MEASURED (sentetik) |
| Oturum kaydetme/yükleme; bozuk, boş ve eski oturumlarda güvenli davranış (NaN hariç) | MEASURED |
| Fabrika ve kullanıcı presetleri, kaydet / üzerine yaz / sil (NaN hariç) | MEASURED |
| 7 dil, diller arası değer okuma, Türkçe İ/ı | MEASURED (PluginTest + ekran görüntüsü, Linux) |
| VST3 = motor (aynı çıktı) | MEASURED |
| Düşük CPU (ortalama %1,5–3,2) | MEASURED (sanal makine) |

## 8. Test edilmemiş özellikler (NOT TESTED)

- **Ses kalitesi:**
  - Gerçek bir DAW'da her şey: REAPER, Ableton, FL Studio, Cubase, Studio One, Logic.
  - PDC, otomasyon, freeze/bounce, offline render, çoklu örnek, proje yeniden açma.
- **Platform:**
  - **macOS derlemesi ve AU**: CI'da hiç çalışmadı.
  - Apple Silicon'da CPU.
  - Windows'ta HiDPI %125/150/200 ölçekleme (CI ekran görüntüleri bu denetimde incelenmedi).
- **Uygulama ve araçlar:**
  - Standalone uygulamada ses giriş/çıkışı.
  - pluginval, ASan / UBSan / TSan, statik analiz, saatlerce süren testler, bellek sızıntısı.
- **Gerçek vokal davranışları:**
  - Tam bantlı gerçek vokalde ünsüz ve sibilans geçişleri, formant doğallığı, vibrato analizi.
  - Rap, hızlı melodi ve uzun nota.
  - Bas / soprano gerçek kayıtları.
- **Kullanım ve hukuk:**
  - Klavye ile gezinme ve ekran okuyucu erişilebilirliği.
  - Kurulum / kaldırma, kod imzalama, lisans uyumu.
- **Dinleme:** **Hiçbir ayar insan kulağıyla dinlenmedi.**

---

## 9. Bilinen riskler (önem sırasıyla)

| # | Risk | Önem | Kanıt | Yer |
|---|---|---|---|---|
| R1 | Tek bir NaN/Inf giriş örneği düzeltmeyi motor sıfırlanana kadar kapatıyor; 5 NaN örnek çıkışa geçiyor. **✅ Düzeltildi (`00bde73`, Bölüm 15.2)** | **Yüksek** | MEASURED | `Source/dsp/TuneEngine.h:170-185`, `Source/dsp/DspCommon.h:43-49` |
| R2 | NaN parametre (bozuk oturum/preset) kalıcı sessizlik ya da NaN çıkış üretiyor; değer düzelse de geçmiyor. **✅ Düzeltildi (`00bde73`, Bölüm 15.2)** | **Yüksek** | MEASURED | `Source/PluginProcessor.cpp:49-65`, `Source/PresetManager.cpp:82-89`, `Source/dsp/TuneEngine.h:192,652` |
| R3 | Gerçek vokal test seti fiilen yok: 6 RAVDESS klibi, 8 kHz bantlı, NC lisans, depo dışı, metadata yok. Önceki bir oturumda kullanılan 2:48'lik vokal stem'i (MP3, kaynak ayrıştırıcı çıktısı) da depoda değil | **Yüksek** | MEASURED / INSPECTED | `~/.cache/vstm-ear/testdata`, `artifacts/audio-runs/tune-listening/README.txt` |
| R4 | İnsan dinlemesi, DAW testi ve macOS/AU derlemesi yok | **Yüksek** | CI / INSPECTED | `.github/workflows/vstm-tune.yml` |
| R5 | REALTIME geçişinde 40 ms sessizlik + tık + Auto key sıfırlanması. **✅ Düzeltildi (`154eddc`, Bölüm 15.3)** | Orta | MEASURED | `Source/dsp/TuneEngine.h:98-122`, `Source/PluginProcessor.cpp:92-94` |
| R6 | Yüksek seste legato yukarı oktavda algılayıcı eski oktavda kalıyor; alt oktav +31 dB, ekranda yanlış oktav | Orta | MEASURED (sentetik) | `Source/dsp/PitchDetector.h:119-136` |
| R7 | Auto key: 1–2 notayla yanlış kesinlik, yavaş modülasyon takibi (>8 s), ilk tahminin yanlış olabilmesi; güven bilgisi yok | Orta | MEASURED (sentetik) | `Source/dsp/KeyDetector.h:29,59,84` |
| R8 | Büyük transpoze / formant'ta −7,4…+2,6 dB seviye ve +5,9 dB tepe kayması; çıkış koruması yok | Orta | MEASURED (sentetik) | `Source/dsp/TuneEngine.h:528,545` |
| R9 | CI test öncesi paket yüklüyor; başarısız testte yayın engeli, pluginval, WAV regresyonu ve sanitizer yok | Orta | INSPECTED | `.github/workflows/vstm-tune.yml:62-138` |
| R10 | `setStateInformation` içinde dosya taraması ve snapshot yeniden kurulumu var; host bunu UI dışı iş parçacığından çağırırsa editör zamanlayıcısındaki `isDirty()` ile veri yarışı olabilir | Orta / Düşük | INSPECTED | `Source/PluginProcessor.cpp:127`, `Source/PresetManager.cpp:179-201`, `Source/PluginEditor.cpp:388-390` |
| R11 | `vocal_metrics.py` depo kökündeki `tools/`'a bağımlı (zip'te yok); varsayılan 20 ms gecikme; pYIN tabanı +1…+4 ct; `out_vs_target` motorun kendi hedefini kullanıyor. (WavProbe'un WAV ayrıştırma açıkları `76f8594`'te kapatıldı; vocal_metrics maddeleri açık) | Orta (QA) | MEASURED / INSPECTED | `tests/vocal_metrics.py:27,62,117` |
| R12 | Stereo bağlı ve mono toplam analizli: kanal içerikleri farklıysa yanlış düzeltme | Düşük–Orta | MEASURED (sentetik) | `Source/dsp/TuneEngine.h:170-177` |
| R13 | REALTIME'da analiz 0–6 ms geç kalabiliyor; perde işaretleri büyük sıçramada gecikmeli algıyı izliyor (oturma 30–70 ms) | Düşük–Orta | INSPECTED + MEASURED | `Source/dsp/TuneEngine.h:322-375` |
| R14 | MIX %50'de 6,3 dB vuruntu (doğrusal geçiş, kaçınılmaz olabilir) | Düşük | MEASURED (sentetik) | `Source/dsp/TuneEngine.h:215` |
| R15 | Preset yükleme Key ve Scale'i de değiştiriyor (kullanıcının tonu kaybolur) | Düşük (UX) | INSPECTED | `Source/PresetManager.cpp:30-33,82` |
| R16 | İngilizce etiketler çakışıyor: vibrato düğmesi "SCALE", transpoze "PITCH RANGE" | Düşük (UX) | INSPECTED + ekran görüntüsü | `Source/Lang.cpp:35,37` |
| R17 | Inter alt kümesinde Kiril ve CJK yok; işletim sistemi yazı tipine bağlı | Düşük | MEASURED | `Resources/fonts/*`, `Source/ui/Theme.h:52-54` |
| R18 | Oturum `version` alanı okunmuyor; ileride göç yolu yok | Düşük | INSPECTED | `Source/PluginProcessor.cpp:113,118-130` |
| R19 | Undo ve A/B yok | Düşük | INSPECTED | `Source/PluginProcessor.cpp:11` |
| R20 | Mutlak voicing kapısı (−56 dBFS); çok sessiz kayıtta düzeltme kesilebilir | Düşük | INSPECTED | `Source/dsp/PitchDetector.h:149,168` |
| R21 | ROBOTISM %0–10 ölü bölge | Düşük | MEASURED | `Source/dsp/PitchCorrector.h:150` |
| R22 | `placeTailGrains` her çıkış örneğinde `std::pow` hesaplıyor (gereksiz CPU) | Düşük | INSPECTED | `Source/dsp/TuneEngine.h:628` |
| R23 | Editör kapalıyken perde kuyruğu ~12 s eski veriyle doluyor; açılınca bir anda çiziliyor | Düşük | INSPECTED | `Source/ui/PitchDisplay.h:142-201` |
| R24 | "AUTOTUNE" ifadesi (arayüz, README). "Auto-Tune" Antares Audio Technologies'in tescilli markası; ticari üründe hukuki kontrol gerekli | Hukuki | INSPECTED | `Source/Lang.cpp` ("AUTOTUNE ON/OFF"), `README.md` |
| R25 | JUCE 8 lisansı (açık kaynak seçeneği AGPLv3 ya da ticari lisans) ve paketlenen VST3 SDK koşulları ticari dağıtım için doğrulanmadı | Hukuki | INSPECTED | `CMakeLists.txt` |

---

## 10. Yol haritası maddelerinin durumu

Durum değerleri: **MEVCUT – TEST EDİLECEK** (uygulanmış, kanıtı eksik) · **KISMEN MEVCUT** · **GELİŞTİRİLECEK** (yok).

| Madde | Durum | Gerekçe / kanıt |
|---|---|---|
| **1.1** Gerçek vokal test veri seti | **GELİŞTİRİLECEK** | Yalnızca 6 RAVDESS klibi: 8 kHz bantlı, NC lisans, depo dışı, metadata yok (MEASURED). Ses aralığı, stil, mikrofon çeşitliliği yok. Commit `faf17eb`'deki ölçümlerde kullanılan 2:48'lik stem (MP3, ayrıştırıcı çıktısı) depoda değil; kayıplı formatta, hakları belirsiz |
| **1.2** WAV test hattı otomasyonu | KISMEN MEVCUT | WavProbe + vocal_metrics var. Tek komutluk matris, JSON/CSV toplama, önceki sürümle regresyon raporu ve otomatik gecikme yok. Bu denetimdeki `vocals/run_baseline.sh` bir başlangıç. *Güncelleme:* WavProbe WAV hata denetimi ve kuyruk (flush) eklendi, `VSTMTune_AuditProbe` depoda (`76f8594`) |
| **1.3** Bağımsız perde doğrulama | KISMEN MEVCUT | engine_test ölçümü oktav hatasına kör; pYIN+YIN tabanı +1…+4 ct (MEASURED); oktav hatası, voiced FP/FN ve jitter metriği yok |
| **1.4** Gerçek zamanlı kararlılık | KISMEN MEVCUT | Örnekleme hızı, blok, stereo (aynı içerik), 10 dk ve otomasyon ✓ (MEASURED). Denetimde NaN/Inf ✗ ve REALTIME geçişi ✗ idi; **ikisi de düzeltildi ve testle doğrulandı** (`00bde73`, `154eddc`). Host içinde preset/bypass/yeniden başlatma NOT TESTED |
| **2.1** F0 doğruluğu | KISMEN MEVCUT | Sentetikte doğru; **soprano legato oktavında takılma** (MEASURED); gerçek vokalde referanslı doğruluk yok |
| **2.2** Güven sistemi | KISMEN MEVCUT | Clarity yalnızca voicing ve ton histogramında; birleşik güven, güvene göre yumuşatma ve UI göstergesi yok |
| **2.3** Voiced / unvoiced | MEVCUT – TEST EDİLECEK | Histerezis, başlangıç, köprü, U→V çapraz geçiş var; sentetikte FP yok (MEASURED). Tam bantlı gerçek ünsüzlerde NOT TESTED |
| **2.4** Algılayıcı performansı | KISMEN MEVCUT | Motorun toplam CPU'su ölçüldü (MEASURED); modül bazlı profil ve önce/sonra raporu yok |
| **3.1** ROBOTISM ana kontrolü | MEVCUT – TEST EDİLECEK | Sürekli ve monoton (MEASURED tablo), değer yazısı var; dinleme yok |
| **3.2** ROBOTISM eğri kalibrasyonu | KISMEN MEVCUT | Formüle dayalı; 9 nokta sentetik ölçüldü; algısal kalibrasyon ve ses aralığı tutarlılığı yok; %0–10 ölü bölge |
| **3.3** Nota seçimi ve histerezis | MEVCUT – TEST EDİLECEK | 15–35 ct histerezis, 14–52 ms bekleme, "vardı" kuralı; key/scale değişiminde hemen geçiş (glide yalnızca İnsansı tarafta) |
| **3.4** Retune ve geçiş | MEVCUT – TEST EDİLECEK | Geçiş 24–262 ms (MEASURED); kısa nota, rap ve uzun nota gerçek örneklerle NOT TESTED |
| **3.5** Amount | MEVCUT – TEST EDİLECEK | 0 şeffaf, 100 tam (MEASURED). Vibrato Scale ≠ %100 iken amount 0'da da perde değişiyor (`PitchCorrector.h:89,166`, tasarım gereği; belgelenmeli) |
| **4.1** Geçiş artefaktları | KISMEN MEVCUT | U→V düzeltmesi var (commit `232651d`); gerçek vokalde LF patlaması 0–8/dosya (MEASURED); büyük transpozede LF bilinen sınır |
| **4.2** Formant | KISMEN MEVCUT | Perde korunuyor; seviye −7,4…+2,6 dB (MEASURED); doğallık ve metaliklik dinlenmedi |
| **4.3** Vibrato | KISMEN MEVCUT | Var olan vibratoyu ölçekliyor (MEASURED); vibrato analizi ve **vibrato ekleme yok** |
| **4.4** Seviye ve spektrum | KISMEN MEVCUT | 0 st'de seviye korunuyor; büyük kaydırmada kayma; limiter / true-peak yok; MIX doğrusal (%50'de vuruntu) |
| **4.5** Gecikme | MEVCUT – TEST EDİLECEK | Bildirilen = ölçülen ✓; mod değişiminde kesinti denetimde ✗ idi, **düzeltildi** (5 ms kısma/açma, `154eddc`); DAW PDC NOT TESTED |
| **5.1** Auto Key doğruluğu | KISMEN MEVCUT | Örnek melodilerde doğru; yanlış kesinlik, yavaş modülasyon (MEASURED); güven ve alternatif yok |
| **5.2** Kullanıcı kontrolü | MEVCUT – TEST EDİLECEK | Auto ile sabit ton ayrı; algılama parametreyi ezmiyor; "Auto · A" gösteriliyor; "bulunan tonu sabitle" eylemi yok |
| **5.3** Dizi kapsamı | KISMEN MEVCUT | 13 dizi maskesi incelemeyle doğru (Hicaz = 0,1,4,5,7,8,10); dizi başına otomatik test, UI nota haritası ve Custom Scale yok |
| **5.4** Akor farkındalığı | GELİŞTİRİLECEK | Yok; arayüzde de gösterilmiyor (doğru) |
| **6.1** Üç karakter modu | GELİŞTİRİLECEK | Yok (fabrika presetleri kısmen bu rolü görüyor) |
| **6.2** Adaptive mod | GELİŞTİRİLECEK | Yok |
| **6.3** Scale Note Edit | GELİŞTİRİLECEK | Yok |
| **6.4** Gelişmiş panel | KISMEN MEVCUT | Bütün kontroller ana ekranda; Vocal Range ⚙ menüsünde; ipuçları var; ayrı panel ve birim açıklaması yok |
| **7.1** Ana ekran | MEVCUT – TEST EDİLECEK | ROBOTISM merkezde, Robotik / İnsansı etiketli, perde ekranı üstte (ekran görüntüsü) |
| **7.2** Perde ekranı | MEVCUT – TEST EDİLECEK | 6 s tarama, kilitsiz kuyruk, nota noktaları; zoom yok; ses akarken sessizlikte de 60 Hz çizim; açılışta eski veri |
| **7.3** Kontroller | KISMEN MEVCUT | Sürükleme, Shift, tekerlek, çift tık, yazarak giriş, sağ tık var; ok tuşları doğrulanmadı; 768×512'de küçük yazılar; etiket çakışması (R16) |
| **7.4** Ölçekleme | KISMEN MEVCUT | 768–1536 sabit oran; Linux ekran görüntüleri ✓; Windows/macOS DPI NOT TESTED |
| **7.5** Dil desteği | MEVCUT – TEST EDİLECEK | 7 dil, 92 metin tam; Kiril/CJK için yazı tipi düşüşü (Linux ✓, diğerleri NOT TESTED) |
| **8.1** Parametre uyumluluğu | MEVCUT – TEST EDİLECEK | Sabit kimlikler, version hint 1; NaN temizleme denetimde yoktu, **eklendi** (`00bde73`); yumuşatma yalnızca mix ve bypass'ta |
| **8.2** Oturum geri yükleme | KISMEN MEVCUT | Gidiş-dönüş, bozuk, eski ve yabancı oturum ✓; **NaN artık ✓** (PluginTest 3b, `00bde73`); `version` okunmuyor; iş parçacığı riski (R10) |
| **8.3** Preset sistemi | KISMEN MEVCUT | 14 fabrika + kullanıcı ✓; bozuk dosya ✓, **NaN artık ✓**, kaydet/üzerine yaz/sil otomatik testte (PluginTest 3c); Transparent, Fast Rap ve Balanced Tune kategorileri yok; preset Key/Scale'i değiştiriyor; geçiş anlık |
| **8.4** A/B ve undo | GELİŞTİRİLECEK | UndoManager yok, A/B yok |
| **9** Host uyumluluğu | KISMEN MEVCUT | JUCE VST3 host'unda yükleme: Linux (MEASURED) ve Windows (CI) ✓; DAW matrisi NOT TESTED; **macOS/AU hiç derlenmedi** (CI); pluginval yok |
| **10.1** Ses iş parçacığı | KISMEN MEVCUT | Bellek ayırma ve kilit yok (INSPECTED), denormal ✓; **NaN/Inf artık ✓** (`00bde73`); REALTIME geçişinde ~2 MB'lık tam sıfırlama yerine yalnızca eski zaman çizgisinin ilerisi temizleniyor (48 kHz'de ~9 400 konum; `154eddc`); TSan yok |
| **10.2** CPU | KISMEN MEVCUT | Bu denetimde ölçüldü; depoda benchmark yok |
| **10.3** Bellek | KISMEN MEVCUT | Örnek başına sabit ~2,1 MB; sızıntı testi yok; oturum ve preset ayrıştırma güvenli (NaN dahil, `00bde73`); WavProbe ayrıştırıcısı ASan altında temiz (`76f8594`) |
| **10.4** Kod kalitesi | KISMEN MEVCUT | 0 uyarı (GCC 13); sanitizer ve statik analiz yok |
| **11.1** Gelişmiş tuning | KISMEN MEVCUT | Transpoze ±12 ve giriş perdesi gösterimi var; **A4 = 440 sabit** (`DspCommon.h:19-20`); MIDI hedefi ve dizi dışı nota seçenekleri yok |
| **11.2** İş akışı | KISMEN MEVCUT | Sıfırlama ve preset klasörünü açma var; favori, son kullanılan, A/B, undo, kopyala/yapıştır yok; kısa yardım yalnızca ROBOTISM "i" balonu |
| **11.3** Gelişmiş analiz | KISMEN MEVCUT | Sapma (ct) ve hedef nota etiketi var, gecikme "Hakkında" penceresinde; güven ve düzeltme miktarı göstergesi yok |
| **11.4** Preset tasarımı | KISMEN MEVCUT | Fabrika ile kullanıcı ayrı; preset açıklamaları ve dinlemeyle hazırlama kaydı yok |
| **12** A/B değerlendirme protokolü | GELİŞTİRİLECEK | `tools/audio_view.py pack` var; önceki paketlerde kayıtlı insan sonucu yok; referans yazılımla karşılaştırma yok |
| **13.1** Build | KISMEN MEVCUT | Windows ve Linux ✓ (CI); macOS Universal/AU yapılandırılmış ama hiç derlenmemiş; sürüm sabit 1.0.0, build numarası yok |
| **13.2** Installer | GELİŞTİRİLECEK | Yalnızca zip + KURULUM.txt; imzasız |
| **13.3** CI/CD | KISMEN MEVCUT | 3 OS'ta derleme ve test var; yayın kapısı, WAV regresyonu, pluginval, sanitizer ve changelog yok |
| **13.4** Lisanslar | KISMEN MEVCUT | Inter OFL dosyası var; JUCE 8 ve VST3 SDK ticari koşulları, RAVDESS NC ve "Auto-Tune" markası doğrulanmalı (R24, R25) |

### 10.1 Bölüm 17 (release öncesi kontrol listesi) – mevcut durum

| Madde | Durum |
|---|---|
| Release build Windows | ✅ CI |
| Release build macOS | ❌ hiç çalışmadı (CI iptal) |
| VST3 tarama ve yükleme | ✅ JUCE host (Linux MEASURED, Windows CI); DAW ❌ |
| AU tarama ve yükleme | ❌ |
| Standalone açılış ve ses G/Ç | ❌ NOT TESTED |
| Örnekleme hızı ve blok testleri | ✅ motor (MEASURED); host ❌ |
| Gecikme host'a doğru bildiriliyor | ✅ (JUCE host); DAW PDC ❌ |
| Oturum kaydet/yükle | ✅ bozuk / eski / NaN oturum dahil (PluginTest 3b) |
| Preset kaydet/yükle | ✅ bozuk / NaN dosya, kaydet / üzerine yaz / sil dahil (PluginTest 3c) |
| Tüm parametrelerde host otomasyonu | ⚠️ yalnızca sayı ve stres (motor); DAW ❌ |
| Gerçek vokal perde doğruluğu ölçüldü | ⚠️ 8 kHz bantlı 6 klip |
| ROBOTISM tüm aralıkta dinlendi | ❌ |
| Sibilans ve ünsüz geçişleri | ❌ |
| Formant ve vibrato gerçek vokalde | ❌ |
| Click/pop/thump testleri | ⚠️ ölçüm var, dinleme yok; REALTIME geçişi ✅ (EngineTest 12) |
| CPU ve bellek stresi | ⚠️ CPU ve 10 dk ✓; sızıntı ❌ |
| Ses iş parçacığı güvenliği | ⚠️ inceleme ✓; NaN / Inf ✅ (EngineTest 10, 11); TSan ❌ |
| DAW uyumluluk matrisi | ❌ |
| UI çözünürlük ve DPI | ⚠️ yalnızca Linux |
| Çeviriler | ✅ tablo tam; görsel kontrol yalnızca Linux |
| Lisans ve üçüncü taraf bildirimleri | ❌ |
| Installer ve kaldırma | ❌ |
| Kullanıcı belgeleri ve changelog | ⚠️ README var; changelog yok |
| Bilinen sorunlar listesi | ⚠️ README'de kısmen; bu rapor eklendi |

---

## 11. P0 için uygulanabilir görev sırası

**Genel kurallar:**
- Her adım küçük ve geri alınabilir olmalı, ayrı commit'te yapılmalı.
- Her adım şu sırayı izlemeli: başlangıç ölçümü → tek değişiklik → derleme → aynı malzemeyi yeniden render → seviye eşitlenmiş karşılaştırma → regresyon.
- Parametre kimlikleri ve oturum formatı değişmemeli.
- Sıralama gerekçesi: 1–3 küçük ve ölçülebilir, sonraki her şeyin önünü açar. 4 kullanıcıya bağlı, hemen ve paralel başlamalı. 5–8'in kabulü gerçek veri setine bağlı.

### P0-1 Test altyapısını depoya al (1.2, 1.3) — **kısmen yapıldı** (`76f8594`: madde 1 ve 6; 2–5 açık)

**Dosyalar:** `tests/wav_probe.cpp`, `tests/vocal_metrics.py`, yeni `tests/run_vocal_matrix.py`, `tests/engine_test.cpp`, `CMakeLists.txt`.

**İşler:**
1. **WavProbe sağlamlığı:**
   - WAV chunk boyutu ve `fmt` uzunluğu denetimi.
   - Desteklenmeyen format için açık hata.
   - Çıkışı gecikme kadar uzatma; gecikmeyi stdout ve JSON'a yazma.
2. **vocal_metrics bağımsızlığı:** `audio_view` bağımlılığını `tests/` altına taşı ya da yolu parametre yap; gecikmeyi WavProbe çıktısından al.
3. **Yeni metrikler:** Oktav hatası oranı, voiced FP/FN (kuru sesin pYIN'ine göre) ve jitter. Geçiş metrikleri için kısa pencereli ayrı bir tahminci ekle.
4. **Analizörün kendi hatası:** Sentetik bilinen frekanslar tablosu (6.1) teste dönüştürülsün.
5. **Toplu çalıştırma:** `run_vocal_matrix.py` manifest okusun → parametre matrisi → kayıt başına JSON → toplama → önceki baseline'la karşılaştırma ve eşikler.
6. **Ölçüm senaryolarını teste taşı:** Bu denetimin S1–S15 senaryoları ayrı bir `VSTMTune_AuditTest` hedefi olarak eklensin. Bilinen hataları belgelesin; düzeltmeler geldikçe kontrollere dönüşsün.

**Kabul:** Tek komut. Başarısız metrikler dosya, parametre ve zaman damgasıyla raporlanır. Ölçüm aracı hatası belgelenir.

### P0-2 Sayısal sağlamlık: NaN/Inf (1.4, 10.1) — ✅ **yapıldı** (`00bde73`, Bölüm 15.2)

**Dosyalar ve işler:**
1. `Source/PluginProcessor.cpp:49-65` (`readParams`): Her değer `std::isfinite` + aralık kırpma; NaN ise varsayılan.
2. `Source/PresetManager.cpp:74-91` (`applyValues`) ve `Source/PluginProcessor.cpp:118-130` (`setStateInformation`): Sonlu olmayan değerleri reddet → varsayılan.
3. `Source/dsp/TuneEngine.h` (`process` / `analysisHop` / `placeGrains`):
   - Sonlu olmayan giriş örneğini 0 yap.
   - Biquad / algılayıcı durumu bozulursa yalnızca analiz tarafını sıfırla.
   - Yeniden eşitleme koşuluna `!std::isfinite(sNext)` ekle (`:652`).
   - `mixSmooth` / `wetSmooth` sonlu değilse hedefe eşitle (`:192-193`).

**Testler:** engine_test'e S2 ve S15 eşdeğerleri; plugin_test'e NaN oturum ve NaN preset.

**Kabul:**
- Tek NaN/Inf örneğinden sonra ≤ 0,5 s içinde düzeltme < 1 ct.
- Çıkışta sonlu olmayan örnek **0**.
- NaN parametre gelince çıkış sonlu; geçerli değer gelince ≤ 0,1 s içinde normal davranış.
- Mevcut 69 test (37 + 32) geçmeye devam eder; blok bağımsızlığı (S5) korunur.

### P0-3 REALTIME geçişi kesintisiz / tıksız (4.5, 1.4) — ✅ **yapıldı** (`154eddc`, Bölüm 15.3)

**Dosyalar:** `Source/dsp/TuneEngine.h:98-122` (`setRealtime` / `reset`), `Source/PluginProcessor.cpp:92-94`.

**Seçenekler:**
- (a) 5–10 ms çıkış fade-out → yalnızca sentez tamponlarını sıfırla → fade-in; `KeyDetector` ve algılayıcı korunur.
- (b) İki gecikme için tek tampon; okuma noktasını çapraz geçişle kaydır.

**Kabul (S3):**
- Geçişteki en büyük örnek adımı ≤ sabit durumun 1,5 katı.
- Auto key korunur.
- Bildirilen gecikme yeni moda eşit.
- Ses iş parçacığında bellek ayırma yok.

### P0-4 Gerçek vokal veri seti (1.1) — **kullanıcı girdisi gerekli**

**İçerik:**
- ≥ 12 kuru, tam bantlı kayıt (44,1 / 48 / 96 kHz).
- Bas/bariton, tenor, alto, soprano.
- Nefesli, fısıltılı, güçlü ve yumuşak kayıtlar; vibratolu ve vibratosuz.
- Hızlı melodi, uzun nota, glissando, rap ve konuşmaya yakın bölümler.
- Farklı mikrofon ve odalar.

**Kayıt düzeni:** Lisans belgesi; metadata JSON (sr, bit, kanal, aralık, amaç); SHA-256 manifest; depo dışında ya da Git LFS'te. Testlerde salt-okunur.

**Kabul:** Yol haritası 1.1. Sentetik başarı ticari kanıt sayılmaz.

### P0-5 Algılayıcı: oktav takılması ve sıçramalar (2.1)

**Dosya:** `Source/dsp/PitchDetector.h:119-136` (süreklilik kuralı), `:241-324` (`PitchTracker`).

**Öneri:** Yukarı oktav için eski oktavı ancak belirgin şekilde daha iyiyse tut (`cmnd[alt] < cmnd[best] − δ`) ya da 2–3 adım tutarlılık ara. Harmonik / enerji kontrolü ekle. Mevcut kısa yanlış ölçüm (spike) reddi korunmalı.

**Testler:**
- S1 senaryoları (tenor / bas / soprano legato oktav ve beşli, iki ses aralığı).
- Gerçek setteki sıçramalar.
- Mevcut 8c testi.

**Kabul:**
- Sıçramadan sonra ≤ 30 ms içinde doğru oktav.
- Çıkış alt oktav enerjisi kuru sese göre ≤ +6 dB.
- Gerçek sette oktav hatası oranı raporlanır ve önceki sürümden kötü değildir.

### P0-6 Güven ve voiced/unvoiced (2.2, 2.3)

**Dosyalar:** `PitchDetector.h`, `TuneEngine.h` (`DisplayPoint`), `PitchCorrector.h`.

**İşler:**
- Clarity × RMS × periyot kararlılığından yumuşatılmış bir güven değeri.
- Güven düşükken düzeltme miktarını yumuşat; sert yanlış notaya çekme olmasın.
- Mutlak −56 dBFS kapısı yerine göreli eşik değerlendirilsin.

**Kabul:** Gerçek sette voiced FP/FN ile LF patlaması ve zarf sıçraması raporu; ünsüz geçişlerinde artış yok; dinleme paketi.

### P0-7 ROBOTISM / retune kalibrasyonu (3.1–3.5)

**Dosya:** `Source/dsp/PitchCorrector.h:75-172`.

**İşler:**
- 6.3'teki 9 noktalı tablo gerçek vokallerle tekrarlansın.
- %0–10 ölü bölge yeniden eşlensin.
- Her nokta için render + **insan dinleme notu (USER-REPORTED)** saklansın.
- Vibrato Scale ile Amount etkileşimi belgelensin.

**Kabul:** Monoton tablo, 0,5 çevresinde süreklilik, kayıtlı dinleme notları.

### P0-8 Seviye ve artefakt (4.1–4.4)

**Dosya:** `Source/dsp/TuneEngine.h:518-546` (tanecik kazancı).

**İşler:**
- Büyük transpoze ve formant'ta seviyeyi ölçerek dengele.
- Tepe ve klip riskini raporla; limiter eklenecekse ayrı ve kapatılabilir olsun.
- MIX eğrisi dinleme ile karara bağlansın.
- Büyük transpozedeki 20–150 Hz enerji (README'deki bilinen sınır) gerçek sette ölçülsün.

**Kabul:**
- ±12 st ve ±12 formant'ta |seviye farkı| ≤ 1 dB (sentetik + gerçek).
- LF metrikleri baseline'dan kötü değil.
- Dinleme paketi değerlendirilmiş.

**P0 kapanışında (1.4 kabulü için):**
- pluginval (izinle) ve macOS CI işinin çalışır hale getirilmesi.
- En az bir DAW'da (ör. REAPER) duman testi: PASS / FAIL tablosu.

---

## 12. Dinleme / insan değerlendirmesi durumu

**Bu denetimde dinleme yapılmadı.**
- **LISTENED-MODEL:** Kullanılmadı. Kalibrasyona göre model metalik ses, gümleme ve autotune karakteri için kanıt vermiyor.
- **USER-REPORTED:** Yok.

**Hazırlanan kör A/B paketleri:** `artifacts/audio-runs/2026-11-17_vstm-tune-audit/packs/`. Her pakette seviye eşitlenmiş, gecikmeye göre hizalı `blind_A.wav` / `blind_B.wav`, gizli anahtar ve dinleme formu var.
1. `soprano_octave_leap_hard/`: Sentetik soprano, hard. A5 notasında (1–2 s) alt oktav, pürüz ya da "hırıltı" duyuluyor mu? (R6)
2. `vocal0_hard_baseline/`: RAVDESS vocal0, kuru ve hard ayar karşılaştırması (başlangıç kaydı).

Önceki oturumların paketlerinde (`artifacts/audio-runs/tune-listening/`) kayıtlı dinleme sonucu yok. Sonuçlar raporlanırsa USER-REPORTED olarak bu belgeye eklenmeli.

---

## 13. Tamamlanma raporu (yol haritası Bölüm 16 şablonu) — bu denetim görevi

| Alan | |
|---|---|
| Görev | Mevcut kodun denetimi; VSTM_TUNE_AUDIT.md (ilk teslim) |
| Değiştirilen dosyalar | Yalnızca **eklendi:** `VSTM_Tune/VSTM_TUNE_AUDIT.md`. Kaynak kodda değişiklik **yok** |
| Sorunun mevcut davranışı | Bölüm 6 ve 9 |
| Yapılan değişiklik | Yok (yalnızca ölçüm ve rapor) |
| Neden bu yöntem | Yol haritası 18: "audit tamamlanmadan geniş kapsamlı refactor yapma" |
| Derleme sonucu | Başarılı, 0 uyarı (Linux / GCC 13 / JUCE 8.0.6) |
| Çalıştırılan testler | EngineTest, PluginTest (+VST3), WavProbe + vocal_metrics (12 render), VSTMOfflineHost (list + 2 render), audit_engine S1–S15, state/preset testi, pYIN doğrulaması |
| Test sonucu | Mevcut testler: 69/69 geçti (37 + 32). Denetim testleri: 2 FAIL (NaN oturum ve NaN preset) + ölçülen riskler (Bölüm 9) |
| Gerçek vokal test sonucu | 6 RAVDESS klibi baseline'ı (5.3); bant sınırlı, dinlenmedi |
| Önceki sürümle fark | Yok (değişiklik yapılmadı); bu rapor baseline'dır |
| CPU / gecikme etkisi | Yok (değişiklik yapılmadı); ölçülen değerler 6.5'te |
| Bilinen sınırlamalar | Sanal makine zaman ölçümleri gürültülü; macOS ve DAW testi yok, Windows yalnızca CI kaydından; dinleme yok; ölçümlerin çoğu sentetik |
| Geri alma noktası | Commit `21ff3f6` (dokunulmadı); bu rapor tek dosya |

---

## 14. Ekler

### 14.1 Yeniden üretme komutları

```bash
# derleme + mevcut testler
cmake -S VSTM_Tune -B VSTM_Tune/build-audit -G Ninja -DCMAKE_BUILD_TYPE=Release -DVSTM_BUILD_TESTS=ON \
      -DVSTM_COPY_AFTER_BUILD=OFF -DVSTM_JUCE_PATH=/home/user/juce-framework/juce
cmake --build VSTM_Tune/build-audit --parallel 4
VSTM_Tune/build-audit/VSTMTune_EngineTest
HOME=$(mktemp -d) xvfb-run -a VSTM_Tune/build-audit/VSTMTune_PluginTest_artefacts/Release/VSTMTune_PluginTest goruntuler \
      --vst3 "VSTM_Tune/build-audit/VSTMTune_artefacts/Release/VST3/VSTM Tune.vst3"

# denetim ölçümleri (depo kökünden). 76f8594'ten beri aynı program depoda: VSTM_Tune/tests/audit_probe.cpp
VSTM_Tune/build-audit/VSTMTune_AuditProbe <klasor> [S1 ... S15]
# denetim anındaki kopyası:
R=artifacts/audio-runs/2026-11-17_vstm-tune-audit
g++ -std=c++17 -O2 -I VSTM_Tune/Source $R/harness/audit_engine.cpp -o $R/harness/audit_engine
$R/harness/audit_engine $R/out            # tüm senaryolar; tek senaryo: $R/harness/audit_engine $R/out S2
(cd $R && python3 analysis/independent_pitch.py)
$R/vocals/run_baseline.sh                  # 6 vokal × 2 ayar
# oturum / preset testi (HOME geçici olmalı: JUCE Linux'ta XDG_CONFIG_HOME'u okumaz)
cmake -S $R/harness/state_test -B /tmp/state_test_build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build /tmp/state_test_build
HOME=$(mktemp -d) xvfb-run -a /tmp/state_test_build/StateAudit_artefacts/Release/StateAudit
```

### 14.2 Üretilen kanıt dosyaları

Hepsi `artifacts/audio-runs/2026-11-17_vstm-tune-audit/` altında, yerel ve git dışı:

| Dosya / klasör | İçerik |
|---|---|
| `environment.txt`, `build.log` | Ortam, commit, derleme çıktısı |
| `engine_test.log`, `plugin_test.log`, `goruntuler/` | Mevcut testlerin çıktıları ve 15 ekran görüntüsü |
| `offlinehost_list.txt`, `host/` | VST3 parametre listesi, VST3 render'ları |
| `vocals/` | 6 vokal × 2 ayar render, CSV, metrik JSON, `run_baseline.sh`, `input_sha256.txt`, vocal5 perde grafiği |
| `harness/audit_engine.cpp`, `out/measurements.jsonl`, `out/*.wav, *.csv` | S1–S15 ölçüm programı, sonuçları ve sesleri |
| `harness/state_test/`, `state_preset_test.log` | Oturum / preset sağlamlık testi |
| `analysis/independent_pitch.{py,json}` | pYIN doğrulaması, oktav ve alt oktav analizi |
| `packs/` | İnsan kör A/B dinleme paketleri |

---

## 15. Uygulanan P0 düzeltmeleri (17.11.2026, denetimden sonra)

Kullanıcı onayıyla (“Herşey için iznim var”) yapıldı. Her adım ayrı commit; önce test yazıldı ve düzeltmesiz kodda **kaldığı** kaydedildi, sonra düzeltme uygulandı. Kanıt dosyaları: `artifacts/audio-runs/2026-11-17_p0-fixes/` (yerel, git dışı).

### 15.1 Ölçüm aracı depoda + WavProbe sağlamlığı (P0-1, kısmi) — `76f8594`

| Alan | |
|---|---|
| Görev | Denetim ölçümlerini depoya almak; WavProbe'un WAV ayrıştırma açıklarını kapatmak |
| Değiştirilen dosyalar | `tests/audit_probe.cpp` (yeni, hedef `VSTMTune_AuditProbe`), `tests/wav_probe.cpp`, `CMakeLists.txt`, `README.md` |
| Sorunun mevcut davranışı | Ölçüm programı depo dışındaydı. WavProbe: kısa `fmt` chunk'ta **dosya sınırı dışına okuma** (ASan: heap-buffer-overflow), 1–7 bit PCM'de **sıfıra bölme** (UBSan), 0 Hz dosyayı "0 Hz'de render" ediyordu; çıkışın son 20/40 ms'si kesiliyordu (MEASURED) |
| Yapılan değişiklik | AuditProbe hedefi (S1–S15, JSONL + WAV). WavProbe: chunk / `fmt` uzunluğu, 8–768 kHz, yalnız PCM 16/24/32 ve float32 kabul; çıkışın sonuna gecikme kadar sessizlik (flush) |
| Neden bu yöntem | Test araçlarında davranışı değiştirmeyen en küçük düzeltme; ölçümler tek komutla yeniden üretilebilir |
| Derleme sonucu | Başarılı, proje kaynaklarında 0 uyarı |
| Çalıştırılan testler | AuditProbe S2/S3/S5/S15; WavProbe 6 vokal × 2 ayar; ASan + UBSan altında 3 bozuk WAV (eski / yeni) |
| Test sonucu | AuditProbe değerleri denetimle birebir. 12 render önceki sürümle **bit düzeyinde aynı** (+960 örnek kuyruk). Bozuk dosyaların üçü de temiz hata ile reddediliyor (eskisi: taşma / sıfıra bölme / anlamsız render) |
| Gerçek vokal test sonucu | 6 RAVDESS × 2 ayar: fark 0 |
| Önceki sürümle fark | Yalnızca WavProbe çıkışı gecikme kadar uzun (girişin sonu artık kesilmiyor) |
| CPU / latency etkisi | Yok (test araçları) |
| Bilinen sınırlamalar | P0-1'in 2–5. maddeleri açık: `vocal_metrics.py` bağımsızlığı ve gecikmeyi WavProbe'dan alması, oktav hatası / voiced FP-FN / jitter metrikleri, `run_vocal_matrix.py` |
| Geri alma noktası | `git revert 76f8594` (öncesi `8124b64`) |

### 15.2 NaN / Inf koruması (P0-2) — `00bde73`

| Alan | |
|---|---|
| Görev | R1 ve R2: bozuk giriş ya da bozuk değer eklentiyi kalıcı olarak bozmasın |
| Değiştirilen dosyalar | `Source/dsp/TuneEngine.h`, `Source/PluginProcessor.h/.cpp`, `Source/PresetManager.h/.cpp`, `tests/engine_test.cpp`, `tests/plugin_test.cpp`, `README.md` |
| Sorunun mevcut davranışı | Tek NaN/Inf giriş örneği → düzeltme motor sıfırlanana kadar yok, 5 NaN çıkış örneği. 0,5 s NaN parametre → `amount`, `formant`, `transpose`, `retune`, `robotism`: **kalıcı sessizlik**; `mix`: **kalıcı NaN**. Oturum / preset'teki `nan` → NaN ses (MEASURED) |
| Yapılan değişiklik | **Motor:** NaN/Inf giriş örneği 0, ±1e4 (+80 dBFS) dışı sınırlanır; `setParams` sayı olmayan değerde son geçerli değeri tutar, değerleri güvenli aralıklara kırpar; tanecik zamanlayıcısının yeniden eşitlenmesi NaN'ı da yakalar; MIX / bypass yumuşatması takılmaz. **Processor:** canlı okumada sayı olmayan değer → parametrenin varsayılanı (key/scale/range aralığa kırpılır); oturum yüklenince NaN/Inf parametreler varsayılana döner; `uiScale` NaN ise 0,75. **Presetler:** dosyadaki NaN/Inf değer atlanır (varsayılan). Testler için `PresetManager::setUserPresetFolderOverride` |
| Neden bu yöntem | Üç bağımsız katman: kaynakta (oturum / preset) değer düzelir, okumada varsayılan kullanılır, motor her durumda son geçerli değerde kalır. Normal yolda yalnızca birer karşılaştırma eklenir; parametre kimlikleri ve oturum formatı değişmedi |
| Derleme sonucu | Başarılı, 0 uyarı (ilk denemedeki bir `-Wshadow` uyarısı giderildi) |
| Çalıştırılan testler | EngineTest (+2: NaN / Inf / 1e30 giriş; 6 parametrede 0,5 s NaN), PluginTest (+11: bozuk ve eski oturum, bozuk preset dosyaları, kullanıcı preseti kaydet / üzerine yaz / yükle / sil), AuditProbe S1–S15, WavProbe 12 render, VST3 2 render, CPU A/B |
| Test sonucu | **Düzeltmeden önce:** EngineTest 2 FAIL, PluginTest 4 FAIL. **Sonra:** EngineTest 39/39, PluginTest 43/43 (VST3 ve ekran görüntüleri dahil). S2: non-finite çıkış 5 → 0, düzeltme 563/563 adımda sürüyor; S15: dört parametrede de çıkış sonlu, seviye 0,0 dB, perde 0,00 ct |
| Gerçek vokal test sonucu | 6 RAVDESS × 2 ayar ve 2 VST3 render: **bit düzeyinde aynı** |
| Önceki sürümle fark | Normal girişte yok. AuditProbe'da S2 ve S15 dışındaki 42 ölçüm birebir aynı |
| CPU / latency etkisi | Latency değişmedi. CPU: aynı derleme bayraklarıyla, yapılandırma başına 3 koşu medyanı −0,08 puan; aynı programın koşudan koşuya oynaması 0,54 puan → **ölçülebilir fark yok** (sanal makine) |
| Bilinen sınırlamalar | +80 dBFS üstü giriş sınırlanır (gerçek seste oluşmaz). Motor kırpma aralıkları plugin aralıklarını kapsar; yalnızca WavProbe gibi doğrudan kullanımda formant/transpoze ±24 st, Vibrato Scale 0–4 ile sınırlı |
| Geri alma noktası | `git revert 00bde73` (öncesi `76f8594`) |

### 15.3 REALTIME geçişi kesintisiz ve tıksız (P0-3) — `154eddc`

| Alan | |
|---|---|
| Görev | R5: çalarken REALTIME ↔ HQ değişince ses kesilmesin, tık olmasın, bulunan ton kaybolmasın |
| Değiştirilen dosyalar | `Source/dsp/TuneEngine.h`, `tests/engine_test.cpp`, `README.md` |
| Sorunun mevcut davranışı | Motor tamamen sıfırlanıyordu: **40 ms tam sessizlik** (1920 örnek @ 48 kHz), geçişte örnek adımı 0,155 (sabit durumda 0,029), Auto key siliniyordu; ses iş parçacığında ~2 MB tampon temizleniyordu (MEASURED) |
| Yapılan değişiklik | `setRealtime` sıfırlamıyor: çıkış 5 ms kısılır, tam sessiz örnekte sentez yeni gecikmenin zaman çizgisine taşınır (yalnızca eski çizginin ilerisi temizlenir) ve 5 ms'de geri açılır. Giriş geçmişi, perde analizi, işaretler ve ton korunur. Geçiş sürerken yeniden değişirse ulaşılan kazançtan devam eder |
| Neden bu yöntem | En küçük ve güvenli değişiklik. İki zaman çizgisini aynı anda sentezleyip çapraz geçmek daha karmaşık ve CPU'da pahalı olurdu; gecikme değişimi içerikte zaten bir sıçrama demek, kısa kısma bunu tıksız yapıyor |
| Derleme sonucu | Başarılı, 0 uyarı |
| Çalıştırılan testler | EngineTest (+2: geçiş ve "bulunan ton korunur"), PluginTest (43, VST3 dahil), AuditProbe S1–S15, WavProbe 12 render, VST3 2 render |
| Test sonucu | **Önce:** 2 FAIL (1920 örnek sıfır, adım 0,155 / 0,029, ton bir süre kayıp). **Sonra:** EngineTest 41/41; en uzun sıfır dizisi **1 örnek**, geçişte adım **0,0293** (sabit 0,0294), yeni gecikme yürürlükte (1920 / 960), perde 0,00 ct, ton korunuyor. S3: 40 ms → 0,02 ms |
| Gerçek vokal test sonucu | Geçiş olmayan 12 render ve 2 VST3 render bit düzeyinde aynı |
| Önceki sürümle fark | Yalnızca geçiş anında. İçerik gecikme farkı kadar kayar (RT→HQ'da 20 ms tekrar, HQ→RT'de 20 ms atlanır); bu, 10 ms'lik V biçimli kısmanın içinde kalıyor |
| CPU / latency etkisi | Geçiş yokken her örnekte tek bir dal; geçişte sınırlı temizlik (48 kHz'de ~9 400 konum). Bildirilen gecikme parametre değişince hemen, motordaki geçiş 5 ms sonra |
| Bilinen sınırlamalar | Geçiş anındaki 20 ms tekrar / atlama **dinlenmedi**; DAW'da (PDC yeniden hesaplama) denenmedi |
| Geri alma noktası | `git revert 154eddc` (öncesi `00bde73`) |

### 15.4 Engellenen ve açık işler

- **pluginval: ENGELLİ.**
  - Önceki oturumdan kalan `pluginval.zip`, resmi v1.0.4 Linux sürümüyle aynı (SHA-256 `c01c49d8…b55352`; GitHub'dan indirilip karşılaştırıldı).
  - Bu ikiliyi çalıştırma isteği otomatik izin denetimi tarafından reddedildi. Aşmaya çalışılmadı.
  - Çalıştırmak için kullanıcının bir izin kuralı eklemesi ya da kendi makinesinde `pluginval --strictness-level 10 --validate "VSTM Tune.vst3"` komutunu çalıştırması gerekiyor (bayraklar `pluginval --help` ile doğrulanmalı).
- **macOS CI:** İş hâlâ runner almıyor (Bölüm 0, madde 4).
- **Kullanıcı girdisi gerektirenler:** gerçek vokal veri seti (P0-4) ve insan dinlemesi. P0-5…P0-8'in kabulü bunlara bağlı.
- **P0-1'in kalan maddeleri:** Bölüm 15.1'de listelendi.

### 15.5 Güncel test durumu

| | Denetim (`21ff3f6`) | Şimdi (`154eddc`) |
|---|---|---|
| EngineTest | 37/37 | **41/41** |
| PluginTest (`--vst3`, ekran görüntüleri) | 32/32 | **43/43** |
| AuditProbe S2 (NaN/Inf giriş) | düzeltme kalıcı kayıp | düzeltme sürüyor, non-finite 0 |
| AuditProbe S3 (REALTIME geçişi) | 40 ms sessizlik, adım ×5 | 0,02 ms, adım ≤ sabit durum |
| AuditProbe S15 (NaN parametre) | kalıcı sessizlik / NaN | normal |
| Normal girişte çıktı | – | bit düzeyinde aynı |
