# VSTM Tune 1.3.0 – Yayın kontrol listesi (durum: aday 1)

**[x]** = tamam (kanıtı rapordadır), **[~]** = kısmen, **[ ]** = yapılmadı.
**1.3.0 bu liste tamamlanmadan etiketlenmeyecek ve yayımlanmayacak.**

- [x] **1.2.0 korundu ve geri dönüş noktası doğrulandı:** zip SHA-256, manifest, byte byte aynı yeniden derleme.
  Git commit'i senin onayını bekliyor.
- [~] **Hard tune geçiş gerilemesi incelendi:**
  - HQ'da duyulan medyan 11,2 → 6,3 ms.
  - REALTIME'daki yapısal sınır belgelendi.
  - p95 hedefi (≤ 15 ms) tutmuyor: bas ve yavaş kaymalar. Takaslar ve reddedilen adaylar rapor bölüm 2'de.
- [~] **Tepe / aşırı yük riski ele alındı ve zorlandı:** 145 kayıtta 0 dBFS aşımı 0. Gerçek tepe +1,0 dBTP'ye kadar
  (formant +12). Dinleme yok.
- [ ] **Adaptive / vibrato sorunu düzeltilmedi.** Deneysel ve varsayılan kapalı kalıyor; belgede yazıyor.
- [x] **Perde algılayıcı gerilemesi yok:** kod değişmedi, hop kayıtları bit bit aynı.
- [ ] **Oktav / voiced uç durumları genişletilmiş kapsamla incelenmedi.**
- [~] **Transpoze / formant normal ve uç ayarlarda:** tepeler ölçüldü. Artefakt karakteri dinlenmedi.
- [ ] **ROBOTISM tüm aralıkta ve otomasyonla yeniden test edilmedi.** %0 dışında çıkış değişmedi.
- [ ] **Auto Key gerçek kayıtla test edilmedi.** Tonu etiketli gerçek set yok.
- [ ] **Kör, seviye eşleştirilmiş dinleme:** paket hazır (9 paket), **dinlenmedi**.
- [ ] **Gerçek DAW doğrulaması:** REAPER, FL Studio, Ableton **NOT TESTED**.
- [ ] **CPU / en kötü blok / gecikme gerçek donanımda ölçülmedi.** Sanal makinede göreli: gerileme yok.
- [x] **REALTIME / HQ geçiş bütünlüğü:** 132 durum, 1.2.0 ile aynı.
- [x] **1.0 / 1.1 / 1.2 preset ve projeleriyle uyumluluk:** PluginTest 73 / 73.
- [x] **Parametre kimlikleri, sırası, aralıkları ve durum biçimi korundu.**
- [ ] **Windows / macOS temiz derleme yapılmadı:** CI için commit + push gerekiyor. Linux yapıldı.
- [~] **Testler, raporlar, günlükler arşivlendi:** `artifacts/audio-runs/2026-11-24_q13/`.
- [ ] **Yayını engelleyen bilinen sorun:** dinleme ve DAW sonuçları gelmeden karar verilemez.
- [ ] **1.3.0 sürüm adayı gözden geçirilip onaylanmadı.**
