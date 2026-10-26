#include "MessageSynth.h"

void MessageSynth::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;

    // ~2 ms fades between columns: fast enough to keep the edges of letters sharp, slow enough not to click.
    smoothing = 1.0f - std::exp (-1.0f / (0.002f * (float) sampleRate));

    // Fixed, spread-out starting phases keep the peaks of a full column low and the output repeatable.
    juce::Random random (0x01);

    for (size_t i = 0; i < (size_t) oscillators; ++i)
    {
        const auto phase = random.nextFloat() * juce::MathConstants<float>::twoPi;
        cosine[i] = std::cos (phase);
        sine[i] = std::sin (phase);
    }

    reset();
}

void MessageSynth::reset()
{
    targets.fill (0.0f);
    levels.fill (0.0f);
    currentColumn = -1;
    active = false;
}

void MessageSynth::setMessage (const Message* newMessage)
{
    if (newMessage == message)
        return;

    message = newMessage;
    currentColumn = -2;   // no real column, so the next tick reloads the levels
}

void MessageSynth::setBand (float lowHz, float highHz)
{
    const auto nyquistGuard = (float) sampleRate * 0.47f;
    auto low = juce::jlimit (20.0f, nyquistGuard, juce::jmin (lowHz, highHz));
    auto high = juce::jlimit (20.0f, nyquistGuard, juce::jmax (lowHz, highHz));

    for (size_t i = 0; i < (size_t) oscillators; ++i)
    {
        const auto frequency = high - (high - low) * (float) i / (float) (oscillators - 1);

        if (juce::exactlyEqual (frequency, frequencies[i]))
            continue;

        frequencies[i] = frequency;
        const auto step = juce::MathConstants<float>::twoPi * frequency / (float) sampleRate;
        stepCosine[i] = std::cos (step);
        stepSine[i] = std::sin (step);
    }
}

void MessageSynth::updateTargets (int column)
{
    currentColumn = column;

    for (int row = 0; row < rows; ++row)
        targets[(size_t) row] = column >= 0 ? message->at (row, column) : 0.0f;

    if (column >= 0)
        active = true;
}

float MessageSynth::tick (double position)
{
    auto column = -1;

    if (position >= 0.0 && message != nullptr && ! message->isEmpty())
        column = juce::jmin ((int) (position * message->columns), message->columns - 1);

    if (column != currentColumn)
        updateTargets (column);

    if (! active)
        return 0.0f;

    auto sum = 0.0f;
    auto anySounding = column >= 0;

    for (size_t i = 0; i < (size_t) oscillators; ++i)
    {
        const auto target = targets[i / (size_t) perRow];
        auto& level = levels[i];
        level += (target - level) * smoothing;

        if (level < 1.0e-5f && target <= 0.0f)
        {
            level = 0.0f;
            continue;
        }

        anySounding = true;
        const auto c = cosine[i];
        const auto s = sine[i];
        cosine[i] = c * stepCosine[i] - s * stepSine[i];
        sine[i] = c * stepSine[i] + s * stepCosine[i];
        sum += sine[i] * level;
    }

    // Rounding slowly shrinks or grows the phasors; pull them back onto the unit circle now and then.
    if (++samplesSinceNormalise >= 1024)
    {
        samplesSinceNormalise = 0;

        for (size_t i = 0; i < (size_t) oscillators; ++i)
        {
            const auto magnitude = std::sqrt (cosine[i] * cosine[i] + sine[i] * sine[i]);

            if (magnitude > 0.0f)
            {
                cosine[i] /= magnitude;
                sine[i] /= magnitude;
            }
        }
    }

    active = anySounding;
    return sum * gain;
}
