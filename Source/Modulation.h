#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>

namespace romp
{
    enum class FilterType { lowpass = 0, bandpass = 1, highpass = 2 };

    enum class ModSource
    {
        none = 0, lfo1, lfo2, filterEnv, ampEnv, velocity, modWheel, keyTrack
    };

    enum class ModDest
    {
        none = 0, cutoff, resonance, pitch, level, grainSize, grainDensity
    };

    struct ModSlot
    {
        ModSource source = ModSource::none;
        ModDest dest = ModDest::none;
        float amount = 0.0f;   // -1..1
    };

    static constexpr int kNumModSlots = 4;
    static constexpr int kNumModSources = 8;   // ModSource::none .. keyTrack
    using ModMatrix = std::array<ModSlot, kNumModSlots>;

    struct LfoSettings
    {
        double rateHz = 1.0;
        int shape = 0;       // 0 sine, 1 triangle, 2 saw, 3 square
        float depth = 0.0f;  // 0..1
    };

    struct FilterSettings
    {
        FilterType type = FilterType::lowpass;
        double cutoffHz = 18000.0;
        double resonance = 0.7;     // 0.1..10
        double drive = 0.0;         // 0..1, pre-filter tanh
        double envAmountOct = 0.0;  // bipolar octaves at full filter envelope
        double keyTrack = 0.0;      // 0..1
    };

    // LFO shape in [-1, 1] for a normalised phase.
    inline float lfoShapeValue (int shape, double phase01) noexcept
    {
        const double p = phase01 - std::floor (phase01);

        switch (shape)
        {
            case 1:  return (float) (p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p);   // triangle
            case 2:  return (float) (2.0 * p - 1.0);                             // saw
            case 3:  return p < 0.5 ? 1.0f : -1.0f;                              // square
            default: return (float) std::sin (2.0 * juce::MathConstants<double>::pi * p);
        }
    }
}
