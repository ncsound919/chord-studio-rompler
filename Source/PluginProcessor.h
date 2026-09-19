#pragma once

#include <JuceHeader.h>
#include "SampleSource.h"
#include "RomplerSynthesiser.h"

namespace romp
{
    class SamplePlayerVoice;

    class RomplerAudioProcessor : public juce::AudioProcessor,
                                  private juce::Timer
    {
    public:
        RomplerAudioProcessor();
        ~RomplerAudioProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override                          { return true; }
        const juce::String getName() const override              { return JucePlugin_Name; }
        bool acceptsMidi() const override                        { return true; }
        bool producesMidi() const override                       { return false; }
        bool isMidiEffect() const override                       { return false; }
        double getTailLengthSeconds() const override             { return 6.0; }
        int getNumPrograms() override                            { return 1; }
        int getCurrentProgram() override                         { return 0; }
        void setCurrentProgram (int) override                    {}
        const juce::String getProgramName (int) override         { return {}; }
        void changeProgramName (int, const juce::String&) override {}
        void getStateInformation (juce::MemoryBlock&) override;
        void setStateInformation (const void*, int) override;

        juce::AudioProcessorValueTreeState apvts;

        // --- message-thread API used by the editor -----------------------------
        void loadFolder (const juce::File& folder);
        void loadSampleFile (const juce::File& file);
        void selectBrowserSample (int index);
        void stepBrowserSample (int delta);

        int getNumBrowserSamples() const                         { return folderFiles.size(); }
        juce::String getLoadedSampleName() const                 { return loadedSampleName; }
        juce::String getLastError() const                        { return lastError; }

        juce::var buildUiState() const;

        static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    private:
        void refreshFolder();
        void pushParamsToVoices();
        SampleSourcePtr decodeFile (const juce::File& file, juce::String& error);
        void publishSource (const SampleSourcePtr& newSource);
        void computePeaks (const SampleSource& source);
        void buildPool();

        void cacheParamPointers();
        void updateReportedLatency();

        void timerCallback() override;   // reclaims retired buffers, polls genMode

        // Resolved once; never do string-keyed lookups on the audio thread.
        struct ParamPtrs
        {
            std::atomic<float> *volume = nullptr, *attack = nullptr, *decay = nullptr,
                *sustain = nullptr, *release = nullptr, *loopStart = nullptr,
                *loopEnd = nullptr, *crossfade = nullptr, *crossfadeCurve = nullptr,
                *grainSize = nullptr, *density = nullptr, *spray = nullptr,
                *spread = nullptr, *grainDrift = nullptr, *grainWindow = nullptr,
                *genProbability = nullptr,
                *sustainMode = nullptr, *pitchMode = nullptr, *genMode = nullptr,
                *loopEnabled = nullptr,
                *filterType = nullptr, *filterCutoff = nullptr, *filterReso = nullptr,
                *filterDrive = nullptr, *filterEnvAmount = nullptr, *filterKeyTrack = nullptr,
                *filterAttack = nullptr, *filterDecay = nullptr, *filterSustain = nullptr,
                *filterRelease = nullptr,
                *lfo1Rate = nullptr, *lfo1Shape = nullptr, *lfo1Depth = nullptr,
                *lfo2Rate = nullptr, *lfo2Shape = nullptr, *lfo2Depth = nullptr;

            std::array<std::atomic<float>*, kNumModSlots> modSource {}, modDest {}, modAmount {};
        };

        ParamPtrs p;

        RomplerSynthesiser synth;
        std::array<SamplePlayerVoice*, 16> voiceArray {};        juce::AudioFormatManager formats;

        juce::dsp::Limiter<float> limiter;
        std::array<float, 2> dcX {}, dcY {};

        // Published to the audio thread as raw atomic pointers. The message
        // thread keeps a strong reference (the pin vectors below) until no voice
        // can still be reading, so a buffer is never freed on the audio thread.
        // Avoids std::atomic_load/store on shared_ptr, which is a global spinlock
        // on MSVC and a priority-inversion hazard.
        std::atomic<const SampleSource*> liveSource { nullptr };
        std::atomic<const SamplePoolView*> livePool { nullptr };

        struct PinnedSource { SampleSourcePtr ptr; double retiredAt = 0.0; };
        struct PinnedPool   { SamplePoolPtr ptr;   double retiredAt = 0.0; };

        SampleSourcePtr heldSource;               // current live strong ref
        SamplePoolPtr heldPool;
        std::vector<PinnedSource> retiredSources;
        std::vector<PinnedPool> retiredPools;

        static constexpr double kReclaimGraceMs = 2000.0;

        juce::File currentFolder;
        juce::Array<juce::File> folderFiles;
        int browserIndex = 0;
        juce::String loadedSampleName;
        juce::String lastError;
        int lastGenMode = -1;

        std::vector<float> peakMin, peakMax;
        static constexpr int kPeakBuckets = 256;
        static constexpr int kRetainedSources = 16;
        static constexpr int kGenPoolSize = 8;
        static constexpr int kMaxScanFiles = 2000;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RomplerAudioProcessor)
    };
}
