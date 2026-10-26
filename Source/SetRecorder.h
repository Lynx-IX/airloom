#pragma once

#include <JuceHeader.h>

// Records everything going to the speakers to a WAV file, for the whole set.
// The audio thread hands blocks to a FIFO; a background thread writes them to disk.
//
// The master limiter keeps the output under -0.5 dBFS, so 24-bit is plenty
// (about 1 GB an hour at 48 kHz).
class SetRecorder
{
public:
    SetRecorder()   { thread.startThread(); }
    ~SetRecorder()  { stop(); }

    // Message thread.
    bool start (const juce::File& newFile, double sampleRate)
    {
        stop();
        newFile.getParentDirectory().createDirectory();

        std::unique_ptr<juce::OutputStream> stream (newFile.createOutputStream());
        if (stream == nullptr)
            return false;

        juce::WavAudioFormat wav;
        const auto options = juce::AudioFormatWriterOptions{}
                                 .withSampleRate (sampleRate)
                                 .withNumChannels (2)
                                 .withBitsPerSample (24);
        auto w = wav.createWriterFor (stream, options);
        if (w == nullptr)
            return false;

        file = newFile;
        rate = sampleRate;
        written = 0;
        dropped = false;
        // Ten seconds of buffer, so a slow disk never costs any audio.
        writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (w.release(), thread, (int) (sampleRate * 10.0));

        const juce::ScopedLock sl (lock);
        active = writer.get();
        return true;
    }

    // Message thread. Finishes writing what's buffered and closes the file.
    void stop()
    {
        {
            const juce::ScopedLock sl (lock);
            active = nullptr;
        }
        writer.reset();
    }

    // Audio thread.
    void write (const juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
    {
        const juce::ScopedLock sl (lock);
        auto* w = active.load();
        if (w == nullptr || buffer.getNumChannels() == 0)
            return;

        const float* channels[2] = { buffer.getReadPointer (0, startSample),
                                     buffer.getReadPointer (juce::jmin (1, buffer.getNumChannels() - 1), startSample) };
        if (w->write (channels, numSamples))
            written += numSamples;
        else
            dropped = true;
    }

    bool isRecording() const        { return active.load() != nullptr; }
    double getSeconds() const       { return rate > 0.0 ? (double) written.load() / rate : 0.0; }
    bool hasDroppedAudio() const    { return dropped; }
    juce::File getFile() const      { return file; }

private:
    juce::TimeSliceThread thread { "Set recorder" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    juce::CriticalSection lock;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> active { nullptr };
    std::atomic<juce::int64> written { 0 };
    std::atomic<bool> dropped { false };
    double rate = 0.0;
    juce::File file;
};
