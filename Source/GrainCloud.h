#pragma once

#include <JuceHeader.h>
#include "SampleSource.h"

namespace romp
{
    struct GrainCloudParams
    {
        double grainSizeMs = 80.0;
        double density = 20.0;   // grains per second
        double spray = 0.2;      // 0..1, random read-head offset
        double spread = 0.5;     // 0..1, stereo scatter
        double drift = 0.0;      // 0..1, per-grain random pitch
        int window = 0;          // 0 = Hann, 1 = Tukey
        bool timePreserve = true;
    };

    // Windowed-grain cloud over a SampleSource. Preallocated grain pool; the
    // audio thread never allocates or locks. Reads via SampleReader, so grains
    // are band-limited on pitched playback too.
    //
    // Time model (the settled decision): the read head advances at original
    // speed when timePreserve is on (pitch comes from each grain's rate), and at
    // the pitch ratio otherwise. This is why granular needs no separate
    // time-stretch stage.
    class GrainCloud
    {
    public:
        GrainCloud() = default;

        static constexpr int kMaxGrains = 192;
        static constexpr int kWindowTableSize = 4096;

        void prepare (double hostSampleRate);

        void start (const SampleSourcePtr& source, double pitchRatio,
                    double basePosition, double loopStart, double loopEnd,
                    bool looping, bool timePreserve);

        void reset();

        // Stops spawning new grains; existing grains finish their windows. Used
        // for click-free note-off: the cloud then drains to silence naturally.
        void release() noexcept { spawning = false; }
        bool hasActiveGrains() const noexcept;

        void setParams (const GrainCloudParams& newParams);
        const GrainCloudParams& getParams() const noexcept { return params; }

        // Mixes the cloud into `dest` (adds, does not replace), gated by the
        // amp envelope values in `env` and the master `level`.
        void render (juce::AudioBuffer<float>& dest, int startSample, int numSamples,
                     const float* env, float level);

    private:
        struct Grain
        {
            bool active = false;
            double sourcePos = 0.0;
            double readRate = 1.0;
            double pitchRatio = 1.0;
            int age = 0;
            int length = 1;
            float gain = 0.0f;
            float panL = 0.7071f;
            float panR = 0.7071f;
        };

        void spawnGrain();
        float windowAt (int age, int length) const noexcept;

        Grain grains[kMaxGrains];

        SampleSourcePtr source;
        GrainCloudParams params;

        double hostRate = 44100.0;
        double srRatio = 1.0;
        double pitchRatio = 1.0;
        double headRate = 1.0;
        double readHead = 0.0;
        double samplesUntilNextGrain = 0.0;

        double loopStart = 0.0;
        double loopEnd = 0.0;
        bool looping = false;
        bool spawning = true;
        double cloudGain = 1.0;

        juce::Random random;
        std::array<float, kWindowTableSize> hannTable {};
        std::array<float, kWindowTableSize> tukeyTable {};
        bool prepared = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrainCloud)
    };
}
