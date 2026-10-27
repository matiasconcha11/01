#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace Params
{
    juce::StringArray lengthNames() { return { "1 bar", "2 bars", "4 bars", "8 bars", "16 bars" }; }
    juce::StringArray gapNames() { return { "None", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars" }; }
    juce::StringArray playNames() { return { "Transport", "Free" }; }

    int lengthBars (int index) { return 1 << juce::jlimit (0, 4, index); }
    int gapBars (int index) { return index <= 0 ? 0 : 1 << (juce::jlimit (1, 5, index) - 1); }
}

ZeroOneProcessor::ZeroOneProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ZeroOne", createLayout())
{
    levelParam = apvts.getRawParameterValue (Params::level);
    lowParam = apvts.getRawParameterValue (Params::low);
    highParam = apvts.getRawParameterValue (Params::high);
    startParam = apvts.getRawParameterValue (Params::start);
    lengthParam = apvts.getRawParameterValue (Params::length);
    repeatParam = apvts.getRawParameterValue (Params::repeat);
    gapParam = apvts.getRawParameterValue (Params::gap);
    playParam = apvts.getRawParameterValue (Params::play);
    soloParam = apvts.getRawParameterValue (Params::solo);
    bypassParam = apvts.getRawParameterValue (Params::bypass);

    setMessageText (messageText);
}

juce::AudioProcessorValueTreeState::ParameterLayout ZeroOneProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto hz = AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction (
        [] (float value, int)
        {
            return value >= 1000.0f ? String (value / 1000.0f, 1) + " kHz" : String (roundToInt (value)) + " Hz";
        });

    NormalisableRange<float> frequencyRange (200.0f, 20000.0f, 1.0f);
    frequencyRange.setSkewForCentre (4000.0f);

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { Params::level, 1 }, "Level", NormalisableRange<float> (-60.0f, -12.0f, 0.1f), -30.0f,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (
            [] (float value, int) { return String (value, 1) + " dB"; })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { Params::low, 1 }, "Low", frequencyRange,
                                                       11000.0f, hz));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { Params::high, 1 }, "High", frequencyRange,
                                                       16000.0f, hz));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { Params::start, 1 }, "Start Bar", 1, 512, 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { Params::length, 1 }, "Length",
                                                        Params::lengthNames(), 3));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { Params::repeat, 1 }, "Repeat", true));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { Params::gap, 1 }, "Gap",
                                                        Params::gapNames(), 2));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { Params::play, 1 }, "Play",
                                                        Params::playNames(), Params::playWithTransport));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { Params::solo, 1 }, "Solo", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { Params::bypass, 1 }, "Bypass", false));

    return layout;
}

bool ZeroOneProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

juce::AudioProcessorParameter* ZeroOneProcessor::getBypassParameter() const
{
    return apvts.getParameter (Params::bypass);
}

void ZeroOneProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    sampleRateForDisplay = sampleRate;
    synth.prepare (sampleRate);
    freePpq = 0.0;

    levelGain.reset (sampleRate, 0.03);
    dryGain.reset (sampleRate, 0.02);
    wet.reset (sampleRate, 0.02);
    levelGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (levelParam->load()));
    dryGain.setCurrentAndTargetValue (soloParam->load() > 0.5f ? 0.0f : 1.0f);
    wet.setCurrentAndTargetValue (bypassParam->load() > 0.5f ? 0.0f : 1.0f);
}

Schedule ZeroOneProcessor::currentSchedule (const juce::AudioPlayHead::PositionInfo* position) const
{
    Schedule schedule;
    schedule.startBar = juce::roundToInt (startParam->load());
    schedule.lengthBars = Params::lengthBars (juce::roundToInt (lengthParam->load()));
    schedule.repeat = repeatParam->load() > 0.5f;
    schedule.gapBars = Params::gapBars (juce::roundToInt (gapParam->load()));

    if (position != nullptr)
        if (auto signature = position->getTimeSignature())
            if (signature->numerator > 0 && signature->denominator > 0)
            {
                schedule.beatsPerBar = signature->numerator;
                schedule.beatUnit = signature->denominator;
            }

    return schedule;
}

void ZeroOneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = juce::jmin (getTotalNumInputChannels(), getTotalNumOutputChannels());

    for (auto channel = numChannels; channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear (channel, 0, numSamples);

    {
        const juce::SpinLock::ScopedTryLockType lock (messageLock);

        if (lock.isLocked() && audioMessage != latestMessage)
            audioMessage = latestMessage;   // the old one is still held by `retired`, so this never frees it
    }

    synth.setMessage (audioMessage.get());
    synth.setBand (lowParam->load(), highParam->load());

    juce::Optional<juce::AudioPlayHead::PositionInfo> position;

    if (auto* head = getPlayHead())
        position = head->getPosition();

    const auto schedule = currentSchedule (position ? &*position : nullptr);
    const auto bpm = position && position->getBpm() && *position->getBpm() > 0.0 ? *position->getBpm() : 120.0;
    const auto ppqPerSample = bpm / 60.0 / currentSampleRate;
    const auto free = juce::roundToInt (playParam->load()) == Params::playFree;
    const auto playing = position && position->getIsPlaying();
    const auto hostPpq = position && position->getPpqPosition() ? *position->getPpqPosition() : 0.0;

    levelGain.setTargetValue (juce::Decibels::decibelsToGain (levelParam->load()));
    dryGain.setTargetValue (soloParam->load() > 0.5f ? 0.0f : 1.0f);
    wet.setTargetValue (bypassParam->load() > 0.5f ? 0.0f : 1.0f);

    auto lastPosition = -1.0;
    int scopeStart1 = 0, scopeSize1 = 0, scopeStart2 = 0, scopeSize2 = 0;
    const auto scopeCount = numChannels > 0 ? juce::jmin (numSamples, scopeFifo.getFreeSpace()) : 0;
    scopeFifo.prepareToWrite (scopeCount, scopeStart1, scopeSize1, scopeStart2, scopeSize2);
    auto blockPeak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        auto messagePosition = -1.0;

        if (free)
            messagePosition = schedule.loopPositionAt (freePpq + i * ppqPerSample);
        else if (playing)
            messagePosition = schedule.positionAt (hostPpq + i * ppqPerSample);

        const auto wetGain = wet.getNextValue();
        const auto message = synth.tick (messagePosition) * levelGain.getNextValue() * wetGain;
        // Bypass brings the dry signal back even when soloed.
        const auto dry = dryGain.getNextValue() * wetGain + (1.0f - wetGain);

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto* samples = buffer.getWritePointer (channel);
            samples[i] = samples[i] * dry + message;
            blockPeak = juce::jmax (blockPeak, std::abs (samples[i]));
        }

        if (i < scopeSize1 + scopeSize2)
        {
            auto sum = 0.0f;
            for (int channel = 0; channel < numChannels; ++channel)
                sum += buffer.getReadPointer (channel)[i];
            const auto index = i < scopeSize1 ? scopeStart1 + i : scopeStart2 + i - scopeSize1;
            scopeBuffer[(size_t) index] = sum / (float) numChannels;
            messageScopeBuffer[(size_t) index] = message;
        }

        lastPosition = messagePosition;
    }

    if (free)
        freePpq += numSamples * ppqPerSample;

    displayPosition = (float) lastPosition;
    scopeFifo.finishedWrite (scopeSize1 + scopeSize2);
    auto previous = outputPeakSinceRead.load (std::memory_order_relaxed);
    while (previous < blockPeak && ! outputPeakSinceRead.compare_exchange_weak (previous, blockPeak,
                                                                                std::memory_order_relaxed)) {}
}

void ZeroOneProcessor::publish (std::shared_ptr<const Message> message)
{
    {
        const juce::SpinLock::ScopedLockType lock (messageLock);
        latestMessage = message;
    }

    // Free the messages nobody uses any more: not the latest, and not held by the audio thread
    // (which would make their count above 1).
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [&] (const auto& old) { return old != message && old.use_count() == 1; }),
                   retired.end());
    retired.push_back (std::move (message));
}

void ZeroOneProcessor::setMessageText (const juce::String& text)
{
    messageText = text;
    imageName = {};
    imageMessage.reset();
    publish (std::make_shared<const Message> (Message::fromText (text)));
}

void ZeroOneProcessor::setMessageImage (const juce::Image& image, const juce::String& name)
{
    auto message = Message::fromImage (image);

    if (message.isEmpty())
        return;

    imageName = name.isNotEmpty() ? name : juce::String ("image");
    imageMessage = message.toMemory();
    publish (std::make_shared<const Message> (std::move (message)));
}

std::shared_ptr<const Message> ZeroOneProcessor::getMessage() const
{
    return latestMessage;
}

void ZeroOneProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("text", messageText, nullptr);
    state.setProperty ("imageName", imageName, nullptr);

    if (imageName.isNotEmpty())
        state.setProperty ("image", imageMessage.toBase64Encoding(), nullptr);
    else
        state.removeProperty ("image", nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ZeroOneProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    const auto text = state.getProperty ("text", "01").toString();
    const auto name = state.getProperty ("imageName", "").toString();
    juce::MemoryBlock image;

    if (name.isNotEmpty())
        image.fromBase64Encoding (state.getProperty ("image", "").toString());

    state.removeProperty ("text", nullptr);
    state.removeProperty ("imageName", nullptr);
    state.removeProperty ("image", nullptr);
    apvts.replaceState (state);

    auto restored = name.isNotEmpty() ? Message::fromMemory (image) : Message();

    if (restored.isEmpty())
    {
        setMessageText (text);
        return;
    }

    messageText = text;
    imageName = name;
    imageMessage = image;
    publish (std::make_shared<const Message> (std::move (restored)));
}

juce::AudioProcessorEditor* ZeroOneProcessor::createEditor()
{
    return new ZeroOneEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ZeroOneProcessor();
}
