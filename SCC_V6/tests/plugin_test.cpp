// SCC V6 plugin katmani testi (JUCE gerektirir). PLUGIN DERLEMESINE DAHIL DEGILDIR.
//
//   cmake -B build -DSCC_BUILD_TESTS=ON
//   cmake --build build --config Release --target SCC_PluginTest
//   build/SCC_PluginTest_artefacts/Release/SCC_PluginTest [goruntu_klasoru] [--vst3 yol/SCC.vst3]
//
// DSP testleri (dsp_test.cpp, v6_test.cpp) ses cekirdegini olcer. Bu program
// plugin'in geri kalanini dogrular:
//   parametre listesi ve V5 uyumlulugu, state kaydet / yukle, V5 oturumu yukleme,
//   processBlock (mono / stereo, otomasyon), karakter presetleri (kaydet, yukle,
//   yeniden adlandir, sil), A/B, ve arayuzun acilip cizilebilmesi.
// Bir klasor verilirse arayuzun ekran goruntuleri oraya PNG olarak yazilir.
// --vst3 ile derlenmis VST3 verilirse, plugin bir DAW'in yapacagi gibi VST3 arayuzu
// uzerinden yuklenir ve sinanir (tarama, parametreler, ses, state, editor).
#ifdef SCC_BUILD_PLUGIN_TEST

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cstdio>
#include <set>

static int failures = 0;
#define CHECK(cond, ...)                                          \
    do { if (cond) std::printf ("  [OK]   "); else { std::printf ("  [FAIL] "); ++failures; } \
         std::printf (__VA_ARGS__); std::printf ("\n"); } while (0)

namespace
{
    constexpr double kFs = 48000.0;

    float paramValue (SCCAudioProcessor& p, const juce::String& id)
    {
        return p.apvts.getRawParameterValue (id)->load();
    }

    bool near (float a, float b, float tol = 1.0e-4f) { return std::abs (a - b) <= tol; }

    // Ses benzeri test sinyali: harmonikli ton + hece zarfi
    void fillVoice (juce::AudioBuffer<float>& buffer, juce::int64 startSample, double fs = kFs)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const double t   = (double) (startSample + i) / fs;
            const double syl = std::pow (std::abs (std::sin (juce::MathConstants<double>::pi * 3.1 * t)), 1.5);
            double s = 0.0;
            for (int h = 1; h <= 12; ++h)
                s += std::sin (juce::MathConstants<double>::twoPi * 140.0 * h * t) / (double) h;
            const float v = (float) (0.32 * syl * s);
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample (ch, i, v);
        }
    }

    // processBlock'u blok blok calistir; cikisin RMS'ini (dB) dondur
    double run (SCCAudioProcessor& p, double seconds, int block = 512, int channels = 2, bool* finiteOut = nullptr)
    {
        juce::AudioBuffer<float> buffer (channels, block);
        juce::MidiBuffer midi;
        double energy = 0.0;
        juce::int64 pos = 0, counted = 0;
        bool finite = true;
        const juce::int64 total = (juce::int64) (seconds * kFs);

        while (pos < total)
        {
            fillVoice (buffer, pos);
            p.processBlock (buffer, midi);
            for (int i = 0; i < block; ++i)
            {
                const float s = buffer.getSample (0, i);
                if (! std::isfinite (s)) finite = false;
                if (pos + i > total / 2) { energy += (double) s * s; ++counted; }
            }
            pos += block;
        }
        if (finiteOut != nullptr) *finiteOut = finite;
        return 10.0 * std::log10 (energy / (double) juce::jmax ((juce::int64) 1, counted) + 1.0e-20);
    }

    void pump (int ms)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    bool saveSnapshot (juce::Component& c, const juce::File& file, float scale)
    {
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
        file.deleteFile();
        juce::FileOutputStream out (file);
        if (! out.openedOk())
            return false;
        juce::PNGImageFormat png;
        return png.writeImageToStream (image, out);
    }
}

