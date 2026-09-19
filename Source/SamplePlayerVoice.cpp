#include "SamplePlayerVoice.h"

#include <cmath>

namespace romp
{
    SamplePlayerVoice::SamplePlayerVoice()
    {
        adsr.setParameters (adsrParams);
        filterAdsr.setParameters (filterAdsrParams);
        vcf.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    }

    void SamplePlayerVoice::prepare (double sampleRate, int maxBlockSize, int numChannels)
    {
        maxBlock = juce::jmax (1, maxBlockSize);
        configuredChannels = juce::jlimit (1, 2, numChannels);

        cloud.prepare (sampleRate);
        stretch.prepare (sampleRate, maxBlock, configuredChannels);

        stretchIn.setSize (2, maxBlock);
        stretchOut.setSize (2, maxBlock);
        voiceScratch.setSize (juce::jmax (2, configuredChannels), maxBlock);
        envScratch.assign ((size_t) maxBlock, 0.0f);
        declickSamples = juce::jmax (1, (int) (0.005 * juce::jmax (1.0, sampleRate)));

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = (juce::uint32) maxBlock;
        spec.numChannels = (juce::uint32) juce::jmax (2, configuredChannels);
        vcf.prepare (spec);
        vcf.reset();
    }

    void SamplePlayerVoice::setLoopPoints (double startNormalised, double endNormalised)
    {
        loopStartNorm = juce::jlimit (0.0, 1.0, startNormalised);
        loopEndNorm   = juce::jlimit (0.0, 1.0, endNormalised);
    }

    void SamplePlayerVoice::setCrossfade (double fractionOfLoop)
    {
        crossfadeNorm = juce::jlimit (0.0, 0.5, fractionOfLoop);
    }

    void SamplePlayerVoice::setLfoSettings (int index, const LfoSettings& l)
    {
        if (index >= 0 && index < 2)
            lfo[index] = l;
    }

    bool SamplePlayerVoice::canPlaySound (juce::SynthesiserSound* sound)
    {
        return dynamic_cast<RomplerSound*> (sound) != nullptr;
    }

    bool SamplePlayerVoice::isUsingStretch() const noexcept
    {
        return sustainMode != SustainMode::granular
               && pitchMode == PitchMode::timePreserve
               && stretch.isActive();
    }

    void SamplePlayerVoice::computeBaseReadRate()
    {
        if (source == nullptr)
        {
            baseReadRate = 1.0;
            pitchRatio = 1.0;
            readRate = 1.0;
            return;
        }

        pitchRatio = std::pow (2.0, ((double) currentMidiNote - (double) source->rootNote
                                     + bendSemitones) / 12.0);

        const double hostRate = juce::jmax (1.0, getSampleRate());
        const double srRatio = (source->sampleRate > 0.0 ? source->sampleRate : hostRate) / hostRate;

        baseReadRate = isUsingStretch() ? srRatio : pitchRatio * srRatio;
        readRate = baseReadRate;
    }

    void SamplePlayerVoice::resolveLoopForSource()
    {
        const int len = source->numSamples();

        loopStart = std::floor (juce::jlimit (0.0, (double) len, loopStartNorm * (double) len));

        double end = std::floor (juce::jlimit (0.0, (double) len, loopEndNorm * (double) len));

        if (end <= loopStart + 1.0)
        {
            loopStart = 0.0;
            end = (double) len;
        }

        loopEnd = end;

        const double loopLen = loopEnd - loopStart;
        crossfadeLen = juce::jlimit (0.0, juce::jmin (loopLen * 0.5, loopStart),
                                     crossfadeNorm * loopLen);
    }

    SampleSourcePtr SamplePlayerVoice::pickSource()
    {
        if (pendingPool == nullptr || pendingPool->sources.empty() || genMode == GenMode::off)
            return source;

        const int count = (int) pendingPool->sources.size();

        if (genMode == GenMode::cycle)
        {
            auto picked = pendingPool->sources[(size_t) (cycleIndex % count)];
            cycleIndex = (cycleIndex + 1) % count;
            return picked;
        }

        if (random.nextFloat() < genProbability)
            return pendingPool->sources[(size_t) random.nextInt (count)];

        return source;
    }

