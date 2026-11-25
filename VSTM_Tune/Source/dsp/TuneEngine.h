#pragma once
//==============================================================================
//  VSTM Tune ses motoru (JUCE'dan bagimsiz, test edilebilir)
//
//  Akis (her ornek icin, sabit analiz adimi = 32 x seyreltme orani, ~2.9 ms):
//
//    giris --> halka tampon --> alcak geciren + seyreltme --> YIN perde algilayici
//                                                            |
//                         ton bulucu (Auto) <----------------+
//                                                            v
//                                                 PitchCorrector (hedef perde)
//                                                            |
//    perde isaretleri (her periyotta bir, korelasyonla hizali)  v
//    --------------------------------------------> TD-PSOLA sentez (tanecik ekle-topla)
//                                                            |
//    giris (L ornek gecikmeli, "dry") --------------------> MIX / bypass --> cikis
//
//  voiced <-> unvoiced gecisleri (iki farkli tanecik turu: perdeye senkron ve sabit 4 ms):
//    - unvoiced -> voiced: unvoiced (zamana hizali) akis 4 ms daha surer ve voiced akisla
//      capraz gecis yapar; ilk voiced taneciginin eksik yukselen kenari duyulmaz. Eskiden
//      iki tur arka arkaya dikiliyordu: bir periyotluk genlik cukuru asimetrik glottal
//      darbeyi yarim birakip DC'li bir tik / alcak frekans "gumlemesi" yapiyordu.
//    - voiced -> unvoiced: unvoiced tanecikler son voiced taneciginin kaynak ofsetini
//      devralir ve yarilayarak sondurur (iki kopya faz disi ust uste binmez). Yalniz
//      kaydirma kucukken (<= 0.5 yarim ses tam, >= 1.5 yarim ses hic): buyuk kaydirmada
//      icerik zaten farklidir, ofset yalnizca zaman sicramasi olur.
//
//  Gecikme sabittir (REALTIME 20 ms, kapaliyken 40 ms) ve host'a bildirilir. Mod calarken
//  degisirse motor sifirlanmaz ve icerik ne tekrarlanir ne atlanir: gercek gecikme yeni degere
//  yumusak bir rampayla (yukseltilmis kosinus) kayar. Islenmis ses PSOLA ile zamanda esnetilir
//  (perde degismez); dry / bypass ayni rampayla, ara degerlemeyle okunur (rampa boyunca hafif hiz
//  degisimi, en cok ~%1.6 / 27 ct; dry duyulmuyorsa rampa kisa). Giris gecmisi, perde analizi ve
//  bulunan ton korunur.
//  Motor blok boyutundan bagimsizdir: ayni giris her blok boyutunda ayni cikisi verir.
//
//  Otomatik ton (KeyDetector) vokalden bir TAHMINDIR ve bir kez kilitlenir; host'un yeniden
//  hazirlamasi (prepare) onu silmez. Kullanici istekleri (kilitle / yeniden analiz) UI'dan
//  kilitsiz bir posta kutusuyla gelir ve ses is parcaciginda blok basinda uygulanir.
//==============================================================================
#include "DspCommon.h"
#include <array>
#include <limits>
#include "Scales.h"
#include "PitchDetector.h"
#include "KeyDetector.h"
#include "PitchCorrector.h"

namespace vstm
{
class TuneEngine
{
public:
    struct Params
    {
        int   key = -1;                 // -1 = Auto, 0..11 = C..B
        int   scale = kMajor;
        float amount = 1.0f;            // 0..1
        float retuneMs = 15.0f;
        float humanize = 0.35f;         // 0..1
        float robotism = 0.5f;          // 0 = Robotik, 1 = Insansi
        float vibratoScale = 1.0f;      // 0..2
        float formant = 0.0f;           // semitones
        float transpose = 0.0f;         // semitones
        float mix = 1.0f;               // 0..1
        bool  active = true;            // false = bypassed
        int   range = 1;                // 0 = high voice, 1 = mid, 2 = low
        bool  customScale = false;      // true: customMask decides the allowed notes, not the scale
        uint16_t customMask = 0x0AB5;   // bit i = the note i semitones above the key (default: major)
        bool  adaptive = false;         // Adaptive correction (PitchCorrector); off = unchanged engine
        bool  followKeyChanges = false; // Auto key: opt-in modulation tracking after the lock (off = the lock stays)
        bool  peakGuard = true;         // output safety limiter (internal, not a plug-in parameter; tests switch it off)
        bool  backdateSwitch = true;    // hard tune: backdate confirmed note switches (internal; tests switch it off)
        bool  freshAnalysis = true;     // REALTIME: newest-aligned detector windows (internal; tests switch it off)
        bool  swingKeep = true;         // human side: keep the retune speed across a switch in a vibrato swing (internal)
        bool  vibratoBand = true;       // 1.5: Vibrato Scale scales the vibrato band only, the note decision never (internal)
        float tuningRefHz = 440.0f;     // 1.5: A4 reference; the note grid (scale, Auto Key, display) moves with it
        bool  protectConsonants = false; // 1.5: the formant shift leaves unvoiced sound (s, sh, breaths) untouched
        bool  formantFollowsTranspose = false; // 1.5: the formant moves with the transpose ("chipmunk" / "giant")
        float transitionMs = -1.0f;     // 1.5: Note Transition in ms; < 0 = automatic (ROBOTISM)
        uint16_t bypassMask = 0;        // 1.5: custom scale: degrees that pass through uncorrected (relative to key)
        float toleranceCt = 0.0f;       // 1.5: tolerance (cents) around the target, 0 = off
        float confidenceMin = 0.0f;     // 1.5: detector clarity below which the correction fades out, 0 = off
        float synthVibRateHz = 0.0f;    // 1.5: synthetic vibrato rate
        float synthVibDepthCt = 0.0f;   // 1.5: synthetic vibrato depth (cents), 0 = off
        int   octave = 0;               // 1.5: octave shift added to the transpose (-2 .. +2)
        uint16_t midiMask = 0;          // 1.5: pitch classes of the held MIDI notes (MIDI target mode); 0 = none held
        float midiMinNoteS = -1.0f;     // 1.5 notes as MIDI: shorter notes are not sent (slide passing notes, blips);
                                        // < 0 = as long as the look-ahead allows: REALTIME 8 ms, HQ 15 ms (MEASURED)
        float midiMinGapS = 0.020f;     // 1.5 notes as MIDI: a shorter break in the voice keeps the note
        int midiNote = -1;              // 1.5 MIDI perform (roadmap 49): the voice goes to this note, octave as played
        float degreeCents[12] {};       // 1.5 (roadmap 45): cents each degree (from the key's tonic) sits off the equal grid
        int scaleSteps = 0;             // 1.5 (roadmap 41): diatonic transpose in steps of the scale (-7 .. +7)
        float attackMs = -1.0f;         // 1.5 (roadmap 27): correction fade-in at a phrase start (< 0 = automatic)
        float releaseMs = 0.0f;         // 1.5 (roadmap 27): correction fade-out on a decayed note (0 = off)
        bool naturalCentre = true;      // 1.6 (P0): vibrato-free note decision on the natural side (internal switch)
        bool naturalMotion = true;      // 1.6 (P0): only the centre is corrected during a vibrato (internal switch)
        bool naturalTransition = true;  // 1.6 (P0): the automatic glide follows the singer (internal switch)
        bool octaveGuard = true;        // 1.6 (P0): a sudden octave jump of the detection is held 40 ms (internal switch)
        int naturalCentreRef = 0;       // 1.6 experiments (WavProbe): 0 = centre2, 1 = 2-pole 1.5 Hz
    };

    struct DisplayPoint
    {
        float   inMidi = 0.0f;          // detected input pitch (meaningful when voiced)
        float   outMidi = 0.0f;         // corrected output pitch
        float   deviation = 0.0f;       // input minus its target note (semitones, before transpose)
        int16_t note = -1;              // target note incl. transpose, -1 = none
        uint8_t flags = 0;
        float   analysisDelay = 0.0f;   // seconds: the analysed audio is centred this long before the hop (tests, logs)
        float   clarity = 0.0f;         // detector clarity of this hop, 1 - CMND minimum (tests, logs)
    };
    enum : uint8_t { kVoiced = 1, kNoteChange = 2, kSettled = 4 };

    static int latencyFor (double sampleRate, bool realtime) noexcept
    {
        return (int) std::lround (sampleRate * (realtime ? 0.020 : 0.040));
    }

