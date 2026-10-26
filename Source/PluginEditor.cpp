#include "PluginEditor.h"

namespace
{
    constexpr int margin = 16;
    constexpr int headerHeight = 60;
    constexpr int controlsHeight = 118;

    struct BandPreset
    {
        const char* name;
        float low, high, level;
    };

    // Lossy streaming (AAC/Ogg) cuts most of what's above ~16 kHz, so only the first band survives everywhere.
    const BandPreset bands[] {
        { "Hidden - 11 to 16 kHz", 11000.0f, 16000.0f, -30.0f },
        { "Faint - 16 to 19 kHz (lossless only)", 16000.0f, 19000.0f, -36.0f },
        { "Visible - 2 to 12 kHz", 2000.0f, 12000.0f, -24.0f },
        { "Low - 300 Hz to 4 kHz", 300.0f, 4000.0f, -24.0f },
    };

    void setParameter (ZeroOneProcessor& p, const juce::String& id, float value)
    {
        auto* parameter = p.apvts.getParameter (id);
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        parameter->endChangeGesture();
    }
}

//==============================================================================
MessagePreview::MessagePreview (ZeroOneProcessor& p) : processor (p)
{
    setOpaque (true);
    refresh();
}

void MessagePreview::refresh()
{
    const auto message = processor.getMessage();

    if (message == nullptr || message->isEmpty())
    {
        picture = {};
        repaint();
        return;
    }

    picture = juce::Image (juce::Image::ARGB, message->columns, Message::rows, true);

    for (int row = 0; row < Message::rows; ++row)
        for (int column = 0; column < message->columns; ++column)
            picture.setPixelAt (column, row, Spectrogram::colourFor (0.15f + 0.85f * message->at (row, column)));

    repaint();
}

void MessagePreview::setPosition (float newPosition)
{
    if (! juce::exactlyEqual (newPosition, position))
    {
        position = newPosition;
        repaint();
    }
}

void MessagePreview::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    const auto area = getLocalBounds().toFloat().reduced (8.0f, 6.0f);

    if (! picture.isValid())
    {
        g.setColour (Theme::dim);
        g.setFont (Theme::font (13.0f));
        g.drawText ("Type a message, or drop an image here", area, juce::Justification::centred);
        return;
    }

    // Stretched to the width, the way it stretches over the bars it plays across.
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    g.drawImage (picture, area, juce::RectanglePlacement::stretchToFit);

    if (position >= 0.0f)
    {
        const auto x = area.getX() + position * area.getWidth();
        g.setColour (Theme::text.withAlpha (0.12f));
        g.fillRect (juce::Rectangle<float> (area.getX(), area.getY(), x - area.getX(), area.getHeight()));
        g.setColour (Theme::text);
        g.drawLine (x, area.getY() - 2.0f, x, area.getBottom() + 2.0f, 1.5f);
    }
}

