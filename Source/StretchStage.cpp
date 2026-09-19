#include "StretchStage.h"

namespace romp
{
    void StretchStage::prepare (double sampleRate, int maxBlockSize, int numChannels)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        channels = juce::jlimit (1, 2, numChannels);

        // ~80 ms analysis block / ~20 ms interval => ~40 ms in + ~40 ms out.
        // Lower than the 0.12/0.03 default; raise for cleaner sustained pads at
        // the cost of latency, once this is exposed as a quality control.
        const int block = juce::jmax (256, (int) (sr * 0.08));
        const int interval = juce::jmax (64, (int) (sr * 0.02));

        stretch.configure (channels, block, interval, false);
        stretch.reset();

        latencySamples = stretch.inputLatency() + stretch.outputLatency();
        active = true;

        // Pre-warm the stretcher's internal buffers at the worst-case block size
        // so process() never resizes (allocates) on the audio thread.
        const int warmN = juce::jmax (maxBlockSize, interval);

        warmIn.setSize (channels, warmN);
        warmOut.setSize (channels, warmN);
        warmIn.clear();
        warmOut.clear();

        stretch.process (warmIn.getArrayOfReadPointers(), warmN,
                         warmOut.getArrayOfWritePointers(), warmN);

        stretch.reset();
    }

    void StretchStage::reset()
    {
        if (active)
            stretch.reset();
    }

    void StretchStage::setTransposeSemitones (float semitones) noexcept
    {
        transposeSemitones = semitones;

        if (active)
            stretch.setTransposeSemitones (semitones);
    }

    void StretchStage::processBlock (const juce::AudioBuffer<float>& input,
                                     juce::AudioBuffer<float>& output,
                                     int numSamples)
    {
        if (! active || numSamples <= 0)
            return;

        stretch.process (input.getArrayOfReadPointers(), numSamples,
                         output.getArrayOfWritePointers(), numSamples);
    }
}
