#include "PluginProcessor.h"
#include "PluginEditor.h"

DaddySerchAudioProcessor::DaddySerchAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    gatedMS.reserve (1 << 19); // ~14 horas de bloques de 100 ms
}

bool DaddySerchAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void DaddySerchAudioProcessor::setupFilters (double fs)
{
    const double pi = juce::MathConstants<double>::pi;

    // Etapa 1: high-shelf (K-weighting)
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (pi * f0 / fs);
        const double Vh = std::pow (10.0, G / 20.0);
        const double Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        for (auto& f : s1)
        {
            f.b0 = (Vh + Vb * K / Q + K * K) / a0;
            f.b1 = 2.0 * (K * K - Vh) / a0;
            f.b2 = (Vh - Vb * K / Q + K * K) / a0;
            f.a1 = 2.0 * (K * K - 1.0) / a0;
            f.a2 = (1.0 - K / Q + K * K) / a0;
            f.reset();
        }
    }
    // Etapa 2: high-pass RLB
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (pi * f0 / fs);
        const double a0 = 1.0 + K / Q + K * K;
        for (auto& f : s2)
        {
            f.b0 = 1.0; f.b1 = -2.0; f.b2 = 1.0;
            f.a1 = 2.0 * (K * K - 1.0) / a0;
            f.a2 = (1.0 - K / Q + K * K) / a0;
            f.reset();
        }
    }
}

void DaddySerchAudioProcessor::prepareToPlay (double sampleRate, int)
{
    setupFilters (sampleRate);
    blockSamples = std::max (1, (int) std::lround (sampleRate * 0.1));
    blockCounter = 0;
    accum = 0.0;
    blockIdx = 0;
    blocksFilled = 0;
    blockMS.fill (0.0);
    gatedMS.clear();
    momentary.store (-100.f);
    shortTerm.store (-100.f);
    integrated.store (-100.f);
}

void DaddySerchAudioProcessor::finishLoudnessBlock()
{
    const double ms = accum / (double) blockSamples;
    accum = 0.0;
    blockCounter = 0;

    blockMS[(size_t) blockIdx] = ms;
    blockIdx = (blockIdx + 1) % 30;
    if (blocksFilled < 30) ++blocksFilled;

    auto meanLast = [this] (int count)
    {
        count = std::min (count, blocksFilled);
        if (count <= 0) return 0.0;
        double s = 0.0;
        for (int k = 1; k <= count; ++k)
            s += blockMS[(size_t) ((blockIdx - k + 30) % 30)];
        return s / (double) count;
    };

    const double mMS = meanLast (4);
    momentary.store ((float) toLufs (mMS));
    shortTerm.store ((float) toLufs (meanLast (30)));

    // Integrado con doble compuerta (-70 LUFS absoluta, -10 LU relativa)
    if (blocksFilled >= 4 && toLufs (mMS) > -70.0 && gatedMS.size() < gatedMS.capacity())
        gatedMS.push_back (mMS);

    if (! gatedMS.empty())
    {
        double sum = 0.0;
        for (double v : gatedMS) sum += v;
        const double thr = toLufs (sum / (double) gatedMS.size()) - 10.0;

        double sum2 = 0.0;
        size_t cnt = 0;
        for (double v : gatedMS)
            if (toLufs (v) > thr) { sum2 += v; ++cnt; }

        if (cnt > 0)
            integrated.store ((float) toLufs (sum2 / (double) cnt));
    }
}

void DaddySerchAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    const int nch = buffer.getNumChannels();
    if (n == 0 || nch == 0)
        return;

    if (resetFlag.exchange (false))
    {
        gatedMS.clear();
        integrated.store (-100.f);
    }

    // Solo lectura: el audio pasa intacto
    const float* l = buffer.getReadPointer (0);
    const float* r = buffer.getReadPointer (nch > 1 ? 1 : 0);

    int w = writePos.load (std::memory_order_relaxed);
    float pl = 0.f, pr = 0.f;

    for (int i = 0; i < n; ++i)
    {
        const float a = l[i], b = r[i];
        ringL[(size_t) w] = a;
        ringR[(size_t) w] = b;
        w = (w + 1) & (kRingSize - 1);

        pl = std::max (pl, std::abs (a));
        pr = std::max (pr, std::abs (b));

        const double ya = s2[0].process (s1[0].process ((double) a));
        const double yb = s2[1].process (s1[1].process ((double) b));
        accum += ya * ya + yb * yb;

        if (++blockCounter >= blockSamples)
            finishLoudnessBlock();
    }

    writePos.store (w, std::memory_order_release);

    float cur = peakL.load();
    while (pl > cur && ! peakL.compare_exchange_weak (cur, pl)) {}
    cur = peakR.load();
    while (pr > cur && ! peakR.compare_exchange_weak (cur, pr)) {}
}

void DaddySerchAudioProcessor::copyLatest (int numSamples, float* L, float* R) const
{
    numSamples = juce::jlimit (1, kRingSize, numSamples);
    const int w = writePos.load (std::memory_order_acquire);
    const int start = (w - numSamples) & (kRingSize - 1);
    for (int i = 0; i < numSamples; ++i)
    {
        const int idx = (start + i) & (kRingSize - 1);
        L[i] = ringL[(size_t) idx];
        R[i] = ringR[(size_t) idx];
    }
}

juce::AudioProcessorEditor* DaddySerchAudioProcessor::createEditor()
{
    return new DaddySerchAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DaddySerchAudioProcessor();
}
