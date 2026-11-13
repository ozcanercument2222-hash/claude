# SCC – Single Knob Compressor

Tek knob'lu, sese göre kendini ayarlayan vokal kompresörü. Formatlar: **VST3**, **AU** (sadece macOS), **Standalone**.

## Derleme

Gerekenler: **CMake 3.22+** ve bir C++ derleyici.

- macOS: `xcode-select --install` ve `brew install cmake`
- Windows: Visual Studio 2022 ("Desktop development with C++") ve CMake
- Linux: `sudo apt install build-essential cmake libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libfreetype-dev libfontconfig1-dev libgl1-mesa-dev`

Proje klasörünün içinde:

```bash
# macOS / Linux
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j8

# Windows (Developer PowerShell)
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

İlk derlemede JUCE otomatik indirilir (internet gerekir, 5–10 dk sürebilir).
Elinde JUCE varsa: `cmake -B build -DSCC_JUCE_PATH=/yol/JUCE`

Çıktılar `build/SCC_artefacts/Release/` altında:

| Format | Yer |
|---|---|
| VST3 | `VST3/SCC.vst3` |
| AU (macOS) | `AU/SCC.component` |
| Standalone | `Standalone/SCC.app` (veya `.exe`) |

macOS/Linux'ta derleme bitince eklenti otomatik plugin klasörüne kopyalanır
(`~/Library/Audio/Plug-Ins/VST3`, `.../Components`). DAW'ı yeniden başlat / plugin taraması yap.
Windows'ta `SCC.vst3` klasörünü elle `C:\Program Files\Common Files\VST3` içine kopyala.
macOS "doğrulanamadı" derse: Sistem Ayarları → Gizlilik ve Güvenlik → "Yine de izin ver".

## Nasıl çalışır

**Tek knob (AMOUNT 0–100 %)** yalnızca threshold'u sürer: %0 → −1 dB, %100 → −32 dB (dB'de doğrusal).

**Attack / Release knob'dan bağımsızdır**, sinyalin dalga formuna göre sürekli ayarlanır.
Nominal değerler attack **20 ms**, release **100 ms**; sesin durumuna göre bunların çevresinde gezer
(sessizlikte tam 20 / 100'e döner). Ekrandaki kutular canlı değerleri gösterir.

- **Attack**: zirve, kısa vadeli ortalamadan çok yüksekse (keskin başlangıç, patlayıcı ünsüz) kısalır, ses oturup dolgunlaşınca uzar.
- **Release**: seviye hızlı dalgalanıyorsa (hece hece konuşma) kısalır, seviye durağansa (uzun nota) uzar. Pompalama olmaz.

**Ratio**: 2 / 4 / 8 / 12 / 16 / 18 tuşları veya CUSTOM kutusuna 1–100 arası istediğin değer (yazabilir ya da oklarla 0.5 adımla değiştirebilirsin).

**Karakter** (SMOOTH / BALANCED / HARD): diz (knee) genişliği ve adaptasyonun ne kadar hızlı olabileceği.

**Diz (knee)** hem karaktere hem knob'a bağlıdır. Karakter genişliğin üst sınırını belirler, knob ise onu ölçekler: knob arttıkça diz genişler. Yani kompresyon derinleştikçe geçiş yumuşar ve "boğma" azalır. Canlı değer KNEE kutusunda, bölgesi de threshold çubuğunda (eşiğin etrafındaki açık şerit) görünür.

| Knob | Smooth | Balanced | Hard |
|---|---|---|---|
| %0 | 4.2 dB | 2.1 dB | 0.5 dB |
| %25 | 7.2 dB | 3.6 dB | 0.9 dB |
| %50 | 9.0 dB | 4.5 dB | 1.1 dB |
| %75 | 10.6 dB | 5.3 dB | 1.3 dB |
| %100 | 12.0 dB | 6.0 dB | 1.5 dB |

Yönü tersine çevirmek (knob arttıkça diz daralsın) için `SCCDsp.h` içindeki `kKneeAt0PctFraction` değerini 0.35 yerine 1'den büyük yap (örn. 2.0). Üst sınırlar `characterFor` tablosunda.

**AUTO GAIN**: giriş ve çıkış enerjisini uzun vadede eşitler, yani açıp kapatınca "aynı yüksekliğte" karşılaştırırsın.

**LOW CUT** (varsayılan kapalı): yalnızca kompresörün *dedektörü* 100 Hz altını görmez, sesin kendisi değişmez. Bas ağırlıklı seslerde (kalın erkek vokal, nefes/patlama gürültüsü) düşük frekansın kompresörü gereksiz tetiklemesini önler.

**BYPASS**: yumuşak geçişli (tıksız) devre dışı bırakma. Açıkken buton turuncu, ekranda "BYPASSED" yazar.

**PRESET** (üst çubuk): 8 hazır ayar (Default, Light Touch, Natural Vocal, Smooth Control, Tight Vocal, Radio Rap, Full Smooth, Max Squash). Oklarla gez ya da ismin üstüne tıklayıp listeden seç. Hazır ayar sadece AMOUNT, RATIO ve karakteri ayarlar. Bir ayarı elle değiştirirsen ismin yanında `*` çıkar. Pencere boyutu da hatırlanır.

### Bilerek eklemediklerim

- **Lookahead**: attack zaten ~20 ms civarında; kazancı küçük, ama gecikme getirir (kayıt sırasında istenmez). Plugin sıfır gecikmeyle çalışır.
- **Oversampling**: kazanç eğrisi yumuşatıldığı için (yavaş attack/release) takma ses (aliasing) pratikte oluşmaz.
- **Dry/Mix, Input/Output knob'ları**: "tek knob" fikrini bozar.

Ekranda gri = giriş zarfı, yeşil çizgi = gain reduction. IN/OUT metreleri stereo.

## Ayar noktaları

Davranışı değiştirmek istersen `Source/SCCDsp.h` başındaki sabitler: threshold aralığı (−1 / −32), nominal attack/release (20 / 100), adaptasyon hassasiyeti (`kCrest*`, `kFlux*`) ve karakter tablosu (`characterFor`).

## DSP testi (JUCE gerekmez, plugin'e dahil degil)

```bash
# macOS / Linux
g++ -O2 -std=c++17 -DSCC_BUILD_DSP_TEST tests/dsp_test.cpp -o dsp_test && ./dsp_test

