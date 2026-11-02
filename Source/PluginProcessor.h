#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Message.h"
#include "MessageSynth.h"
#include "Schedule.h"

namespace Params
{
    inline const juce::String level { "level" };
    inline const juce::String low { "low" };
    inline const juce::String high { "high" };
    inline const juce::String start { "start" };
    inline const juce::String length { "length" };
    inline const juce::String repeat { "repeat" };
    inline const juce::String gap { "gap" };
    inline const juce::String play { "play" };
    inline const juce::String solo { "solo" };
    inline const juce::String bypass { "bypass" };

    enum Play
    {
        playWithTransport,
        playFree
    };

    juce::StringArray lengthNames();
    juce::StringArray gapNames();
    juce::StringArray playNames();
    int lengthBars (int index);
    int gapBars (int index);
}

class ZeroOneProcessor : public juce::AudioProcessor
{
public:
    ZeroOneProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Inscript"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    void setMessageText (const juce::String& text);
    void setMessageImage (const juce::Image& image, const juce::String& name);
    juce::String getMessageText() const { const juce::ScopedLock lock (contentLock); return messageText; }
    juce::String getImageName() const { const juce::ScopedLock lock (contentLock); return imageName; }
    std::shared_ptr<const Message> getMessage() const;

    juce::AudioProcessorValueTreeState apvts;

    // Written by the audio thread, read by the editor.
    std::atomic<float> displayPosition { -1.0f };
    std::atomic<double> sampleRateForDisplay { 44100.0 };

    // Mono copy of the output, for the editor's spectrogram.
    static constexpr int scopeSize = 1 << 15;
    juce::AbstractFifo scopeFifo { scopeSize };
    std::vector<float> scopeBuffer = std::vector<float> (scopeSize);
    std::vector<float> messageScopeBuffer = std::vector<float> (scopeSize);
    std::atomic<float> outputPeakSinceRead { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void publish (std::shared_ptr<const Message>);
    Schedule currentSchedule (const juce::AudioPlayHead::PositionInfo*) const;

    std::atomic<float>* levelParam = nullptr;
    std::atomic<float>* lowParam = nullptr;
    std::atomic<float>* highParam = nullptr;
    std::atomic<float>* startParam = nullptr;
    std::atomic<float>* lengthParam = nullptr;
    std::atomic<float>* repeatParam = nullptr;
    std::atomic<float>* gapParam = nullptr;
    std::atomic<float>* playParam = nullptr;
    std::atomic<float>* soloParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;

    // UI or host threads publish under the lock; the audio thread only tries it and keeps the
    // old message if busy. Retired messages are freed outside the audio thread.
    mutable juce::SpinLock messageLock;
    std::shared_ptr<const Message> latestMessage;
    std::shared_ptr<const Message> audioMessage;
    std::vector<std::shared_ptr<const Message>> retired;

    mutable juce::CriticalSection contentLock;
    juce::String messageText { "01" };
    juce::String imageName;   // empty when the message is text
    juce::MemoryBlock imageMessage;

    MessageSynth synth;
    double currentSampleRate = 44100.0;
    double freePpq = 0.0;
    juce::SmoothedValue<float> levelGain, dryGain, wet;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZeroOneProcessor)
};
