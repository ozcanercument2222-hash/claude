# VSTM Tune 1.3.0 – Kalite raporu (aday 3: P0 işleri + REALTIME + doğal vibrato)

Temel: 1.2.0 (kaynak zip SHA-256 `b0651f0e…a2af9a`).

- **Aday 1** = 1.2.0 + iki yama (bölüm 2–5).
- **Aday 2** = aday 1 + REALTIME güncel analiz (bölüm 8). HQ çıkışı aday 1 ile bit bit aynı.
- **Aday 3** = aday 2 + insansı tarafta vibrato koruması (bölüm 9). Nota kararları aday 2 ile aynı; %0–%50 bit bit aynı.

**Bu rapor yayın raporu değil.** Yol haritasının P0 maddeleri ele alındı ve REALTIME'ın kalitesi iyileştirildi. P1 maddelerinin
çoğu, DAW testleri, kör dinleme ve gerçek donanımda CPU ölçümü henüz yapılmadı; durumları bölüm 6'da.

Kanıt etiketleri: **MEASURED** = ölçüm (bu ortamda, Linux, paylaşılan sanal makine). **INSPECTED** = kaynak incelemesi.
Bu adayda dinleme (USER-REPORTED) ve DAW sonucu **yok**. Ses kalitesinin arttığı iddia edilmiyor; ölçümler ve dinleme paketi var.

---

## 1. Temel ve yöntem

- **Geri dönüş noktası:** 1.2.0 kaynak zip'i ve dosya manifesti (69 dosya, SHA-256). Ayrıntı: `VSTM_Tune_1.3.0_Rollback.md`.
- **Çalışma ağacı:** 1.2.0 zip'iyle birebir aynı (`diff -r`: fark yok).
- **Derleme:** 1.2.0 ikili dosyalarını byte byte yeniden üretiyor (`bin-s8` ile `cmp`). Bu yüzden 1.2.0 sayıları aynı araçlarla
  doğrudan karşılaştırılabilir.
- **Kayıt yeri:** `artifacts/audio-runs/2026-11-24_q13/`. Yamalar, ara derlemeler (`bin-p1` … `bin-cand1`), ölçümler, araçlar ve
  günlükler orada.
- **Her yama tek değişiklik:** Yeniden derlendi, aynı girdilerle yeniden ölçüldü, 1.2.0 ile karşılaştırıldı.
  Yanlış nota değişimini artıran adaylar reddedildi.

### Yeni ölçümler (araçlar `tests/` altında)

**`transition_decompose.py` – geçiş zamanlamasını ikiye ayırır:**

- **Karar zamanı:** Motorun hedef notayı değiştirdiği an. 1.2.0 raporundaki ölçüt buydu; hop kaydından alınır.
- **Duyulan zaman:** Çıkış sesinin perdesinin yeni notaya geçtiği an. YIN, çerçeve ortalı, 0,5 ms adım; yeni nota 10 ms sürmeli.
- **Kalibrasyon:** Düzeltme kapalıyken (`amount=0`) aynı ölçüm HQ'da +1,3 ms, REALTIME'da +3,7 ms veriyor.
  Duyulan değerler bu sapmayı içeriyor.
- **Temiz geçiş:** İki nota da ≥ 80 ms tutuluyor ve ses 40 ms içinde yeni notanın 30 ct yakınına varıyor.
  - Bu tanımla 47 geçişin 43'ü temiz.
  - Kalan 4'ü iki notanın tam ortasında biten 50 ct kaymalardır. Burada geçişin "doğru zamanı" tanımlı değil ve eski ölçüt −79 ms
    gibi yapay değerler veriyordu.
  - Eski ölçüt de ayrıca raporlanıyor; hiçbir değer gizlenmedi.

**`false_switch.py` – olmaması gereken hedef nota değişimleri, kesin doğru sentetik set (77 dosya):**

- **Sabit notada:** Söylenen nota ±100 ms boyunca değişmiyorken.
- **Vibratoda:** Vibratosuz nota sabitken.

**`hot_peaks.py` – sınırlayıcı kapalı / açık karşılaştırması, 145 gerçek kayıt:**

- Kayıtlar iki seviyeye ölçeklenir: tepe −1 dBFS ("sıcak") ve tepe −6 dBFS ("normal"). Girdi dosyalarına yazılmaz.
- Ölçülenler: örnek tepesi, gerçek tepe (4× aşırı örnekleme), 0 dBFS üstü örnekler, kazanç azaltma, değişen örnek oranı,
  seviye değişimi.

---

## 2. P0 – Hard tune nota geçişleri

### 2.1 Kök neden

**Algılayıcı zamanında (MEASURED).** Algılanan perde orta noktayı gerçekten medyan +1,3 ms sonra geçiyor.

**Gecikme kararda (MEASURED).** Karar medyan +8 ms ekliyor. Kaynağı histerezis (7,5 ct, orta noktanın ötesinde) ve
bekleme süresi (14 ms; ses yeni notaya 30 ct yaklaşınca hemen geçilir).

**1.2.0 raporundaki +9,2 ms karar zamanıdır (INSPECTED + MEASURED).** Duyulan çıkış daha geç:

| | HQ | REALTIME |
|---|---|---|
| Duyulan medyan | 11,2 ms | 18,7 ms |
| 15 ms'den geç | %28 | %61 |

**REALTIME'daki ek gecikme yapısaldır (INSPECTED).** Her grain kendi perdesinden yaklaşık bir periyot önce yerleşir. Bu anda
analiz o içeriğe henüz ulaşmamıştır. Analizin merkez gecikmesi tenor için ~22,5 ms, bas için ~26 ms; REALTIME gecikmesi ise
20 ms. Bu yüzden REALTIME'da grain en yeni (eski) kararı kullanır ve tenor için ~7 ms, bas için ~11 ms ek gecikme oluşur.

### 2.2 Alınan değişiklik: geri tarihli geçiş (yalnız HQ, yalnız anlık düzeltmede)

Karar kesinleştiğinde, ses orta noktayı geçtikten sonraki ve henüz grain'e dönüşmemiş analiz kayıtları da yeni notayı alır.
Kararın kendisi değişmez.

43 temiz geçiş (MEASURED):

| | 1.2.0 | 1.3 aday 1 |
|---|---|---|
| Karar zamanı medyan / \|p95\| | 9,7 / 30,4 ms | 9,7 / 30,4 ms (aynı) |
| **HQ duyulan medyan / \|p95\|** | 11,2 / 37,8 ms | **6,3 / 29,7 ms** |
| HQ 15 ms'den geç | %27,9 | **%14,0** |
| REALTIME duyulan medyan / \|p95\| | 18,7 / 48,0 ms | 18,7 / 48,0 ms (bit bit aynı çıkış) |
| Sabit notada yanlış değişim (dk) | 29,1 | 29,1 (aynı) |
| Vibratonun trile dönmesi (dk) | 664,8 | 664,8 (aynı) |

**HQ'da duyulan zamanlar türlere göre (1.2.0 → 1.3, ms):**

| Tür | 1.2.0 | 1.3 aday 1 |
|---|---|---|
| Hızlı koşu | 5,7…9,3 | −0,9…6,3 |
| Tenor / soprano 60 ms kayma | 8…12 | 1…7 |
| Soprano 200 ms kayma | 25…29 | 7…11 |
| Tenor 200 ms kayma | 12…30 | 8…26 |
| Erkek melodisi (vibratolu) | 12…14 | 9…11 |
| Bas 60 ms kayma | 11…15 | 11…12 |
| Bas 200 ms kayma | 24…39 | 20…30 |

Bas ve yavaş kaymalar az iyileşiyor. Bas sesde çalınmamış kayıt penceresi küçük: 40 ms gecikmeden ~26 ms analiz gecikmesi ve
~9 ms'lik bir grain çıkınca ~5 ms kalıyor.

**Hedef karşılaştırması (yol haritası: medyan 0–5 ms, p95 ≤ 15 ms):**

- **HQ:**
  - Medyan sapma düşülünce ~5 ms; sınırda.
  - p95 29,7 ms; tutmuyor. Neden: bas ve 200 ms kaymalar.
- **REALTIME:**
  - Tutmuyor ve bu turda değişmedi.
  - Takas belgelendi: 0–5 ms için ya REALTIME gecikmesi artmalı ya da analiz penceresi kısalmalı. İlki yasak, ikincisi bas
    algılamasını riske atar.

### 2.3 Reddedilen adaylar (MEASURED; hepsi kararı hızlandırır, hepsi yanlış değişimi artırdı)

| Aday | Karar medyanı | HQ duyulan | REALTIME duyulan | Sabit notada yanlış (dk) | Vibrato trili (dk) |
|---|---|---|---|---|---|
| 1.2.0 | 9,74 | 11,15 | 18,67 | 29,1 | 664,8 |
| A: histerezis 6 ct + 2 hop + geri dönüş koruması | 6,04 | 8,67 | 14,15 | **75,5** | 694,0 |
| B: A'sız, geri tarih + "kararlı hareket" kısayolu | 9,76 | 6,33 | 20,65 | **39,1** | 668,7 |
| C: geri tarih + bekleme süresi 2 hop | 8,05 | **5,65** (p95 15,6) | 17,17 | **52,9** | 678,4 |
| **Alınan: yalnız geri tarih** | 9,74 | 6,33 | 18,67 | 29,1 | 664,8 |

C, HQ hedefini neredeyse tutturuyor, ama yanlış değişimi %80 artırıyor:

| Set | 1.2.0 (dk) | C (dk) |
|---|---|---|
| Vocal fry / creak | 303 | 445 |
| Gürültülü | 58 | 77 |
| ±50 ct vibrato | 0 | 44 |

Yol haritası ("no measurable increase in false note switching") bunu yasaklıyor. Bekleme süresi bu tür seslerde tam olarak bu
korumayı sağlıyor.

### 2.4 1.2.0'dan beri var olan yanlış değişimler (MEASURED, değişmedi)

- **Vocal fry ve creak** (48–90 Hz, düzensiz darbeler): sabit notada dakikada ~300 yanlış değişim (sy_bass_fry, sy_bass_creak).
  Hard tune bu seste titrer.
