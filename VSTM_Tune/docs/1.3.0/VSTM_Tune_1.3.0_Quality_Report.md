# VSTM Tune 1.3.0 – Kalite raporu (aday 1: P0 işleri)

Temel: 1.2.0 (kaynak zip SHA-256 `b0651f0e…a2af9a`). Aday 1 = 1.2.0 + iki yama.

**Bu rapor 1.3'ün ilk adımıdır, yayın raporu değil.** Yol haritasının P0 maddeleri ele alındı. P1 maddeleri, DAW testleri, kör
dinleme ve gerçek donanımda CPU ölçümü henüz yapılmadı; durumları bölüm 6'da.

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

## 6. Yol haritası durumu

| Madde | Durum |
|---|---|
| P0 nota geçişi | HQ'da duyulan medyan 11,2 → 6,3 ms. REALTIME değişmedi, takas belgelendi. p95 hedefi tutmuyor (bas, yavaş kayma) |
| P0 tepe güvenliği | 0 dBFS aşımı 145 kayıtta 0. Gerçek tepe ve dinleme açık |
| P1 doğal ayar / vibrato, Adaptive | **Yapılmadı.** Adaptive varsayılan kapalı kalıyor |
| P1 algılayıcı regresyon koruması | Algılayıcı değişmedi (bit bit aynı). Genişletilmiş kapsam (falsetto, E6, nefesli, fry) **yapılmadı** |
| P1 formant / transpoze artefaktları | Tepeler ölçüldü. Ses karakteri (metalik, fazlı ses vb.) **dinlenmedi**. PSOLA tepe artışının kökü açık |
| P1 ROBOTISM eğrisi | Değişmedi (%0 dışında bit bit aynı). Yeniden değerlendirme **yapılmadı** |
| P1 Auto Key gerçek kayıtla | **Yapılmadı.** Tonu bilinen gerçek kayıt seti yok |
| P1 kör dinleme | Paket hazır (9 paket). **Dinlenmedi** |
| P1 DAW testleri | **Yapılmadı (NOT TESTED).** Bu ortamda REAPER, FL Studio, Ableton yok |
| P2 CPU / bellek | Sanal makinede göreli ölçüm. Gerçek donanımda **yapılmadı** |
| P2 uyumluluk | PluginTest 73 / 73 |
| P2 Windows / macOS | **Yapılmadı.** CI için commit + push gerekiyor |

---

## 7. Bilinen sorunlar (aday 1)

1. **REALTIME'da hard tune geçişleri ~13–17 ms geç duyuluyor** (ölçüm sapması düşülünce). Yapısal; bölüm 2.1.
2. **HQ'da bas ve yavaş kaymalarda geçiş 20–30 ms geç.**
3. **Gerçek tepe** formant +12 ve sıcak girişte +1,0 dBTP'ye kadar çıkabiliyor (örnek tepesi −1 dBFS'te tutuluyor).
4. **PSOLA tepe artışı:** Kalın erkek seslerinde kuruya göre +5…+7 dB. Sınırlayıcı yakalıyor, kök neden açık.
5. **Vocal fry / creak'te hard tune titriyor** (1.2.0'dan beri).
6. **Kromatik robotik modda geniş vibrato trile dönüşüyor** (1.2.0'dan beri).
7. **Adaptive insansı uçta vibratoyu azaltıyor** (1.2.0'dan beri; varsayılan kapalı).
8. **Dinleme, DAW ve gerçek donanım sonucu yok.**
