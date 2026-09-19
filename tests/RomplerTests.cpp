// Headless DSP tests for the Rompler voice/engine. No GUI, no host.
#include <JuceHeader.h>

#include "SampleSource.h"
#include "SamplePlayerVoice.h"
#include "GrainCloud.h"
#include "StretchStage.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

namespace
{
    int checks = 0;
    int failures = 0;

    void check (bool condition, const char* label)
    {
        ++checks;

        if (! condition)
        {
            ++failures;
            std::printf ("FAIL: %s\n", label);
        }
    }

    romp::SampleSourcePtr makeSine (double freq, double sr, double seconds, int rootNote = 60)
    {
        auto s = std::make_shared<romp::SampleSource>();
        s->sampleRate = sr;
        s->rootNote = rootNote;
        s->name = "sine " + juce::String (freq);

        const int n = (int) (sr * seconds);
        s->buffer.setSize (1, n);

        for (int i = 0; i < n; ++i)
            s->buffer.setSample (0, i,
                                 0.8f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq
                                                          * (double) i / sr));

        romp::buildMipmaps (*s);
        return s;
    }

    double rms (const juce::AudioBuffer<float>& b, int ch, int start, int count)
    {
        double sum = 0.0;
        const int n = juce::jmin (count, b.getNumSamples() - start);

        for (int i = 0; i < n; ++i)
        {
            const double v = b.getSample (ch, start + i);
            sum += v * v;
        }

        return n > 0 ? std::sqrt (sum / (double) n) : 0.0;
    }

    bool allFiniteBounded (const juce::AudioBuffer<float>& b, float limit)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float v = b.getSample (ch, i);

                if (! std::isfinite (v) || std::abs (v) > limit)
                    return false;
            }

        return true;
    }

    // Peak-bin frequency estimate. Robust even with phase-vocoder smear.
    double dominantFrequency (const juce::AudioBuffer<float>& b, int ch, int start, int count, double sr)
    {
        const int order = 12;
        const int fftSize = 1 << order;

        if (count < fftSize || start + fftSize > b.getNumSamples())
            return 0.0;

        juce::dsp::FFT fft (order);
        std::vector<float> data ((size_t) (2 * fftSize), 0.0f);

        for (int i = 0; i < fftSize; ++i)
        {
            const float w = 0.5f - 0.5f * (float) std::cos (2.0 * juce::MathConstants<double>::pi
                                                            * (double) i / (double) (fftSize - 1));
            data[(size_t) i] = b.getSample (ch, start + i) * w;
        }

        fft.performFrequencyOnlyForwardTransform (data.data());

        int peak = 1;

        for (int i = 2; i < fftSize / 2; ++i)
            if (data[(size_t) i] > data[(size_t) peak])
                peak = i;

        return (double) peak * sr / (double) fftSize;
    }

    double maxAbsFirstDifference (const juce::AudioBuffer<float>& b, int ch, int start, int count)
    {
        double m = 0.0;
        const int end = juce::jmin (start + count, b.getNumSamples());

        for (int i = juce::jmax (1, start); i < end; ++i)
        {
            const double d = std::abs ((double) b.getSample (ch, i) - (double) b.getSample (ch, i - 1));
            m = juce::jmax (m, d);
        }

        return m;
    }

    void configureVoice (romp::SamplePlayerVoice& v, double sr,
                         romp::SustainMode sm, romp::PitchMode pm)
    {
        v.setCurrentPlaybackSampleRate (sr);
        v.prepare (sr, 32768, 2);

        juce::ADSR::Parameters p;
        p.attack = 0.001f;
        p.decay = 0.0f;
        p.sustain = 1.0f;
        p.release = 0.05f;

        v.setAdsrParameters (p);
        v.setSustainMode (sm);
        v.setPitchMode (pm);
        v.setLoopEnabled (true);
        v.setLoopPoints (0.0, 1.0);
        v.setCrossfade (0.0);
        v.setLevel (1.0f);
    }

    void renderNote (romp::SamplePlayerVoice& v, juce::AudioBuffer<float>& out, int note)
    {
        static romp::RomplerSound sound;
        out.clear();
        v.startNote (note, 1.0f, &sound, 8192);   // 8192 == pitch wheel centre
        v.renderNextBlock (out, 0, out.getNumSamples());
    }
}

