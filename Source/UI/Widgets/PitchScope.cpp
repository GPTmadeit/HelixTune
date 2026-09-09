#include "PitchScope.h"
#include "../../DSP/ScaleQuantizer.h"

namespace helix::ui
{

static const char* kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

PitchScope::PitchScope (HelixTuneProcessor& p) : processor (p)
{
    startTimerHz (30);
}

PitchScope::~PitchScope() { stopTimer(); }

void PitchScope::clear()
{
    history.clear();
    repaint();
}

void PitchScope::pushFrames (const std::vector<PitchFrame>& frames)
{
    for (const auto& f : frames)
    {
        history.push_back (f);
        if (history.size() > historyLength)
            history.pop_front();
    }

    if (! frames.empty())
    {
        const auto& last = frames.back();
        lastVoiced = last.voiced;

        if (last.voiced)
        {
            lastNote = last.detectedMidi;
            lastCents = (last.detectedMidi - std::round (last.detectedMidi)) * 100.0f;
        }
    }
}

void PitchScope::timerCallback()
{
    // Retarget the vertical window from whatever is actually on screen.
    float lo = 1000.0f, hi = -1000.0f;
    for (const auto& f : history)
    {
        if (! f.voiced)
            continue;

        lo = std::min (lo, std::min (f.detectedMidi, f.outputMidi));
        hi = std::max (hi, std::max (f.detectedMidi, f.outputMidi));
    }

    if (hi > lo)
    {
        const float centre = (lo + hi) * 0.5f;
        const float span = juce::jmax (11.0f, (hi - lo) + 4.0f);
        targetLow  = centre - span * 0.5f;
        targetHigh = centre + span * 0.5f;
    }

    viewLow  += (targetLow  - viewLow)  * 0.12f;
    viewHigh += (targetHigh - viewHigh) * 0.12f;

    repaint();
}

bool PitchScope::isInScale (int pc) const
{
    const int key = (int) *processor.apvts.getRawParameterValue (params::key);
    const int scaleIdx = (int) *processor.apvts.getRawParameterValue (params::scale);

    const auto* table = getScaleTable();
    const auto mask = table[juce::jlimit (0, getNumScales() - 1, scaleIdx)].mask;

    const int degree = ((pc - key) % 12 + 12) % 12;
    return ((mask >> degree) & 1) != 0;
}

float PitchScope::pitchToY (float midi, juce::Rectangle<float> plot) const
{
    const float span = juce::jmax (0.001f, viewHigh - viewLow);
    const float t = (midi - viewLow) / span;
    return plot.getBottom() - t * plot.getHeight();
}

void PitchScope::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    drawGlassPanel (g, area, colours::cyan, 6.0f);

    auto inner = area.reduced (2.0f);
    auto meter = inner.removeFromRight (meterWidth);
    auto gutter = inner.removeFromLeft (gutterWidth);
    auto plot = inner.reduced (2.0f);

    const int lowNote = (int) std::floor (viewLow);
    const int highNote = (int) std::ceil (viewHigh);

    // Everything inside this scope is clipped to the plot; the gutter and the
    // readout below it are not.
    {
    juce::Graphics::ScopedSaveState clipToPlot (g);
    g.reduceClipRegion (plot.toNearestInt());

    // --- pitch grid -------------------------------------------------------
    for (int n = lowNote; n <= highNote; ++n)
    {
        const float y = pitchToY ((float) n, plot);
        if (y < plot.getY() - 2.0f || y > plot.getBottom() + 2.0f)
            continue;

        const int pc = ((n % 12) + 12) % 12;
        const bool inScale = isInScale (pc);

        g.setColour (inScale ? colours::cyan.withAlpha (0.13f) : colours::grid.withAlpha (0.55f));
        g.fillRect (plot.getX(), y - 0.5f, plot.getWidth(), inScale ? 1.0f : 0.6f);
    }

    if (history.size() >= 2)
    {
        const float dx = plot.getWidth() / (float) (historyLength - 1);
        const float xStart = plot.getRight() - (float) (history.size() - 1) * dx;

        auto xAt = [&] (size_t i) { return xStart + (float) i * dx; };

        // --- correction band ---------------------------------------------
        // Shading the gap between sung and corrected makes over-correction
        // obvious without having to read two overlapping lines.
        juce::Path band;
        bool bandOpen = false;

        for (size_t i = 0; i < history.size(); ++i)
        {
            const auto& f = history[i];
            if (! f.voiced)
            {
                bandOpen = false;
                continue;
            }

            if (! bandOpen)
            {
                band.startNewSubPath (xAt (i), pitchToY (f.detectedMidi, plot));
                bandOpen = true;
            }

            band.lineTo (xAt (i), pitchToY (f.detectedMidi, plot));
        }

        for (size_t k = history.size(); k-- > 0;)
        {
            const auto& f = history[k];
            if (f.voiced)
                band.lineTo (xAt (k), pitchToY (f.outputMidi, plot));
        }

        band.closeSubPath();
        g.setColour (colours::magenta.withAlpha (0.14f));
        g.fillPath (band);

        // --- traces -------------------------------------------------------
        auto buildTrace = [&] (bool useOutput)
        {
            juce::Path p;
            bool open = false;

            for (size_t i = 0; i < history.size(); ++i)
            {
                const auto& f = history[i];

                if (! f.voiced)
                {
                    open = false;
                    continue;
                }

                const float y = pitchToY (useOutput ? f.outputMidi : f.detectedMidi, plot);

                if (! open)
                {
                    p.startNewSubPath (xAt (i), y);
                    open = true;
                }
                else
                {
                    p.lineTo (xAt (i), y);
                }
            }

            return p;
        };

        glowPath (g, buildTrace (false), colours::cyan, 1.6f, 0.85f);
        glowPath (g, buildTrace (true),  colours::magenta, 2.0f, 1.0f);

        // playhead edge
        g.setColour (colours::text.withAlpha (0.25f));
        g.fillRect (plot.getRight() - 1.0f, plot.getY(), 1.0f, plot.getHeight());
    }
    else
    {
        g.setColour (colours::textFaint);
        g.setFont (FuturisticLookAndFeel::uiFont (12.0f));
        g.drawText ("AWAITING SIGNAL", plot, juce::Justification::centred, false);
    }
    }

