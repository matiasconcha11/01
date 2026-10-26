#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <juce_audio_formats/juce_audio_formats.h>
#include "MessageSynth.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Schedule.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;

    // Amplitude of one frequency in a signal (single-bin DFT).
    float amplitudeAt (const std::vector<float>& signal, float hz)
    {
        double re = 0.0, im = 0.0;

        for (size_t i = 0; i < signal.size(); ++i)
        {
            const auto phase = juce::MathConstants<double>::twoPi * hz * (double) i / sampleRate;
            re += signal[i] * std::cos (phase);
            im += signal[i] * std::sin (phase);
        }

        return (float) (2.0 * std::sqrt (re * re + im * im) / (double) signal.size());
    }

    Message singleRow (int row, int columns = 1)
    {
        Message message;
        message.columns = columns;
        message.cells.assign ((size_t) (Message::rows * columns), 0.0f);

        for (int column = 0; column < columns; ++column)
            message.cells[(size_t) (row * columns + column)] = 1.0f;

        return message;
    }

    struct FakePlayHead : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override { return info; }
        PositionInfo info;
    };

    juce::AudioBuffer<float> constantBuffer (int numSamples, float value)
    {
        juce::AudioBuffer<float> buffer (2, numSamples);

        for (int channel = 0; channel < 2; ++channel)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (channel), value, numSamples);

        return buffer;
    }
}

TEST_CASE ("Text becomes a picture the height of the message")
{
    const auto message = Message::fromText ("01");
    REQUIRE (! message.isEmpty());
    CHECK (message.cells.size() == (size_t) (Message::rows * message.columns));
    CHECK (*std::max_element (message.cells.begin(), message.cells.end()) > 0.9f);

    // The margins on both sides are silent.
    for (int row = 0; row < Message::rows; ++row)
    {
        CHECK (message.at (row, 0) == 0.0f);
        CHECK (message.at (row, message.columns - 1) == 0.0f);
    }

    // Longer text takes more columns; empty text is no message.
    CHECK (Message::fromText ("0101 0101").columns > message.columns);
    CHECK (Message::fromText ("   ").isEmpty());

    // The last character must still affect the image when the input reaches the UI limit.
    const auto longPrefix = juce::String::repeatedString ("W", 63);
    const auto endingX = Message::fromText (longPrefix + "X");
    const auto endingO = Message::fromText (longPrefix + "O");
    REQUIRE (endingX.columns <= 1024);
    REQUIRE (endingX.columns == endingO.columns);
    auto differentPixels = 0;
    for (size_t i = 0; i < endingX.cells.size(); ++i)
        if (std::abs (endingX.cells[i] - endingO.cells[i]) > 0.1f)
            ++differentPixels;
    CHECK (differentPixels > 10);
}

TEST_CASE ("Images keep their aspect ratio and their bright parts")
{
    juce::Image image (juce::Image::RGB, 200, 100, true);
    juce::Graphics (image).fillAll (juce::Colours::black);
    image.clear ({ 0, 0, 200, 50 }, juce::Colours::white);   // top half white

    const auto message = Message::fromImage (image);
    REQUIRE (message.columns == Message::rows * 2);
    CHECK (message.at (2, 10) > 0.9f);
    CHECK (message.at (Message::rows - 3, 10) < 0.05f);
}

TEST_CASE ("Messages survive being saved")
{
    const auto message = Message::fromText ("Hi");
    const auto copy = Message::fromMemory (message.toMemory());
    REQUIRE (copy.columns == message.columns);

    for (size_t i = 0; i < message.cells.size(); ++i)
        CHECK_THAT (copy.cells[i], WithinAbs (message.cells[i], 1.0 / 255.0));

    CHECK (Message::fromMemory (juce::MemoryBlock ("junk", 4)).isEmpty());
}

