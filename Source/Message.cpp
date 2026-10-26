#include "Message.h"

namespace
{
    constexpr int maxColumns = 1024;

    Message fromBrightness (const juce::Image& image, float gamma)
    {
        Message message;
        message.columns = image.getWidth();
        message.cells.resize ((size_t) (Message::rows * message.columns));

        const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);

        for (int row = 0; row < Message::rows; ++row)
            for (int column = 0; column < message.columns; ++column)
            {
                const auto brightness = pixels.getPixelColour (column, row).getPerceivedBrightness();
                message.cells[(size_t) (row * message.columns + column)] = std::pow (brightness, gamma);
            }

        return message;
    }
}

Message Message::fromText (const juce::String& text)
{
    const auto trimmed = text.trim();

    if (trimmed.isEmpty())
        return {};

    const auto margin = rows / 3;
    // Keep the complete line inside the stored image, including long messages.
    auto font = juce::Font (juce::FontOptions ((float) rows * 1.15f, juce::Font::bold));
    auto textWidth = juce::GlyphArrangement::getStringWidth (font, trimmed);
    const auto availableWidth = (float) (maxColumns - 2 * margin);

    if (textWidth > availableWidth)
    {
        font = font.withHeight (font.getHeight() * availableWidth / textWidth);
        textWidth = juce::GlyphArrangement::getStringWidth (font, trimmed);
    }

    const auto width = juce::jlimit (1, maxColumns, (int) std::ceil (textWidth) + 2 * margin);

    juce::Image image (juce::Image::RGB, width, rows, true, juce::SoftwareImageType());

    {
        juce::Graphics g (image);
        g.fillAll (juce::Colours::black);
        g.setColour (juce::Colours::white);
        g.setFont (font);
        g.drawText (trimmed, juce::Rectangle<int> (width, rows), juce::Justification::centred, false);
    }

    return fromBrightness (image, 1.0f);
}

Message Message::fromImage (const juce::Image& image)
{
    if (! image.isValid() || image.getHeight() == 0)
        return {};

    const auto aspect = (double) image.getWidth() / (double) image.getHeight();
    const auto width = juce::jlimit (1, maxColumns, juce::roundToInt (rows * aspect));
    const auto scaled = image.convertedToFormat (juce::Image::RGB)
                             .rescaled (width, rows, juce::Graphics::highResamplingQuality);

    // Squaring the brightness keeps dark backgrounds quiet, so the shapes stand out.
    return fromBrightness (scaled, 2.0f);
}

juce::MemoryBlock Message::toMemory() const
{
    juce::MemoryOutputStream stream;
    stream.writeInt (columns);

    for (auto cell : cells)
        stream.writeByte ((char) juce::roundToInt (juce::jlimit (0.0f, 1.0f, cell) * 255.0f));

    return stream.getMemoryBlock();
}

Message Message::fromMemory (const juce::MemoryBlock& block)
{
    juce::MemoryInputStream stream (block, false);
    Message message;
    const auto columns = stream.readInt();

    if (columns <= 0 || columns > maxColumns || stream.getNumBytesRemaining() < (juce::int64) rows * columns)
        return {};

    message.columns = columns;
    message.cells.resize ((size_t) (rows * columns));

    for (auto& cell : message.cells)
        cell = (float) (juce::uint8) stream.readByte() / 255.0f;

    return message;
}
