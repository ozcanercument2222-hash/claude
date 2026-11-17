#pragma once
//==============================================================================
//  Arayuz dili: Ingilizce, Turkce, Almanca, Fransizca, Italyanca, Rusca, Cince.
//
//  Dil, isletim sisteminin arayuz dilinden secilir (Windows: kullanicinin tercih
//  ettigi UI dili, macOS: tercih edilen dil); bunlardan biri degilse Ingilizce.
//  VSTM_TUNE_LANG ortam degiskeni (en, tr, de, fr, it, ru, zh) dili zorlar.
//
//  Ceviriler Lang.cpp'deki tek tabloda; anahtar Ingilizce metnin kendisidir
//  (ayni Ingilizce metnin iki anlami varsa ayri bir anahtar kullanilir).
//  Tabloda olmayan anahtar oldugu gibi (Ingilizce) doner.
//==============================================================================
#include <juce_core/juce_core.h>

namespace vstm::i18n
{
enum class Lang { english = 0, turkish, german, french, italian, russian, chinese };
constexpr int kNumLangs = 7;

/** "tr-TR", "zh_Hans_CN", "de" ... -> a supported language, English for anything else. */
Lang fromCode (const juce::String& code) noexcept;

/** VSTM_TUNE_LANG, else the OS display language, else the user locale; English if unsupported. */
Lang detect();

/** The language the UI uses (detected once, on first use). */
Lang current() noexcept;
void setCurrent (Lang) noexcept;               // tests

const char* code (Lang) noexcept;              // "en", "tr", ...

/** Text for key in the current language (English when the key or the translation is missing). */
juce::String tr (const char* key);
juce::String tr (const char* key, Lang);

/** tr() with "%1" replaced by arg. */
juce::String trf (const char* key, const juce::String& arg);

/** The key's text in every language (for parsing typed values in any of them). */
juce::StringArray allTexts (const char* key);

/** Upper case that respects Turkish dotted / dotless i when the UI is Turkish. */
juce::String upper (const juce::String&);

/** Number of table rows and whether every row has text in every language (tests). */
int numEntries() noexcept;
juce::String firstIncompleteEntry();
} // namespace vstm::i18n
