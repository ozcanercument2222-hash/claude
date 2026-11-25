#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

namespace vstm
{
/** 1.5 (roadmap 47 / 48): MIDI CC -> parameter assignments.

    "MIDI Learn" on a control waits for the next controller message (any channel) and assigns its number to
    that parameter; from then on the CC moves it (0..127 -> the parameter's whole range). One CC per
    parameter, one parameter per CC. The assignments belong to the session (saved with it, not in presets).

    Threads: handle() runs on the audio thread; learn / forget / clear / state on the message thread. Every
    slot is a single atomic, nothing is locked or allocated on the audio thread. A parameter changed from the
    audio thread reaches the host through JUCE (VST3: the output parameter changes of the next block). */
class MidiLearn
{
public:
    static constexpr int kNumCC = 120;          // 120..127 are channel mode messages, never assigned

    explicit MidiLearn (juce::AudioProcessor& p) : processor (p)
    {
        for (auto& m : map)
            m.store (-1, std::memory_order_relaxed);
    }

    //---------------------------------------------------------------- audio thread
    /** Returns true when the message was a controller this class used (learnt or applied). */
    bool handle (const juce::MidiMessage& m) noexcept
    {
        if (! m.isController())
            return false;
        const int cc = m.getControllerNumber();
        if (cc < 0 || cc >= kNumCC)
            return false;
        int want = learning.load (std::memory_order_acquire);
        if (want >= 0 && learning.compare_exchange_strong (want, -1, std::memory_order_acq_rel))
        {
            for (auto& slot : map)
            {
                int expected = want;
                slot.compare_exchange_strong (expected, -1, std::memory_order_relaxed);
            }
            map[(size_t) cc].store (want, std::memory_order_release);
            changes.fetch_add (1, std::memory_order_release);
        }
        const int index = map[(size_t) cc].load (std::memory_order_acquire);
        auto& all = processor.getParameters();
        if (index < 0 || index >= all.size())
            return false;
        all[index]->setValueNotifyingHost ((float) m.getControllerValue() / 127.0f);
        return true;
    }

    //---------------------------------------------------------------- message thread
    /** Only automatable parameters can be learnt. */
    bool canLearn (const juce::AudioProcessorParameter& p) const { return p.isAutomatable() && p.getParameterIndex() >= 0; }

    void startLearning (const juce::AudioProcessorParameter& p)
    {
        if (canLearn (p))
        {
            learning.store (p.getParameterIndex(), std::memory_order_release);
            changes.fetch_add (1, std::memory_order_release);
        }
    }
    void cancelLearning()                         { learning.store (-1, std::memory_order_release); changes.fetch_add (1); }
    bool isLearning (const juce::AudioProcessorParameter& p) const
    {
        return learning.load (std::memory_order_acquire) == p.getParameterIndex();
    }
    bool isLearningAny() const                    { return learning.load (std::memory_order_acquire) >= 0; }

    /** The CC assigned to the parameter, -1 = none. */
    int ccFor (const juce::AudioProcessorParameter& p) const
    {
        for (int cc = 0; cc < kNumCC; ++cc)
            if (map[(size_t) cc].load (std::memory_order_acquire) == p.getParameterIndex())
                return cc;
        return -1;
    }
    void forget (const juce::AudioProcessorParameter& p)
    {
        for (auto& slot : map)
        {
            int expected = p.getParameterIndex();
            slot.compare_exchange_strong (expected, -1, std::memory_order_acq_rel);
        }
        changes.fetch_add (1, std::memory_order_release);
    }
    void clear()
    {
        for (auto& slot : map)
            slot.store (-1, std::memory_order_release);
        learning.store (-1, std::memory_order_release);
        changes.fetch_add (1, std::memory_order_release);
    }
    int numAssigned() const
    {
        int n = 0;
        for (auto& slot : map)
            n += slot.load (std::memory_order_acquire) >= 0 ? 1 : 0;
        return n;
    }
    /** Grows whenever an assignment or the learning state changes (the UI repaints on it). */
    uint32_t getChangeCount() const noexcept      { return changes.load (std::memory_order_acquire); }

    /** "21=retune,7=mix": CC number = parameter ID. */
    juce::String toString() const
    {
        juce::StringArray parts;
        auto& all = processor.getParameters();
        for (int cc = 0; cc < kNumCC; ++cc)
        {
            const int index = map[(size_t) cc].load (std::memory_order_acquire);
            if (index >= 0 && index < all.size())
                if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (all[index]))
                    parts.add (juce::String (cc) + "=" + withId->paramID);
        }
        return parts.joinIntoString (",");
    }

    /** Replaces every assignment. Damaged entries (unknown ID, CC out of range, not automatable, a
        parameter or CC given twice) are skipped. */
    void fromString (const juce::String& text)
    {
        for (auto& slot : map)
            slot.store (-1, std::memory_order_release);
        learning.store (-1, std::memory_order_release);
        auto& all = processor.getParameters();
        for (const auto& part : juce::StringArray::fromTokens (text, ",", ""))
        {
            const auto ccText = part.upToFirstOccurrenceOf ("=", false, false).trim();
            const auto id = part.fromFirstOccurrenceOf ("=", false, false).trim();
            if (ccText.isEmpty() || ! ccText.containsOnly ("0123456789") || ccText.length() > 3 || id.isEmpty())
                continue;
            const int cc = ccText.getIntValue();
            if (cc < 0 || cc >= kNumCC || map[(size_t) cc].load() >= 0)
                continue;
            for (auto* p : all)
                if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
                    if (withId->paramID == id && canLearn (*p) && ccFor (*p) < 0)
                    {
                        map[(size_t) cc].store (p->getParameterIndex(), std::memory_order_release);
                        break;
                    }
        }
        changes.fetch_add (1, std::memory_order_release);
    }

private:
    juce::AudioProcessor& processor;
    std::array<std::atomic<int>, kNumCC> map;
    std::atomic<int> learning { -1 };
    std::atomic<uint32_t> changes { 0 };
};
} // namespace vstm
