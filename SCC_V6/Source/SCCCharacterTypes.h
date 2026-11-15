//==============================================================================
//  SCCCharacterTypes.h  -  Character Engine'in veri tipleri (JUCE'dan bagimsiz)
//
//  Tek dogruluk kaynagi: Character Designer'in 15 kontrolu burada tanimlidir.
//  Plugin parametreleri, arayuz, preset dosyalari ve testler bu tabloyu kullanir.
//==============================================================================
#ifndef SCC_CHARACTER_TYPES_H_INCLUDED
#define SCC_CHARACTER_TYPES_H_INCLUDED

namespace scc
{

//------------------------------------------------------------------------------
//  Karakter tipleri. Ilk alti hazir (her biri farkli DSP yapisi), sonuncusu
//  kullanicinin Character Designer ile kurdugu zincir.
//------------------------------------------------------------------------------
enum CharacterType
{
    kCharClean = 0,
    kCharWarm,
    kCharTape,
    kCharTube,
    kCharTransformer,
    kCharDrive,
    kCharCustom,
    kNumCharacterTypes
};

inline const char* characterTypeName (int type) noexcept
{
    static const char* const names[kNumCharacterTypes] =
        { "Clean", "Warm", "Tape", "Tube", "Transformer", "Drive", "Custom" };
    return names[type < 0 ? 0 : (type >= kNumCharacterTypes ? kNumCharacterTypes - 1 : type)];
}

//------------------------------------------------------------------------------
//  Character Designer kontrolleri
//------------------------------------------------------------------------------
enum CustomParam
{
    // HARMONIC
    kCpHarmAmount = 0,     // uretilen harmoniklerin toplam seviyesi
    kCpEven,               // cift harmonikler (2., 4.)
    kCpOdd,                // tek harmonikler (3., 5.)
    kCpHarmBalance,        // 0 = dusuk dereceler (2./3.)  ...  100 = yuksek dereceler (4./5.)
    // SATURATION
    kCpSaturation,         // doygunlugun basladigi seviye (yuksek = daha erken, daha cok)
    kCpDrive,              // doyurucuya giris itisi (0 .. +24 dB, cikis telafili)
    kCpHardness,           // 0 = yumusak diz ... 100 = sert kirpma
    kCpSatMix,             // doyurucunun kuru / islak karisimi
    // TRANSIENT
    kCpTransient,          // -100 = vuruslari yumusat ... +100 = vuruslari one cikar
    kCpAttackChar,         // vurus penceresi: 0 = kisa / "tik" ... 100 = uzun / "govde"
    kCpRounding,           // vurus tepelerini (zarfa gore) yuvarlayan doyurma
    // DENSITY
    kCpDensity,            // dusuk seviyeli ayrintiyi yukari tasir (yukari kompresyon)
    kCpThickness,          // alt bandin harmonikleri (govde kalinligi)
    // CHARACTER TONE
    kCpLowChar,            // -100 = baslar temiz kalir ... +100 = baslar daha cok doyar
    kCpHighChar,           // -100 = tizler temiz kalir ... +100 = tizler daha cok doyar
    kNumCustomParams
};

struct CustomParamInfo
{
    const char* id;        // parametre kimliginin soneki (degismez)
    const char* name;      // gorunen ad
    const char* shortName; // arayuzdeki kisa etiket
    float minValue, maxValue, defaultValue;      // yuzde biriminde
};

inline const CustomParamInfo& customParamInfo (int index) noexcept
{
    static const CustomParamInfo table[kNumCustomParams] =
    {
        { "harm",   "Harmonic Amount",     "AMOUNT",    0.0f,   100.0f,  30.0f },
        { "even",   "Even Harmonics",      "EVEN",      0.0f,   100.0f,  60.0f },
        { "odd",    "Odd Harmonics",       "ODD",       0.0f,   100.0f,  40.0f },
        { "hbal",   "Harmonic Balance",    "BALANCE",   0.0f,   100.0f,  20.0f },
        { "sat",    "Saturation",          "SATURATE",  0.0f,   100.0f,  40.0f },
        { "drive",  "Drive",               "DRIVE",     0.0f,   100.0f,  30.0f },
        { "shape",  "Soft / Hard",         "SOFT/HARD", 0.0f,   100.0f,  25.0f },
        { "mix",    "Saturation Mix",      "MIX",       0.0f,   100.0f, 100.0f },
        { "trans",  "Transient",           "TRANSIENT", -100.0f, 100.0f,  0.0f },
        { "tatk",   "Attack Character",    "ATTACK",    0.0f,   100.0f,  50.0f },
        { "tround", "Transient Rounding",  "ROUNDING",  0.0f,   100.0f,   0.0f },
        { "dens",   "Density",             "DENSITY",   0.0f,   100.0f,  20.0f },
        { "thick",  "Harmonic Thickness",  "THICKNESS", 0.0f,   100.0f,   0.0f },
        { "low",    "Low Character",       "LOW",       -100.0f, 100.0f,  0.0f },
        { "high",   "High Character",      "HIGH",      -100.0f, 100.0f,  0.0f },
    };
    return table[index < 0 ? 0 : (index >= kNumCustomParams ? kNumCustomParams - 1 : index)];
}

//------------------------------------------------------------------------------
//  Bir karakter: tip + Designer'in 15 degeri (yuzde biriminde)
//------------------------------------------------------------------------------
struct CharacterSpec
{
    int   type = kCharWarm;
    float custom[kNumCustomParams];

    CharacterSpec() noexcept
    {
        for (int i = 0; i < kNumCustomParams; ++i)
            custom[i] = customParamInfo (i).defaultValue;
    }

    bool operator== (const CharacterSpec& o) const noexcept
    {
        if (type != o.type) return false;
        for (int i = 0; i < kNumCustomParams; ++i)
            if (custom[i] < o.custom[i] || custom[i] > o.custom[i]) return false;
        return true;
    }
    bool operator!= (const CharacterSpec& o) const noexcept { return ! (*this == o); }
};

struct CharacterSettings
{
    bool  on         = false;
    float intensity  = 0.5f;     // CHARACTER knob'u, 0 .. 1
    bool  levelMatch = false;    // karakter cikisini girisiyle ayni yukseklige getir
    int   slot       = 0;        // 0 = A, 1 = B
    CharacterSpec spec[2];
};

} // namespace scc

#endif // SCC_CHARACTER_TYPES_H_INCLUDED
