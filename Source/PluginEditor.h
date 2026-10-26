#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ChoiceSelector.h"
#include "LookAndFeel.h"
#include "PluginProcessor.h"
#include "Spectrogram.h"

// The message as it will be written: a strip of warm light, with a playhead while it plays.
class MessagePreview : public juce::Component
{
public:
    explicit MessagePreview (ZeroOneProcessor&);

    void refresh();
    void setPosition (float);
    void paint (juce::Graphics&) override;

private:
    ZeroOneProcessor& processor;
    juce::Image picture;
    float position = -1.0f;
};

class ZeroOneEditor : public juce::AudioProcessorEditor,
                      public juce::FileDragAndDropTarget,
                      private juce::Timer
{
public:
    explicit ZeroOneEditor (ZeroOneProcessor&);
    ~ZeroOneEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    void timerCallback() override;
    void loadImage (const juce::File&);
    void applyBandPreset (int index);
    void showSource();

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    ZeroOneProcessor& processor;
    ZeroOneLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    juce::TextEditor messageBox;
    juce::TextButton imageButton { "Image..." };
    juce::Label sourceLabel;
    juce::ComboBox bandPresets;
    std::unique_ptr<juce::FileChooser> chooser;

    MessagePreview preview;
    Spectrogram spectrogram;

    juce::Slider level { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Slider low { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Slider high { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Slider startBar { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::ComboBox gap;
    juce::TextButton repeatButton { "Repeat" }, soloButton { "Solo" }, bypassButton { "Bypass" };
    ChoiceSelector lengthSelector, playSelector;

    std::unique_ptr<SliderAttachment> levelAttachment, lowAttachment, highAttachment, startAttachment;
    std::unique_ptr<ComboBoxAttachment> gapAttachment;
    std::unique_ptr<ButtonAttachment> repeatAttachment, soloAttachment, bypassAttachment;

    juce::Rectangle<int> controlsArea;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> captions;
    std::shared_ptr<const Message> shownMessage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZeroOneEditor)
};