    void prepare (double sampleRate, bool realtimeMode)
    {
        sr = sampleRate;
        decim = std::max (1, (int) std::lround (sr / 11025.0));
        hop = 32 * decim;
        realtime = realtimeMode;
        latency = latencyFor (sr, realtime);
        unvoicedHop = std::max (32.0, std::round (sr * 0.004));

        for (auto& b : in)  b.assign ((size_t) kRing, 0.0f);
        for (auto& b : acc) b.assign ((size_t) kRing, 0.0f);
        for (auto& b : accU) b.assign ((size_t) kRing, 0.0f);
        xfade.assign ((size_t) kRing, -1.0f);
        monoLp.assign ((size_t) kRing, 0.0f);
        marks.assign ((size_t) kMarkRing, Mark {});

        detector.prepare (sr, decim);
        detector.setNewestAligned (realtime && params.freshAnalysis);
        const double fc = 0.30 * sr / decim;
        aa1.setLowpass (sr, fc, 0.5412);
        aa2.setLowpass (sr, fc, 1.3066);

        levelPowCoef = 1.0f - std::exp (-1.0f / (0.05f * (float) sr));
        levelGainCoef = 1.0f - std::exp (-1.0f / (0.03f * (float) sr));
        levelFastCoef = 1.0f - std::exp (-1.0f / (0.01f * (float) sr));
        levelDownCoef = 1.0f - std::exp (-1.0f / (0.001f * (float) sr));
        peakBlockLen = std::max (1, (int) std::lround (0.005 * sr));
        levelMinCount = (int) std::lround (0.02 * sr);

        applyRange();
        reset();                        // the key analysis (evidence and lock) is kept: it does not depend on the rate
    }

    /** REALTIME (20 ms) <-> HQ (40 ms) while audio runs. Called on the audio thread at a block
        boundary. The reported latency changes at once (the host compensates it); the real delay of
        the output glides to it, so no input is repeated or skipped (1.0 / 1.1 faded out, jumped the
        content by 20 ms and faded in). The synthesis keeps its own fixed time axis (accLatency): the
        offset D between that axis and the content time ramps along a raised cosine. Voiced grains pick
        their source marks along the moving content time (a PSOLA time-stretch: the pitch does not
        change), unvoiced grains follow it, and the dry signal is read at the same moving position
        (interpolated). The ramp takes kRampWetSeconds when only the processed voice is heard (MIX
        100 %, not bypassed) and kRampDrySeconds when the dry is heard: reading the dry at a moving
        position changes its speed by at most (pi/2) x 20 ms / 2 s = 1.6 % (27 ct). A switch during a
        ramp starts a new ramp from where the delay is. Analysis, marks, the input history and the key
        are kept. No allocation, no reset. */
    void setRealtime (bool shouldBeRealtime) noexcept
    {
        if (shouldBeRealtime == realtime)
            return;
        realtime = shouldBeRealtime;
        detector.setNewestAligned (realtime && params.freshAnalysis);   // analysis ~10 ms fresher in REALTIME (1.3)
        latency = latencyFor (sr, realtime);
        const int64_t tNow = nIn - accLatency;                  // the next output sample on the synthesis axis
        const double dNow = contentOffset ((double) tNow);
        rampFrom = dNow;
        rampTo = (double) (latency - accLatency);
        rampStart = tNow;
        const bool dryHeard = ! (params.active && params.mix >= 0.999f && mixSmooth >= 0.999f && wetSmooth >= 0.999f);
        const double full = std::max (1.0, (double) std::abs (latencyFor (sr, true) - latencyFor (sr, false)));
        rampLen = std::max (1.0, std::round (sr * (dryHeard ? kRampDrySeconds : kRampWetSeconds) * std::abs (rampTo - rampFrom) / full));
        ramping = std::abs (rampTo - rampFrom) > 1.0e-9;
    }

    /** True while the output's delay is still gliding to a new latency (tests, display). */
    bool isLatencyRamping() const noexcept  { return ramping; }

    //------------------------------------------------------------ Auto key requests (any thread)
    /** Lock the automatic key to `key` (0..11) now: "Lock now", "Use the suggested key", a restored
        session. conf = the confidence shown / saved with it. Applied at the next block. */
    void requestKeyLock (int key, float conf) noexcept
    {
        if (key < 0 || key > 11)
            return;
        keyRequestConf.store (std::isfinite (conf) ? clamp01 (conf) : 0.0f, std::memory_order_relaxed);
        keyRequest.store (key, std::memory_order_release);
    }
    /** Forget the evidence and the lock and listen again ("Re-analyze"). Applied at the next block. */
    void requestKeyReanalyze() noexcept     { keyRequest.store (kKeyReanalyze, std::memory_order_release); }

    /** What a saved session should hold: a lock request that has not reached the audio thread yet
        counts (a host may save right after restoring a session, before any audio ran). */
    int getKeyLockForState() const noexcept
    {
        const int r = keyRequest.load (std::memory_order_acquire);
        if (r >= 0) return r;
        if (r == kKeyReanalyze) return -1;
        return detectedKey.load();
    }
    float getKeyConfidenceForState() const noexcept
    {
        const int r = keyRequest.load (std::memory_order_acquire);
        if (r >= 0) return keyRequestConf.load (std::memory_order_relaxed);
        if (r == kKeyReanalyze || detectedKey.load() < 0) return 0.0f;
        return keyConfidence.load();
    }

    int getLatencySamples() const noexcept  { return latency; }
    bool isRealtime() const noexcept        { return realtime; }
    double getHopSeconds() const noexcept   { return hop / sr; }

    void reset()
    {
        for (auto& b : in)  std::fill (b.begin(), b.end(), 0.0f);
        for (auto& b : acc) std::fill (b.begin(), b.end(), 0.0f);
        for (auto& b : accU) std::fill (b.begin(), b.end(), 0.0f);
        std::fill (xfade.begin(), xfade.end(), -1.0f);
        std::fill (monoLp.begin(), monoLp.end(), 0.0f);
        aa1.reset(); aa2.reset();
        detector.reset();
        tracker.reset();
        corrector.reset();

        nIn = 0;
        decPhase = 0;
        markHead = markCount = 0;
        lastMarkPos = -unvoicedHop;
        lastMarkVoiced = false;
        curVoiced = false;
        curPeriod = 0.0;
        lastInMidi = 60.0f;
        recCount = 0;
        recWrite = 0;
        sNext = 0.0;
        lastS = -1.0e9;
        lastGrainSrc = -1.0e9;
        lastGrainVoiced = false;
        lastSrcOffset = 0.0;
        lastHOut = 0.0;
        tailActive = false;
        xfadeArmed = false;
        tailNext = tailLast = tailOffset = 0.0;
        xfadeEnd = -1;
        planValid = false;
        mixSmooth = params.mix;
        wetSmooth = params.active ? 1.0f : 0.0f;
        accLatency = latency;
        rampFrom = rampTo = 0.0;
        rampStart = 0;
        rampLen = 1.0;
        ramping = false;
        lagNow = (double) latency;
        levelGain = 1.0f;
        guardGain = 1.0f;
        guardHoldLeft = 0;
        levelPd = levelPv = levelFd = levelFv = 0.0f;
        std::fill (std::begin (peakD), std::end (peakD), 0.0f);
        std::fill (std::begin (peakV), std::end (peakV), 0.0f);
        peakDNow = peakVNow = 0.0f;
        peakPos = 0;
        levelCount = 0;
        levelHeld = levelActive ? levelPrior (params.formant - params.transpose) : 1.0f;
        currentNote.store (-1);
        currentVoiced.store (false);
        noteEvHead = noteEvCount = 0;
        midiCand = midiSent = -1;
        midiCandStart = midiLeave = 0.0;
        midiAway = false;
    }

