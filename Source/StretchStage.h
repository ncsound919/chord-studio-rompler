#pragma once

#include <JuceHeader.h>

// Vendored third-party headers are not warning-clean under MSVC /W4; keep their
// warnings contained here rather than polluting the project's build output.
#if defined(_MSC_VER)
 #pragma warning(push)
 #pragma warning(disable: 4244 4305 4456)
#endif
#include "signalsmith-stretch.h"
#if defined(_MSC_VER)
 #pragma warning(pop)
#endif

namespace romp
{
    // Time-preserving pitch shift for Loop / Crossfade sustain modes.
    //
    // Wraps signalsmith-stretch (MIT). Deliberately NOT used by granular mode:
    // granular is the live time-preserving path (pitch comes from grain rate and
    // the read head advances at original speed), so stacking a phase vocoder in
    // front of it would smear transients and double the CPU for no benefit.
    //
    // Latency is real and reported via getLatencySamples(); the default block
    // size here is chosen for a playable-ish ~80 ms rather than maximum quality.
    class StretchStage
    {
    public:
        void prepare (double sampleRate, int maxBlockSize, int numChannels);
        void reset();

        void setTransposeSemitones (float semitones) noexcept;
        float getTransposeSemitones() const noexcept { return transposeSemitones; }
        int getLatencySamples() const noexcept       { return latencySamples; }
        bool isActive() const noexcept               { return active; }

        // input and output must both have `numChannels()` channels and at least
        // `numSamples` frames. Same count in == time preserved, pitch transposed.
        void processBlock (const juce::AudioBuffer<float>& input,
                           juce::AudioBuffer<float>& output,
                           int numSamples);

    private:
        signalsmith::stretch::SignalsmithStretch<float> stretch;

        juce::AudioBuffer<float> warmIn, warmOut;

        double sr = 44100.0;
        int latencySamples = 0;
        float transposeSemitones = 0.0f;
        int channels = 2;
        bool active = false;
    };
}
