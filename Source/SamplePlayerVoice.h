#pragma once

#include <JuceHeader.h>
#include "SampleSource.h"
#include "GrainCloud.h"
#include "StretchStage.h"
#include "Modulation.h"

namespace romp
{
    enum class SustainMode
    {
        loop = 0,       // wrap at the loop end
        crossfade = 1,  // pre-roll crossfade across the loop seam
        granular = 2    // windowed grain cloud
    };

    enum class PitchMode
    {
        chromatic = 0,     // classic repitch: playback rate follows the note
        timePreserve = 1   // loop/crossfade via StretchStage; granular natively
    };

    enum class GenMode
    {
        off = 0, random = 1, cycle = 2
    };

    // One polyphonic voice: sample playback (loop / crossfade / granular /
    // time-preserving), amp + filter envelopes, two per-voice LFOs, a small mod
    // matrix, and a per-voice state-variable filter.
    //
    // Audio-thread contract: renderNextBlock allocates and locks nothing; all
    // scratch is sized in prepare().
    class SamplePlayerVoice : public juce::SynthesiserVoice
    {
    public:
        SamplePlayerVoice();

        void prepare (double sampleRate, int maxBlockSize, int numChannels);

        // Raw pointers published by the processor's atomic slots; safe because
        // every published object is pinned until no voice can reference it.
        void setSource (const SampleSource* newSource) { pendingSource = newSource; }
        void setPool (const SamplePoolView* newPool)   { pendingPool = newPool; }
        void setGenerativeMode (GenMode m, float probability)
        {
            genMode = m;
            genProbability = juce::jlimit (0.0f, 1.0f, probability);
        }

        // --- parameter snapshots, pushed from the processor -------------------
        void setAdsrParameters (const juce::ADSR::Parameters& p) { adsrParams = p; }
        void setFilterEnvParameters (const juce::ADSR::Parameters& p) { filterAdsrParams = p; }
        void setSustainMode (SustainMode m)                      { sustainMode = m; }
        void setPitchMode (PitchMode m)                          { pitchMode = m; }
        void setLoopPoints (double startNormalised, double endNormalised);
        void setCrossfade (double fractionOfLoop);
        void setCrossfadeCurve (int curve)                       { crossfadeCurve = juce::jlimit (0, 1, curve); }
        void setLoopEnabled (bool enabled)                       { loopEnabled = enabled; }
        void setGranularParams (const GrainCloudParams& p)       { grainParams = p; }
        void setLevel (float newLevel)                           { level = newLevel; }
        void setFilterSettings (const FilterSettings& f)        { filterSettings = f; }
        void setLfoSettings (int index, const LfoSettings& l);
        void setModMatrix (const ModMatrix& m)                   { modMatrix = m; }
        void setModWheel (float v)                               { modWheel = juce::jlimit (0.0f, 1.0f, v); }
        void setPitchBendRange (int semitones)                   { bendRangeSemitones = juce::jlimit (1, 24, semitones); }
        int getStretchLatencySamples() const noexcept            { return stretch.getLatencySamples(); }
        bool isUsingStretch() const noexcept;

        void requestFastRelease();
        float getActivityLevel() const noexcept { return envelopeLevel; }

        bool canPlaySound (juce::SynthesiserSound* sound) override;
        void startNote (int midiNoteNumber, float velocity,
                        juce::SynthesiserSound* sound,
                        int currentPitchWheelPosition) override;
        void stopNote (float velocity, bool allowTailOff) override;
        void pitchWheelMoved (int newValue) override;
        void controllerMoved (int controllerNumber, int newValue) override;
        void renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                              int startSample, int numSamples) override;

    private:
        float readLooped (int channel, double pos, double rate) const noexcept;
        void  computeBaseReadRate();
        void  resolveLoopForSource();
        SampleSourcePtr pickSource();
        float voiceGain() const noexcept { return level * velocityGain * expressionGain; }

        void  updateModulation (int chunkSamples);
        bool  generateChunk (juce::AudioBuffer<float>& scratch, int numSamples);

        SampleSourcePtr source;
        const SampleSource* pendingSource = nullptr;
        const SamplePoolView* pendingPool = nullptr;
        GenMode genMode = GenMode::off;
        float genProbability = 0.5f;
        int cycleIndex = 0;
        juce::Random random;

        juce::ADSR adsr;
        juce::ADSR::Parameters adsrParams;
        juce::ADSR filterAdsr;
        juce::ADSR::Parameters filterAdsrParams;

        SustainMode sustainMode = SustainMode::loop;
        PitchMode pitchMode = PitchMode::chromatic;
        GrainCloudParams grainParams;

        GrainCloud cloud;
        StretchStage stretch;
        juce::dsp::StateVariableTPTFilter<float> vcf;

        FilterSettings filterSettings;
        LfoSettings lfo[2];
        ModMatrix modMatrix;
        float modWheel = 0.0f;
        double lfoPhase[2] { 0.0, 0.0 };

        // Per-chunk modulation results.
        double modCutoffOct = 0.0;
        double modResonance = 0.0;
        double modPitchSemis = 0.0;
        float  modLevel = 1.0f;
        double modGrainSize = 1.0;
        double modGrainDensity = 1.0;

        double position = 0.0;
        double baseReadRate = 1.0;   // includes sample-rate and pitch ratio
        double readRate = 1.0;       // baseReadRate * pitch modulation
        double pitchRatio = 1.0;
        double currentMipRatio = 1.0;

        double loopStartNorm = 0.0;
        double loopEndNorm = 1.0;
        double crossfadeNorm = 0.02;
        int crossfadeCurve = 0;

        double loopStart = 0.0;
        double loopEnd = 0.0;
        double crossfadeLen = 0.0;
        bool loopEnabled = false;

        int currentMidiNote = 60;
        int bendRangeSemitones = 2;
        double bendSemitones = 0.0;

        float velocityGain = 1.0f;
        float expressionGain = 1.0f;
        float level = 1.0f;

        int declickSamples = 0;
        bool granularReleasing = false;
        int stretchEndCounter = 0;
        float envelopeLevel = 0.0f;

        juce::AudioBuffer<float> stretchIn, stretchOut, voiceScratch;
        std::vector<float> envScratch;
        int maxBlock = 512;
        int configuredChannels = 2;

        static constexpr int kControlBlock = 32;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplePlayerVoice)
    };
}