    void SamplePlayerVoice::startNote (int midiNoteNumber, float velocity,
                                       juce::SynthesiserSound* sound, int currentPitchWheelPosition)
    {
        source = pendingSource != nullptr ? pendingSource->shared_from_this() : nullptr;

        auto chosen = pickSource();

        if (chosen == nullptr || chosen->numSamples() <= 0 || ! canPlaySound (sound))
        {
            clearCurrentNote();
            return;
        }

        source = chosen;
        currentMidiNote = midiNoteNumber;

        velocityGain = juce::jlimit (0.0f, 1.0f, velocity);
        velocityGain *= velocityGain;   // SFZ amp_veltrack=100
        expressionGain = 1.0f;
        granularReleasing = false;
        stretchEndCounter = 0;
        envelopeLevel = 0.0f;

        bendSemitones = ((double) currentPitchWheelPosition - 8192.0) / 8192.0
                        * (double) bendRangeSemitones;

        computeBaseReadRate();
        resolveLoopForSource();
        position = 0.0;
        lfoPhase[0] = lfoPhase[1] = 0.0;

        vcf.reset();

        filterAdsr.setSampleRate (getSampleRate());
        filterAdsr.setParameters (filterAdsrParams);
        filterAdsr.reset();
        filterAdsr.noteOn();

        if (sustainMode == SustainMode::granular)
        {
            cloud.setParams (grainParams);
            cloud.start (source, pitchRatio, 0.0, loopStart, loopEnd,
                         loopEnabled && loopEnd > loopStart + 1.0,
                         pitchMode == PitchMode::timePreserve);
        }
        else if (isUsingStretch())
        {
            stretch.setTransposeSemitones ((float) ((double) (midiNoteNumber - source->rootNote) + bendSemitones));
            stretch.reset();
        }

        adsr.setSampleRate (getSampleRate());
        adsr.setParameters (adsrParams);
        adsr.reset();
        adsr.noteOn();
    }

    void SamplePlayerVoice::stopNote (float /*velocity*/, bool allowTailOff)
    {
        filterAdsr.noteOff();

        if (sustainMode == SustainMode::granular)
        {
            cloud.release();
            granularReleasing = true;
            adsr.noteOff();
            return;
        }

        if (allowTailOff)
        {
            adsr.noteOff();
        }
        else
        {
            adsr.reset();
            clearCurrentNote();
        }
    }

    void SamplePlayerVoice::requestFastRelease()
    {
        if (sustainMode == SustainMode::granular)
        {
            cloud.release();
            granularReleasing = true;
            adsr.noteOff();
            return;
        }

        auto prm = adsr.getParameters();
        prm.release = 0.005f;
        adsr.setParameters (prm);
        adsr.noteOff();
    }

    void SamplePlayerVoice::pitchWheelMoved (int newValue)
    {
        bendSemitones = ((double) newValue - 8192.0) / 8192.0 * (double) bendRangeSemitones;
        computeBaseReadRate();

        if (isVoiceActive() && isUsingStretch() && source != nullptr)
            stretch.setTransposeSemitones ((float) ((double) (currentMidiNote - source->rootNote) + bendSemitones));
    }

    void SamplePlayerVoice::controllerMoved (int controllerNumber, int newValue)
    {
        if (controllerNumber == 11)
            expressionGain = juce::jlimit (0.0f, 1.0f, (float) newValue / 127.0f);
    }

    float SamplePlayerVoice::readLooped (int channel, double pos, double rate) const noexcept
    {
        if (source == nullptr || source->numSamples() <= 0)
            return 0.0f;

        const int srcCh = juce::jlimit (0, source->numChannels() - 1, channel);
        float s = SampleReader::read (*source, srcCh, pos, rate);

        const bool crossfading = loopEnabled
                                 && sustainMode == SustainMode::crossfade
                                 && (loopEnd - loopStart) > 1.0 && crossfadeLen > 0.0;

        if (crossfading && pos >= loopEnd - crossfadeLen)
        {
            const double u = juce::jlimit (0.0, 1.0,
                                           (pos - (loopEnd - crossfadeLen)) / crossfadeLen);
            const double otherPos = pos - (loopEnd - loopStart);
            const float other = SampleReader::read (*source, srcCh, otherPos, rate);

            float gIn, gOut;

            if (crossfadeCurve == 1)
            {
                gIn  = std::sin ((float) u * juce::MathConstants<float>::halfPi);
                gOut = std::cos ((float) u * juce::MathConstants<float>::halfPi);
            }
            else
            {
                gIn  = 0.5f - 0.5f * (float) std::cos (juce::MathConstants<double>::pi * u);
                gOut = 1.0f - gIn;
            }

            s = s * gOut + other * gIn;
        }

        return s;
    }