    /** A value that is not a number (damaged preset / session, broken host) keeps the last good one;
        everything is limited to ranges that keep grain sizes and gains sane (the plugin's own ranges
        lie inside them, so normal values pass unchanged). */
    void setParams (const Params& requested) noexcept
    {
        Params p = requested;
        auto keep = [] (float v, float last, float lo, float hi) { return std::isfinite (v) ? std::clamp (v, lo, hi) : last; };
        p.amount       = keep (p.amount,       params.amount,         0.0f,    1.0f);
        p.retuneMs     = keep (p.retuneMs,     params.retuneMs,       0.0f, 1000.0f);
        p.humanize     = keep (p.humanize,     params.humanize,       0.0f,    1.0f);
        p.robotism     = keep (p.robotism,     params.robotism,       0.0f,    1.0f);
        p.vibratoScale = keep (p.vibratoScale, params.vibratoScale,   0.0f,    4.0f);
        p.formant      = keep (p.formant,      params.formant,      -24.0f,   24.0f);
        p.transpose    = keep (p.transpose,    params.transpose,    -24.0f,   24.0f);
        p.tuningRefHz  = keep (p.tuningRefHz,  params.tuningRefHz,  400.0f,  480.0f);
        p.transitionMs = keep (p.transitionMs, params.transitionMs,  -1.0f, 1000.0f);
        p.toleranceCt  = keep (p.toleranceCt,  params.toleranceCt,    0.0f,  100.0f);
        p.confidenceMin = keep (p.confidenceMin, params.confidenceMin, 0.0f,   1.0f);
        p.synthVibRateHz = keep (p.synthVibRateHz, params.synthVibRateHz, 0.0f, 12.0f);
        p.synthVibDepthCt = keep (p.synthVibDepthCt, params.synthVibDepthCt, 0.0f, 200.0f);
        p.octave = std::clamp (p.octave, -2, 2);
        if (p.octave != 0)                       // 1.5 (roadmap 35): octave shift on top of the transpose, up to +-24 st
            p.transpose = std::clamp (p.transpose + 12.0f * (float) p.octave, -24.0f, 24.0f);
        if (p.formantFollowsTranspose)          // 1.5 (roadmap 34): formant + transpose as one creative shift
            p.formant = std::clamp (p.formant + p.transpose, -24.0f, 24.0f);
        p.mix          = keep (p.mix,          params.mix,            0.0f,    1.0f);
        p.customMask = (uint16_t) (p.customMask & 0x0fff);
        p.bypassMask = (uint16_t) (p.bypassMask & 0x0fff);
        p.midiMask = (uint16_t) (p.midiMask & 0x0fff);
        p.midiMinNoteS = keep (p.midiMinNoteS, params.midiMinNoteS, -1.0f, 0.2f);
        p.midiMinGapS = keep (p.midiMinGapS, params.midiMinGapS, 0.0f, 0.2f);
        p.midiNote = std::clamp (p.midiNote, -1, 127);
        p.scaleSteps = std::clamp (p.scaleSteps, -7, 7);
        p.attackMs = keep (p.attackMs, params.attackMs, -1.0f, 1000.0f);
        p.releaseMs = keep (p.releaseMs, params.releaseMs, 0.0f, 2000.0f);
        for (int d = 0; d < 12; ++d)
            p.degreeCents[d] = keep (p.degreeCents[d], params.degreeCents[d], -100.0f, 100.0f);
        detector.setNewestAligned (realtime && p.freshAnalysis);
        if (p.key < -1 || p.key > 11) p.key = -1;
        p.scale = std::clamp (p.scale, 0, (int) kNumScales - 1);
        p.range = std::clamp (p.range, 0, 2);

        const bool rangeChanged = p.range != params.range;
        params = p;
        if (rangeChanged)
            applyRange();

        const bool shifting = std::abs (p.transpose) >= 0.5f || std::abs (p.formant) >= 0.05f;
        if (shifting && ! levelActive)
        {
            levelPd = levelPv = 0.0f;           // new measurement; start from the typical value
            levelCount = 0;
            levelHeld = levelPrior (p.formant - p.transpose);
        }
        levelActive = shifting;
        levelUp = shifting && p.formant - p.transpose < -0.5f;
    }

    /** In-place processing of up to two channels. */
    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        const int nc = std::min (2, std::max (1, numChannels));
        numCh = nc;
        applyKeyRequest();
        const float smoothCoef = 1.0f - std::exp (-1.0f / (0.02f * (float) sr));
        const float guardRelease = 1.0f - std::exp (-1.0f / (kGuardReleaseSeconds * (float) sr));
        const int guardHold = (int) std::lround (kGuardHoldSeconds * sr);
        const float wetTarget = params.active ? 1.0f : 0.0f;
        if (! std::isfinite (mixSmooth)) mixSmooth = params.mix;      // never stay stuck on a bad value
        if (! std::isfinite (wetSmooth)) wetSmooth = wetTarget;

        for (int i = 0; i < numSamples; ++i)
        {
            // ---- input
            float mono = 0.0f;
            for (int ch = 0; ch < nc; ++ch)
            {
                const float x = sanitize (channels[ch][i]);
                in[(size_t) ch][(size_t) (nIn & kMask)] = x;
                mono += x;
            }
            mono /= (float) nc;
            const float lp = aa2.process (aa1.process (mono));
            monoLp[(size_t) (nIn & kMask)] = lp;
            if (++decPhase >= decim)
            {
                decPhase = 0;
                detector.pushDecimated (lp);
            }
            ++nIn;

            if (nIn % hop == 0)
                analysisHop();

            // ---- output (synthesis axis position t = nIn - 1 - accLatency; the content it shows is
            //      t - D, D = 0 unless the latency changed while playing, see setRealtime())
            const int64_t t = nIn - 1 - accLatency;
            double dOff = rampTo;
            if (ramping)
            {
                dOff = contentOffset ((double) t);
                if ((double) t >= (double) rampStart + rampLen)
                    ramping = false;                         // settled on rampTo
            }
            lagNow = (double) accLatency + dOff;
            mixSmooth += (params.mix - mixSmooth) * smoothCoef;
            wetSmooth += (wetTarget - wetSmooth) * smoothCoef;
            const float wetGain = mixSmooth * wetSmooth;

            if (t < 0)
            {
                for (int ch = 0; ch < nc; ++ch)
                    channels[ch][i] = 0.0f;
                continue;
            }

            placeGrains (t);

            const size_t ti = (size_t) (t & kMask);
            const float xf = xfade[ti];          // < 0: plain sum, else unvoiced -> voiced crossfade
            xfade[ti] = -1.0f;
            const float vg = levelGain;          // level of the shifted voice (exactly 1 without transpose / formant)
            float dryPow = 0.0f, voicedPow = 0.0f, dryPeak = 0.0f, voicedPeak = 0.0f;
            bool onlyVoiced = xf < 0.0f;
            float dryCh[2] = { 0.0f, 0.0f }, wetCh[2] = { 0.0f, 0.0f };
            for (int ch = 0; ch < nc; ++ch)
            {
                float& v = acc[(size_t) ch][ti];
                float& u = accU[(size_t) ch][ti];
                const float dry = std::abs (dOff) > 0.0 ? dryAt (ch, (double) t - dOff) : in[(size_t) ch][ti];
                dryPow += dry * dry;
                voicedPow += v * v;
                dryPeak = std::max (dryPeak, std::abs (dry));
                voicedPeak = std::max (voicedPeak, std::abs (v));
                onlyVoiced = onlyVoiced && ! (std::abs (u) > 0.0f);            // no unvoiced grain here
                const float vv = v * vg;
                dryCh[ch] = dry;
                wetCh[ch] = xf < 0.0f ? vv + u : u + xf * (vv - u);
                v = u = 0.0f;
            }

            // Output safety limiter on the processed voice (1.3, P0 peak safety). PSOLA can raise peaks
            // above the dry ones (grains overlapping in phase, transpose / formant) and 1.2 had no
            // protection: a hot vocal could leave the plug-in above 0 dBFS. Zero latency (the reported
            // latency does not change): instant attack, 10 ms hold, 80 ms release, stereo-linked,
            // ceiling -1 dBFS (sample peak). It acts on the processed signal before MIX, so bypass and
            // the dry part of a mix stay exactly the input; below the ceiling the gain is exactly 1 and
            // the output is bit-identical to an unguarded one.
            float g = 1.0f;
            if (params.peakGuard)
            {
                const float pk = std::max (std::abs (wetCh[0]), nc > 1 ? std::abs (wetCh[1]) : 0.0f);
                if (pk > kGuardCeiling && std::isfinite (pk))
                {
                    const float need = kGuardCeiling / pk;
                    if (need < guardGain)
                        guardGain = need;
                    guardHoldLeft = guardHold;
                }
                else if (guardGain < 1.0f)
                {
                    if (guardHoldLeft > 0)
                        --guardHoldLeft;
                    else
                    {
                        guardGain += (1.0f - guardGain) * guardRelease;
                        if (guardGain > 0.99999f)
                            guardGain = 1.0f;
                    }
                }
                g = guardGain;
                guardMin = std::min (guardMin, guardGain);
            }
            for (int ch = 0; ch < nc; ++ch)
            {
                float wet = g < 1.0f ? wetCh[ch] * g : wetCh[ch];
               #if VSTM_TEST_HOOKS
                if (testNonFinite > 0)
                {
                    wet = std::numeric_limits<float>::quiet_NaN();
                    if (ch == nc - 1)
                        --testNonFinite;
                }
               #endif
                // 1.5 fail-safe (roadmap item 80): a processed sample that is not a finite number never
                // reaches the host; the dry sample goes out in its place. The input is sanitised and no
                // fuzz / sanitizer run has produced one, so this is a last line of defence: for finite
                // samples nothing changes.
                if (! std::isfinite (wet))
                {
                    wet = dryCh[ch];
                    failSafeSamples.fetch_add (1, std::memory_order_relaxed);
                }
                channels[ch][i] = dryCh[ch] + wetGain * (wet - dryCh[ch]);
            }
            if (levelActive || std::abs (vg - 1.0f) > 0.0f)
                updateLevelGain (onlyVoiced && voicedPow > 0.0f, dryPow, voicedPow, dryPeak, voicedPeak);
        }
    }

