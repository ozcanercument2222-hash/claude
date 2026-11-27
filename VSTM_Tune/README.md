# VSTM Tune – gerçek zamanlı vokal autotune

Tek düğmeyle **robotik ↔ insansı** arasında gezinen, gerçek zamanlı perde düzeltme plugin'i.
Formatlar: **VST3**, **AU** (yalnız macOS), **Standalone**. Mono ve stereo. Sürüm **1.6.0 adayı** (değişiklikler: `RELEASE_NOTES.md`,
1.6 ayrıntıları ve ölçümler: `docs/1.6.0/`).

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

Testle ölçülen davranış (1.2; 48 kHz, sentetik vokal, A3 → C4 sıçraması, ±50 cent vibrato):

| | Robotik (0) | İnsansı (100) |
|---|---|---|
| Nota geçiş süresi (%10 → %90) | 12 ms (basamak) | ~216 ms (kayarak) |
| Vibrato (tepe–tepe) | 1 cent (dümdüz) | 100 cent (korunur) |

**Eğri kalibrasyonu (dört ses tipi: bas, tenor, alto, soprano – sentetik, MEASURED):** düğme artık her
adımda benzer oranda değişir. Robotik uçtaki %0–10 "ölü bölge" kalktı (vibratonun %1'i → %4,5'i kalır),
%50 → %65 arasındaki ani sıçrama yumuşadı (tenor geçişi: %50 22 ms · %65 48 · %80 98 · %90 142 · %100 214 ms;
önce 22 · 76 · 124 · 160 · 214). %0, %50 (ayarlar olduğu gibi) ve %100 değişmedi. Tenor, alto ve soprano aynı
düğme değerinde ±5 puan içinde aynı vibrato miktarını bırakır. Gerçek vokallerde (91 kayıt) çıkışın en yakın
yarım sese uzaklığı %0'da ~2 ct, %50'de ~18 ct, %65 ve üstünde ~22–24 ct (doğal şarkı sapması); kadın, erkek ve rap
vokali gruplarında aynı sırayı izler. Algısal kalibrasyon (insan dinlemesi) henüz yapılmadı.

**1.2'de değişen (MEASURED, dinlenmedi):** algılayıcı doğru zamanlandığı için Dengeli ve İnsansı yarıda vibratonun daha çoğu
kalır. Tenor, sentetik: %10'da %4,5 → %9, %35'te %49 → %72, %50'de %74 → %97. Geçişler biraz uzadı: %50 22 → 28 ms,
%65 48 → 58, %80 98 → 112, %100 214 → 232 ms. Gerçek vokallerde notaya yakınlık aynı sırayı izliyor
(%0 1,5 ct · %20 7 · %50 18 · %80 23 ct).

## Kontroller

