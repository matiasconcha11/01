#include "Spectrogram.h"
#include "LookAndFeel.h"
#include "PluginProcessor.h"

Spectrogram::Spectrogram (ZeroOneProcessor& p) : processor (p)
{
    setOpaque (true);
    startTimerHz (30);
}

void Spectrogram::setBand (float lowHz, float highHz)
{
    if (juce::exactlyEqual (bandLow, lowHz) && juce::exactlyEqual (bandHigh, highHz))
        return;
    bandLow = lowHz;
    bandHigh = highHz;
    if (! fullSpectrum && draggingEdge == 0)
    {
        image = {};
        resized();
    }
    repaint();
}

void Spectrogram::setFullSpectrum (bool full)
{
    if (fullSpectrum == full) return;
    fullSpectrum = full;
    image = {};
    resized();
    repaint();
}

void Spectrogram::setMessageOnly (bool only)
{
    if (messageOnly == only) return;
    messageOnly = only;
    std::fill (history.begin(), history.end(), 0.0f);
    image = {};
    resized();
    repaint();
}

void Spectrogram::mouseMove (const juce::MouseEvent& event)
{
    const auto span = juce::jmax (1.0f, getViewHigh() - getViewLow());
    const auto yFor = [this, span] (float hz) { return (getViewHigh() - hz) / span * (float) getHeight(); };
    const auto near = std::abs ((float) event.y - yFor (bandLow)) < 10.0f
                   || std::abs ((float) event.y - yFor (bandHigh)) < 10.0f;
    setMouseCursor (near ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
}

void Spectrogram::mouseDown (const juce::MouseEvent& event)
{
    const auto span = juce::jmax (1.0f, getViewHigh() - getViewLow());
    const auto yFor = [this, span] (float hz) { return (getViewHigh() - hz) / span * (float) getHeight(); };
    const auto lowDistance = std::abs ((float) event.y - yFor (bandLow));
    const auto highDistance = std::abs ((float) event.y - yFor (bandHigh));
    const auto viewLow = getViewLow(), viewHigh = getViewHigh();
    draggingEdge = juce::jmin (lowDistance, highDistance) <= 12.0f
        ? (lowDistance < highDistance ? -1 : 1) : 0;
    if (draggingEdge != 0 && onBandEdgeBegin)
    {
        dragViewLow = viewLow;
        dragViewHigh = viewHigh;
        onBandEdgeBegin (draggingEdge < 0);
    }
}

void Spectrogram::mouseDrag (const juce::MouseEvent& event)
{
    if (draggingEdge == 0 || ! onBandEdgeDrag) return;
    const auto hz = getViewHigh() - (float) event.y / (float) juce::jmax (1, getHeight())
                                 * (getViewHigh() - getViewLow());
    const auto low = draggingEdge < 0;
    onBandEdgeDrag (low, juce::jlimit (200.0f, 20000.0f,
        low ? juce::jmin (hz, bandHigh - 200.0f) : juce::jmax (hz, bandLow + 200.0f)));
}

void Spectrogram::mouseUp (const juce::MouseEvent&)
{
    const auto wasDragging = draggingEdge != 0;
    if (draggingEdge != 0 && onBandEdgeEnd)
        onBandEdgeEnd (draggingEdge < 0);
    draggingEdge = 0;
    if (wasDragging && ! fullSpectrum)
    {
        image = {};
        resized();
        repaint();
    }
}

float Spectrogram::getViewLow() const
{
    if (draggingEdge != 0) return dragViewLow;
    if (fullSpectrum) return 0.0f;
    const auto padding = juce::jmax (500.0f, (bandHigh - bandLow) * 0.12f);
    return juce::jmax (0.0f, bandLow - padding);
}

float Spectrogram::getViewHigh() const
{
    if (draggingEdge != 0) return dragViewHigh;
    const auto nyquist = (float) processor.sampleRateForDisplay.load() * 0.5f;
    if (fullSpectrum) return nyquist;
    const auto padding = juce::jmax (500.0f, (bandHigh - bandLow) * 0.12f);
    return juce::jmin (nyquist, bandHigh + padding);
}

juce::Colour Spectrogram::colourFor (float level)
{
    level = juce::jlimit (0.0f, 1.0f, level);

    struct Stop { float at; juce::Colour colour; };
    static const Stop stops[] { { 0.0f, Theme::background },
                                { 0.35f, Theme::violet.darker (0.4f) },
                                { 0.6f, Theme::amber },
                                { 0.82f, Theme::honey },
                                { 1.0f, Theme::text } };

    for (size_t i = 1; i < std::size (stops); ++i)
        if (level <= stops[i].at)
        {
            const auto amount = (level - stops[i - 1].at) / (stops[i].at - stops[i - 1].at);
            return stops[i - 1].colour.interpolatedWith (stops[i].colour, amount);
        }

    return stops[std::size (stops) - 1].colour;
}

void Spectrogram::resized()
{
    const auto width = juce::jmax (1, getWidth());
    const auto height = juce::jmax (1, getHeight());

    if (image.getWidth() != width || image.getHeight() != height)
    {
        image = juce::Image (juce::Image::RGB, width, height, true);
        juce::Graphics g (image);
        g.fillAll (Theme::background);
    }
}

void Spectrogram::update()
{
    auto& fifo = processor.scopeFifo;
    auto newColumns = false;

    while (fifo.getNumReady() > 0)
    {
        const auto wanted = juce::jmin (fifo.getNumReady(), hopSize - samplesSinceColumn);
        const auto read = fifo.read (wanted);
        const auto copy = [&] (int start, int size)
        {
            // Shift the history left and append; fftSize is small enough for this to be cheap.
            std::move (history.begin() + size, history.end(), history.begin());
            const auto& source = messageOnly ? processor.messageScopeBuffer : processor.scopeBuffer;
            std::copy_n (source.begin() + start, size, history.end() - size);
        };

        copy (read.startIndex1, read.blockSize1);
        copy (read.startIndex2, read.blockSize2);
        samplesSinceColumn += read.blockSize1 + read.blockSize2;

        if (samplesSinceColumn >= hopSize)
        {
            samplesSinceColumn = 0;
            drawColumn();
            newColumns = true;
        }
    }

    if (newColumns)
        repaint();
}

void Spectrogram::drawColumn()
{
    if (! image.isValid())
        return;

    std::fill (fftData.begin(), fftData.end(), 0.0f);
    std::copy (history.begin(), history.end(), fftData.begin());
    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    const auto width = image.getWidth();
    const auto height = image.getHeight();
    image.moveImageSection (0, 0, 1, 0, width - 1, height);

    const auto bins = fftSize / 2;
    const auto lowHz = getViewLow(), highHz = getViewHigh();
    const auto binPerHz = (float) fftSize / (float) processor.sampleRateForDisplay.load();
    const auto normalise = 2.0f / (float) fftSize;
    constexpr auto floorDb = -110.0f;

    for (int y = 0; y < height; ++y)
    {
        const auto hz = juce::jmap ((float) y / (float) height, highHz, lowHz);
        const auto bin = juce::jlimit (0, bins - 1, juce::roundToInt (hz * binPerHz));
        const auto db = juce::Decibels::gainToDecibels (fftData[(size_t) bin] * normalise, floorDb);
        const auto level = juce::jlimit (0.0f, 1.0f, juce::jmap (db,
            messageOnly ? -105.0f : -100.0f, messageOnly ? -48.0f : -40.0f, 0.0f, 1.0f));
        image.setPixelAt (width - 1, y, colourFor (std::pow (level, 1.4f)));
    }
}

void Spectrogram::paint (juce::Graphics& g)
{
    g.drawImageAt (image, 0, 0);

    const auto lowHz = getViewLow(), highHz = getViewHigh();
    const auto span = juce::jmax (1.0f, highHz - lowHz);
    const auto yFor = [&] (float hz) { return (highHz - hz) / span * (float) getHeight(); };
    const auto top = yFor (juce::jmax (bandLow, bandHigh));
    const auto bottom = yFor (juce::jmin (bandLow, bandHigh));

    g.setColour (Theme::honey.withAlpha (0.35f));
    const float dashes[] { 4.0f, 4.0f };

    for (auto y : { top, bottom })
        if (y >= 0.0f && y <= (float) getHeight())
            g.drawDashedLine ({ 0.0f, y, (float) getWidth(), y }, dashes, 2, 1.0f);

    if (getWidth() > 240)
    {
        g.setFont (Theme::caps (10.0f));
        g.setColour (Theme::honey.withAlpha (0.8f));
        if (top > 12.0f && top < (float) getHeight() - 8.0f)
            g.drawText ("HIGH  drag", getWidth() - 92, (int) top - 15, 86, 14, juce::Justification::centredRight);
        if (bottom > 12.0f && bottom < (float) getHeight() - 8.0f)
            g.drawText ("LOW  drag", getWidth() - 92, (int) bottom + 2, 86, 14, juce::Justification::centredRight);
    }

    g.setFont (Theme::font (11.0f));
    g.setColour (Theme::dim);

    for (int tick = 0; tick <= 4; ++tick)
    {
        const auto hz = lowHz + span * (float) tick / 4.0f;
        const auto y = yFor (hz);
        const auto label = hz >= 1000.0f ? juce::String (hz / 1000.0f, 1) + "k" : juce::String (juce::roundToInt (hz));
        g.setColour (Theme::text.withAlpha (0.7f));
        g.drawText (label, 8, juce::jlimit (2, juce::jmax (2, getHeight() - 18), (int) y - 9), 52, 18,
                    juce::Justification::centredLeft);
    }

}
