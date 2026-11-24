# VSTM Tune 1.2.0 – Değişiklik günlüğü (1.1.0 → 1.2.0)

Tarih: 19.11.2026. Kalite / güvenilirlik sürümü: **yeni özellik, yeni parametre, yeni arayüz öğesi yok.**
Kanıt etiketleri: **MEASURED** (ölçüm), **INSPECTED** (kod incelemesi). **Hiçbir ses dinlenmedi** (USER-REPORTED yok,
LISTENED-MODEL kullanılmadı). Ayrıntılı sonuçlar: `VSTM_Tune_1.2.0_Quality_Report.md`.

Geri dönüş noktası: git etiketi `vstm-tune-1.1.0-baseline` (`193d2f3`). Adım adım yamalar ve kaynak anlık görüntüleri:
`artifacts/audio-runs/2026-11-19_q12/patches/` ve `snapshots/` (git dışında; bkz. `VSTM_Tune_1.2.0_Rollback.md`).

## DSP değişiklikleri (sırayla, her biri ayrı adım)

| # | Değişiklik | Dosya | Neden (ölçülen kök neden) | Etki (MEASURED) |
|---|---|---|---|---|
| 2 | **İnce ayar YIN çerçevesinin ortasında ölçülüyor** | `dsp/PitchDetector.h` (`refine`) | Tam hız ince ayarı en yeni ~6 ms'yi ölçüyor, tahmin ise YIN çerçevesinin ortasına (~19–26 ms önce) damgalanıyordu: vibrato ve nota geçişlerinde perde ~17 ms "erken" kaydediliyordu. 1.1'deki kaba hata artışının (%0,69 → %1,21) asıl nedeni bu | Kesin doğrulu yeniden sentez (145): kaba hata %1,473 → %0,160, ince hata medyanı 13,1 → 0,39 ct, p95 65 → 3,1 ct, zaman artığı −19 → 0 ms. 1.1'in sentetik bas vibratosunda (±200 ct, 6,5 Hz, "a"): > 1 st hata %19,3 → %0 |
| 3 | **Süren sesin voiced kalması** (perde ≤ 2 yarım ses sürekli, seviye son voiced seviyenin −6 dB'si içinde, en az 8 adımlık kurulmuş ses) | `dsp/PitchDetector.h` | Bas aralığında hızlı geniş vibratoda (±200 ct, 6,5 Hz) YIN çukuru bir çerçeve içinde yayılıyor, her döngünün üçte biri unvoiced düşüyordu | Aynı bas vibratosu (Bass aralığı): voiced %66,2 → %95,0 (son sürüm %94,6). Toplamda (1.1.0 → 1.2.0) kesin doğrulu sette voiced recall %98,99 → %99,04, yanlış voiced %0,022 → %0,031 |
| 4 | **Kısa periyot denetimi (yalnız ≤ 12 seyreltilmiş örnek)** | `dsp/PitchDetector.h` | Aralığın tepesinde (Soprano "i" E6 1319 Hz, "e" D6) YIN 2P / 4P okuyordu (1.0'dan beri) | "i" E6: −23,8 → −0,2 st (medyan); sentetik sette ≥ 15 st hata %0,109 → %0,064. Tenor aralığındaki küçük yan etkileri sonradan bulundu → adım 11 (tüm gecikmelere uygulanan ilk sürüm oktav yukarı hata ürettiği için reddedilmişti) |
| 6 | **REALTIME ↔ HQ geçişinde içerik sürekliliği** | `dsp/TuneEngine.h` | Geçiş 5 ms kısıp içeriği 20 ms atlatıyor / tekrarlatıyordu | Gecikme yükseltilmiş kosinüsle kayıyor (yalnız işlenmiş ses duyuluyorsa 0,4 s, dry duyuluyorsa 2 s); 132 durumda tekrar / atlama 68 → 0, geçişte perde sapması 23,4 → 0,01 ct |
| 7 | **Yeni notaya yakınlıkla varış** (yeni notanın 30 ct içi = bekleme yok) | `dsp/PitchCorrector.h` | Doğru zamanlanmış algılayıcıyla yarım ses adımları bekleme süresi (14–52 ms) kadar geç değişiyordu | Hard tune hızlı koşu: nota değişimi medyanı +16,7 → +6,3 ms (p95 46,7 → 7,1 ms; adım 6'ya göre); nota titremesi testi geçiyor. 1.1.0'a göre hâlâ geç (−1,7 ms → +6,3 ms) |
| 8 | **Seviye dengelemesine tepe sınırı** (eklenen kazanç kaydırılmış sesin ~20 ms tepe penceresini kuru sesin +2 dB'sinin üstüne çıkaramaz; 1'in altına inmez) | `dsp/TuneEngine.h` | 1.1'deki seviye dengelemesi kısa tepeleri de yükseltiyordu | 50 kayıt, transpoze +12: seviye eşitlenmiş gerçek tepe p95 6,39 → 4,02 dB, 10 ms patlama 211 → 81; seviye eşleşmesi değişmedi (≤ 0,02 dB). Sınır yalnız kazancın yükseltildiği yerde çalışır: hard, transpoze −12 ve tr+7 fm+12 ayarlarında 50 kaydın ölçüm değerleri bir önceki adımla aynı (bit düzeyinde karşılaştırılmadı) |
| 9 | **Ton / dizi değişiminde 25 ms kayma** (robotik tarafta da) | `dsp/PitchCorrector.h` | Tutulan notada KEY / SCALE değişimi (ya da Auto Key kilidi) robotik tarafta anlık sıçrama yapıyordu | A → A# %10–90 süresi 4 → 15 ms, tık yok |
| 11 | **Kısa periyot denetimi yalnız Soprano / Alto aralığında** (aralığın üst sınırı 1,2 kHz'in üstündeyse) | `dsp/PitchDetector.h` | Son ölçümde bulundu: Tenor aralığında (üst sınır 1050 Hz; varsayılan aralık) adım 4 hiçbir hatayı ölçülebilir biçimde düzeltmedi, iki tür hata ekledi: kadın seslerinde oktav yukarı okuma (f5 "belt", f5 "dona") ve tutulan alçak notanın içindeki ünsüzde YIN'in zaten yanlış aldığı kısa gecikmeyi daha da yukarı taşıma (m11 "dona": 208 Hz'lik seste 525 → ~1050 Hz; hard tune'da o noktada kuru sesin +9,6 dB üstünde kısa tepe) | Gerçek kayıt referansı (145): ≥ 15 st hata %0,282 → %0,279; yeniden sentez: oktav yukarı %0,073 → %0,071, ≥ 15 st %0,133 → %0,131 (adım 4 öncesi düzeye döndü); sentetik set değişmedi (E6 düzeltmesi korunuyor, 77 kaydın hop kayıtları birebir aynı). Gerçek kayıtların 26 / 145'inde algılayıcı çıktısı değişiyor |

Adım 5 (3'ün düzeltmesi): sürekli voiced kuralı yalnız **en az 8 adım (~22 ms) voiced kalmış** seste uygulanıyor; düzensiz tık
dizisinin 5 adım voiced sayılması önlendi.

Adım 10 DSP değişikliği değildir: sürüm numarası, PluginTest 3e, WavProbe `switch=`, yeni ölçüm araçları ve belgeler. Tablodaki
"Etki" sütunu o adımın kendi ölçümüdür (bir önceki adıma göre); 1.1.0 → 1.2.0 toplam karşılaştırması kalite raporundadır.

**Denenip reddedilenler:** tüm gecikmelere kısa periyot denetimi (oktav yukarı hatalar 14 → 18), seviye kapısız süreklilik
kuralı (gerçek kayıtlarda fazla voiced), tepe sınırında 0 dB pay (seviye −0,3…−0,8 dB düşüyor), 3 ms kazanç inişi (tepe için
zayıf, alçak frekansa faydası yok), perde işaretleri için ayrı "en yeni" periyot (ölçülebilir etkisi yok), adım 11 yerine iki
alternatif: kurulmuş seste önceki periyodun bir oktavdan fazla üstüne çıkmayı yasaklamak (sentetik E6 düzeltmesinin yarısını
kaybetti: ≥ 15 st %3,8 → %5,7) ve bunu son ~46 ms'nin en kısa periyoduna göre yapmak (E6 korundu, ama m11 "dona" olayını
düzeltmedi: hatayı YIN'in kendi yanlış seçimi başlatıyor).

## Varsayılan sesteki değişiklikler (dinlenmeli)

- Algılayıcı artık doğru zamanlandığı için **ROBOTISM %50 ("Dengeli") vibratonun daha çoğunu koruyor** (sentetik S14:
  %73,5 → %96,7). İnsansı yarıda vibrato ~%100, robotik yarıda %1–71 arası. 1.1'deki değerler zaman kaymasının yan etkisiydi.
- Hard tune'da hedef nota, şarkıcının nota değişiminden **medyan 9,2 ms sonra** değişiyor. 1.1'de bu 2,9 ms'ydi. Hızlı koşuda
  1.2 6,3 ms, 1.1 −1,7 ms (sentetik kesin doğru set, 47 geçiş). 1.1'de perde tahmini ~17 ms erken damgalanıyordu ve bu, karar
  bekleme süresini tesadüfen telafi ediyordu. 1.2'de tahmin doğru zamanda; bekleme süresi yakın varış kuralıyla (adım 7) kısaldı
  ama sıfırlanmadı. **Bu ölçüt 1.2'de 1.1'e göre daha kötü** (rapor 4.2).
- Gerçek kayıtlarda seviye ve artefakt metrikleri küçük farklarla değişti (ayrıntı rapor 4.3).

## Test ve ölçüm araçları (yeni, `tests/`)

`gt_corpus.py` (kesin doğrulu perde seti: 145 kaydın yeniden sentezi, 77 parametrik ses, gerçek kayıtlar için kısa çerçeveli
referans), `gt_eval.py`, `gt_variants.py` (örnekleme hızı, kazanç, gürültü, stereo), `peak_check.py`, `rt_switch_probe.cpp` +
`rt_switch_eval.py`, `transition_eval.py`, `autokey_eval.py` (etiketli ton seti), `bench_probe.cpp`. WavProbe: `switch=`
(çalarken mod değiştir), CSV'de `est_delay_s` ve `clarity`. EngineTest 66 → 71 (12b içerik sürekliliği, 13 nota eşleme
24,9 milyon durum, 14 hızlı otomasyon / tık, 15 ton değişiminde kayma), PluginTest 69 → 73 (3e: 1.1 oturumu, deterministik
durum, yinelenen kimlik, ses çalarken oturum geri yükleme).

## Uyumluluk

- Parametre kimlikleri, sırası (27), aralıkları, otomasyon, preset ve oturum biçimi (state sürümü 2) **değişmedi**.
- 1.0 ve 1.1 projeleri açılır (PluginTest 3b / 3d / 3e). Sürüm numarası 1.2.0 (CMake → Hakkında penceresi).
- Gecikme değişmedi: REALTIME 20 ms, HQ 40 ms.
