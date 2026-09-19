#include "SampleSource.h"

#include <cmath>

namespace romp
{
    namespace
    {
        // Normalised-cutoff windowed-sinc lowpass. `cutoff` is in cycles per
        // sample (0.25 == half of Nyquist), so the same kernel anti-aliases
        // every halving stage regardless of that stage's absolute rate.
        std::vector<float> makeLowpass (double cutoff, int order)
        {
            const int taps = order + 1;
            const int delay = order / 2;
            std::vector<float> h ((size_t) taps, 0.0f);

            double sum = 0.0;

            for (int m = 0; m < taps; ++m)
            {
                const double x = (double) (m - delay);
                const double sinc = (std::abs (x) < 1.0e-9)
                                        ? 1.0
                                        : std::sin (2.0 * juce::MathConstants<double>::pi * cutoff * x)
                                              / (2.0 * juce::MathConstants<double>::pi * cutoff * x);
                const double hann = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi
                                                          * (double) m / (double) (taps - 1));
                const double v = sinc * hann;
                h[(size_t) m] = (float) v;
                sum += v;
            }

            if (sum != 0.0)
                for (auto& v : h)
                    v = (float) ((double) v / sum);

            return h;
        }

        void decimate (const juce::AudioBuffer<float>& in, juce::AudioBuffer<float>& out,
                       const std::vector<float>& h, int factor)
        {
            const int taps = (int) h.size();
            const int delay = (taps - 1) / 2;
            const int inN = in.getNumSamples();
            const int channels = in.getNumChannels();
            const int outN = inN / factor;

            out.setSize (channels, outN);
            out.clear();

            for (int ch = 0; ch < channels; ++ch)
            {
                const float* x = in.getReadPointer (ch);
                float* y = out.getWritePointer (ch);

                for (int j = 0; j < outN; ++j)
                {
                    const int base = j * factor + delay;
                    const int mStart = juce::jmax (0, base - (inN - 1));
                    const int mEnd = juce::jmin (taps - 1, base);

                    float acc = 0.0f;

                    for (int m = mStart; m <= mEnd; ++m)
                        acc += h[(size_t) m] * x[base - m];

                    y[j] = acc;
                }
            }
        }
    }

    void buildMipmaps (SampleSource& source, int maxExtraLevels)
    {
        source.mipLevels.clear();

        const int n = source.numSamples();

        if (n <= 0 || source.numChannels() <= 0 || maxExtraLevels <= 0)
            return;

        // Generation is O(N * taps) per level. Very long files get fewer levels
        // rather than none, so upward transposition still stays alias-free.
        int levels = maxExtraLevels;

        if (n > 44100 * 60)
            levels = juce::jmin (levels, 2);
        else if (n > 44100 * 20)
            levels = juce::jmin (levels, 3);

        const auto kernel = makeLowpass (0.25, 32);

        for (int k = 1; k <= levels; ++k)
        {
            const auto& prev = source.levelBuffer (k - 1);

            if (prev.getNumSamples() < 64)
                break;

            juce::AudioBuffer<float> level;
            decimate (prev, level, kernel, 2);
            source.mipLevels.push_back (std::move (level));
        }
    }

    int SampleReader::levelForRate (double readRate) noexcept
    {
        if (readRate <= 1.0)
            return 0;

        // 2^k >= readRate keeps the level's band limit below the output Nyquist.
        // The epsilon stops an exact power of two from flickering a level up.
        return (int) std::ceil (std::log2 (readRate) - 1.0e-9);
    }

    float SampleReader::catmullRomAtLevel (const SampleSource& source, int level,
                                           int channel, double position) noexcept
    {
        const auto& buf = source.levelBuffer (level);
        const int len = buf.getNumSamples();

        if (len <= 0)
            return 0.0f;

        const int i1 = (int) std::floor (position);
        const float frac = (float) (position - (double) i1);

        auto at = [&] (int i) -> float
        {
            return (i < 0 || i >= len) ? 0.0f : buf.getSample (channel, i);
        };

        const float y0 = at (i1 - 1);
        const float y1 = at (i1);
        const float y2 = at (i1 + 1);
        const float y3 = at (i1 + 2);

        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

    float SampleReader::read (const SampleSource& source, int channel,
                              double position, double readRate) noexcept
    {
        const int maxLevel = source.numLevels() - 1;

        const int level = juce::jlimit (0, maxLevel, levelForRate (readRate));

        if (level == 0)
            return catmullRomAtLevel (source, 0, channel, position);

        return catmullRomAtLevel (source, level, channel,
                                  position / std::ldexp (1.0, level));
    }
}
