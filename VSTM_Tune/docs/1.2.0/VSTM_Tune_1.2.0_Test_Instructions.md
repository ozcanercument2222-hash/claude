# VSTM Tune 1.2.0 – Test talimatı

Bu belge 1.2.0'ı üç düzeyde sınamayı anlatır:

1. **Otomatik testler:** derleme, EngineTest, PluginTest, pluginval. Bu ortamda (Linux) çalıştırıldı.
2. **Ölçümler:** raporun sayılarını yeniden üretir. Bu ortamda çalıştırıldı.
3. **Gerçek DAW testi ve kör dinleme:** **yapılmadı.** Bunları senin yapman gerekiyor.

Raporda "test edildi" yazan her şey 1. ve 2. düzeydir. DAW ve dinleme sonuçları gelene kadar ses kalitesi için
"teknik test başarılı, dinleme doğrulaması bekliyor" geçerlidir.

## 1. Otomatik testler

### 1.1 Derleme ve testler (Linux; Windows / macOS'ta aynı komutlar)

```
cd VSTM_Tune
cmake -B build -DCMAKE_BUILD_TYPE=Release -DVSTM_BUILD_TESTS=ON -DVSTM_COPY_AFTER_BUILD=OFF
cmake --build build --config Release --parallel 4
build/VSTMTune_EngineTest                                   # beklenen: ALL PASSED, 71 kontrol
xvfb-run -a build/VSTMTune_PluginTest_artefacts/Release/VSTMTune_PluginTest goruntuler \
    --vst3 "build/VSTMTune_artefacts/Release/VST3/VSTM Tune.vst3"    # beklenen: ALL PASSED, 73 kontrol
```

Windows'ta `build\Release\VSTMTune_EngineTest.exe`. PluginTest `xvfb-run` olmadan çalışır.
`--vst3` verilince derlenmiş VST3, bir DAW gibi taranıp yüklenir ve ses işletilir (bölüm 5).

GitHub'a gönderilen her değişiklikte CI (Actions → **VSTM Tune derle**) Windows, macOS ve Linux'ta aynı derlemeyi ve iki testi
çalıştırır, kurulabilir paketleri yükler. 1.2.0 değişiklikleri henüz gönderilmedi. Bu yüzden **Windows ve macOS derlemesi bu
sürüm için henüz yapılmadı ve test edilmedi.**

### 1.2 pluginval (biçim ve kararlılık)

pluginval 1.0.4 (Tracktion, GitHub sürümü):

```
xvfb-run -a pluginval --strictness-level 10 --validate-in-process --output-dir pluginval_log \
    --validate "build/VSTMTune_artefacts/Release/VST3/VSTM Tune.vst3"         # beklenen: SUCCESS
```

Bu ortamda Linux VST3 için çalıştırıldı. "vst3 validator" grubu atlandı (Steinberg doğrulayıcısının yolu verilmedi).
auval yalnız macOS'ta vardır ve çalıştırılmadı.

### 1.3 Bellek / tanımsız davranış denetimi (ASan + UBSan)

```
FL="-g -O1 -std=gnu++17 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined -ISource"
g++ $FL tests/engine_test.cpp -o engine_test_asan && ./engine_test_asan
g++ $FL tests/wav_probe.cpp -o wav_probe_asan && ./wav_probe_asan girdi.wav cikis.wav transpose=12 formant=-6 adaptive=1
g++ $FL tests/rt_switch_probe.cpp -o rt_switch_asan && ./rt_switch_asan klasor
```

## 2. Ölçümleri yeniden üretmek (MEASURED)

Ölçüm kayıtları (VocalSet CC BY 4.0, RAVDESS, kendi rap vokalin, ayrıştırılmış bir stem) lisans ve gizlilik nedeniyle
**depoda yok**. Araçlar bir manifest (`{"files": [{"id", "path", "group", "range"}]}`) okur ve girdi dosyalarına hiç yazmaz.
Bu çalışmanın manifestleri ve tüm çıktıları `artifacts/audio-runs/2026-11-19_q12/` altındadır (git dışı).
Çalıştırılan komutların tamamı `run_final2.sh` betiğindedir. Tek tek:

| Ne ölçülür | Komut (VSTM_Tune/tests/) | Not |
|---|---|---|
| Kesin doğrulu perde seti | `gt_corpus.py resynth / synth / refgt` | 145 kaydın sinüs-toplamı yeniden sentezi (perde birebir bilinir), 77 parametrik ses, gerçek kayıtlar için kısa çerçeveli referans (emin olunmayan kareler −1) |
| Perde doğruluğu | `gt_eval.py run --manifest M --probe WavProbe --out D` → `gt_eval.py compare` | oktav / kaba hata, sürekli oktav koşuları (20 / 30 / 50 ms), ince hata medyan / p95, voiced recall / precision, yanlış voiced; motorun kendi zaman damgasıyla |
| Sağlamlık | `gt_variants.py --kind rate=96000 / gain=-30 / snr=20 / stereo=left ...` → `gt_eval.py` | örnekleme hızı, seviye, gürültü, stereo |
| Tepe / patlama | `peak_check.py run` → `peak_check.py compare A B` | seviye ve tepe ayrı: gerçek tepe (4× aşırı örnekleme), 10 ms patlama, 20–150 Hz gümleme, 0 dBFS üstü |
| REALTIME ↔ HQ geçişi | `VSTMTune_RtSwitchProbe klasor` → `rt_switch_eval.py klasor --json j --md m` | 132 durum: tekrar / atlanan içerik, perde sapması, tık |
| Nota geçiş zamanlaması | `transition_eval.py run --manifest gt_synth/manifest.json --probe P --out D --realtime 1` | karar gecikmesi (hop kaydından) |
| ROBOTISM eğrisi | `robotism_sweep.py --points 0,10,20,25,35,50,65,80,90,100` | vibrato korunumu, notaya yakınlık, geçiş süresi |
| Gerçek kayıt matrisi | `run_vocal_matrix.py --settings hard,default,natural,off --baseline ESKI/summary.json` | voiced, oktav, seviye, artefakt sayıları |
| Auto Key | `autokey_eval.py make` → `run` → `compare` | yalnız **etiketli sentetik** melodiler. Gerçek kayıtta doğruluk **ölçülmedi** |
| CPU / bellek ayırma | `VSTMTune_BenchProbe matrix / instances / denormal / longrun` | paylaşılan 4 çekirdekli sanal makine: mutlak değerler bir DAW makinesini temsil etmez |

## 3. DAW testleri (yapılmadı: lütfen sen yap)

Kit: `VSTM_Tune/daw_test/README.md`. Test sinyalleri, REAPER betiği, FL Studio adımları ve render'ları ölçen
`analyze_daw_renders.py` (R01–R14: perde, seviye, PDC, otomasyon, yeniden açma, tampon boyutu, freeze, stereo) orada.
**REAPER betiği hiç çalıştırılmadı.** İlk denemede bir adım başarısız olursa konsoldaki nedeni bana gönder.

1.2.0 için ek kontroller (dinleyerek ve DAW ölçerleriyle):