//==============================================================================
int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;

    juce::File shotDir, vst3File;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        if (arg == "--vst3" && i + 1 < argc)
            vst3File = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[++i]));
        else
            shotDir = juce::File::getCurrentWorkingDirectory().getChildFile (arg);
    }

    //--------------------------------------------------------------------------
    std::printf ("=== 1) Parametreler ===\n");
    {
        SCCAudioProcessor p;
        std::set<juce::String> ids;
        int automatable = 0;
        for (auto* param : p.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
            {
                ids.insert (rp->getParameterID());
                if (rp->isAutomatable()) ++automatable;
            }
        const int n = p.getParameters().size();
        CHECK ((int) ids.size() == n && n == 49, "%d parametre, kimliklerin hepsi benzersiz", n);
        CHECK (automatable == n, "hepsi DAW otomasyonuna acik (%d / %d)", automatable, n);

        // V5 parametreleri: kimlik, aralik, varsayilan ayni
        struct V5 { const char* id; float lo, hi, def; };
        const V5 v5[] = { { "amount", 0, 100, 50 }, { "ratio", 1, 100, 4 }, { "mode", 0, 2, 0 },
                          { "autogain", 0, 1, 1 }, { "lowcut", 0, 1, 0 }, { "bypass", 0, 1, 0 } };
        bool same = true;
        for (const auto& e : v5)
        {
            auto* rp = p.apvts.getParameter (e.id);
            if (rp == nullptr) { same = false; continue; }
            const auto& range = rp->getNormalisableRange();
            if (std::abs (range.start - e.lo) > 1e-6f || std::abs (range.end - e.hi) > 1e-6f
                || std::abs (rp->convertFrom0to1 (rp->getDefaultValue()) - e.def) > 1e-4f)
                same = false;
        }
        CHECK (same, "V5'in 6 parametresi: kimlik, aralik ve varsayilanlar degismedi");

        // yeni ozellikler varsayilan olarak kapali -> yeni acilan plugin V5 gibi calar
        CHECK (paramValue (p, "analog") < 0.5f && paramValue (p, "char_on") < 0.5f, "Analog Mode ve Character varsayilan olarak kapali");

        // Analog parametre araliklari (TODO'daki degerler) ve logaritmik olcek
        auto range = [&p] (const char* id) { return p.apvts.getParameter (id)->getNormalisableRange(); };
        CHECK (near (range ("a_attack").start, 0.1f) && near (range ("a_attack").end, 100.0f)
               && std::abs (range ("a_attack").convertFrom0to1 (0.5f) - 3.162f) < 0.01f,
               "Attack 0.1 - 100 ms, logaritmik (knob ortasi %.2f ms)", range ("a_attack").convertFrom0to1 (0.5f));
        CHECK (near (range ("a_release").start, 10.0f) && near (range ("a_release").end, 1000.0f)
               && std::abs (range ("a_release").convertFrom0to1 (0.5f) - 100.0f) < 0.1f,
               "Release 10 - 1000 ms, logaritmik (knob ortasi %.1f ms)", range ("a_release").convertFrom0to1 (0.5f));
        CHECK (near (range ("a_thr").start, -60.0f) && near (range ("a_thr").end, 0.0f), "Threshold -60 - 0 dB");
        CHECK (near (range ("a_knee").end, 24.0f) && near (range ("a_makeup").end, 24.0f), "Knee 0 - 24 dB, Makeup 0 - 24 dB");

        auto* ratio = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter ("a_ratio"));
        CHECK (ratio != nullptr && ratio->choices.size() == 10 && ratio->choices[0] == "1.5:1" && ratio->choices[9] == "LIMIT",
               "Analog ratio: %s", ratio != nullptr ? ratio->choices.joinIntoString (", ").toRawUTF8() : "?");

        CHECK (p.getBypassParameter() == p.apvts.getParameter ("bypass"), "host'un bypass dugmesi plugin'in yumusak BYPASS'ina bagli");
        CHECK (p.getLatencySamples() == 0, "bildirilen gecikme: 0 ornek");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 2) V5 oturumu yukleme ===\n");
    {
        // V5'in kaydettigi bicimde bir state: yalnizca 6 parametre + preset
        juce::ValueTree v5 ("SCC_STATE");
        auto add = [&v5] (const char* id, float value)
        {
            juce::ValueTree c ("PARAM");
            c.setProperty ("id", id, nullptr);
            c.setProperty ("value", value, nullptr);
            v5.appendChild (c, nullptr);
        };
        add ("amount", 85.0f); add ("ratio", 12.0f); add ("mode", 2.0f);
        add ("autogain", 1.0f); add ("lowcut", 1.0f); add ("bypass", 0.0f);
        v5.setProperty ("preset", 5, nullptr);
        v5.setProperty ("uiWidth", 1200, nullptr);

        juce::MemoryBlock block;
        if (auto xml = v5.createXml())
            juce::AudioProcessor::copyXmlToBinary (*xml, block);

        SCCAudioProcessor p;
        // once V6 ozelliklerini ac: V5 state'i yuklenince hepsi kapanmali
        p.setParameterReal ("analog", 1.0f);
        p.setParameterReal ("char_on", 1.0f);
        p.setParameterReal ("char_amt", 100.0f);
        p.setParameterReal ("ca_type", (float) scc::kCharDrive);
        p.setStateInformation (block.getData(), (int) block.getSize());

        CHECK (std::abs (paramValue (p, "amount") - 85.0f) < 0.01f && std::abs (paramValue (p, "ratio") - 12.0f) < 0.01f
               && near (paramValue (p, "mode"), 2.0f) && paramValue (p, "lowcut") > 0.5f,
               "V5 degerleri yuklendi (amount %.0f, ratio %.0f, Hard, low cut)", paramValue (p, "amount"), paramValue (p, "ratio"));
        CHECK (p.getCurrentPreset() == 5, "V5 preset secimi yuklendi (%s)", SCCAudioProcessor::getPresetName (p.getCurrentPreset()));
        CHECK (paramValue (p, "analog") < 0.5f && paramValue (p, "char_on") < 0.5f
               && std::abs (paramValue (p, "char_amt") - 50.0f) < 0.01f && (int) paramValue (p, "ca_type") == scc::kCharWarm,
               "V5 state'inde olmayan V6 parametreleri varsayilana dondu (Analog ve Character kapali)");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 3) State kaydet / yukle (oturum geri cagirma) ===\n");
    {
        SCCAudioProcessor a;
        juce::Random rng (42);
        for (auto* param : a.getParameters())
            param->setValueNotifyingHost (rng.nextFloat());
        a.setSlotPresetName (0, "Velvet Even");
        a.setSlotPresetName (1, "My Own Thing");

        juce::MemoryBlock block;
        a.getStateInformation (block);

        SCCAudioProcessor b;
        b.setStateInformation (block.getData(), (int) block.getSize());

        // gercek (adimina oturmus) degerleri karsilastir: ses motorunun okudugu sayilar bunlar
        float worst = 0.0f;
        for (auto* param : a.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
            {
                const auto& range = rp->getNormalisableRange();
                const float span = std::max (1.0e-6f, range.end - range.start);
                worst = std::max (worst, std::abs (paramValue (a, rp->getParameterID()) - paramValue (b, rp->getParameterID())) / span);
            }
        CHECK (worst < 1.0e-5f, "49 parametrenin hepsi ayni geri geldi (en buyuk goreli fark %.1e)", (double) worst);
        CHECK (b.getSlotPresetName (0) == "Velvet Even" && b.getSlotPresetName (1) == "My Own Thing", "A / B slotlarinin karakter adlari geri geldi");

        // Designer'in 15 degeri de state'in parcasi
        bool specSame = true;
        for (int s = 0; s < 2; ++s)
            if (! (a.getSlotSpec (s) == b.getSlotSpec (s))) specSame = false;
        CHECK (specSame, "Character Designer degerleri (2 x 16) state ile seri hale geliyor");

        // bozuk / yabanci veri plugin'i bozmaz
        const char junk[] = "this is not a plugin state";
        b.setStateInformation (junk, (int) sizeof (junk));
        b.setStateInformation (nullptr, 0);
        CHECK (near (paramValue (a, "amount"), paramValue (b, "amount")) && near (paramValue (a, "ca_drive"), paramValue (b, "ca_drive")),
               "bozuk state yok sayiliyor");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 4) processBlock ===\n");
    {
        SCCAudioProcessor p;
        p.setPlayConfigDetails (2, 2, kFs, 512);
        p.prepareToPlay (kFs, 512);
        p.setParameterReal ("autogain", 0.0f);

        bool finite = true;
        const double base = run (p, 2.0, 512, 2, &finite);

        // Analog Mode: hafif ayar -> adaptif %50'den farkli seviye
        p.setParameterReal ("analog", 1.0f);
        p.setParameterReal ("a_thr", -40.0f);
        p.setParameterReal ("a_ratio", 9.0f);                    // LIMIT
        const double analog = run (p, 2.0, 512, 2, &finite);
        CHECK (finite && analog < base - 6.0, "Analog Mode (thr -40, LIMIT): cikis %.1f dB (Adaptive: %.1f dB)", analog, base);

        p.setParameterReal ("a_makeup", 12.0f);
        const double makeup = run (p, 2.0);
        CHECK (std::abs ((makeup - analog) - 12.0) < 0.5, "Makeup +12 dB -> cikis %+.1f dB", makeup - analog);
        p.setParameterReal ("a_makeup", 0.0f);
        p.setParameterReal ("analog", 0.0f);
        run (p, 0.5);

        // Character: acinca ses degisir, kapatinca geri gelir
        p.setParameterReal ("char_on", 1.0f);
        p.setParameterReal ("char_amt", 100.0f);
        p.setParameterReal ("ca_type", (float) scc::kCharDrive);
        const double drive = run (p, 2.0, 512, 2, &finite);
        CHECK (finite && std::abs (drive - base) > 0.3, "Character DRIVE %%100: cikis %.1f dB (kapaliyken %.1f dB)", drive, base);

        // A/B: B = Clean -> farkli ses, ayni kompresor
        p.setParameterReal ("cb_type", (float) scc::kCharClean);
        p.setActiveSlot (1);
        const double slotB = run (p, 2.0);
        p.setActiveSlot (0);
        const double slotA = run (p, 2.0);
        CHECK (std::abs (slotA - drive) < 0.3 && std::abs (slotB - slotA) > 0.3, "A/B: A (DRIVE) %.1f dB, B (CLEAN) %.1f dB, A'ya donus %.1f dB", drive, slotB, slotA);

        // BYPASS: cikis = giris
        p.setParameterReal ("bypass", 1.0f);
        run (p, 0.5);
        juce::AudioBuffer<float> buffer (2, 512), ref (2, 512);
        juce::MidiBuffer midi;
        fillVoice (buffer, 100000);
        ref.makeCopyOf (buffer);
        p.processBlock (buffer, midi);
        float maxErr = 0.0f;
        for (int i = 0; i < 512; ++i) maxErr = std::max (maxErr, std::abs (buffer.getSample (0, i) - ref.getSample (0, i)));
        CHECK (maxErr < 1.0e-6f, "BYPASS acik: cikis = giris (fark %.1e)", (double) maxErr);
        p.setParameterReal ("bypass", 0.0f);

        // mono, kucuk / buyuk / degisken bloklar, baska ornekleme hizi
        SCCAudioProcessor m;
        juce::AudioProcessor::BusesLayout mono;
        mono.inputBuses.add (juce::AudioChannelSet::mono());
        mono.outputBuses.add (juce::AudioChannelSet::mono());
        const bool monoOk = m.checkBusesLayoutSupported (mono) && m.setBusesLayout (mono);
        m.setRateAndBufferSizeDetails (96000.0, 64);
        m.prepareToPlay (96000.0, 64);
        m.setParameterReal ("char_on", 1.0f);
        bool monoFinite = true;
        for (int block : { 1, 17, 64, 1024, 4096 })
        {
            bool f = true;
            run (m, 0.3, block, 1, &f);
            monoFinite = monoFinite && f;
        }
        CHECK (monoOk && monoFinite, "mono @ 96 kHz, 1 / 17 / 64 / 1024 / 4096 ornekli bloklar: cikis sonlu");

        juce::AudioProcessor::BusesLayout bad;
        bad.inputBuses.add (juce::AudioChannelSet::mono());
        bad.outputBuses.add (juce::AudioChannelSet::stereo());
        CHECK (! m.checkBusesLayoutSupported (bad), "mono giris / stereo cikis gibi uyumsuz duzenler reddediliyor");

        // otomasyon: her blokta butun parametreler rastgele
        juce::Random rng (7);
        juce::AudioBuffer<float> ab (2, 256);
        bool autoFinite = true;
        for (int b = 0; b < 600; ++b)
        {
            for (auto* param : p.getParameters())
                if (rng.nextFloat() < 0.3f)
                    param->setValueNotifyingHost (rng.nextFloat());
            fillVoice (ab, (juce::int64) b * 256);
            p.processBlock (ab, midi);
            for (int i = 0; i < 256; ++i)
                if (! std::isfinite (ab.getSample (0, i)) || ! std::isfinite (ab.getSample (1, i))) autoFinite = false;
        }
        CHECK (autoFinite, "3.2 sn boyunca butun parametreler rastgele otomasyonla degisirken cikis sonlu");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 5) Karakter presetleri (CharacterPresetManager) ===\n");
    {
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("scc_preset_test_" + juce::String (juce::Random::getSystemRandom().nextInt (1000000)));
        dir.deleteRecursively();

        CharacterPresetManager pm (dir);
        const int numFactory = pm.getNumFactoryPresets();
        CHECK (numFactory >= 10 && pm.getNumPresets() == numFactory, "%d fabrika karakteri, kullanici klasoru bos", numFactory);

        bool factorySane = true;
        for (int i = 0; i < numFactory; ++i)
        {
            const auto& pr = pm.getPreset (i);
            if (! pr.factory || pr.spec.type != scc::kCharCustom) factorySane = false;
            for (int k = 0; k < scc::kNumCustomParams; ++k)
                if (pr.spec.custom[k] < scc::customParamInfo (k).minValue || pr.spec.custom[k] > scc::customParamInfo (k).maxValue)
                    factorySane = false;
        }
        CHECK (factorySane, "fabrika karakterlerinin butun degerleri gecerli aralikta");

        scc::CharacterSpec spec;
        spec.type = scc::kCharCustom;
        for (int k = 0; k < scc::kNumCustomParams; ++k)
            spec.custom[k] = scc::customParamInfo (k).minValue + 0.37f * (float) (k + 1);

        CHECK (pm.saveUserPreset ("My Vocal", spec, false).wasOk() && pm.getNumPresets() == numFactory + 1, "Save: \"My Vocal\" kaydedildi");
        CHECK (pm.saveUserPreset ("My Vocal", spec, false).failed(), "ayni adla tekrar kaydetme (uzerine yazma kapali) reddedildi");
        CHECK (pm.saveUserPreset ("Init (Blank)", spec, true).failed(), "fabrika karakterinin adiyla kaydetme reddedildi");
        CHECK (pm.saveUserPreset ("  ", spec, false).failed(), "bos ad reddedildi");
        CHECK (CharacterPresetManager::sanitiseName ("a/b\\c:d*?\"<>|. ") == "abcd", "dosya adina uymayan karakterler temizleniyor");

        // diskten geri oku: yeni bir yonetici ayni preseti ayni degerlerle gorur
        CharacterPresetManager pm2 (dir);
        const int idx = pm2.indexOf ("My Vocal");
        CHECK (idx >= numFactory && pm2.getPreset (idx).spec == spec && ! pm2.getPreset (idx).factory, "Load: diskten ayni 15 deger + tip geri okundu");

        // uzerine yaz
        spec.custom[scc::kCpDrive] = 77.0f;
        CHECK (pm.saveUserPreset ("My Vocal", spec, true).wasOk()
               && std::abs (pm.getPreset (pm.indexOf ("My Vocal")).spec.custom[scc::kCpDrive] - 77.0f) < 1e-4f, "Save (uzerine yaz): degisiklik kaydedildi");

        CHECK (pm.renameUserPreset ("My Vocal", "Lead Vox").wasOk() && pm.indexOf ("My Vocal") < 0 && pm.indexOf ("Lead Vox") >= numFactory,
               "Rename: \"My Vocal\" -> \"Lead Vox\"");
        CHECK (pm.renameUserPreset ("Default", "X").failed() && pm.deleteUserPreset ("Default").failed(), "fabrika karakterleri yeniden adlandirilamaz / silinemez");

        pm.saveUserPreset ("Second", spec, false);
        CHECK (pm.renameUserPreset ("Second", "Lead Vox").failed(), "var olan bir ada yeniden adlandirma reddedildi");

        // bozuk dosya listeyi bozmaz
        dir.getChildFile ("broken.scchar").replaceWithText ("<not really xml");
        dir.getChildFile ("other.scchar").replaceWithText ("<SOMETHING_ELSE/>");
        pm.refresh();
        CHECK (pm.getNumPresets() == numFactory + 2, "bozuk / yabanci dosyalar yok sayiliyor (%d kullanici karakteri)", pm.getNumPresets() - numFactory);

        CHECK (pm.deleteUserPreset ("Lead Vox").wasOk() && pm.indexOf ("Lead Vox") < 0 && ! dir.getChildFile ("Lead Vox.scchar").exists(), "Delete: dosya silindi");
        dir.deleteRecursively();
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 6) Slotlar: preset yukleme, degisiklik isareti, kopyala, degistir ===\n");
    {
        SCCAudioProcessor p;
        auto& pm = p.getCharacterPresets();
        const int crunch = pm.indexOf ("Crunch"), velvet = pm.indexOf ("Velvet Even");

        p.loadCharacterPreset (0, crunch);
        CHECK (p.getSlotSpec (0) == pm.getPreset (crunch).spec && p.getSlotPresetName (0) == "Crunch" && (int) paramValue (p, "ca_type") == scc::kCharCustom,
               "preset A slotuna yuklendi: 15 deger + tip parametrelere yazildi");
        CHECK (! p.isSlotModified (0), "yukledikten hemen sonra degisiklik isareti yok");

        p.setParameterReal ("ca_drive", 12.0f);
        CHECK (p.isSlotModified (0), "bir knob degisince degisiklik isareti (*) cikiyor");

        p.loadCharacterPreset (1, velvet);
        const auto a = p.getSlotSpec (0), b = p.getSlotSpec (1);
        p.swapSlots();
        CHECK (p.getSlotSpec (0) == b && p.getSlotSpec (1) == a && p.getSlotPresetName (0) == "Velvet Even" && p.getSlotPresetName (1) == "Crunch",
               "SWAP: A ve B yer degistirdi (adlariyla birlikte)");

        p.copySlot (0, 1);
        CHECK (p.getSlotSpec (1) == p.getSlotSpec (0) && p.getSlotPresetName (1) == "Velvet Even", "COPY: A, B'ye kopyalandi");

        // kompresor presetleri karakter slotlarina dokunmaz
        const auto before = p.getSlotSpec (0);
        p.setParameterReal ("analog", 1.0f);
        p.applyPreset (5);
        CHECK (p.getSlotSpec (0) == before && paramValue (p, "analog") < 0.5f && std::abs (paramValue (p, "amount") - 85.0f) < 0.01f,
               "kompresor preseti (Radio Rap): Adaptive'e doner, karakteri degistirmez");
    }

    //--------------------------------------------------------------------------
    std::printf ("\n=== 7) Arayuz ===\n");
    {
        SCCAudioProcessor p;
        p.setPlayConfigDetails (2, 2, kFs, 512);
        p.prepareToPlay (kFs, 512);

        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        auto* ed = dynamic_cast<SCCAudioProcessorEditor*> (editor.get());
        CHECK (ed != nullptr && editor->getWidth() == MainView::kW && editor->getHeight() == MainView::kH,
               "editor acildi: %d x %d", editor->getWidth(), editor->getHeight());

        // ses calarken arayuzu canlandir (metreler, dalga formu, spektrum dolsun)
        juce::AudioBuffer<float> buffer (2, 1024);
        juce::MidiBuffer midi;
        juce::int64 pos = 0;
        auto play = [&] (int frames)
        {
            for (int f = 0; f < frames; ++f)
            {
                for (int k = 0; k < 2; ++k) { fillVoice (buffer, pos); p.processBlock (buffer, midi); pos += 1024; }
                pump (33);
            }
        };

        if (ed != nullptr)
        {
            int shots = 0;
            auto shot = [&] (const char* name)
            {
                if (shotDir != juce::File())
                {
                    shotDir.createDirectory();
                    if (saveSnapshot (*editor, shotDir.getChildFile (name), 2.0f)) ++shots;
                }
            };

            // 1) ADAPTIVE (V5 gorunumu + karakter seridi)
            p.applyPreset (4);
            play (110);
            shot ("1_adaptive.png");

            // 2) ANALOG
            p.setParameterReal ("analog", 1.0f);
            p.setParameterReal ("a_thr", -24.0f);
            p.setParameterReal ("a_ratio", 5.0f);
            p.setParameterReal ("a_attack", 4.0f);
            p.setParameterReal ("a_release", 180.0f);
            p.setParameterReal ("a_makeup", 4.0f);
            p.setParameterReal ("char_on", 1.0f);
            p.setParameterReal ("char_amt", 60.0f);
            p.setParameterReal ("ca_type", (float) scc::kCharTape);
            play (110);
            shot ("2_analog.png");

            // 3) DESIGNER: hazir tip (TUBE)
            p.setParameterReal ("ca_type", (float) scc::kCharTube);
            p.setParameterReal ("char_amt", 85.0f);
            ed->showDesigner (true);
            play (110);
            CHECK (ed->isDesignerShown(), "Character Designer acildi");
            shot ("3_designer_tube.png");

            // 4) DESIGNER: CUSTOM, B slotu, fabrika karakteri
            p.setActiveSlot (1);
            p.loadCharacterPreset (1, p.getCharacterPresets().indexOf ("Dense Radio"));
            p.setParameterReal ("char_match", 1.0f);
            play (110);
            shot ("4_designer_custom.png");

            ed->showDesigner (false);
            play (5);
            CHECK (! ed->isDesignerShown(), "kompresor ekranina geri donuldu");

            // boyutlandirma: en kucuk ve buyuk pencere, oran korunur
            editor->setSize (600, 460);
            play (3);
            const bool smallOk = editor->getWidth() == 600;
            shot ("5_small.png");
            editor->setSize (1440, 1104);
            play (3);
            CHECK (smallOk && editor->getWidth() == 1440, "pencere 600 - 1920 piksel arasinda olceklenebiliyor");
            CHECK ((int) p.apvts.state.getProperty ("uiWidth", 0) == 1440, "pencere boyutu oturumla birlikte hatirlaniyor");

            if (shotDir != juce::File())
                CHECK (shots == 5, "%d ekran goruntusu yazildi: %s", shots, shotDir.getFullPathName().toRawUTF8());
        }

        // editor kapatilip yeniden acilabilir (DAW'da pencereyi kapat / ac)
        editor.reset();
        editor.reset (p.createEditor());
        pump (50);
        CHECK (editor != nullptr, "editor kapatilip yeniden acilabiliyor");
        editor.reset();
    }

    //--------------------------------------------------------------------------
    //  Derlenmis VST3'u bir host gibi yukle (bu programin icindeki siniflari degil,
    //  DAW'in gorecegi ikili dosyayi ve VST3 sarmalayicisini sinar)
    //--------------------------------------------------------------------------
    if (vst3File != juce::File())
    {
        std::printf ("\n=== 8) Derlenmis VST3, host tarafindan ===\n");

        juce::VST3PluginFormat format;
        juce::OwnedArray<juce::PluginDescription> found;
        format.findAllTypesForFile (found, vst3File.getFullPathName());
        CHECK (found.size() == 1, "tarama: %d plugin bulundu (%s)", found.size(), vst3File.getFileName().toRawUTF8());

        if (found.size() == 1)
        {
            const auto& desc = *found[0];
            std::printf ("  ad \"%s\", uretici \"%s\", surum %s, kategori \"%s\"\n",
                         desc.name.toRawUTF8(), desc.manufacturerName.toRawUTF8(), desc.version.toRawUTF8(),
                         desc.category.toRawUTF8());
            CHECK (desc.name == "SCC" && desc.manufacturerName == "SCC Audio" && ! desc.isInstrument && desc.version.startsWith ("6."),
                   "kimlik: SCC / SCC Audio / efekt / surum 6.x");

            juce::AudioPluginFormatManager manager;
            manager.addFormat (new juce::VST3PluginFormat());

            juce::String error;
            auto plugin = manager.createPluginInstance (desc, kFs, 512, error);
            CHECK (plugin != nullptr, "ornek olusturuldu %s", error.toRawUTF8());

            if (plugin != nullptr)
            {
                plugin->setPlayConfigDetails (2, 2, kFs, 512);
                plugin->prepareToPlay (kFs, 512);

                auto find = [&plugin] (const juce::String& name) -> juce::AudioProcessorParameter*
                {
                    for (auto* prm : plugin->getParameters())
                        if (prm->getName (64) == name) return prm;
                    return nullptr;
                };

                // host'un gordugu parametreler
                int named = 0;
                for (auto* prm : plugin->getParameters())
                    if (prm->getName (64).isNotEmpty() && prm->isAutomatable()) ++named;
                CHECK (named >= 49 && find ("Amount") != nullptr && find ("Analog Mode") != nullptr && find ("Character On") != nullptr
                       && find ("A Harmonic Amount") != nullptr && find ("B Soft / Hard") != nullptr,
                       "host %d otomasyon parametresi goruyor (V5 + Analog + Character + A/B Designer)", named);
                CHECK (plugin->getLatencySamples() == 0, "host'a bildirilen gecikme: %d ornek", plugin->getLatencySamples());

                auto process = [&plugin] (double seconds)
                {
                    juce::AudioBuffer<float> buffer (2, 512);
                    juce::MidiBuffer midi;
                    double energy = 0.0; juce::int64 pos = 0, counted = 0;
                    const juce::int64 total = (juce::int64) (seconds * kFs);
                    bool finite = true;
                    while (pos < total)
                    {
                        fillVoice (buffer, pos);
                        plugin->processBlock (buffer, midi);
                        for (int i = 0; i < 512; ++i)
                        {
                            const float smp = buffer.getSample (0, i);
                            if (! std::isfinite (smp)) finite = false;
                            if (pos + i > total / 2) { energy += (double) smp * smp; ++counted; }
                        }
                        pos += 512;
                    }
                    return finite ? 10.0 * std::log10 (energy / (double) juce::jmax ((juce::int64) 1, counted) + 1.0e-20) : 1000.0;
                };

                const double base = process (2.0);

                // host otomasyonuyla Analog Mode + LIMIT
                find ("Analog Mode")->setValueNotifyingHost (1.0f);
                find ("Analog Threshold")->setValueNotifyingHost (0.25f);          // -45 dB
                find ("Analog Ratio")->setValueNotifyingHost (1.0f);               // LIMIT
                find ("Auto Gain")->setValueNotifyingHost (0.0f);
                const double analog = process (2.0);
                CHECK (analog < base - 6.0, "host otomasyonu: Analog Mode + LIMIT -> cikis %.1f dB (once %.1f dB)", analog, base);

                // Character
                find ("Character On")->setValueNotifyingHost (1.0f);
                find ("Character Amount")->setValueNotifyingHost (1.0f);
                find ("A Character Type")->setValueNotifyingHost ((float) scc::kCharDrive / (float) (scc::kNumCharacterTypes - 1));
                const double drive = process (2.0);
                CHECK (drive < 900.0 && std::abs (drive - analog) > 0.3, "host otomasyonu: Character DRIVE -> cikis %.1f dB", drive);

                // state: host'un kaydettigi blok yeni bir ornekte ayni sesi verir
                juce::MemoryBlock state;
                plugin->getStateInformation (state);

                juce::String error2;
                auto second = manager.createPluginInstance (desc, kFs, 512, error2);
                bool stateOk = false;
                if (second != nullptr)
                {
                    second->setPlayConfigDetails (2, 2, kFs, 512);
                    second->prepareToPlay (kFs, 512);
                    second->setStateInformation (state.getData(), (int) state.getSize());

                    float worst = 0.0f;
                    for (int i = 0; i < plugin->getParameters().size(); ++i)
                        worst = std::max (worst, std::abs (plugin->getParameters()[i]->getValue() - second->getParameters()[i]->getValue()));
                    stateOk = worst < 1.0e-4f;

                    std::swap (plugin, second);
                    const double again = process (2.0);
                    std::swap (plugin, second);
                    CHECK (stateOk && std::abs (again - drive) < 0.2, "oturum geri cagirma: parametreler ayni, cikis %.1f dB (once %.1f dB)", again, drive);
                    second->releaseResources();
                }
                else
                {
                    CHECK (false, "ikinci ornek olusturulamadi");
                }

                // host bypass'i
                if (auto* bypass = plugin->getBypassParameter())
                {
                    bypass->setValueNotifyingHost (1.0f);
                    process (0.5);
                    juce::AudioBuffer<float> buffer (2, 512), ref (2, 512);
                    juce::MidiBuffer midi;
                    fillVoice (buffer, 12345);
                    ref.makeCopyOf (buffer);
                    plugin->processBlock (buffer, midi);
                    float maxErr = 0.0f;
                    for (int i = 0; i < 512; ++i) maxErr = std::max (maxErr, std::abs (buffer.getSample (0, i) - ref.getSample (0, i)));
                    CHECK (maxErr < 1.0e-6f, "host'un bypass dugmesi: cikis = giris (fark %.1e)", (double) maxErr);
                    bypass->setValueNotifyingHost (0.0f);
                }
                else
                {
                    CHECK (false, "host bypass parametresi bulunamadi");
                }

                // editor
                if (plugin->hasEditor())
                {
                    std::unique_ptr<juce::AudioProcessorEditor> editor (plugin->createEditorIfNeeded());
                    pump (300);
                    CHECK (editor != nullptr && editor->getWidth() >= 600 && editor->getHeight() >= 460,
                           "host editoru acti: %d x %d", editor != nullptr ? editor->getWidth() : 0, editor != nullptr ? editor->getHeight() : 0);
                    editor.reset();
                    pump (50);
                }

                plugin->releaseResources();
            }
        }
    }

    std::printf ("\n%s (%d hata)\n", failures == 0 ? "TUM PLUGIN TESTLERI GECTI" : "BAZI TESTLER BASARISIZ", failures);
    return failures == 0 ? 0 : 1;
}

#endif // SCC_BUILD_PLUGIN_TEST
