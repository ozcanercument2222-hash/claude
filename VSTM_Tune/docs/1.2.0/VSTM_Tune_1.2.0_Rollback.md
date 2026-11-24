# VSTM Tune 1.2.0 – Yedek ve geri dönüş (rollback)

1.2.0, 1.1.0'ın üstüne küçük, ayrı adımlar olarak yapıldı. Parametre kimlikleri, sırası, preset ve oturum biçimi
değişmediği için **1.2.0 ile kaydedilen bir proje 1.1.0'da da açılır**, tersi de geçerli (PluginTest 3b / 3d / 3e;
1.2.0'ın yazdığı durumu 1.1.0'ın okuması ayrıca test edilmedi, ama biçim bayt düzeyinde aynı: state sürümü 2, aynı 27 parametre).

## 1. Geri dönüş noktaları

| Ne | Nerede | Kalıcı mı? |
|---|---|---|
| 1.1.0 kaynak kodu (commit `193d2f3`) | GitHub, `claude/laughing-rubin-45nmp2` dalı | **Evet**: uzak depoda |
| Git etiketi `vstm-tune-1.1.0-baseline` → `193d2f3` | Bu çalışma ortamındaki yerel depo | Hayır: etiket henüz gönderilmedi (push izin bekliyor) |
| Orijinal yükleme `VSTM_Tune_1.1.0_source.zip` (salt okunur kopya) | `artifacts/audio-runs/2026-11-19_q12/backup/` | Hayır: `artifacts/` git dışı, bulut oturumu kapanınca silinir |
| Tüm geçmişin git paketi `vstm-tune-1.1.0-full-history.bundle` | aynı klasör | Hayır (aynı neden) |
| 1.1.0 Linux derlemesi `VSTM_Tune_1.1.0_Linux.zip` | aynı klasör | Hayır (aynı neden) |
| Adım adım yamalar (`NN_ad.incremental.patch`, `NN_ad.cumulative.patch`) ve kaynak anlık görüntüleri | `artifacts/audio-runs/2026-11-19_q12/patches/`, `snapshots/` | Hayır (aynı neden) |

SHA-256 (`backup/SHA256SUMS.txt`):

```
3276cc872128edb51db81d3027bc93c050a99eb7a9c251a9f1aa56267e3a44a3  VSTM_Tune_1.1.0_source.zip
1d87c1685c465243c098d0d4f23a81a764585b4b60c0e42a18540dccf56cad1c  vstm-tune-1.1.0-full-history.bundle
8c42d0a283caf76ee2579f848da76ae592b49267aa9f90a5b5eef87e50ad48e1  VSTM_Tune_1.1.0_Linux.zip
```

Orijinal ZIP'in kendisi (yüklediğin dosya) hiç değiştirilmedi; yalnız okunup kopyalandı.

## 2. Tamamen 1.1.0'a dönmek

Kurulu eklentiyi geri almak için 1.1.0'ın `VSTM Tune.vst3` klasörünü (ya da macOS'te 1.1.0 kurulum paketini) yeniden kur.
Projelerin açılır: ayarlar aynı kalır. **Ses biraz farklı olur**, ayrıntı changelog'un "Varsayılan sesteki değişiklikler" bölümünde.

Kaynak kod için:

```
git fetch origin claude/laughing-rubin-45nmp2
git switch -c vstm-tune-1.1.0-geri 193d2f3          # ya da: git switch -c ... vstm-tune-1.1.0-baseline
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release
```

Yalnız eklenti klasörünü geri almak, dalın geri kalanına dokunmamak için:

```
git checkout 193d2f3 -- VSTM_Tune
```

Git paketinden (depo yoksa): `git clone vstm-tune-1.1.0-full-history.bundle vstm-tune-1.1.0 && cd vstm-tune-1.1.0 && git checkout 193d2f3`.

## 3. Tek bir 1.2.0 değişikliğini geri almak

