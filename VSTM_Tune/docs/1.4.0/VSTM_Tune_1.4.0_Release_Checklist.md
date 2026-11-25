# VSTM Tune 1.4.0 – Yayın kontrol listesi

**[x]** tamam (kanıtı belgelerde), **[~]** kısmen, **[ ]** yapılmadı. **Bu liste tamamlanmadan 1.4.0 etiketlenmez ve
yayımlanmaz.**

## Şartname: yayın koşulları

- [~] **Her P0 / P1 sorunu tekrarlanabilir bir test, önce / sonra kanıtı ve regresyon kapsamı:**
  - P0 oktav: yapıldı (`gt_eval`, 3 set × 2 mod).
  - P0 fry: tekrarlandı, ölçüldü; düzeltme alınmadı (bilinen sınır).
  - P0 geçiş sürekliliği: PSOLA yolu değişmedi; mevcut testler geçti (EngineTest başlangıç düşüşü ve otomasyon tıkı,
    132 REALTIME/HQ geçişi, PluginTest 3g). **Yeni tık ölçüm kampanyası yapılmadı.**
  - P1 nota kararı: vibrato sınır geçişi 1.3 aday 3'te çözüldü; karar kuralı 1.4'te değişmedi.
  - P1 sıçrama ve güven: ölçüldü, değişiklik yapılmadı (gerekçe changelog'da).
- [ ] **Ses kalitesi hem perde analiziyle hem dinlemeyle değerlendirildi:** perde analizi var, **dinleme yok**.
- [x] **Presetler doğrulanıyor ve sürümlü; bozuk dosya eklentiyi çökertemiyor** (PluginTest 3h).
- [x] **Yetki dosyaları kriptografik olarak doğrulanıyor ve eklentiye bağlı; özel anahtar dağıtılmıyor** (PluginTest 3i;
  depoda ve ikilide yalnız açık anahtar).
- [~] **Durum / preset uyumluluğu, CPU, gecikme, kararlılık ve pluginval kontrol edildi:**
  - Linux'ta: PluginTest 97 / 97, pluginval SUCCESS, CPU göreli, gecikme aynı.
  - Windows / macOS ve gerçek DAW: **yapılmadı**.

## Diğer

- [x] Eklenti ve parametre kimlikleri, sırası, aralıkları, oturum biçimi (state 2) ve gecikmeler korundu.
- [x] 1.0–1.3 `.vstmpreset` dosyaları okunuyor. 1.3, 1.4'ün `.drp` dosyalarını okuyamaz (belgelendi).
- [x] Ses iş parçacığında bellek ayırma ve kilit yok (BenchProbe 0 ayırma; yetki ve dosya işleri yalnız mesaj
  iş parçacığında).
- [x] Dinleme paketi hazırlandı (`VSTM_Tune_1.4.0_Blind_Listening_Pack`: 5 paket, oktav korumasının değiştirdiği anlar).
- [ ] Dinleme paketi dinlenmedi.
- [ ] Oktav koruması gerçek kayıtlarda dosya başına karışık (REALTIME 8 iyileşen / 7 kötüleşen). Kötüleşen paketlerde (O03, O04)
  1.3 tercih edilirse 3 kat koruması daraltılmalı (kalite raporu 3.6).
- [ ] REAPER / FL Studio / Ableton: **NOT TESTED**.
- [ ] Windows / macOS derlemesi: kullanıcı isteğiyle bu turda yapılmadı.
- [ ] Çeviriler (de / fr / it / ru / zh) ana dili konuşan biri tarafından gözden geçirilmedi.
- [ ] Commit: 1.3 aday 2 ve 3 ile 1.4 değişiklikleri commit edilmedi (onay bekliyor).
- [ ] 1.4.0 adayı gözden geçirilip onaylanmadı.
