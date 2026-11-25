# VSTM Tune 1.4.0 – Eklenti içi admin yetkisi

## Ne açar, ne açmaz

İmzalı bir yetki dosyası (`.drp`, tür 2) **yalnız VSTM Tune içinde** admin özelliklerini açar. Admin rolü bu eklentinin
bütün admin özelliklerini kapsar.

**1.4'te açtıkları:**

- **Geliştirici paneli** (ayarlar menüsü → Geliştirici paneli): giriş ve çıkış perdesi, hedef nota, algılayıcı kesinliği,
  analiz gecikmesi, eklenti gecikmesi, yetki bilgisi.
- **Uzman motor anahtarları** (panelde):
  - çıkış tepe koruması;
  - HQ geri tarihli nota değişimi;
  - REALTIME güncel analiz;
  - insansı tarafta vibratoda düzeltme hızını koruma.

**Uzman anahtarların davranışı:**

- Hepsi varsayılan olarak açıktır; bu, normal davranıştır.
- Ne oturuma ne presete kaydedilir.
- Eklenti yeniden yüklenince ya da yetki bitince hepsi açık konuma döner.
- Yetki yokken değiştirilemez; işlemci isteği reddeder.

**Açmadıkları:** işletim sistemi, DAW ya da başka bir program üzerinde hiçbir yetki. Dosya sistemi işlemi ya da kod
çalıştırma da yoktur. Yetki dosyasının içindeki hiçbir şey çalıştırılmaz.

## Tasarım

- **İmza:** Ed25519 (RFC 8032, SHA-512).
  - Eklenti yalnız **açık anahtarları** içerir (`Source/Authorization.cpp`, `kTrustedKeys`).
  - Özel anahtar yalnız dosyayı veren kişidedir. Çevrimdışı tutulur ve depoda ya da eklenti ikilisinde **yoktur**.
- **Doğrulayıcı:** Monocypher 4.0.2, değiştirilmeden eklendi.
  - Kaynak doğrulandı: iki ayrı kaynaktan indirildi, iki dosya bayt bayt aynı, resmî SHA-512 tuttu.
  - Lisans: BSD-2 / CC0 (`Source/third_party/monocypher/`).
  - Çapraz kontrol (MEASURED): RFC 8032 test vektörü kabul edildi, değiştirilmiş hâli reddedildi. Python `cryptography`
    ile atılan 200 imzanın 200'ü kabul edildi; değiştirilmiş mesajlardan hiçbiri kabul edilmedi.
- **Doğrulama sırası** (ilk hata bildirilir):
  1. başlık;
  2. içerik yapısı;
  3. eklenti kodları;
  4. içerik sürümü;
  5. güvenilen anahtar (anahtar kimliği);
  6. **imza**;
  7. rol ve yetkiler;
  8. iptal listesi;
  9. geçerlilik başlangıcı ve bitişi.
- **Neler yetmez:** Dosya adı, uzantı, içerideki tür işareti, sabit bir parola ya da imzasız içerik **hiçbir zaman yetki
  vermez**. Geçerli imza ve güvenilen anahtar şarttır.

## Anahtar (1.4.0)

| | |
|---|---|
| Anahtar kimliği | `a2f22028b35ac70d` |
| Açık anahtar | `a61e95f0fa0f78f3debfec17466c63159ec55ea669e883b262bf6ec318d7c03d` |
| Üretim | 24.11.2026, kullanıcının isteğiyle bu bulut oturumunda, `tools/drp_auth.py keygen` ile |
| Özel anahtar | `vstm_tune_auth_private_key.pem`. Kullanıcıya teslim edildi; depoda ve eklentide yok. Teslimden sonra bu ortamdan silindi. |

Özel anahtar bu bulut oturumunda bir süre bulunduğu için **tamamen çevrimdışı üretilmiş bir anahtar kadar güvenli sayılmaz.**
Daha sıkı bir güvenlik için:

1. Kendi bilgisayarında `drp_auth.py keygen` ile yeni bir çift üret.
2. Açık anahtarı `kTrustedKeys`'e ekle, bu anahtarı çıkar.
3. Yeni yetki dosyalarını yeni anahtarla imzala.

## Yetki dosyası vermek

```
python3 tools/drp_auth.py sign --key vstm_tune_auth_private_key.pem --out Admin.drp \
        [--label "VSTM Tune admin"] [--serial 2] [--expires 2027-12-31] [--not-before 2027-01-01]
python3 tools/drp_auth.py inspect Admin.drp
python3 tools/drp_auth.py verify Admin.drp --public a61e95f0…c03d
```

`cryptography` Python paketi gerekir. Her dosyaya ayrı bir seri numarası ver; 0 kullanılmaz.

## Saklama, yenileme, kaldırma, iptal, süre

- **İçe aktarma** (preset menüsü → Yetki dosyası içe aktar…):
  - Dosya doğrulanır. Geçerliyse **bayt bayt** `<kullanıcı veri klasörü>/VSTM Audio/VSTM Tune/authorization.drp` olarak
    kopyalanır. Önce geçici bir dosyaya yazılır, sonra yerine konur.
  - Reddedilen bir dosya hiçbir şey yazmaz; varsa önceki yetki kalır.
- **Yeniden doğrulama:**
  - Saklanan kopya eklenti açılınca doğrulanır; editör açıkken dakikada bir yeniden doğrulanır.
  - Diskte değiştirilmiş, iptal edilmiş ya da süresi dolmuş bir kopya artık çalışmaz.
  - O anda geliştirici paneli kapanır, uzman anahtarlar açık konuma döner.
- **Kaldırma** (preset menüsü → Yetkiyi kaldır): yalnız `authorization.drp` silinir.
- **İptal:**
  - Belirli dosyalar için: seri numarası `kRevokedSerials` listesine eklenir ve yeni bir sürüm dağıtılır.
  - Anahtar sızarsa: o anahtar `kTrustedKeys`'ten çıkarılır.
- **Süre:**
  - Dosyada bir bitiş tarihi varsa sistem saatine göre denetlenir.
  - Sistem saatini geri alan bir kullanıcı süreyi uzatabilir. Bu, dürüst kullanıcıya yönelik bir mekanizmadır; kopya
    koruması değildir.
- **Bilgilendirme:**
  - Preset menüsünde "Admin yetkisi etkin (süresiz / … tarihine kadar geçerli)" satırı görünür.
  - Panelde etiket, anahtar kimliği, seri numarası ve bitiş tarihi gösterilir.
  - Reddedilen dosya için çevrilmiş bir neden gösterilir: bozuk, başka eklentinin, imza geçersiz, süresi dolmuş, preset vb.
- **Kişisel veri:** toplanmaz. Etiket serbest metindir; kişisel veri yazmak gerekmez.

## Sınırlar (açıkça)

- **Değiştirilmiş eklenti ikilisi:** İkiliyi değiştirebilen biri (kendi bilgisayarında) doğrulamayı atlatabilir. İmza,
  sahte yetki dosyası üretilmesini engeller; değiştirilmiş eklentiyi engellemez.
- **Paylaşılan dosya:** Yetki dosyası kopyalanırsa başka bilgisayarda da çalışır. Makineye bağlama, kişisel veri toplamayı
  gerektireceği için yapılmadı.
- **Testlerdeki anahtarlar:** Testler (PluginTest 3i) her çalıştırmada yeni test anahtarı üretir; hiçbir özel anahtar depoya
  girmez. Gerçek anahtarla imzalanmış dosya, `VSTM_TEST_AUTH_FILE` ile verildiğinde eklentinin kendi doğrulayıcısıyla
  sınanır (MEASURED: `ok`, anahtar `a2f22028b35ac70d`).
