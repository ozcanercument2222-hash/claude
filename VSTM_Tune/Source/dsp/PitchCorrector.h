#pragma once
//==============================================================================
//  Perde duzeltme mantigi: hangi notaya, ne hizla, nasil bir gecisle?
//
//  Her analiz adiminda (~3 ms) girisin perdesi (MIDI, ondalikli) alinir ve cikis
//  perdesi hesaplanir. Klasik "retune speed" modeli:  cikis = giris + LPF(hedef - giris)
//    - LPF zaman sabiti 0 ise cikis hedef notanin kendisidir (dumduz, robotik),
//    - zaman sabiti buyudukce vibrato ve dogal oynamalar gecer, yalniz ortalama
//      sapma duzeltilir (dogal).
//
//  ROBOTISM (0 = Robotik ... 0.5 = notr ... 1 = Insansi) bu modelin uzerinde
//  calisan tek bir makrodur:
//    sola  -> retune hizi sifira gider, nota gecisleri basamak olur, histerezis kuculur,
//             humanize devreden cikar: dumduz, "T-Pain" tarzi robotik ses.
//    saga  -> retune 4 kata kadar yavaslar, notalar arasi gecis S-egrisiyle kayar
//             (en fazla 110 ms), notanin baslangicindaki dogal "kayis" serbest kalir,
//             uzun notalarda humanize artar: duzeltme duyulmaz, ses insani kalir.
//
//  Nota secimi: yeni nota ancak (1) histerezisi asinca ve (2) kisa bir sure kararli
//  kalinca secilir. Sarkici iki nota arasinda (or. E ile F'nin ortasinda) dururken
//  hedefin her 3-10 ms'de bir gidip gelmesi ("nota titremesi") boylece engellenir;
//  ses yeni notaya acikca vardiginda (>= 1.5 yarim ses sicrama) beklemeden gecilir.
//==============================================================================
#include "DspCommon.h"
#include "Scales.h"

namespace vstm
{
struct CorrectorSettings
{
    int      key = 0;               // tonic 0..11
    uint16_t mask = 0x0fff;         // allowed degrees relative to key
    float    amount = 1.0f;         // pitch correction 0..1
    float    retuneMs = 15.0f;      // retune speed
    float    humanize = 0.35f;      // 0..1
    float    robotism = 0.5f;       // 0 = robotic, 1 = human
    float    vibratoScale = 1.0f;   // 0..2, depth of natural pitch modulation
    float    transpose = 0.0f;      // semitones
    bool     adaptive = false;      // Adaptive: follows the performance within fixed limits (see below)
    bool     swingKeep = true;      // 1.3: a switch inside a vibrato swing keeps the retune speed (internal switch)
};

class PitchCorrector
{
public:
    struct Output
    {
        float outMidi = 0.0f;       // output pitch (includes transpose)
        float passThrough = 1.0f;   // how much of fast input movement survives (0 = fully flattened)
        int   note = -1;            // target note (without transpose), -1 = none
        bool  noteChanged = false;
        bool  settled = false;
        // A switch to a new note (noteChanged) was decided only after the hysteresis and the dwell; the
        // voice had already been past the midpoint toward that note for backdateHops earlier hops. When
        // the correction is instantaneous (instant: hard tune, no glide), the engine may give those hops
        // the new note too, where the output has not been played yet (1.3, P0: the switch then sits
        // where the singer crossed the midpoint, not where the decision was certain).
        int   backdateHops = 0;
        int   fromNote = -1;        // the note the target left (valid when noteChanged)
        float amountNow = 1.0f;     // correction amount applied this hop (0..1)
        bool  instant = false;      // the output follows the target note within about one hop
    };