    // --- note gutter ------------------------------------------------------
    g.setFont (FuturisticLookAndFeel::monoFont (9.5f));
    for (int n = lowNote; n <= highNote; ++n)
    {
        const float y = pitchToY ((float) n, plot);
        if (y < plot.getY() || y > plot.getBottom())
            continue;

        const int pc = ((n % 12) + 12) % 12;
        if (pc != 0 && ! isInScale (pc))
            continue;

        g.setColour (pc == 0 ? colours::text.withAlpha (0.75f) : colours::textFaint);
        g.drawText (juce::String (kNoteNames[pc]) + juce::String (n / 12 - 1),
                    juce::Rectangle<float> (gutter.getX(), y - 7.0f, gutter.getWidth() - 3.0f, 14.0f),
                    juce::Justification::centredRight, false);
    }

    // --- readout ----------------------------------------------------------
    auto readout = meter.reduced (6.0f);

    if (lastNote > 0.0f)
    {
        const int nearest = (int) std::lround (lastNote);
        const int pc = ((nearest % 12) + 12) % 12;

        g.setColour (lastVoiced ? colours::text : colours::textFaint);
        g.setFont (FuturisticLookAndFeel::uiFont (26.0f, true));
        g.drawText (juce::String (kNoteNames[pc]) + juce::String (nearest / 12 - 1),
                    readout.removeFromTop (30.0f), juce::Justification::centred, false);
    }

    // cents deviation bar, centred on zero
    auto bar = readout.removeFromTop (26.0f).reduced (2.0f, 8.0f);
    g.setColour (colours::bgSunken);
    g.fillRoundedRectangle (bar, 3.0f);

    const float dev = juce::jlimit (-50.0f, 50.0f, lastCents);
    const float cx = bar.getCentreX();
    const float halfW = bar.getWidth() * 0.5f;
    const float devX = cx + (dev / 50.0f) * halfW;

    const auto devColour = (std::abs (dev) < 8.0f) ? colours::lime
                         : (std::abs (dev) < 25.0f) ? colours::amber
                                                    : colours::danger;

    if (lastVoiced)
    {
        g.setColour (devColour.withAlpha (0.35f));
        g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (cx, devX), bar.getY(),
                                                        std::abs (devX - cx), bar.getHeight()), 2.0f);

        g.setColour (devColour);
        g.fillRect (devX - 1.0f, bar.getY(), 2.0f, bar.getHeight());
    }

    g.setColour (colours::text.withAlpha (0.35f));
    g.fillRect (cx - 0.5f, bar.getY() - 2.0f, 1.0f, bar.getHeight() + 4.0f);

    g.setColour (lastVoiced ? devColour : colours::textFaint);
    g.setFont (FuturisticLookAndFeel::monoFont (12.0f, true));
    g.drawText ((lastCents >= 0.0f ? "+" : "") + juce::String (lastCents, 1) + " ct",
                readout.removeFromTop (16.0f), juce::Justification::centred, false);

    g.setColour (colours::textFaint);
    g.setFont (FuturisticLookAndFeel::uiFont (9.0f, true));
    g.drawText (lastVoiced ? "TRACKING" : "NO PITCH",
                readout.removeFromTop (12.0f), juce::Justification::centred, false);

    // legend
    auto legend = readout.removeFromBottom (26.0f);
    g.setFont (FuturisticLookAndFeel::uiFont (9.0f));

    g.setColour (colours::cyan);
    g.fillRect (legend.getX(), legend.getY() + 4.0f, 10.0f, 2.0f);
    g.setColour (colours::textFaint);
    g.drawText ("SUNG", legend.withTrimmedLeft (14.0f).withHeight (11.0f),
                juce::Justification::centredLeft, false);

    g.setColour (colours::magenta);
    g.fillRect (legend.getX(), legend.getY() + 17.0f, 10.0f, 2.0f);
    g.setColour (colours::textFaint);
    g.drawText ("CORRECTED", legend.withTrimmedLeft (14.0f).withTrimmedTop (13.0f).withHeight (11.0f),
                juce::Justification::centredLeft, false);
}

} // namespace helix::ui
