#include "PluginEditor.h"

namespace
{
    constexpr int margin = 16;
    constexpr int headerHeight = 66;
    constexpr int controlsHeight = 138;

    struct BandPreset
    {
        const char* name;
        float low, high, level;
    };

    // Delivery codecs vary; the final export must be checked in a spectrogram.
    const BandPreset bands[] {
        { "Hidden - 11 to 16 kHz", 11000.0f, 16000.0f, -30.0f },
        { "Faint - 16 to 19 kHz", 16000.0f, 19000.0f, -36.0f },
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

    juce::RangedAudioParameter* parameterForEdge (ZeroOneProcessor& p, bool lowEdge)
    {
        const auto rawLow = p.apvts.getRawParameterValue (Params::low)->load();
        const auto rawHigh = p.apvts.getRawParameterValue (Params::high)->load();
        return p.apvts.getParameter (lowEdge == (rawLow <= rawHigh) ? Params::low : Params::high);
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
        scaledPicture = {};
        repaint();
        return;
    }

    picture = juce::Image (juce::Image::ARGB, message->columns, Message::rows, true);

    for (int row = 0; row < Message::rows; ++row)
        for (int column = 0; column < message->columns; ++column)
            picture.setPixelAt (column, row, Spectrogram::colourFor (0.15f + 0.85f * message->at (row, column)));

    updateScaledPicture();
    repaint();
}

void MessagePreview::resized()
{
    updateScaledPicture();
}

void MessagePreview::updateScaledPicture()
{
    const auto area = getLocalBounds().reduced (8, 6);
    scaledPicture = {};

    if (picture.isValid() && ! area.isEmpty())
    {
        const auto scale = juce::jmin ((float) area.getWidth() / (float) picture.getWidth(),
                                      (float) area.getHeight() / (float) picture.getHeight());
        scaledPicture = picture.rescaled (juce::jmax (1, juce::roundToInt ((float) picture.getWidth() * scale)),
                                          juce::jmax (1, juce::roundToInt ((float) picture.getHeight() * scale)),
                                          juce::Graphics::highResamplingQuality);
    }
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
    g.fillAll (picture.isValid() ? Spectrogram::colourFor (0.15f) : Theme::panel);
    const auto area = getLocalBounds().toFloat().reduced (8.0f, 6.0f);

    if (! picture.isValid())
    {
        g.setColour (Theme::dim);
        g.setFont (Theme::font (13.0f));
        g.drawText ("Type a message, or drop an image here", area, juce::Justification::centred);
        return;
    }

    if (! scaledPicture.isValid())
        return;

    // Preserve letter proportions. The playhead follows the image's own timeline.
    const auto imageArea = juce::Rectangle<float> ((float) scaledPicture.getWidth(),
                                                    (float) scaledPicture.getHeight())
                               .withCentre (area.getCentre());
    g.drawImageAt (scaledPicture, juce::roundToInt (imageArea.getX()), juce::roundToInt (imageArea.getY()));

    if (position >= 0.0f)
    {
        const auto x = imageArea.getX() + position * imageArea.getWidth();
        g.setColour (Theme::text.withAlpha (0.12f));
        g.fillRect (juce::Rectangle<float> (imageArea.getX(), imageArea.getY(), x - imageArea.getX(), imageArea.getHeight()));
        g.setColour (Theme::text);
        g.drawLine (x, imageArea.getY() - 2.0f, x, imageArea.getBottom() + 2.0f, 1.5f);
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
    spectrogram.onBandEdgeBegin = [this] (bool lowEdge)
    {
        parameterForEdge (processor, lowEdge)->beginChangeGesture();
    };
    spectrogram.onBandEdgeDrag = [this] (bool lowEdge, float hz)
    {
        auto* parameter = parameterForEdge (processor, lowEdge);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (hz));
    };
    spectrogram.onBandEdgeEnd = [this] (bool lowEdge)
    {
        parameterForEdge (processor, lowEdge)->endChangeGesture();
    };

    viewButton.onClick = [this]
    {
        const auto messageOnly = viewButton.getButtonText() != "Mix (mono)";
        spectrogram.setMessageOnly (messageOnly);
        viewButton.setButtonText (messageOnly ? "Mix (mono)" : "Message");
        scaleInfo.setText (messageOnly ? "MESSAGE  /  -105 TO -48 dBFS" : "MIX (MONO)  /  -100 TO -40 dBFS",
                           juce::dontSendNotification);
    };
    viewButton.setTooltip ("Switch between the actual processed mix and only the message added by Inscript.");
    zoomButton.onClick = [this]
    {
        const auto full = zoomButton.getButtonText() == "Full view";
        spectrogram.setFullSpectrum (full);
        zoomButton.setButtonText (full ? "Band view" : "Full view");
    };
    zoomButton.setTooltip ("Switch between the message band and the full frequency range.");
    captureButton.onClick = [this]
    {
        captureMode = ! captureMode;
        captureButton.setButtonText (captureMode ? "Edit" : "Capture");
        for (auto* component : std::array<juce::Component*, 14> { &messageBox, &imageButton, &sourceLabel,
                                 &bandPresets, &level, &low, &high, &startBar, &gap, &repeatButton,
                                 &soloButton, &bypassButton, &lengthSelector, &playSelector })
            component->setVisible (! captureMode);
        showSource();
        resized();
        repaint();
    };
    captureButton.setTooltip ("Show a larger spectrum and message preview for a screen recording.");
    for (auto* button : { &viewButton, &zoomButton, &captureButton })
        addAndMakeVisible (button);

    for (auto* label : { &timingInfo, &peakInfo, &scaleInfo })
    {
        label->setFont (Theme::font (13.0f));
        label->setColour (juce::Label::textColourId, Theme::dim);
        addAndMakeVisible (label);
    }
    scaleInfo.setFont (Theme::caps (10.0f));
    scaleInfo.setJustificationType (juce::Justification::centredRight);
    scaleInfo.setText ("MIX (MONO)  /  -100 TO -40 dBFS", juce::dontSendNotification);

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
    setResizeLimits (800, 560, 1600, 1100);
    setSize (980, 660);
    startTimerHz (30);
    timerCallback();
}

ZeroOneEditor::~ZeroOneEditor()
{
    setLookAndFeel (nullptr);
}

void ZeroOneEditor::showSource()
{
    const auto name = processor.getImageName();
    sourceLabel.setText ("Image: " + name, juce::dontSendNotification);
    sourceLabel.setVisible (name.isNotEmpty() && ! captureMode);
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
    const auto rawLow = processor.apvts.getRawParameterValue (Params::low)->load();
    const auto rawHigh = processor.apvts.getRawParameterValue (Params::high)->load();
    spectrogram.setBand (juce::jmin (rawLow, rawHigh), juce::jmax (rawLow, rawHigh));

    const auto lengthIndex = juce::roundToInt (processor.apvts.getRawParameterValue (Params::length)->load());
    const auto bars = Params::lengthBars (lengthIndex);
    const auto columns = message ? message->columns : 0;
    const auto chars = processor.getMessageText().length();
    const auto source = processor.getImageName().isNotEmpty() ? "Image" : juce::String (chars) + " characters";
    const auto space = processor.getImageName().isNotEmpty() ? juce::String (columns) + " slices"
        : juce::String ((float) bars / (float) juce::jmax (1, chars), 2) + " bar / character";
    const auto compressed = processor.getImageName().isEmpty() && chars >= 6 && (float) bars / (float) chars < 0.5f;
    timingInfo.setText (source + "  ·  " + juce::String (bars) + (bars == 1 ? " bar" : " bars")
                        + "  ·  " + space + (compressed ? "  ·  try 8 bars" : ""), juce::dontSendNotification);
    timingInfo.setColour (juce::Label::textColourId, compressed ? Theme::honey : Theme::dim);

    const auto peak = processor.outputPeakSinceRead.exchange (0.0f);
    peakHold = juce::jmax (peak, peakHold * 0.95f);
    const auto db = juce::Decibels::gainToDecibels (peakHold, -120.0f);
    peakInfo.setText (peakHold > 1.0f ? "OUTPUT OVER 0 dBFS" : "OUTPUT PEAK  " + juce::String (db, 1) + " dBFS",
                      juce::dontSendNotification);
    peakInfo.setColour (juce::Label::textColourId, peakHold > 1.0f ? Theme::amber : Theme::dim);
}

void ZeroOneEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    auto header = getLocalBounds().reduced (margin, 0).removeFromTop (headerHeight);
    g.setColour (Theme::honey);
    g.setFont (Theme::serif (38.0f));
    g.drawText ("Inscript", header.removeFromLeft (188), juce::Justification::centredLeft);

