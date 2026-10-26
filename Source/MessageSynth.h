#pragma once

#include "Message.h"
#include <array>

// A bank of sine oscillators spread linearly between a low and a high frequency (spectrogram
// views are usually linear, so the letters keep their shape). Each message row gets a few
// neighbouring oscillators, close enough that the row reads as one solid band instead of a thin
// line. Each column of the message sets how loud every row is for that slice of time.
class MessageSynth
{
public:
    void prepare (double sampleRate);
    void reset();

    // Message thread owns the message; this only reads it. Pass nullptr for silence.
    void setMessage (const Message* message);
    void setBand (float lowHz, float highHz);

    // One sample of the message at a position from 0 to 1; negative means silent.
    float tick (double position);

    // The centre of the row's band.
    float frequencyOfRow (int row) const { return frequencies[(size_t) (row * perRow + perRow / 2)]; }

    static constexpr int perRow = 3;

private:
    void updateTargets (int column);

    static constexpr int rows = Message::rows;
    static constexpr int oscillators = rows * perRow;

    const Message* message = nullptr;
    double sampleRate = 44100.0;
    float smoothing = 0.01f;
    float gain = 1.0f / std::sqrt ((float) oscillators);
    int currentColumn = -1;
    int samplesSinceNormalise = 0;
    bool active = false;

    std::array<float, rows> targets {};
    std::array<float, oscillators> frequencies {}, levels {};
    std::array<float, oscillators> cosine {}, sine {};          // oscillator state (a unit phasor)
    std::array<float, oscillators> stepCosine {}, stepSine {};  // per-sample rotation
};
