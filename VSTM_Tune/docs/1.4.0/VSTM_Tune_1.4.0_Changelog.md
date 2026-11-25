# VSTM Tune 1.4.0 – Değişiklik günlüğü (1.3 aday 3 → 1.4.0)

**Yayın adayı değil, yayımlanmadı.** Durum için kontrol listesine bak (`VSTM_Tune_1.4.0_Release_Checklist.md`).

- **Sürüm numarası:** 1.4.0 (CMake → host'ta ve Hakkında penceresinde).
- **Değişmeyenler:** eklenti kimliği (Vstm / Vtn1), parametre kimlikleri, sırası ve aralıkları, oturum biçimi (state 2),
  gecikmeler (REALTIME 20 ms, HQ 40 ms).
- **Kanıt etiketleri:** **MEASURED** ölçüm, **INSPECTED** kaynak incelemesi. **Dinleme ve DAW testi yok.**

## Yeni

### 1. `.drp` preset biçimi

- **Dosyalar:** `Source/Drp.{h,cpp}`, `Source/PresetManager.{h,cpp}`. Biçim: `VSTM_Tune_1.4.0_DRP_Format.md`.
- **Kayıt:** Yeni kayıtlar `.drp`: sürümlü, ikili ve sıkıştırılmış; metin düzenleyicide okunmaz. Başlıkta CRC-32 var.
  Önce geçici dosyaya yazılıp sonra yerine konur.
- **Okuma:** Her alan sınır denetiminden geçer: boyut, uzunluklar, kimlik karakterleri, açılmış boyut (64 KiB), eklenti
  kodları. Bozuk bir dosya hiçbir şeyi değiştirmez.
- **Eski presetler:** 1.0–1.3 `.vstmpreset` (XML) dosyaları okunmaya devam eder; yeniden adlandırılınca `.drp`'ye geçer.
- **Preset menüsü → Preset içe aktar…:** `.drp` ya da `.vstmpreset` dosyası doğrulanır ve preset klasörüne yeni bir adla
  kopyalanıp yüklenir. Hiçbir dosyanın üzerine yazılmaz.
- **Yeniden adlandırma düzeltmesi:** Artık aynı adda eski biçimde bir preset varken de reddediliyor (önce onu gizliyordu).
- **Testler (PluginTest 3h, 12 kontrol, MEASURED):**
  - her kesilmiş hâl, her tek bayt değişikliği ve 2000 rastgele dosya reddedildi;
  - sıkıştırma bombası, başka eklentinin dosyası ve geçersiz kimlik reddedildi;
  - `nan` / `inf` değerler güvenli;
  - 24 parametrelik kayıt / okuma turunda fark 0.

### 2. İmzalı admin yetkisi (eklenti içi)

- **Dosyalar:** `Source/Authorization.{h,cpp}`, `Source/third_party/monocypher/` (Monocypher 4.0.2, değiştirilmedi),
  `tools/drp_auth.py`. Tasarım: `VSTM_Tune_1.4.0_Authorization.md`.
- **İmza:** Ed25519. Eklentide yalnız açık anahtar var (anahtar kimliği `a2f22028b35ac70d`); özel anahtar kullanıcıda.
- **İçe aktarma** (Preset menüsü → Yetki dosyası içe aktar…): dosya doğrulanır ve yalnız
  `…/VSTM Audio/VSTM Tune/authorization.drp` olarak saklanır.
- **Yeniden doğrulama:** eklenti açılışında ve dakikada bir. "Yetkiyi kaldır" ile silinir.
- **İptal ve süre:** iptal listesi (seri numarası) ve bitiş tarihi desteklenir.
- **Açtıkları:** geliştirici paneli (canlı tanılama) ve uzman motor anahtarları. Uzman anahtarlar kaydedilmez, varsayılan
  açık, yetki yokken değiştirilemez. İşletim sistemi ya da dosya sistemi yetkisi yoktur.
- **Testler (PluginTest 3i, 11 kontrol + editörde panel kilidi, MEASURED):**
  - geçerli dosya doğrulandı; 147 baytlık dosyanın 1176 tek-bit değişikliğinin ve her kesilmiş hâlinin hepsi reddedildi;
  - imzasız, güvenilmeyen anahtar, sahte imza, bilinmeyen rol, eksik yetki, süresi dolmuş, henüz geçerli olmayan, iptal
    edilmiş, başka eklentinin dosyası ve preset reddedildi;
  - diskte değişen ya da süresi dolan kopya çalışmayı bıraktı;
  - `tools/drp_auth.py` ile gerçek anahtarla imzalanan dosya eklentide doğrulandı.
- **Arayüz:** 36 yeni metin, 7 dilde. Türkçe ve İngilizce dışındakileri ana dili konuşan biri gözden geçirmedi.

## Ses değişiklikleri

### 3. Algılayıcı: oktav-aşağı koruması genişledi (P0, oktav sağlamlığı)

- **Dosya:** `Source/dsp/PitchDetector.h`. Hem REALTIME'da hem HQ'da çalışır.
- **Sorun (MEASURED, kesin doğrulu sentetik set):**
  - 5 dB SNR'lik gürültülü seste ses kısa bir an unvoiced sayılıyor, sonra **bir oktav aşağıda** yeniden yakalanıyordu
    (30–60 ms'lik diziler). Mevcut oktav koruması yalnız ses kesintisizken çalıştığı için bunu yakalamıyordu.
  - Aynı seste bir de **3 kat periyot** (−19 yarım ses) hatası vardı; koruma yalnız 2 katı denetliyordu.
- **Değişiklik:**
  - Aşağı yönlü koruma artık kısa (≤ ~100 ms) bir boşluktan sonraki yeniden yakalamada da, son voiced periyoda göre çalışır.
  - Koruma 3 katı da kapsar.
  - **Kural aynı:** önceki periyot, uzun gecikme ezici biçimde daha periyodik değilse korunur. Gerçek aşağı sıçramalar
    geçer.
  - Yukarı yönlü kontrol değişmedi.
- **Etki (MEASURED; kesin doğrulu sentetik 77 / yeniden sentez 145 / gerçek kayıt 145):**

  | Set | Oktav-aşağı hata | Oktav dizileri ≥ 20 / 30 / 50 ms | Voiced isabeti |
  |---|---|---|---|
  | Sentetik, REALTIME | %0,062 → %0,044 | 5/2/1 → 4/1/**0** | −0,02 puan |
  | Sentetik, HQ | %0,070 → %0,059 | 7/4/0 → 6/3/0 | −0,03 puan |
  | Yeniden sentez, REALTIME | %0,004 → %0 | 26/6/1 → 23/6/1 | +0,01 puan |
  | Yeniden sentez, HQ | %0,004 → %0 | 32/14/2 → 29/14/2 | +0,01 puan |
  | Gerçek kayıt, REALTIME | %0,144 → %0,135 | 74/29/11 → 72/28/10 | −0,003 puan |
  | Gerçek kayıt, HQ | %0,141 → %0,133 | 86/31/12 → 82/30/12 | −0,003 puan |

  - 1.3'ün bilinen sorunu 9 kapandı: aday 2'den beri gürültülü sentetik seste REALTIME'da görülen 50 ms'den uzun oktav
    dizisi artık yok.
  - Gürültülü seste voiced isabeti düştü (REALTIME 1,5, HQ 2,6 puan): eskiden yanlış oktavda "voiced" sayılan adımlar artık
    unvoiced.
  - **Gerçek kayıtlarda (145) etki küçük ve dosyaya göre karışık:**
    - REALTIME'da 8 dosya iyileşti, 7 kötüleşti, 130 değişmedi; HQ'da 10 / 4 / 131 (oktav + kaba hata, referans perdeye
      göre).
    - En çok iyileşen: stem (REALTIME −0,27, HQ −0,38 puan), VocalSet f8 fry arpej (HQ −0,66 puan).
    - En çok kötüleşen: VocalSet f3 konuşur gibi söyleme (REALTIME %0,52 → %0,84, HQ %0,45 → %0,64).
    - Kötüleşenlerde artan hata oktav değil, 15 yarım sesten büyük kısa sıçramalar. Çıkıştaki farklar 17–20 yarım ses, yani
      3 kat periyot değişimiyle tutarlı. Mekanizma ayrıca doğrulanmadı.
- **Çıkış sesi (MEASURED, 145 gerçek kayıt, 1.3 aday 3 ile aynı dosya ve ayarlar):**
  - Bit bit aynı dosya: hard REALTIME 109, hard HQ 106, varsayılan ayarlar 104.
  - Çıkış perdesi, iki tarafta da ses olan adımların %0,05'inde oktav düzeyinde (≥ 6 yarım ses) farklı. En çok stem'de (%0,8)
    ve RAVDESS 5'te (HQ, %1,3).
  - Farklı dosyalarda fark sinyalinin medyanı −25…−29 dB. Bir perde olayından sonra grain fazı kaydığı için bazı dosyalarda fark
    sinyali sinyal düzeyine çıkıyor (en çok +2,8 dB). Bu sayı duyulan farkın ölçüsü değildir.
  - **Hangisinin daha iyi duyulduğu dinlenmedi.** Bu anları içeren kör dinleme paketi: `VSTM_Tune_1.4.0_Blind_Listening_Pack`
    (5 paket).
  - Kaynak: `artifacts/audio-runs/2026-11-25_v14/render_diff/` (betik, dosya başına CSV, özet).

## Denenip alınmayanlar

- **Fry kapısı:**
  - Fry'da hard tune'un titremesi ölçüldü: sentetik fry'da hedef nota saniyede 27, VocalSet fry arpejlerinde saniyede 8–76
    kez değişiyor.
  - İki basit ölçüt denendi, ikisi de işe yaramadı:
    1. 85 Hz altı perde ve adımdan adıma sıçrama: fry kayıtlarında adımların yalnız %0–0,1'inde devreye girdi; titremeyi
       değiştirmedi.
    2. ~110 ms'lik pencerede 4 ya da daha çok büyük sıçrama: normal kayıtların en kötüleri (stem %12, RAVDESS 5 %10),
       14 VocalSet fry kaydının 12'sinden yüksek çıktı. Fry'ı normal şarkıdan ayırmıyor.
  - Normal vokalde yanlışlıkla devreye girme riski yüzünden alınmadı. Bilinen sınır olarak kaldı.
- **Güvene duyarlı düzeltme:**
  - Algılayıcı kesinliği (clarity) hatalı adımları zayıf ayırıyor: 0,7 eşiği adımların %0,8'ini işaretliyor ve hataların
    yalnız %29'unu yakalıyor.
  - Hataların en çok olduğu iki kayıtta doğru ve hatalı adımların medyan kesinliği birbirine çok yakın: fry 0,94 / 0,89,
    5 dB gürültü 0,80 / 0,79.
  - Bu yüzden kesinliğe göre düzeltmeyi kısan bir değişiklik yapılmadı.

## Değişmeyenler (MEASURED)

- Hard tune geçiş zamanlaması (43 temiz geçiş): özet sayıları REALTIME'da ve HQ'da aynı.
  - HQ'da 47 geçişin hepsi aynı.
  - REALTIME'da 47 geçişin biri farklı: duyulan geçiş 0,5 ms erken (6,15 → 5,65 ms).
- Yanlış nota değişimi (77 sentetik, hard): sabit notada dakikada 28,2 → 27,6, vibratoda 666,0 → 666,1.
- REALTIME ↔ HQ geçişi: 132 durum, tekrar / atlama 0.
- CPU: 1.4 / 1.3 aday 2 medyanı REALTIME 0,99 (110 yapılandırma), HQ 1,00 (108); ses iş parçacığında bellek ayırma 0.
  Aday 3'ün ayrı bir BenchProbe'u yok. Aday 3, aday 2'ye analiz adımı başına yalnız birkaç işlem ekliyor (INSPECTED).
- Testler: EngineTest 78 / 78, PluginTest 97 / 97 (73 → 97), pluginval seviye 10 SUCCESS.
- Derleme: 0 uyarı.
