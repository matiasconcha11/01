#pragma once

#include <juce_graphics/juce_graphics.h>

// The picture a message paints into the spectrum: rows are frequencies (row 0 = the highest),
// columns are moments in time, and each cell is how loud that frequency is at that moment (0-1).
struct Message
{
    static constexpr int rows = 48;

    int columns = 0;
    std::vector<float> cells;   // row-major, rows * columns

    float at (int row, int column) const { return cells[(size_t) (row * columns + column)]; }
    bool isEmpty() const { return columns == 0; }

    // Draws the text in a bold face, one pixel per cell, with a little silence on both sides.
    static Message fromText (const juce::String& text);

    // Scales the image to the message height (keeping its aspect ratio) and uses its brightness.
    static Message fromImage (const juce::Image& image);

    juce::MemoryBlock toMemory() const;
    static Message fromMemory (const juce::MemoryBlock&);
};