    //==========================================================================
    //  read by the UI thread
    SpscQueue<DisplayPoint, 4096> display;
    std::atomic<int>   detectedKey { -1 };          // Auto: locked tonic, -1 = not locked
    std::atomic<int>   keyCandidate { -1 };         // Auto: best tonic of the evidence (also before the lock)
    std::atomic<float> keyConfidence { 0.0f };      // Auto: unlocked = the candidate's, locked = at the lock (KeyDetector)
    std::atomic<int>   keyState { KeyDetector::kListening };   // KeyDetector::State
    std::atomic<int>   keySuggestion { -1 };        // locked: a key the evidence now clearly prefers (shown only)
    std::atomic<float> keySuggestionConfidence { 0.0f };
    std::atomic<int>   currentNote { -1 };
    std::atomic<uint32_t> failSafeSamples { 0 };    // 1.5: processed samples replaced by the dry signal (not finite)

   #if VSTM_TEST_HOOKS
    /** Test builds only (EngineTest): the next n output samples of the processed voice are made NaN. */
    void testInjectNonFinite (int n) noexcept { testNonFinite = n; }
    int testNonFinite = 0;
   #endif
    std::atomic<bool>  currentVoiced { false };
    std::atomic<float> currentInMidi { 0.0f };
    std::atomic<float> currentOutMidi { 0.0f };
    std::atomic<float> currentClarity { 0.0f };    // 1.4 developer panel: detector certainty 0..1
    std::atomic<float> currentDelayMs { 0.0f };    // 1.4 developer panel: analysis delay of the last hop

    //------------------------------------------------------------ 1.5 (roadmap 49 / 53): the notes as MIDI
    /** Input samples taken so far (the axis the note changes are placed on). Audio thread. */
    int64_t getInputSampleCount() const noexcept  { return nIn; }

    /** The note the voice is tuned to (transpose included; -1 = not voiced), for MIDI: a change counts
        once the new note has lasted Params::midiMinNoteS (a gap: midiMinGapS), so the passing notes
        of a slide, one-hop blips and short breaks in the voicing make no MIDI notes. A change is placed
        where the OUTPUT plays it (analysis centre + the output delay): a note-on after silence and a
        note-off where the voice left the old note, a note-to-note change in the middle of what was skipped
        between them (a slide's passing notes, a break in the voicing). When that sample has been played
        already the change goes out at the sample the decision was made (late by up to the minimum length;
        REALTIME has less look-ahead than HQ). Decided per analysis hop, so the positions do not depend on
        the block size. Calls fn (offset, note) for every change before the end of the audio processed so
        far; offset counts from blockStart (the input sample count before the block) and is never
        negative. Audio thread only, no allocation. */
    template <typename Fn>
    void takeNoteEvents (int64_t blockStart, Fn&& fn) noexcept
    {
        while (noteEvCount > 0)
        {
            const NoteEvent& ev = noteEvs[(size_t) noteEvHead];
            if (ev.when >= nIn)
                break;                                    // plays in a later block
            fn ((int) std::max<int64_t> (0, ev.when - blockStart), ev.note);
            noteEvHead = (noteEvHead + 1) % kNoteEvRing;
            --noteEvCount;
        }
    }

    /** Adaptive's view of the performance (tests, display). Audio thread only. */
    const PitchCorrector::AdaptiveState& getAdaptiveState() const noexcept { return corrector.getAdaptiveState(); }

private:
    static constexpr int kNoKeyRequest = -100;
    static constexpr int kKeyReanalyze = -2;
    std::atomic<int> keyRequest { kNoKeyRequest };
    std::atomic<float> keyRequestConf { 0.0f };

    // 1.5 (roadmap 49 / 53): note changes waiting for the output to reach them (see takeNoteEvents)
    struct NoteEvent { int64_t when; int note; };
    static constexpr int kNoteEvRing = 256;
    std::array<NoteEvent, kNoteEvRing> noteEvs {};
    int noteEvHead = 0, noteEvCount = 0;
    int midiCand = -1, midiSent = -1;      // the note of the last hops / the last one queued
    double midiCandStart = 0.0;            // content time (input samples) the candidate started
    double midiLeave = 0.0;                // content time the hops first differed from the queued note
    bool midiAway = false;

    /** contentTime: the hop's analysis centre; noteFrom: where the output starts this hop's note (earlier
        than contentTime when a hard-tune switch was backdated, HQ). */
    void trackMidiNote (double contentTime, double noteFrom, int note) noexcept
    {
        if (note != midiCand)
        {
            midiCand = note;
            midiCandStart = noteFrom;
        }
        if (note == midiSent)
        {
            midiAway = false;                             // back on the note that sounds: nothing changes
            return;
        }
        if (! midiAway)
        {
            midiAway = true;
            midiLeave = noteFrom;
        }
        const double minNote = params.midiMinNoteS >= 0.0f ? (double) params.midiMinNoteS : (realtime ? 0.008 : 0.015);
        if (contentTime - midiCandStart < (midiCand >= 0 ? minNote : (double) params.midiMinGapS) * sr)
            return;
        const double at = midiSent >= 0 && midiCand >= 0 ? 0.5 * (midiLeave + midiCandStart) : midiLeave;
        midiSent = midiCand;
        midiAway = false;
        auto when = std::max ((int64_t) std::llround (at + lagNow), nIn);
        if (noteEvCount > 0)
        {
            NoteEvent& back = noteEvs[(size_t) ((noteEvHead + noteEvCount - 1) % kNoteEvRing)];
            when = std::max (when, back.when);            // time order holds when the delay shrinks (HQ -> REALTIME)
            if (noteEvCount == kNoteEvRing)
            {
                back.note = midiSent;                     // full (not in practice): the newest state wins
                return;
            }
        }
        noteEvs[(size_t) ((noteEvHead + noteEvCount) % kNoteEvRing)] = { when, midiSent };
        ++noteEvCount;
    }

    /** The note set the corrector uses: the scale's, or the custom one (relative to the key). */
    uint16_t allowedMask() const noexcept
    {
        return params.customScale ? (uint16_t) (params.customMask & 0x0fff) : scaleMask (params.scale);
    }

    void applyKeyRequest() noexcept
    {
        if (keyRequest.load (std::memory_order_relaxed) == kNoKeyRequest)
            return;                                      // the normal case: one relaxed load per block
        const int r = keyRequest.exchange (kNoKeyRequest, std::memory_order_acquire);
        if (r == kKeyReanalyze)
            keys.reset();
        else if (r >= 0 && r <= 11)
        {
            keys.lockTo (r, keyRequestConf.load (std::memory_order_relaxed));
            keys.noteScale (params.customScale ? -1 : params.scale, allowedMask());
        }
        else
            return;
        publishKey();
    }

    void publishKey() noexcept
    {
        detectedKey.store (keys.getKey());
        keyCandidate.store (keys.getCandidate());
        keyConfidence.store (keys.getConfidence());
        keyState.store (keys.getState());
        keySuggestion.store (keys.getSuggestion());
        keySuggestionConfidence.store (keys.getSuggestionConfidence());
    }

    static constexpr int kRing = 1 << 16;
    static constexpr int64_t kMask = kRing - 1;

    //------------------------------------------------------------ level of the shifted voice
    /** TD-PSOLA changes the level of the voiced stream with the shift (formant - transpose):
        transposing up an octave keeps only the input's even harmonics (real voices: -2 ... -9 dB,
        depending on how strong the fundamental is), transposing down or the formant up leaves gaps
        between shorter grains (about +1.9 dB at 12 st). While transpose or formant is in use the
        voiced stream is matched to the dry signal's power, measured only where the output is purely
        voiced: consonants, breaths and silences are never boosted (the gain holds there). Limited
        to -6 ... +12 dB; 50 ms power windows, 30 ms gain glide; until 20 ms of voice has been
        measured the typical value for the shift is used. Shifting up, short grain bursts are kept
        within +4 dB of the dry signal (see below). With transpose and formant at 0 the gain glides
        back to exactly 1 and the output is bit-identical to the engine without it. */
    void updateLevelGain (bool measure, float dryPow, float voicedPow, float dryPeak, float voicedPeak) noexcept
    {
        float target = 1.0f;
        if (levelActive)
        {
            if (measure)
            {
                levelPd += (dryPow - levelPd) * levelPowCoef;
                levelPv += (voicedPow - levelPv) * levelPowCoef;
                if (levelCount < levelMinCount)
                    ++levelCount;
                else if (levelPv > 1.0e-10f && levelPd > 1.0e-10f)
                    levelHeld = std::clamp (std::sqrt (levelPd / levelPv), 0.5f, 4.0f);
            }
            target = levelHeld;

            // Shifting up, overlapping grains sometimes line up for 10-30 ms and the voice jumps
            // 10-16 dB above its own level (also without this gain; measured on real arpeggios at
            // +9 st). The matched gain must not lift such a burst above the dry signal: in 10 ms
            // windows the shifted voice stays within +4 dB of the dry one (fast down, slow back).
            if (levelUp)
            {
                levelFd += (dryPow - levelFd) * levelFastCoef;
                levelFv += (voicedPow - levelFv) * levelFastCoef;
                if (levelFv > 1.0e-10f)
                    target = std::min (target, std::sqrt (levelFd / levelFv) * 1.585f);

                // Peak safety, kept apart from the level match: the gain added for the level must not lift
                // the shifted voice's short peaks above the dry signal's own (sliding ~20 ms peak windows,
                // + kPeakMargin). It never cuts below 1: PSOLA's own peaks are not limited (no hidden limiter).
                peakTrack (dryPeak, voicedPeak);
                const float pv = peakWindow (peakV, peakVNow);
                if (pv > 1.0e-6f)
                    target = std::min (target, std::max (1.0f, kPeakMargin * peakWindow (peakD, peakDNow) / pv));
            }
        }
        levelGain += (target - levelGain) * (target < levelGain ? levelDownCoef : levelGainCoef);
        if (! levelActive && std::abs (levelGain - 1.0f) < 1.0e-4f)
            levelGain = 1.0f;
        if (! std::isfinite (levelGain) || ! std::isfinite (levelPd) || ! std::isfinite (levelPv)
            || ! std::isfinite (levelFd) || ! std::isfinite (levelFv))
        {
            levelGain = 1.0f;
            levelPd = levelPv = levelFd = levelFv = 0.0f;
            levelCount = 0;
        }
    }

