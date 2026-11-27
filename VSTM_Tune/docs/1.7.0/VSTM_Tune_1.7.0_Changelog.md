# VSTM Tune 1.7.0 (aday, 27.11.2026): ara notalarda oktav hataları ve daha kısa REALTIME gecikmesi

**Durum:**

- Commit edilmedi.
- Dinleme yapılmadı, gerçek DAW'da denenmedi.
- macOS derlenmedi (istenmedi); Windows derlemesi commit onayı bekliyor.

**Kaynak:** kullanıcının gerçek kullanımı (USER-REPORTED): "arada oktav nota hataları yapıyor, genelde ara notalarında; realtime 20 ms'den az olmalı".

**Kanıt etiketleri:**

- **MEASURED:** çalıştırılan ölçüm ya da test.
- **INSPECTED:** kaynak kod incelemesi.

**Ölçüm verisi:**

- 145 yeniden sentezlenmiş gerçek kayıt (`gt_resynth`), REALTIME, 44,1 kHz.
- Sonuçlar ve betikler: `artifacts/audio-runs/2026-11-27_v17/`.

## 1. Oktav hataları (ara notalar)

### Teşhis (MEASURED, 1.6, varsayılan ayar)

Çıkışta 92 oktav hatası vardı, toplam 3,71 s. Hepsi yukarı yönlüydü. Dağılımları:

