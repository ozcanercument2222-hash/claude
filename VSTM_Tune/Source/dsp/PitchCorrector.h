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
    };

    void reset() noexcept
    {
        wasVoiced = false;
        note = kNoNote;
        pendingNote = kNoNote;
        pendingAge = 0.0f;
        delta = 0.0f;
        glideProgress = 1.0f;
        settledFlag = true;
    }

    Output process (bool voiced, float inMidi, float dt, const CorrectorSettings& s) noexcept
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
            onsetAge = 0.0f;
            delta = 0.0f;
            note = kNoNote;
            pendingNote = kNoNote;
            pendingAge = 0.0f;
        }
        centre += (inMidi - centre) * (1.0f - std::exp (-dt * 2.0f * (float) kPi * 3.0f));
        const float shaped = centre + s.vibratoScale * (inMidi - centre);

        // target note with hysteresis (cents) and a minimum dwell time before switching
        const float hyst = (15.0f + 20.0f * human) * 0.01f;
        const int candidate = nearestAllowedNote (shaped, s.key, s.mask);
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
            // clearly arrived at another note (a real leap): switch now; hovering between
            // two notes must persist before the target moves
            const bool arrived = std::abs (shaped - (float) note) >= 1.5f
                              && std::abs (shaped - (float) candidate) <= 0.5f;
            const float dwellMs = arrived ? 0.0f : 22.0f + 30.0f * human - 8.0f * robot;
            if (pendingAge * 1000.0f >= dwellMs)
            {
                startTransition (candidate);
                out.noteChanged = true;
            }
        }
        else
        {
            pendingNote = kNoNote;
            pendingAge = 0.0f;
        }

        // "insansi gecis": S-curve glide between notes, only on the human side
        const float glideMs = 110.0f * human;
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

        // retune speed, shaped by robotism and (for sustained notes) humanize. Robotik side:
        // (1 - r)(1 - r/2) instead of (1 - r)^2, so 0 - 10 % is no longer one flat "dead" zone
        // (vibrato kept 1.0 % -> 0.9 % before); 0 % stays instant and 50 % unchanged.
        const float speedMul = (1.0f - robot) * (1.0f - 0.5f * robot) * (1.0f + 3.0f * human);
        const float humanizeEff = clamp01 (s.humanize * (1.0f - robot) + 0.4f * human * (1.0f - s.humanize));
        const float sustain = smoothstep (0.08f, 0.40f, noteAge);
        const float tauMs = s.retuneMs * speedMul * (1.0f + 6.0f * humanizeEff * sustain);

        const float target = glide - shaped;
        if (tauMs < 0.5f)
            delta = target;
        else
            delta += (target - delta) * (1.0f - std::exp (-dt * 1000.0f / tauMs));

        // natural scoop at the start of a phrase survives on the human side
        const float onsetMs = 70.0f * human;
        const float onsetGain = onsetMs > 1.0f ? smoothstep (onsetAge * 1000.0f / onsetMs) : 1.0f;
        const float amount = clamp01 (s.amount) * onsetGain;

        out.outMidi = shaped + amount * delta + s.transpose;
        out.note = note;

        // fraction of fast input movement that survives in the output: used by the shifter
        // to apply the correction against the *exact* local period of each grain
        const float follow = std::exp (-tauMs / 30.0f);
        out.passThrough = clamp01 (1.0f - amount * follow);

        if (! settledFlag && glideProgress >= 1.0f && std::abs (target - delta) < 0.05f)
        {
            settledFlag = true;
            out.settled = true;
        }

        noteAge += dt;
        onsetAge += dt;
        wasVoiced = true;
        return out;
    }

private:
    static constexpr int kNoNote = -1000;

    void startTransition (int newNote) noexcept
    {
        pendingNote = kNoNote;
        pendingAge = 0.0f;
        glideFrom = glide;
        note = newNote;
        glideProgress = 0.0f;
        noteAge = 0.0f;
        settledFlag = false;
    }

    bool wasVoiced = false;
    int note = kNoNote;
    int pendingNote = kNoNote;
    float pendingAge = 0.0f;
    float centre = 0.0f, delta = 0.0f;
    float glideFrom = 0.0f, glide = 0.0f, glideProgress = 1.0f;
    float noteAge = 0.0f, onsetAge = 0.0f;
    bool settledFlag = true;
};
} // namespace vstm