| Kontrol | Açıklama |
|---|---|
| **KEY** | Tonik. **Beat'in tonunu biliyorsan buradan seç** (en güvenilir yol: seçtiğin ton sen değiştirene kadar sabit kalır). **Auto** tonu yalnızca **vokalden tahmin eder** (beat'i duymaz; KEY yanında `VOKALDEN TAHMİN` yazar): `Auto · …` dinliyor, `Auto · A? 41%` kilitlenmemiş tahmin ve güveni, `Auto · A` + 🔒 kilitli. Kilitlenene kadar notalar en yakın yarım sese gider. Bir kez kilitlenince söylenen notalar (yanlış, kromatik, geçiş notası, vibrato, melisma) tonu **değiştirmez**. Ayrıntı: aşağıda *Auto Key*. |
| **SCALE** | İzinli notalar: Chromatic, Major, Minor, Harmonic / Melodic Minor, Major / Minor Pentatonic, Blues, Dorian, Phrygian, Lydian, Mixolydian, **Hicaz**, **Özel dizi** (menünün sonunda: `Custom scale…`). |
| **Özel dizi (Custom scale)** | Bir oktavlık klavyede 12 notayı tek tek aç / kapat. Notalar tona göre tutulur (1, b2 … 7): ton değişince ya da Auto yeni tonu bulunca dizi de onunla kayar; tonik halkayla gösterilir. İlk açılışta seçili dizinin notalarıyla başlar; `Diziden kopyala` istediğin an tekrar alır. Preset ve projeyle kaydedilir, otomasyona açıktır. |
| **PITCH CORRECTION** | Hedef notaya olan mesafenin ne kadarı düzeltilsin (0–100%). |
| **RETUNE SPEED** | Perdenin notaya çekilme hızı (0–200 ms). 0 ms = anında. |
| **HUMANIZE** | Uzun, tutulan notalarda düzeltmeyi gevşetir; kısa notalar sıkı kalır. |
| **SCALE** (alt sıra) | Doğal vibrato ve perde hareketinin derinliği (0–200%, 100% = söylendiği gibi). |
| **FORMANT** | Sesin rengini (gırtlak boyu) perdeyi değiştirmeden kaydırır (±12, yarım ses). |
| **PITCH RANGE** | Düzeltilmiş sesi ±12 yarım ses transpoze eder. Transpoze ve formant kullanılırken seviye kuru sese eşitlenir (aşağıya bak). |
| **MIX** | Orijinal ↔ düzeltilmiş ses karışımı. |
| **ADAPTIVE** (deneysel) | Kapalıyken motor eskisiyle aynı çıkışı verir. Açıkken performansa sabit sınırlar içinde uyum sağlamaya çalışır: algılayıcı emin değilken düzeltme en çok %50'ye iner; vibrato ve bilinçli kaymalarda düzeltme gevşer (ROBOTISM %15'ten %50'ye doğru); hızlı koşularda notalar daha çabuk oturur. Hiçbir parametreyi değiştirmez. **Ölçüm (1.2, 91 gerçek kayıt):** İnsansı uçta (%100) vibratonun yalnız ~%50'si kalıyor (kapalıyken ~%92). Robotik uçta da küçük bir etkisi var (notaya yakınlık 1,5 → 2,6 ct). Varsayılan değildir; dinleme testi yapılmadı. |
| **AUTOTUNE ON** | Aç / kapat. DAW'ın bypass düğmesiyle senkron; kapalıyken ses gecikmesi korunur (takır yok). |
| **REALTIME** | Açık: düşük gecikme (kayıt / canlı için; 1.7: ses aralığına göre **Soprano / Alto 13 ms, Tenor 15 ms, Bas 17 ms**, önceden 20 ms; aralık çalarken değişirse gecikme kayarak değişir). Kapalı: yüksek kalite (**40 ms**, çok kalın sesler için). DAW gecikmeyi otomatik telafi eder. Çalarken değiştirilebilir (1.2): bildirilen gecikme hemen değişir. Ses kesilmeden yeni gecikmeye kayar: 0,4 s içinde, kuru ses de duyuluyorsa 2 s içinde. Hiçbir hece tekrarlanmaz ya da atlanmaz; bulunan ton korunur. Kayma sırasında kuru sesin hızı en çok ~%1,6 değişir. |
| 〰 düğmesi | Perde ekranında ham giriş perdesini de gösterir. |

**Perde ekranı:** dikey çizgi yazma kafasıdır, soldan sağa tarar (6 saniye). Mor eğri düzeltilmiş çıkış,
noktalar nota geçişleri. Kafadaki etiket o an söylenen notayı ve girişin o notadan sapmasını gösterir (`A4 −16 ct`).
Ses yokken ekran durur (gereksiz CPU yok).

