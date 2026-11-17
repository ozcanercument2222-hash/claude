# VSTM Tune – gerçek zamanlı vokal autotune

Tek düğmeyle **robotik ↔ insansı** arasında gezinen, gerçek zamanlı perde düzeltme plugin'i.
Formatlar: **VST3**, **AU** (yalnız macOS), **Standalone**. Mono ve stereo.

![VSTM Tune](docs/vstm_tune.png)

## ROBOTISM düğmesi

| Yön | Ne olur |
|---|---|
| ◀ **Robotik** (Daha Yapay) | Düzeltme anında olur (retune → 0), notalar basamak gibi oturur, vibrato dümdüz olur, nota değişim eşiği küçülür. Klasik "robotik autotune" efekti. |
| **Ortada** (Dengeli) | RETUNE SPEED, HUMANIZE ve PITCH CORRECTION ayarları olduğu gibi uygulanır. |
| ▶ **İnsansı** (Daha Doğal) | Retune 4 katına kadar yavaşlar, notalar arası geçiş **S-eğrisiyle kayar** (en fazla 110 ms), cümle başındaki doğal "kayış" serbest kalır, uzun notalarda humanize artar. Düzeltme duyulmaz, ses insan gibi kalır. |

Düğmenin sol yayı (mor) robotik, sağ yayı (beyaz) insansı bölgedir; işaretçi hangi tarafa dönükse o taraf parlar.
Üzerine gelince altında değer yazar: `Robotik 40%`, `Dengeli`, `İnsansı 60%`. "i" düğmesi açıklamayı gösterir.

![Robotik ve İnsansı](docs/robotism_states.png)

Testle ölçülen davranış (48 kHz, sentetik vokal, A3 → C4 sıçraması, ±50 cent vibrato):

| | Robotik (0) | İnsansı (100) |
|---|---|---|
| Nota geçiş süresi (%10 → %90) | 24 ms (basamak) | ~250 ms (kayarak) |
| Vibrato (tepe–tepe) | 1 cent (dümdüz) | 98 cent (korunur) |

## Kontroller

| Kontrol | Açıklama |
|---|---|
| **KEY** | Tonik. **Auto**: şarkıyı dinleyip tonu kendisi bulur (bulduğu ton kutunun içinde `Auto · A` gibi görünür). İlk ~1 saniye, ton bulunana kadar en yakın yarım sese düzeltir. |
| **SCALE** | İzinli notalar: Chromatic, Major, Minor, Harmonic / Melodic Minor, Major / Minor Pentatonic, Blues, Dorian, Phrygian, Lydian, Mixolydian, **Hicaz**. |
| **PITCH CORRECTION** | Hedef notaya olan mesafenin ne kadarı düzeltilsin (0–100%). |
| **RETUNE SPEED** | Perdenin notaya çekilme hızı (0–200 ms). 0 ms = anında. |
| **HUMANIZE** | Uzun, tutulan notalarda düzeltmeyi gevşetir; kısa notalar sıkı kalır. |
| **SCALE** (alt sıra) | Doğal vibrato ve perde hareketinin derinliği (0–200%, 100% = söylendiği gibi). |
| **FORMANT** | Sesin rengini (gırtlak boyu) perdeyi değiştirmeden kaydırır (±12, yarım ses). |
| **PITCH RANGE** | Düzeltilmiş sesi ±12 yarım ses transpoze eder. |
| **MIX** | Orijinal ↔ düzeltilmiş ses karışımı. |
| **AUTOTUNE ON** | Aç / kapat. DAW'ın bypass düğmesiyle senkron; kapalıyken ses gecikmesi korunur (takır yok). |
| **REALTIME** | Açık: düşük gecikme (**20 ms**, kayıt / canlı için). Kapalı: yüksek kalite (**40 ms**, çok kalın sesler için). DAW gecikmeyi otomatik telafi eder. |
| 〰 düğmesi | Perde ekranında ham giriş perdesini de gösterir. |

**Perde ekranı:** dikey çizgi yazma kafasıdır, soldan sağa tarar (6 saniye). Mor eğri düzeltilmiş çıkış,
noktalar nota geçişleri. Kafadaki etiket o an söylenen notayı ve girişin o notadan sapmasını gösterir (`A4 −16 ct`).
Ses yokken ekran durur (gereksiz CPU yok).

**Üst bar:** preset seçici (`<` `>` veya isme tıkla: 14 fabrika preseti + kendi presetlerin; değiştirince isim yanında nokta çıkar),
⤓ preset kaydet, ▢ pencere boyutu (768 × 512 … 1536 × 1024, köşeden de sürüklenir), ⚙ ayarlar (vokal aralığı:
Soprano / Alto, Tenor, Bas / Bariton; tüm parametreleri sıfırla; hakkında).