//==============================================================================
ZeroOneEditor::ZeroOneEditor (ZeroOneProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      preview (p),
      spectrogram (p),
      lengthSelector (*p.apvts.getParameter (Params::length), { "1", "2", "4", "8", "16" }),
      playSelector (*p.apvts.getParameter (Params::play), Params::playNames())
{
    setLookAndFeel (&lookAndFeel);

    messageBox.setFont (Theme::font (17.0f));
    messageBox.setJustification (juce::Justification::centredLeft);
    messageBox.setIndents (10, 0);
    messageBox.setTextToShowWhenEmpty ("Your message", Theme::faint);
    messageBox.setInputRestrictions (64);
    messageBox.setText (processor.getMessageText(), false);
    messageBox.onTextChange = [this]
    {
        processor.setMessageText (messageBox.getText());
        showSource();
    };
    messageBox.setTooltip ("What to write into the spectrum. Keep it short: every letter shares the same bars.");
    addAndMakeVisible (messageBox);

    imageButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Pick an image to write into the spectrum", juce::File(),
                                                       "*.png;*.jpg;*.jpeg;*.gif");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().existsAsFile())
                                      loadImage (fc.getResult());
                              });
    };
    imageButton.setTooltip ("Write an image instead: bright parts sound, dark parts stay quiet. You can also drop one on the window.");
    addAndMakeVisible (imageButton);

    sourceLabel.setFont (Theme::font (13.0f));
    sourceLabel.setColour (juce::Label::textColourId, Theme::honey);
    addChildComponent (sourceLabel);

    for (int i = 0; i < (int) std::size (bands); ++i)
        bandPresets.addItem (bands[i].name, i + 1);

    bandPresets.setTextWhenNothingSelected ("Band presets");
    bandPresets.onChange = [this]
    {
        applyBandPreset (bandPresets.getSelectedItemIndex());
        bandPresets.setSelectedItemIndex (-1, juce::dontSendNotification);
    };
    addAndMakeVisible (bandPresets);

    addAndMakeVisible (preview);
    addAndMakeVisible (spectrogram);

    for (auto* knob : { &level, &low, &high })
    {
        knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 84, 18);
        addAndMakeVisible (knob);
    }

    level.setTooltip ("How loud the message is. Under a full mix, -30 dB in the top band is very hard to hear.");
    low.setTooltip ("The lowest frequency the message uses: the bottom of the letters.");
    high.setTooltip ("The highest frequency the message uses: the top of the letters.");
    levelAttachment = std::make_unique<SliderAttachment> (processor.apvts, Params::level, level);
    lowAttachment = std::make_unique<SliderAttachment> (processor.apvts, Params::low, low);
    highAttachment = std::make_unique<SliderAttachment> (processor.apvts, Params::high, high);

    startBar.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 48, 26);
    startBar.setTooltip ("The bar the message starts on (with Play set to Transport).");
    startAttachment = std::make_unique<SliderAttachment> (processor.apvts, Params::start, startBar);
    addAndMakeVisible (startBar);

    gap.addItemList (Params::gapNames(), 1);
    gap.setTooltip ("Silence between repeats.");
    gapAttachment = std::make_unique<ComboBoxAttachment> (processor.apvts, Params::gap, gap);
    addAndMakeVisible (gap);

    for (auto* button : { &repeatButton, &soloButton, &bypassButton })
    {
        button->setClickingTogglesState (true);
        addAndMakeVisible (button);
    }

    repeatButton.setTooltip ("Play the message again after the gap, for as long as the song runs.");
    soloButton.setTooltip ("Mute the track and hear only the message.");
    repeatAttachment = std::make_unique<ButtonAttachment> (processor.apvts, Params::repeat, repeatButton);
    soloAttachment = std::make_unique<ButtonAttachment> (processor.apvts, Params::solo, soloButton);
    bypassAttachment = std::make_unique<ButtonAttachment> (processor.apvts, Params::bypass, bypassButton);

    lengthSelector.setTooltip ("How many bars the whole message is stretched over.");
    playSelector.setTooltip ("Transport: plays from the start bar when the song plays, so bounces always match. "
                             "Free: loops all the time, even when stopped, for trying things out.");
    addAndMakeVisible (lengthSelector);
    addAndMakeVisible (playSelector);

    showSource();
    shownMessage = processor.getMessage();

    setResizable (true, true);
    setResizeLimits (760, 520, 1600, 1100);
    setSize (900, 600);
    startTimerHz (30);
}

ZeroOneEditor::~ZeroOneEditor()
{
    setLookAndFeel (nullptr);
}

void ZeroOneEditor::showSource()
{
    const auto name = processor.getImageName();
    sourceLabel.setText ("Image: " + name, juce::dontSendNotification);
    sourceLabel.setVisible (name.isNotEmpty());
    messageBox.setAlpha (name.isNotEmpty() ? 0.45f : 1.0f);
}

void ZeroOneEditor::loadImage (const juce::File& file)
{
    const auto image = juce::ImageFileFormat::loadFrom (file);

    if (! image.isValid())
        return;

    processor.setMessageImage (image, file.getFileName());
    showSource();
}

void ZeroOneEditor::applyBandPreset (int index)
{
    if (index < 0 || index >= (int) std::size (bands))
        return;

    const auto& preset = bands[(size_t) index];
    setParameter (processor, Params::low, preset.low);
    setParameter (processor, Params::high, preset.high);
    setParameter (processor, Params::level, preset.level);
}

bool ZeroOneEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& file : files)
        if (file.endsWithIgnoreCase (".png") || file.endsWithIgnoreCase (".jpg") || file.endsWithIgnoreCase (".jpeg")
            || file.endsWithIgnoreCase (".gif"))
            return true;

    return false;
}

void ZeroOneEditor::filesDropped (const juce::StringArray& files, int, int)
{
    if (! files.isEmpty())
        loadImage (juce::File (files[0]));
}

