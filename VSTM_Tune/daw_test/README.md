# VSTM Tune – DAW testi (REAPER, FL Studio)

Bu klasör, eklentiyi **gerçek bir DAW içinde** sınamak için hazırlanmış kittir. Bu depo bir Linux bulut
ortamında geliştiriliyor; orada REAPER ve FL Studio yok (FL Studio'nun Linux sürümü hiç yok, REAPER indirme
sitesi ağ politikasınca kapalı). Bu yüzden testi sen kendi bilgisayarında çalıştırırsın, render'ları ve
tabloyu geri gönderirsin; ölçümü `analyze_daw_renders.py` yapar.

| Dosya | Ne işe yarar |
|---|---|
| `make_test_signals.py` | 6 test sinyali (sentetik vokal; kimsenin kaydı değil) + `manifest.json` |
| `reaper/vstm_tune_daw_test.lua` | REAPER'da test projesini kurar, otomasyonu çizer, her parçayı ayrı WAV olarak render eder. **REAPER'da çalıştırılmadı** (bkz. aşağı) |
| `analyze_daw_renders.py` | DAW render'larını ölçer: perde, seviye, gecikme telafisi (PDC), tık, oktav, stereo, yeniden açma / freeze eşitliği → PASS / FAIL tablosu |

> Ölçüm aracı, DAW yerine motorun kendi render'larıyla doğrulandı: gecikmesi telafi edilmiş render'larda
> 16/16 PASS, telafi edilmemişte PDC satırları 960 / 1920 örnek kayma ile FAIL (MEASURED). Bu, aracın
> doğru çalıştığını gösterir; **hiçbir DAW sonucu değildir.**

## 1. Hazırlık

1. Eklentiyi kur: GitHub → Actions → **VSTM Tune derle** → en son çalıştırma → Artifacts.
   Windows: `VSTM-Tune-Windows` → `VSTM Tune.vst3` klasörünü `C:\Program Files\Common Files\VST3` içine kopyala.
   macOS: `VSTM-Tune-macOS` → `VSTM Tune Kurulum.pkg`'ye çift tıkla (yanındaki `KURULUM.txt`'ye bak).
2. Sinyalleri üret (Python 3 + `numpy scipy soundfile`):
   ```
   python3 make_test_signals.py sinyaller            # 48 kHz
   python3 make_test_signals.py sinyaller44 --rate 44100
   ```
   Python yoksa hazır zip'teki `sinyaller/` klasörünü kullan.
3. DAW'ın proje örnekleme hızını **48 kHz** yap (44,1 kHz testi için `sinyaller44`).

## 2. Render'lar (iki DAW'da da aynı adlar)

Her render projenin **başından (0 s)** başlamalı, sinyalle aynı örnekleme hızında, WAV (24-bit ya da 32-bit float).
Ayarların hepsi eklentinin kendi kontrolleriyle; "hard" = KEY **C**, SCALE **Chromatic**, ROBOTISM **0** (Robotik %100), RETUNE **0 ms**, HUMANIZE **0 %**.

| Render | Kaynak | Eklenti ayarı | Ne bekleniyor |
|---|---|---|---|
| `R01_hard_t1` | `t1_a3_plus40.wav` | hard | 220,00 Hz (±2 ct), seviye ±1 dB |
| `R02_pdc_realtime_t3` | `t3_pdc_clicks.wav` | varsayılan (REALTIME açık) | kuru dosyayla ±1 örnek hizalı (PDC) |
| `R03_pdc_hq_t3` | `t3_pdc_clicks.wav` | REALTIME kapalı | ±1 örnek hizalı (40 ms gecikme telafi edilmeli) |
| `R04_transpose+12_t5` | `t5_level.wav` | hard + PITCH RANGE +12 | 440 Hz, seviye ±1 dB |
| `R05_formant-12_t5` | `t5_level.wav` | hard + FORMANT −12 | 220 Hz, seviye ±1 dB |
| `R06_formant+12_t5` | `t5_level.wav` | hard + FORMANT +12 | 220 Hz, seviye ±1 dB |
| `R07_transpose-12_t5` | `t5_level.wav` | hard + PITCH RANGE −12 | 110 Hz, seviye ±1 dB |
| `R08_octave_t4` | `t4_octave_leap.wav` | hard | A5 = 880 Hz, f0/2 en az 50 dB aşağıda |
| `R09_automation_t2` | `t2_melody.wav` | varsayılan + otomasyon: ROBOTISM 50→0 (2 s)→100 (4 s)→50 (6 s); AUTOTUNE kapalı 7–8 s; PITCH RANGE +5 st 9–11 s | NaN yok, en büyük örnek adımı kuru sesin 3 katından az |
| `R10_automation_reopened_t2` | — | projeyi kaydet, DAW'ı kapat, aç, R09'u tekrar render et | R09 ile fark < −60 dB (durum ve otomasyon geri geldi) |
| `R11_buffer64_t1`, `R12_buffer2048_t1` | `t1` | hard; ses kartı tamponu 64 ve 2048 iken **gerçek zamanlı** kaydet (Render ≠ canlı) | R01 ile fark < −60 dB |
| `R13_freeze_t2` | — | R09 parçasını dondur / bounce et | R09 ile fark < −60 dB |
| `R14_stereo_t6` | `t6_stereo_lr.wav` | hard, **stereo** parça | sol 220 Hz, sağ < −90 dBFS |