    void SamplePlayerVoice::updateModulation (int chunkSamples)
    {
        float fenv = 0.0f;

        for (int i = 0; i < chunkSamples; ++i)
            fenv = filterAdsr.getNextSample();

        const double sr = juce::jmax (1.0, getSampleRate());

        for (int l = 0; l < 2; ++l)
        {
            lfoPhase[l] += lfo[l].rateHz * (double) chunkSamples / sr;
            lfoPhase[l] -= std::floor (lfoPhase[l]);
        }

        float src[kNumModSources] = {};
        src[(int) ModSource::lfo1]     = lfoShapeValue (lfo[0].shape, lfoPhase[0]) * lfo[0].depth;
        src[(int) ModSource::lfo2]     = lfoShapeValue (lfo[1].shape, lfoPhase[1]) * lfo[1].depth;
        src[(int) ModSource::filterEnv] = fenv;
        src[(int) ModSource::ampEnv]   = envelopeLevel;
        src[(int) ModSource::velocity] = std::sqrt (juce::jmax (0.0f, velocityGain));
        src[(int) ModSource::modWheel] = modWheel;
        src[(int) ModSource::keyTrack] = (float) ((currentMidiNote - 60) / 60.0);

        modCutoffOct = 0.0;
        modResonance = 0.0;
        modPitchSemis = 0.0;
        modLevel = 1.0f;
        modGrainSize = 1.0;
        modGrainDensity = 1.0;

        for (const auto& slot : modMatrix)
        {
            if (slot.source == ModSource::none || slot.dest == ModDest::none)
                continue;

            const float v = src[(int) slot.source] * slot.amount;

            switch (slot.dest)
            {
                case ModDest::cutoff:       modCutoffOct += v * 4.0; break;   // up to +/- 4 octaves
                case ModDest::resonance:    modResonance += v * 4.0; break;
                case ModDest::pitch:        modPitchSemis += v * 12.0; break; // up to +/- 1 octave
                case ModDest::level:        modLevel += v; break;
                case ModDest::grainSize:    modGrainSize += v * 2.0; break;
                case ModDest::grainDensity: modGrainDensity += v * 2.0; break;
                default: break;
            }
        }

        const double cutoff = filterSettings.cutoffHz
                              * std::pow (2.0, filterSettings.envAmountOct * (double) fenv
                                              + filterSettings.keyTrack * (double) src[(int) ModSource::keyTrack]
                                              + modCutoffOct);

        vcf.setType ((juce::dsp::StateVariableTPTFilterType) (int) filterSettings.type);
        vcf.setCutoffFrequency ((float) juce::jlimit (20.0, juce::jmin (20000.0, sr * 0.49), cutoff));
        vcf.setResonance ((float) juce::jlimit (0.1, 10.0, filterSettings.resonance + modResonance));

        if (isUsingStretch())
        {
            readRate = baseReadRate;
            currentMipRatio = baseReadRate;
            stretch.setTransposeSemitones ((float) ((double) (currentMidiNote - source->rootNote)
                                                    + bendSemitones + modPitchSemis));
        }
        else
        {
            readRate = baseReadRate * std::pow (2.0, modPitchSemis / 12.0);
            currentMipRatio = readRate;
        }

        GrainCloudParams g = grainParams;
        g.grainSizeMs = juce::jlimit (5.0, 500.0, g.grainSizeMs * modGrainSize);
        g.density = juce::jlimit (0.1, 400.0, g.density * modGrainDensity);
        cloud.setParams (g);
    }

