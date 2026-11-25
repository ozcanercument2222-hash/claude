# VSTM Tune 1.3.0 – Yayın kontrol listesi (durum: aday 3)

**[x]** = tamam (kanıtı rapordadır), **[~]** = kısmen, **[ ]** = yapılmadı.
**1.3.0 bu liste tamamlanmadan etiketlenmeyecek ve yayımlanmayacak.**

- [x] **1.2.0 korundu ve geri dönüş noktası doğrulandı:**
  - zip SHA-256, manifest ve byte byte aynı yeniden derleme;
  - git'te ayrı commit: `77433e0` (1.2.0), `f146f26` (1.3 aday 1);
  - aday 2 ve 3 commit edilmedi.
- [~] **Hard tune geçiş gerilemesi incelendi:**
  - HQ'da duyulan medyan 11,2 → 6,3 ms;
  - REALTIME'da 18,7 → 11,7 ms (aday 2);
  - |p95| hedefi (≤ 15 ms) tutmuyor (HQ 29,7 ms, REALTIME 38,0 ms): bas ve yavaş kaymalar. Rapor bölüm 2 ve 8.
- [~] **Tepe / aşırı yük riski ele alındı ve zorlandı:** 145 kayıtta 0 dBFS aşımı 0 (aday 1 ve 2). Gerçek tepe +0,6 dBTP'ye kadar
  (formant +12, sıcak giriş). Dinleme yok.
- [~] **Doğal uç vibrato düzeltildi (aday 3), dinlenmedi:** ROBOTISM %80'de vibrato %47 → %74, %100'de REALTIME %81 → %92.
  Nota kararları ve kayma aynı (rapor bölüm 9). Adaptive varsayılan kapalı; ayrıca değerlendirilmedi.
- [~] **Perde algılayıcı gerilemesi:**
  - HQ'da yok (bit bit aynı);
  - REALTIME'da oktav dizileri azaldı, voiced isabeti 0,02–0,2 puan düştü;
  - gürültülü (5 dB SNR) sentetik kayıtta bir yeni ≥ 50 ms oktav dizisi var (rapor 8.3).
- [ ] **Oktav / voiced uç durumları genişletilmiş kapsamla incelenmedi.**
- [~] **Transpoze / formant normal ve uç ayarlarda:** tepeler ölçüldü. Artefakt karakteri dinlenmedi.
- [~] **ROBOTISM:** %0 / %50 / %65 / %80 / %100 ölçüldü (REALTIME ve HQ). Tüm aralık ve otomasyonla yeniden test edilmedi.
- [ ] **Auto Key gerçek kayıtla test edilmedi.** Tonu etiketli gerçek set yok.
- [ ] **Kör, seviye eşleştirilmiş dinleme:** aday 1 paketi (9) ve aday 3 paketi (9: REALTIME + doğal uç) hazır, **dinlenmedi**.
- [ ] **Gerçek DAW doğrulaması:** REAPER, FL Studio, Ableton **NOT TESTED**.
- [ ] **CPU / en kötü blok / gecikme gerçek donanımda ölçülmedi.** Sanal makinede göreli: gerileme yok (aday 2 / aday 1 = 0,98).
- [x] **REALTIME / HQ geçiş bütünlüğü:** 132 durum, 1.2.0 ile aynı (aday 2'de de).
- [x] **1.0 / 1.1 / 1.2 preset ve projeleriyle uyumluluk:** PluginTest 73 / 73 (aday 3).
- [x] **Parametre kimlikleri, sırası, aralıkları ve durum biçimi korundu.**
- [~] **Windows / macOS temiz derleme:**
  - aday 1 Windows'ta CI ile derlendi; motor ve plugin testi geçti;
  - macOS işi makine bulunamadığı için kuyrukta kaldı;
  - aday 2 ve 3 commit edilmediği için CI'da derlenmedi.
- [~] **Testler, raporlar, günlükler arşivlendi:** `artifacts/audio-runs/2026-11-24_q13/`.
- [ ] **Yayını engelleyen bilinen sorun:** dinleme ve DAW sonuçları gelmeden karar verilemez.
- [ ] **1.3.0 sürüm adayı gözden geçirilip onaylanmadı.**