    /** Sliding peak windows of the dry signal and the shifted voice: the maximum of the running 5 ms block
        and the last kPeakBlocks blocks (~20 - 25 ms, at least one period of a 50 Hz voice). */
    void peakTrack (float d, float v) noexcept
    {
        peakDNow = std::max (peakDNow, d);
        peakVNow = std::max (peakVNow, v);
        if (++peakPos >= peakBlockLen)
        {
            for (int i = kPeakBlocks - 1; i > 0; --i) { peakD[i] = peakD[i - 1]; peakV[i] = peakV[i - 1]; }
            peakD[0] = peakDNow;
            peakV[0] = peakVNow;
            peakDNow = peakVNow = 0.0f;
            peakPos = 0;
        }
    }
    static float peakWindow (const float* blocks, float now) noexcept
    {
        float m = now;
        for (int i = 0; i < kPeakBlocks; ++i) m = std::max (m, blocks[i]);
        return m;
    }

    /** Typical level correction for a shift of x = formant - transpose semitones: the median level
        change of 50 real recordings, sign flipped (VSTM_TUNE_AUDIT.md 16.2). Transpose and formant
        give the same change for the same x. Only the starting point: the measurement takes over. */
    static float levelPrior (float x) noexcept
    {
        static const float lossDb[9] = { -3.1f, -3.5f, -2.7f, -2.2f, 0.0f, -0.4f, 0.4f, 1.2f, 1.9f };   // x = -12, -9 ... +12
        const float pos = (std::clamp (x, -12.0f, 12.0f) + 12.0f) / 3.0f;
        const int i = std::min (7, (int) pos);
        const float db = lossDb[i] + (pos - (float) i) * (lossDb[i + 1] - lossDb[i]);
        return std::pow (10.0f, -db / 20.0f);
    }

    //------------------------------------------------------------ REALTIME <-> HQ
    /** D at a position s of the synthesis axis: the content shown there is s - D. */
    double contentOffset (double pos) const noexcept
    {
        if (! ramping)
            return rampTo;
        const double x = (pos - (double) rampStart) / rampLen;
        if (x <= 0.0) return rampFrom;
        if (x >= 1.0) return rampTo;
        return rampFrom + (rampTo - rampFrom) * (0.5 - 0.5 * std::cos (kPi * x));
    }
    double contentAt (double pos) const noexcept { return pos - contentOffset (pos); }

    /** The dry input at a content position that is not a whole sample (only while a latency changes,
        or after it changed by a whole number of samples: then it is the plain sample). */
    float dryAt (int ch, double pos) const noexcept
    {
        const double fl = std::floor (pos);
        const auto i0 = (int64_t) fl;
        const float fr = (float) (pos - fl);
        const auto& b = in[(size_t) ch];
        if (! (fr > 0.0f))
            return b[(size_t) (i0 & kMask)];
        return hermite (b[(size_t) ((i0 - 1) & kMask)], b[(size_t) (i0 & kMask)],
                        b[(size_t) ((i0 + 1) & kMask)], b[(size_t) ((i0 + 2) & kMask)], fr);
    }

    /** NaN / Inf from a plugin before this one would stay in the analysis filters for good: they
        become silence. Absurd finite values are limited to +80 dBFS so no sum can overflow.
        Normal audio passes unchanged (one comparison). */
    static float sanitize (float x) noexcept
    {
        constexpr float kMaxInput = 1.0e4f;
        if (std::abs (x) <= kMaxInput)
            return x;
        return std::isfinite (x) ? std::copysign (kMaxInput, x) : 0.0f;
    }
    static constexpr int kMarkRing = 1024;
    static constexpr int kRecRing = 256;

    struct Mark { double pos = 0.0; float period = 0.0f; bool voiced = false; };
    struct Rec  { double time = 0.0; float inMidi = 0.0f, outMidi = 0.0f, pass = 1.0f; bool voiced = false; };
    struct Plan { bool voiced = false; double s = 0.0, src = 0.0, hIn = 1.0, hOut = 1.0, spacing = 1.0, shiftSt = 0.0, period = 0.0; float gain = 1.0f; };

    void applyRange()
    {
        switch (params.range)
        {
            case 0:  detector.setRange (110.0, 1400.0); break;   // soprano / alto
            case 2:  detector.setRange (50.0, 620.0);   break;   // bass / baritone
            default: detector.setRange (72.0, 1050.0);  break;   // tenor and most voices
        }
    }