Sonra:
```
python3 analyze_daw_renders.py sinyaller sinyaller/renders --json sonuc.json
```
FL Studio dosya adlarını "Proje - R01_hard_t1.wav" diye verir: araç adın içindeki R-kodunu bulur, yeniden adlandırma gerekmez.

## 3. REAPER

**Otomatik kısım (R01–R09, R14):** boş proje (48 kHz) → *Actions → Show action list → New action → Load ReaScript*
→ `reaper/vstm_tune_daw_test.lua` → çalıştır → sinyal klasörünü seç. Betik her test için bir parça açar, VSTM Tune'u
ekler, parametreleri ve R09 otomasyonunu kurar, parçaları `renders/` klasörüne render eder.

- **Bu betik REAPER'da denenmedi** (geliştirme ortamında REAPER yok). Bir adım başarısız olursa konsol nedenini yazar;
  aynı ayarları tablodaki gibi elle yap. İlk çalıştırmada konsola eklentinin 27 parametresinin adı/sırası yazılır:
  0 Key, 1 Scale, 2 Pitch Correction, 3 Retune Speed, 4 Humanize, 5 Robotism, 6 Vibrato Scale, 7 Formant,
  8 Pitch Range, 9 Mix, 10 Bypass, 11 Realtime, 12 Vocal Range, 13 Custom Scale, 14–25 Custom Note, 26 Adaptive.
  Sıra farklıysa betik durur.
- Render biçimi bir kez *File → Render* içinde WAV 24-bit (ya da 32-bit float) olarak seçilmiş olmalı.

**Elle (R10–R13):** kaydet → kapat → aç → R09 parçasını tek render et (`R10_…`); *Options → Preferences → Audio → Device*
tampon 64 ve 2048 iken R01 parçasını gerçek zamanlı kaydet (`R11`, `R12`; *Render* değil, yeni bir parçaya kayıt);
R09 parçasını *Freeze* et ve dondurulmuş sesi dışa aktar (`R13`).

## 4. FL Studio

FL Studio betikle kurulamıyor; adımlar elle:
1. Her test için bir **Mixer insert** aç, adını tablodaki render adı yap (ör. `R01_hard_t1`).
2. Sinyal dosyasını Playlist'te o insert'e yönlenen bir ses kanalına bırak (0. saniyeden başlasın).
3. Insert'e **VSTM Tune**'u (VST3) ekle, ayarları tablodaki gibi yap.
4. R09 otomasyonu: ROBOTISM, AUTOTUNE ON ve PITCH RANGE üzerinde sağ tık → *Create automation clip*, noktaları tablodaki zamanlara koy.
5. *File → Export → Wave file*, **Split mixer tracks** işaretli, 48 kHz, 24-bit ya da 32-bit float → `renders/`.
6. R10: projeyi kaydet, FL'i kapat-aç, R09'u yeniden dışa aktar. R13: R09 insert'ini *Render to audio / Consolidate*.
   R11 / R12: *Options → Audio settings* tampon 64 ve 2048 iken R01'i gerçek zamanlı kaydet.
7. FL'in gecikme telafisi: *Options → Mixer → Plugin delay compensation* açık olmalı (R02 / R03 bunu ölçer).

## 5. Elle kontrol tablosu (yol haritası 9)

| Kontrol | REAPER | FL Studio | Not |
|---|---|---|---|
| Tarama ve yükleme (VST3), ad "VSTM Tune / VSTM Audio" | | | |
| Mono parça ve stereo parça (R14) | | | |
| Örnekleme hızı 44,1 → 48 → 96 kHz değişimi (ses ve perde doğru) | | | |
| Tampon boyutu değişimi (R11 / R12) | | | |
| Transport başlat / durdur / döngü, sessizlikte gürültü yok | | | |
| Offline render = gerçek zamanlı çalma | | | |
| Freeze / bounce (R13) | | | |
| Bypass: DAW bypass'ı ↔ AUTOTUNE ON senkron, tık yok | | | |
| Otomasyon yazma ve okuma (R09; 25 parametre otomasyona açık) | | | |
| Preset: kullanıcı preseti kaydet → başka ayar → geri yükle | | | |
| Gecikme / PDC (R02, R03); çalarken REALTIME değişimi | | | |
| Editör aç / kapat / boyutlandır, HiDPI %125 / %150 / %200 | | | |
| SCALE menüsü → "Custom scale…" klavye penceresi SCALE kutusunun yanında açılıyor, notalar tıklanıyor | | | |
| ADAPTIVE anahtarı ve KEY kutusunda "Auto · C" / "Auto · C?" gösterimi | | | |
| Aynı projede birden fazla kopya (bağımsız ayarlar) | | | |
| Projeyi yeniden açma (R10) | | | |
| DAW kapanırken / eklenti silinirken çökme yok | | | |

Doldurduğun tabloyu, `sonuc.json`'u ve varsa dinleme notlarını (USER-REPORTED) geri gönder; ölçümleri raporun DAW bölümüne
işleyeyim. Test edilmeyen DAW için "uyumlu" denmez.