**Üst bar:** preset seçici (`<` `>` veya isme tıkla: 12 fabrika preseti Robotik / Doğal / Efekt grupları + kendi presetlerin;
değiştirince isim yanında nokta çıkar; menüde kaydet, **yeniden adlandır**, sil, klasörü aç, **preset içe aktar** ve
**Presetler ton ve diziyi korur**; 1.4'ten beri **yetki dosyası içe aktar / kaldır**),
⤓ preset kaydet, ▢ pencere boyutu (768 × 512 … 1536 × 1024, köşeden de sürüklenir), ⚙ ayarlar (vokal aralığı:
Soprano / Alto, Tenor, Bas / Bariton; tüm parametreleri sıfırla; hakkında).

**Dil:** arayüz, bilgisayarın dilinde açılır: Türkçe, İngilizce, Almanca, Fransızca, İtalyanca, Rusça veya Çince.
Bunlardan biri değilse İngilizce. Düğme adları, değer yazıları, menüler, ipuçları ve DAW'da görünen parametre adları çevrilir;
preset ve nota adları (C, D, E…) değişmez. Kayıtlı projeler, otomasyonlar ve presetler dilden bağımsızdır (parametre kimlikleri aynı).
Dili zorlamak için ortam değişkeni: `VSTM_TUNE_LANG=de` (en, tr, de, fr, it, ru, zh).

**Fare:** düğmelerde dikey sürükle, **Shift** = ince ayar, **çift tık** = varsayılan, tekerlek, **değere tıkla** = yazarak gir,
**sağ tık** = DAW'ın parametre menüsü (otomasyon, MIDI learn).
**Klavye:** knob / kaydırıcıya tıkla ya da **Tab** ile gel: ok tuşları %1, **Shift** + ok bir adım, PageUp / PageDown %10,
Home / End uçlar, Delete varsayılan; KEY / SCALE kutusu ve alt anahtarlar Enter / Boşluk ile. Her tuş DAW'a tek bir otomasyon
hareketi olarak gider; kullanılmayan tuşlar (ör. Boşluk = DAW'ı çal) DAW'a geçer. Odak halkası yalnız Tab ile gelince görünür.

## Auto Key: vokalden tahmin, bir kez kilit (1.1)

Kullanıcının iş akışında ton **beat'e göre** seçilir. Plugin yalnız vokali duyar; Auto bu yüzden bir *tahmindir* ve beat'in
tonunu garanti etmez (eksik notalı bir ezgide tonik / majör-minör belirsiz kalabilir). Sidechain ile beat analizi **yok**.

| Durum | KEY kutusu | Düzeltme |
|---|---|---|
| Dinliyor (kanıt yok) | `Auto · …` | en yakın yarım ses (güvenli geri dönüş) |
| Aday (kilitsiz) | `Auto · A? 41%` (yüzde = motorun hesapladığı güven) | en yakın yarım ses |
| Kilitli | `Auto · A` 🔒 | A + seçili dizi; **söylenen notalar değiştirmez** |
| Kromatik dizi | `Auto` | ton gerekmez |

- **Kilit ne zaman:** yeterli ve *ayırt edici* kanıt toplanınca: uzun bellekte ~4 s'lik net şarkı, güven ≥ %50 ve aynı aday
  arka arkaya 6 bakışta (her bakışta yeni şarkı). Gerçek kayıtlarda tipik olarak 10–20 s. Ayırt edici nota yoksa (ör.
  yalnız C D E G A: C, F ve G majörün hepsine uyar) kilit olmaz, aday gösterilir.
- **KEY menüsü (Auto seçiliyken):** durum satırı, **Şimdi kilitle** (görünen adayı), **Önerilen tona geç** (kilitliyken vokal
  açıkça başka bir tonu gösterirse), **Yeniden analiz et** (kanıt ve kilit silinir, baştan dinler), **Ton değişimlerini izle**
  (modülasyon; isteğe bağlı, varsayılan kapalı; açıkken kilitli ton ~1 s kararlı kanıtla değişebilir).
- **Kalıcılık:** kilit, host'un yeniden hazırlamasından (çal / durdur, tampon / örnekleme hızı değişimi) etkilenmez ve projeyle
  kaydedilir (oturum sürümü 2: `autoKeyLock`, `autoKeyConf`, `followKeyChanges`, `presetsKeepKey`). 1.0 projeleri kilitsiz açılır.
- **SCALE türü değişince** kilitli tonik korunur; mevcut kanıt yeni dizide başka bir toniği açıkça (güven ≥ %50) gösteriyorsa ona
  geçilir (ör. A minör ezgide Minor → Major: A → C, aynı notalar). Özel dizinin notalarını düzenlemek tonu oynatmaz.
- **Ölçüm (MEASURED, 145 gerçek kayıt, varsayılan ayar):** kilitten sonra ton değişimi 35 → **0** (eski sürüm 23 kayıtta
  avlanıyordu); kilitlenen kayıtlar 99 → 21 (temkinli); şarkılarda kilidin, kaydın tamamının gösterdiği tonla uyumu
  47/55 → **20/20**; medyan kilit süresi 5,5 → 17,5 s. Ayrıntı: `VSTM_TUNE_AUDIT.md` 17.2.

## Presetler: ton ve dizi şarkıya aittir (1.1)

**Presetler ton ve diziyi korur** (preset menüsünde, varsayılan **açık**, projeyle kaydedilir): preset yüklemek KEY, SCALE ve
özel diziyi değiştirmez; yalnız sesi (ROBOTISM, RETUNE, HUMANIZE, PITCH CORRECTION, vibrato, formant, transpoze, MIX, Adaptive)
değiştirir. Kapatılırsa 1.0'daki gibi preset kendi ton ve dizisini de yükler (kullanıcı presetleri bunları dosyada saklamaya
devam eder). **Tüm parametreleri sıfırla** (⚙) her şeyi, ton ve dizi dahil, varsayılana döndürür.
Fabrika presetleri yalnız ses ayarıdır: 1.0'daki "Hicaz Vocal" ve "Chromatic Fix"in tek farkı diziydi, kaldırıldı (Hicaz ve
Kromatik SCALE menüsünde; bu adlı eski projeler değerleriyle açılır). Preset değişiminde tık ölçülmedi: geçişteki en büyük örnek
adımı sabit durumun 1,17 katı (sınır 1,5; MEASURED, PluginTest 3g).

## ROBOTISM diğer kontrolleri nasıl etkiler (deterministik)

ROBOTISM ayarların üstünde çalışan bir makrodur; parametrelerin **değerini değiştirmez**, etkilerini şöyle ölçekler
(`dsp/PitchCorrector.h`; r = robotik yarı 0…1, h = (insansı yarı)^1,6):

| | ROBOTISM 0 (Robotik) | 50 (Dengeli) | 100 (İnsansı) |
|---|---|---|---|
| RETUNE SPEED | yok sayılır (anında, 0 ms) | olduğu gibi | 4 kat yavaş |
| HUMANIZE | yok sayılır | olduğu gibi | en az %40'a yükselir |
| Nota geçişi | basamak | basamak | S-eğrisi kayma 110 ms |
| Histerezis / bekleme | 15 ct / 14 ms | 15 ct / 22 ms | 35 ct / 52 ms |
| Cümle başı kayış (scoop) | düzeltilir | düzeltilir | 70 ms serbest |
| PITCH CORRECTION, VIBRATO, KEY / SCALE | her konumda olduğu gibi uygulanır | | |

