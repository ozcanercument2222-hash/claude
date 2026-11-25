# VSTM Tune 1.4.0 – `.drp` dosya biçimi (sürüm 1)

`.drp` iki tür dosya taşır:

- **Preset:** tür 1. Kullanıcı presetleri.
- **Yetki dosyası:** tür 2. Eklentideki admin özelliklerini açar (`VSTM_Tune_1.4.0_Authorization.md`).

İki tür ayrı doğrulanır. Preset içe aktarma bir yetki dosyasını, yetki içe aktarma bir preseti reddeder.

Kaynak: `Source/Drp.h`, `Source/Drp.cpp` (presetler), `Source/Authorization.cpp` (yetki), `tools/drp_auth.py` (imzalama
aracı). Tüm tamsayılar **little-endian**.

## Başlık (20 bayt, iki türde aynı)

| Konum | Boyut | Alan | Değer / kural |
|---|---|---|---|
| 0 | 8 | sihirli bayt dizisi | `56 53 54 4D 44 52 50 1A` ("VSTMDRP" + 0x1A) |
| 8 | 2 | biçim sürümü | 1; başka değer: `unsupportedVersion` |
| 10 | 2 | dosya türü | 1 = preset, 2 = yetki; başka değer: `badPayload` |
| 12 | 4 | içerik uzunluğu N | ≤ 65 536; büyükse: `badLength` |
| 16 | 4 | CRC-32 (IEEE 802.3) | içeriğin CRC'si; tutmazsa: `badChecksum` |
| 20 | N | içerik | türe göre, aşağıda |
| 20 + N | 64 | Ed25519 imzası | **yalnız yetki dosyası**: 0 … 20+N−1 baytların imzası |

Dosya boyu tam olarak `20 + N` (preset) ya da `20 + N + 64` (yetki) olmalı. Kısaysa `truncated`, fazlası varsa `badLength`.
Okunmadan önce dosya boyu sınırlanır: 20 + 65 536 + 64 bayttan büyükse `tooLarge`.

## Preset içeriği (tür 1)

İçerik, aşağıdaki ikili verinin gzip ile sıkıştırılmış hâlidir. Açılmış veri en çok 64 KiB olabilir; fazlası
"sıkıştırma bombası" sayılıp `badPayload` ile reddedilir.

| Alan | Tür | Kural |
|---|---|---|
| üretici kodu | u32 | `0x5673746D` ('Vstm'); değilse `wrongPlugin` |
| eklenti kodu | u32 | `0x56746E31` ('Vtn1'); değilse `wrongPlugin` |
| durum sürümü | u16 | yazan eklentinin oturum sürümü (1.4'te 2) |
| ad uzunluğu + ad | u16 + UTF-8 | ≤ 256 bayt, geçerli UTF-8 |
| değer sayısı | u16 | ≤ 256 |
| her değer | u8 uzunluk + ASCII kimlik + f32 | kimlik 1–64 karakter, yalnız `[a-z0-9_]`; değer parametrenin kendi biriminde (IEEE 754) |

İçerikte artık bayt kalırsa `badPayload`.

**Yüklerken:**

- Bilinmeyen kimlikler yok sayılır.
- `nan` / `inf` değerler parametrenin varsayılanına döner.
- Aralık dışı değerler aralığa kırpılır.
- Bypass, REALTIME ve vokal aralığı presetlere dahil değildir (1.0'dan beri).

**Okunabilirlik ≠ güvenlik.** Sıkıştırılmış ikili veri bir metin düzenleyicide okunamaz. Ama bu gizlilik değildir: herkes
içeriği açıp değiştirebilir; CRC yalnız kazara bozulmayı yakalar. Presetlerde gizlilik bir gereksinim değil, bu yüzden
şifrelenmez.

## Yetki içeriği (tür 2)

| Alan | Tür | Kural |
|---|---|---|
| üretici kodu, eklenti kodu | u32, u32 | yukarıdaki gibi; değilse `wrongPlugin` |
| içerik sürümü | u16 | 1; değilse `unsupportedVersion` |
| rol | u8 | 1 = admin; başkası `unsupportedRole` |
| ayrılmış | u8 | 0 |
| yetkiler | u32 | `0xFFFFFFFF` = tüm admin özellikleri; başka değer `unsupportedRole` |
| verilme / geçerlilik başı / bitiş | u64 × 3 | Unix saniyesi (UTC); başlangıç ve bitiş için 0 = yok |
| seri numarası | u32 | iptal listesi için; 0 hiçbir zaman verilmez |
| anahtar kimliği | 8 bayt | açık anahtarın SHA-512 özetinin ilk 8 baytı |
| etiket uzunluğu + etiket | u8 + UTF-8 | ≤ 64 bayt; kişisel veri gerekmez |

## Sınırlar ve dayanıklılık (MEASURED, PluginTest 3h / 3i)

| Durum | Sonuç |
|---|---|
| Geçerli bir presetin her kesilmiş hâli (247) | reddedildi |
| Her tek bayt değişikliği (247) | reddedildi |
| 2000 rastgele dosya (yarısında doğru sihirli bayt dizisi var) | reddedildi |
| 2 MB'a açılan 2 KB'lık dosya | reddedildi |
| Başka eklentinin dosyası | reddedildi |
| Geçersiz parametre kimliği | reddedildi |
| `nan` / `inf` / dev değerler | varsayılan ya da kırpılmış |
| Yetki dosyasının her tek-bit değişikliği (147 baytın 1176 biti) ve her kesilmiş hâli | reddedildi |
| Çökme, takılma, sonsuz döngü | hiçbirinde yok |

## Geçiş (1.0–1.3 presetleri)

- **Eski dosyalar:** `*.vstmpreset` (XML) okunmaya devam eder. Okumadan önce 1 MiB ile sınırlanır; kök etiket
  `VSTMTunePreset` olmalı.
- **Aynı ad iki biçimde:** aynı adda bir `.drp` varsa listede `.drp` görünür.
- **Yeniden adlandırma:** eski bir presetin adı değişince `.drp` olarak yazılır, eski dosya ancak yenisi yazıldıktan sonra
  silinir.
- **İçe aktarma:** `.drp` ya da `.vstmpreset` dosyası doğrulanır ve preset klasörüne **yeni bir adla** `.drp` olarak
  kopyalanır. Örneğin `Ad (2)`. Hiçbir dosyanın üzerine yazılmaz, preset klasörü dışına hiçbir şey yazılmaz.
- **Kayıt:** önce `ad.drp.tmp`'ye yazılır, sonra yerine konur. Yazma başarısız olursa eski dosya olduğu gibi kalır.
- **1.4 presetleri 1.3'te:** 1.3 `.drp` okumaz. Bir preseti eski sürüme taşımak için ayarları 1.3'te yeniden kaydetmek gerekir.
  Oturumlar (proje dosyaları) değişmedi; 1.4 projeleri 1.3'te açılır.
