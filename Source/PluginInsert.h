#pragma once

#include <JuceHeader.h>

// An effect plug-in in a signal path (an input, a group or the master). setPlugin()
// prepares the newcomer first, so the audio thread never waits long, and hands back the
// old one to be freed off the audio thread. process() runs it in place on a stereo buffer.
//
// Thread model: setPlugin() and getPlugin() from the message thread, prepare() and
// process() from the audio thread.
class PluginInsert
{
public:
    void prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        blockSize = juce::jmax (32, maxBlockSize);

        const juce::SpinLock::ScopedLockType sl (lock);
        if (plugin != nullptr)
        {
            plugin->prepareToPlay (sampleRate, blockSize);
            buffer.setSize (channelsFor (*plugin), blockSize);
        }
    }

    std::unique_ptr<juce::AudioPluginInstance> setPlugin (std::unique_ptr<juce::AudioPluginInstance> newPlugin)
    {
        juce::AudioBuffer<float> newBuffer;
        if (newPlugin != nullptr)
        {
            newPlugin->enableAllBuses();
            newPlugin->setNonRealtime (false);
            newPlugin->prepareToPlay (sampleRate, blockSize);
            newBuffer.setSize (channelsFor (*newPlugin), blockSize);
        }

        {
            const juce::SpinLock::ScopedLockType sl (lock);
            std::swap (plugin, newPlugin);
            std::swap (buffer, newBuffer);
            loaded = plugin != nullptr;
        }

        if (newPlugin != nullptr)
            newPlugin->releaseResources();
        return newPlugin;   // the previous one
    }

    juce::AudioPluginInstance* getPlugin() const   { return plugin.get(); }
    bool isLoaded() const                          { return loaded; }

    void process (juce::AudioBuffer<float>& stereo, int startSample, int numSamples)
    {
        const juce::SpinLock::ScopedTryLockType sl (lock);
        if (! sl.isLocked() || plugin == nullptr || stereo.getNumChannels() < 2)
            return;

        const int channels = buffer.getNumChannels();
        const bool stereoOut = plugin->getTotalNumOutputChannels() > 1;

        for (int done = 0; done < numSamples;)
        {
            const int n = juce::jmin (numSamples - done, blockSize);

            // A view onto the preallocated buffer at this chunk's length: no allocation.
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), channels, n);
            view.clear();
            view.copyFrom (0, 0, stereo, 0, startSample + done, n);
            view.copyFrom (1, 0, stereo, 1, startSample + done, n);

            noMidi.clear();
            plugin->processBlock (view, noMidi);

            stereo.copyFrom (0, startSample + done, view, 0, 0, n);
            stereo.copyFrom (1, startSample + done, view, stereoOut ? 1 : 0, 0, n);
            done += n;
        }
    }

private:
    static int channelsFor (juce::AudioPluginInstance& p)
    {
        return juce::jmax (2, p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    }

    std::unique_ptr<juce::AudioPluginInstance> plugin;   // swapped under lock
    juce::SpinLock lock;
    juce::AudioBuffer<float> buffer;                     // swapped under lock
    juce::MidiBuffer noMidi;
    double sampleRate = 48000.0;
    int blockSize = 512;
    std::atomic<bool> loaded { false };
};