TEST_CASE ("Schedule: start bar, length, repeat and gap")
{
    Schedule schedule;
    schedule.startBar = 3;    // starts at quarter note 8 in 4/4
    schedule.lengthBars = 2;  // 8 quarter notes long

    CHECK (schedule.positionAt (7.99) < 0.0);
    CHECK_THAT (schedule.positionAt (8.0), WithinAbs (0.0, 1e-9));
    CHECK_THAT (schedule.positionAt (12.0), WithinAbs (0.5, 1e-9));
    CHECK (schedule.positionAt (16.0) < 0.0);
    CHECK (schedule.positionAt (40.0) < 0.0);

    schedule.repeat = true;
    schedule.gapBars = 2;     // plays 8, rests 8
    CHECK (schedule.positionAt (20.0) < 0.0);
    CHECK_THAT (schedule.positionAt (24.0), WithinAbs (0.0, 1e-9));
    CHECK_THAT (schedule.positionAt (26.0), WithinAbs (0.25, 1e-9));

    // 3/4: a bar is three quarter notes.
    schedule.beatsPerBar = 3;
    schedule.repeat = false;
    CHECK_THAT (schedule.positionAt (6.0), WithinAbs (0.0, 1e-9));
    CHECK_THAT (schedule.positionAt (9.0), WithinAbs (0.5, 1e-9));

    // Free loop ignores the start bar.
    schedule.beatsPerBar = 4;
    schedule.gapBars = 0;
    CHECK_THAT (schedule.loopPositionAt (0.0), WithinAbs (0.0, 1e-9));
    CHECK_THAT (schedule.loopPositionAt (12.0), WithinAbs (0.5, 1e-9));
}

TEST_CASE ("Each row sounds at its own frequency, highest row on top")
{
    MessageSynth synth;
    synth.prepare (sampleRate);
    synth.setBand (11000.0f, 16000.0f);

    // Row bands run from the top of the range down to the bottom.
    const auto rowWidth = 5000.0f / (float) Message::rows;
    CHECK_THAT (synth.frequencyOfRow (0), WithinAbs (16000.0 - rowWidth / 2, rowWidth / 4));
    CHECK_THAT (synth.frequencyOfRow (Message::rows - 1), WithinAbs (11000.0 + rowWidth / 2, rowWidth / 4));

    for (auto row : { 0, Message::rows / 2, Message::rows - 1 })
    {
        const auto message = singleRow (row);
        synth.reset();
        synth.setMessage (&message);

        std::vector<float> output (4800);

        for (auto& sample : output)
            sample = synth.tick (0.5);

        const auto own = amplitudeAt (output, synth.frequencyOfRow (row));
        const auto neighbour = amplitudeAt (output, synth.frequencyOfRow (row == 0 ? 2 : row - 2));
        CHECK (own > 0.05f);
        CHECK (neighbour < own * 0.1f);
    }
}

TEST_CASE ("Silent outside the message, and bounded inside it")
{
    MessageSynth synth;
    synth.prepare (sampleRate);
    synth.setBand (2000.0f, 12000.0f);

    // Every cell lit: the loudest the message can be.
    Message full;
    full.columns = 4;
    full.cells.assign ((size_t) (Message::rows * 4), 1.0f);
    synth.setMessage (&full);

    auto peak = 0.0f;

    for (int i = 0; i < 48000; ++i)
        peak = juce::jmax (peak, std::abs (synth.tick ((double) i / 48000.0)));

    CHECK (peak > 0.3f);
    CHECK (peak * juce::Decibels::decibelsToGain (-12.0f) < 1.0f);

    // After the message: it fades out within a few milliseconds and stays silent.
    for (int i = 0; i < 2000; ++i)
        synth.tick (-1.0);

    for (int i = 0; i < 1000; ++i)
        CHECK (synth.tick (-1.0) == 0.0f);
}

