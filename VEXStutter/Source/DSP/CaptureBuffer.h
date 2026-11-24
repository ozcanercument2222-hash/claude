#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vex::dsp
{
/** Rolling capture of the DRY input (specification section 3), with Freeze.

    Two banks of `capacity` frames. The live bank is written continuously; Freeze makes the live bank
    the frozen one and live writing continues in the other bank, so freezing never copies audio and
    never stops the live timeline. Samples are addressed by an absolute frame index (frames since
    reset); every read is clamped to the frames a bank really holds, anything else reads as silence.

    Threads and ownership
      - prepare() / reset(): while audio is stopped (prepareToPlay, releaseResources).
      - write / read / freeze / unfreeze: the audio thread only (the StutterEngine).
      - The UI never touches the samples. It reads the peak columns (std::atomic) through
        columnMin / columnMax / liveColumnsWritten / frozen*: tearing between a min and a max is
        harmless for a display, nothing can block or crash. The column arrays are allocated once in
        the constructor and never resized, so the UI may read them at any time.
    The plug-in's own output is never written here: no feedback into the capture (section 11).
*/
class CaptureBuffer
{
public:
    static constexpr double kSeconds = 8.0;              // capture length per bank
    static constexpr int kColumnsPerSecond = 200;        // waveform resolution for the UI (5 ms per column)
    static constexpr int kColumns = (int) (kSeconds * kColumnsPerSecond) + 8;

    CaptureBuffer()
    {
        for (auto& b : banks)
        {
            b.colMin = std::vector<std::atomic<float>> (kColumns);
            b.colMax = std::vector<std::atomic<float>> (kColumns);
        }
    }

    /** Allocates the sample storage for this sample rate (never call from the audio thread). */
    void prepare (double sampleRate)
    {
        rate = sampleRate > 0.0 ? sampleRate : 44100.0;
        cap = (int64_t) std::ceil (kSeconds * rate);
        framesPerColumn = std::max<int64_t> (1, (int64_t) std::llround (rate / kColumnsPerSecond));
        framesPerColumnUi.store (framesPerColumn, std::memory_order_relaxed);
        for (auto& b : banks)
            for (auto& ch : b.samples)
                ch.assign ((size_t) cap, 0.0f);
        reset();
    }

    void reset()
    {
        for (auto& b : banks)
        {
            for (auto& ch : b.samples)
                std::fill (ch.begin(), ch.end(), 0.0f);
            for (int i = 0; i < kColumns; ++i)
            {
                b.colMin[(size_t) i].store (0.0f, std::memory_order_relaxed);
                b.colMax[(size_t) i].store (0.0f, std::memory_order_relaxed);
            }
            b.startAbs = 0;
            b.endAbs = 0;
        }
        live = 0;
        frozenBank = -1;
        writeAbs = 0;
        liveBankUi.store (0);
        switchColumnUi.store (0);
        accMin = accMax = 0.0f;
        accCount = 0;
        liveColumns.store (0);
        frozenFlag.store (false);
        frozenColumnStart.store (0);
        frozenColumnEnd.store (0);
    }

    int64_t capacity() const noexcept { return cap; }
    int64_t written() const noexcept  { return writeAbs; }        // absolute index of the next frame
    double sampleRate() const noexcept { return rate; }

    //------------------------------------------------------------------ live --
    void write (float left, float right) noexcept
    {
        auto& b = banks[live];
        const auto pos = (size_t) (writeAbs % cap);
        b.samples[0][pos] = left;
        b.samples[1][pos] = right;
        ++writeAbs;
        b.endAbs = writeAbs;
        if (b.endAbs - b.startAbs > cap)
            b.startAbs = b.endAbs - cap;

        const float peakHi = std::max (left, right), peakLo = std::min (left, right);
        if (accCount == 0) { accMin = peakLo; accMax = peakHi; }
        else { accMin = std::min (accMin, peakLo); accMax = std::max (accMax, peakHi); }
        if (++accCount >= framesPerColumn)
        {
            const int64_t column = (writeAbs - 1) / framesPerColumn;
            const auto c = (size_t) (column % kColumns);
            b.colMin[c].store (accMin, std::memory_order_relaxed);
            b.colMax[c].store (accMax, std::memory_order_relaxed);
            liveColumns.store (column + 1, std::memory_order_release);
            accCount = 0;
        }
    }

    /** A frame of the live timeline (the live bank, or the frozen bank for frames written before the
        freeze); frames that are not held anywhere read as 0. */
    float readLive (int channel, int64_t abs) const noexcept
    {
        const auto& b = banks[live];
        if (abs >= b.startAbs && abs < b.endAbs)
            return b.samples[(size_t) (channel & 1)][(size_t) (abs % cap)];
        if (frozenBank >= 0)
        {
            const auto& f = banks[frozenBank];
            if (abs >= f.startAbs && abs < f.endAbs)
                return f.samples[(size_t) (channel & 1)][(size_t) (abs % cap)];
        }
        return 0.0f;
    }

    /** Oldest absolute frame still readable on the live timeline. */
    int64_t oldestLive() const noexcept
    {
        int64_t oldest = banks[live].startAbs;
        if (frozenBank >= 0 && banks[frozenBank].endAbs == banks[live].startAbs)
            oldest = std::min (oldest, banks[frozenBank].startAbs);
        return oldest;
    }

    //---------------------------------------------------------------- freeze --
    bool isFrozen() const noexcept { return frozenBank >= 0; }

    void freeze() noexcept
    {
        if (frozenBank >= 0)
            return;
        frozenBank = live;
        live = 1 - live;
        auto& nb = banks[live];
        nb.startAbs = nb.endAbs = writeAbs;      // the new live bank continues the same timeline
        switchColumnUi.store (writeAbs / framesPerColumn, std::memory_order_relaxed);
        liveBankUi.store (live, std::memory_order_relaxed);
        const auto& f = banks[frozenBank];
        frozenColumnStart.store ((f.startAbs + framesPerColumn - 1) / framesPerColumn, std::memory_order_relaxed);
        frozenColumnEnd.store (f.endAbs / framesPerColumn, std::memory_order_relaxed);
        frozenUiBank.store (frozenBank, std::memory_order_relaxed);
        frozenFlag.store (true, std::memory_order_release);
    }

    void unfreeze() noexcept
    {
        if (frozenBank < 0)
            return;
        frozenFlag.store (false, std::memory_order_release);
        frozenBank = -1;
    }

    /** Frames held by the frozen bank (0 when not frozen). */
    int64_t frozenLength() const noexcept { return frozenBank >= 0 ? banks[frozenBank].endAbs - banks[frozenBank].startAbs : 0; }

    /** Frame `index` (0 = oldest) of the frozen capture, clamped to it. */
    float readFrozen (int channel, int64_t index) const noexcept
    {
        if (frozenBank < 0)
            return 0.0f;
        const auto& f = banks[frozenBank];
        const auto len = f.endAbs - f.startAbs;
        if (len <= 0)
            return 0.0f;
        index = std::clamp<int64_t> (index, 0, len - 1);
        return f.samples[(size_t) (channel & 1)][(size_t) ((f.startAbs + index) % cap)];
    }

    //------------------------------------------------------------- UI (any thread) --
    /** Completed columns of the live timeline (the newest is liveColumnsWritten() - 1). */
    int64_t liveColumnsWritten() const noexcept { return liveColumns.load (std::memory_order_acquire); }
    /** Which bank holds the live columns from the UI's point of view, and the frozen range. */
    bool uiFrozen() const noexcept { return frozenFlag.load (std::memory_order_acquire); }
    int64_t uiFrozenColumnStart() const noexcept { return frozenColumnStart.load (std::memory_order_relaxed); }
    int64_t uiFrozenColumnEnd() const noexcept   { return frozenColumnEnd.load (std::memory_order_relaxed); }
    /** First column of the current live bank: a freeze now would capture from here (at most kSeconds). */
    int64_t uiLiveBankStartColumn() const noexcept { return switchColumnUi.load (std::memory_order_relaxed); }
    /** Frames per waveform column (converts the engine's frame positions to columns). */
    int64_t uiFramesPerColumn() const noexcept { return framesPerColumnUi.load (std::memory_order_relaxed); }

    /** Peak column of the live timeline (`frozen` = false) or of the frozen capture (`frozen` = true). */
    void column (bool frozen, int64_t columnIndex, float& lo, float& hi) const noexcept
    {
        const int bank = frozen ? frozenUiBank.load (std::memory_order_relaxed) : liveBankForUi (columnIndex);
        const auto c = (size_t) (((columnIndex % kColumns) + kColumns) % kColumns);
        lo = banks[bank].colMin[c].load (std::memory_order_relaxed);
        hi = banks[bank].colMax[c].load (std::memory_order_relaxed);
    }

private:
    struct Bank
    {
        std::vector<float> samples[2];
        std::vector<std::atomic<float>> colMin, colMax;
        int64_t startAbs = 0, endAbs = 0;       // absolute frames held: [startAbs, endAbs)
    };

    /** The live bank holds the columns from the last bank switch on; older ones are in the other bank. */
    int liveBankForUi (int64_t columnIndex) const noexcept
    {
        const int lb = liveBankUi.load (std::memory_order_relaxed);
        return columnIndex >= switchColumnUi.load (std::memory_order_relaxed) ? lb : 1 - lb;
    }

    Bank banks[2];
    double rate = 44100.0;
    int64_t cap = 1, framesPerColumn = 1;
    int live = 0, frozenBank = -1;
    int64_t writeAbs = 0;
    float accMin = 0.0f, accMax = 0.0f;
    int64_t accCount = 0;

    std::atomic<int64_t> liveColumns { 0 }, frozenColumnStart { 0 }, frozenColumnEnd { 0 };
    std::atomic<bool> frozenFlag { false };
    std::atomic<int> frozenUiBank { 0 }, liveBankUi { 0 };
    std::atomic<int64_t> switchColumnUi { 0 }, framesPerColumnUi { 1 };
};
}