**Dil:** arayüz, bilgisayarın dilinde açılır: Türkçe, İngilizce, Almanca, Fransızca, İtalyanca, Rusça veya Çince.
Bunlardan biri değilse İngilizce. Düğme adları, değer yazıları, menüler, ipuçları ve DAW'da görünen parametre adları çevrilir;
preset ve nota adları (C, D, E…) değişmez. Kayıtlı projeler, otomasyonlar ve presetler dilden bağımsızdır (parametre kimlikleri aynı).
Dili zorlamak için ortam değişkeni: `VSTM_TUNE_LANG=de` (en, tr, de, fr, it, ru, zh).

**Fare:** düğmelerde dikey sürükle, **Shift** = ince ayar, **çift tık** = varsayılan, tekerlek, **değere tıkla** = yazarak gir,
**sağ tık** = DAW'ın parametre menüsü (otomasyon, MIDI learn).

Kullanıcı presetleri: `<kullanıcı veri klasörü>/VSTM Audio/VSTM Tune/Presets/*.vstmpreset`
(Windows: `%APPDATA%`, macOS: `~/Library`, Linux: `~/.config`).

## Nasıl çalışır

```
giriş ─► alçak geçiren + seyreltme (~11 kHz) ─► YIN perde algılayıcı (tam hızda alt-örnek ince ayar)
                                                   │
                         otomatik ton bulucu ◄─────┤
                                                   ▼
                                       düzeltme mantığı (key/scale, retune, humanize, ROBOTISM)
                                                   │
perde işaretleri (her periyotta bir, dalga benzerliğiyle hizalı)
            └──────────────────────────► TD-PSOLA sentez (formant korumalı, istenirse formant kaydırma)
                                                   │
giriş (gecikmeli, "dry") ─────────────────────► MIX / bypass ─► çıkış
```

- Düzeltme sesin kendi periyoduna göre yapılır: hard-tune'da çıkış hedef notada **0.05 cent'ten** daha az hata verir.
- **Nota seçimi titremez:** yeni nota ancak histerezisi (15 cent, İnsansı'da 35 cent) geçip kısa bir süre kararlı kalınca
  seçilir (Robotik 14 ms, Dengeli 22 ms, İnsansı 52 ms); sesin yeni notaya açıkça vardığı sıçramalarda (≥ 1.5 yarım ses) hemen.
  İki notanın tam ortasında söylenen seste hedefin her 3–10 ms'de bir gidip gelmesi ("trill") böylece önlenir:
  gerçek vokallerde 40 ms'den kısa nota bölümleri 3–10 kat azaldı.
- **Hece başlarında "güm" yok:** ünsüzden ünlüye geçişte (unvoiced → voiced) zamana hizalı unvoiced akış 4 ms daha
  sürer ve perdeye senkron voiced akışla çapraz geçiş yapar. Eskiden iki tanecik türü arka arkaya dikiliyordu;
  ünlünün ilk periyodu %50–70 seviyeye düşüyor, yarım kalan glottal darbe alçak frekanslı bir tık yapıyordu.
  Ölçüm (6 gerçek vokal, 540 geçiş): bu geçişlerdeki tık medyanı düzeltme modlarında 12–19 dB düştü,
  −45 dBFS'i aşan tıklar yarıya indi (40 → 22).
- Düzeltme 0 iken plugin şeffaftır (çıkış = giriş, 61 dB SNR) ve dry sinyalle örneğine kadar hizalıdır; MIX'te faz kayması olmaz.
- Motor blok boyutundan bağımsızdır (64 veya 1013 örneklik bloklar bit-bit aynı çıkışı verir), ses iş parçacığında kilit / bellek ayırma yoktur.
- Arayüz 1536 × 1024'lük vektörel bir tuval üzerinde çizilir ve ölçeklenir (her boyutta ve HiDPI'da keskin);
  sabit katmanlar ekranın gerçek piksel ölçeğinde bir kez çizilip saklanır.

## Derleme

