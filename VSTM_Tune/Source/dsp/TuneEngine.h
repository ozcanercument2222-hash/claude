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
//  Gecikme sabittir (REALTIME 20 ms, kapaliyken 40 ms) ve host'a bildirilir.
//  Motor blok boyutundan bagimsizdir: ayni giris her blok boyutunda ayni cikisi verir.
//==============================================================================
#include "DspCommon.h"
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
    };

    struct DisplayPoint
    {
        float   inMidi = 0.0f;          // detected input pitch (meaningful when voiced)
        float   outMidi = 0.0f;         // corrected output pitch
        float   deviation = 0.0f;       // input minus its target note (semitones, before transpose)
        int16_t note = -1;              // target note incl. transpose, -1 = none
        uint8_t flags = 0;
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
        const double fc = 0.30 * sr / decim;
        aa1.setLowpass (sr, fc, 0.5412);
        aa2.setLowpass (sr, fc, 1.3066);

        applyRange();
        reset();
    }

    void setRealtime (bool shouldBeRealtime)
    {
        if (shouldBeRealtime == realtime)
            return;
        realtime = shouldBeRealtime;
        latency = latencyFor (sr, realtime);
        reset();
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
        keys.reset();
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
        detectedKey.store (-1);
        currentNote.store (-1);
        currentVoiced.store (false);
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
        p.mix          = keep (p.mix,          params.mix,            0.0f,    1.0f);
        if (p.key < -1 || p.key > 11) p.key = -1;
        p.scale = std::clamp (p.scale, 0, (int) kNumScales - 1);
        p.range = std::clamp (p.range, 0, 2);

        const bool rangeChanged = p.range != params.range;
        params = p;
        if (rangeChanged)
            applyRange();
    }

    /** In-place processing of up to two channels. */
    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        const int nc = std::min (2, std::max (1, numChannels));
        numCh = nc;
        const float smoothCoef = 1.0f - std::exp (-1.0f / (0.02f * (float) sr));
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

            // ---- output (timeline position t = nIn - 1 - latency)
            const int64_t t = nIn - 1 - latency;
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
            for (int ch = 0; ch < nc; ++ch)
            {
                float& v = acc[(size_t) ch][ti];
                float& u = accU[(size_t) ch][ti];
                const float wet = xf < 0.0f ? v + u : u + xf * (v - u);
                v = u = 0.0f;
                const float dry = in[(size_t) ch][ti];
                channels[ch][i] = dry + wetGain * (wet - dry);
            }
        }
    }

    //==========================================================================
    //  read by the UI thread
    SpscQueue<DisplayPoint, 4096> display;
    std::atomic<int>   detectedKey { -1 };
    std::atomic<int>   currentNote { -1 };
    std::atomic<bool>  currentVoiced { false };
    std::atomic<float> currentInMidi { 0.0f };
    std::atomic<float> currentOutMidi { 0.0f };