    void reset() noexcept
    {
        wasVoiced = false;
        note = kNoNote;
        pendingNote = kNoNote;
        pendingAge = 0.0f;
        midHops = 0;
        midCandidate = kNoNote;
        swingSide = 0;
        swingAge[0] = swingAge[1] = swingAge[2] = kLongAgo;
        delta = 0.0f;
        glideProgress = 1.0f;
        forcedGlideMs = 0.0f;
        settledFlag = true;
        adapt = {};
        adaptState = {};
    }

    /** What Adaptive is doing right now (0..1 each), for tests and display. */
    struct AdaptiveState
    {
        float confidence = 1.0f;    // detector certainty (clarity), smoothed
        float vibrato = 0.0f;       // a regular 3.5 - 8.5 Hz modulation is being sung
        float slide = 0.0f;         // the note centre is moving fast (a deliberate slide)
        float fast = 0.0f;          // many note changes per second (runs, rap)
    };
    const AdaptiveState& getAdaptiveState() const noexcept { return adaptState; }

    /** clarity: the detector's 1 - CMND minimum of this frame (only used by Adaptive). */
    Output process (bool voiced, float inMidi, float dt, const CorrectorSettings& s, float clarity = 1.0f) noexcept
    {
        Output out;
        if (! voiced)
        {
            wasVoiced = false;
            out.outMidi = inMidi + s.transpose;
            out.note = note == kNoNote ? -1 : note;
            return out;
        }

        const float robot = clamp01 ((0.5f - s.robotism) * 2.0f);    // 0..1 toward "Robotik"
        // Insansi side: the human-side amounts (glide, onset scoop, slower retune, wider
        // hysteresis, longer dwell) grow along h^1.6 instead of linearly. Measured on four voice
        // types (VSTM_TUNE_AUDIT.md 16.4): linearly, the note transition went 22 -> 76 ms between
        // 50 % and 65 % and only 160 -> 214 ms between 90 % and 100 %; now it grows by a similar
        // factor per step. 50 % (user settings as they are) and 100 % are unchanged.
        const float human = std::pow (clamp01 ((s.robotism - 0.5f) * 2.0f), 1.6f);

        // vibrato / fast-modulation split: note centre is a 3 Hz one-pole of the input
        if (! wasVoiced)
        {
            centre = inMidi;
            centre2 = inMidi;
            swingSide = 0;
            swingAge[0] = swingAge[1] = swingAge[2] = kLongAgo;
            onsetAge = 0.0f;
            delta = 0.0f;
            note = kNoNote;
            pendingNote = kNoNote;
            pendingAge = 0.0f;
            midHops = 0;
            midCandidate = kNoNote;
            if (s.adaptive)
                adapt.restart (inMidi);
        }
        const float centreBefore = centre;
        const float centreCoef = 1.0f - std::exp (-dt * 2.0f * (float) kPi * 3.0f);
        centre += (inMidi - centre) * centreCoef;
        centre2 += (centre - centre2) * centreCoef;     // 2-pole: a 5.5 Hz vibrato is ~13 dB down
        const float shaped = centre + s.vibratoScale * (inMidi - centre);

        // vibrato swing tracker (used at a note switch below): which side of its vibrato-free centre the
        // voice is on, and how long ago it last changed sides (the three most recent changes)
        for (float& a : swingAge)
            a = std::min (a + dt, kLongAgo);
        const int side = shaped > centre2 + kSwingSt ? 1 : (shaped < centre2 - kSwingSt ? -1 : 0);
        if (side != 0 && side != swingSide)
        {
            swingAge[2] = swingAge[1];
            swingAge[1] = swingAge[0];
            swingAge[0] = 0.0f;
            swingSide = side;
        }
        if (s.adaptive)
            adapt.observe (inMidi, centre, centreBefore, clarity, noteAge, dt, adaptState);

        // target note with hysteresis (cents) and a minimum dwell time before switching
        const float hyst = (15.0f + 20.0f * human) * 0.01f;
        const int candidate = nearestAllowedNote (shaped, s.key, s.mask);

        // consecutive hops the voice has been past the midpoint toward `candidate` (for backdateHops)
        if (note != kNoNote && candidate != note && std::abs (shaped - (float) candidate) < std::abs (shaped - (float) note))
        {
            midHops = candidate == midCandidate ? midHops + 1 : 1;
            midCandidate = candidate;
        }
        else
        {
            midHops = 0;
            midCandidate = kNoNote;
        }
        if (note == kNoNote)
        {
            note = candidate;
            glideFrom = (float) note;
            glide = (float) note;
            glideProgress = 1.0f;
            noteAge = 0.0f;
            settledFlag = false;
            out.noteChanged = true;
        }
        else if (candidate != note && ! isAllowed (note, s.key, s.mask))
        {
            startTransition (candidate);           // scale or key changed under the held note
            forcedGlideMs = kKeyChangeGlideMs;     // a held note never jumps because the key / scale changed
            out.noteChanged = true;
        }
        else if (candidate != note
                 && std::abs (shaped - (float) candidate) + hyst < std::abs (shaped - (float) note))
        {
            if (candidate == pendingNote)
                pendingAge += dt;
            else
            {
                pendingNote = candidate;
                pendingAge = dt;
            }
            // clearly arrived at another note: switch now; hovering between two notes must persist
            // before the target moves. Arrived = a real leap (>= 1.5 st from the held note, within 0.5 st
            // of the new one) or close to the new note (within kArriveNear): a singer hovering between
            // two neighbouring notes stays >= 40 ct from both, so the dwell still stops note chatter, while
            // a sung step no longer waits for it (with the correctly timed detector of 1.2 the dwell
            // made every semitone step switch 14 - 52 ms after the singer did; 1.2 report, P0-E)
            const bool leap = std::abs (shaped - (float) note) >= 1.5f
                           && std::abs (shaped - (float) candidate) <= 0.5f;
            const bool arrived = leap || std::abs (shaped - (float) candidate) <= kArriveNear;
            const float dwellMs = arrived ? 0.0f : 22.0f + 30.0f * human - 8.0f * robot;
            if (pendingAge * 1000.0f >= dwellMs)
            {
                // Vibrato (1.3, human side). A vibrato wide enough to come near the neighbouring note makes
                // the target switch to it and back on every cycle (VocalSet vibrato: 9 - 26 switches per second
                // on held notes, at every ROBOTISM setting). Each switch restarted the note's retune speed
                // (the slow "sustained" retune of humanize), so the correction followed the vibrato and
                // flattened it: at 80 % only 42 % of the vibrato was left. While the voice is swinging around
                // its centre (three side changes within kSwingWindowS), a switch keeps the retune speed of the
                // held note instead. The switch itself (which note, when) and the glide are unchanged, so the
                // number of note switches is the same; leaps restart as before. Faded in over the first 15 %
                // of `human`: 50 % and the robotic side are unchanged.
                const float keep = ! s.swingKeep || leap || swingAge[2] >= kSwingWindowS ? 0.0f : smoothstep (0.0f, 0.15f, human);
                const float keptAge = noteAge * keep;
                out.fromNote = note;
                out.backdateHops = std::max (0, midHops - 1);
                startTransition (candidate);
                noteAge = keptAge;
                out.noteChanged = true;
            }
        }
        else
        {
            pendingNote = kNoNote;
            pendingAge = 0.0f;
        }

        // "insansi gecis": S-curve glide between notes, only on the human side
        // (Adaptive: up to 60 % shorter in fast runs, so short notes can still land)
        const float glideMs = std::max (110.0f * human * (s.adaptive ? 1.0f - 0.6f * adaptState.fast : 1.0f), forcedGlideMs);
        if (glideMs > 0.5f && glideProgress < 1.0f)
        {
            glideProgress = std::min (1.0f, glideProgress + dt * 1000.0f / glideMs);
            glide = glideFrom + ((float) note - glideFrom) * smoothstep (glideProgress);
        }
        else
        {
            glideProgress = 1.0f;
            glide = (float) note;
        }
        if (glideProgress >= 1.0f)
            forcedGlideMs = 0.0f;

        // retune speed, shaped by robotism and (for sustained notes) humanize. Robotik side:
        // (1 - r)(1 - r/2) instead of (1 - r)^2, so 0 - 10 % is no longer one flat "dead" zone
        // (vibrato kept 1.0 % -> 0.9 % before); 0 % stays instant and 50 % unchanged.
        const float speedMul = (1.0f - robot) * (1.0f - 0.5f * robot) * (1.0f + 3.0f * human);
        const float humanizeEff = clamp01 (s.humanize * (1.0f - robot) + 0.4f * human * (1.0f - s.humanize));
        const float sustain = smoothstep (0.08f, 0.40f, noteAge);
        float tauMs = s.retuneMs * speedMul * (1.0f + 6.0f * humanizeEff * sustain);

        float target = glide - shaped;
        float adaptAmount = 1.0f;
        if (s.adaptive)
        {
            // Adaptive (VSTM_TUNE_AUDIT.md 16.5). It never changes a parameter; it scales the
            // correction inside fixed limits, and the robotic end keeps its hard effect:
            //  - unsure detector (breathy, noisy, ambiguous voice): correction down to 50 %
            //  - a sung vibrato: only the note centre is pulled, the vibrato stays (fades in
            //    from ROBOTISM 15 % to 50 %)
            //  - a deliberate slide: correction down to 40 % while the centre moves fast (same fade)
            //  - fast runs (> ~4 note changes / s): retune up to twice as fast and the human-side
            //    glide up to 60 % shorter, so short notes land
            const float freedom = smoothstep (0.15f, 0.5f, s.robotism);
            target += adaptState.vibrato * freedom * (shaped - centre);
            tauMs *= 1.0f - 0.5f * adaptState.fast;
            adaptAmount = (0.5f + 0.5f * adaptState.confidence) * (1.0f - 0.6f * adaptState.slide * freedom);
        }
        if (tauMs < 0.5f)
            delta = target;
        else
            delta += (target - delta) * (1.0f - std::exp (-dt * 1000.0f / tauMs));

        // natural scoop at the start of a phrase survives on the human side
        const float onsetMs = 70.0f * human;
        const float onsetGain = onsetMs > 1.0f ? smoothstep (onsetAge * 1000.0f / onsetMs) : 1.0f;
        const float amount = clamp01 (s.amount) * onsetGain * adaptAmount;

        out.outMidi = shaped + amount * delta + s.transpose;
        out.note = note;
        out.amountNow = amount;
        out.instant = tauMs < kInstantTauMs && glideMs < 0.5f;

        // fraction of fast input movement that survives in the output: used by the shifter
        // to apply the correction against the *exact* local period of each grain
        const float follow = std::exp (-tauMs / 30.0f);
        out.passThrough = clamp01 (1.0f - amount * follow);

        if (! settledFlag && glideProgress >= 1.0f && std::abs (target - delta) < 0.05f)
        {
            settledFlag = true;
            out.settled = true;
        }
        if (s.adaptive && out.noteChanged)
            adapt.noteRate += 1.0f;

        noteAge += dt;
        onsetAge += dt;
        wasVoiced = true;
        return out;
    }

private:
    static constexpr int kNoNote = -1000;
    static constexpr float kArriveNear = 0.30f;      // semitones from the new note: arrived, no dwell
    static constexpr float kSwingSt = 0.25f;         // vibrato swing: beyond this from the vibrato-free centre
    static constexpr float kSwingWindowS = 0.40f;    // three side changes within this = the voice is swinging
    static constexpr float kLongAgo = 10.0f;
    static constexpr float kInstantTauMs = 3.0f;     // retune time constant below which the output follows the target in ~1 hop
    // A key / scale change (the user's, or the Auto Key lock) moves a held note to the new target over at
    // least this long, also on the robotic side (1.0 / 1.1: an instant jump there; 1.2, P0-E)
    static constexpr float kKeyChangeGlideMs = 25.0f;