Gerekenler: **CMake 3.22+** ve bir C++17 derleyici (Linux bağımlılıkları için kök README'ye bak).

```bash
cd VSTM_Tune
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j8
```

İlk derlemede JUCE 8.0.6 otomatik indirilir. Elinde JUCE varsa: `-DVSTM_JUCE_PATH=/yol/JUCE`.
Çıktılar `build/VSTMTune_artefacts/Release/` altında (`VST3/VSTM Tune.vst3`, `AU/VSTM Tune.component`, `Standalone/`).

**Hazır paketler:** GitHub → Actions → **"VSTM Tune derle"** → en üstteki çalışma → sayfanın altındaki *Artifacts*
(`VSTM-Tune-Windows`, `VSTM-Tune-macOS`, `VSTM-Tune-Linux`; kurulum talimatı paketin içindeki `KURULUM.txt`'de).

## Testler

```bash
cmake -B build -DVSTM_BUILD_TESTS=ON
cmake --build build --config Release
./build/VSTMTune_EngineTest                                  # ses motoru (44.1 / 48 / 96 kHz)
./build/VSTMTune_PluginTest_artefacts/Release/VSTMTune_PluginTest goruntuler --vst3 "build/VSTMTune_artefacts/Release/VST3/VSTM Tune.vst3"
```

- **EngineTest:** hard-tune doğruluğu, seviye, şeffaflık + gecikme hizası, ±12 / +7 transpoze, formant, blok boyutu
  bağımsızlığı, ünlü başlangıcında genlik çukuru olmaması, Robotik / İnsansı geçiş süresi ve vibrato, iki nota
  arasında nota titremesi olmaması, otomatik ton (C majör), sessizlik.
- **WavProbe + vocal_metrics.py:** gerçek bir kaydı motordan geçirir, her analiz adımını (voiced, perde, hedef nota)
  CSV'ye yazar ve ölçer: nota titremesi, çıkış–hedef sapması, cent dağılımı, 20–150 Hz patlamaları, seviye.
  ```bash
  ./build/VSTMTune_WavProbe vokal.wav cikis.wav --csv adimlar.csv key=-1 robotism=0 retune=0
  python3 tests/vocal_metrics.py vokal.wav cikis.wav --csv adimlar.csv
  ```
  Çıkış, motor gecikmesini korur ve sonuna gecikme kadar sessizlik eklenir (girişin sonu kesilmez).
  Yalnız PCM 16 / 24 / 32 bit ve 32-bit float WAV okunur; bozuk veya kısa chunk'lı dosyalar reddedilir.
- **AuditProbe:** `VSTM_TUNE_AUDIT.md` bölüm 6'daki ölçümleri (S1–S15) sentetik sinyallerle yeniden üretir:
  büyük sıçramalar, NaN / Inf, REALTIME geçişi, örnekleme hızları, blok boyutları, stereo, otomasyon stresi,
  CPU, 10 dakika, unvoiced, Auto key, formant / transpoze, MIX, ROBOTISM taraması. Karar vermez, ölçer:
  ```bash
  ./build/VSTMTune_AuditProbe olcumler            # hepsi;  tek senaryo: ... olcumler S2 S3
  ```
- **PluginTest:** parametre varsayılanları ve yazıları, 7 dilin eksiksizliği ve diller arası değer okuma, gecikme bildirimi (20 / 40 ms), bypass, oturum kaydet / geri yükle,
  presetler, editör; `goruntuler/` klasörüne ekran görüntüleri (her dil için `07_lang_xx.png`); `--vst3` ile derlenmiş VST3'ü bir DAW gibi yükleyip sınar.

## Bilinen sınırlar

- Plugin imzasızdır: Windows "bilinmeyen yayıncı", macOS "doğrulanamadı" uyarısı verebilir (`KURULUM.txt`).
- Bu ortamda gerçek bir DAW içinde denenmedi; derlenmiş VST3 JUCE'un VST3 host'uyla yüklenip test edildi.
  DAW'da kontrol edilecekler: Windows HiDPI ölçekleme, Ableton / FL Studio / Logic'te pencere boyutlandırma,
  otomasyon yazıları, aynı plugin'den iki pencere.
- Monofonik vokal içindir (akor / koro düzeltmez). Çok kalın bas seslerde REALTIME'ı kapat veya ⚙ → Vocal range → Bass.
- **Büyük transpozede düşük seviyeli alçak frekans:** düzeltme modlarında (Robotik / Dengeli / İnsansı) 20–150 Hz'de
  kalan enerji ≈ −47 dBFS'tir; geniş transpozede TD-PSOLA geçici bölgelerde hâlâ alçak frekanslı enerji üretir
  (−3 st ≈ −42 dBFS, −12 st ≈ −30 dBFS tepe). Düzeltme kapalıyken yoktur. Ölçülerek doğrulandı; insan kulağıyla
  değerlendirilmedi.

## Lisanslar

Arayüz yazı tipi **Inter** (SIL Open Font License 1.1) — `Resources/fonts/OFL-Inter.txt`.