- **Kromatik robotik modda geniş vibrato** (±100 / ±200 ct) nota triline dönüşüyor. Bu, 1.2'nin bilinen sınırı 5.

---

## 3. P0 – Tepe güvenliği

### 3.1 İz sürme (MEASURED, 145 kayıt, sınırlayıcı kapalı = 1.2.0 davranışı)

En büyük tepe artışı (kuruya göre +5…+7 dB) transpoze olmadan bile oluşuyor:

- Kaynak: PSOLA yeniden sentezi.
- En kötü dosyalar: kalın erkek sesleri (m6, m7, m11: vibrato, düz, konuşma) ve vocal fry.
- Olası neden (INSPECTED, doğrulanmadı): işaretler (pitch mark) darbenin tam üstünde değilse, komşu grain'lerdeki darbeler üst
  üste gelip toplanıyor. 1.2 raporu bu artışı adım 2'ye bağlamıştı.
- Formant +12 ve transpoze −12 bunun üstüne her dosyada tepeyi yükseltiyor.

**Sıcak giriş** (tepe −1 dBFS):

| Ayar | 0 dBFS'i aşan dosya (kapalı → açık) | En büyük örnek tepesi (kapalı → açık) | Açıkken gerçek tepe (en büyük / p95) | Sınırlayıcının çalıştığı dosya | En büyük / medyan kazanç azaltma |
|---|---|---|---|---|---|
| hard | 20 → **0** | +6,01 → −1,00 dBFS | +0,18 / −0,87 dBTP | 58 | 7,0 / 0,15 dB |
| varsayılan | 14 → **0** | +4,70 → −1,00 | −0,23 / −0,90 | 61 | 5,7 / 0,09 |
| natural | 8 → **0** | +1,76 → −1,00 | −0,41 / −0,92 | 70 | 2,8 / 0,12 |
| transpoze +12 | 25 → **0** | +7,91 → −1,00 | −0,24 / −0,84 | 41 | 8,9 / 1,37 |
| transpoze −12 | 144 → **0** | +5,35 → −1,00 | +0,32 / −0,80 | 145 | 6,4 / 3,35 |
| formant +12 | 144 → **0** | +7,89 → −1,00 | **+1,02** / −0,47 | 145 | 8,9 / 3,48 |
| formant −12 | 25 → **0** | +4,11 → −1,00 | −0,85 / −0,93 | 46 | 5,1 / 1,12 |
| tr +7 / fm +12 | 125 → **0** | +8,56 → −1,00 | +0,58 / −0,54 | 141 | 9,6 / 1,76 |
| hard + tr +12 | 30 → **0** | +9,58 → −1,00 | −0,41 / −0,83 | 57 | 10,6 / 1,23 |

**Normal seviye** (tepe −6 dBFS):