    //------------------------------------------------------------------ analysis
    void analysisHop() noexcept
    {
        const auto r = tracker.process (detector.analyse (monoLp.data(), (int) kMask, nIn));
        const double dt = hop / sr;

        float inMidi = lastInMidi;
        if (r.voiced && r.period > 1.0)
            inMidi = (float) hzToMidi (sr / r.period);
        lastInMidi = inMidi;

        // 1.5 (roadmap 45): A4 reference. The corrector, Auto Key and the display work on the pitch relative
        // to the moved note grid; the grains keep the real pitch. At 440 Hz this path is skipped entirely.
        const bool refShift = std::abs (params.tuningRefHz - 440.0f) > 1.0e-3f;
        const float refSt = refShift ? (float) (12.0 * std::log2 ((double) params.tuningRefHz / 440.0)) : 0.0f;
        const float gridMidi = refShift ? inMidi - refSt : inMidi;

        // allowed notes: the scale's, or the custom set (relative to the key, like a scale)
        // 1.5: bypassed degrees (custom scale only) are targets too, so the note decision can land on them
        const uint16_t bypass = params.customScale ? params.bypassMask : (uint16_t) 0;
        const uint16_t mask = (uint16_t) (allowedMask() | bypass);
        keys.addFrame (r.voiced, gridMidi, r.clarity, dt);
        const int autoKey = keys.update (params.customScale ? -1 : params.scale, mask, dt, params.followKeyChanges);
        publishKey();

        CorrectorSettings cs;
        cs.amount = params.amount;
        cs.retuneMs = params.retuneMs;
        cs.humanize = params.humanize;
        cs.robotism = params.robotism;
        cs.vibratoScale = params.vibratoScale;
        cs.transpose = params.transpose;
        cs.adaptive = params.adaptive;
        cs.swingKeep = params.swingKeep;
        cs.vibratoBand = params.vibratoBand;
        cs.transitionMs = params.transitionMs;
        cs.toleranceCt = params.toleranceCt;
        cs.confidenceMin = params.confidenceMin;
        cs.synthVibRateHz = params.synthVibRateHz;
        cs.synthVibDepthCt = params.synthVibDepthCt;
        cs.scaleSteps = params.scaleSteps;
        cs.attackMs = params.attackMs;
        cs.naturalCentre = params.naturalCentre;
        cs.naturalMotion = params.naturalMotion;
        cs.naturalTransition = params.naturalTransition;
        cs.octaveGuard = params.octaveGuard;
        cs.naturalCentreRef = params.naturalCentreRef;
        cs.releaseMs = params.releaseMs;
        float levelDb = 0.0f;                    // 1.5 release: level of the last hop (only computed when it is used)
        if (params.releaseMs > 0.0f)
        {
            double sum = 0.0;
            for (int64_t k = nIn - hop; k < nIn; ++k)
            {
                const double v = monoLp[(size_t) (k & kMask)];
                sum += v * v;
            }
            levelDb = (float) (10.0 * std::log10 (sum / hop + 1.0e-12));
        }
        if (mask == 0x0fff || mask == 0)         { cs.key = 0; cs.mask = 0x0fff; }    // every note allowed
        else if (params.key >= 0)                { cs.key = params.key; cs.mask = mask; }
        else if (autoKey >= 0)                   { cs.key = autoKey;    cs.mask = mask; }
        else                                     { cs.key = 0; cs.mask = 0x0fff; }    // still listening: nearest semitone
        // 1.5 (roadmap 45): tuning system / microtonal degrees, counted from the key's tonic (chosen, else detected,
        // else C); every target note sits that far off the equal grid. All zero (equal temperament): not used.
        for (int d = 0; d < 12 && ! cs.tuned; ++d)
            cs.tuned = std::abs (params.degreeCents[d]) > 1.0e-4f;
        if (cs.tuned)
        {
            const int tonic = params.key >= 0 ? params.key : (autoKey >= 0 ? autoKey : 0);
            for (int pc = 0; pc < 12; ++pc)
                cs.pcOffset[pc] = 0.01f * params.degreeCents[(pc - tonic + 12) % 12];
        }
        if (params.midiNote >= 0)
        {
            // 1.5 MIDI perform (roadmap 49): the last held MIDI note is the target, octave as played
            cs.key = 0;
            cs.mask = (uint16_t) (1u << (params.midiNote % 12));
            cs.forcedNote = params.midiNote;
        }
        else if (params.midiMask != 0)
        {
            // 1.5 MIDI target notes (roadmap 42 / 46 / 52 / 33): the held MIDI notes are the targets, in any octave,
            // like a scale played live; the scale, the custom notes and note bypass wait until they are released
            cs.key = 0;
            cs.mask = params.midiMask;
        }
        else if (bypass != 0)
        {
            // the bypass degrees count from the same key as the custom notes (chosen, else detected, else C)
            if (cs.mask == 0x0fff)
                cs.key = params.key >= 0 ? params.key : (autoKey >= 0 ? autoKey : 0);
            cs.bypassMask = bypass;
        }

        const auto o = corrector.process (r.voiced, gridMidi, (float) dt, cs, r.clarity, levelDb);
        const float outAbs = refShift ? o.outMidi + refSt : o.outMidi;      // the real output pitch

        // the history must stay in time order: after REALTIME -> HQ the analysis is centred further
        // back than the last REALTIME records, so those (now "in the future") are dropped
        const double recTime = (double) nIn - r.centreDelay;
        while (recCount > 0 && recs[(size_t) ((recWrite - 1 + kRecRing) % kRecRing)].time >= recTime)
        {
            recWrite = (recWrite - 1 + kRecRing) % kRecRing;
            --recCount;
        }
        Rec& rec = recs[(size_t) recWrite];
        rec.time = recTime;
        rec.inMidi = inMidi;
        rec.outMidi = outAbs;
        rec.pass = o.passThrough;
        rec.voiced = r.voiced;
        recWrite = (recWrite + 1) % kRecRing;
        recCount = std::min (recCount + 1, kRecRing);

        // 1.3 (P0, note timing): a hard-tune switch takes effect where the voice crossed the midpoint,
        // not where the decision became certain, for the hops whose grains are not placed yet. HQ has
        // ~5 - 15 ms of such hops (latency 40 ms minus the analysis delay and one grain); in REALTIME the
        // grains have always been placed already, so nothing changes there. The decision itself (and so
        // how often the target switches) is not touched.
        double noteFrom = recTime;               // where the output starts the note of this hop (MIDI, 1.5)
        if (params.backdateSwitch && o.noteChanged && o.instant && o.backdateHops > 0 && o.fromNote >= 0 && r.voiced)
            noteFrom = std::min (noteFrom, backdateSwitch (o.backdateHops, o.amountNow * o.switchShift));

        DisplayPoint dp;
        dp.inMidi = gridMidi;
        dp.outMidi = o.outMidi;
        dp.note = (int16_t) (r.voiced && o.note >= 0 ? (o.stepNote >= 0 ? o.stepNote : o.note) + (int) std::lround (params.transpose) : -1);
        dp.deviation = o.note >= 0 ? gridMidi - (float) o.note : 0.0f;
        dp.flags = (uint8_t) ((r.voiced ? kVoiced : 0) | (o.noteChanged ? kNoteChange : 0) | (o.settled ? kSettled : 0));
        dp.analysisDelay = (float) (r.centreDelay / sr);
        dp.clarity = r.clarity;
        display.push (dp);

        currentVoiced.store (r.voiced);
        currentInMidi.store (inMidi);
        currentOutMidi.store (outAbs);
        currentClarity.store (r.clarity, std::memory_order_relaxed);
        currentDelayMs.store ((float) (1000.0 * r.centreDelay / sr), std::memory_order_relaxed);
        currentNote.store (dp.note);
        trackMidiNote (recTime, noteFrom, dp.note);

        curVoiced = r.voiced;
        curPeriod = r.voiced ? r.period : 0.0;
        generateMarks();
        planValid = false;
    }

    //------------------------------------------------------------- pitch marks
    void pushMark (double pos, float period, bool voiced) noexcept
    {
        if (markCount == kMarkRing)
        {
            markHead = (markHead + 1) % kMarkRing;
            --markCount;
        }
        marks[(size_t) ((markHead + markCount) % kMarkRing)] = { pos, period, voiced };
        ++markCount;
        lastMarkPos = pos;
        lastMarkVoiced = voiced;
    }

    const Mark& markAt (int i) const noexcept { return marks[(size_t) ((markHead + i) % kMarkRing)]; }

    void generateMarks() noexcept
    {
        // never let mark generation fall far behind (e.g. after silence at start-up)
        if (lastMarkPos < (double) nIn - kRing / 4)
            lastMarkPos = (double) nIn - kRing / 8;

        for (int guard = 0; guard < 512; ++guard)
        {
            if (curVoiced && curPeriod > 8.0)
            {
                const double T = curPeriod;
                if (lastMarkVoiced)
                {
                    const double predicted = lastMarkPos + T;
                    const int half = (int) (T * 0.5);
                    const int radius = std::max (2, (int) (T / 6.0));
                    if (predicted + radius + half + 3 > (double) nIn)
                        break;
                    pushMark (refineMark (predicted, T, half, radius), (float) T, true);
                    // keep the stored period equal to the real spacing (exact local pitch)
                    if (markCount >= 2)
                    {
                        Mark& m = marks[(size_t) ((markHead + markCount - 1) % kMarkRing)];
                        const double spacing = m.pos - markAt (markCount - 2).pos;
                        if (spacing > 0.7 * T && spacing < 1.4 * T)
                            m.period = (float) spacing;
                    }
                }
                else
                {
                    // first voiced mark: the strongest excitation within one period
                    const int64_t start = (int64_t) std::floor (lastMarkPos) + 1;
                    const int64_t end = start + (int64_t) T;
                    if ((double) end + 2 > (double) nIn)
                        break;
                    int64_t bestPos = start;
                    float bestVal = -1.0f;
                    for (int64_t k = start; k < end; ++k)
                    {
                        const float v = std::abs (monoLp[(size_t) (k & kMask)]);
                        if (v > bestVal) { bestVal = v; bestPos = k; }
                    }
                    pushMark ((double) bestPos, (float) T, true);
                }
            }
            else
            {
                const double next = lastMarkPos + unvoicedHop;
                if (next > (double) nIn - 1)
                    break;
                pushMark (next, (float) unvoicedHop, false);
            }
        }
    }

    /** Waveform-similarity alignment: the segment around the previous mark is compared with
        candidates around the predicted position; best normalised correlation wins (sub-sample). */
    double refineMark (double predicted, double T, int half, int radius) const noexcept
    {
        const auto p0 = (int64_t) std::llround (lastMarkPos);
        const double carry = lastMarkPos - (double) p0;
        const auto c0 = (int64_t) std::llround (predicted - carry);
        const int step = half > 200 ? 2 : 1;

        double ea = 0.0;
        for (int j = -half; j < half; j += step)
        {
            const float a = monoLp[(size_t) ((p0 + j) & kMask)];
            ea += (double) a * a;
        }

        double scores[2 * 160 + 1];
        const int r = std::min (radius, 160);
        int best = 0;
        double bestScore = -2.0;
        for (int d = -r; d <= r; ++d)
        {
            double num = 0.0, eb = 0.0;
            for (int j = -half; j < half; j += step)
            {
                const float a = monoLp[(size_t) ((p0 + j) & kMask)];
                const float b = monoLp[(size_t) ((c0 + d + j) & kMask)];
                num += (double) a * b;
                eb += (double) b * b;
            }
            const double sc = num / std::sqrt (ea * eb + 1.0e-20);
            scores[d + r] = sc;
            if (sc > bestScore) { bestScore = sc; best = d; }
        }

        if (bestScore < 0.3)
            return predicted;

        double frac = 0.0;
        if (best > -r && best < r)
        {
            const double a = scores[best + r - 1], b = scores[best + r], c = scores[best + r + 1];
            const double den = a - 2.0 * b + c;
            if (den < -1.0e-12)
                frac = std::clamp (0.5 * (a - c) / den, -0.5, 0.5);
        }

        const double pos = (double) (c0 + best) + frac + carry;
        const double spacing = pos - lastMarkPos;
        if (spacing < 0.7 * T || spacing > 1.4 * T)
            return predicted;
        return pos;
    }