private:
    static constexpr int kRing = 1 << 16;
    static constexpr int64_t kMask = kRing - 1;

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

        keys.addFrame (r.voiced, inMidi, r.clarity, dt);
        const int autoKey = keys.update (params.scale, dt);
        detectedKey.store (autoKey);

        CorrectorSettings cs;
        cs.amount = params.amount;
        cs.retuneMs = params.retuneMs;
        cs.humanize = params.humanize;
        cs.robotism = params.robotism;
        cs.vibratoScale = params.vibratoScale;
        cs.transpose = params.transpose;
        if (params.scale == kChromatic)          { cs.key = 0; cs.mask = 0x0fff; }
        else if (params.key >= 0)                { cs.key = params.key; cs.mask = scaleMask (params.scale); }
        else if (autoKey >= 0)                   { cs.key = autoKey;    cs.mask = scaleMask (params.scale); }
        else                                     { cs.key = 0; cs.mask = 0x0fff; }    // still listening: nearest semitone

        const auto o = corrector.process (r.voiced, inMidi, (float) dt, cs);

        Rec& rec = recs[(size_t) recWrite];
        rec.time = (double) nIn - r.centreDelay;
        rec.inMidi = inMidi;
        rec.outMidi = o.outMidi;
        rec.pass = o.passThrough;
        rec.voiced = r.voiced;
        recWrite = (recWrite + 1) % kRecRing;
        recCount = std::min (recCount + 1, kRecRing);

        DisplayPoint dp;
        dp.inMidi = inMidi;
        dp.outMidi = o.outMidi;
        dp.note = (int16_t) (r.voiced && o.note >= 0 ? o.note + (int) std::lround (params.transpose) : -1);
        dp.deviation = o.note >= 0 ? inMidi - (float) o.note : 0.0f;
        dp.flags = (uint8_t) ((r.voiced ? kVoiced : 0) | (o.noteChanged ? kNoteChange : 0) | (o.settled ? kSettled : 0));
        display.push (dp);

        currentVoiced.store (r.voiced);
        currentInMidi.store (inMidi);
        currentOutMidi.store (o.outMidi);
        currentNote.store (dp.note);

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
    double maxHalf() const noexcept { return std::max (unvoicedHop, 0.55 * latency); }

    bool makePlan (Plan& out) noexcept
    {
        const double phi = std::pow (2.0, params.formant / 12.0);
        const double hCap = maxHalf();

        // nearest mark to sNext among the marks whose grain data is already available
        int chosen = -1;
        double bestDist = 1.0e30;
        for (int i = 0; i < markCount; ++i)
        {
            const Mark& m = markAt (i);
            const double hIn = m.voiced ? std::min ((double) m.period, hCap) : unvoicedHop;
            if (m.pos + hIn + 3.0 > (double) nIn)
                break;                                   // marks are sorted by position
            const double d = std::abs (m.pos - sNext);
            if (d < bestDist) { bestDist = d; chosen = i; }
            else if (m.pos > sNext) break;
        }

        // unvoiced -> voiced: the first voiced grain must lie after the last unvoiced grain, so
        // it can sit exactly on its source mark (in phase with the crossfaded unvoiced stream)
        if (chosen >= 0 && markAt (chosen).voiced && ! lastGrainVoiced)
        {
            while (chosen < markCount && markAt (chosen).pos <= lastS + 0.5)
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
            out.s = lastGrainVoiced ? sNext : std::max (m.pos, lastS + 1.0);
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
        out.src = std::min (sNext + (std::abs (offset) < 0.5 ? 0.0 : offset), (double) nIn - hIn - 3.0);
        out.hIn = hIn;
        out.hOut = hIn / phi;
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
        p.src = std::min (tailNext + (std::abs (tailOffset) < 0.5 ? 0.0 : tailOffset), (double) nIn - unvoicedHop - 3.0);
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
        p.src = std::min (next + (std::abs (tailOffset) < 0.5 ? 0.0 : tailOffset), (double) nIn - unvoicedHop - 3.0);
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
        const double phi = std::pow (2.0, params.formant / 12.0);
        const double hU = unvoicedHop / phi;
        while (tailActive && tailNext - hU <= (double) t)
        {
            Plan g;
            g.voiced = false;
            g.s = tailNext;
            g.src = std::min (tailNext + (std::abs (tailOffset) < 0.5 ? 0.0 : tailOffset),
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
            // phase continuity only helps while the voiced grains are close to the source pitch
            // (correction); after a large shift the inherited offset is just a time jump
            lastSrcOffset = plan.src - plan.s;
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
        const double keepFrom = (double) t - 4.0 * maxHalf() - 4096.0;
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
    bool lastGrainVoiced = false;
    bool tailActive = false, xfadeArmed = false;
    double tailNext = 0.0, tailLast = 0.0, tailOffset = 0.0;
    int64_t xfadeEnd = -1;

    float mixSmooth = 1.0f, wetSmooth = 1.0f;
};
} // namespace vstm