    void startTransition (int newNote) noexcept
    {
        forcedGlideMs = 0.0f;
        pendingNote = kNoNote;
        pendingAge = 0.0f;
        glideFrom = glide;
        note = newNote;
        glideProgress = 0.0f;
        noteAge = 0.0f;
        settledFlag = false;
    }

    /** Adaptive's observations of the performance (only updated while Adaptive is on). */
    struct Adapt
    {
        float conf = 1.0f, devPow = 0.0f, rate = 0.0f, slope = 0.0f, noteRate = 0.0f;
        float lastDev = 0.0f, sinceCross = 0.0f;

        void restart (float) noexcept
        {
            devPow = 0.0f;
            rate = 0.0f;
            slope = 0.0f;
            lastDev = 0.0f;
            sinceCross = 0.0f;
        }

        void observe (float inMidi, float centreNow, float centreBefore, float clarity, float noteAge, float dt,
                      AdaptiveState& st) noexcept
        {
            auto follow = [dt] (float& v, float x, float tau) { v += (x - v) * (1.0f - std::exp (-dt / tau)); };

            follow (conf, smoothstep (0.70f, 0.92f, clarity), 0.03f);

            // vibrato: regular movement around the note centre, 3.5 - 8.5 Hz, +-10 ... +-100 ct
            const float dev = inMidi - centreNow;
            follow (devPow, dev * dev, 0.25f);
            sinceCross += dt;
            if (lastDev < -0.02f && dev >= -0.02f && dev > lastDev)        // upward crossing
            {
                if (sinceCross > 0.08f && sinceCross < 0.4f)
                    rate = rate <= 0.0f ? 1.0f / sinceCross : rate + (1.0f / sinceCross - rate) * 0.5f;
                sinceCross = 0.0f;
            }
            if (sinceCross > 0.4f)
                rate *= 0.5f;                                              // no regular crossings: no vibrato
            lastDev = dev;
            const float depth = std::sqrt (devPow);                       // rms in semitones
            const float inBand = smoothstep (2.8f, 3.5f, rate) * (1.0f - smoothstep (8.5f, 10.0f, rate));
            st.vibrato = smoothstep (0.05f, 0.12f, depth) * inBand * smoothstep (0.15f, 0.35f, noteAge);

            // fast runs: decaying count of note changes (1 s memory)
            noteRate *= std::exp (-dt / 1.0f);
            st.fast = smoothstep (3.0f, 6.0f, noteRate);

            // slide: the 3 Hz note centre moves faster than 4 ... 10 semitones per second. A fast
            // run moves the centre just as fast, in steps: there it is not a slide.
            follow (slope, (centreNow - centreBefore) / std::max (dt, 1.0e-4f), 0.03f);
            st.slide = smoothstep (4.0f, 10.0f, std::abs (slope)) * (1.0f - st.fast);
            st.confidence = conf;
        }
    };

    bool wasVoiced = false;
    int note = kNoNote;
    int pendingNote = kNoNote, midCandidate = kNoNote, midHops = 0;
    float pendingAge = 0.0f;
    float centre = 0.0f, delta = 0.0f;
    float glideFrom = 0.0f, glide = 0.0f, glideProgress = 1.0f, forcedGlideMs = 0.0f;
    float noteAge = 0.0f, onsetAge = 0.0f;
    float centre2 = 0.0f;                  // 2-pole vibrato-free centre (swing tracker)
    int swingSide = 0;
    float swingAge[3] = { kLongAgo, kLongAgo, kLongAgo };
    bool settledFlag = true;
    Adapt adapt;
    AdaptiveState adaptState;
};
} // namespace vstm
