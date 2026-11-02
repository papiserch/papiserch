#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <vector>
#include <cmath>

// DaddySerch: medidor visual. NO modifica el audio, solo lo analiza.
class DaddySerchAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kRingSize = 1 << 15; // 32768 muestras

    DaddySerchAudioProcessor();
    ~DaddySerchAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "DaddySerch"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    // Lectura de las ultimas muestras (para la interfaz)
    void copyLatest (int numSamples, float* L, float* R) const;
    void resetIntegrated() { resetFlag.store (true); }

    // Picos desde la ultima lectura (la interfaz hace exchange(0))
    std::atomic<float> peakL { 0.f }, peakR { 0.f };
    // Sonoridad en LUFS (ITU-R BS.1770)
    std::atomic<float> momentary { -100.f }, shortTerm { -100.f }, integrated { -100.f };

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double process (double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void reset() { z1 = z2 = 0; }
    };

    void setupFilters (double fs);
    void finishLoudnessBlock();
    static double toLufs (double ms) { return ms > 1e-10 ? -0.691 + 10.0 * std::log10 (ms) : -100.0; }

    std::array<float, kRingSize> ringL {}, ringR {};
    std::atomic<int> writePos { 0 };

    Biquad s1[2], s2[2];
    int blockSamples = 4410, blockCounter = 0, blockIdx = 0, blocksFilled = 0;
    double accum = 0.0;
    std::array<double, 30> blockMS {};
    std::vector<double> gatedMS;
    std::atomic<bool> resetFlag { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DaddySerchAudioProcessor)
};
