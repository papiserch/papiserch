#include "PluginEditor.h"

DaddySerchAudioProcessorEditor::DaddySerchAudioProcessorEditor (DaddySerchAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    views = { &mascot, &spectrum, &levels, &scope, &stereometer, &spectrogram };
    for (auto* v : views)
        addAndMakeVisible (*v);

    setResizable (true, true);
    setResizeLimits (640, 360, 2400, 1400);
    setSize (1000, 560);

    startTimerHz (60);
}

DaddySerchAudioProcessorEditor::~DaddySerchAudioProcessorEditor()
{
    stopTimer();
}

void DaddySerchAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (ds::pal::bg);
}

void DaddySerchAudioProcessorEditor::resized()
{
    layoutViews();
}

void DaddySerchAudioProcessorEditor::layoutViews()
{
    auto area = getLocalBounds().reduced (4);

    if (solo >= 0 && solo < (int) views.size())
    {
        for (int i = 0; i < (int) views.size(); ++i)
        {
            views[(size_t) i]->setVisible (i == solo);
            if (i == solo) views[(size_t) i]->setBounds (area);
        }
        return;
    }

    const int cols = 3, rows = 2;
    const int cw = area.getWidth() / cols, ch = area.getHeight() / rows;
    for (int i = 0; i < (int) views.size(); ++i)
    {
        auto* v = views[(size_t) i];
        v->setVisible (true);
        v->setBounds (area.getX() + (i % cols) * cw, area.getY() + (i / cols) * ch, cw, ch);
    }
}

int DaddySerchAudioProcessorEditor::viewIndexAt (juce::Point<int> pt) const
{
    for (int i = 0; i < (int) views.size(); ++i)
        if (views[(size_t) i]->isVisible() && views[(size_t) i]->getBounds().contains (pt))
            return i;
    return -1;
}

void DaddySerchAudioProcessorEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int idx = viewIndexAt (e.getPosition());
    if (idx < 0) return;
    solo = (solo == idx) ? -1 : idx;
    layoutViews();
}

void DaddySerchAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        return;

    juce::PopupMenu m;
    m.addItem (1, "Reset integrated loudness (LUFS)");
    m.addSeparator();
    m.addItem (2, "Show all modules", solo >= 0);
    for (int i = 0; i < (int) views.size(); ++i)
        m.addItem (10 + i, "Solo: " + views[(size_t) i]->getTitle(), true, solo == i);

    juce::Component::SafePointer<DaddySerchAudioProcessorEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r == 1) safe->proc.resetIntegrated();
        else if (r == 2) { safe->solo = -1; safe->layoutViews(); }
        else if (r >= 10) { safe->solo = r - 10; safe->layoutViews(); }
    });
}

void DaddySerchAudioProcessorEditor::timerCallback()
{
    constexpr int N = ds::Shared::fftSize;
    proc.copyLatest (N, shared.L.data(), shared.R.data());

    const double sr = proc.getSampleRate();
    shared.sampleRate = sr > 0.0 ? sr : 44100.0;

    // RMS (ventana completa) y energia rapida (ultimas 512 muestras)
    double sl = 0.0, sr2 = 0.0, fast = 0.0;
    for (int i = 0; i < N; ++i)
    {
        const double l = shared.L[(size_t) i], r = shared.R[(size_t) i];
        sl += l * l;
        sr2 += r * r;
        if (i >= N - 512) fast += 0.5 * (l * l + r * r);
    }
    shared.rmsL = (float) std::sqrt (sl / N);
    shared.rmsR = (float) std::sqrt (sr2 / N);
    const float fastF = (float) std::sqrt (fast / 512.0);

    shared.peakL = proc.peakL.exchange (0.f);
    shared.peakR = proc.peakR.exchange (0.f);
    shared.momentary  = proc.momentary.load();
    shared.shortTerm  = proc.shortTerm.load();
    shared.integrated = proc.integrated.load();

    // Nivel y golpes para la mascota
    const float db = juce::Decibels::gainToDecibels (std::max (shared.rmsL, shared.rmsR), -100.f);
    shared.level  = juce::jlimit (0.f, 1.f, (db + 48.f) / 42.f);
    shared.silent = db < -60.f;

    slowEnv += (fastF - slowEnv) * 0.06f;
    cooldown -= 1.f / 60.f;
    shared.onset = false;
    if (fastF > 0.015f && fastF > slowEnv * 1.7f + 0.005f && cooldown <= 0.f)
    {
        shared.onset = true;
        cooldown = 0.13f;
    }

    analyser.process (shared);

    for (auto* v : views)
        if (v->isVisible())
        {
            v->tick();
            v->repaint();
        }
}