TEST_CASE ("Processor: passes audio through untouched while the transport is stopped")
{
    ZeroOneProcessor processor;
    FakePlayHead head;
    head.info.setIsPlaying (false);
    head.info.setBpm (120.0);
    head.info.setPpqPosition (0.0);
    processor.setPlayHead (&head);
    processor.prepareToPlay (sampleRate, 512);

    auto buffer = constantBuffer (512, 0.25f);
    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);

    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < 512; ++i)
            REQUIRE (buffer.getSample (channel, i) == 0.25f);

    processor.setPlayHead (nullptr);
}

TEST_CASE ("Processor: writes the message while playing, from the start bar")
{
    ZeroOneProcessor processor;
    processor.setMessageText ("I");
    FakePlayHead head;
    head.info.setIsPlaying (true);
    head.info.setBpm (120.0);
    processor.setPlayHead (&head);
    processor.prepareToPlay (sampleRate, 4800);

    juce::MidiBuffer midi;
    const auto energyAt = [&] (double ppq)
    {
        head.info.setPpqPosition (ppq);
        auto buffer = constantBuffer (4800, 0.0f);
        processor.processBlock (buffer, midi);
        return buffer.getMagnitude (0, 0, 4800);
    };

    // Default: start bar 1, 2 bars long. The middle of the message is the letter.
    CHECK (energyAt (3.8) > 0.001f);
    // After the message (2 bars) comes a 2-bar gap: the first block still has the 2 ms fade-out, then silence.
    energyAt (9.0);
    CHECK (energyAt (9.2) == 0.0f);
    CHECK (energyAt (19.8) > 0.001f);   // the middle of the repeat

    processor.setPlayHead (nullptr);
}

TEST_CASE ("Processor: state brings back the parameters and the message")
{
    ZeroOneProcessor original;
    original.setMessageText ("hello world");
    original.apvts.getParameter (Params::start)->setValueNotifyingHost (
        original.apvts.getParameter (Params::start)->convertTo0to1 (9.0f));

    juce::MemoryBlock state;
    original.getStateInformation (state);

    ZeroOneProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (restored.getMessageText() == "hello world");
    CHECK (restored.getImageName().isEmpty());
    CHECK (juce::roundToInt (restored.apvts.getRawParameterValue (Params::start)->load()) == 9);
    CHECK (restored.getMessage()->columns == original.getMessage()->columns);

    // An image message is saved as the picture itself.
    juce::Image image (juce::Image::RGB, 96, 48, true);
    image.clear ({ 0, 0, 48, 48 }, juce::Colours::white);
    original.setMessageImage (image, "cover.png");
    original.getStateInformation (state);

    ZeroOneProcessor withImage;
    withImage.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (withImage.getImageName() == "cover.png");
    REQUIRE (withImage.getMessage()->columns == 96);
    CHECK (withImage.getMessage()->at (10, 10) > 0.9f);
    CHECK (withImage.getMessage()->at (10, 80) < 0.05f);
}

