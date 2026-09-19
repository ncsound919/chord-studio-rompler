#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SamplePlayerVoice.h"

#include <algorithm>
#include <limits>

namespace romp
{
    RomplerAudioProcessor::RomplerAudioProcessor()
        : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
    {
        formats.registerBasicFormats();

        synth.setNoteStealingEnabled (true);
        synth.addSound (new RomplerSound());

        for (int i = 0; i < (int) voiceArray.size(); ++i)
        {
            auto* v = new SamplePlayerVoice();
            voiceArray[(size_t) i] = v;
            synth.addVoice (v);
        }

        cacheParamPointers();

        lastGenMode = (int) p.genMode->load();
        startTimerHz (1);   // reclaim retired buffers, poll genMode
    }

    void RomplerAudioProcessor::timerCallback()
    {
        const double now = juce::Time::getMillisecondCounterHiRes();

        auto sweep = [now] (auto& pool)
        {
            for (auto it = pool.begin(); it != pool.end();)
            {
                // Only free once the grace period has passed and no voice holds a
                // reference (use_count()==1 means the pin is the sole owner).
                if (now - it->retiredAt > kReclaimGraceMs && it->ptr.use_count() == 1)
                    it = pool.erase (it);
                else
                    ++it;
            }
        };

        sweep (retiredSources);
        sweep (retiredPools);

        const int gm = (int) p.genMode->load();

        if (gm != lastGenMode)
        {
            lastGenMode = gm;

            if ((GenMode) gm != GenMode::off)
                buildPool();
        }

        updateReportedLatency();
    }

    void RomplerAudioProcessor::cacheParamPointers()
    {
        p.volume          = apvts.getRawParameterValue ("volume");
        p.attack          = apvts.getRawParameterValue ("attack");
        p.decay           = apvts.getRawParameterValue ("decay");
        p.sustain         = apvts.getRawParameterValue ("sustain");
        p.release         = apvts.getRawParameterValue ("release");
        p.loopStart       = apvts.getRawParameterValue ("loopStart");
        p.loopEnd         = apvts.getRawParameterValue ("loopEnd");
        p.crossfade       = apvts.getRawParameterValue ("crossfade");
        p.crossfadeCurve  = apvts.getRawParameterValue ("crossfadeCurve");
        p.grainSize       = apvts.getRawParameterValue ("grainSize");
        p.density         = apvts.getRawParameterValue ("density");
        p.spray           = apvts.getRawParameterValue ("spray");
        p.spread          = apvts.getRawParameterValue ("spread");
        p.grainDrift      = apvts.getRawParameterValue ("grainDrift");
        p.grainWindow     = apvts.getRawParameterValue ("grainWindow");
        p.genProbability  = apvts.getRawParameterValue ("genProbability");
        p.sustainMode     = apvts.getRawParameterValue ("sustainMode");
        p.pitchMode       = apvts.getRawParameterValue ("pitchMode");
        p.genMode         = apvts.getRawParameterValue ("genMode");
        p.loopEnabled     = apvts.getRawParameterValue ("loopEnabled");

        p.filterType      = apvts.getRawParameterValue ("filterType");
        p.filterCutoff    = apvts.getRawParameterValue ("filterCutoff");
        p.filterReso      = apvts.getRawParameterValue ("filterReso");
        p.filterDrive     = apvts.getRawParameterValue ("filterDrive");
        p.filterEnvAmount = apvts.getRawParameterValue ("filterEnvAmount");
        p.filterKeyTrack  = apvts.getRawParameterValue ("filterKeyTrack");
        p.filterAttack    = apvts.getRawParameterValue ("filterAttack");
        p.filterDecay     = apvts.getRawParameterValue ("filterDecay");
        p.filterSustain   = apvts.getRawParameterValue ("filterSustain");
        p.filterRelease   = apvts.getRawParameterValue ("filterRelease");
        p.lfo1Rate        = apvts.getRawParameterValue ("lfo1Rate");
        p.lfo1Shape       = apvts.getRawParameterValue ("lfo1Shape");
        p.lfo1Depth       = apvts.getRawParameterValue ("lfo1Depth");
        p.lfo2Rate        = apvts.getRawParameterValue ("lfo2Rate");
        p.lfo2Shape       = apvts.getRawParameterValue ("lfo2Shape");
        p.lfo2Depth       = apvts.getRawParameterValue ("lfo2Depth");

        for (int i = 0; i < kNumModSlots; ++i)
        {
            const juce::String n (i + 1);
            p.modSource[(size_t) i] = apvts.getRawParameterValue ("mod" + n + "Source");
            p.modDest[(size_t) i]   = apvts.getRawParameterValue ("mod" + n + "Dest");
            p.modAmount[(size_t) i] = apvts.getRawParameterValue ("mod" + n + "Amount");
        }
    }