| # | Ne yap | Ne bekleniyor | Neden |
|---|---|---|---|
| R15 | Çalarken REALTIME'ı birkaç kez aç / kapat (MIX %100) | Tık, kesinti, tekrar eden ya da atlanan hece yok; geçiş ~0,4 s içinde yumuşak | adım 6 |
| R16 | Aynısını MIX %50 ile yap | Aynı. Kuru ses 2 s'lik geçiş boyunca hafifçe hız değiştirir (en çok ~%1,6, ~27 cent). Duyuluyorsa yaz | adım 6, bilinen sınır |
| R17 | DAW gecikme telafisi: R15 sırasında ve sonrasında vokal beat'le hizalı mı? | Geçişten sonra hizalı. DAW'ın yeni gecikmeyi ne zaman uyguladığı DAW'a bağlıdır: kaç saniye kaydığını yaz | bildirilen gecikme anında değişir, ses 0,4–2 s'de kayar |
| R18 | ROBOTISM 0, tutulan notada KEY'i A → A# değiştir (ya da Auto Key kilitlensin) | Tık yok, ~25 ms'lik kısa kayma | adım 9 |
| R19 | Yüksek seviyeli vokal (tepeler 0 dBFS'e yakın), PITCH RANGE +12 ve FORMANT −12 | Çıkışın gerçek tepesini (true peak) ölç. Eklentide limiter yok: tepe kuru sesin birkaç dB üstüne çıkabilir (rapor 4.3) | adım 8 |
| R20 | 1.1 ile kaydedilmiş bir projeyi 1.2 ile aç | 27 parametre aynı, otomasyon çalışıyor. Ses biraz farklı (beklenen, changelog) | uyumluluk |
| R21 | 4, 8 ve 16 örnek, 48 kHz, tampon 64 ve 256 | DAW'ın CPU ölçeri, takılma (dropout) var mı | performans |
| R22 | Hızlı rap ve hızlı gam, hard tune (ROBOTISM 0, RETUNE 0) | Notalar zamanında, ara nota ya da gecikme yok | adım 2 ve 7 |
| R23 | Kalın erkek sesi, VOCAL RANGE = Bass, hızlı geniş vibrato | Kesinti, pürüz ya da yanlış oktav yok | adım 3 ve 5 |
| R24 | Soprano, VOCAL RANGE = Soprano / Alto, "i" sesinde E6 civarı | Alçak (yanlış) perde, homurtu yok | adım 4 ve 11 |

Her test için şunları yaz: DAW ve sürümü, işletim sistemi, örnekleme hızı, tampon boyutu, sonuç (geçti / kaldı / şüpheli), varsa zaman ve ne duyulduğu.
Ölçüm aracı sonuçları (`sonuc.json`) ve kısa bir not yeterli.

## 4. Kör dinleme (yapılmadı: dinleyici sensin)

Paket: `artifacts/release-1.2.0/VSTM_Tune_1.2.0_Blind_Listening_Pack.zip` (bu oturumun dosya alanında; bkz. rapor bölüm 8).
22 paket, her pakette aynı kuru vokalin 1.1.0 ve 1.2.0 ile ya da farklı ayarlarla işlenmiş hâlleri var.

1. `LISTENING_SHEET.md`'yi oku. Her paketin sorusu ve malzemesi orada.
2. Dosyalar aynı yüksekliğe (−20 LUFS) ve aynı zamana getirildi (eklenti gecikmesi çıkarıldı). Adlar rastgele 4 harftir.
   Tepe sınırı −1 dBTP. Kulaklık ya da monitör kullan, ses seviyesini sabit tut.
3. `feedback_form.csv`'ye her dosya için 1–5 puan ver (5 = en iyi; Artefakt sütununda 5 = hiç yok). Duyduğun sorunun zamanını
   ve ne olduğunu yaz ("3,2 s 'gece' kelimesinde metalik ses").
4. **`_ANSWER_KEY` klasörünü dinleme bitene kadar açma.** Formu bana gönder; eşleştirmeyi ben yaparım.

Önce A12 (REALTIME geçişi), A10 / A11 (transpoze / formant patlamaları), A13 (varsayılan ses: 1.2'de ROBOTISM %50 vibratonun daha
çoğunu korur) ve B01 (ROBOTISM eğrisi) paketlerini dinlemeni öneririm: ölçümün en çok sese yansıyabileceği yerler bunlar.

## 5. Bu ortamda yapılan ve yapılamayanlar

| Test | Durum |
|---|---|
| Linux derleme, 0 proje uyarısı | yapıldı |
| EngineTest 71 / 71, PluginTest 73 / 73 (Linux VST3 JUCE host'unda yüklendi) | yapıldı |
| pluginval 10 (Linux VST3) | yapıldı (rapor bölüm 6) |
| ASan + UBSan (EngineTest, WavProbe, RtSwitchProbe) | yapıldı |
| Windows / macOS derleme ve testleri | **yapılmadı.** Commit + push gerekiyor (CI) |
| REAPER, FL Studio, Ableton, Logic, Cubase, Studio One | **yapılmadı.** Bu ortamda DAW yok |
| AU / auval | **yapılmadı** (yalnız macOS) |
| İnsan dinleme testi | **yapılmadı** |