# Windows
cmake -B build -DSCC_BUILD_TESTS=ON
cmake --build build --config Release --target SCC_DspTest
build\Release\SCC_DspTest.exe
```

Threshold haritasi, statik kompresyon egrisi dogrulugu, adaptif attack/release davranisi, auto gain, bypass, low cut ve dayaniklilik (NaN/Inf, mono, 44.1-192 kHz) test edilir.

## Dosyalar

- `Source/SCCDsp.h` – DSP çekirdeği (JUCE'dan bağımsız)
- `Source/PluginProcessor.*` – parametreler ve ses işleme
- `Source/PluginEditor.*`, `Source/UIComponents.h` – arayüz (960×600, pencere büyütülebilir)
- `CMakeLists.txt` – derleme

JUCE, AGPLv3 veya ticari lisansla dağıtılır. Kişisel kullanım serbest; plugin'i satmayı düşünürsen JUCE lisansına bak.

## Sorun giderme

- Projeye (Projucer / kendi Visual Studio projen) **sadece `Source` klasorundekileri** ekle. `tests` klasoru plugin'e ait degildir.

- Derleme hatasından sonra (veya dosyaları değiştirince) `build` klasörünü silip komutları baştan çalıştır.
- Visual Studio'da klasörü CMake ile açtıysan: Project → Delete Cache and Reconfigure, sonra Build.
- Hata listesinde en önemlisi *ilk* hatadır; sonrakiler genelde ondan türer.