    /** Gives the `hops` records before the newest one the new target too (see analysisHop), as far as
        no grain has been placed from them: rec.outMidi moves by `shift` semitones, the new note minus the
        old one times the correction amount (exact while the correction is instantaneous). */
    /** Returns the content time of the oldest record moved (the output plays the new note from there),
        or +infinity when none was. */
    double backdateSwitch (int hops, float shift) noexcept
    {
        double from = std::numeric_limits<double>::infinity();
        for (int b = 1; b <= hops && b < recCount; ++b)
        {
            Rec& r = recs[(size_t) ((recWrite - 1 - b + kRecRing) % kRecRing)];
            if (r.time <= lastGrainSrc)
                break;                               // a placed grain already used this record
            if (r.voiced)
            {
                r.outMidi += shift;
                from = r.time;
            }
        }
        return from;
    }

    //---------------------------------------------------------------- history
    Rec historyAt (double time) const noexcept
    {
        if (recCount == 0)
            return {};

        const auto at = [this] (int back) -> const Rec& { return recs[(size_t) ((recWrite - 1 - back + kRecRing) % kRecRing)]; };

        const Rec& newest = at (0);
        if (time >= newest.time || recCount == 1)
            return newest;

        for (int b = 1; b < recCount; ++b)
        {
            const Rec& older = at (b);
            if (older.time <= time)
            {
                const Rec& newer = at (b - 1);
                if (! older.voiced) return newer;
                if (! newer.voiced) return older;
                const double span = newer.time - older.time;
                const float f = span > 0.0 ? (float) ((time - older.time) / span) : 1.0f;
                Rec r = older;
                r.inMidi  = older.inMidi  + f * (newer.inMidi  - older.inMidi);
                r.outMidi = older.outMidi + f * (newer.outMidi - older.outMidi);
                r.pass    = older.pass    + f * (newer.pass    - older.pass);
                return r;
            }
        }
        return at (recCount - 1);
    }

    //---------------------------------------------------------------- synthesis
    // never longer than the delay that is really there (while a latency glides: the smaller one)
    double maxHalf() const noexcept { return std::max (unvoicedHop, 0.55 * std::min ((double) latency, lagNow)); }

    bool makePlan (Plan& out) noexcept
    {
        const double phi = std::pow (2.0, params.formant / 12.0);
        const double hCap = maxHalf();

        // nearest mark to sNext (its content position) among the marks whose grain data is already available
        const double dNext = contentOffset (sNext);
        const double cNext = sNext - dNext;
        int chosen = -1;
        double bestDist = 1.0e30;
        for (int i = 0; i < markCount; ++i)
        {
            const Mark& m = markAt (i);
            const double hIn = m.voiced ? std::min ((double) m.period, hCap) : unvoicedHop;
            if (m.pos + hIn + 3.0 > (double) nIn)
                break;                                   // marks are sorted by position
            const double d = std::abs (m.pos - cNext);
            if (d < bestDist) { bestDist = d; chosen = i; }
            else if (m.pos > cNext) break;
        }

        // unvoiced -> voiced: the first voiced grain must lie after the last unvoiced grain, so
        // it can sit exactly on its source mark (in phase with the crossfaded unvoiced stream)
        if (chosen >= 0 && markAt (chosen).voiced && ! lastGrainVoiced)
        {
            while (chosen < markCount && markAt (chosen).pos + dNext <= lastS + 0.5)
                ++chosen;
            if (chosen >= markCount || ! markAt (chosen).voiced
                || markAt (chosen).pos + std::min ((double) markAt (chosen).period, hCap) + 3.0 > (double) nIn)
                chosen = -1;                     // not yet: one more unvoiced grain
        }

        if (chosen >= 0 && markAt (chosen).voiced)
        {
            const Mark& m = markAt (chosen);
            const double hIn = std::min ((double) m.period, hCap);

            // the period this grain stands for = distance to the next mark when it is known;
            // with no correction the grains then land exactly on the analysis marks (transparent)
            double period = m.period;
            if (chosen + 1 < markCount)
            {
                const Mark& next = markAt (chosen + 1);
                const double d = next.pos - m.pos;
                if (next.voiced && d > 0.7 * m.period && d < 1.4 * m.period)
                    period = d;
            }
            const double localMidi = hzToMidi (sr / period);
            const Rec h = historyAt (m.pos);
            double outMidi = localMidi + params.transpose;
            if (recCount > 0)
                outMidi = h.outMidi + h.pass * (localMidi - h.inMidi);

            double spacing = sr / midiToHz (outMidi);
            spacing = std::clamp (spacing, 0.40 * period, 2.5 * period);

            out.voiced = true;
            out.s = lastGrainVoiced ? sNext : std::max (m.pos + dNext, lastS + 1.0);
            out.src = m.pos;
            out.hIn = hIn;
            out.hOut = hIn / phi;
            out.spacing = spacing;
            out.shiftSt = std::abs (12.0 * std::log2 (period / spacing));
            out.period = period;
            out.gain = (float) std::clamp (spacing / out.hOut, 0.25, 4.0);
            return true;
        }

        // unvoiced (or nothing analysed yet): time-aligned grains, 50 % overlap. Right after a
        // voiced stretch the source offset of the last voiced grain is inherited and halved
        // with every grain, so the two grain types do not overlap out of phase.
        const double hIn = unvoicedHop;
        const double offset = lastGrainVoiced ? lastSrcOffset : 0.5 * lastSrcOffset;
        out.voiced = false;
        out.period = 0.0;
        out.shiftSt = 0.0;
        out.s = sNext;
        out.src = std::min (cNext + (std::abs (offset) < 0.5 ? 0.0 : offset), (double) nIn - hIn - 3.0);
        out.hIn = hIn;
        out.hOut = params.protectConsonants ? hIn : hIn / phi;      // 1.5: consonants keep their spectrum if asked
        out.spacing = out.hOut;
        out.gain = 1.0f;
        return true;
    }

    /** unvoiced -> voiced: keep the time-aligned unvoiced grid running past the first voiced
        grain and crossfade (raised cosine, one unvoiced hop long) from the complete unvoiced
        stream to the complete voiced stream. Called again when the plan is remade: the tail
        grid keeps its phase, only its length and the crossfade weights follow the new plan. */
    void armCrossfade (const Plan& p, int64_t t) noexcept
    {
        const double hU = unvoicedHop / std::pow (2.0, params.formant / 12.0);
        if (! xfadeArmed)
        {
            xfadeArmed = true;
            tailNext = lastS + hU;                     // next centre on the unvoiced grid
            tailOffset = 0.5 * lastSrcOffset;          // continue the grid's own offset
        }
        const double start = std::max (p.s, (double) t);
        const double end = start + hU;
        tailLast = lastS + hU * std::ceil ((end - lastS) / hU - 1.0e-9);
        tailActive = tailNext <= tailLast + 0.5;

        for (int64_t k = t; k <= xfadeEnd; ++k)        // drop weights of an earlier plan
            xfade[(size_t) (k & kMask)] = -1.0f;
        const auto k0 = std::max ((int64_t) std::ceil (p.s - p.hOut), t);
        const auto k1 = (int64_t) std::floor (tailLast + hU);
        for (int64_t k = k0; k <= k1; ++k)
        {
            const double x = (double) k;
            float w = 1.0f;
            if (x < start)       w = 0.0f;
            else if (x < end)    w = (float) (0.5 - 0.5 * std::cos (kPi * (x - start) / hU));
            xfade[(size_t) (k & kMask)] = w;
        }
        xfadeEnd = k1;
    }

    /** the remade plan is unvoiced after all: the main stream continues the tail grid */
    void resumeTailGrid (Plan& p, int64_t t) noexcept
    {
        p.s = tailNext;
        p.src = std::min (contentAt (tailNext) + (std::abs (tailOffset) < 0.5 ? 0.0 : tailOffset), (double) nIn - unvoicedHop - 3.0);
        cancelCrossfade (t);
    }

    void cancelCrossfade (int64_t t) noexcept
    {
        tailActive = false;
        xfadeArmed = false;
        for (int64_t k = t; k <= xfadeEnd; ++k)
            xfade[(size_t) (k & kMask)] = -1.0f;
        xfadeEnd = -1;
    }

