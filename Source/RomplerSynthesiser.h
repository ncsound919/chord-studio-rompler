#pragma once

#include <JuceHeader.h>
#include "SamplePlayerVoice.h"

namespace romp
{
    // Custom synthesiser:
    //  * Click-free stealing: when the active count reaches a soft limit, the
    //    least-active voice is fast-released so the new note lands on a spare
    //    voice, instead of JUCE's hard stopNote(0,false)->startNote on the same
    //    voice (which clicks).
    //  * CC cache: JUCE forwards controllers only to already-active voices, so a
    //    mod wheel moved before a note is otherwise lost to that note.
    class RomplerSynthesiser : public juce::Synthesiser
    {
    public:
        void setSoftLimit (int limit) noexcept { softLimit = juce::jmax (1, limit); }
        int getSoftLimit() const noexcept      { return softLimit; }

        int getCachedController (int channel, int cc) const noexcept
        {
            if (channel < 1 || channel > kChannels || cc < 0 || cc > 127)
                return 0;

            return cachedCC[channel - 1][cc];
        }

    protected:
        void noteOn (int midiChannel, int midiNoteNumber, float velocity) override
        {
            int active = 0;
            SamplePlayerVoice* victim = nullptr;
            float lowest = 2.0f;

            for (int i = 0; i < getNumVoices(); ++i)
            {
                auto* v = static_cast<SamplePlayerVoice*> (getVoice (i));

                if (v->isVoiceActive())
                {
                    ++active;
                    const float a = v->getActivityLevel();

                    if (a < lowest) { lowest = a; victim = v; }
                }
            }

            if (active >= softLimit && victim != nullptr)
                victim->requestFastRelease();

            juce::Synthesiser::noteOn (midiChannel, midiNoteNumber, velocity);
        }

        void handleController (int midiChannel, int controllerNumber, int controllerValue) override
        {
            if (midiChannel >= 1 && midiChannel <= kChannels
                && controllerNumber >= 0 && controllerNumber < 128)
                cachedCC[midiChannel - 1][controllerNumber] = controllerValue;

            juce::Synthesiser::handleController (midiChannel, controllerNumber, controllerValue);
        }

    private:
        static constexpr int kChannels = 16;
        int softLimit = 12;
        int cachedCC[kChannels][128] = {};
    };
}
