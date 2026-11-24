# VSTM Tune 1.3.0 – Değişiklik günlüğü (aday 1, yayımlanmadı)

Durum: **1.3.0 aday 1.** Bu bir kalite sürümü; yeni özellik, parametre ya da arayüz değişikliği yok. Yayın kontrol listesi
tamamlanmadı (`VSTM_Tune_1.3.0_Release_Checklist.md`). Bu yüzden derleme hâlâ 1.2.0 sürüm numarasını taşıyor; numara yayında
değişecek.

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
- **REALTIME:** Çıkış bit bit aynı. Orada her kayıt, analiz edildiğinde zaten çalınmış oluyor.
- **Varsayılan (%50) ve doğal ayarlar:** Çıkış bit bit aynı.

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
- **Etki (MEASURED, 145 kayıt):** 9 ayarın hiçbirinde 0 dBFS'i aşan örnek kalmadı.
  - **Sınır:** Örnekler arası tepe (true peak) formant +12'de +1,0 dBTP'ye kadar çıkabiliyor.
- **Normal seviyede (tepe −6 dBFS):**
  - natural: hiç devreye girmedi.
  - varsayılan: 145 dosyanın 2'sinde devreye girdi (en fazla 0,7 dB).
  - hard tune: 8 dosyada devreye girdi (en fazla 2,0 dB).
- **Duyulup duyulmadığı test edilmedi.** Kör dinleme paketi L01–L05 bunun için.

## Değişmeyenler (MEASURED)

- Parametre kimlikleri, sırası (27), aralıkları, preset ve oturum biçimi (PluginTest 73 / 73).
- Gecikme: REALTIME 20 ms, HQ 40 ms.
- Perde algılayıcı: hop kayıtları 1.2.0 ile bit bit aynı.
- Nota kararları ve yanlış nota değişimi sayıları aynı.
- REALTIME ↔ HQ geçişi: 132 durumda tekrar ya da atlama yok; 1.2.0 ile aynı.

## Denenip reddedilenler (MEASURED, belgede ayrıntılı)

Nota kararını hızlandıran üç aday yanlış nota değişimini artırdığı için alınmadı. Yol haritası bunu yasaklıyor. Sabit notada
yanlış değişim, 1.2.0'da dakikada 29:

| Aday | Değişiklik | Karar medyanı | Yanlış değişim (dk) |
|---|---|---|---|
| A | Histerezis 6 ct ve iki hop bekleme | 9,7 → 6,0 ms | 75 |
| B | "Kararlı hareket" kısayolu | zamanlama iyileşmedi | 39 |
| C | Bekleme süresi iki hop (HQ'da medyan 5,7 ms, p95 15,6 ms) | – | 53 |