Ara değerler iki yarıda da sürekli ve monotondur (tablo: `VSTM_TUNE_AUDIT.md` 17.4).

**Akort (A4)** (1.5, ⚙ menüsü): 432–446 Hz. Nota ızgarası referansla birlikte kayar. Varsayılan 440 Hz; oturuma kaydedilir,
presetlere girmez, otomasyonsuz.

**Ünsüzleri formanttan koru** (1.5, ⚙ menüsü, varsayılan kapalı): açıkken FORMANT yalnız sesli bölümlere uygulanır; s, ş ve
nefes doğal kalır. Presetlere kaydedilir.

**Nota geçişi** (1.5, ⚙ menüsü): notalar arası kayma süresi. Auto (varsayılan) ROBOTISM'e bağlıdır, bugünkü gibi; 0 / 20 / 50 /
100 / 200 ms ise iki tarafta da sabit S-eğrili kaymadır. Presetlere kaydedilir.

**Formant transpozu izlesin** (1.5, ⚙ menüsü, varsayılan kapalı): formant transpozla birlikte kayar; ince ya da kalın ses
efekti için. Kapalıyken transpoz formantı korur.

**Note bypass** (1.5, özel dizi editörü): bir notaya **sağ tık** (Mac'te Ctrl-tık) o notayı "atla" yapar. O notaya yakın söylenen
ses düzeltilmeden geçer. Sol tık eskisi gibi notayı açar ya da kapatır.

**⚙ → Gelişmiş** (1.5; hepsi varsayılan olarak kapalı):

- nota geçişi;
- **tolerans** (5–50 sent): hedefe bu kadar yakın notalar söylendiği gibi kalır;
- **güven eşiği**: algılayıcı emin değilken düzeltme yapılmaz;
- **sentetik vibrato**: hafif / orta / geniş;
- **oktav kaydırma**: ±2 oktav;
- ünsüz koruması ve formant izleme;
- **Hedef notalar MIDI'den**: basılı MIDI notaları hedef olur, oktav fark etmez. VST3 ve Standalone'da çalışır; AU'da yok.

**64 bit host'lar:** double ses blokları desteklenir; motor 32 bitte çalışır.

Kullanıcı presetleri: `<kullanıcı veri klasörü>/VSTM Audio/VSTM Tune/Presets/*.drp`
(Windows: `%APPDATA%`, macOS: `~/Library`, Linux: `~/.config`).

- **`.drp` (1.4):** sürümlü, doğrulanan, sıkıştırılmış ikili biçim (`docs/1.4.0/VSTM_Tune_1.4.0_DRP_Format.md`). Bozuk ya da
  başka bir eklentinin dosyası yüklenmez, hiçbir şeyi değiştirmez.
- **Eski presetler:** 1.0–1.3 `*.vstmpreset` dosyaları okunmaya devam eder; yeniden adlandırılınca `.drp` olur.
- **1.3 ve öncesi** `.drp` okuyamaz.

## Natural Auto Correction ve Auto Character (1.6)

- **Doğal düzeltme** (Robotism %25'ten itibaren devreye girer, %50'den itibaren tam; hard tune değişmez):
  - vibrato sürerken nota vibratosuz merkezden seçilir;
  - yalnız merkez düzeltilir, vibrato geçer;
  - insansı tarafta geçişler şarkıcının portamentosunu izler;
  - analiz adımında oktav sıçraması 40 ms beklenir.
- **Auto Character** (ROBOTISM başlığının solundaki **AUTO**):
  - **Açık:** ROBOTISM, Retune Speed, Humanize ve Vibrato Scale'i yönetir. Bu kontroller "AUTO" rozetli ve kilitlidir; %50'de fabrika değerlerini verir.
  - **Kapatınca:** o anki değerler manuel değer olarak kalır, ses değişmez.
  - Otomasyonlu ve presete girer; varsayılan kapalı.
- Ölçümler ve davranış değişiklikleri: `docs/1.6.0/VSTM_Tune_1.6.0_Changelog.md`. **Dinleme yapılmadı.**

## Admin yetkisi ve geliştirici paneli (1.4)

- **Nasıl açılır:** Preset menüsü → **Yetki dosyası içe aktar…** ile imzalı bir yetki dosyası (`.drp`, Ed25519) yüklenince,
  ⚙ menüsünde **Geliştirici paneli** görünür.
- **Panelde:**
  - canlı tanılama: giriş / çıkış perdesi, hedef nota, algılayıcı kesinliği, analiz ve eklenti gecikmesi;
  - uzman motor anahtarları: varsayılan açık, kaydedilmez.
- **Kapsam:** Yetki yalnız bu eklentinin içinde geçerli; işletim sistemine, DAW'a ya da dosya sistemine hiçbir yetki vermez.
- **Doğrulama:** Eklentide yalnız açık anahtar var. Dosya açılışta ve dakikada bir yeniden doğrulanır. Preset menüsü →
  **Yetkiyi kaldır** ile silinir.
- **Ayrıntı:** `docs/1.4.0/VSTM_Tune_1.4.0_Authorization.md`. Dosya verme aracı: `tools/drp_auth.py`.

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
- **Oktav kararı tam hızda verilir:** perdenin P mi 2P mi olduğu, seyreltilmiş YIN eğrisiyle değil tam hızdaki sinyalin
  periyodikliğiyle seçilir. Eskiden legato bir yukarı oktav sıçramasında (soprano A4 → A5) algılayıcı notanın tamamında bir
  oktav aşağıda kalıyor, çıkışa f0/2'de −45 dB'lik bir alt oktav ekleniyordu; şimdi f0/2 kuru sesle aynı düzeyde (−78 dB).
  Gerçek vokallerde (145 kayıt) oktav hatası %0,81 → %0,21, ≥ 30 ms süren hatalı bölümler 44 → 16. Tam hız ince ayarı
  artık kaba tahminin dışına çıkan perdeyi de izler: Bass aralığında vibrato tahmini 4 kat pürüzsüz, hızlı bir geçişte
  hard-tune artık aradaki notada takılmaz (sentetik ±100 ct vibratoda izleme hatası 16–23 ct → 1–2 ct).
- **Perde zamanlaması (1.2):** ince ayar YIN çerçevesinin ortasında yapılır. Tahmin ve zaman damgası aynı sesi anlatır; 1.0 / 1.1'de
  ~17 ms kaymıştı. Perdesi kesin bilinen yeniden sentez setinde (145 gerçek kaydın perde eğrisi): kaba hata %1,47 → %0,16,
  ince hata p95 65 → 3 ct. Süren bir ses, perde ve seviye sürdükçe voiced kalır: Bass aralığında ±200 ct 6,5 Hz vibratoda
  voiced %66 → %95. Kısa periyot denetimi yalnız Soprano / Alto'da çalışır; E6 gibi notalarda 2P / 4P hatasını azaltır.
- **Transpoze ve formant seviyesi dengeli:** TD-PSOLA kaydırmayla seviyeyi değiştirir (oktav yukarı: −2…−9 dB, oktav aşağı /
  formant yukarı: +1,9 dB). Transpoze ya da formant kullanılırken kaydırılmış ses kuru sesin gücüne eşitlenir; yalnız saf
  ünlülerde ölçülür, ünsüz / nefes / sessizlik yükseltilmez, yukarı kaydırmada kısa tanecik patlamaları kuru sesin +4 dB
  üstüne çıkamaz. 50 gerçek kayıtta 18 ayarın hepsinde her dosya ±0,9 dB içinde (önce −7,9 / +2,4 dB'ye kadar); 145 kayıtta
  ±12 transpoze / formant'ta 145 dosyanın 144'ü ±1 dB içinde (aşan: konuşma ve vocal fry, en çok 1,3 dB).
  Transpoze ve formant 0 iken çıkış bit bit eskisiyle aynıdır. 1.2: eklenen kazanç, kaydırılmış sesin ~20 ms'lik tepe
  penceresini kuru sesin +2 dB'sinin üstüne çıkaramaz. Bu gizli bir limiter değildir: kazanç 1'in altına inmez. Yan etkiler için
  bkz. Bilinen sınırlar.
- **Nota seçimi titremez:** yeni nota ancak histerezisi (15 cent, İnsansı'da 35 cent) geçip kısa bir süre kararlı kalınca
  seçilir (Robotik 14 ms, Dengeli 22 ms, İnsansı 52 ms); sesin yeni notaya açıkça vardığı sıçramalarda (≥ 1.5 yarım ses) hemen.
  İki notanın tam ortasında söylenen seste hedefin her 3–10 ms'de bir gidip gelmesi ("trill") böylece önlenir:
  gerçek vokallerde 40 ms'den kısa nota bölümleri 3–10 kat azaldı. 1.2: yeni notanın 30 cent'ine gelen ses beklemeden varmış
  sayılır. Tutulan notada KEY / SCALE değişirse robotik tarafta da en az 25 ms kayılır (tık yok).
- **Hece başlarında "güm" yok:** ünsüzden ünlüye geçişte (unvoiced → voiced) zamana hizalı unvoiced akış 4 ms daha
  sürer ve perdeye senkron voiced akışla çapraz geçiş yapar. Eskiden iki tanecik türü arka arkaya dikiliyordu;
  ünlünün ilk periyodu %50–70 seviyeye düşüyor, yarım kalan glottal darbe alçak frekanslı bir tık yapıyordu.
  Ölçüm (6 gerçek vokal, 540 geçiş): bu geçişlerdeki tık medyanı düzeltme modlarında 12–19 dB düştü,
  −45 dBFS'i aşan tıklar yarıya indi (40 → 22).
- Düzeltme 0 iken plugin şeffaftır (çıkış = giriş, 61 dB SNR) ve dry sinyalle örneğine kadar hizalıdır; MIX'te faz kayması olmaz.
- Motor blok boyutundan bağımsızdır (64 veya 1013 örneklik bloklar bit-bit aynı çıkışı verir), ses iş parçacığında kilit / bellek ayırma yoktur.
- **Bozuk veriye karşı korumalı:** girişteki NaN / Inf örnekleri (bozuk bir önceki plugin) sessizliğe çevrilir, düzeltme
  hemen kaldığı yerden sürer. Oturum veya preset dosyasındaki `nan` / `inf` değerler parametrenin varsayılanına döner;
  motor sayı olmayan bir parametre değerinde son geçerli değeri kullanır. Normal seste çıkış bit-bit aynıdır.
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

**macOS kurulum paketi:** `VSTM-Tune-macOS` içindeki **`VSTM Tune Kurulum.pkg`** dosyasına çift tıkla: sihirbaz VST3'ü
`/Library/Audio/Plug-Ins/VST3`'e, AU'yu `/Library/Audio/Plug-Ins/Components`'e, uygulamayı `/Applications`'a kurar
("Özelleştir" ile seçilebilir, yönetici parolası ister). Paket Apple Developer ID ile imzalı / noter onaylı değil:
macOS ilk açılışta engellerse *Sistem Ayarları → Gizlilik ve Güvenlik → Yine de Aç*. Paketi `installer/macos/build_pkg.sh`
üretir (bir Mac'te: `installer/macos/build_pkg.sh build/VSTMTune_artefacts/Release 1.0.0 "VSTM Tune Kurulum.pkg"`);
CI her derlemede paketi macOS makinesine gerçekten kurar, kurulan VST3'ü host gibi yükler ve AU'yu `auval` ile doğrular.

## Testler

```bash
cmake -B build -DVSTM_BUILD_TESTS=ON
cmake --build build --config Release
./build/VSTMTune_EngineTest                                  # ses motoru (44.1 / 48 / 96 kHz)
./build/VSTMTune_PluginTest_artefacts/Release/VSTMTune_PluginTest goruntuler --vst3 "build/VSTMTune_artefacts/Release/VST3/VSTM Tune.vst3"
```

- **EngineTest:** hard-tune doğruluğu, seviye, şeffaflık + gecikme hizası, ±12 / +7 transpoze, formant, blok boyutu
  bağımsızlığı, ünlü başlangıcında genlik çukuru olmaması, Robotik / İnsansı geçiş süresi ve vibrato, iki nota
  arasında nota titremesi olmaması, otomatik ton (C majör) ve güveni (tek / iki nota, A minör, modülasyon, geçiş notaları),
  legato oktav sıçramaları, transpoze / formant seviyesi, özel dizi + Auto, Adaptive, sessizlik.
- **Gerçek vokal matrisi:** `tests/run_vocal_matrix.py` bir manifestteki kayıtları her ayarla WavProbe'dan geçirir ve
  bağımsız referansa (pYIN + YIN, `tests/pitch_eval.py`) göre ölçer: oktav / kaba hata, voiced doğruluğu, seviye,
  LF patlamaları, kuru sesin üstüne çıkan patlamalar, hedef sapması; önceki sürümle yan yana tablo verir.
  `tests/robotism_sweep.py` ROBOTISM'i ses gruplarına göre tarar. Kayıtlar depoda değildir (lisans); manifest yol verir.
  ```bash
  python3 tests/run_vocal_matrix.py --manifest vokaller.json --probe build/VSTMTune_WavProbe --out sonuc \
          --settings hard,default,tr+12 --baseline eski/summary.json
  ```
- **DAW testi:** `daw_test/` – test sinyalleri, REAPER betiği, FL Studio adımları ve render'ları ölçen araç (README orada).
- **WavProbe + vocal_metrics.py:** gerçek bir kaydı motordan geçirir, her analiz adımını (voiced, perde, hedef nota)
  CSV'ye yazar ve ölçer: nota titremesi, çıkış–hedef sapması, cent dağılımı, 20–150 Hz patlamaları, seviye.
  ```bash
  ./build/VSTMTune_WavProbe vokal.wav cikis.wav --csv adimlar.csv key=-1 robotism=0 retune=0
  python3 tests/vocal_metrics.py vokal.wav cikis.wav --csv adimlar.csv
  ```
  Çıkış, motor gecikmesini korur ve sonuna gecikme kadar sessizlik eklenir (girişin sonu kesilmez).
  Yalnız PCM 16 / 24 / 32 bit ve 32-bit float WAV okunur; bozuk veya kısa chunk'lı dosyalar reddedilir.
- **AuditProbe:** `VSTM_TUNE_AUDIT.md` bölüm 6'daki ölçümleri (S1–S17) sentetik sinyallerle yeniden üretir:
  büyük sıçramalar, NaN / Inf, REALTIME geçişi, örnekleme hızları, blok boyutları, stereo, otomasyon stresi,
  CPU, 10 dakika, unvoiced, Auto key (kilit ve modülasyon izleme), formant / transpoze, MIX, ROBOTISM taraması
  (0 / 10 / 25 / 35 / 50 / 65 / 80 / 90 / 100), ROBOTISM'in bas / tenor / alto / soprano'da taraması (S16),
  30 dakika + bellek, otomasyon, ton istekleri ve REALTIME geçişleriyle (S17). Karar vermez, ölçer:
  ```bash
  ./build/VSTMTune_AuditProbe olcumler            # hepsi;  tek senaryo: ... olcumler S2 S3
  ```
- **PluginTest:** parametre varsayılanları ve yazıları, 7 dilin eksiksizliği ve diller arası değer okuma, gecikme bildirimi (REALTIME 13 / 15 / 17 ms aralığa göre, HQ 40 ms), bypass, oturum kaydet / geri yükle,
  Auto Key kilidinin oturumda ve `prepareToPlay`'de korunması, bozuk oturum değerleri, presetlerin tonu koruması, yeniden adlandırma,
  preset / ton / dizi değişiminde tık, KEY kutusundaki yüzdenin motorla aynı olması, klavye ve host gesture'ları, editör;
  `goruntuler/` klasörüne ekran görüntüleri (her dil için `07_lang_xx.png`); `--vst3` ile derlenmiş VST3'ü bir DAW gibi yükleyip sınar.
- **Dinleme paketi:** `tests/make_listening_pack.py` kör, seviye (-20 LUFS) ve gecikme eşitlenmiş çoklu karşılaştırma paketleri,
  cevap anahtarı ayrı, Türkçe dinleme formu ve `feedback_form.csv` (1–5 puanlı 10 ölçüt + zaman damgalı not) üretir.
- **1.2 ölçüm araçları** (komutlar: `docs/1.2.0/VSTM_Tune_1.2.0_Test_Instructions.md`):
  - `gt_corpus.py` / `gt_eval.py`: perdesi kesin bilinen setler ve puanlama,
  - `gt_variants.py`: örnekleme hızı, kazanç, gürültü, stereo,
  - `peak_check.py`: seviye ve tepe ayrı,
  - `VSTMTune_RtSwitchProbe` + `rt_switch_eval.py`: REALTIME ↔ HQ bütünlüğü,
  - `transition_eval.py`: nota karar zamanlaması,
  - `autokey_eval.py`: etiketli ton seti,
  - `VSTMTune_BenchProbe`: CPU, gecikme, bellek ayırma.

## Bilinen sınırlar

- Plugin imzasızdır: Windows "bilinmeyen yayıncı", macOS "doğrulanamadı" uyarısı verebilir (`KURULUM.txt`).
- **Auto Key temkinlidir ve yalnız vokali duyar:** ayırt edici kanıt yoksa (pentatonik ezgi, üçlü arpej, rap) kilitlemez ve en yakın
  yarım sese düzeltir; 145 kaydın 21'i kilitlendi. Beat'in tonunu biliyorsan KEY'den seç. Beat / sidechain analizi yok (DAW'da
  doğrulanamadığı için eklenmedi).
- Bu ortamda gerçek bir DAW içinde denenmedi (REAPER / FL Studio yok); derlenmiş VST3 JUCE'un VST3 host'uyla yüklenip
  test edildi. DAW testi için kit: `daw_test/README.md` (REAPER betiği de REAPER'da çalıştırılmadı).
- **Adaptive deneyseldir** ve hiçbir insan dinlemesiyle doğrulanmadı. İnsansı uçta vibratonun yaklaşık yarısını siliyor (ölçüldü,
  1.2 raporu 5.3). Özel dizi düzenleyicisinin açılır penceresi yalnız ekran görüntüsüyle (bileşen tek başına) denetlendi; DAW
  içinde açılışı denenmedi.
- Algılayıcı aralığın üstündeki notaları bir oktav aşağıda bulur: Tenor / Mid ≈ C6'ya (1,05 kHz), Soprano / Alto ≈ F6'ya
  (1,4 kHz) kadar. Daha yüksek notalar için Soprano / Alto seç.
- Bass aralığında çok hızlı (≤ 20 ms) küçük üçlü kaymalarda algılayıcı ~30 ms "voiced değil" der; çıkış o anda şarkıcının
  kendi kaymasını izler, İnsansı taraftaki S-eğrisi kayma oluşmaz (60 ms'lik kaymada olmuyor). Sentetik ölçüm.
- Ani biten cümlelerde çıkış, kuru sesin sönümünden 10–20 ms daha uzun sürebiliyor (düzeltme kapalıyken de). 3 dakikalık bir
  rap vokalinde, hard ayarda kuru sesin 6 dB üstüne çıkan 70 adet 10 ms karenin 70'i de cümle sonu / sönümde; yumuşak
  biten şarkı kayıtlarında (VocalSet) hiç yok. Ölçüldü, dinlenmedi; açık iş.
- Monofonik vokal içindir (akor / koro düzeltmez). Çok kalın bas seslerde REALTIME'ı kapat veya ⚙ → Vocal range → Bass.
- **Büyük transpozede düşük seviyeli alçak frekans:** düzeltme modlarında (Robotik / Dengeli / İnsansı) 20–150 Hz'de
  kalan enerji ≈ −47 dBFS'tir; geniş transpozede TD-PSOLA geçici bölgelerde hâlâ alçak frekanslı enerji üretir
  (−3 st ≈ −42 dBFS, −12 st ≈ −30 dBFS tepe). Düzeltme kapalıyken yoktur. Ölçülerek doğrulandı; insan kulağıyla
  değerlendirilmedi.
- **Büyük yukarı kaydırmada (≥ +6 st) tanecik patlamaları:** üst üste binen tanecikler 10–30 ms boyunca aynı faza gelip sesi
  kendi düzeyinin 10–16 dB üstüne çıkarabiliyor (seviye dengelemesi olmadan da). Dengeleme bunları 10 ms'lik pencerelerde
  kuru sesin +4 dB'si ile sınırlar ama kaldırmaz; tek tek örnek tepeleri bunu geçebilir. TD-PSOLA'nın yapısından.
- **Seviye dengelemesinin yan etkileri (yukarı transpoze / aşağı formant):** seviye geri geldiği için tepe de yükselir; sıcak
  kayıtlarda çıkış 0 dBFS'i geçebilir (145 kaydın 4–6'sında; eklentide limiter yok, gerekiyorsa arkasına limiter koy).
  Seviye eşitlenmiş ölçümde transpoze +12'de tepe kuyruğu (p95) +2,6 dB, formant −12'de +1,3 dB arttı; 20–150 Hz
  olayları %10–21 arttı, 10 ms patlamaların toplamı azaldı. Ölçüldü, dinlenmedi; paketler ve ayrıntı:
  `VSTM_TUNE_AUDIT.md` 16.2.
  1.2'de tepe sınırı eklendi. Seviye eşitlenmiş tepe p95 transpoze +12'de 6,9 → 5,4 dB, formant −12'de 4,6 → 3,7 dB.
  0 dBFS'i aşan dosya transpoze +12'de 5 → 3. Kaydırmasız ve aşağı kaydırmalı ayarlarda tepe p95 0,1–0,6 dB arttı.
- **Bass aralığında çok geniş ve hızlı vibrato** (±150–200 ct, 6 Hz ve üstü): 1.1'de bir yarım sesten fazla yanılıyordu. 1.2'de
  sentetik ölçümde düzeldi (> 1 st hata %19 → %0, voiced %66 → %95). Gerçek bir bas şarkıcısı kaydıyla denenmedi.
- **Hard tune geçiş zamanı (1.2):** hedef nota, şarkıcının nota değişiminden medyan 9 ms sonra değişir (1.1: 3 ms). Robotik uçta
  gerçek kayıtlarda geçiş %10–90 süresi 41 → 61 ms. Ölçüldü, dinlenmedi.
- **REALTIME'ı çalarken değiştirmek:** bildirilen gecikme hemen, ses 0,4–2 s içinde kayar. DAW'ın gecikme telafisini ne zaman
  güncellediği DAW'a bağlıdır ve hiçbir DAW'da denenmedi.
- **Bass aralığı erkek seslerde** %0,24 oktav-alçak hata verir (1.1'de de). Yalnız ~75 Hz'in altına inen sesler için seç;
  diğer erkek sesler için Tenor / Mid daha iyidir.
- **Kromatik robotik modda** iki notanın arasında söylenen geniş vibrato çıkışı iki yarım ses arasında titretebilir (trill).
  Şarkının ton ve dizisini seçmek bunu azaltır.
- Tutulan alçak bir notanın içindeki ünsüzlerde algılayıcı arada bir yanlış kısa periyot seçer (1.0'dan beri); hard tune'da
  orada ~10 ms'lik kısa bir tepe oluşabilir.
- **Çok yüksek örnekleme hızında çok küçük tampon** (176,4 / 192 kHz, 32 örnek): analiz içeren bloklar (her ~17 blokta bir)
  blok bütçesinin ~%100'ünü kullanır (1.1'de de; ortalama yük düşük). Bu ayarda takılma olursa tamponu 64+ örnek yap.

## Lisanslar

Arayüz yazı tipi **Inter** (SIL Open Font License 1.1) — `Resources/fonts/OFL-Inter.txt`.