    RomplerAudioProcessor::~RomplerAudioProcessor()
    {
        stopTimer();
    }

    juce::AudioProcessorValueTreeState::ParameterLayout
    RomplerAudioProcessor::createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        auto addFloat = [&layout] (const juce::String& id, const juce::String& name,
                                   float min, float max, float def, float skew = 1.0f)
        {
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { id, 1 }, name,
                juce::NormalisableRange<float> (min, max, 0.0f, skew), def));
        };

        addFloat ("volume",    "Volume",    0.0f,   1.0f,   0.8f,  0.5f);
        addFloat ("attack",    "Attack",    0.0f,   2.0f,   0.005f, 0.4f);
        addFloat ("decay",     "Decay",     0.0f,   2.0f,   0.2f,  0.4f);
        addFloat ("sustain",   "Sustain",   0.0f,   1.0f,   0.8f);
        addFloat ("release",   "Release",   0.0f,   5.0f,   0.4f,  0.4f);
        addFloat ("loopStart", "Loop Start", 0.0f,  1.0f,   0.0f);
        addFloat ("loopEnd",   "Loop End",  0.0f,   1.0f,   1.0f);
        addFloat ("crossfade", "Crossfade", 0.0f,   0.5f,   0.02f);
        addFloat ("grainSize", "Grain Size", 5.0f,  500.0f, 80.0f, 0.5f);
        addFloat ("density",   "Density",   1.0f,   200.0f, 20.0f, 0.5f);
        addFloat ("spray",     "Spray",     0.0f,   1.0f,   0.2f);
        addFloat ("spread",    "Spread",    0.0f,   1.0f,   0.5f);
        addFloat ("grainDrift", "Drift",    0.0f,   1.0f,   0.0f);

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { "grainWindow", 1 }, "Grain Window",
            juce::StringArray { "Hann", "Tukey" }, 0));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { "crossfadeCurve", 1 }, "Crossfade Curve",
            juce::StringArray { "Gain", "Power" }, 0));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { "loopEnabled", 1 }, "Loop", false));

        // --- filter -------------------------------------------------------
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { "filterType", 1 }, "Filter Type",
            juce::StringArray { "Low Pass", "Band Pass", "High Pass" }, 0));

        addFloat ("filterCutoff",  "Cutoff",      20.0f, 20000.0f, 18000.0f, 0.25f);
        addFloat ("filterReso",    "Resonance",   0.1f,  10.0f,    0.7f,    0.5f);
        addFloat ("filterDrive",   "Drive",       0.0f,  1.0f,     0.0f);
        addFloat ("filterEnvAmount", "Filter Env", -1.0f, 1.0f,    0.0f);
        addFloat ("filterKeyTrack", "Key Track",  0.0f,  1.0f,     0.0f);
        addFloat ("filterAttack",  "F Attack",    0.0f,  2.0f,     0.005f,  0.4f);
        addFloat ("filterDecay",   "F Decay",     0.0f,  2.0f,     0.2f,    0.4f);
        addFloat ("filterSustain", "F Sustain",   0.0f,  1.0f,     0.8f);
        addFloat ("filterRelease", "F Release",   0.0f,  5.0f,     0.4f,    0.4f);

        // --- LFOs ---------------------------------------------------------
        const juce::StringArray lfoShapes { "Sine", "Triangle", "Saw", "Square" };

        addFloat ("lfo1Rate",  "LFO1 Rate",  0.01f, 30.0f, 2.0f, 0.3f);
        addFloat ("lfo1Depth", "LFO1 Depth", 0.0f,  1.0f,  0.0f);
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { "lfo1Shape", 1 }, "LFO1 Shape", lfoShapes, 0));

        addFloat ("lfo2Rate",  "LFO2 Rate",  0.01f, 30.0f, 0.5f, 0.3f);
        addFloat ("lfo2Depth", "LFO2 Depth", 0.0f,  1.0f,  0.0f);
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { "lfo2Shape", 1 }, "LFO2 Shape", lfoShapes, 0));

        // --- mod matrix ---------------------------------------------------
        const juce::StringArray modSources { "None", "LFO1", "LFO2", "Filter Env",
                                             "Amp Env", "Velocity", "Mod Wheel", "Key Track" };
        const juce::StringArray modDests { "None", "Cutoff", "Resonance", "Pitch",
                                           "Level", "Grain Size", "Grain Density" };

        for (int i = 0; i < kNumModSlots; ++i)
        {
            const juce::String n (i + 1);
            layout.add (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { "mod" + n + "Source", 1 }, "Mod " + n + " Source", modSources, 0));
            layout.add (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { "mod" + n + "Dest", 1 }, "Mod " + n + " Dest", modDests, 0));
            addFloat ("mod" + n + "Amount", "Mod " + n + " Amount", -1.0f, 1.0f, 0.0f);
        }

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { "sustainMode", 1 }, "Sustain Mode",
            juce::StringArray { "Loop", "Crossfade", "Granular" }, 0));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { "pitchMode", 1 }, "Pitch Mode",
            juce::StringArray { "Chromatic", "Time Preserve" }, 0));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { "genMode", 1 }, "Generative Mode",
            juce::StringArray { "Off", "Random", "Cycle" }, 0));

        addFloat ("genProbability", "Variation", 0.0f, 1.0f, 0.5f);

        return layout;
    }

    void RomplerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        synth.setCurrentPlaybackSampleRate (sampleRate);

        const int channels = juce::jmax (1, getTotalNumOutputChannels());

        for (auto* v : voiceArray)
            if (v != nullptr)
                v->prepare (sampleRate, juce::jmax (1, samplesPerBlock), channels);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = (juce::uint32) juce::jmax (1, samplesPerBlock);
        spec.numChannels = (juce::uint32) channels;

        limiter.prepare (spec);
        limiter.setThreshold (-1.0f);
        limiter.setRelease (50.0f);
        limiter.reset();

        dcX.fill (0.0f);
        dcY.fill (0.0f);

        updateReportedLatency();
    }

    void RomplerAudioProcessor::updateReportedLatency()
    {
        // Instruments cannot be MIDI-compensated by the host, so report the
        // stretch latency only when the time-preserving path is actually in use.
        // setLatencySamples is a no-op when the value is unchanged; it is called
        // only from the message thread (prepareToPlay / timer).
        const bool useStretch = ((PitchMode) (int) p.pitchMode->load() == PitchMode::timePreserve)
                                && ((SustainMode) (int) p.sustainMode->load() != SustainMode::granular);

        const int latency = (useStretch && voiceArray[0] != nullptr)
                                ? voiceArray[0]->getStretchLatencySamples()
                                : 0;

        if (latency != getLatencySamples())
            setLatencySamples (latency);
    }

    void RomplerAudioProcessor::releaseResources()
    {
    }

    bool RomplerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto& out = layouts.getMainOutputChannelSet();
        return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
    }

    void RomplerAudioProcessor::pushParamsToVoices()
    {
        juce::ADSR::Parameters adsrParams;
        adsrParams.attack  = p.attack->load();
        adsrParams.decay   = p.decay->load();
        adsrParams.sustain = p.sustain->load();
        adsrParams.release = p.release->load();

        const auto sustainMode = (SustainMode) (int) p.sustainMode->load();
        const auto pitchMode   = (PitchMode)   (int) p.pitchMode->load();
        const double loopStart = (double) p.loopStart->load();
        const double loopEnd   = (double) p.loopEnd->load();
        const double crossfade = (double) p.crossfade->load();
        const int crossfadeCurve = (int) p.crossfadeCurve->load();

        GrainCloudParams grain;
        grain.grainSizeMs = (double) p.grainSize->load();
        grain.density     = (double) p.density->load();
        grain.spray       = (double) p.spray->load();
        grain.spread      = (double) p.spread->load();
        grain.drift       = (double) p.grainDrift->load();
        grain.window      = (int) p.grainWindow->load();
        grain.timePreserve = (pitchMode == PitchMode::timePreserve);

        const auto genMode = (GenMode) (int) p.genMode->load();
        const float genProbability = p.genProbability->load();
        const bool loopEnabled = p.loopEnabled->load() > 0.5f;

        FilterSettings filter;
        filter.type         = (FilterType) (int) p.filterType->load();
        filter.cutoffHz     = (double) p.filterCutoff->load();
        filter.resonance    = (double) p.filterReso->load();
        filter.drive        = (double) p.filterDrive->load();
        filter.envAmountOct = (double) p.filterEnvAmount->load() * 4.0;   // +/- 4 octaves
        filter.keyTrack     = (double) p.filterKeyTrack->load();

        juce::ADSR::Parameters fenv;
        fenv.attack  = p.filterAttack->load();
        fenv.decay   = p.filterDecay->load();
        fenv.sustain = p.filterSustain->load();
        fenv.release = p.filterRelease->load();

        LfoSettings lfo1 { (double) p.lfo1Rate->load(), (int) p.lfo1Shape->load(), p.lfo1Depth->load() };
        LfoSettings lfo2 { (double) p.lfo2Rate->load(), (int) p.lfo2Shape->load(), p.lfo2Depth->load() };

        ModMatrix mods;

        for (int i = 0; i < kNumModSlots; ++i)
        {
            mods[(size_t) i].source = (ModSource) (int) p.modSource[(size_t) i]->load();
            mods[(size_t) i].dest   = (ModDest)   (int) p.modDest[(size_t) i]->load();
            mods[(size_t) i].amount = p.modAmount[(size_t) i]->load();
        }

        const float modWheel = (float) synth.getCachedController (1, 1) / 127.0f;

        for (auto* v : voiceArray)
        {
            if (v == nullptr)
                continue;

            v->setAdsrParameters (adsrParams);
            v->setFilterEnvParameters (fenv);
            v->setSustainMode (sustainMode);
            v->setPitchMode (pitchMode);
            v->setLoopEnabled (loopEnabled);
            v->setLoopPoints (loopStart, loopEnd);
            v->setCrossfade (crossfade);
            v->setCrossfadeCurve (crossfadeCurve);
            v->setGranularParams (grain);
            v->setGenerativeMode (genMode, genProbability);
            v->setFilterSettings (filter);
            v->setLfoSettings (0, lfo1);
            v->setLfoSettings (1, lfo2);
            v->setModMatrix (mods);
            v->setModWheel (modWheel);
        }
    }

    void RomplerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
    {
        juce::ScopedNoDenormals noDenormals;
        buffer.clear();

        // Publish the current sample to idle voices. Sounding voices keep the
        // buffer they started with (see SamplePlayerVoice::setSource).
        auto* rawSource = liveSource.load (std::memory_order_acquire);
        auto* rawPool = livePool.load (std::memory_order_acquire);

        for (auto* v : voiceArray)
        {
            if (v == nullptr)
                continue;

            v->setSource (rawSource);
            v->setPool (rawPool);
        }

        pushParamsToVoices();

        synth.renderNextBlock (buffer, midiMessages, 0, buffer.getNumSamples());

        buffer.applyGain (p.volume->load());

        // DC blocker (one-pole, ~5 Hz) then a brickwall-ish limiter so extreme
        // grain density/size can't hard-clip the output.
        const int numCh = buffer.getNumChannels();
        const int numS = buffer.getNumSamples();
        constexpr float r = 0.9995f;

        for (int ch = 0; ch < juce::jmin (numCh, 2); ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            float x1 = dcX[(size_t) ch], y1 = dcY[(size_t) ch];

            for (int i = 0; i < numS; ++i)
            {
                const float x = d[i];
                const float y = x - x1 + r * y1;
                x1 = x;
                y1 = y;
                d[i] = y;
            }

            dcX[(size_t) ch] = x1;
            dcY[(size_t) ch] = y1;
        }

        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        limiter.process (ctx);
    }

    juce::AudioProcessorEditor* RomplerAudioProcessor::createEditor()
    {
        return new RomplerAudioProcessorEditor (*this);
    }

    // --- sample loading (message thread) --------------------------------------

    SampleSourcePtr RomplerAudioProcessor::decodeFile (const juce::File& file, juce::String& error)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

        if (reader == nullptr)
        {
            error = "Unsupported or unreadable file: " + file.getFileName();
            return {};
        }

        const juce::int64 totalSamples = reader->lengthInSamples;

        if (totalSamples <= 0)
        {
            error = "Empty file: " + file.getFileName();
            return {};
        }

        const int numChannels = juce::jmax (1, (int) reader->numChannels);
        const int numSamples = (int) juce::jmin<juce::int64> (totalSamples, std::numeric_limits<int>::max());

        auto source = std::make_shared<SampleSource>();
        source->sampleRate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
        source->name = file.getFileNameWithoutExtension();
        source->sourcePath = file.getFullPathName();
        source->buffer.setSize (numChannels, numSamples);
        reader->read (&source->buffer, 0, numSamples, 0, true, true);

        buildMipmaps (*source);

        return source;
    }

    void RomplerAudioProcessor::publishSource (const SampleSourcePtr& newSource)
    {
        if (heldSource != nullptr)
            retiredSources.push_back ({ heldSource, juce::Time::getMillisecondCounterHiRes() });

        heldSource = newSource;
        liveSource.store (newSource.get(), std::memory_order_release);
    }

    void RomplerAudioProcessor::computePeaks (const SampleSource& source)
    {
        peakMin.assign (kPeakBuckets, 0.0f);
        peakMax.assign (kPeakBuckets, 0.0f);

        const int n = source.numSamples();
        const int channels = source.numChannels();

        if (n <= 0 || channels <= 0)
            return;

        for (int b = 0; b < kPeakBuckets; ++b)
        {
            const int start = (int) ((juce::int64) b * n / kPeakBuckets);
            const int end = juce::jmax (start + 1, (int) ((juce::int64) (b + 1) * n / kPeakBuckets));

            float lo = 1.0e9f, hi = -1.0e9f;

            for (int ch = 0; ch < channels; ++ch)
                for (int i = start; i < end && i < n; ++i)
                {
                    const float v = source.buffer.getSample (ch, i);
                    lo = juce::jmin (lo, v);
                    hi = juce::jmax (hi, v);
                }

            if (lo > hi) { lo = 0.0f; hi = 0.0f; }
            peakMin[b] = lo;
            peakMax[b] = hi;
        }
    }

    void RomplerAudioProcessor::loadSampleFile (const juce::File& file)
    {
        juce::String error;
        auto source = decodeFile (file, error);

        if (source == nullptr)
        {
            lastError = error;
            return;
        }

        computePeaks (*source);
        publishSource (source);

        loadedSampleName = source->name;
        lastError.clear();

        const int idx = folderFiles.indexOf (file);

        if (idx >= 0)
            browserIndex = idx;
    }

    void RomplerAudioProcessor::refreshFolder()
    {
        folderFiles.clear();

        if (currentFolder.isDirectory())
        {
            const juce::String wildcards = "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3";

            std::vector<juce::File> files;

            // Recursive so libraries organised into subfolders are visible.
            for (const auto& entry : juce::RangedDirectoryIterator (currentFolder, true, wildcards,
                                                                    juce::File::findFiles))
            {
                files.push_back (entry.getFile());

                if ((int) files.size() >= kMaxScanFiles)
                    break;
            }

            std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
            {
                return a.getFullPathName().compareIgnoreCase (b.getFullPathName()) < 0;
            });

            for (const auto& f : files)
                folderFiles.add (f);
        }

        browserIndex = 0;

        if (folderFiles.isEmpty())
        {
            lastError = currentFolder.isDirectory() ? "No audio files in this folder"
                                                    : "Folder not found";
            return;
        }

        loadSampleFile (folderFiles[0]);

        if ((GenMode) (int) p.genMode->load() != GenMode::off)
            buildPool();
    }

    void RomplerAudioProcessor::buildPool()
    {
        auto view = std::make_shared<SamplePoolView>();

        const int count = juce::jmin (kGenPoolSize, folderFiles.size());

        for (int i = 0; i < count; ++i)
        {
            juce::String error;

            if (auto source = decodeFile (folderFiles[i], error))
                view->sources.push_back (source);
        }

        if (heldPool != nullptr)
            retiredPools.push_back ({ heldPool, juce::Time::getMillisecondCounterHiRes() });

        heldPool = view;
        livePool.store (view.get(), std::memory_order_release);
    }

    void RomplerAudioProcessor::loadFolder (const juce::File& folder)
    {
        currentFolder = folder;
        refreshFolder();
    }

    void RomplerAudioProcessor::selectBrowserSample (int index)
    {
        if (folderFiles.isEmpty())
            return;

        browserIndex = juce::jlimit (0, folderFiles.size() - 1, index);
        loadSampleFile (folderFiles[browserIndex]);
    }

    void RomplerAudioProcessor::stepBrowserSample (int delta)
    {
        if (folderFiles.isEmpty())
            return;

        const int count = folderFiles.size();
        selectBrowserSample ((browserIndex + (delta % count) + count) % count);
    }

    juce::var RomplerAudioProcessor::buildUiState() const
    {
        auto* root = new juce::DynamicObject();

        root->setProperty ("sampleName", loadedSampleName.isNotEmpty() ? loadedSampleName : juce::String ("-"));
        root->setProperty ("folder", currentFolder.getFullPathName());
        root->setProperty ("browserIndex", browserIndex);
        root->setProperty ("numSamples", folderFiles.size());
        root->setProperty ("error", lastError);
        root->setProperty ("sustainMode", (int) apvts.getRawParameterValue ("sustainMode")->load());
        root->setProperty ("pitchMode", (int) apvts.getRawParameterValue ("pitchMode")->load());
        root->setProperty ("genMode", (int) apvts.getRawParameterValue ("genMode")->load());

        juce::Array<juce::var> fileNames;

        for (const auto& f : folderFiles)
            fileNames.add (f.getFileNameWithoutExtension());

        root->setProperty ("files", fileNames);

        int activeVoices = 0;

        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (synth.getVoice (i)->isVoiceActive())
                ++activeVoices;

        root->setProperty ("activeVoices", activeVoices);
        root->setProperty ("voiceCount", synth.getNumVoices());

        juce::Array<juce::var> minArr, maxArr;

        for (float v : peakMin) minArr.add (v);
        for (float v : peakMax) maxArr.add (v);

        root->setProperty ("peaksMin", minArr);
        root->setProperty ("peaksMax", maxArr);

        auto* params = new juce::DynamicObject();

        for (auto* param : getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
                params->setProperty (ranged->paramID, ranged->getValue());

        root->setProperty ("params", juce::var (params));

        return juce::var (root);
    }

    // --- state ----------------------------------------------------------------

    void RomplerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        auto state = apvts.copyState();
        state.setProperty ("folder", currentFolder.getFullPathName(), nullptr);
        state.setProperty ("browserIndex", browserIndex, nullptr);

        if (auto xml = state.createXml())
            copyXmlToBinary (*xml, destData);
    }

    void RomplerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

        if (xml == nullptr)
            return;

        auto state = juce::ValueTree::fromXml (*xml);

        if (! state.isValid())
            return;

        apvts.replaceState (state);

        const juce::File folder (state.getProperty ("folder").toString());

        if (folder.isDirectory())
        {
            currentFolder = folder;
            refreshFolder();
            selectBrowserSample ((int) state.getProperty ("browserIndex", 0));
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new romp::RomplerAudioProcessor();
}