| Ayar | 0 dBFS'i aşan dosya, sınırlayıcı kapalı | Sınırlayıcının çalıştığı dosya | En büyük kazanç azaltma |
|---|---|---|---|
| natural | 0 | **0** (bit bit aynı) | 0 |
| varsayılan | 0 | 2 | 0,7 dB |
| hard | 5 (+1,0 dBFS'e kadar) | 8 | 2,0 dB |
| transpoze +12 | 3 (+2,9) | 4 | 3,9 dB |
| formant +12 | 9 (+2,9) | 12 | 3,9 dB |

### 3.2 Seçilen yöntem ve gerekçe

**Neden gecikmesiz bir sınırlayıcı.**

- Bildirilen gecikme değişmemeli; ileriye bakan (lookahead) bir sınırlayıcı ek gecikme ister.
- PSOLA'nın OLA tamponu, çıkış anından sonrası için tamamlanmamıştır. Bu yüzden gecikme eklemeden ileriye bakılamaz.

**Neden yumuşak kırpıcı değil.**

- Yumuşak kırpıcı (soft clip) tavanın altındaki tepeleri de bükerdi; "yalnız gerektiğinde" şartına uymaz.
- Sınırlayıcı ise tavanın altında kazancı tam 1 tutar.

**Neden MIX'ten önce.** Bypass ve karışımın kuru kısmı her zaman girişin kendisi kalır. İlk sürüm sınırlayıcıyı karışımdan sonra
uyguluyordu ve bypass'ta sıcak kuru sesi kısıyordu; EngineTest bunu yakaladı ve düzeltildi.

**Bilinen sınırlar:**

- **Gerçek tepe (MEASURED):** Örnek tepesi korunuyor, örnekler arası tepe korunmuyor; formant +12'de +1,0 dBTP'ye kadar.
  Gerçek tepe tavanı için 2× aşırı örneklemeli tepe tahmini gerekir. Bu, CPU ve dinlemeyle değerlendirilmeli.
- **Bırakma süresi (INSPECTED):** Sıcak girişte sınırlayıcı çalışan dosyalarda örneklerin yarısına yakınına dokunuyor (bırakma
  80 ms). Değişikliğin enerjisi sinyalin −24…−53 dB altında.
- **Pompalama ve donukluk:** Ölçümle kanıtlanamaz. **Dinlenmeli (paketler L01–L05).**
- **Kök neden çözülmedi:** PSOLA tepe artışının kendisi 1.3'ün sonraki adımında incelenmeli. Sınırlayıcı yalnız güvenlik ağıdır.

---

## 4. Regresyon kapıları (aday 1, MEASURED)

| Kapı | Sonuç |
|---|---|
| EngineTest | **76 / 76** (1.2'nin 71'i + 5 yeni: sınırlayıcı şeffaflığı, sıcak girişte tavan, bypass'ın saf kalması, REALTIME'ın değişmemesi, HQ geçiş zamanı 7 → 2 ms) |
| EngineTest, ASan + UBSan | geçti, 0 bulgu |
| PluginTest (VST3, JUCE host'unda) | **73 / 73**: parametre kimlikleri, sırası, aralıkları, 1.0 / 1.1 / 1.2 oturumları, bozuk durum |
| pluginval 1.0.4, seviye 10 | SUCCESS (25 grup) |
| REALTIME ↔ HQ geçişi | 132 durum: tekrar / atlama 0, perde sapması 0,01 ct, en büyük adım oranı 1,59 (1.2.0 ile aynı) |
| Perde algılayıcı | Kod değişmedi; hop kayıtları 1.2.0 ile bit bit aynı (3 dosya × 2 mod). Kesin doğru setler (145 + 77 + refgt) bu yüzden değişmedi; tekrar koşulmadı |
| Yanlış nota değişimi (77 sentetik) | 1.2.0 ile aynı |
| Varsayılan / doğal / REALTIME çıkışı (normal seviye) | 1.2.0 ile bit bit aynı (3 dosya × 3 ayar × 2 mod; natural'da 145 dosyanın hiçbirinde sınırlayıcı çalışmadı) |
| CPU | `bench/` (bölüm 5) |

---

## 5. CPU

Kaynak: `bench/v120_matrix.csv`, `bench/cand1_matrix.csv` (BenchProbe matrix, 10 s), aynı paylaşılan sanal makine. Bu göreli
bir karşılaştırma; mutlak değerler bir DAW makinesini temsil etmez.

Sonuçlar (MEASURED):

- **Blok başına ortalama süre:** 216 yapılandırma (44,1 / 48 / 96 kHz, blok 32–1024, mono / stereo, REALTIME / HQ, ayarlar).
  Aday 1 / 1.2.0 oranının medyanı **0,994** (en az 0,56, en çok 1,86). Uç değerler sanal makinenin gürültüsü; ölçülebilir bir
  gerileme yok.
- **process() içinde bellek ayırma:** iki sürümde de 0.
- **Ek iş (INSPECTED):** Sınırlayıcı örnek başına birkaç karşılaştırma yapıyor. Geri tarih, nota değişiminde en fazla ~5 kaydı
  düzeltiyor.
- **Test aracı hatası bulundu ve düzeltildi.** BenchProbe'un `denormal` modu (1.2.0'dan beri) son blokta tampon sonunun 128 örnek
  ötesini işliyordu. Bu bir heap taşmasıydı; ASan buldu, çıkışta "double free" veriyordu. Eklentiyi etkilemiyor, yalnız test
  aracındaydı. Düzeltilmiş araçla `denormal` ASan altında temiz.
- **Gerçek donanımda en kötü blok süresi ölçülmedi.**

---

## 6. Yol haritası durumu (aday 3)

| Madde | Durum |
|---|---|
| P0 nota geçişi | HQ'da duyulan medyan 11,2 → 6,3 ms (aday 1). REALTIME'da 18,7 → 11,7 ms (aday 2, bölüm 8). |p95| hedefi (≤ 15 ms) tutmuyor: HQ 29,7 ms, REALTIME 38,0 ms (bas, yavaş kayma) |
| P0 tepe güvenliği | 0 dBFS aşımı 145 kayıtta 0. Gerçek tepe ve dinleme açık |
| P1 doğal ayar / vibrato, Adaptive | **Düzeltildi, dinlenmedi** (aday 3, bölüm 9): ROBOTISM %80'de vibrato %47 → %74, %100'de REALTIME %81 → %92, HQ %80 → %91. Nota kararları ve kayma aynı. Adaptive varsayılan kapalı kalıyor; ayrıca değerlendirilmedi |
| P1 algılayıcı regresyon koruması | HQ'da algılayıcı değişmedi (bit bit aynı). REALTIME'da kesin doğru setlerle ölçüldü (bölüm 8.3): oktav dizileri azaldı, voiced isabeti 0,02–0,2 puan düştü. Genişletilmiş kapsam (falsetto, E6, nefesli, fry) **yapılmadı** |
| P1 formant / transpoze artefaktları | Tepeler ölçüldü. Ses karakteri (metalik, fazlı ses vb.) **dinlenmedi**. PSOLA tepe artışının kökü açık |
| P1 ROBOTISM eğrisi | REALTIME'da her noktada çıkış değişti (ölçüm zamanı); insansı tarafta iki modda da (vibrato). %0 / %50 / %65 / %80 / %100 ölçüldü (bölüm 8.4, 9). Tüm aralık ve otomasyonla yeniden değerlendirme **yapılmadı** |
| P1 Auto Key gerçek kayıtla | **Yapılmadı.** Tonu bilinen gerçek kayıt seti yok |
| P1 kör dinleme | Aday 1 paketi hazır (9 paket); aday 2 için REALTIME karşılaştırmaları eklendi. **Dinlenmedi** |
| P1 DAW testleri | **Yapılmadı (NOT TESTED).** Bu ortamda REAPER, FL Studio, Ableton yok |
| P2 CPU / bellek | Sanal makinede göreli ölçüm. Gerçek donanımda **yapılmadı** |
| P2 uyumluluk | PluginTest 73 / 73 (aday 1 ve 2) |
| P2 Windows / macOS | **Aday 1** Windows'ta CI ile derlendi; motor testi ve plugin testi geçti (çalıştırma 45451522539). macOS işi makine bulunamadığı için kuyrukta kaldı. **Aday 2** commit edilmedi, Windows / macOS'ta derlenmedi |

---

## 7. Bilinen sorunlar (aday 3)

1. **REALTIME'da hard tune geçişleri hâlâ geç duyuluyor:** medyan 11,7 ms, ölçüm sapması (+3,7 ms) düşülünce ~8 ms; geri
   tarihleme REALTIME'da yapılamıyor (bölüm 2.1, 8).
2. **HQ'da bas ve yavaş kaymalarda geçiş 20–30 ms geç.**
3. **Gerçek tepe** formant +12 ve sıcak girişte +1,0 dBTP'ye kadar çıkabiliyor (örnek tepesi −1 dBFS'te tutuluyor).
4. **PSOLA tepe artışı:** Kalın erkek seslerinde kuruya göre +5…+7 dB. Sınırlayıcı yakalıyor, kök neden açık.
5. **Vocal fry / creak'te hard tune titriyor** (1.2.0'dan beri).
6. **Kromatik robotik modda geniş vibrato trile dönüşüyor** (1.2.0'dan beri).
7. **Adaptive insansı uçta vibratoyu azaltıyor** (1.2.0'dan beri; varsayılan kapalı).
8. **Geniş vibratoda hedef nota hâlâ her döngüde komşuya geçip dönüyor** (karar kuralı değişmedi). Aday 3'te bu, insansı tarafta
   vibratoyu artık düzleştirmiyor (bölüm 9). Robotik tarafta ve %50'de etkisi aynı (bkz. 6).
9. **Algılayıcı, REALTIME:** 5 dB SNR'lik gürültülü sentetik kayıtta 50 ms'den uzun bir oktav-aşağı dizisi (bölüm 8.3).
10. **Dinleme, DAW ve gerçek donanım sonucu yok.**

---

## 8. REALTIME – güncel analiz (aday 2)

### 8.1 Kök neden (MEASURED + INSPECTED)

- **Ölçümün yaşı:** Algılayıcı (YIN, seyreltilmiş) her gecikme değerini, en uzun gecikmeye göre boyutlanmış tek bir çerçevenin
  ortasında karşılaştırıyordu. Perde tahmininin yaşı (256 + tauMax) − (256 + tau) / 2 seyreltilmiş örnek, yani tenor aralığında
  ~22,5 ms, bas aralığında ~26 ms.
- **HQ:** 40 ms'lik gecikme bunu karşılıyor; düzeltme, ölçülen sese zamanında uygulanıyor.
- **REALTIME:** Gecikme 20 ms. Düzeltme, sesin kendisinden daha eski bir ölçüme göre yapılıyordu. Hard tune'da (43 temiz geçiş):
  - karar zamanı iki modda aynı (9,7 ms);
  - duyulan geçiş medyanı REALTIME'da 18,7 ms, HQ'da 11,2 ms;
  - aradaki fark kararın kuralından değil, ölçümün yaşından geliyor.

### 8.2 Değişiklik ve ara sürümler

**Son hâl (rt3 = aday 2).** Yalnız REALTIME'da, süregelen bir seste her gecikme en yeni 256 seyreltilmiş örneği kendisinden tau
önceki örneklerle karşılaştırıyor. Tahminin yaşı (256 + tau) / 2 oluyor (tenor 220 Hz: 13 ms). Merkez gecikmesi ve kısa periyot
denetimi bu yeni yaşa göre hesaplanıyor. Ek kurallar:

1. **Ses başında eski düzen.** Bir sesin başında ve ilk 8 adımında, yani süreklilik kuralı devreye girene kadar, eski düzen
   kullanılır.
2. **Sert bitişte voiced değil.** En yeni ~5 ms çerçevenin ortalamasından 14 dB'den fazla sessizse o adım voiced sayılmaz.
3. **Kayıtların zaman sırası.** REALTIME → HQ geçişinde analiz kayıtlarının zaman sırası korunur. HQ'nun analizi daha geride
   kaldığı için "gelecekte" kalan REALTIME kayıtları atılır.

| Sürüm | Fark | Sonuç (MEASURED) |
|---|---|---|
| rt1 | hizalama hep açık | Sert biten seslerde (nefes kesilmesi, ünsüz) yanlış nota değişimi. Reddedildi |
| rt2 | rt1 + kural 2 | Kısa aradan sonra oktav yukarı okuma (yeniden sentez seti: G4, 100 ms boyunca G5). Reddedildi |
| rt3 | rt2 + kural 1 | Aşağıdaki kapıları geçti. Aday 2 olarak alındı; ikilileri aday 2 ile aynı sesi üretiyor (32 / 32 render bit bit aynı) |

### 8.3 Ölçümler (1.2.0 REALTIME → aday 2 REALTIME)

| Kapı | 1.2.0 | Aday 2 |
|---|---|---|
| Hard tune, duyulan geçiş: medyan / \|p95\| / > 15 ms (43 temiz geçiş, kesin doğru) | 18,67 ms / 47,97 ms / %60,5 | **11,65 ms / 38,02 ms / %46,5** |
| Hard tune, karar zamanı: medyan / \|p95\| | 9,74 / 30,37 ms | 9,75 / 31,11 ms |
| Hard tune, gerçek kayıtta çıkış geçiş süresi (91 kayıt, ROBOTISM %0) | 61,0 ms | 40,6 ms |
| Yanlış nota değişimi, hard (77 sentetik): sabit / vibrato / tüm değişimler (dakikada) | 29,07 / 664,78 / 856,9 | 28,18 / 665,98 / 854,3 |
| Algılayıcı, sentetik 77: oktav-aşağı / kaba / oktav dizileri 20-30-50 ms | %0,070 / %0,331 / 7-4-0 | %0,062 / %0,315 / 5-2-1 |
| Algılayıcı, sentetik 77: voiced isabeti / yanlış voiced / ince hata medyanı | %99,160 / %0,253 / 4,71 ct | %99,046 / %0,369 / 4,73 ct |
| Algılayıcı, yeniden sentez 145: oktav-yukarı / kaba / oktav dizileri 20-30-50 ms | %0,071 / %0,159 / 32-14-2 | %0,057 / %0,161 / 26-6-1 |
| Algılayıcı, yeniden sentez 145: voiced isabeti / yanlış voiced / ince p95 | %99,040 / %0,031 / 3,14 ct | %99,024 / %0,094 / 3,18 ct |
| Algılayıcı, gerçek kayıt 145 (referans izleyiciye göre): kaba / oktav dizileri / voiced isabeti / yanlış voiced | %0,834 / 86-31-12 / %96,92 / %11,72 | %0,821 / 74-29-11 / %96,72 / %11,19 |
| Tepe, 145 kayıt, sınırlayıcı açık: 0 dBFS'i aşan dosya (normal ve sıcak, tüm ayarlar) | – (aday 1: 0) | 0 |
| Tepe, sıcak giriş: en yüksek gerçek tepe (formant +12) | – (aday 1: +1,02 dBTP) | +0,58 dBTP |
| REALTIME ↔ HQ geçişi (132 durum): tekrar / atlama, perde sapması | 0, 0,01 ct | 0, 0,01 ct |
| EngineTest | 71 (1.2.0) / 76 (aday 1) | **78 / 78** (+2: REALTIME'da nota değişimi 16 → 6,5 ms; HQ bit bit aynı) |
| ASan + UBSan (EngineTest ve REALTIME render) | – | 0 bulgu |
| PluginTest / pluginval seviye 10 | 73 / 73, SUCCESS | 73 / 73, SUCCESS (25 grup) |
| CPU (aday 2 / aday 1, 218 yapılandırma, medyan) | – | REALTIME 0,98, HQ 0,98; ses iş parçacığında bellek ayırma 0 |

**Algılayıcıda dikkat edilecek nokta:** sentetik setteki 50 ms'den uzun yeni oktav dizisi tek bir kayıtta, 5 dB SNR'lik gürültülü
seste (`sy_noise_snr5`). O kayıtta 20 ms'den uzun diziler 7'den 5'e, toplam oktav hatası süresi 0,224 s'den 0,198 s'ye indi;
kalan dizilerden biri uzadı. Yol haritasındaki "yeni süren oktav hatası olmayacak" koşulu bu kayıtta tam karşılanmıyor; gerçek
kayıtlarda ve yeniden sentezde ≥ 50 ms diziler azaldı (12 → 11, 2 → 1).

### 8.4 ROBOTISM taraması, REALTIME (91 gerçek kayıt, MEASURED)

| Ölçüt (ALL) | ROBOTISM | 1.2.0 REALTIME | Aday 2 REALTIME | 1.2.0 HQ (karşılaştırma) |
|---|---|---|---|---|
| Vibratonun korunan kısmı | %0 / %50 / %100 | %97,1 / %84,5 / %91,8 | %97,0 / %86,3 / **%81,0** | %96,2 / %86,2 / %80,3 |
| Nota merkezine uzaklık (medyan) | %0 / %50 / %100 | 1,5 / 18,0 / 23,5 ct | 1,6 / 15,5 / 23,3 ct | 1,7 / 15,7 / 23,4 ct |
| Çıkış geçiş süresi (medyan) | %0 / %50 / %100 | 61,0 / 63,9 / 34,8 ms | 40,6 / 52,2 / 40,6 ms | 46,4 / 52,2 / 40,6 ms |

REALTIME artık her noktada HQ'ya yakın davranıyor. Tek olumsuz fark doğal uçta vibrato: %91,8 → %81,0 (HQ 1.2.0'da %80,3).

### 8.5 Doğal uçta vibrato: kök neden ve denenen düzeltme (alınmadı)

**Kök neden (MEASURED; 1.2.0'dan beri, her modda):**

- **Sentetik:** A3'ün 25 ct üstünde, ±50 ct vibratolu bir seste hedef nota 3 saniyede 31 kez değişiyor. Bu her ayarda ve her
  sürümde aynı. ROBOTISM %80'de vibratonun %31–34'ü kalıyor. Komşu notaya yaklaşmayan ±30 ct vibratoda değişim yok ve vibrato
  ~%100 korunuyor.
- **Gerçek (4 VocalSet vibrato kaydı, tutulan notalar):** saniyede 9–26 değişim; %50, %80 ve %100'de; 1.2.0 REALTIME'da, aday 2
  REALTIME'da ve HQ'da. %80'de vibratonun %39–51'i kalıyor (1.2.0'da da).
- **Mekanizma:** 1.2'de eklenen kural (yeni notanın 30 ct yakınına gelince beklemeden geç) vibratonun tepesinde tetikleniyor.
  %100'de, motorun birebir taklidinde (aşağıda) vibrato kaynaklı değişimlerin 515 / 517'si bu yoldan geçiyor. Değişim notanın
  yaşını sıfırladığı için düzeltme de hızlanıyor ve vibratoyu bastırıyor.

**Yöntem:** Nota kararı ve düzelticinin tamamı Python'da birebir taklit edildi (`tools/decsim.py`). Doğrulama (91 kayıt, %50 /
%80 / %100): 210 360 adımın 4'ünde nota farkı; çıkış perdesi farkı < 0,001 yarım ses. Seçenekler bu taklitle elendi; seçilen
seçenek C++'ta derlenip taklitle karşılaştırıldı (fark 0 adım).

**Seçenekler (taklit üzerinde):**

- Vibrato ve yanlış değişim sütunları: 4 VocalSet vibrato kaydının tutulan notaları.
- Gerçek geçiş sütunları: 91 kaydın tamamı. Geçişler kuru sesin titreşimsiz merkezinden bulundu: 55 vibratolu kayıtta 317,
  36 diğer kayıtta 85 geçiş. Varış = çıkışın yeni notanın 30 ct yakınına gelip en az 30 ms kalması.

| Seçenek | Vibrato korunan, %65 / %80 / %100 | Yanlış değişim (sn) | Gerçek geçiş, vibratolu: varış medyanı %65 / %80 / %100 | Vibratosuz: varış medyanı |
|---|---|---|---|---|
| Şimdiki (aday 2) | %74 / %42 / %70 | 12,9 | 15 / 46 / 26 ms | 44 / 76 / 131 ms |
| Salınım koruması, sıçramalar dahil | %78 / %87 / %95 | 1,8 | 61 / 67 / 86 ms | 90 / 109 / 131 ms |
| Salınım koruması, sıçramalar serbest (`X1` yaması) | %74 / %72 / %86 | 3,7 | 17 / 41 / 38 ms | 61 / 90 / 118 ms |
| Yakın varışa bekleme (1.1 benzeri) | %74 / %31 / %80 | 7,0 | – | – |
| Vibratosuz merkezi çek (nv1) | EngineTest'te insansı kayma bozuldu (210 → 18 ms) | – | – | – |

**Salınım koruması nasıl çalışıyor:** Ses son 220 ms içinde tutulan notanın öbür yanına 25 ct'ten fazla geçtiyse, yeni notaya
gidiş vibrato sayılır ve 90 ms sürmesi beklenir. %50 ve robotik tarafta kapalı (bit bit aynı, 24 / 24 render).

**Neden alınmadı:**

- Vibratoyu en iyi koruyan seçenek, vibratolu şarkıcının gerçek nota geçişlerini 20–60 ms geciktiriyor.
- Yama hâlindeki seçenekte (`X1`) medyan değişim küçük, ama uç değerler (p95) 75–95 ms kötüleşiyor.
- "Her şey korunsun" şartıyla bu bir takas ve dinlemeden karar verilemez.
- Dinleme paketine doğal uç örnekleri eklendi.
- **Ölçüt notu:** Bu tablodaki "varış" çıkışın kendisinden ölçüldü. Vibrato korunduğunda çıkış ±50 ct salınmaya devam ettiği
  için bu ölçüt vibratoyu koruyan seçenekleri cezalandırıyor. Bölüm 9'da varış, çıkışın titreşimsiz merkezinden ölçüldü.
- Aday 3'te alınan çözüm başka (bölüm 9): kararı geciktirmiyor, yalnız düzeltme hızını koruyor.

---

## 9. İnsansı tarafta vibrato (aday 3)

### 9.1 Değişiklik

- **Dosya:** `Source/dsp/PitchCorrector.h`. Yama: `04_vibrato_keep_retune.patch`.
- **Ne yapıyor:** Bölüm 8.5'teki mekanizmada vibratoyu düzleştiren şey nota değişiminin kendisi değil, değişimin notanın "uzun
  nota" düzeltme hızını (humanize, `sustain`) sıfırlamasıydı. Her vibrato tepesinde düzeltme hızlanıyor ve vibratoyu takip ediyordu.
  - Aday 3'te, ses titreşimsiz merkezinin etrafında salınıyorsa, nota değişimi tutulan notanın düzeltme hızını korur.
  - "Salınıyor" şu demek: 2 kutuplu 3 Hz merkezin ±25 ct ötesine son 400 ms içinde en az üç kez yön değiştirerek geçti.
- **Değişmeyenler:**
  - Nota kararı (hangi nota, ne zaman) ve kayma süresi değişmedi.
  - Sıçramalar (≥ 1,5 yarım ses) her zaman sıfırlar.
  - Etki, insansı tarafın ilk %15'inde yumuşakça devreye girer: `smoothstep(0, 0.15, human)`, yaklaşık ROBOTISM %50 → %65.
- **İşlem maliyeti (INSPECTED):** adım başına birkaç kayan nokta işlemi; bellek ayırma yok.

### 9.2 Ölçümler (MEASURED)

**ROBOTISM taraması, 91 gerçek kayıt, gerçek ikili (ALL):**

| Mod | ROBOTISM | Vibrato korunan, aday 2 → aday 3 | Nota merkezine uzaklık | Çıkış geçiş süresi |
|---|---|---|---|---|
| REALTIME | %65 | %74,7 → %80,2 | 20,9 → 20,7 ct | 63,9 → 58,0 ms |
| REALTIME | %80 | %46,9 → **%74,0** | 23,5 → 23,3 ct | 63,9 → 52,2 ms |
| REALTIME | %100 | %81,0 → **%91,9** (1.2.0: %91,8) | 23,3 → 24,2 ct | 40,6 → 40,6 ms |
| HQ | %65 | %74,5 → %80,5 | 20,5 → 20,7 ct | 63,9 → 55,1 ms |
| HQ | %80 | %46,5 → **%73,8** | 23,6 → 23,1 ct | 61,0 → 52,2 ms |
| HQ | %100 | %80,3 → **%91,4** | 23,4 → 24,2 ct | 40,6 → 40,6 ms |

**Kesin doğru sentetik vibrato (`nv3_exp/`):**

- A3 + 25 ct, ±50 ct, 5,5 Hz: hedef nota her döngüde değişiyor.
  - Vibrato korunan: %80'de %32 → %75, %100'de %74 → %94 (REALTIME ve HQ).
  - Nota merkezine uzaklık, %80: 36 → 31 ct.
- Komşu notaya yaklaşmayan ±30 ct vibrato: bit bit aynı.

**Gerçek nota geçişleri (düzelticinin taklidi, 91 kayıt):**

- Varış = çıkışın titreşimsiz merkezinin yeni notanın 30 ct yakınına gelip 30 ms kalması.
- Vibratolu kayıtlar (317 geçiş) — medyan, %65 / %80 / %100:
  - aday 2: 61 / 89 / 125 ms;
  - aday 3: 61 / 80 / 81 ms.
- Diğer kayıtlar (85 geçiş):
  - aday 2: 70 / 99 / 141 ms;
  - aday 3: 73 / 106 / 136 ms (+3 / +7 / −4 ms).
- Kesin doğru sentetik geçişler (gt corpus): medyan 38 / 81 / 149 ms; aday 2 ile aynı (38 / 79 / 154 ms).

**Kapılar:**

| Kapı | Sonuç |
|---|---|
| Nota kararları aday 2 ile aynı | 36 / 36 (6 kayıt × %65 / %80 / %100 × REALTIME / HQ) → yanlış nota değişimi sayısı değişmedi |
| %0 / %25 / %50 bit bit aynı | 24 / 24 (4 kayıt × 3 ayar × 2 mod) |
| EngineTest | 78 / 78 ("İnsansı: nota değişimi kayar" 210 ms, değişmedi; "İnsansı vibratoyu korur" geçti) |
| ASan + UBSan | 0 bulgu (EngineTest; %80 render, REALTIME ve HQ) |
| PluginTest / pluginval seviye 10 | 73 / 73, SUCCESS |
| Taklit ↔ C++ | adımların %0,036'sı 0,01 yarım sesten fazla farklı (yuvarlama; en çok 0,05 yarım ses) |

### 9.3 Sınırlar

- **Dinlenmedi.** Dinleme paketi R07–R09 bu değişiklik için.
- **Etkinin kaynağı:** İyileşme, geniş vibratoda hedef notanın her döngüde değişmesinin yan etkisini (hızlanan düzeltme) kaldırıyor.
  Değişimin kendisi sürüyor. Bir değişim yanlışsa hedef notaya doğru kayma eskisi gibi başlıyor, yalnız düzeltme yavaş kalıyor.
- **Vibratolu şarkıcının gerçek nota geçişinde** de düzeltme yavaş kalıyor. Yeni notaya oturma, şarkıcının kendi geçişini daha çok
  izliyor; titreşimsiz merkez ölçüsüne göre gecikme yok.
- **Adaptive (varsayılan kapalı):** notanın yaşını kullandığı için davranışı da değişebilir. EngineTest'teki Adaptive kontrolleri
  geçti; ayrıca ölçülmedi.
