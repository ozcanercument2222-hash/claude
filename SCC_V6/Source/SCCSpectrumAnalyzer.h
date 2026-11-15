//==============================================================================
//  SCCSpectrumAnalyzer.h  -  Gercek zamanli spektrum analizi (JUCE'dan bagimsiz)
//
//  Is bolumu:
//    * SES is parcacigi yalnizca ornekleri kilitsiz bir halka tampona yazar
//      (ornek basina bir atomik yazma; FFT, bellek ayirma, kilit YOK).
//    * FFT ve butun analiz ARAYUZ is parcaciginda yapilir.
//
//  Motor (SCCEngine) iki tampon tutar: karakter motorunun GIRISI ve CIKISI.
//  Aralarindaki fark motorun urettigi harmonik iceriktir.
//==============================================================================
#ifndef SCC_SPECTRUM_ANALYZER_H_INCLUDED
#define SCC_SPECTRUM_ANALYZER_H_INCLUDED

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace scc
{

//------------------------------------------------------------------------------
//  Tek yazan / tek okuyan halka tampon (mono)
//------------------------------------------------------------------------------
class AnalyzerFifo
{
public:
    static constexpr int kSize = 1 << 14;       // 16384 ornek (2'nin kuvveti)

    AnalyzerFifo() { clear(); }

    void clear() noexcept
    {
        for (auto& s : buffer) s.store (0.0f, std::memory_order_relaxed);
        head.store (0, std::memory_order_relaxed);
    }

    // ses is parcacigi: kanallarin ortalamasini yazar
    void push (const float* const* x, int numCh, int n) noexcept
    {
        std::uint32_t h = head.load (std::memory_order_relaxed);
        if (numCh > 1)
        {
            for (int i = 0; i < n; ++i)
                buffer[(h + (std::uint32_t) i) & kMask].store (0.5f * (x[0][i] + x[1][i]), std::memory_order_relaxed);
        }
        else
        {
            for (int i = 0; i < n; ++i)
                buffer[(h + (std::uint32_t) i) & kMask].store (x[0][i], std::memory_order_relaxed);
        }
        head.store (h + (std::uint32_t) n, std::memory_order_release);
    }

    // arayuz is parcacigi: en yeni n ornegi (eskiden yeniye) kopyalar
    void readLatest (float* dest, int n) const noexcept
    {
        n = std::min (n, kSize);
        const std::uint32_t h = head.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i)
            dest[i] = buffer[(h - (std::uint32_t) (n - i)) & kMask].load (std::memory_order_relaxed);
    }

    std::uint32_t writeCount() const noexcept { return head.load (std::memory_order_acquire); }

private:
    static constexpr std::uint32_t kMask = (std::uint32_t) kSize - 1u;
    std::atomic<float> buffer[kSize];
    std::atomic<std::uint32_t> head { 0 };
};

//------------------------------------------------------------------------------
//  Yerinde, radix-2 karmasik FFT (arayuz is parcacigi icin; n 2'nin kuvveti)
//------------------------------------------------------------------------------
inline void fftInPlace (float* re, float* im, int n) noexcept
{
    // bit cevirme
    for (int i = 1, j = 0; i < n; ++i)
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
    }

    for (int len = 2; len <= n; len <<= 1)
    {
        // donme carpanlari cift hassasiyetle ilerletilir (tek hassasiyette biriken hata
        // spektrumda ~-80 dB'de sahte "ayna" cizgileri uretir)
        const double ang = -6.28318530717958647692 / (double) len;
        const double wRe = std::cos (ang);
        const double wIm = std::sin (ang);
        const int half = len >> 1;
        for (int i = 0; i < n; i += len)
        {
            double cRe = 1.0, cIm = 0.0;
            for (int k = 0; k < half; ++k)
            {
                const int a = i + k, b = a + half;
                const float fRe = (float) cRe, fIm = (float) cIm;
                const float tRe = re[b] * fRe - im[b] * fIm;
                const float tIm = re[b] * fIm + im[b] * fRe;
                re[b] = re[a] - tRe;  im[b] = im[a] - tIm;
                re[a] += tRe;         im[a] += tIm;
                const double nRe = cRe * wRe - cIm * wIm;
                cIm = cRe * wIm + cIm * wRe;
                cRe = nRe;
            }
        }
    }
}

//------------------------------------------------------------------------------
class SpectrumAnalyzer
{
public:
    static constexpr int kFftSize = 4096;
    static constexpr int kNumBins = kFftSize / 2;

    SpectrumAnalyzer()
    {
        // Hann penceresi; tam olcekli bir sinus 0 dB okunacak sekilde olceklenir
        for (int i = 0; i < kFftSize; ++i)
            window[i] = (float) (0.5 - 0.5 * std::cos (6.28318530717958647692 * (double) i / (double) kFftSize));
    }

    //--------------------------------------------------------------------------
    //  Arayuz is parcacigi. magsDb: kNumBins eleman (bin k -> k * fs / kFftSize Hz).
    //  Tam olcekli sinus = 0 dBFS.
    //--------------------------------------------------------------------------
    void compute (const AnalyzerFifo& fifo, float* magsDb) noexcept
    {
        fifo.readLatest (re, kFftSize);
        for (int i = 0; i < kFftSize; ++i)
        {
            re[i] *= window[i];
            im[i] = 0.0f;
        }
        fftInPlace (re, im, kFftSize);

        const float scale = 4.0f / (float) kFftSize;        // Hann (0.5) ve tek tarafli spektrum (2)
        for (int k = 0; k < kNumBins; ++k)
        {
            const float mag = std::sqrt (re[k] * re[k] + im[k] * im[k]) * scale;
            magsDb[k] = 20.0f * std::log10 (std::max (mag, 1.0e-7f));
        }
    }

private:
    float window[kFftSize];
    float re[kFftSize] = {};
    float im[kFftSize] = {};
};

} // namespace scc

#endif // SCC_SPECTRUM_ANALYZER_H_INCLUDED
