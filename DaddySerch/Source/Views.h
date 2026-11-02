#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>

namespace ds
{
//==============================================================================
// Paleta 90s: neon sobre morado oscuro
namespace pal
{
    inline const juce::Colour bg      { 0xff0d0620 };
    inline const juce::Colour panel   { 0xff1a0e3a };
    inline const juce::Colour cyan    { 0xff00f0ff };
    inline const juce::Colour magenta { 0xffff2bd6 };
    inline const juce::Colour yellow  { 0xffffe600 };
    inline const juce::Colour lime    { 0xff7cff3a };
    inline const juce::Colour orange  { 0xffff8a1f };
    inline const juce::Colour red     { 0xffff3355 };
    inline const juce::Colour white   { 0xfff4f0ff };
    inline const juce::Colour ink     { 0xff10071f };
}

inline juce::Font boldFont (float h) { return juce::Font (juce::FontOptions (h).withStyle ("Bold")); }

//==============================================================================
// Datos compartidos que el editor actualiza 60 veces por segundo
struct Shared
{
    static constexpr int fftOrder = 12;
    static constexpr int fftSize  = 1 << fftOrder; // 4096
    static constexpr int numBins  = fftSize / 2;

    std::vector<float> L = std::vector<float> ((size_t) fftSize, 0.f);
    std::vector<float> R = std::vector<float> ((size_t) fftSize, 0.f);
    std::vector<float> magDb = std::vector<float> ((size_t) numBins, -120.f);

    double sampleRate = 44100.0;
    float peakL = 0.f, peakR = 0.f;   // pico desde el frame anterior (lineal)
    float rmsL = 0.f, rmsR = 0.f;     // RMS de la ventana (lineal)
    float momentary = -100.f, shortTerm = -100.f, integrated = -100.f;

    float level = 0.f;      // 0..1 para la mascota
    bool  onset = false;    // golpe/transitorio detectado
    bool  silent = true;
};

//==============================================================================
class SpectrumAnalyser
{
public:
    void process (Shared& s)
    {
        for (int i = 0; i < Shared::fftSize; ++i)
            data[(size_t) i] = 0.5f * (s.L[(size_t) i] + s.R[(size_t) i]);
        std::fill (data.begin() + Shared::fftSize, data.end(), 0.f);

        window.multiplyWithWindowingTable (data.data(), (size_t) Shared::fftSize);
        fft.performFrequencyOnlyForwardTransform (data.data());

        const float norm = 4.f / (float) Shared::fftSize; // seno a fondo de escala = 0 dB
        for (int i = 0; i < Shared::numBins; ++i)
            s.magDb[(size_t) i] = juce::Decibels::gainToDecibels (data[(size_t) i] * norm, -120.f);
    }

private:
    juce::dsp::FFT fft { Shared::fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) Shared::fftSize,
                                                 juce::dsp::WindowingFunction<float>::hann };
    std::array<float, 2 * Shared::fftSize> data {};
};

inline float binValue (const std::vector<float>& v, double sr, float f0, float f1)
{
    const int nb = (int) v.size();
    const float b0 = f0 * (float) Shared::fftSize / (float) sr;
    const float b1 = f1 * (float) Shared::fftSize / (float) sr;
    int i0 = (int) std::floor (b0), i1 = (int) std::ceil (b1);

    if (i1 - i0 <= 1)
    {
        const float c = 0.5f * (b0 + b1);
        const int i = juce::jlimit (0, nb - 2, (int) c);
        const float fr = juce::jlimit (0.f, 1.f, c - (float) i);
        return v[(size_t) i] * (1.f - fr) + v[(size_t) i + 1] * fr;
    }

    i0 = juce::jlimit (0, nb - 1, i0);
    i1 = juce::jlimit (0, nb - 1, i1);
    float m = -200.f;
    for (int i = i0; i <= i1; ++i)
        m = std::max (m, v[(size_t) i]);
    return m;
}

//==============================================================================
class ViewBase : public juce::Component
{
public:
    ViewBase (const juce::String& t, const Shared& s) : title (t), shared (s)
    {
        setInterceptsMouseClicks (false, false);
    }

    virtual void tick() {}
    const juce::String& getTitle() const { return title; }

protected:
    // Dibuja el marco y devuelve el area interior
    juce::Rectangle<float> frame (juce::Graphics& g, juce::Colour accent)
    {
        auto b = getLocalBounds().toFloat().reduced (3.f);
        g.setColour (pal::panel);
        g.fillRoundedRectangle (b, 10.f);
        g.setColour (accent.withAlpha (0.9f));
        g.drawRoundedRectangle (b, 10.f, 2.f);

        g.setColour (accent);
        g.setFont (boldFont (13.f));
        g.drawText (title, b.removeFromTop (22.f).reduced (10.f, 2.f), juce::Justification::centredLeft);
        return b.reduced (8.f, 4.f);
    }

