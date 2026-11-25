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
    bool     vibratoBand = true;    // 1.5: Vibrato Scale acts on the vibrato band only, never on the note decision (internal switch)
    float    transitionMs = -1.0f;  // 1.5: glide between notes; < 0 = automatic (follows ROBOTISM, as before)
    uint16_t bypassMask = 0;        // 1.5: degrees (relative to key) that pass through uncorrected (note bypass)
    float    toleranceCt = 0.0f;    // 1.5 (roadmap 22): the note centre may stay this close to its target, as sung
    float    confidenceMin = 0.0f;  // 1.5 (roadmap 28): below this detector clarity the correction fades out (0 = off)
    float    synthVibRateHz = 0.0f; // 1.5 (roadmap 26): synthetic vibrato on the output (depth 0 = off)
    float    synthVibDepthCt = 0.0f;
    int      forcedNote = -1;       // 1.5 (roadmap 49, MIDI perform): the target is this note, octave as played (-1 = off)
    int      scaleSteps = 0;        // 1.5 (roadmap 41): diatonic transpose, steps of the allowed notes (-7 .. +7)
    float    attackMs = -1.0f;      // 1.5 (roadmap 27): correction fade-in at a phrase start; < 0 = automatic (ROBOTISM)
    float    releaseMs = 0.0f;      // 1.5 (roadmap 27): fade-out time once the note has decayed 12 dB (0 = off)
    bool     naturalCentre = true;  // 1.6 (P0): during a vibrato the note is decided on the vibrato-free centre (internal switch)
    bool     naturalMotion = true;  // 1.6 (P0): during a vibrato only the centre is corrected, the vibrato passes (internal switch)
    bool     naturalTransition = true; // 1.6 (P0): the automatic glide follows the singer's own transition (internal switch)
    bool     octaveGuard = true;    // 1.6 (P0): a sudden octave jump of the detected pitch is held 40 ms (internal switch)
    int      naturalCentreRef = 0;  // 1.6 experiments: 0 = centre2 (2-pole 3 Hz), 1 = 2-pole 1.5 Hz
    bool     tuned = false;         // 1.5 (roadmap 45): pcOffset is in use (tuning system / microtonal degrees)
    float    pcOffset[12] {};       // semitones each pitch class (absolute, C = 0) sits off the equal-tempered grid
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
        float switchShift = 0.0f;   // target pitch of note minus that of fromNote (semitones; tunings: not whole)
        int   stepNote = -1;        // 1.5 diatonic transpose: the note the voice is moved to (-1 = none / off)
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
        swingW = 0.0f;
        swingEnv = 0.0f;
        natAge[0] = natAge[1] = natAge[2] = kLongAgo;
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
    Output process (bool voiced, float inMidi, float dt, const CorrectorSettings& s, float clarity = 1.0f, float levelDb = 0.0f) noexcept
    {
        Output out;
        if (! voiced)
        {
            wasVoiced = false;
            octSuspect = false;
            octStable = 0.0f;
            out.outMidi = inMidi + s.transpose;
            out.note = note == kNoNote ? -1 : note;
            return out;
        }

        // 1.6 (P0, octave robustness). No voice moves an octave within one analysis hop (a sung leap glides over tens
        // of ms), but the detector's octave errors do: MEASURED on the 145 real recordings, every octave error that
        // reached the output came from such a detector jump (3.6 per minute of voice, half of them <= 30 ms), and the
        // target followed it at once. A jump of 10 - 14 st between two voiced hops is now held: the pitch is read one
        // octave back (so the target and the correction stay, and the shifter puts the voice back where it was) until
        // the detector returns or the new octave has lasted 40 ms (then it is accepted: a real leap is late by that).
        // Natural side only (fades in from ROBOTISM 25 % to 50 %): hard tune follows every jump at once, as before.
        if (s.octaveGuard && wasVoiced && s.robotism > 0.25f)
        {
            octSinceAccept += dt;
            if (! octSuspect)
            {
                const float jump = inMidi - octLastGood;
                // a jump back to where the voice was before an octave change accepted within the last 0.3 s is the
                // detector returning from a longer error: followed at once (holding it would prolong the error)
                const bool back = octSinceAccept < 0.3f && std::abs (inMidi - octBefore) < 3.0f;
                // only a pitch that has held for 60 ms is trusted: a stretch that STARTS on a detector octave error
                // must be allowed to jump to the right octave
                if (std::abs (jump) >= 10.0f && std::abs (jump) <= 14.0f && ! back && octStable >= 0.06f)
                {
                    octSuspect = true;
                    octFold = jump > 0.0f ? -12.0f : 12.0f;
                    octAge = 0.0f;
                    octBefore = octLastGood;
                }
            }
            if (octSuspect)
            {
                octAge += dt;
                if (std::abs (inMidi - octLastGood) < 3.0f)
                    octSuspect = false;            // the detector is back
                else if (octAge >= kOctaveHoldS)
                {
                    octSuspect = false;            // the new octave has lasted: accept it
                    octSinceAccept = 0.0f;
                }
                else
                    inMidi += octFold;
            }
            octStable = std::abs (inMidi - octLastGood) < 1.0f ? octStable + dt : 0.0f;
        }
        else
        {
            octSuspect = false;
            octStable = 0.0f;
        }
        octLastGood = inMidi;

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
            confGate = 0.0f;           // confidence threshold: a note starts uncorrected until the detector is sure
            synthPhase = 0.0f;
            vibLp1 = inMidi;
            vibLp2 = inMidi;
            swingSide = 0;
            swingAge[0] = swingAge[1] = swingAge[2] = kLongAgo;
            swingW = 0.0f;
            swingEnv = 0.0f;
            natAge[0] = natAge[1] = natAge[2] = kLongAgo;
            onsetAge = 0.0f;
            relPeakDb = levelDb;
            relGate = 1.0f;
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

        // Vibrato Scale (1.5). Measured on a sung-like tone with a 5.5 Hz, +-35 ct vibrato: through the 3 Hz
        // one-pole centre, 0 % still left 40 % of the vibrato (the centre itself carries about half of a
        // 5.5 Hz vibrato), and an enlarged vibrato crossed the midpoint to the neighbouring note, so the
        // target switched on every cycle (200 %: less vibrato than 150 % on the natural side, a trill in hard
        // tune). Now:
        //  - shaped (the note decision) never sees an enlarged vibrato: its scale is capped at 1, so up to
        //    100 % the decision is exactly as before;
        //  - shapedOut (what is corrected and output) scales only the band above a 2-pole 1.5 Hz low-pass of
        //    the input: 0 % leaves ~7 % of a 5.5 Hz vibrato, 200 % gives ~2x.
        // At 100 % (the default) both are the old expression, so the output does not change.
        const float vibCoef = 1.0f - std::exp (-dt * 2.0f * (float) kPi * kVibLowpassHz);
        vibLp1 += (inMidi - vibLp1) * vibCoef;
        vibLp2 += (vibLp1 - vibLp2) * vibCoef;
        const float shaped = centre + (s.vibratoBand ? std::min (s.vibratoScale, 1.0f) : s.vibratoScale) * (inMidi - centre);
        const float shapedOut = (! s.vibratoBand || std::abs (s.vibratoScale - 1.0f) < 1.0e-6f) ? shaped
                              : inMidi + (s.vibratoScale - 1.0f) * (inMidi - vibLp2);

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
            natAge[2] = natAge[1];
            natAge[1] = natAge[0];
            natAge[0] = 0.0f;
        }
        for (float& a : natAge)
            a = std::min (a + dt, kLongAgo);
        if (s.adaptive)
            adapt.observe (inMidi, centre, centreBefore, clarity, noteAge, dt, adaptState);

        // 1.6 (P0, natural pitch centre). With a vibrato wide enough to reach past the midpoint to the neighbouring
        // note, deciding on the raw pitch switched the target on every peak / trough (MEASURED on the 145 real
        // recordings, key C chromatic: 12 - 15 switches per second on sustained vibrato notes, the corrected
        // centre 17 ct off the note). While the voice swings around its centre (three side changes within
        // kSwingWindowS: a vibrato), the note is now decided on the vibrato-free centre (centre2); a real change
        // still switches: a leap at once (raw pitch, as before), a step once the centre gets there. Fades in from
        // ROBOTISM 25 % to 50 %: the robotic end decides on the raw pitch as before (the hard-tune warble stays).
        // after a real change (a leap, or the voice leaving its vibrato) the swing is re-learnt around the new note,
        // so it lands with the normal retune (the slow centre is still on its way there); switches the centre
        // itself decides keep it
        swingW += ((natAge[2] < kSwingWindowS ? 1.0f : 0.0f) - swingW) * (1.0f - std::exp (-dt / 0.03f));
        if (swingW < 1.0e-3f)
            swingW = 0.0f;
        // A real change leaves the vibrato's recent extent around its centre (a vibrato never does): the decision
        // then goes back to the raw pitch at once, so a sung step switches as before instead of when the slow
        // centre arrives. Extent: peak of |pitch - centre| with a 0.3 s release.
        const float swingRef = s.naturalCentreRef == 1 ? vibLp2 : centre2;
        const float away = std::abs (shaped - swingRef);
        const bool departing = away > 1.15f * swingEnv + 0.10f;
        swingEnv = std::max (away, swingEnv * std::exp (-dt / 0.3f));
        const float natW = s.naturalCentre && ! departing ? smoothstep (0.25f, 0.5f, s.robotism) * swingW : 0.0f;
        const float dec = natW > 0.0f ? shaped + natW * (swingRef - shaped) : shaped;

        // target note with hysteresis (cents) and a minimum dwell time before switching
        const float hyst = (15.0f + 20.0f * human) * 0.01f;
        // 1.5 (roadmap 45): a note's target pitch; off the equal grid only with a tuning system (else exactly n)
        auto tp = [&s] (int n) { return s.tuned ? (float) n + s.pcOffset[((n % 12) + 12) % 12] : (float) n; };
        auto nearest = [&s] (float x) { return s.tuned ? nearestAllowedTunedNote (x, s.key, s.mask, s.pcOffset)
                                                       : nearestAllowedNote (x, s.key, s.mask); };
        int candidate = s.forcedNote >= 0 ? s.forcedNote : nearest (dec);
        if (natW > 0.0f && s.forcedNote < 0 && note != kNoNote)
        {
            // a leap on the raw pitch (>= 1.5 st from the held note, within 0.5 st of another): no vibrato does that
            const int raw = nearest (shaped);
            if (raw != note && std::abs (shaped - tp (note)) >= 1.5f && std::abs (shaped - tp (raw)) <= 0.5f)
                candidate = raw;
        }

        // consecutive hops the voice has been past the midpoint toward `candidate` (for backdateHops)
        if (note != kNoNote && candidate != note && std::abs (shaped - tp (candidate)) < std::abs (shaped - tp (note)))
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
            glideFrom = tp (note);
            glide = tp (note);
            glideProgress = 1.0f;
            noteAge = 0.0f;
            settledFlag = false;
            out.noteChanged = true;
        }
        else if (candidate != note && s.forcedNote >= 0)
        {
            startTransition (candidate);           // MIDI perform: the played note is the target at once
            out.noteChanged = true;
        }
        else if (candidate != note && ! isAllowed (note, s.key, s.mask))
        {
            startTransition (candidate);           // scale or key changed under the held note
            forcedGlideMs = kKeyChangeGlideMs;     // a held note never jumps because the key / scale changed
            out.noteChanged = true;
        }
        else if (candidate != note
                 && std::abs (dec - tp (candidate)) + hyst < std::abs (dec - tp (note)))
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
            const bool leap = std::abs (shaped - tp (note)) >= 1.5f
                           && std::abs (shaped - tp (candidate)) <= 0.5f;
            const bool arrived = leap || std::abs (dec - tp (candidate)) <= kArriveNear;
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
                out.switchShift = tp (candidate) - tp (note);
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

        if (out.noteChanged && s.naturalCentre && natW <= 0.0f && swingW > 0.0f)
        {
            natAge[0] = natAge[1] = natAge[2] = kLongAgo;   // 1.6: the natural decision / motion re-learn the new note's swing
            swingW = 0.0f;
            swingEnv = 0.0f;
        }

        // "insansi gecis": S-curve glide between notes, only on the human side
        // (Adaptive: up to 60 % shorter in fast runs, so short notes can still land)
        // 1.5 (roadmap 20 / 36): a set Note Transition replaces the automatic glide, on both sides of ROBOTISM
        const float autoGlideMs = 110.0f * human * (s.adaptive ? 1.0f - 0.6f * adaptState.fast : 1.0f);
        const float glideMs = std::max (s.transitionMs >= 0.0f ? s.transitionMs : autoGlideMs, forcedGlideMs);
        // 1.6 (P0, natural transitions): the automatic glide of the human side follows the singer. Its progress is
        // how far the voice has come from where the glide started toward the new note (never backwards), at least the
        // pace of twice the automatic glide time (a voice that stops short still lands). The singer's own portamento
        // speed and shape pass instead of a fixed S-curve (MEASURED, 1.5 at ROBOTISM 80 %: the output transitions
        // took 1.28 x the singer's on real recordings, 2.45 x on synthetic glides). A set Note Transition and the
        // key-change glide keep the S-curve; the robotic side has no glide.
        const bool followSinger = s.naturalTransition && s.transitionMs < 0.0f && forcedGlideMs <= 0.0f;
        if (glideMs > 0.5f && glideProgress < 1.0f && followSinger)
        {
            const float span = tp (note) - glideFrom;
            const float sung = std::abs (span) > 1.0e-3f ? clamp01 ((shaped - glideFrom) / span) : 1.0f;
            followP = std::max (followP, sung);
            followT = std::min (1.0f, followT + dt * 1000.0f / (2.0f * glideMs));
            glideProgress = std::max (followP, followT);
            glide = glideFrom + span * glideProgress;
        }
        else if (glideMs > 0.5f && glideProgress < 1.0f)
        {
            glideProgress = std::min (1.0f, glideProgress + dt * 1000.0f / glideMs);
            glide = glideFrom + (tp (note) - glideFrom) * smoothstep (glideProgress);
        }
        else
        {
            glideProgress = 1.0f;
            glide = tp (note);
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

        float target = glide - shapedOut;
        // 1.6 (P0, natural motion): while the voice swings (and is not leaving its vibrato), the correction pulls the
        // vibrato-free centre to the note and lets the vibrato itself pass: deciding on a steady note made the
        // retune smoothing act on the vibrato (MEASURED: depth 0.85 -> 0.71 at ROBOTISM 50 %). Same weight as the
        // natural decision (none at the robotic end).
        if (s.naturalMotion && natW > 0.0f)
            target += natW * (shapedOut - swingRef);
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
            target += adaptState.vibrato * freedom * (shapedOut - centre);
            tauMs *= 1.0f - 0.5f * adaptState.fast;
            adaptAmount = (0.5f + 0.5f * adaptState.confidence) * (1.0f - 0.6f * adaptState.slide * freedom);
        }
        // 1.5 tolerance (roadmap 22, like a flex / tolerance control): the vibrato-free note centre may stay up
        // to toleranceCt from the target as sung; only the part beyond it is corrected. Vibrato and faster
        // movement stay as they are. 0 = off (the target above, unchanged).
        if (s.toleranceCt > 0.0f)
        {
            const float tol = s.toleranceCt * 0.01f;
            target = glide + std::clamp (centre - glide, -tol, tol) - centre;
        }
        // 1.5 note bypass: on a bypassed note the correction goes to zero (through the retune smoothing,
        // so entering and leaving it glides like any other target change)
        if (s.bypassMask != 0 && note != kNoNote && isAllowed (note, s.key, s.bypassMask))
            target = 0.0f;
        if (tauMs < 0.5f)
            delta = target;
        else
            delta += (target - delta) * (1.0f - std::exp (-dt * 1000.0f / tauMs));

        // natural scoop at the start of a phrase survives on the human side
        const float onsetMs = s.attackMs >= 0.0f ? s.attackMs : 70.0f * human;     // 1.5: a set attack replaces it
        const float onsetGain = onsetMs > 1.0f ? smoothstep (onsetAge * 1000.0f / onsetMs) : 1.0f;
        // 1.5 confidence threshold (roadmap 28): while the detector's clarity is below the threshold the correction
        // fades out (20 ms) and the voice passes as sung; 0.03 of hysteresis. 0 = off.
        float confAmount = 1.0f;
        if (s.confidenceMin > 0.0f)
        {
            const float want = clarity >= s.confidenceMin ? 1.0f
                             : (clarity < s.confidenceMin - 0.03f ? 0.0f : (confGate > 0.5f ? 1.0f : 0.0f));
            confGate += (want - confGate) * (1.0f - std::exp (-dt * 1000.0f / 20.0f));
            confAmount = confGate;
        }
        // 1.5 release (roadmap 27): once the voice has decayed 12 dB below its recent peak (the peak falls back 6 dB/s),
        // the correction fades out over releaseMs, so a natural fall at a note's end passes as sung; it comes back
        // (same time) when the level does. 0 = off.
        float relAmount = 1.0f;
        if (s.releaseMs > 0.0f)
        {
            relPeakDb = std::max (levelDb, relPeakDb - 6.0f * dt);
            const float want = levelDb < relPeakDb - 12.0f ? 0.0f : 1.0f;
            relGate += (want - relGate) * (1.0f - std::exp (-dt * 1000.0f / s.releaseMs));
            relAmount = relGate;
        }
        const float amount = clamp01 (s.amount) * onsetGain * adaptAmount * confAmount * relAmount;

        out.outMidi = shapedOut + amount * delta + s.transpose;
        // 1.5 (roadmap 41): diatonic transpose. The voice moves by scaleSteps of the allowed notes (in C major +2:
        // C -> E, D -> F), on top of the correction and the transpose, whatever the correction amount; the step
        // size changes with the note and glides with it (the note's transition).
        if (s.scaleSteps != 0 && note != kNoNote)
        {
            int n = note;
            const int dir = s.scaleSteps > 0 ? 1 : -1;
            for (int k = 0; k != s.scaleSteps; k += dir)
            {
                int m = n + dir;
                for (int guard = 0; guard < 12 && ! isAllowed (m, s.key, s.mask == 0 ? (uint16_t) 0x0fff : s.mask); ++guard)
                    m += dir;
                n = m;
            }
            const float want = tp (n) - tp (note);
            if (out.noteChanged)
                stepFrom = stepNow;
            stepTo = want;
            stepNow = glideProgress >= 1.0f ? stepTo : stepFrom + (stepTo - stepFrom) * smoothstep (glideProgress);
            out.outMidi += stepNow;
            out.stepNote = n;
        }
        else
            stepNow = stepFrom = stepTo = 0.0f;
        if (s.synthVibDepthCt > 0.0f && s.synthVibRateHz > 0.0f)
        {
            // 1.5 synthetic vibrato (roadmap 26): restarts with each note, starts 250 ms in, fades in over 200 ms
            if (out.noteChanged)
                synthPhase = 0.0f;
            synthPhase += dt * s.synthVibRateHz;
            synthPhase -= std::floor (synthPhase);
            out.outMidi += s.synthVibDepthCt * 0.01f * smoothstep (0.25f, 0.45f, noteAge)
                         * std::sin (2.0f * (float) kPi * synthPhase);
        }
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
    static constexpr float kOctaveHoldS = 0.040f;    // 1.6: a sudden octave jump must last this long to be followed
    static constexpr float kVibLowpassHz = 1.5f;     // Vibrato Scale reference: 2 x one-pole, a 5.5 Hz vibrato is -23 dB
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
        followP = followT = 0.0f;
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
    float stepNow = 0.0f, stepFrom = 0.0f, stepTo = 0.0f;     // 1.5 diatonic transpose (semitones)
    float followP = 0.0f, followT = 0.0f;                       // 1.6 natural transition: sung / time progress
    bool octSuspect = false;                                    // 1.6 octave guard: a sudden octave jump is being held
    float octFold = 0.0f, octAge = 0.0f, octLastGood = 0.0f;
    float octBefore = 0.0f, octSinceAccept = 10.0f;            // where the voice was before an accepted octave change
    float octStable = 0.0f;                                     // how long the pitch has held (within 1 st)
    float relPeakDb = 0.0f, relGate = 1.0f;                     // 1.5 release
    float noteAge = 0.0f, onsetAge = 0.0f;
    float centre2 = 0.0f;                  // 2-pole vibrato-free centre (swing tracker)
    float vibLp1 = 0.0f, vibLp2 = 0.0f;    // 1.5: 2-pole 1.5 Hz low-pass of the input (Vibrato Scale reference)
    float confGate = 1.0f;                 // 1.5: confidence threshold gate (1 = correcting)
    float synthPhase = 0.0f;               // 1.5: synthetic vibrato phase (cycles)
    int swingSide = 0;
    float swingAge[3] = { kLongAgo, kLongAgo, kLongAgo };
    float swingW = 0.0f;                   // 1.6: smoothed "the voice is swinging" (natural centre decision)
    float swingEnv = 0.0f;                 // 1.6: recent extent of the swing around its centre (semitones)
    float natAge[3] = { kLongAgo, kLongAgo, kLongAgo };   // 1.6: swing side changes since the current note began
    bool settledFlag = true;
    Adapt adapt;
    AdaptiveState adaptState;
};
} // namespace vstm