    g.setColour (Theme::dim);
    g.setFont (Theme::font (13.0f));
    g.drawText ("write into the spectrum", header.removeFromLeft (220).withTrimmedTop (8),
                juce::Justification::centredLeft);

    g.setFont (Theme::caps (11.0f));
    g.setColour (Theme::dim);

    for (const auto& [area, text] : captions)
        g.drawText (text, area, juce::Justification::centredLeft);

    g.setColour (Theme::outline);
    if (! captureMode)
        g.drawHorizontalLine (controlsArea.getY() - 8, (float) margin, (float) (getWidth() - margin));
}

void ZeroOneEditor::resized()
{
    captions.clear();
    auto bounds = getLocalBounds().reduced (margin, 0);

    auto header = bounds.removeFromTop (headerHeight);
    captureButton.setBounds (header.removeFromRight (86).withSizeKeepingCentre (86, 30));
    header.removeFromRight (8);
    bypassButton.setBounds (header.removeFromRight (80).withSizeKeepingCentre (80, 30));
    header.removeFromRight (8);
    bandPresets.setBounds (header.removeFromRight (210).withSizeKeepingCentre (210, 30));

    if (captureMode)
    {
        bounds.removeFromTop (4);
        timingInfo.setBounds (bounds.removeFromTop (24));
        bounds.removeFromTop (8);
        preview.setBounds (bounds.removeFromTop (108));
        bounds.removeFromTop (12);
        auto toolbar = bounds.removeFromTop (34);
        zoomButton.setBounds (toolbar.removeFromRight (108));
        toolbar.removeFromRight (8);
        viewButton.setBounds (toolbar.removeFromRight (100));
        peakInfo.setBounds (toolbar.removeFromLeft (220));
        scaleInfo.setBounds (toolbar);
        bounds.removeFromTop (6);
        spectrogram.setBounds (bounds.withTrimmedBottom (margin));
        return;
    }

    auto messageRow = bounds.removeFromTop (38);
    imageButton.setBounds (messageRow.removeFromRight (96));
    messageRow.removeFromRight (8);
    messageBox.setBounds (messageRow);
    sourceLabel.setBounds (messageRow.reduced (10, 0).removeFromRight (260));

    bounds.removeFromTop (8);
    timingInfo.setBounds (bounds.removeFromTop (24));
    preview.setBounds (bounds.removeFromTop (70));
    bounds.removeFromTop (10);

    auto toolbar = bounds.removeFromTop (34);
    zoomButton.setBounds (toolbar.removeFromRight (108));
    toolbar.removeFromRight (8);
    viewButton.setBounds (toolbar.removeFromRight (100));
    peakInfo.setBounds (toolbar.removeFromLeft (220));
    scaleInfo.setBounds (toolbar);
    bounds.removeFromTop (6);

    controlsArea = bounds.removeFromBottom (controlsHeight).withTrimmedBottom (12);
    bounds.removeFromBottom (12);
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
    auto timing = controls.removeFromLeft (juce::jmin (330, controls.getWidth() - 150));
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