// Not part of the normal run: renders the editor to a PNG.  ./build/ZeroOneTests "[.snapshot]"
TEST_CASE ("Editor snapshot", "[.snapshot]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    ZeroOneProcessor processor;
    processor.setMessageText (juce::SystemStats::getEnvironmentVariable ("ZEROONE_SNAPSHOT_TEXT", "Created"));

    FakePlayHead head;
    head.info.setBpm (120.0);
    head.info.setIsPlaying (true);
    processor.setPlayHead (&head);
    processor.prepareToPlay (sampleRate, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    Spectrogram* spectrogram = nullptr;
    MessagePreview* preview = nullptr;

    for (auto* child : editor->getChildren())
    {
        if (auto* s = dynamic_cast<Spectrogram*> (child)) spectrogram = s;
        if (auto* m = dynamic_cast<MessagePreview*> (child)) preview = m;
    }

    REQUIRE (spectrogram != nullptr);
    REQUIRE (preview != nullptr);

    // A slow pad (a minor chord with a little movement) under the message, for about 9 seconds.
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    const float chord[] { 110.0f, 220.0f, 261.63f, 329.63f, 440.0f, 523.25f, 659.25f, 987.77f };
    juce::Random random (7);

    for (int block = 0; block < 860; ++block)
    {
        for (int i = 0; i < 512; ++i)
        {
            const auto t = (double) (block * 512 + i) / sampleRate;
            auto v = 0.0f;

            for (size_t k = 0; k < std::size (chord); ++k)
                v += (float) (std::sin (juce::MathConstants<double>::twoPi * chord[k] * t * (1.0 + 0.002 * std::sin (t * 0.7 + (double) k)))
                              * (0.12 / (1.0 + (double) k * 0.4)));

            v += (random.nextFloat() - 0.5f) * 0.0004f;
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v);
        }

        head.info.setPpqPosition ((double) block * 512.0 / 24000.0);
        processor.processBlock (buffer, midi);
        spectrogram->update();
    }

    spectrogram->setBand (11000.0f, 16000.0f);
    preview->setPosition (processor.displayPosition.load());

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
    const auto path = juce::SystemStats::getEnvironmentVariable ("ZEROONE_SNAPSHOT", "/tmp/zeroone.png");
    juce::File (path).deleteFile();
    juce::FileOutputStream stream { juce::File (path) };
    REQUIRE (juce::PNGImageFormat().writeImageToStream (image, stream));
    processor.setPlayHead (nullptr);
}

// Not part of the normal run: bounces 10 s of a pad with a message to a WAV, for reading back in a
// spectrogram (or after a lossy encode).  ./build/ZeroOneTests "[.render]"
TEST_CASE ("Render a bounce", "[.render]")
{
    ZeroOneProcessor processor;
    processor.setMessageText (juce::SystemStats::getEnvironmentVariable ("ZEROONE_RENDER_TEXT", "Created"));

    for (auto [id, variable] : { std::pair { Params::low, "ZEROONE_RENDER_LOW" }, std::pair { Params::high, "ZEROONE_RENDER_HIGH" } })
        if (const auto value = juce::SystemStats::getEnvironmentVariable (variable, {}); value.isNotEmpty())
            processor.apvts.getParameter (id)->setValueNotifyingHost (
                processor.apvts.getParameter (id)->convertTo0to1 (value.getFloatValue()));

    FakePlayHead head;
    head.info.setBpm (120.0);
    head.info.setIsPlaying (true);
    processor.setPlayHead (&head);
    processor.prepareToPlay (sampleRate, 512);

    const auto numBlocks = (int) (10.0 * sampleRate / 512.0);
    juce::AudioBuffer<float> bounce (2, numBlocks * 512);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    const float chord[] { 110.0f, 220.0f, 261.63f, 329.63f, 440.0f, 523.25f, 659.25f, 987.77f };

    for (int block = 0; block < numBlocks; ++block)
    {
        for (int i = 0; i < 512; ++i)
        {
            const auto t = (double) (block * 512 + i) / sampleRate;
            auto v = 0.0f;

            for (size_t k = 0; k < std::size (chord); ++k)
                v += (float) (std::sin (juce::MathConstants<double>::twoPi * chord[k] * t) * (0.12 / (1.0 + (double) k * 0.4)));

            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v);
        }

        head.info.setPpqPosition ((double) block * 512.0 / 24000.0);
        processor.processBlock (buffer, midi);

        for (int channel = 0; channel < 2; ++channel)
            bounce.copyFrom (channel, block * 512, buffer, channel, 0, 512);
    }

    const juce::File file (juce::SystemStats::getEnvironmentVariable ("ZEROONE_RENDER", "/tmp/zeroone.wav"));
    file.deleteFile();
    auto stream = file.createOutputStream();
    REQUIRE (stream != nullptr);

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.release(), sampleRate, 2, 24, {}, 0));
    REQUIRE (writer != nullptr);
    REQUIRE (writer->writeFromAudioSampleBuffer (bounce, 0, bounce.getNumSamples()));
    processor.setPlayHead (nullptr);
}