Her DSP değişikliği ayrı bir adımdır (changelog tablosundaki #). Hepsi tek başına geri alınabilir. Yalnız 3 ile 5 aynı kuralın
iki parçasıdır: 3 geri alınacaksa 5 de geri alınır.

| Adım | Geri almak için | Not |
|---|---|---|
| 2 ince ayar zamanlaması | `PitchDetector.h`: `refine()` merkez dizinini `endIndex`'e geri ver | Kaba hata 1.1 düzeyine döner (%1,47). Önerilmez |
| 3 + 5 sürekli voiced | `kStayVoicedContinuing`'i `kStayVoiced` yap (kural devreden çıkar) | Bas hızlı vibratoda voiced %95 → %65 |
| 4 kısa periyot | `kSubharmonicMaxLag = 0` (adım 11 de etkisiz kalır) | E6 / D6'da 2P / 4P hatası geri gelir |
| 6 REALTIME geçişi | `06_rt_hq_content_ramp.incremental.patch`'i ters uygula (`patch -R -p1`) | 5 ms kısma + 20 ms atlama geri gelir |
| 7 yakın varış | `kArriveNear = 0.0f` | Nota değişimi ~10 ms gecikir |
| 8 tepe sınırı | `kPeakMargin`'i çok büyük bir değer yap (ör. `1.0e6f`) | Sınır devreden çıkar, 1.1 davranışı |
| 9 ton değişiminde kayma | `kKeyChangeGlideMs = 0.0f` | Robotik tarafta anlık sıçrama geri gelir |
| 11 kısa periyot yalnız Soprano / Alto | `kShortLagRangeHz = 0.0` (denetim her aralıkta çalışır, adım 9'daki hâl) | Tenor aralığında adım 4'ün yan etkileri geri gelir |

Bir adımı değiştirdikten sonra **her zaman** `VSTMTune_EngineTest` ve `VSTMTune_PluginTest`'i çalıştır (test talimatı, bölüm 1).
EngineTest 12b adım 6'yı, 15 adım 9'u doğrudan sınar; o adım geri alınırsa ilgili testin başarısız olması beklenir
(geri alıp denemedim: **doğrulanmadı**). Tablodaki "geri almak için" değişiklikleri koddan okunarak yazıldı (INSPECTED),
tek tek derlenip ölçülmedi; sağdaki etkiler ise ilgili adımın ölçümleridir (MEASURED, kalite raporu).

## 4. Yamaları sırayla uygulamak (1.1.0'dan 1.2.0'a)

Depo kökünde, 1.1.0'dan başlayarak:

```
Y=artifacts/audio-runs/2026-11-19_q12/patches
git switch -c deneme 193d2f3
git apply $Y/01_gt_tools_instrumentation.cumulative.patch        # 01'in öncesi yok: tek parça (git biçimi)
for p in 02 03 04 05 06 07 08 09 10 11; do patch -p1 < $Y/${p}_*.incremental.patch; done
```

- `NN_ad.incremental.patch`: bir önceki anlık görüntüye göre fark (`diff -ruN`, yol öneki `NN_ad/VSTM_Tune/...`;
  `-p1` bu öneki atar, bu yüzden depo kökünde çalışır).
- `NN_ad.cumulative.patch`: 1.1.0'a (`vstm-tune-1.1.0-baseline`) göre o adıma kadar her şey, `git apply` ile.
- `10_*`: DSP dışı değişiklikler (sürüm 1.2.0, PluginTest 3e, WavProbe `switch=`, yeni ölçüm araçları, CMake hedefleri, belgelerin ilk hâli).
- `11_*`: adım 11 (kısa periyot denetimi yalnız Soprano / Alto aralığında). Bundan sonraki belge düzeltmeleri yamada yoktur.

Yama ve anlık görüntü klasörleri yalnız bu bulut oturumunda duruyor (`artifacts/` git dışı). Kalıcı olması için değişikliklerin
commit edilip gönderilmesi gerekir. Her commit bir adıma karşılık gelirse, yamalara gerek kalmadan `git revert <commit>` ile tek adım geri alınır.
