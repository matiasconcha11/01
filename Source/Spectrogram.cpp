#include "Spectrogram.h"
#include "LookAndFeel.h"
#include "PluginProcessor.h"

Spectrogram::Spectrogram (ZeroOneProcessor& p) : processor (p)
{
    setOpaque (true);
    startTimerHz (60);
}

void Spectrogram::setBand (float lowHz, float highHz)
{
    bandLow = lowHz;
    bandHigh = highHz;
    repaint();
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
            std::copy_n (processor.scopeBuffer.begin() + start, size, history.end() - size);
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
    const auto normalise = 2.0f / (float) fftSize;
    constexpr auto floorDb = -110.0f;

    for (int y = 0; y < height; ++y)
    {
        // Top row = Nyquist, bottom row = 0 Hz.
        const auto bin = juce::jlimit (0, bins - 1, (int) ((1.0f - (float) y / (float) height) * (float) bins));
        const auto db = juce::Decibels::gainToDecibels (fftData[(size_t) bin] * normalise, floorDb);
        // Quiet stuff stays near the night background; the curve lifts only what's clearly there.
        const auto level = juce::jlimit (0.0f, 1.0f, juce::jmap (db, -100.0f, -40.0f, 0.0f, 1.0f));
        image.setPixelAt (width - 1, y, colourFor (std::pow (level, 1.4f)));
    }
}

void Spectrogram::paint (juce::Graphics& g)
{
    g.drawImageAt (image, 0, 0);

    // The band the message is written in.
    const auto nyquist = (float) processor.sampleRateForDisplay.load() * 0.5f;
    const auto yFor = [&] (float hz) { return (1.0f - hz / nyquist) * (float) getHeight(); };
    const auto top = yFor (juce::jmax (bandLow, bandHigh));
    const auto bottom = yFor (juce::jmin (bandLow, bandHigh));

    g.setColour (Theme::honey.withAlpha (0.35f));
    const float dashes[] { 4.0f, 4.0f };

    for (auto y : { top, bottom })
        g.drawDashedLine ({ 0.0f, y, (float) getWidth(), y }, dashes, 2, 1.0f);

    g.setFont (Theme::font (11.0f));
    g.setColour (Theme::dim);

    for (auto khz : { 5, 10, 15, 20 })
    {
        const auto y = yFor ((float) khz * 1000.0f);

        if (y > 8.0f)
            g.drawText (juce::String (khz) + "k", 6, (int) y - 7, 40, 14, juce::Justification::centredLeft);
    }
}
