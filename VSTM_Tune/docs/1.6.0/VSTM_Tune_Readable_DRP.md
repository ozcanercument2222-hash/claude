# VSTM Tune – yetkiyle okunabilir `.drp` presetleri (1.6)

**Kapsam:** yalnız preset dosya biçimi, kaydetme/yükleme ve açıklama katmanı.

- DSP, parametre anlamları ve varsayılan değerler değişmedi.
- Ses iş parçacığına dosya işlemi, ayrıştırma, bellek ayırma ya da yetki kontrolü eklenmedi.

**Kanıt etiketleri:**

- **INSPECTED:** kaynak kod incelemesi.
- **MEASURED:** çalıştırılan test.

## 1. Mevcut sistem (INSPECTED, değiştirilmeden önce)

**İkili `.drp` (biçim 1, `Source/Drp.h/.cpp`):**

- 20 baytlık başlık: `VSTMDRP\x1A` sihirli baytları, sürüm, tür, uzunluk ve yükün CRC-32 değeri.
- Yük: gzip ile sıkıştırılmış ikili veri (parametre kimliği ve gerçek birimdeki float değer).
- Sınırlar: dosya en fazla 64 KiB + 84 bayt, açılmış yük en fazla 64 KiB.
- Not Defteri'nde okunamaz. Bu bir güvenlik özelliği değil: CRC yalnızca hasarı yakalar.

**Diğer okunan biçim:** 1.0 – 1.3 `.vstmpreset` (XML).

**Yetki dosyaları** de `.drp` biçimindedir (tür 2), ama Ed25519 imzası taşır. Bunlara dokunulmadı.

**Preset kapsamı** (`PresetManager::isPresetParameter`):

- Presete giren: oturuma ait olanlar dışındaki her parametre.
- Oturuma ait olanlar: bypass, realtime, range, tuningref, miditarget, midiout, midioctave.

**Oturum (host durumu):** presetten ayrı; APVTS XML'i ve özellikler.

**Taban çizgisi (MEASURED, değişiklikten önce, commit `a2603b6`):** EngineTest ve PluginTest'in tamamı geçiyordu. `.drp` bölümü 3h, yetki bölümü 3i.

## 2. Okunabilir biçim (biçim 2, `Source/DrpText.h/.cpp`)

**Yazım kuralları:**

- **Kodlama:** UTF-8, **BOM yok**, LF satır sonu.
  - Windows 10 / 11 Not Defteri BOM'suz UTF-8'i ve LF satır sonunu tanıyor (Windows 10 1809 sonrası).
  - Okurken BOM ve CRLF de kabul ediliyor; dosya başka bir editörde kaydedilse de açılıyor.
  - Bu bilinçli bir karar. Windows'ta gerçek Not Defteri testi **yapılmadı**.
- **Yapı:** INI benzeri. `#` (ve `;`) ile başlayan satırlar yorumdur ve okunmaz. Yeni bir bağımlılık eklenmedi.
- **Değerler:** parametrenin kendi biriminde, geri okununca **aynı 32 bit float'ı veren en kısa sade ondalıkla** yazılıyor (`80`, `75.3`, `0.35`). Kayıt → yükleme → kayıt bayt bayt aynı (test edildi).
- **Bölümler:**
  - `[Preset]`: `format = VSTM-DRP-TEXT`, `format_version = 2`, `plugin`, `plugin_version`, `state_version`, `name`.
  - `[Parameters]`: `kimlik = değer`.

**Her parametrenin üstündeki açıklama:**

- Görünen ad ve kimlik.
- Ne işe yaradığı.
- Motorun değeri nasıl kullandığı (`PluginProcessor::readParams` ile aynı).
- Aralık, adım, varsayılan, birim, eğiklik (skew), seçeneklerin listesi, host otomasyonu.
- Auto Character tarafından yönetilip yönetilmediği.
- Eklentinin gösterdiği metin ("Shown in the plug-in as").
- Tablolu seçeneklerin değer tabloları (geçiş ms, tolerans, güven, sentetik vibrato, attack, release) yazma anında koddaki dizilerden okunuyor.

**Auto Character:**

- Dosyanın başında `params::characterFor` formülleri var.
- 0, 25, 50, 75 ve 100 için örnekler yazma anında bu fonksiyon çağrılarak hesaplanıyor.
- Bu presetteki durum (AÇIK / KAPALI) ve etkin değerler de yazılıyor.
- Formülün koda eşitliği testte 201 noktada doğrulandı (en büyük fark 7e-6).

**Kaynak koddaki karşılığı olmayan formül yazılmadı.** Açıklaması bulunmayan bir kimlik için "No explanation for this parameter" yazılıyor.

**Gizli bilgi yok:**

- Yetki dosyası, anahtar, imza ya da seri numarası yazılmıyor (test edildi).
- Metin biçimin sağlama toplamı yok. Okunabilir dosya gizli de değildir, değiştirilmeye karşı korumalı da değildir; dosyanın kendisi de bunu söylüyor.

