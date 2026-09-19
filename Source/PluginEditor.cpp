#include "PluginEditor.h"

#include <RomplerWeb.h>

#include <cstring>

namespace romp
{
    RomplerAudioProcessorEditor::RomplerAudioProcessorEditor (RomplerAudioProcessor& p)
        : AudioProcessorEditor (&p), processor (p)
    {
        setResizable (true, true);
        setResizeLimits (720, 460, 1600, 1000);
        setSize (980, 620);

        auto opts = buildWebOptions();

        // JUCE silently falls back to a legacy backend if the requested one is
        // unavailable; check explicitly and show a real message rather than a
        // blank/broken editor (e.g. missing WebView2 runtime).
        if (juce::WebBrowserComponent::areOptionsSupported (opts))
        {
            webView = std::make_unique<juce::WebBrowserComponent> (opts);
            addAndMakeVisible (*webView);
            webView->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
        }
        else
        {
            fallback = std::make_unique<juce::Label>();
            fallback->setJustificationType (juce::Justification::centred);
            fallback->setColour (juce::Label::textColourId, juce::Colour (0xffe6e8ea));
            fallback->setText ("Chord Studio Rompler needs the Microsoft Edge WebView2 Runtime.\n"
                               "Install it from https://go.microsoft.com/fwlink/p/?LinkId=2124703 "
                               "and reopen the plugin.",
                               juce::dontSendNotification);
            addAndMakeVisible (*fallback);
        }
    }
    RomplerAudioProcessorEditor::~RomplerAudioProcessorEditor()
    {
        chooser.reset();
    }

    void RomplerAudioProcessorEditor::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colour (0xff0e0f11));
    }

    void RomplerAudioProcessorEditor::resized()
    {
        if (webView != nullptr)
            webView->setBounds (getLocalBounds());

        if (fallback != nullptr)
            fallback->setBounds (getLocalBounds().reduced (24));
    }

    juce::String RomplerAudioProcessorEditor::mimeFor (const juce::String& name)
    {
        const auto ext = name.fromLastOccurrenceOf (".", false, false).toLowerCase();

        if (ext == "html" || ext == "htm") return "text/html";
        if (ext == "js"  || ext == "mjs")  return "text/javascript";
        if (ext == "css")                  return "text/css";
        if (ext == "json")                 return "application/json";
        if (ext == "svg")                  return "image/svg+xml";
        if (ext == "png")                  return "image/png";
        if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
        if (ext == "woff2")                return "font/woff2";
        if (ext == "wasm")                 return "application/wasm";

        return "application/octet-stream";
    }

    std::optional<juce::WebBrowserComponent::Resource>
    RomplerAudioProcessorEditor::getWebResource (const juce::String& path)
    {
        auto name = path;

        if (name.isEmpty() || name == "/")
            name = "index.html";
        else
            name = name.fromLastOccurrenceOf ("/", false, false);

        for (int i = 0; i < RomplerWeb::namedResourceListSize; ++i)
        {
            if (juce::String (RomplerWeb::originalFilenames[i]).equalsIgnoreCase (name))
            {
                int size = 0;
                const char* data = RomplerWeb::getNamedResource (RomplerWeb::namedResourceList[i], size);

                if (data == nullptr)
                    break;

                std::vector<std::byte> bytes ((size_t) size);
                std::memcpy (bytes.data(), data, (size_t) size);

                return juce::WebBrowserComponent::Resource { std::move (bytes), mimeFor (name) };
            }
        }

        return std::nullopt;
    }

    void RomplerAudioProcessorEditor::openFolderChooser (juce::WebBrowserComponent::NativeFunctionCompletion done)
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a sample folder", juce::File{}, "*");

        chooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectDirectories,
                              [this, done] (const juce::FileChooser& fc)
                              {
                                  const auto result = fc.getResult();

                                  if (result.isDirectory())
                                      processor.loadFolder (result);

                                  done (processor.buildUiState());
                              });
    }

    juce::WebBrowserComponent::Options RomplerAudioProcessorEditor::buildWebOptions()
    {
        using Options    = juce::WebBrowserComponent::Options;
        using Args       = juce::Array<juce::var>;
        using Completion = juce::WebBrowserComponent::NativeFunctionCompletion;

        auto opts = Options {}
                        .withNativeIntegrationEnabled()
                        // Without this, JUCE blanks the page when the editor is
                        // hidden (FL Studio and others then show a white panel).
                        .withKeepPageLoadedWhenBrowserIsHidden()
                        .withResourceProvider ([this] (const juce::String& path)
                                               { return getWebResource (path); });

#if JUCE_WINDOWS
        auto webviewOpts = Options::WinWebView2 {}
                               .withBackgroundColour (juce::Colour (0xff0e0f11))
                               .withBuiltInErrorPageDisabled()
                               .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                                        .getChildFile ("ChordStudioRomplerWebView"));

        opts = opts.withBackend (Options::Backend::webview2)
                   .withWinWebView2Options (webviewOpts);
#endif

        opts = opts
                   .withNativeFunction ("getState", [this] (const Args&, Completion done)
                                        { done (processor.buildUiState()); })
                   .withNativeFunction ("openFolder", [this] (const Args&, Completion done)
                                        { openFolderChooser (done); })
                   .withNativeFunction ("setParam", [this] (const Args& a, Completion done)
                                        {
                                            if (a.size() >= 2)
                                                if (auto* param = processor.apvts.getParameter (a[0].toString()))
                                                    param->setValueNotifyingHost ((float) (double) a[1]);

                                            done (processor.buildUiState());
                                        })
                   .withNativeFunction ("selectSample", [this] (const Args& a, Completion done)
                                        {
                                            processor.selectBrowserSample (a.isEmpty() ? 0 : (int) a[0]);
                                            done (processor.buildUiState());
                                        })
                   .withNativeFunction ("nextSample", [this] (const Args&, Completion done)
                                        {
                                            processor.stepBrowserSample (1);
                                            done (processor.buildUiState());
                                        })
                   .withNativeFunction ("prevSample", [this] (const Args&, Completion done)
                                        {
                                            processor.stepBrowserSample (-1);
                                            done (processor.buildUiState());
                                        });

        return opts;
    }
}