    juce::String title;
    const Shared& shared;
};

//==============================================================================
// MASCOTA: personaje original estilo dibujos animados de los 90.
// Duerme en silencio; al entrar sonido despierta, abre la boca con el nivel,
// rebota, mueve los brazos y lanza estrellas, notas y ondas con cada golpe.
class MascotView : public ViewBase
{
public:
    explicit MascotView (const Shared& s) : ViewBase ("DADDY", s) {}

    void tick() override
    {
        const float dt = 1.f / 60.f;
        time += dt;
        level += (shared.level - level) * (shared.level > level ? 0.5f : 0.10f);
        silentTime = shared.silent ? silentTime + dt : 0.f;
        asleep = silentTime > 2.f;
        punch *= 0.86f;
        phase += dt * (2.f + level * 10.f);

        if (shared.onset)
        {
            punch = 1.f;
            burst();
        }

        if (asleep)
        {
            zzTimer -= dt;
            if (zzTimer <= 0.f)
            {
                zzTimer = 1.0f;
                addP (3, 0.20f, -0.30f, 0.03f, -0.10f, 0.f, 2.2f, 0.08f, pal::white);
            }
        }

        for (auto& p : parts)
        {
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            p.vy += p.g * dt;
            p.life -= dt;
            p.rot += p.vrot * dt;
            if (p.kind == 2) p.size += 0.9f * dt;
        }
        parts.erase (std::remove_if (parts.begin(), parts.end(),
                                     [] (const P& p) { return p.life <= 0.f; }),
                     parts.end());
    }

