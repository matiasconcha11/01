#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>

class ZeroOneProcessor;

// Linear-frequency spectrogram of the processed mix or just the added message.
class Spectrogram : public juce::Component, private juce::Timer
{
public:
    explicit Spectrogram (ZeroOneProcessor&);

    void setBand (float lowHz, float highHz);
    void setFullSpectrum (bool full);
    void setMessageOnly (bool messageOnly);
    float getViewLow() const;
    float getViewHigh() const;
    std::function<void(bool, float)> onBandEdgeDrag;
    std::function<void(bool)> onBandEdgeBegin, onBandEdgeEnd;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    // Draws whatever audio has arrived since the last call.
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
    static constexpr int hopSize = 1024;

    ZeroOneProcessor& processor;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };

    std::vector<float> history = std::vector<float> (fftSize);   // last fftSize samples, oldest first
    std::vector<float> fftData = std::vector<float> (fftSize * 2);
    int samplesSinceColumn = 0;

    juce::Image image;
    float bandLow = 0.0f, bandHigh = 0.0f;
    bool fullSpectrum = false, messageOnly = false;
    int draggingEdge = 0; // -1 low, +1 high
    float dragViewLow = 0.0f, dragViewHigh = 0.0f;
};
