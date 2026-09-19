#pragma once

#include <JuceHeader.h>
#include <memory>
#include <vector>

namespace romp
{
    // Immutable decoded sample. Owned by shared_ptr and published to the audio
    // thread via RomplerAudioProcessor. Decoded and mipmapped on the message
    // thread only.
    //
    // enable_shared_from_this lets a voice grab its own owning reference from a
    // bare atomic pointer the processor publishes, without the audio thread ever
    // touching std::atomic_load(&shared_ptr) (a global spinlock on MSVC).
    struct SampleSource : public std::enable_shared_from_this<SampleSource>
    {
        juce::AudioBuffer<float> buffer;
        double sampleRate = 44100.0;
        juce::String name;
        juce::String sourcePath;
        int rootNote = 60;

        // Band-limited decimations of `buffer`: mipLevels[k-1] is decimated by
        // 2^k and lowpassed to half the bandwidth of the previous level. Level 0
        // is `buffer` itself. Used to avoid aliasing when transposing upward.
        std::vector<juce::AudioBuffer<float>> mipLevels;

        int numSamples() const noexcept { return buffer.getNumSamples(); }
        int numChannels() const noexcept { return buffer.getNumChannels(); }
        int numLevels() const noexcept  { return 1 + (int) mipLevels.size(); }

        const juce::AudioBuffer<float>& levelBuffer (int level) const noexcept
        {
            if (level <= 0 || level > (int) mipLevels.size())
                return buffer;

            return mipLevels[(size_t) (level - 1)];
        }
    };

    using SampleSourcePtr = std::shared_ptr<const SampleSource>;

    // Immutable snapshot of the decoded sample folder, used for generative
    // per-note variation. Published to voices the same way as an individual
    // SampleSource (atomic raw pointer, pinned on the message thread).
    struct SamplePoolView : public std::enable_shared_from_this<SamplePoolView>
    {
        std::vector<SampleSourcePtr> sources;
    };

    using SamplePoolPtr = std::shared_ptr<const SamplePoolView>;

    // Builds band-limited mip levels for `source` in place, up to maxExtraLevels
    // beyond the original. Cheap levels are skipped for very long files.
    void buildMipmaps (SampleSource& source, int maxExtraLevels = 5);

    // Random-access, band-limited sample reader over a SampleSource. `position`
    // is always in original-sample coordinates. `readRate` is the ACTUAL number
    // of source frames consumed per output frame
    // (noteRatio * sourceSampleRate / hostSampleRate), which is what determines
    // the required band limit -- using only the note ratio aliases whenever the
    // sample rate differs from the host's.
    struct SampleReader
    {
        static float read (const SampleSource& source, int channel,
                           double position, double readRate) noexcept;

        // Level index needed to keep content below the output Nyquist for an
        // actual read rate. Alias-free by construction (ceils).
        static int levelForRate (double readRate) noexcept;

    private:
        static float catmullRomAtLevel (const SampleSource& source, int level,
                                        int channel, double position) noexcept;
    };

    // A single catch-all sound. The companion voice picks which SampleSource to
    // read, so a SynthesiserSound per file is unnecessary.
    //
    // Note: this is NOT juce::SamplerSound. JUCE's built-in SamplerSound/Voice
    // have no loop points, no start offset and no crossfade, and interpolate
    // linearly, so they are unusable as a foundation for this instrument.
    struct RomplerSound : public juce::SynthesiserSound
    {
        bool appliesToNote (int) override { return true; }
        bool appliesToChannel (int) override { return true; }
    };
}
