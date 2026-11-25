# VSTM Tune 1.6.0 – Natural Auto Correction (aday, 25.11.2026)

**Durum:** aday; commit edilmedi, yayımlanmadı. **Dinleme yapılmadı** ve gerçek bir DAW'da denenmedi.

- **Dayanak:** `VSTM_Tune_1.6.0_Natural_Auto_Correction_Specification.txt` şartnamesi, 1.5.0 adayının üzerine.
- **Kanıt etiketleri:**
  - **MEASURED:** sayısal ölçüm;
  - **INSPECTED:** kaynak kodu incelemesi.
- **Dinleme:** "daha doğal" sonucu ancak kör dinlemeyle doğrulanabilir; paketler hazır (bölüm 8).
- **Hiçbir ürünle eşdeğerlik iddiası yoktur.**

## Özet

| | 1.5 | 1.6 |
|---|---|---|
| Vibratolu uzun notada yanlış nota değişimi (doğal / varsayılan) | 6,9 / 8,2 s⁻¹ | **1,8 / 2,8 s⁻¹** |
| Uzun notada doğru hedef nota (doğal / varsayılan) | %75 / %72,5 | **%94,5 / %92** |
| Çıkış merkezinin notaya uzaklığı, medyan (doğal / varsayılan) | 12 / 12 ct | **7,2 / 6,5 ct** |
| Korunan vibrato derinliği (doğal / varsayılan) | 0,73 / 0,85 | **0,98 / 0,98** |
| Vibrato şekil benzerliği, korelasyon (doğal) | 0,96 | **0,99** |
| Geçiş süresi / şarkıcınınki (doğal; gerçek / sentetik) | 1,28 / 2,22 | **1,03 / 1,15** |
| Yeni notaya oturma, doğal (gerçek / sentetik) | 154 / 115 ms | **119 / 65 ms** |
| Çıkışa ulaşan oktav hatası, varsayılan | 105 parça, 3,96 s | **92 parça, 3,71 s** |
| Hard tune (Robotism %0) | – | **1.5 ile bit bit aynı** |

**Ölçüm koşulları** (MEASURED, `tests/natural_eval.py`):

- **Gerçek kayıtlar:** 145 kayıt, perde eğrisi kesin bilinen yeniden sentez (40 dakika). Bunlar VocalSet (vibrato, düz, messa, fry, nefesli, belt, konuşma), RAVDESS ve kullanıcının vokallerinden alındı.
- **Sentetik:** 77 parametrik ses.
- **Ayarlar:** Key C, kromatik.
- **Değerler:** medyan; ayrıntılar `artifacts/audio-runs/2026-11-25_v16/`.

## 1. P0 – Doğal perde merkezi (nota kararı)

**Sorun (MEASURED, 1.5):** nota kararı vibratolu ham perdeye bakıyordu.

- Vibrato yarım ses sınırını geçince hedef her tepe ve çukurda değişiyordu. Vibratolu uzun notalarda bu, saniyede 12–15 değişim demekti.
- Düzeltilmiş merkez notadan 17–18 cent uzakta kalıyordu.

**Değişiklik** (`PitchCorrector`, iç anahtar `naturalCentre`):