    /** voiced -> unvoiced while the unvoiced -> voiced handover is still running: the main
        stream continues the running unvoiced grid (same phase and offset, no doubled grains)
        and the output fades back from the voiced to the complete unvoiced stream. */
    void resumeUnvoiced (Plan& p, int64_t t) noexcept
    {
        const double hU = p.hOut;
        const double next = tailActive ? tailNext : tailLast + hU;
        p.s = next;
        p.src = std::min (contentAt (next) + (std::abs (tailOffset) < 0.5 ? 0.0 : tailOffset), (double) nIn - unvoicedHop - 3.0);
        tailActive = false;
        xfadeArmed = false;

        const float now = xfade[(size_t) (t & kMask)];
        const float x0 = now < 0.0f ? 1.0f : now;
        const auto fadeEnd = (int64_t) std::floor ((double) t + hU);
        const auto k1 = std::max ({ fadeEnd, (int64_t) std::ceil (lastS + lastHOut), xfadeEnd });
        for (int64_t k = t; k <= k1; ++k)
        {
            float w = 0.0f;
            if (k < fadeEnd)
                w = x0 * (float) (0.5 + 0.5 * std::cos (kPi * (double) (k - t) / hU));
            xfade[(size_t) (k & kMask)] = w;
        }
        xfadeEnd = k1;
    }

    /** extra time-aligned unvoiced grains for a running crossfade */
    void placeTailGrains (int64_t t) noexcept
    {
        const double phi = params.protectConsonants ? 1.0 : std::pow (2.0, params.formant / 12.0);
        const double hU = unvoicedHop / phi;
        while (tailActive && tailNext - hU <= (double) t)
        {
            Plan g;
            g.voiced = false;
            g.s = tailNext;
            g.src = std::min (contentAt (tailNext) + (std::abs (tailOffset) < 0.5 ? 0.0 : tailOffset),
                              (double) nIn - unvoicedHop - 3.0);
            tailOffset *= 0.5;
            g.hIn = unvoicedHop;
            g.hOut = hU;
            g.spacing = hU;
            g.gain = 1.0f;
            addGrain (g, t);
            tailNext += hU;
            if (tailNext > tailLast + 0.5)
                tailActive = false;
        }
    }

    void placeGrains (int64_t t) noexcept
    {
        placeTailGrains (t);
        if (! (sNext >= (double) t - 4.0 * maxHalf()))  // fell behind or not a number (should not happen): resync
        {
            sNext = (double) t;
            planValid = false;
        }

        for (int guard = 0; guard < 64; ++guard)
        {
            if (! planValid)
            {
                makePlan (plan);
                planValid = true;
                // decided right after the previous grain, i.e. before any of the handover
                // region has been output: schedule the crossfade now. The plan is remade on
                // every analysis hop, so an armed handover is updated, never restarted.
                if (! lastGrainVoiced && lastS > -1.0e8)
                {
                    if (plan.voiced)
                        armCrossfade (plan, t);
                    else if (xfadeArmed)
                        resumeTailGrid (plan, t);    // voicing flipped back before the switch
                }
            }
            if (plan.s - plan.hOut > (double) t)
                return;

            if (plan.voiced && ! lastGrainVoiced)
                xfadeArmed = false;              // handover grain placed; the tail runs on
            if (! plan.voiced && lastGrainVoiced)
            {
                if (xfadeEnd >= t)
                    resumeUnvoiced (plan, t);    // a voiced blip shorter than the handover
                else
                    cancelCrossfade (t);
            }
            addGrain (plan, t);
            lastHOut = plan.hOut;
            lastS = plan.s;
            lastGrainSrc = std::max (lastGrainSrc, plan.src);
            // phase continuity only helps while the voiced grains are close to the source pitch
            // (correction); after a large shift the inherited offset is just a time jump
            lastSrcOffset = plan.src - contentAt (plan.s);
            if (plan.voiced)
            {
                // a whole number of periods does not change the phase of periodic content:
                // keep the smallest equivalent offset (|offset| <= T/2), so the decay over the
                // following unvoiced grains makes small time steps
                if (plan.period > 1.0)
                    lastSrcOffset -= plan.period * std::round (lastSrcOffset / plan.period);
                lastSrcOffset *= std::clamp (1.5 - plan.shiftSt, 0.0, 1.0);
            }
            sNext = plan.s + plan.spacing;
            lastGrainVoiced = plan.voiced;
            planValid = false;
            pruneMarks (t);
        }
    }

    void addGrain (const Plan& p, int64_t t) noexcept
    {
        const auto k0 = std::max ((int64_t) std::ceil (p.s - p.hOut), t);
        const auto k1 = (int64_t) std::floor (p.s + p.hOut);
        const double phi = p.hIn / p.hOut;
        const double piOverH = kPi / p.hIn;
        const int nc = numCh;

        for (int64_t k = k0; k <= k1; ++k)
        {
            const double u = ((double) k - p.s) * phi;
            const float w = (float) (0.5 * (1.0 + std::cos (u * piOverH))) * p.gain;
            const double src = p.src + u;
            const auto i0 = (int64_t) std::floor (src);
            const float fr = (float) (src - (double) i0);
            const size_t ko = (size_t) (k & kMask);
            for (int ch = 0; ch < nc; ++ch)
            {
                const auto& b = in[(size_t) ch];
                auto& dst = p.voiced ? acc[(size_t) ch] : accU[(size_t) ch];
                const float x = hermite (b[(size_t) ((i0 - 1) & kMask)], b[(size_t) (i0 & kMask)],
                                         b[(size_t) ((i0 + 1) & kMask)], b[(size_t) ((i0 + 2) & kMask)], fr);
                dst[ko] += w * x;
            }
        }
    }

    void pruneMarks (int64_t t) noexcept
    {
        const double keepFrom = contentAt ((double) t) - 4.0 * maxHalf() - 4096.0;
        while (markCount > 2 && markAt (0).pos < keepFrom)
        {
            markHead = (markHead + 1) % kMarkRing;
            --markCount;
        }
    }

    //------------------------------------------------------------------ state
    double sr = 44100.0;
    int decim = 4, hop = 128, latency = 882;
    bool realtime = true;
    double unvoicedHop = 176.0;
    Params params;

    std::vector<float> in[2], acc[2], accU[2], xfade, monoLp;
    Biquad aa1, aa2;
    int decPhase = 0;
    int numCh = 2;
    int64_t nIn = 0;

    PitchDetector detector;
    PitchTracker tracker;
    KeyDetector keys;
    PitchCorrector corrector;
    bool curVoiced = false;
    double curPeriod = 0.0;
    float lastInMidi = 60.0f;

    std::vector<Mark> marks;
    int markHead = 0, markCount = 0;
    double lastMarkPos = 0.0;
    bool lastMarkVoiced = false;

    Rec recs[kRecRing];
    int recWrite = 0, recCount = 0;

    Plan plan;
    bool planValid = false;
    double sNext = 0.0, lastS = -1.0e9, lastHOut = 0.0, lastSrcOffset = 0.0;
    double lastGrainSrc = -1.0e9;       // content position of the newest placed grain (records before it are used up)
    bool lastGrainVoiced = false;
    bool tailActive = false, xfadeArmed = false;
    double tailNext = 0.0, tailLast = 0.0, tailOffset = 0.0;
    int64_t xfadeEnd = -1;

    float mixSmooth = 1.0f, wetSmooth = 1.0f;

    // level of the shifted voice (updateLevelGain)
    bool levelActive = false, levelUp = false;
    float levelGain = 1.0f, levelHeld = 1.0f, levelPd = 0.0f, levelPv = 0.0f, levelFd = 0.0f, levelFv = 0.0f;
    float guardGain = 1.0f;             // output safety limiter (see process)
    int guardHoldLeft = 0;
public:
    float guardMin = 1.0f;              // lowest guard gain since this was last set to 1 (tests, logs)
private:
    static constexpr float kGuardCeiling = 0.89125094f;          // -1 dBFS
    static constexpr float kGuardReleaseSeconds = 0.080f;
    static constexpr double kGuardHoldSeconds = 0.010;
    float levelPowCoef = 0.0f, levelGainCoef = 0.0f, levelFastCoef = 0.0f, levelDownCoef = 0.0f;
    int levelCount = 0, levelMinCount = 960;
    static constexpr int kPeakBlocks = 4;
    static constexpr float kPeakMargin = 1.26f;                  // shifted peaks may exceed the dry's by this much (x)
    float peakD[kPeakBlocks] {}, peakV[kPeakBlocks] {}, peakDNow = 0.0f, peakVNow = 0.0f;
    int peakPos = 0, peakBlockLen = 240;

    // REALTIME <-> HQ: synthesis axis offset and the content offset ramp (setRealtime)
    static constexpr double kRampWetSeconds = 0.4;    // only the processed voice is heard
    static constexpr double kRampDrySeconds = 2.0;    // the dry is heard (MIX < 100 %% or bypass)
    int accLatency = 882;
    double rampFrom = 0.0, rampTo = 0.0, rampLen = 1.0, lagNow = 882.0;
    int64_t rampStart = 0;
    bool ramping = false;
};
} // namespace vstm