Örnek dosya: `Example_AutoCharacter_On.drp`, eklentinin kendi yazdığı dosya.

## 3. Yükleme ve uyumluluk

**Biçimi ayırma:**

- Dosya adı değil içerik belirleyicidir: ikili sihirli baytlarla başlıyorsa ikili okuyucu kullanılır.
- `VSTM-DRP-TEXT` işaretini taşıyan UTF-8 metin metin okuyucuya gider.
- Eski `.vstmpreset` dosyaları XML olarak okunuyor.

**Toleranslı okuma** (`drp::text::read`):

- Bilinmeyen parametre kimliği korunuyor (eklenti yok sayıyor). Yeniden adlandırma ve içe aktarmada dosyada kalıyor.
- Bilinmeyen bölüm yok sayılıyor.
- Bozuk satır atlanıp sayılıyor; aynı kimlik iki kez verilmişse ilki geçerli.
- Eksik değer varsayılanı alıyor; aralık dışı değer aralığa sıkıştırılıyor.

**Reddedilenler:**

| Durum | Hata |
|---|---|
| Biçim satırı yok | `badMagic` |
| Başka bir eklentinin dosyası | `wrongPlugin` |
| Daha yeni ana sürüm (3) | `unsupportedVersion` |
| Sürüm satırı yok | `badPayload` |
| Boş dosya | `truncated` |
| 512 KiB'tan büyük | `tooLarge` |
| UTF-8 olmayan veri | `badMagic` |

**Toplu dönüştürme yok:**

- Var olan dosyalara dokunulmuyor.
- Yeniden adlandırılan ya da içe aktarılan dosya geldiği biçimde kalıyor.

**Oturum (host durumu):**

- Preset metnini hiç içermiyor.
- Yalnız "readable" tercihini (`readablePresets`) taşıyor.

## 4. Yetki

Okunabilir kayıt yalnızca şu durumda yapılıyor: yetki aktif (admin) **ve** preset menüsünde "Save presets as readable text (.drp)" açık. Bu seçenek varsayılan olarak açık, kapatılabilir ve oturumla saklanıyor.

- **Kayıt penceresi:** biçimi açıkça yazıyor ("Format: readable text .drp (authorized)").
- **Yetki yokken:** menü öğesi görünmüyor; presetler bugünkü gibi ikili kaydediliyor.
- **Yükleme:** iki biçim de herkes için yükleniyor; yetki yüklemeyi kısıtlamıyor.
- **Yetkinin etki alanı:** yalnız kaydetme biçimini seçiyor. DSP'ye, ses iş parçacığına ve parametre değerlerine dokunmuyor.

## 5. Testler

**Çalıştırılan ve geçen testler (MEASURED, Linux):**

| Test | Sonuç |
|---|---|
| PluginTest 3j (yeni, 11 kontrol) | ✅ |
| Yetkisizken ikili kayıt | ✅ |
| Yetkili ve seçenek açıkken metin kayıt | ✅ |
| Başlık, her parametre ve açıklaması, gizli bilgi yok, üslü sayı yok | ✅ |
| Kayıt → yükleme → kayıt: değerler ve baytlar aynı | ✅ |
| Auto Character AÇIK ve KAPALI gidiş-dönüş, değer devri dahil | ✅ |
| Formül = `characterFor` (201 nokta) | ✅ |
| BOM ve CRLF; bilinmeyen alan, bozuk satır, bilinmeyen bölüm | ✅ |
| Reddedilenler (biçim, sürüm 3, başka eklenti, eksik sürüm, boş) | ✅ |
| İkili veri asla metin sayılmıyor | ✅ |
| İçe aktarmada aralık dışı değerin sıkıştırılması | ✅ |
| Eski ikili presetlerin yüklenmesi; yeniden adlandırmada biçim korunuyor; seçenek kapalıyken ikili kayıt | ✅ |
| Oturum durumu ayrı | ✅ |
| PluginTest'in tamamı (eski 3h `.drp` ve 3i yetki testleri dahil) | ✅ 104/104 |
| EngineTest | ✅ hepsi |
| pluginval seviye 10 | ✅ SUCCESS |

**Çalıştırılmayanlar:**

- Windows'ta Not Defteri ile gerçek dosya açma.
- Gerçek bir DAW'da preset yükleme.
- macOS.

## 6. Bilinen sınırlar

- **Elle eklenen yorumlar:** kaydederken açıklamalar yeniden üretiliyor; dosyaya senin eklediğin yorumlar yeniden kaydedince kaybolur. Değerler korunur.
- **Bilinmeyen kimlikler:** yeniden adlandırmada ve içe aktarmada korunuyor. Normal kayıt o anki durumu yazdığı için orada korunmuyor (ikili biçimde de böyleydi).
- **Gösterilen metin:** "Shown in the plug-in as" satırı eklentinin yuvarlanmış metnidir (75,3 → "75%"). Gerçek değer `=` satırındadır.