    void paint (juce::Graphics& g) override
    {
        auto area = frame (g, pal::magenta);
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (area.toNearestInt());

        drawBackdrop (g, area);

        const float s = std::min (area.getWidth() * 0.95f, area.getHeight() * 0.85f);
        const float k = s * (1.f + 0.06f * punch);
        const float bob = std::sin (phase) * s * (asleep ? 0.006f : 0.015f * (0.4f + level * 1.6f));
        const float cx = area.getCentreX();
        const float cy = area.getCentreY() - 0.06f * s + bob;
        const float ow = std::max (1.5f, 0.014f * k);

        auto R = [&] (float x, float y, float w, float h)
        {
            return juce::Rectangle<float> (cx + x * k, cy + y * k, w * k, h * k);
        };

        const juce::Colour skin (0xffffc08a), skinDark (0xffe89a62);

        // ondas detras del personaje
        for (auto& p : parts)
            if (p.kind == 2)
            {
                const float a = juce::jlimit (0.f, 1.f, p.life / p.maxLife);
                g.setColour (p.c.withAlpha (a));
                const float r = p.size * k;
                g.drawEllipse (cx - r, cy - 0.08f * k - r, 2.f * r, 2.f * r, 0.025f * k * a + 1.f);
            }

        // brazos (suben con cada golpe)
        const float handY = asleep ? 0.46f : (0.30f - 0.28f * punch - 0.05f * level);
        auto arm = [&] (float sx, float hx)
        {
            juce::Path p;
            p.startNewSubPath (cx + sx * k, cy + 0.20f * k);
            p.lineTo (cx + hx * k, cy + handY * k);
            g.setColour (pal::ink);
            g.strokePath (p, juce::PathStrokeType (0.062f * k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (pal::magenta);
            g.strokePath (p, juce::PathStrokeType (0.038f * k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            auto hand = R (hx - 0.06f, handY - 0.06f, 0.12f, 0.12f);
            g.setColour (skin);
            g.fillEllipse (hand);
            g.setColour (pal::ink);
            g.drawEllipse (hand, ow);
        };
        arm (-0.27f, -0.41f);
        arm ( 0.27f,  0.41f);

        // cuello y camiseta con rayas
        g.setColour (skinDark);
        g.fillRect (R (-0.06f, 0.06f, 0.12f, 0.14f));
        {
            auto shirt = R (-0.30f, 0.14f, 0.60f, 0.52f);
            g.setColour (pal::magenta);
            g.fillRoundedRectangle (shirt, 0.09f * k);
            juce::Graphics::ScopedSaveState s2 (g);
            juce::Path clip; clip.addRoundedRectangle (shirt, 0.09f * k);
            g.reduceClipRegion (clip);
            g.setColour (pal::cyan);   g.fillRect (R (-0.31f, 0.32f, 0.62f, 0.055f));
            g.setColour (pal::yellow); g.fillRect (R (-0.31f, 0.42f, 0.62f, 0.055f));
            g.setColour (pal::cyan);   g.fillRect (R (-0.31f, 0.52f, 0.62f, 0.055f));
            g.setColour (pal::ink);
            g.drawRoundedRectangle (shirt, 0.09f * k, ow);
        }

        // auriculares (copas pulsan con el nivel)
        const float pulse = level * 0.03f + punch * 0.02f;
        for (int side = -1; side <= 1; side += 2)
        {
            auto cup = R ((float) side * 0.29f - 0.055f - pulse * 0.5f, -0.155f - pulse * 0.5f,
                          0.11f + pulse, 0.21f + pulse);
            g.setColour (pal::cyan);
            g.fillRoundedRectangle (cup, 0.03f * k);
            g.setColour (pal::ink);
            g.drawRoundedRectangle (cup, 0.03f * k, ow);
        }

        // cabeza
        auto head = R (-0.25f, -0.34f, 0.50f, 0.52f);
        g.setColour (skin);
        g.fillEllipse (head);

        // pelo de puas (lima)
        {
            static const float pts[][2] = {
                { -0.25f, -0.18f }, { -0.30f, -0.40f }, { -0.18f, -0.31f }, { -0.15f, -0.52f },
                { -0.06f, -0.34f }, {  0.00f, -0.56f }, {  0.07f, -0.34f }, {  0.15f, -0.52f },
                {  0.18f, -0.31f }, {  0.30f, -0.40f }, {  0.25f, -0.18f } };
            juce::Path hair;
            for (size_t i = 0; i < std::size (pts); ++i)
            {
                const float px = cx + pts[i][0] * k;
                const float py = cy + (pts[i][1] - 0.02f * punch) * k;
                if (i == 0) hair.startNewSubPath (px, py); else hair.lineTo (px, py);
            }
            hair.closeSubPath();
            g.setColour (pal::lime);
            g.fillPath (hair);
            g.setColour (pal::ink);
            g.strokePath (hair, juce::PathStrokeType (ow, juce::PathStrokeType::mitered));
        }

        g.setColour (pal::ink);
        g.drawEllipse (head, ow);

        // diadema de los auriculares
        {
            juce::Path band;
            band.addCentredArc (cx, cy - 0.08f * k, 0.31f * k, 0.38f * k, 0.f, -1.5f, 1.5f, true);
            g.setColour (pal::ink);
            g.strokePath (band, juce::PathStrokeType (0.05f * k, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
            g.setColour (pal::yellow);
            g.strokePath (band, juce::PathStrokeType (0.03f * k, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        }

        // ojos: gafas oscuras (despierto) o ojos cerrados (dormido)
        if (! asleep)
        {
            const float flash = juce::jlimit (0.f, 1.f, level * 1.4f + punch * 0.5f);
            for (int side = -1; side <= 1; side += 2)
            {
                auto lens = R (side < 0 ? -0.225f : 0.025f, -0.14f, 0.20f, 0.125f);
                g.setColour (pal::ink);
                g.fillRoundedRectangle (lens, 0.04f * k);

                juce::ColourGradient gr (pal::cyan.withAlpha (flash), lens.getX(), lens.getY(),
                                         pal::magenta.withAlpha (flash), lens.getRight(), lens.getBottom(), false);
                g.setGradientFill (gr);
                g.fillRoundedRectangle (lens.reduced (0.012f * k), 0.035f * k);

                g.setColour (pal::white.withAlpha (0.85f));
                juce::Path glint;
                glint.startNewSubPath (lens.getX() + 0.04f * k, lens.getBottom() - 0.035f * k);
                glint.lineTo (lens.getX() + 0.085f * k, lens.getY() + 0.03f * k);
                g.strokePath (glint, juce::PathStrokeType (0.012f * k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                g.setColour (pal::ink);
                g.drawRoundedRectangle (lens, 0.04f * k, ow * 1.3f);
            }
            g.setColour (pal::ink);
            g.fillRect (R (-0.03f, -0.115f, 0.06f, 0.022f));
        }
        else
        {
            g.setColour (pal::ink);
            for (int side = -1; side <= 1; side += 2)
            {
                juce::Path eye;
                eye.addCentredArc (cx + (float) side * 0.115f * k, cy - 0.095f * k,
                                   0.07f * k, 0.035f * k, 0.f, 1.6f, 4.7f, true);
                g.strokePath (eye, juce::PathStrokeType (ow * 1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }

        // nariz
        g.setColour (skinDark);
        g.fillEllipse (R (-0.025f, -0.03f, 0.05f, 0.04f));

        // boca: se abre segun el nivel
        {
            const float open = asleep ? 0.012f : 0.025f + level * 0.16f + punch * 0.03f;
            auto mouth = R (-0.105f, 0.045f, 0.21f, open);
            g.setColour (juce::Colour (0xff35091f));
            g.fillEllipse (mouth);
            if (open > 0.06f)
            {
                juce::Graphics::ScopedSaveState s3 (g);
                juce::Path mp; mp.addEllipse (mouth);
                g.reduceClipRegion (mp);
                g.setColour (juce::Colour (0xffff6f91));
                g.fillEllipse (R (-0.06f, 0.045f + open * 0.58f, 0.12f, open * 0.7f));
            }
            g.setColour (pal::ink);
            g.drawEllipse (mouth, ow);

            if (asleep) // burbuja de ronquido
            {
                const float b = 0.025f + 0.02f * (0.5f + 0.5f * std::sin (time * 2.f));
                auto bubble = R (0.11f, 0.03f, b * 2.f, b * 2.f);
                g.setColour (pal::white.withAlpha (0.55f));
                g.fillEllipse (bubble);
                g.setColour (pal::ink);
                g.drawEllipse (bubble, ow * 0.7f);
            }
        }

        // particulas (estrellas, notas, Z)
        for (auto& p : parts)
        {
            const float a = juce::jlimit (0.f, 1.f, p.life / p.maxLife * 1.6f);
            const float px = cx + p.x * k, py = cy + (-0.08f + p.y) * k, sz = p.size * k;
            g.setColour (p.c.withAlpha (a));

            if (p.kind == 0)
            {
                juce::Path st;
                st.addStar ({ px, py }, 5, sz * 0.45f, sz, p.rot);
                g.fillPath (st);
                g.setColour (pal::ink.withAlpha (a));
                g.strokePath (st, juce::PathStrokeType (1.5f));
            }
            else if (p.kind == 1)
            {
                g.fillEllipse (px - sz * 0.5f, py, sz * 0.85f, sz * 0.6f);
                g.fillRect (px + sz * 0.22f, py - sz * 1.05f, sz * 0.14f, sz * 1.25f);
                g.fillRect (px + sz * 0.22f, py - sz * 1.05f, sz * 0.55f, sz * 0.22f);
            }
            else if (p.kind == 3)
            {
                g.setFont (boldFont (sz * 2.f));
                g.drawText ("Z", juce::Rectangle<float> (px - sz * 2.f, py - sz * 2.f, sz * 4.f, sz * 4.f),
                            juce::Justification::centred);
            }
        }
    }

private:
    struct P
    {
        int kind = 0;
        float x = 0, y = 0, vx = 0, vy = 0, g = 0, life = 1, maxLife = 1, size = 0.05f, rot = 0, vrot = 0;
        juce::Colour c;
    };

    void addP (int kind, float x, float y, float vx, float vy, float grav, float life, float size, juce::Colour c)
    {
        if (parts.size() > 160) return;
        P p;
        p.kind = kind; p.x = x; p.y = y; p.vx = vx; p.vy = vy; p.g = grav;
        p.life = p.maxLife = life; p.size = size; p.c = c;
        p.rot = rng.nextFloat() * 6.28f;
        p.vrot = (rng.nextFloat() - 0.5f) * 8.f;
        parts.push_back (p);
    }

    void burst()
    {
        const juce::Colour cols[] = { pal::yellow, pal::cyan, pal::magenta, pal::lime, pal::orange };
        const int n = 5 + (int) (level * 8.f);
        for (int i = 0; i < n; ++i)
        {
            const float ang = juce::MathConstants<float>::pi + rng.nextFloat() * juce::MathConstants<float>::pi;
            const float spd = 0.35f + rng.nextFloat() * 0.5f + level * 0.3f;
            addP (rng.nextInt (3) < 2 ? 0 : 1, std::cos (ang) * 0.05f, -0.25f,
                  std::cos (ang) * spd, std::sin (ang) * spd, 0.9f,
                  0.9f + rng.nextFloat() * 0.4f, 0.035f + rng.nextFloat() * 0.035f,
                  cols[rng.nextInt (5)]);
        }
        addP (2, 0.f, 0.f, 0.f, 0.f, 0.f, 0.45f, 0.30f, pal::cyan);
    }

    void drawBackdrop (juce::Graphics& g, juce::Rectangle<float> a)
    {
        juce::ColourGradient grad (juce::Colour (0xff3b0f7a), a.getX(), a.getY(),
                                   juce::Colour (0xff067a8f), a.getX(), a.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRect (a);

        // rayos tipo "sunburst" que se intensifican con el sonido
        {
            const auto c = a.getCentre();
            const float r = a.getWidth() + a.getHeight();
            g.setColour (pal::magenta.withAlpha (0.05f + 0.14f * level));
            for (int i = 0; i < 14; ++i)
            {
                const float a0 = time * 0.3f + (float) i * juce::MathConstants<float>::twoPi / 14.f;
                juce::Path ray;
                ray.startNewSubPath (c);
                ray.lineTo (c.x + r * std::cos (a0), c.y + r * std::sin (a0));
                ray.lineTo (c.x + r * std::cos (a0 + 0.16f), c.y + r * std::sin (a0 + 0.16f));
                ray.closeSubPath();
                g.fillPath (ray);
            }
        }

        // decoracion estilo Memphis
        struct Deco { float x, y, size; int kind; juce::uint32 col; float speed; };
        static const Deco decos[] = {
            { 0.08f, 0.22f, 0.06f, 0, 0xffffe600, 0.010f }, { 0.55f, 0.12f, 0.05f, 1, 0xff00f0ff, 0.014f },
            { 0.82f, 0.30f, 0.04f, 2, 0xffff2bd6, 0.008f }, { 0.30f, 0.80f, 0.05f, 3, 0xff7cff3a, 0.012f },
            { 0.70f, 0.78f, 0.06f, 0, 0xff00f0ff, 0.009f }, { 0.12f, 0.62f, 0.035f, 2, 0xffffe600, 0.015f },
            { 0.92f, 0.62f, 0.05f, 4, 0xffff8a1f, 0.011f }, { 0.45f, 0.45f, 0.04f, 1, 0xffff2bd6, 0.007f },
            { 0.22f, 0.08f, 0.04f, 3, 0xff00f0ff, 0.013f }, { 0.65f, 0.55f, 0.03f, 2, 0xff7cff3a, 0.010f },
            { 0.38f, 0.28f, 0.05f, 4, 0xffffe600, 0.012f }, { 0.88f, 0.90f, 0.05f, 1, 0xffffe600, 0.009f } };

        const float m = std::min (a.getWidth(), a.getHeight());
        for (const auto& d : decos)
        {
            const float x = a.getX() + std::fmod (d.x + time * d.speed, 1.f) * a.getWidth();
            const float y = a.getY() + d.y * a.getHeight() + std::sin (time * 1.2f + d.x * 10.f) * 3.f;
            const float sz = d.size * m;
            g.setColour (juce::Colour (d.col).withAlpha (0.8f));
            switch (d.kind)
            {
                case 0: { juce::Path t; t.addTriangle (x, y - sz, x - sz, y + sz, x + sz, y + sz);
                          g.strokePath (t, juce::PathStrokeType (3.f)); break; }
                case 1: { juce::Path w; w.startNewSubPath (x, y);
                          for (float t = 0.f; t <= 1.f; t += 0.05f)
                              w.lineTo (x + t * sz * 3.f, y + std::sin (t * 12.5f) * sz * 0.4f);
                          g.strokePath (w, juce::PathStrokeType (3.f)); break; }
                case 2: g.fillEllipse (x - sz * 0.5f, y - sz * 0.5f, sz, sz); break;
                case 3: g.fillRect (x - sz, y - 1.5f, sz * 2.f, 3.f); g.fillRect (x - 1.5f, y - sz, 3.f, sz * 2.f); break;
                default: g.drawEllipse (x - sz, y - sz, sz * 2.f, sz * 2.f, 3.f); break;
            }
        }
    }

    std::vector<P> parts;
    juce::Random rng;
    float time = 0.f, level = 0.f, punch = 0.f, phase = 0.f, silentTime = 0.f, zzTimer = 0.f;
    bool asleep = true;
};

//==============================================================================
class SpectrumView : public ViewBase
{
public:
    explicit SpectrumView (const Shared& s) : ViewBase ("SPECTRUM", s) {}

    void tick() override
    {
        for (size_t i = 0; i < smooth.size(); ++i)
        {
            const float v = shared.magDb[i];
            smooth[i] = v > smooth[i] ? smooth[i] + (v - smooth[i]) * 0.6f
                                      : smooth[i] + (v - smooth[i]) * 0.12f;
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto a = frame (g, pal::cyan);
        const float dbMin = -90.f, dbMax = -6.f, slope = 3.f; // pendiente 3 dB/oct (ruido rosa plano)

        auto yOf = [&] (float db) { return a.getBottom() - a.getHeight() * juce::jlimit (0.f, 1.f, (db - dbMin) / (dbMax - dbMin)); };
        auto xOf = [&] (float f)  { return a.getX() + a.getWidth() * std::log (f / 20.f) / std::log (1000.f); };

        g.setFont (10.f);
        for (float db : { -80.f, -60.f, -40.f, -20.f })
        {
            g.setColour (pal::white.withAlpha (0.08f));
            g.drawHorizontalLine ((int) yOf (db), a.getX(), a.getRight());
        }
        for (float f : { 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f })
        {
            const float x = xOf (f);
            g.setColour (pal::white.withAlpha (0.08f));
            g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
            g.setColour (pal::white.withAlpha (0.45f));
            g.drawText (f >= 1000.f ? juce::String ((int) (f / 1000.f)) + "k" : juce::String ((int) f),
                        (int) x + 2, (int) a.getBottom() - 13, 30, 12, juce::Justification::left);
        }

        juce::Path line;
        const float w = a.getWidth();
        const int step = 2;
        bool first = true;
        for (float px = 0.f; px <= w; px += (float) step)
        {
            const float f0 = 20.f * std::pow (1000.f, px / w);
            const float f1 = 20.f * std::pow (1000.f, std::min (w, px + (float) step) / w);
            const float fc = std::sqrt (f0 * f1);
            const float db = binValue (smooth, shared.sampleRate, f0, f1) + slope * std::log2 (fc / 1000.f);
            const float x = a.getX() + px, y = yOf (db);
            if (first) { line.startNewSubPath (x, y); first = false; } else line.lineTo (x, y);
        }

        juce::Path fill (line);
        fill.lineTo (a.getRight(), a.getBottom());
        fill.lineTo (a.getX(), a.getBottom());
        fill.closeSubPath();

        g.setGradientFill (juce::ColourGradient (pal::magenta.withAlpha (0.85f), 0.f, a.getY(),
                                                 pal::cyan.withAlpha (0.10f), 0.f, a.getBottom(), false));
        g.fillPath (fill);
        g.setColour (pal::white.withAlpha (0.9f));
        g.strokePath (line, juce::PathStrokeType (1.5f));
    }

private:
    std::vector<float> smooth = std::vector<float> ((size_t) Shared::numBins, -120.f);
};

//==============================================================================
class ScopeView : public ViewBase
{
public:
    explicit ScopeView (const Shared& s) : ViewBase ("OSCILLOSCOPE", s) {}

    void paint (juce::Graphics& g) override
    {
        auto a = frame (g, pal::lime);
        const int N = Shared::fftSize, W = 1024;

        g.setColour (pal::white.withAlpha (0.10f));
        g.drawHorizontalLine ((int) a.getCentreY(), a.getX(), a.getRight());
        g.drawHorizontalLine ((int) (a.getCentreY() - a.getHeight() * 0.25f), a.getX(), a.getRight());
        g.drawHorizontalLine ((int) (a.getCentreY() + a.getHeight() * 0.25f), a.getX(), a.getRight());

        auto mono = [&] (int i) { return 0.5f * (shared.L[(size_t) i] + shared.R[(size_t) i]); };

        // disparo: ultimo cruce por cero ascendente
        int t = N - W;
        for (int i = N - W - 1; i > 1; --i)
            if (mono (i - 1) < 0.f && mono (i) >= 0.f) { t = i; break; }

        juce::Path p;
        for (int i = 0; i < W; ++i)
        {
            const float x = a.getX() + a.getWidth() * (float) i / (float) (W - 1);
            const float y = a.getCentreY() - juce::jlimit (-1.f, 1.f, mono (t + i)) * a.getHeight() * 0.5f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (pal::lime.withAlpha (0.25f));
        g.strokePath (p, juce::PathStrokeType (5.f));
        g.setColour (pal::lime);
        g.strokePath (p, juce::PathStrokeType (1.6f));
    }
};

//==============================================================================
class StereometerView : public ViewBase
{
public:
    explicit StereometerView (const Shared& s) : ViewBase ("STEREOMETER", s) {}

    void tick() override
    {
        double ll = 0, rr = 0, lr = 0;
        for (int i = Shared::fftSize - 2048; i < Shared::fftSize; ++i)
        {
            const double l = shared.L[(size_t) i], r = shared.R[(size_t) i];
            ll += l * l; rr += r * r; lr += l * r;
        }
        const float c = (ll > 1e-9 && rr > 1e-9) ? (float) (lr / std::sqrt (ll * rr)) : 0.f;
        corr += (c - corr) * 0.15f;
    }

    void paint (juce::Graphics& g) override
    {
        auto a = frame (g, pal::yellow);
        auto bar = a.removeFromBottom (18.f);
        const float radius = std::min (a.getWidth(), a.getHeight()) * 0.48f;
        const auto c = a.getCentre();

        g.setColour (pal::white.withAlpha (0.12f));
        g.drawEllipse (c.x - radius, c.y - radius, radius * 2.f, radius * 2.f, 1.f);
        g.drawLine (c.x - radius, c.y, c.x + radius, c.y);
        g.drawLine (c.x, c.y - radius, c.x, c.y + radius);
        const float d = radius * 0.7071f;
        g.drawLine (c.x - d, c.y + d, c.x + d, c.y - d);
        g.drawLine (c.x - d, c.y - d, c.x + d, c.y + d);

        g.setColour (pal::white.withAlpha (0.5f));
        g.setFont (10.f);
        g.drawText ("M", (int) c.x - 10, (int) (c.y - radius) - 1, 20, 12, juce::Justification::centred);
        g.drawText ("L", (int) (c.x - d) - 14, (int) (c.y - d) - 6, 14, 12, juce::Justification::centred);
        g.drawText ("R", (int) (c.x + d), (int) (c.y - d) - 6, 14, 12, juce::Justification::centred);

        juce::Path p;
        const int n = 1024;
        for (int i = 0; i < n; ++i)
        {
            const size_t idx = (size_t) (Shared::fftSize - n + i);
            const float l = shared.L[idx], r = shared.R[idx];
            const float m = (l + r) * 0.7071f, s = (l - r) * 0.7071f;
            const float x = c.x + juce::jlimit (-1.f, 1.f, s) * radius;
            const float y = c.y - juce::jlimit (-1.f, 1.f, m) * radius;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (pal::yellow.withAlpha (0.2f));
        g.strokePath (p, juce::PathStrokeType (3.5f));
        g.setColour (pal::cyan.withAlpha (0.8f));
        g.strokePath (p, juce::PathStrokeType (1.1f));

        // correlacion de fase
        auto b = bar.reduced (4.f, 4.f);
        g.setColour (pal::ink);
        g.fillRoundedRectangle (b, 3.f);
        const float mid = b.getCentreX();
        const float x = mid + corr * b.getWidth() * 0.5f;
        g.setColour (corr >= 0.f ? pal::lime : pal::red);
        g.fillRect (juce::Rectangle<float> (std::min (mid, x), b.getY(), std::abs (x - mid), b.getHeight()));
        g.setColour (pal::white.withAlpha (0.6f));
        g.drawVerticalLine ((int) mid, b.getY() - 2.f, b.getBottom() + 2.f);
    }

private:
    float corr = 0.f;
};

//==============================================================================
class SpectrogramView : public ViewBase
{
public:
    explicit SpectrogramView (const Shared& s)
        : ViewBase ("SPECTROGRAM", s), img (juce::Image::ARGB, W, H, true)
    {
        juce::ColourGradient gr (juce::Colours::black, 0.f, 0.f, juce::Colours::white, 1.f, 0.f, false);
        gr.addColour (0.18, juce::Colour (0xff2a0a6e));
        gr.addColour (0.42, juce::Colour (0xffd01ca8));
        gr.addColour (0.68, juce::Colour (0xffff8a1f));
        gr.addColour (0.88, juce::Colour (0xffffe600));
        for (int i = 0; i < 256; ++i)
            lut[(size_t) i] = gr.getColourAtPosition ((double) i / 255.0);
        img.clear (img.getBounds(), juce::Colours::black);
    }

    void tick() override
    {
        img.moveImageSection (0, 0, 1, 0, W - 1, H);
        const float slope = 3.f;
        for (int y = 0; y < H; ++y)
        {
            const float frac = 1.f - (float) y / (float) (H - 1);
            const float f0 = 20.f * std::pow (1000.f, frac);
            const float f1 = 20.f * std::pow (1000.f, std::min (1.f, frac + 1.f / (float) H));
            const float db = binValue (shared.magDb, shared.sampleRate, f0, f1)
                           + slope * std::log2 (std::sqrt (f0 * f1) / 1000.f);
            const float v = juce::jlimit (0.f, 1.f, (db + 90.f) / 80.f);
            img.setPixelAt (W - 1, y, lut[(size_t) (v * 255.f)]);
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto a = frame (g, pal::orange);
        g.drawImage (img, a, juce::RectanglePlacement::stretchToFit);

        g.setFont (10.f);
        for (float f : { 100.f, 1000.f, 10000.f })
        {
            const float y = a.getY() + a.getHeight() * (1.f - std::log (f / 20.f) / std::log (1000.f));
            g.setColour (pal::white.withAlpha (0.15f));
            g.drawHorizontalLine ((int) y, a.getX(), a.getRight());
            g.setColour (pal::white.withAlpha (0.7f));
            g.drawText (f >= 1000.f ? juce::String ((int) (f / 1000.f)) + "k" : juce::String ((int) f),
                        (int) a.getX() + 3, (int) y - 12, 30, 12, juce::Justification::left);
        }
    }

private:
    static constexpr int W = 512, H = 256;
    juce::Image img;
    std::array<juce::Colour, 256> lut;
};

//==============================================================================
class LevelsView : public ViewBase
{
public:
    explicit LevelsView (const Shared& s) : ViewBase ("LEVELS / LUFS", s) {}

    void tick() override
    {
        const float dt = 1.f / 60.f;
        upd (shared.peakL, dispL, holdL, holdTL, dt);
        upd (shared.peakR, dispR, holdR, holdTR, dt);
        rmsL += (shared.rmsL - rmsL) * (shared.rmsL > rmsL ? 0.3f : 0.06f);
        rmsR += (shared.rmsR - rmsR) * (shared.rmsR > rmsR ? 0.3f : 0.06f);
    }

    void paint (juce::Graphics& g) override
    {
        auto a = frame (g, pal::cyan);
        auto bars = a.removeFromLeft (a.getWidth() * 0.50f);
        auto scale = bars.removeFromLeft (28.f);

        const float dbMin = -60.f;
        auto yOf = [&] (float db) { return bars.getBottom() - bars.getHeight() * juce::jlimit (0.f, 1.f, (db - dbMin) / -dbMin); };

        g.setFont (10.f);
        for (float db : { 0.f, -6.f, -12.f, -24.f, -36.f, -48.f })
        {
            g.setColour (pal::white.withAlpha (0.5f));
            g.drawText (juce::String ((int) db), scale.toNearestInt().withY ((int) yOf (db) - 6).withHeight (12),
                        juce::Justification::centredRight);
            g.setColour (pal::white.withAlpha (0.08f));
            g.drawHorizontalLine ((int) yOf (db), bars.getX(), bars.getRight());
        }

        const float gap = 8.f, bw = (bars.getWidth() - gap) / 2.f;
        auto drawBar = [&] (juce::Rectangle<float> r, float peak, float rms, float hold)
        {
            g.setColour (pal::ink);
            g.fillRoundedRectangle (r, 3.f);

            auto toDb = [] (float v) { return juce::Decibels::gainToDecibels (v, -100.f); };
            const float yp = yOf (toDb (peak));
            juce::ColourGradient gr (pal::red, 0.f, yOf (0.f), pal::lime, 0.f, yOf (-24.f), false);
            gr.addColour (0.12, pal::orange);
            gr.addColour (0.3, pal::yellow);
            g.setGradientFill (gr);
            g.setOpacity (0.55f);
            g.fillRect (juce::Rectangle<float> (r.getX(), yp, r.getWidth(), r.getBottom() - yp));

            const float yr = yOf (toDb (rms));
            g.setGradientFill (gr);
            g.fillRect (juce::Rectangle<float> (r.getX() + r.getWidth() * 0.25f, yr, r.getWidth() * 0.5f, r.getBottom() - yr));

            g.setColour (pal::white);
            const float yh = yOf (toDb (hold));
            g.fillRect (r.getX(), yh - 1.f, r.getWidth(), 2.f);

            // indicador de clip
            g.setColour (toDb (hold) >= -0.1f ? pal::red : pal::red.withAlpha (0.15f));
            g.fillRect (r.getX(), r.getY() - 7.f, r.getWidth(), 4.f);
        };

        drawBar ({ bars.getX(), bars.getY() + 8.f, bw, bars.getHeight() - 8.f }, dispL, rmsL, holdL);
        drawBar ({ bars.getX() + bw + gap, bars.getY() + 8.f, bw, bars.getHeight() - 8.f }, dispR, rmsR, holdR);

        // lecturas numericas
        auto txt = a.reduced (10.f, 0.f);
        const float fh = juce::jlimit (11.f, 24.f, txt.getHeight() / 9.f);
        auto fmt = [] (float v) { return v <= -99.f ? juce::String ("-inf") : juce::String (v, 1); };
        const float peakDb = juce::Decibels::gainToDecibels (std::max (holdL, holdR), -100.f);

        auto row = [&] (const juce::String& label, const juce::String& value, juce::Colour c)
        {
            auto r = txt.removeFromTop (fh * 2.1f);
            g.setColour (c);
            g.setFont (boldFont (fh * 0.8f));
            g.drawText (label, r.removeFromTop (fh), juce::Justification::centredLeft);
            g.setFont (boldFont (fh * 1.25f));
            g.drawText (value, r, juce::Justification::topLeft);
        };
        row ("MOMENTARY LUFS", fmt (shared.momentary), pal::cyan);
        row ("SHORT-TERM LUFS", fmt (shared.shortTerm), pal::magenta);
        row ("INTEGRATED LUFS", fmt (shared.integrated), pal::yellow);
        row ("PEAK dBFS", fmt (peakDb), peakDb >= -0.1f ? pal::red : pal::lime);
    }

private:
    static void upd (float inst, float& disp, float& hold, float& holdT, float dt)
    {
        disp = std::max (inst, disp * 0.90f);
        if (inst >= hold) { hold = inst; holdT = 0.f; }
        else { holdT += dt; if (holdT > 1.5f) hold *= 0.96f; }
    }

    float dispL = 0, dispR = 0, holdL = 0, holdR = 0, holdTL = 0, holdTR = 0, rmsL = 0, rmsR = 0;
};

} // namespace ds
