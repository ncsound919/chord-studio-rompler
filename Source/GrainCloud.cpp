#include "GrainCloud.h"

#include <array>
#include <cmath>

namespace romp
{
    void GrainCloud::prepare (double hostSampleRate)
    {
        hostRate = hostSampleRate > 0.0 ? hostSampleRate : 44100.0;

        for (int i = 0; i < kWindowTableSize; ++i)
        {
            const double p = (double) i / (double) (kWindowTableSize - 1);

            hannTable[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * p));

            const double alpha = 0.5;
            float t;

            if (p < alpha * 0.5)
                t = (float) (0.5 * (1.0 + std::cos (juce::MathConstants<double>::pi
                                                   * (2.0 * p / alpha - 1.0))));
            else if (p <= 1.0 - alpha * 0.5)
                t = 1.0f;
            else
                t = (float) (0.5 * (1.0 + std::cos (juce::MathConstants<double>::pi
                                                   * (2.0 * (p - 1.0) / alpha + 1.0))));

            tukeyTable[(size_t) i] = t;
        }

        prepared = true;
    }

    void GrainCloud::setParams (const GrainCloudParams& newParams)
    {
        params = newParams;
        params.grainSizeMs = juce::jlimit (5.0, 500.0, params.grainSizeMs);
        params.density = juce::jlimit (0.1, 400.0, params.density);
        params.spray = juce::jlimit (0.0, 1.0, params.spray);
        params.spread = juce::jlimit (0.0, 1.0, params.spread);
        params.drift = juce::jlimit (0.0, 1.0, params.drift);
    }

    void GrainCloud::reset()
    {
        for (auto& g : grains)
            g.active = false;
    }

    void GrainCloud::start (const SampleSourcePtr& newSource, double newPitchRatio,
                            double basePosition, double newLoopStart, double newLoopEnd,
                            bool newLooping, bool timePreserve)
    {
        source = newSource;
        pitchRatio = newPitchRatio;
        loopStart = newLoopStart;
        loopEnd = newLoopEnd;
        looping = newLooping && (loopEnd - loopStart) > 1.0;

        srRatio = (source != nullptr && source->sampleRate > 0.0)
                      ? source->sampleRate / hostRate
                      : 1.0;

        headRate = (timePreserve ? 1.0 : pitchRatio) * srRatio;
        readHead = basePosition;
        samplesUntilNextGrain = 0.0;
        spawning = true;
        cloudGain = 1.0;

        reset();
    }

    bool GrainCloud::hasActiveGrains() const noexcept
    {
        for (const auto& g : grains)
            if (g.active)
                return true;

        return false;
    }

    float GrainCloud::windowAt (int age, int length) const noexcept
    {
        if (length <= 1)
            return 1.0f;

        const int idx = juce::jlimit (0, kWindowTableSize - 1,
                                      (int) ((juce::int64) age * kWindowTableSize / length));

        return params.window == 1 ? tukeyTable[(size_t) idx] : hannTable[(size_t) idx];
    }

    void GrainCloud::spawnGrain()
    {
        if (source == nullptr || source->numSamples() <= 0)
            return;

        Grain* g = nullptr;

        for (auto& candidate : grains)
            if (! candidate.active) { g = &candidate; break; }

        if (g == nullptr)
        {
            int oldest = 0;

            for (int i = 1; i < kMaxGrains; ++i)
                if (grains[i].age > grains[oldest].age)
                    oldest = i;

            g = &grains[oldest];
        }

        const int len = source->numSamples();

        g->active = true;
        g->age = 0;
        g->length = juce::jmax (1, (int) (params.grainSizeMs * 0.001 * hostRate));

        const double driftSemis = params.drift > 0.0
                                      ? params.drift * (random.nextDouble() * 2.0 - 1.0)
                                      : 0.0;
        g->pitchRatio = pitchRatio * std::pow (2.0, driftSemis / 12.0);
        g->readRate = g->pitchRatio * srRatio;

        const double span = (looping ? (loopEnd - loopStart) : (double) len) * params.spray * 0.5;
        const double jitter = (random.nextDouble() * 2.0 - 1.0) * span;

        double p = readHead + jitter;

        if (looping)
        {
            const double loopLen = loopEnd - loopStart;
            p = loopStart + std::fmod (p - loopStart, loopLen);
            if (p < loopStart) p += loopLen;
        }

        g->sourcePos = juce::jlimit (0.0, (double) juce::jmax (0, len - 1), p);

        const double pan = (random.nextDouble() * 2.0 - 1.0) * params.spread;
        const double angle = (pan * 0.5 + 0.5) * juce::MathConstants<double>::halfPi;
        g->panL = (float) std::cos (angle);
        g->panR = (float) std::sin (angle);

        const double overlap = juce::jmax (1.0, params.grainSizeMs * 0.001 * params.density);
        g->gain = (float) (1.0 / std::sqrt (overlap));
    }

    void GrainCloud::render (juce::AudioBuffer<float>& dest, int startSample, int numSamples,
                             const float* env, float level)
    {
        if (! prepared || source == nullptr || source->numSamples() <= 0)
            return;

        const int destChannels = dest.getNumChannels();
        const int srcChannels = source->numChannels();
        const double interval = hostRate / juce::jmax (0.1, params.density);
        const double loopLen = loopEnd - loopStart;

        // Overlap gain normalisation, smoothed so density/size changes don't
        // zipper. 1/sqrt(N) is correct for the normally-incoherent grain cloud.
        const double targetGain = 1.0 / std::sqrt (juce::jmax (1.0, params.grainSizeMs * 0.001 * params.density));
        const double gainCoeff = 1.0 - std::exp (-1.0 / (0.03 * hostRate));

        for (int i = 0; i < numSamples; ++i)
        {
            if (spawning)
            {
                samplesUntilNextGrain -= 1.0;

                if (samplesUntilNextGrain <= 0.0)
                {
                    spawnGrain();
                    samplesUntilNextGrain += interval;
                }
            }

            cloudGain += (targetGain - cloudGain) * gainCoeff;
            const float cg = (float) cloudGain;
            const float e = env != nullptr ? env[i] : 1.0f;

            for (auto& g : grains)
            {
                if (! g.active)
                    continue;

                const float w = windowAt (g.age, g.length);

                for (int ch = 0; ch < destChannels; ++ch)
                {
                    const int srcCh = juce::jmin (ch, srcChannels - 1);
                    const float s = SampleReader::read (*source, srcCh, g.sourcePos, g.readRate);
                    const float pan = (ch == 0) ? g.panL : g.panR;

                    dest.addSample (ch, startSample + i, s * w * cg * pan * e * level);
                }

                g.sourcePos += g.readRate;
                ++g.age;

                if (g.age >= g.length)
                    g.active = false;
            }

            readHead += headRate;

            if (looping)
            {
                if (readHead >= loopEnd)
                    readHead = loopStart + std::fmod (readHead - loopStart, juce::jmax (1.0, loopLen));
            }
            else if (readHead >= (double) source->numSamples() - 1.0)
            {
                readHead = (double) source->numSamples() - 1.0;
                spawning = false;   // one-shot cloud: stop at the end, then drain
            }
        }
    }
}
