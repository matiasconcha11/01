#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>

class ZeroOneProcessor;

// A scrolling spectrogram of the plugin's output: the place where the message shows up.
// Frequencies run linearly from 0 Hz at the bottom to half the sample rate at the top,
// the way most spectrogram views draw them, so letters look the same here as anywhere else.
class Spectrogram : public juce::Component, private juce::Timer
{
public:
    explicit Spectrogram (ZeroOneProcessor&);

    void setBand (float lowHz, float highHz);

    // Draws whatever audio has arrived since the last call (the timer calls this 60 times a second).
    void update();

    void paint (juce::Graphics&) override;
    void resized() override;

    // Black at silence, through night violet and amber to bone white at full level.
    static juce::Colour colourFor (float level);

private:
    void timerCallback() override { update(); }
    void drawColumn();

    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int hopSize = 512;

    ZeroOneProcessor& processor;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };

    std::vector<float> history = std::vector<float> (fftSize);   // last fftSize samples, oldest first
    std::vector<float> fftData = std::vector<float> (fftSize * 2);
    int samplesSinceColumn = 0;

    juce::Image image;
    float bandLow = 0.0f, bandHigh = 0.0f;
};