- Ses kendi merkezi etrafında salınırken nota, vibratosuz merkezden (2 kutuplu 3 Hz) seçilir. Salınım ölçütü mevcut salınım izleyicisidir: 0,4 s içinde 3 yön değişimi.
- Gerçek değişimler beklemez:
  - **sıçrama:** ham perdeyle hemen geçer;
  - **adım:** ses vibratonun son genliğinin dışına çıkınca (0,3 s'de sönen tepe izleyici, 1,15 katı + 10 ct) karar o anda ham perdeye döner.
- Gerçek bir değişimden sonra salınım yeni nota etrafında yeniden öğrenilir. Merkezin kendi verdiği kararlar bunu sıfırlamaz. Bu ayrım ölçümle seçildi: her değişimde sıfırlamak kazancı yok ediyordu (aşağıdaki denemeler).
- **Robotism eşiği:** %25 ile %50 arasında devreye girer, %50'den itibaren tamdır. Robotik uç (≤ %25) eskisi gibi ham perdeye bakar; hard tune'un vibratoda "titreşen" karakteri korunur.

## 2. P0 – Doğal hareket ve vibrato koruması

**Sorun (MEASURED):** nota sabitlenince retune yumuşatması vibratoya da uygulanıyordu. Varsayılan ayarda vibrato derinliği 0,85'ten 0,71'e düştü (yalnız P0-1 denemesi).

**Değişiklik** (`naturalMotion`):

- Salınım sürerken düzeltme yalnız vibratosuz merkezi notaya çeker; vibratonun kendisi geçer. Ağırlık P0-1 ile aynıdır.
- **Sonuç:** vibrato korunması 0,98, şekil benzerliği 0,99 (doğal).
- **Kayma (drift):** oranı ~1,0 kaldı; yavaş kayma bilerek silinmiyor. Merkez notaya çekiliyor, ses "dümdüz" olmuyor.

## 3. P0 – Doğal nota geçişleri

**Sorun (MEASURED, 1.5, Robotism %80):** insansı tarafın otomatik geçişi her değişime sabit bir S-eğrisi koyuyordu (en fazla 110 ms). Çıkış geçişi şarkıcınınkinin 1,28 katı (gerçek) ve 2,22 katı (sentetik kaymalar) sürüyordu.

**Değişiklik** (`naturalTransition`):

- Otomatik geçiş şarkıcıyı izler. İlerleme, sesin hedef notaya ne kadar yaklaştığıdır (geri gitmez).
- En azından iki katı otomatik geçiş süresi hızında ilerler; hedefe tam varmayan ses de oturur.
- Ayarlı bir Note Transition ve ton değişimi geçişi S-eğrisini korur. Robotik tarafta geçiş yoktur.

**Bilinçli davranış değişikliği:** İnsansı tarafta hızlı bir atlama artık hızlı kalıyor. 1.0–1.5'te 20 ms'lik bir atlama en az 45 ms'ye uzatılıyordu.

- Şartname "notaları bulanıklaştıran aşırı yumuşatmadan kaçının, portamentoyu koruyun" diyor.
- EngineTest'teki eski test ("İnsansı: nota değişimi kayar, > 45 ms") yeni anlamla değiştirildi:
  - hızlı atlama, şarkıcının hızında;
  - 150 ms'lik portamento korunuyor (0,75–1,3 katı).

**Aşma (overshoot):** gerçek şarkıcılar geçişte medyan 0,31 yarım ses aşıyor. 1.5 bunun çoğunu siliyordu (çıkışta 0,08). 1.6 bir kısmını koruyor (0,185). Çıkış, şarkıcının kendi aşmasını p90'da en fazla 0,12 yarım ses geçiyor; 1.5'te bu 0,06–0,09 idi.

## 4. P0 – Fry, gırtlak sesi ve oktav sağlamlığı

**Bulgu (MEASURED, 1.5):**

- Çıkışa ulaşan oktav hatası parçalarının %100'ü algılayıcının bir analiz adımında oktav sıçramasından geliyordu.
- Sıklık dakikada 3,6; yarısı 30 ms'den kısa.
- Düzeltici bu sıçramayı hemen izliyor, hedefi bir oktav zıplatıyordu.

**Değişiklik** (`octaveGuard`): iki sesli adım arasında 10–14 yarım ses sıçrama artık hemen izlenmiyor.

- Algılayıcı dönene ya da yeni oktav 40 ms sürene kadar perde bir oktav geri okunur.
- Böylece hedef ve düzeltme yerinde kalır, kaydırıcı sesi olduğu yere koyar.
- İki koşul, ölçümle bulunan iki hatayı önlüyor:
  - Kabul edilmiş bir oktav değişiminden sonraki 0,3 s içinde eski perdeye dönüş hemen kabul edilir.
  - Koruma, perde 60 ms kararlı kaldıktan sonra devreye girer; yanlış oktavda başlayan bir parçanın doğru oktava geçişi engellenmez.
- **Kapsam:** yalnız doğal ve varsayılan taraf (Robotism > %25); hard tune her sıçramayı eskisi gibi izler.
- **Sonuç (varsayılan):**
  - 105 → 92 parça, 3,96 → 3,71 s; fry kayıtlarında 0,46 → 0,38 s;
  - korumanın kendi ürettiği hata yok (kalanların %100'ü algılayıcı hatası).

**Sınır:** kalan oktav hataları algılayıcıdan geliyor. Algılayıcıyı yeniden yazmak bu sürümün kapsamında değil (şartname: "kanıt olmadan değiştirmeyin").

### Denenip bırakılanlar (MEASURED)

- **P0-1 tek başına:** varsayılan ayarda vibratoyu 0,71'e düşürdü ve oturmayı 58 → 128 ms yavaşlattı. P0-2 ile birleştirildi.
- **Her nota değişiminde salınım sıfırlama:** nota değişimleri 1,8 → 4,6 / s'ye döndü; vibratonun kendi kararları sıfırlamayı tetikliyordu.
- **Oktav koruması v1 ve v2:** parça sayısı azaldı ama toplam süre azalmadı (3,96 → 4,07 / 4,02 s). Hataların %13–17'sini koruma kendisi uzatıyordu.
- **Merkez referansı olarak 1,5 Hz:** 3 Hz'den biraz kötü (merkez hatası 7,5 → 8,3 ct).

## 5. P1 – Formant ve tını

Ölçüm (MEASURED, 36 gerçek kayıt, kuru sese göre, sesli kareler):

| Ayar | Zarf farkı: 1.5 → 1.6 | Spektral merkez oranı: 1.5 → 1.6 | 4 kHz üstü enerji: 1.5 → 1.6 |
|---|---|---|---|
| Doğal | 1,27 → 1,05 dB | 0,956 → 0,969 | −0,48 → −0,45 dB |
| Varsayılan | 1,34 → 1,28 dB | 0,959 → 0,964 | −0,59 → −0,57 dB |

1.6 tınıyı kuru sese biraz daha yakın tutuyor. Formant tarafında değişiklik yapılmadı.

## 6. Auto Character (AÇIK / KAPALI)

**Parametre:** yeni `autochar` (sürüm ipucu 4).

- Otomasyonlu ve presetin parçası.
- **Varsayılan KAPALI:** eski oturumlar ve presetler eskisi gibi açılır.

**AÇIK:** Robotism, Retune Speed, Humanize ve Vibrato Scale'i ayrı eğrilerle yönetir (`params::characterFor`).

| Robotism | Retune | Humanize | Vibrato Scale |
|---|---|---|---|
| %0 | 0 ms | %0 | %60 |
| %25 | 3,8 ms | %3 | %80 |
| %50 | **15 ms** | **%35** | **%100** (fabrika varsayılanı) |
| %75 | 18,5 ms | %42,5 | %100 |
| %100 | 25 ms | %50 | %100 |

Eğrilerin biçimi:

- **Robotik taraf:**
  - Retune kare eğriyle 0'a iner; sert bölge geniş kalır.
  - Humanize %50 ile %20 arasında söner.
  - Vibrato %60'a kadar azalır.
- **Doğal taraf:**
  - Retune yalnız biraz yavaşlar. 1.6 motoru vibratoyu ve geçişleri zaten koruyor; üstüne çok yavaş bir retune notaları perdeden kaçırırdı.
  - Humanize uzun notalarda artar.

**Yönetilmeyenler:** Formant, Pitch Range ve Mix. Karakter kontrolü sesin tınısını ve aralığını değiştirmemeli.

**Arayüz:**

- ROBOTISM başlığının solunda "AUTO" balonu var; açıkken yanar.
- Yönetilen kontroller etkin değeri mor gösterir, "AUTO" rozeti taşır ve kilitlidir.

**Sahiplik ve değer devri:**

- **AÇIK iken:** yönetilen kontrollerin manuel değerleri dokunulmadan bekler. Bu sırada host'un o parametrelere yazdığı otomasyon kullanılmaz.
- **KAPALI'ya geçince:** o anki otomatik değerler manuel değer olur.
  - Arayüzden kapatılırsa önce değerler devredilir, sonra anahtar kapanır.
  - Host otomasyonuyla kapatılırsa devir dinleyicide yapılır.
  - Oturum geri yüklenirken ve preset yüklenirken devir yapılmaz; dosyanın kendi değerleri geçerlidir.

**Testler (PluginTest, MEASURED):**

- Eğriler %50'de tam fabrika varsayılanı veriyor; tekdüze ve sürekli (0,5'lik adımda en büyük değişim 0,87).
- Varsayılan ayarda AÇIK, KAPALI ile **bit bit aynı**.
- KAPALI iken Robotism diğer kontrollere dokunmuyor.
- AÇIK → KAPALI (bloklar arasında, host otomasyonu gibi) çıkış **bit bit kesintisiz**; değerler devredildi.
- AÇIK iken farklı manuel değerler aynı çıkışı veriyor (deterministik).
- Oturum geri yükleme manuel değerleri koruyor ve AÇIK durumunu geri getiriyor.
- Preset yüklemede devir yapılmıyor.
- 96 / 192 kHz.

Teknik not (INSPECTED): JUCE, dokunulmamış bir parametrenin varsayılanını adıma yuvarlamadan saklıyor (Retune 14,999999), ayarlanan değerleri ise yuvarlıyor (15,0). Otomatik değerler aynı gösterimle hesaplanıyor; bit-bit eşitlik buna dayanıyor.

## 7. 1.5 adayına bu oturumda eklenenler (1.6'ya taşındı)

Hepsi varsayılan olarak kapalı. Kapalıyken çıkış 1.5'in ilk paketiyle bit bit aynı (666 render).

| Özellik | Not |
|---|---|
| **MIDI çıkışı** | Düzeltilen nota, çıkış sesinin çaldığı örnekte gönderilir. Kısa parçalar süzülür: en kısa nota REALTIME 8 ms, HQ 15 ms; boşluk 20 ms. Blok boyutundan bağımsız. |
| **MIDI Perform** | "MIDI: octave as played": ses, son basılan notaya oktavıyla gider. |
| **MIDI CC ve MIDI Learn** | Kontrole sağ tık → MIDI Learn. Bağlamalar oturumla kaydedilir, presetlere girmez. |
| **Akort sistemleri** | Eşit, saf, Pisagor, Rast ve Bayati (çeyrek ton), özel (dereceye ±50 ct). |
| **Dizi içinde transpoze** | ±7 derece. |
| **Attack ve release** | Cümle başında düzeltmenin devreye girmesi; sönen notada bırakma. |
| **64 bit yol hatası** | Söz verilenden uzun bloklarda MIDI olayları artık iki kez işlenmiyor. |

MIDI çıkışı ölçümleri (MEASURED, 145 kayıt):

- MIDI notasının çıkış sesine göre gecikmesi: REALTIME −1 ms, HQ −1..−3 ms.
- Kısa (< 30 ms) notalar: filtresiz 179/dk, filtreli 102–140/dk.

## 8. Doğrulama

| | Sonuç |
|---|---|
| EngineTest | 101/101 ✅ |
| PluginTest | hepsi ✅ (parametre sırası: 67, 54'ü 1.0'dan sonra eklendi) |
| Hard tune, 1.5'e karşı | bit bit aynı (sonuçlar `identity_16vs15/`) |
| Varsayılan ses, 1.5'e karşı | **bilerek değişti** (bölüm 1–4) |
| pluginval seviye 10 | `pluginval/` |
| CPU (BenchProbe) | `bench/` |
| Kör dinleme paketi (1.5 ve 1.6) | `listening/packs/` (16 paket) |

**Kör dinleme paketleri:**

- **İçerik:** 8 kayıt (senin vokalin, stem, VocalSet'ten messa, vibrato, düz ve fry) × varsayılan ve doğal ayar.
- **Dosyalar:** `dry_matched` = 1.5, `wet_matched` = 1.6. `blind_A` ve `blind_B` karıştırılmıştır.
- **Kullanım:** önce `LISTENING_SHEET.md` ile dinle, `blind_key.json` dosyasını sonra aç.
- **Gizlilik:** senin vokallerin yalnız bu makinede.

## 9. Test edilmeyenler ve sınırlar

- **İnsan dinlemesi:** yok. Şartnamenin kabul ölçütü olan "kör dinlemede 1.5'e göre doğallık artışı" henüz kanıtlanmadı.
- **Gerçek DAW:** yok. **Windows ve macOS derlemesi:** push gerekiyor; yapılmadı.
- **Hafif fry ve oktav:** iyileşme küçük. Kökü algılayıcıda.
- **Metriklerin sınırı:** "doğallık" metrikleri vekil ölçülerdir; müzikal sonucu kanıtlamaz.
- **Robotik uç:** çok geniş vibratolu seslerde hard tune'un nota değişimleri eskisi gibi; bu bilinçli bir tercih.