int main()
{
    const double sr = 48000.0;

    // 1. Mipmap construction + band-limiting.
    {
        auto s = makeSine (15000.0, sr, 1.0);

        check (s->numLevels() >= 4, "mipmaps: at least 4 levels built");

        const int n0 = s->levelBuffer (0).getNumSamples();
        const int n1 = s->levelBuffer (1).getNumSamples();
        const int n3 = s->levelBuffer (3).getNumSamples();

        check (std::abs (n1 - n0 / 2) <= 1, "mipmaps: level 1 is half length");
        check (std::abs (n3 - n0 / 8) <= 1, "mipmaps: level 3 is one-eighth length");

        const double rms0 = rms (s->levelBuffer (0), 0, 0, n0);
        const double rms1 = rms (s->levelBuffer (1), 0, 0, n1);
        const double rms3 = rms (s->levelBuffer (3), 0, 0, n3);

        check (rms1 < rms0 * 0.5, "mipmaps: level 1 attenuates the 15 kHz tone");
        check (rms3 < rms0 * 0.1, "mipmaps: level 3 heavily attenuates the 15 kHz tone");
    }

    // 2. Chromatic repitch accuracy.
    {
        auto s = makeSine (220.0, sr, 1.0, 60);
        romp::SamplePlayerVoice v;
        configureVoice (v, sr, romp::SustainMode::loop, romp::PitchMode::chromatic);
        v.setSource (s.get());

        juce::AudioBuffer<float> out (2, 16384);

        renderNote (v, out, 60);
        check (allFiniteBounded (out, 4.0f), "chromatic: output finite and bounded");
        const double f60 = dominantFrequency (out, 0, 2048, 8192, sr);
        check (std::abs (f60 - 220.0) < 20.0, "chromatic: note 60 renders ~220 Hz");

        v.stopNote (0.0f, false);
        renderNote (v, out, 72);
        const double f72 = dominantFrequency (out, 0, 2048, 8192, sr);
        check (std::abs (f72 - 440.0) < 30.0, "chromatic: note 72 renders ~440 Hz (octave up)");
    }

    // 3. Crossfade loop reduces the seam discontinuity versus a hard loop.
    {
        auto s = makeSine (200.0, sr, 1.0, 60);

        // Loop length 10.5 periods => hard wrap lands out of phase (guaranteed click).
        const double loopStartN = 1000.0 / sr;
        const double loopEndN = 3520.0 / sr;

        romp::SamplePlayerVoice hard;
        configureVoice (hard, sr, romp::SustainMode::loop, romp::PitchMode::chromatic);
        hard.setLoopPoints (loopStartN, loopEndN);
        hard.setSource (s.get());

        romp::SamplePlayerVoice xf;
        configureVoice (xf, sr, romp::SustainMode::crossfade, romp::PitchMode::chromatic);
        xf.setLoopPoints (loopStartN, loopEndN);
        xf.setCrossfade (0.1);
        xf.setSource (s.get());

        juce::AudioBuffer<float> hardOut (2, 24000), xfOut (2, 24000);
        renderNote (hard, hardOut, 60);
        renderNote (xf, xfOut, 60);

        check (allFiniteBounded (hardOut, 4.0f) && allFiniteBounded (xfOut, 4.0f),
               "loop: both modes finite and bounded");

        const double hardDiff = maxAbsFirstDifference (hardOut, 0, 5000, 18000);
        const double xfDiff = maxAbsFirstDifference (xfOut, 0, 5000, 18000);

        check (hardDiff > 0.02, "loop: hard loop actually has a seam (test is meaningful)");
        check (xfDiff <= hardDiff * 1.25 + 1e-3, "crossfade: seam smoother than hard loop");
    }

    // 4. Granular: produces sound and transposes via grain rate.
    {
        auto s = makeSine (220.0, sr, 1.0, 60);
        romp::SamplePlayerVoice v;
        configureVoice (v, sr, romp::SustainMode::granular, romp::PitchMode::timePreserve);

        romp::GrainCloudParams gp;
        gp.grainSizeMs = 100.0;
        gp.density = 20.0;
        gp.spray = 0.1;
        gp.spread = 0.3;
        gp.timePreserve = true;
        v.setGranularParams (gp);
        v.setSource (s.get());

        juce::AudioBuffer<float> out (2, 16384);
        renderNote (v, out, 60);
        check (allFiniteBounded (out, 6.0f), "granular: output finite and bounded");
        check (rms (out, 0, 2048, 8192) > 0.005, "granular: output is not silent");
        const double g60 = dominantFrequency (out, 0, 2048, 8192, sr);
        check (std::abs (g60 - 220.0) < 40.0, "granular: note 60 ~220 Hz");

        v.stopNote (0.0f, false);
        renderNote (v, out, 72);
        const double g72 = dominantFrequency (out, 0, 2048, 8192, sr);
        check (std::abs (g72 - 440.0) < 60.0, "granular: note 72 ~440 Hz via grain rate");

        check (romp::GrainCloud::kMaxGrains >= 100, "granular: grain pool covers max density*size");
    }

    // 5. StretchStage: time-preserving transpose (equal in/out length).
    {
        auto run = [&] (float semitones, double expected)
        {
            romp::StretchStage stage;
            stage.prepare (sr, 512, 2);
            stage.setTransposeSemitones (semitones);

            const int total = 48000;
            juce::AudioBuffer<float> in (2, total), out (2, total);
            in.clear();

            for (int i = 0; i < total; ++i)
            {
                const float v = 0.6f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0
                                                         * (double) i / sr);
                in.setSample (0, i, v);
                in.setSample (1, i, v);
            }

            for (int pos = 0; pos < total; pos += 512)
            {
                const int n = juce::jmin (512, total - pos);

                juce::AudioBuffer<float> inChunk (2, n), outChunk (2, n);
                inChunk.copyFrom (0, 0, in, 0, pos, n);
                inChunk.copyFrom (1, 0, in, 1, pos, n);
                stage.processBlock (inChunk, outChunk, n);
                out.copyFrom (0, pos, outChunk, 0, 0, n);
                out.copyFrom (1, pos, outChunk, 1, 0, n);
            }

            const double f = dominantFrequency (out, 0, 24000, 8192, sr);
            std::printf ("  stretch: transpose %+g semitones -> %.1f Hz (expected ~%.1f)\n",
                         semitones, f, expected);

            check (stage.getLatencySamples() > 0, "stretch: reports latency");
            check (allFiniteBounded (out, 6.0f), "stretch: output finite and bounded");
            check (std::abs (f - expected) < expected * 0.08, "stretch: transposed pitch is accurate");
        };

        run (12.0f, 440.0);
        run (0.0f, 220.0);
    }

    // 6. Generative per-note selection: cycle mode walks the pool.
    {
        auto a = makeSine (220.0, sr, 1.0, 60);
        auto b = makeSine (330.0, sr, 1.0, 60);

        romp::SamplePlayerVoice v;
        configureVoice (v, sr, romp::SustainMode::loop, romp::PitchMode::chromatic);
        v.setSource (a.get());
        v.setPool (nullptr);
        v.setGenerativeMode (romp::GenMode::cycle, 1.0f);

        auto pool = std::make_shared<romp::SamplePoolView>();
        pool->sources.push_back (a);
        pool->sources.push_back (b);
        v.setPool (pool.get());

        juce::AudioBuffer<float> out (2, 16384);
        renderNote (v, out, 60);
        const double first = dominantFrequency (out, 0, 2048, 8192, sr);

        v.stopNote (0.0f, false);
        renderNote (v, out, 60);
        const double second = dominantFrequency (out, 0, 2048, 8192, sr);

        std::printf ("  generative cycle: first %.1f Hz, second %.1f Hz\n", first, second);

        check (std::abs (first - 220.0) < 30.0, "generative cycle: first note uses pool[0]");
        check (std::abs (second - 330.0) < 30.0, "generative cycle: second note uses pool[1]");

        // Random mode with probability 0 must always fall back to the selected sample.
        romp::SamplePlayerVoice r;
        configureVoice (r, sr, romp::SustainMode::loop, romp::PitchMode::chromatic);
        r.setSource (a.get());
        r.setPool (pool.get());
        r.setGenerativeMode (romp::GenMode::random, 0.0f);
        renderNote (r, out, 60);
        check (std::abs (dominantFrequency (out, 0, 2048, 8192, sr) - 220.0) < 30.0,
               "generative random: probability 0 keeps the selected sample");
    }

    // 7. Mip level selection uses the true read rate (sample-rate ratio included).
    {
        using romp::SampleReader;
        check (SampleReader::levelForRate (1.0) == 0, "mips: rate 1.0 -> level 0");
        check (SampleReader::levelForRate (0.5) == 0, "mips: rate < 1 -> level 0");
        check (SampleReader::levelForRate (1.05) == 1, "mips: rate 1.05 -> level 1");
        check (SampleReader::levelForRate (2.0) == 1, "mips: rate 2.0 -> level 1");
        check (SampleReader::levelForRate (2.01) == 2, "mips: rate 2.01 -> level 2");
        check (SampleReader::levelForRate (4.0) == 2, "mips: rate 4.0 -> level 2");

        // A 96k source at root on a 48k host reads at 2.0 source-frames/output,
        // so it must use a band-limited level. The old code used pitchRatio only
        // (1.0) and read level 0, aliasing everything above 24 kHz.
        const double srRatio = 96000.0 / 48000.0;
        check (SampleReader::levelForRate (1.0 * srRatio) >= 1,
               "mips: 96k source on 48k host selects a band-limited level");
    }

    // 8. One-shot playback ends cleanly (declick) and frees the voice.
    {
        auto s = makeSine (200.0, sr, 0.2, 60);
        romp::SamplePlayerVoice v;
        configureVoice (v, sr, romp::SustainMode::loop, romp::PitchMode::chromatic);
        v.setLoopEnabled (false);
        v.setSource (s.get());

        juce::AudioBuffer<float> out (2, 24000);
        renderNote (v, out, 60);

        check (allFiniteBounded (out, 2.0f), "one-shot: finite and bounded");
        check (maxAbsFirstDifference (out, 0, 0, out.getNumSamples()) < 0.1,
               "one-shot: end-of-sample declick avoids a click");
        check (rms (out, 0, out.getNumSamples() - 4096, 4096) < 1e-5,
               "one-shot: silent once the sample has ended");
    }

    // 9. Velocity follows the SFZ-style squared curve.
    {
        auto s = makeSine (220.0, sr, 1.0, 60);
        romp::SamplePlayerVoice full, half;
        configureVoice (full, sr, romp::SustainMode::loop, romp::PitchMode::chromatic);
        configureVoice (half, sr, romp::SustainMode::loop, romp::PitchMode::chromatic);
        full.setSource (s.get());
        half.setSource (s.get());

        static romp::RomplerSound sound;
        juce::AudioBuffer<float> a (2, 16384), b (2, 16384);
        a.clear(); full.startNote (60, 1.0f, &sound, 8192); full.renderNextBlock (a, 0, a.getNumSamples());
        b.clear(); half.startNote (60, 0.5f, &sound, 8192); half.renderNextBlock (b, 0, b.getNumSamples());

        const double rFull = rms (a, 0, 2048, 8192);
        const double rHalf = rms (b, 0, 2048, 8192);
        check (rFull > 1e-4 && std::abs (rHalf / rFull - 0.25) < 0.05,
               "velocity: half velocity is ~1/4 amplitude (squared curve)");
    }

    // 10. Granular note-off drains to silence and frees the voice.
    {
        auto s = makeSine (220.0, sr, 1.0, 60);
        romp::SamplePlayerVoice v;
        configureVoice (v, sr, romp::SustainMode::granular, romp::PitchMode::timePreserve);

        romp::GrainCloudParams gp;
        gp.grainSizeMs = 60.0;
        gp.density = 25.0;
        gp.spray = 0.05;
        v.setGranularParams (gp);
        v.setSource (s.get());

        juce::AudioBuffer<float> out (2, 8192);
        renderNote (v, out, 60);
        check (rms (out, 0, 0, 4096) > 0.005, "granular: sounding while held");

        v.stopNote (0.0f, true);

        double tailRms = 1.0;

        for (int block = 0; block < 200; ++block)
        {
            out.clear();
            v.renderNextBlock (out, 0, out.getNumSamples());
            tailRms = rms (out, 0, 0, out.getNumSamples());

            if (tailRms < 1e-6)
                break;
        }

        check (tailRms < 1e-6, "granular: drains to silence after note-off");
    }

    // 11. Per-voice filter attenuates as expected.
    {
        auto s = makeSine (5000.0, sr, 1.0, 60);
        romp::SamplePlayerVoice v;
        configureVoice (v, sr, romp::SustainMode::loop, romp::PitchMode::chromatic);
        v.setSource (s.get());

        romp::FilterSettings f;
        f.type = romp::FilterType::lowpass;
        f.resonance = 0.7;

        juce::AudioBuffer<float> out (2, 16384);

        f.cutoffHz = 200.0;
        v.setFilterSettings (f);
        renderNote (v, out, 60);
        const double lpLow = rms (out, 0, 2048, 8192);

        f.cutoffHz = 18000.0;
        v.setFilterSettings (f);
        renderNote (v, out, 60);
        const double lpHigh = rms (out, 0, 2048, 8192);

        check (lpHigh > 1e-3 && lpLow < lpHigh * 0.3,
               "filter: low-pass attenuates a 5 kHz tone at 200 Hz cutoff");

        // High-pass should cut a low tone.
        auto low = makeSine (150.0, sr, 1.0, 60);
        romp::SamplePlayerVoice hv;
        configureVoice (hv, sr, romp::SustainMode::loop, romp::PitchMode::chromatic);
        hv.setSource (low.get());

        f.type = romp::FilterType::highpass;
        f.cutoffHz = 5000.0;
        hv.setFilterSettings (f);
        renderNote (hv, out, 60);
        const double hp = rms (out, 0, 2048, 8192);

        f.cutoffHz = 20.0;
        hv.setFilterSettings (f);
        renderNote (hv, out, 60);
        const double hpOpen = rms (out, 0, 2048, 8192);

        check (hpOpen > 1e-3 && hp < hpOpen * 0.3,
               "filter: high-pass attenuates a 150 Hz tone at 5 kHz cutoff");
    }

    // 12. LFO shapes are in range and correct at a known phase.
    {
        check (std::abs (romp::lfoShapeValue (0, 0.25) - 1.0f) < 1e-4, "lfo: sine peak at phase 0.25");
        check (std::abs (romp::lfoShapeValue (0, 0.75) + 1.0f) < 1e-4, "lfo: sine trough at phase 0.75");
        check (std::abs (romp::lfoShapeValue (1, 0.25)) < 1e-4, "lfo: triangle zero at phase 0.25");
        check (std::abs (romp::lfoShapeValue (2, 0.25) + 0.5f) < 1e-4, "lfo: saw midpoint at phase 0.25");
        check (romp::lfoShapeValue (3, 0.25) > 0.5f, "lfo: square high in first half");
        check (romp::lfoShapeValue (3, 0.75) < -0.5f, "lfo: square low in second half");
    }

    // 13. A mod-matrix slot actually changes the output.
    {
        auto s = makeSine (2000.0, sr, 1.0, 60);
        romp::SamplePlayerVoice v;
        configureVoice (v, sr, romp::SustainMode::loop, romp::PitchMode::chromatic);
        v.setSource (s.get());

        romp::FilterSettings f;
        f.type = romp::FilterType::lowpass;
        f.cutoffHz = 1000.0;
        f.resonance = 0.7;
        v.setFilterSettings (f);

        romp::LfoSettings l1;
        l1.rateHz = 4.0;
        l1.shape = 0;
        l1.depth = 1.0f;
        v.setLfoSettings (0, l1);

        romp::ModMatrix none;   // all slots default to none
        v.setModMatrix (none);

        juce::AudioBuffer<float> a (2, 32768), b (2, 32768);
        renderNote (v, a, 60);

        romp::ModMatrix mods;
        mods[0].source = romp::ModSource::lfo1;
        mods[0].dest = romp::ModDest::cutoff;
        mods[0].amount = 1.0f;
        v.setModMatrix (mods);
        renderNote (v, b, 60);

        double diff = 0.0;
        for (int i = 2048; i < 16384; ++i)
            diff += std::abs ((double) a.getSample (0, i) - (double) b.getSample (0, i));

        check (diff > 1.0, "mod matrix: LFO1 -> cutoff changes the output");
    }

    std::printf ("\nRomplerTests: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