| Ölçüt | Dağılım |
|---|---|
| Ne zaman | %59 sesin ilk anında (çoğu 100 ms'den kısa bir ünsüz / boşluktan sonra), %35 nota geçişinde, %7 uzun notada |
| Hangi aralık | +19 st (3. harmonik) 37 parça, +12 st 29 parça, +24 / +28 st (4. / 5. harmonik) 6 parça |

**Neden (INSPECTED):**

- Sesin başında YIN'in penceresi önceki boşluğa uzanıyor. Bu, uzun gecikmelerin farkını büyütüyor.
- Bunun sonucunda YIN bir harmoniğin periyodunu seçiyor.
- 1.6'daki oktav koruması bu durumu yakalamıyordu: yalnız 60 ms kararlı sesten sonra ve yalnız 10–14 st sıçramalarda devreye giriyordu.

### Değişiklikler (dedektör)

Her adım tek tek ölçüldü.

**o1 – sesin başındaki kontrol:**

- Ses başlarken ya da boşluktan sonra geri gelirken 2×, 3×, 4× ve 5× periyot sınanır. Sınama tam örnekleme hızındaki en yeni örneklerde yapılır.
- Daha uzun periyot, ancak o ana kadar seçilenden açıkça daha periyodikse kazanır. Ölçüt, oktav kuralının kendi testidir.
- Seslilik kararı YIN'in kendi çukurundan kalır, yani ses tam eskisi gibi başlar.

**o2 – ses sürerken:**

- Önceki periyodun ~1/3, 1/4 ve 1/5'i (+19, +24, +28 st) okunursa aynı test 3×, 4× ve 5× ile yapılır.

**o3c – nota geçişinde:**

- 1.6 kuralı yalnız 11–13 st yukarı oktav sıçramasını sınıyordu. Artık 8–17 st sıçramada da 2× sınanır (yeni notanın bir oktav yukarısı).

**Reddedilen denemeler:**

| Deneme | Neden reddedildi |
|---|---|
| o3: geçişte her harmoniği birden sınamak | Sentetik sopranoda (E6) ve gerçek kayıtta aşağı oktav hatası üretti. |
| o3b: kontrolü ~1 kHz'in altına sınırlamak | 245 Hz sesin 4. harmoniğinde düzeltmeyi kapattı. |

### Sonuç (MEASURED)

| | 1.6 | 1.7 |
|---|---|---|
| Çıkışta oktav hatası (varsayılan ayar) | 92 parça, 3,71 s | **39 parça, 1,25 s** |
| Aşağı oktav hatası | 0 | 0 |
| Dedektör: yukarı oktav (`gt_eval`, hard) | %0,057 | %0,026 |
| Dedektör: ≥ 15 st hata | %0,134 | %0,065 |
| Dedektör: 20 ms'den uzun oktav parçası | 23 | 14 |
| Doğal ölçütler: en kötü %10'luk dosyada oktav payı | %0,6 | %0 |
| Yanlış nota (varsayılan) | %8,38 | %8,24 |
| Doğru nota (varsayılan) | %91,8 | %92,1 |

**Gerileme yok:**

- `gt_eval`: hiçbir grup kötüleşmedi. Sentetik sesler (77) 1.6 ile aynı.
- İnce perde (0,39 ct medyan), seslilik, vibrato, geçiş ve oturma ölçütleri değişmedi.

**Kalan 39 hata:**

- Çoğu çok kısa: %67'si 30 ms'den kısa.
- Nota sonlarında ve başlangıçlarda kalıyorlar.

## 2. REALTIME gecikmesi

### Teşhis (MEASURED, "null test")

**Test:** düzeltme ve kaydırma kapalı. Çıkış, girişin tam gecikme kadar geriden gelen kopyası olmalı. Fark enerjisi ölçüldü.

**Bulgu:**

- HQ'da (40 ms) çıkış her notada şeffaf (−47…−60 dB).
- 1.6 REALTIME'da (20 ms) 156 Hz'in altındaki notalar şeffaf değildi (−4…−6 dB).

**Neden (INSPECTED):** bir grain'in iki periyotluk penceresi var. Bunun yanında:

1. Perde işaretleri yalnız analiz adımlarında (2,7 ms) konuyordu.
2. Her işaretin hizalanması işaretten sonra yarım periyot + P/6 veri bekliyordu.
3. Grain planı vakti gelince değil, adımda yapılıyordu. Gerçek periyodu veren sonraki işaret çoğu zaman henüz yoktu.

### Değişiklikler (yalnız REALTIME; HQ bit bit aynı)

1. **Geçici işaret:** işaretten sonraki yarım periyot henüz gelmediyse işaret önceki periyoda hizalanarak hemen konur.
   - Veri gelince, hiçbir grain kullanmadıysa, çevresindeki periyoda yeniden hizalanır.
   - Bir grain onu kaynak ya da periyot için kullandıysa artık oynamaz. Ondan sonrakiler de aynı hizada kalır, böylece aralık sıçramaz.
   - Neden yalnız önceki periyot değil: o hizalamada ses teli vuruşu pencerenin kenarına düşüyor. Sert düzeltmede çıkış ±3 ct titredi (ölçüldü, reddedildi).
2. **Güncel işaretler:** işaretler her grain planından önce en yeni girdiye kadar üretilir.
3. **Vakti gelen grain:** grain'in vakti gelince plan o andaki işaretlerle yeniden yapılır.
4. **Ses aralığına göre gecikme:** `TuneEngine::latencyFor (sr, realtime, range)`.
   - Aralık çalarken değişirse gecikme REALTIME↔HQ gibi kayarak değişir. Host'a hemen bildirilir.

### Seçilen gecikmeler

Ölçüt: her aralık en az 1.6'nın 20 ms'deki kadar temiz olsun.

| Ses aralığı | 1.6 | 1.7 | Not |
|---|---|---|---|
| Soprano / Alto | 20 ms | **13 ms** | 196 Hz üstü şeffaf; 156–196 Hz −41 dB (1.6: −54 dB). 12 ms'de bu bant −16 dB. |
| Tenor / Orta (varsayılan) | 20 ms | **15 ms** | Eşik 156 Hz, 1.6'nın 20 ms'si ile aynı. Kullanıcının kendi kayıtlarında da aynı sınıf. |
| Bas / Bariton | 20 ms | **17 ms** | 1.6'nın 20 ms'sine denk. 1.7, 20 ms'de tutulsaydı her notada şeffaf olurdu. |
| HQ | 40 ms | 40 ms | Değişmedi, bit bit aynı. |

### Kalite (MEASURED, 1.6 / 20 ms ile karşılaştırma, Tenor)

| | 1.6, 20 ms | 1.7, 15 ms |
|---|---|---|
| Null test: şeffaf en düşük nota | ~156 Hz | ~156 Hz |
| +4 st kaydırma: temiz karelerin p95 hatası | 15,5 ct | 14,1 ct |
| +4 st kaydırma: 50 ct'den büyük hata | %1,31 | %1,20 |
| Sert düzeltme: notadan medyan sapma | 0,3 ct | 0,6 ct |
| Sert düzeltme: 30 ct'den fazla sapan kareler | %4,14 | %4,5 |

**Bedeller (ölçüldü, kabul edildi):**

- **Sert düzeltmede küçük titreşim:** medyan 0,3 → 0,5–0,6 ct. Duyum eşiğinin (~5 ct) çok altında.
- **MIDI çıkışı:** note-off'lar sese göre 5 ms daha geç görünür. Analizin kararı aynı anda geliyor, ses daha erken çalıyor.
  - Bu yüzden testlerdeki note-off sınırı 40 ms + (20 ms − gecikme) yapıldı. Nota değişimleri eski sınırın (30 ms) içinde.
- **10 ms ve altı bu sentez yapısıyla gerçekçi değil:** 10 ms'de çıkışın sesli kalan payı %98'den %95'e düştü. Daha aşağısı yeni bir sentez tasarımı ister (asimetrik grain + ağırlık normalizasyonu).

## 3. Diğer

- **macOS CI'da düşen PluginTest 3j düzeltildi.**
  - Neden: Apple Silicon'da JUCE'nin adım yuvarlaması kalıntı bırakıyor. Preset dosyasına `default 7.45e-07` yazılıyordu.
  - Değerler artık parametrenin aynı tuttuğu en kısa sade ondalıkla yazılıyor.
  - FMA'lı (macOS benzeri) yerel derlemede yeniden üretildi. Linux ve FMA'lı derlemenin dosyası bayt bayt aynı.
- **Arayüz:** REALTIME ipucu artık aralığa göre gecikmeyi yazıyor (7 dil).
- **Test düğmeleri:** `wav_probe` için yalnız teste açık `rtlat` (sabit gecikme) ve `rtwin` (dedektör penceresi). Varsayılanlar ölçümle aynı.
- **WavProbe çağrı sırası:** eklentinin `prepareToPlay` sırasına getirildi (`setParams`, sonra `prepare`).

## 4. Testler (MEASURED, Linux)

| Test | Sonuç |
|---|---|
| EngineTest | Tümü geçti. Yeni 12b: çalarken aralık değişimi (sessizlik / tık yok, akort korunuyor, gecikme 13 / 15 / 17 ms'ye oturuyor). |
| PluginTest | Tümü geçti. Gecikme aralığa göre bildiriliyor (624 / 720 / 816 örnek @ 48 kHz), HQ her aralıkta 1920. |
| Varsayılan çıkışın eşitliği | Son derleme, ölçülen m5d ile 39/39 dosyada bit bit aynı. |
| pluginval 1.0.4, seviye 10 (VST3, Linux) | SUCCESS |
| CPU (BenchProbe matrisi, tek çekirdek, ortalama) | 44,1 / 48 kHz: değişiklik ölçüm gürültüsü düzeyinde (REALTIME %1,55 → %1,46 ve %2,02 → %1,75). 96 / 192 kHz REALTIME: %2,40 → %3,38 ve %4,89 → %7,11 (+%41–45; işaret hizalama ve periyot kontrolü örnekleme hızıyla büyüyor). HQ: 96 / 192 kHz'de +%16 / +%6. Bellek ayırma yok. |
| En kötü blok süresi | Bu sanal makinede güvenilir değil: 1.6 da 13 satırda bütçeyi aştı; 1.7'de 22–24 satır. Yüksek örnekleme hızında gerçek makinede ayrıca denenmeli. |

**Kör dinleme paketi (hazır, dinlenmedi):**

- 1.6 ↔ 1.7, REALTIME, 5 kayıt × 2 ayar (varsayılan, doğal); kullanıcının kendi kaydı dahil.
- Gecikme ve ses yüksekliği eşlenmiş. Gizli sıra her pakette farklı.
- Yer: `artifacts/audio-runs/2026-11-27_v17/listening/` (özel vokaller; repoya girmez).

**Yapılmayanlar:**

- Kör dinleme.
- Gerçek DAW'da gecikme telafisi ve aralık değişimi.
- Windows ve macOS üzerinde test.
