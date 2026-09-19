#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace romp
{
    class RomplerAudioProcessorEditor : public juce::AudioProcessorEditor
    {
    public:
        explicit RomplerAudioProcessorEditor (RomplerAudioProcessor&);
        ~RomplerAudioProcessorEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void openFolderChooser (juce::WebBrowserComponent::NativeFunctionCompletion done);
        juce::WebBrowserComponent::Options buildWebOptions();
        std::optional<juce::WebBrowserComponent::Resource> getWebResource (const juce::String& path);
        static juce::String mimeFor (const juce::String& name);

        RomplerAudioProcessor& processor;
        std::unique_ptr<juce::WebBrowserComponent> webView;
        std::unique_ptr<juce::Label> fallback;
        std::unique_ptr<juce::FileChooser> chooser;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RomplerAudioProcessorEditor)
    };
}
