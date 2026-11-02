#pragma once

#include "PluginProcessor.h"
#include "Views.h"

class DaddySerchAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       private juce::Timer
{
public:
    explicit DaddySerchAudioProcessorEditor (DaddySerchAudioProcessor&);
    ~DaddySerchAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void layoutViews();
    int viewIndexAt (juce::Point<int>) const;

    DaddySerchAudioProcessor& proc;

    ds::Shared shared;
    ds::SpectrumAnalyser analyser;

    ds::MascotView      mascot      { shared };
    ds::SpectrumView    spectrum    { shared };
    ds::LevelsView      levels      { shared };
    ds::ScopeView       scope       { shared };
    ds::StereometerView stereometer { shared };
    ds::SpectrogramView spectrogram { shared };

    std::vector<ds::ViewBase*> views;
    int solo = -1;

    float slowEnv = 0.f, cooldown = 0.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DaddySerchAudioProcessorEditor)
};