void ZeroOneEditor::timerCallback()
{
    const auto message = processor.getMessage();

    if (message != shownMessage)
    {
        shownMessage = message;
        preview.refresh();
    }

    // Setting the text again after a preset/state load keeps the box in sync.
    if (processor.getImageName().isEmpty() && ! messageBox.hasKeyboardFocus (true)
        && messageBox.getText() != processor.getMessageText())
        messageBox.setText (processor.getMessageText(), false);

    showSource();
    preview.setPosition (processor.displayPosition.load());
    spectrogram.setBand (processor.apvts.getRawParameterValue (Params::low)->load(),
                         processor.apvts.getRawParameterValue (Params::high)->load());
}

void ZeroOneEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    auto header = getLocalBounds().reduced (margin, 0).removeFromTop (headerHeight);
    g.setColour (Theme::honey);
    g.setFont (Theme::serif (38.0f));
    g.drawText ("01", header.removeFromLeft (52), juce::Justification::centredLeft);

    g.setColour (Theme::dim);
    g.setFont (Theme::font (13.0f));
    g.drawText ("hidden messages in the spectrum", header.removeFromLeft (260).withTrimmedTop (8),
                juce::Justification::centredLeft);

    g.setFont (Theme::caps (9.5f));
    g.setColour (Theme::dim);

    for (const auto& [area, text] : captions)
        g.drawText (text, area, juce::Justification::centredLeft);

    g.setColour (Theme::outline);
    g.drawHorizontalLine (controlsArea.getY() - 8, (float) margin, (float) (getWidth() - margin));
}

void ZeroOneEditor::resized()
{
    captions.clear();
    auto bounds = getLocalBounds().reduced (margin, 0);

    auto header = bounds.removeFromTop (headerHeight);
    bypassButton.setBounds (header.removeFromRight (80).withSizeKeepingCentre (80, 28));
    header.removeFromRight (8);
    bandPresets.setBounds (header.removeFromRight (250).withSizeKeepingCentre (250, 28));

    auto messageRow = bounds.removeFromTop (38);
    imageButton.setBounds (messageRow.removeFromRight (96));
    messageRow.removeFromRight (8);
    messageBox.setBounds (messageRow);
    sourceLabel.setBounds (messageRow.reduced (10, 0).removeFromRight (260));

    bounds.removeFromTop (10);
    preview.setBounds (bounds.removeFromTop (64));
    bounds.removeFromTop (8);

    controlsArea = bounds.removeFromBottom (controlsHeight).withTrimmedBottom (12);
    bounds.removeFromBottom (16);
    spectrogram.setBounds (bounds);

    auto controls = controlsArea;
    const auto addCaption = [this] (juce::Rectangle<int>& column, const juce::String& text)
    {
        captions.push_back ({ column.removeFromTop (16), text });
        column.removeFromTop (4);
    };

    for (auto [knob, name] : { std::pair { &level, "LEVEL" }, std::pair { &low, "LOW" }, std::pair { &high, "HIGH" } })
    {
        auto column = controls.removeFromLeft (84);
        addCaption (column, name);
        knob->setBounds (column);
        controls.removeFromLeft (10);
    }

    controls.removeFromLeft (14);
    auto timing = controls.removeFromLeft (juce::jmin (330, controls.getWidth() / 2));
    auto top = timing.removeFromTop (timing.getHeight() / 2);
    addCaption (top, "LENGTH (BARS)");
    lengthSelector.setBounds (top.removeFromTop (28));

    auto bottom = timing;
    auto startColumn = bottom.removeFromLeft (110);
    addCaption (startColumn, "START BAR");
    startBar.setBounds (startColumn.removeFromTop (28));
    bottom.removeFromLeft (12);
    auto repeatColumn = bottom.removeFromLeft (bottom.getWidth());
    addCaption (repeatColumn, "REPEAT / GAP");
    auto repeatRow = repeatColumn.removeFromTop (28);
    repeatButton.setBounds (repeatRow.removeFromLeft (repeatRow.getWidth() / 2 - 3));
    repeatRow.removeFromLeft (6);
    gap.setBounds (repeatRow);

    controls.removeFromLeft (24);
    auto playColumn = controls;
    auto playTop = playColumn.removeFromTop (playColumn.getHeight() / 2);
    addCaption (playTop, "PLAY");
    playSelector.setBounds (playTop.removeFromTop (28));
    addCaption (playColumn, "LISTEN");
    soloButton.setBounds (playColumn.removeFromTop (28).removeFromLeft (100));
}