    bool SamplePlayerVoice::generateChunk (juce::AudioBuffer<float>& scratch, int numSamples)
    {
        if (sustainMode == SustainMode::granular)
        {
            for (int i = 0; i < numSamples; ++i)
                envScratch[(size_t) i] = adsr.isActive() ? adsr.getNextSample() : 0.0f;

            if (numSamples > 0)
                envelopeLevel = envScratch[(size_t) (numSamples - 1)];

            cloud.render (scratch, 0, numSamples, envScratch.data(), 1.0f);

            if (granularReleasing && ! cloud.hasActiveGrains())
                return false;

            return true;
        }

        if (isUsingStretch())
        {
            const int len = source->numSamples();
            const int srcChannels = source->numChannels();
            const bool looping = loopEnabled
                                 && (sustainMode == SustainMode::loop || sustainMode == SustainMode::crossfade);
            bool ended = false;

            for (int i = 0; i < numSamples; ++i)
            {
                if (ended)
                {
                    stretchIn.setSample (0, i, 0.0f);
                    stretchIn.setSample (1, i, 0.0f);
                    continue;
                }

                stretchIn.setSample (0, i, readLooped (0, position, currentMipRatio));
                stretchIn.setSample (1, i, readLooped (srcChannels > 1 ? 1 : 0, position, currentMipRatio));

                position += readRate;

                if (looping)
                {
                    const double loopLen = juce::jmax (1.0, loopEnd - loopStart);
                    if (position >= loopEnd)
                        position = loopStart + std::fmod (position - loopStart, loopLen);
                }
                else if (position >= (double) len - 1.0)
                {
                    ended = true;
                }
            }

            stretch.processBlock (stretchIn, stretchOut, numSamples);

            for (int i = 0; i < numSamples; ++i)
            {
                const float env = adsr.isActive() ? adsr.getNextSample() : 0.0f;
                envelopeLevel = env;
                const float v = stretchOut.getSample (0, i) * env;
                scratch.addSample (0, i, v);
                scratch.addSample (juce::jmin (1, scratch.getNumChannels() - 1), i,
                                   stretchOut.getSample (1, i) * env);
            }

            if (ended && ! looping)
            {
                stretchEndCounter += numSamples;

                if (stretchEndCounter > stretch.getLatencySamples() + declickSamples)
                    return false;

                return true;
            }

            return adsr.isActive();
        }

        // Direct (loop / crossfade / one-shot)
        const bool looping = loopEnabled
                             && (sustainMode == SustainMode::loop || sustainMode == SustainMode::crossfade);
        const int len = source->numSamples();
        const double endStart = (double) len - 1.0 - (double) declickSamples;

        for (int i = 0; i < numSamples; ++i)
        {
            if (! adsr.isActive())
                return false;

            const float env = adsr.getNextSample();
            envelopeLevel = env;

            float declick = 1.0f;

            if (! looping && position >= endStart)
                declick = juce::jlimit (0.0f, 1.0f,
                                        (float) (((double) len - 1.0 - position) / (double) declickSamples));

            for (int ch = 0; ch < scratch.getNumChannels(); ++ch)
                scratch.addSample (ch, i, readLooped (ch, position, currentMipRatio) * env * declick);

            position += readRate;

            if (looping)
            {
                const double loopLen = juce::jmax (1.0, loopEnd - loopStart);
                if (position >= loopEnd)
                    position = loopStart + std::fmod (position - loopStart, loopLen);
            }
            else if (position >= (double) len - 1.0)
            {
                return false;
            }
        }

        return true;
    }

    void SamplePlayerVoice::renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                                             int startSample, int numSamples)
    {
        if (source == nullptr || source->numSamples() <= 0)
        {
            clearCurrentNote();
            return;
        }

        const bool granularDraining = (sustainMode == SustainMode::granular && granularReleasing);

        if (! adsr.isActive() && ! granularDraining)
        {
            clearCurrentNote();
            return;
        }

        const int dstChannels = outputBuffer.getNumChannels();
        const int scratchChannels = voiceScratch.getNumChannels();
        int done = 0;

        while (done < numSamples)
        {
            const int n = juce::jmin (kControlBlock, numSamples - done);

            updateModulation (n);

            for (int ch = 0; ch < scratchChannels; ++ch)
                voiceScratch.clear (ch, 0, n);

            const bool alive = generateChunk (voiceScratch, n);

            const float driveGain = 1.0f + 4.0f * (float) filterSettings.drive;
            const float outGain = voiceGain() * modLevel;

            for (int k = 0; k < n; ++k)
            {
                for (int ch = 0; ch < dstChannels; ++ch)
                {
                    const int sc = juce::jmin (ch, scratchChannels - 1);
                    float s = voiceScratch.getSample (sc, k);

                    if (filterSettings.drive > 0.0)
                        s = std::tanh (s * driveGain);

                    s = vcf.processSample (sc, s);
                    outputBuffer.addSample (ch, startSample + done + k, s * outGain);
                }
            }

            done += n;

            if (! alive)
            {
                clearCurrentNote();
                return;
            }
        }

        if (! adsr.isActive() && ! granularDraining)
            clearCurrentNote();
    }
}
