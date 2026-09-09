#include "ScaleKeyboard.h"
#include "../../DSP/ScaleQuantizer.h"

namespace helix::ui
{

static const char* kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const int   kWhiteClasses[7] = { 0, 2, 4, 5, 7, 9, 11 };

// Black keys sit after these white-key indices.
static const int kBlackAfterWhite[5] = { 0, 1, 3, 4, 5 };
static const int kBlackClasses[5]    = { 1, 3, 6, 8, 10 };

ScaleKeyboard::ScaleKeyboard (HelixTuneProcessor& p) : processor (p)
{
    setInterceptsMouseClicks (true, false);
    startTimerHz (30);
}

ScaleKeyboard::~ScaleKeyboard() { stopTimer(); }

bool ScaleKeyboard::isBlackKey (int pc)
{
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

void ScaleKeyboard::setLiveNote (float midiNote, bool voiced)
{
    liveNote = midiNote;
    liveVoiced = voiced;

    if (voiced)
        livePulse = 1.0f;
}

void ScaleKeyboard::timerCallback()
{
    livePulse = juce::jmax (0.0f, livePulse - 0.06f);

    // Repaint unconditionally: the key and scale parameters can change from the
    // host with no signal present, and this view has to follow them.
    repaint();
}

bool ScaleKeyboard::isInScale (int pc) const
{
    const int key = (int) *processor.apvts.getRawParameterValue (params::key);
    const int scaleIdx = (int) *processor.apvts.getRawParameterValue (params::scale);

    const auto* table = getScaleTable();
    const auto mask = table[juce::jlimit (0, getNumScales() - 1, scaleIdx)].mask;

    const int degree = ((pc - key) % 12 + 12) % 12;
    return ((mask >> degree) & 1) != 0;
}

juce::Rectangle<float> ScaleKeyboard::keyBounds (int pc) const
{
    auto area = getLocalBounds().toFloat();
    area.removeFromBottom (legendHeight);

    const float whiteW = area.getWidth() / 7.0f;

    for (int i = 0; i < 7; ++i)
        if (kWhiteClasses[i] == pc)
            return { area.getX() + (float) i * whiteW, area.getY(), whiteW, area.getHeight() };

    for (int i = 0; i < 5; ++i)
    {
        if (kBlackClasses[i] != pc)
            continue;

        const float w = whiteW * 0.62f;
        const float x = area.getX() + (float) (kBlackAfterWhite[i] + 1) * whiteW - w * 0.5f;
        return { x, area.getY(), w, area.getHeight() * 0.60f };
    }

    return {};
}

int ScaleKeyboard::pitchClassAt (juce::Point<int> pos) const
{
    const auto p = pos.toFloat();

    // Black keys are drawn on top, so they must be hit-tested first.
    for (int i = 0; i < 5; ++i)
        if (keyBounds (kBlackClasses[i]).contains (p))
            return kBlackClasses[i];

    for (int i = 0; i < 7; ++i)
        if (keyBounds (kWhiteClasses[i]).contains (p))
            return kWhiteClasses[i];

    return -1;
}

void ScaleKeyboard::mouseDown (const juce::MouseEvent& e)
{
    const int pc = pitchClassAt (e.getPosition());
    if (pc < 0)
        return;

    if (e.mods.isRightButtonDown())
    {
        processor.setNoteState (pc, NoteState::normal);
    }
    else
    {
        const auto current = processor.getNoteState (pc);
        const auto next = (current == NoteState::normal)  ? NoteState::removed
                        : (current == NoteState::removed) ? NoteState::bypassed
                                                          : NoteState::normal;
        processor.setNoteState (pc, next);
    }

    if (onStateChanged)
        onStateChanged();

    repaint();
}

void ScaleKeyboard::mouseMove (const juce::MouseEvent& e)
{
    const int pc = pitchClassAt (e.getPosition());
    if (pc != hoveredClass)
    {
        hoveredClass = pc;
        repaint();
    }
}

void ScaleKeyboard::mouseExit (const juce::MouseEvent&)
{
    hoveredClass = -1;
    repaint();
}

void ScaleKeyboard::paint (juce::Graphics& g)
{
    const int key = (int) *processor.apvts.getRawParameterValue (params::key);
    const int livePc = (liveNote > 0.0f) ? (((int) std::lround (liveNote)) % 12 + 12) % 12 : -1;

    auto drawKey = [&] (int pc)
    {
        const auto r = keyBounds (pc);
        if (r.isEmpty())
            return;

        const bool black = isBlackKey (pc);
        const bool inScale = isInScale (pc);
        const bool isRoot = (pc == key);
        const auto state = processor.getNoteState (pc);

        juce::Colour base = black ? colours::bgSunken : colours::bgRaised;

        if (inScale)
            base = black ? colours::cyan.withAlpha (0.16f) : colours::cyan.withAlpha (0.10f);

        if (state == NoteState::removed)
            base = colours::danger.withAlpha (0.20f);
        else if (state == NoteState::bypassed)
            base = colours::amber.withAlpha (0.20f);

        if (pc == hoveredClass)
            base = base.brighter (0.25f);

        g.setColour (base);
        g.fillRoundedRectangle (r.reduced (1.0f), 3.0f);

        // Root gets a solid underline; scale membership only a soft outline.
        g.setColour (isRoot ? colours::cyan.withAlpha (0.9f)
                            : (inScale ? colours::cyan.withAlpha (0.35f) : colours::outline));
        g.drawRoundedRectangle (r.reduced (1.0f), 3.0f, isRoot ? 1.6f : 1.0f);

        if (isRoot)
        {
            auto bar = r.reduced (5.0f).removeFromBottom (2.5f);
            g.setColour (colours::cyan);
            g.fillRoundedRectangle (bar, 1.25f);
        }

        // Live pitch indicator.
        if (pc == livePc && livePulse > 0.0f)
        {
            const float cents = (liveNote - std::round (liveNote)) * 100.0f;
            glowEllipse (g, juce::Rectangle<float> (9.0f, 9.0f)
                              .withCentre ({ r.getCentreX(), r.getY() + 9.0f }),
                         liveVoiced ? colours::lime : colours::textDim, 1.4f, livePulse);

            g.setColour (colours::lime.withAlpha (livePulse));
            g.setFont (FuturisticLookAndFeel::monoFont (9.0f));
            g.drawText (juce::String (cents, 0),
                        juce::Rectangle<float> (r.getX(), r.getY() + 16.0f, r.getWidth(), 11.0f),
                        juce::Justification::centred, false);
        }

        // State glyph: a cross for removed, a dash for bypassed.
        if (state != NoteState::normal)
        {
            const auto c = (state == NoteState::removed) ? colours::danger : colours::amber;
            const auto box = juce::Rectangle<float> (11.0f, 11.0f)
                                 .withCentre ({ r.getCentreX(), r.getBottom() - 20.0f });
            juce::Path p;

            if (state == NoteState::removed)
            {
                p.startNewSubPath (box.getTopLeft());
                p.lineTo (box.getBottomRight());
                p.startNewSubPath (box.getTopRight());
                p.lineTo (box.getBottomLeft());
            }
            else
            {
                p.startNewSubPath (box.getX(), box.getCentreY());
                p.lineTo (box.getRight(), box.getCentreY());
            }

            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        }

        g.setColour (inScale ? colours::text.withAlpha (0.9f) : colours::textFaint);
        g.setFont (FuturisticLookAndFeel::uiFont (10.0f, isRoot));
        g.drawText (kNoteNames[pc], r.reduced (2.0f).removeFromBottom (13.0f),
                    juce::Justification::centred, false);
    };

    for (int i = 0; i < 7; ++i)
        drawKey (kWhiteClasses[i]);

    for (int i = 0; i < 5; ++i)
        drawKey (kBlackClasses[i]);

    // legend
    auto legend = getLocalBounds().toFloat().removeFromBottom (legendHeight);
    g.setFont (FuturisticLookAndFeel::uiFont (9.5f));

    struct Item { juce::Colour c; const char* text; };
    const Item items[] = { { colours::cyan,    "IN SCALE" },
                           { colours::danger,  "REMOVED" },
                           { colours::amber,   "BYPASSED" } };

    float x = legend.getX();
    for (const auto& it : items)
    {
        g.setColour (it.c);
        g.fillRoundedRectangle ({ x, legend.getCentreY() - 3.0f, 6.0f, 6.0f }, 1.5f);

        g.setColour (colours::textFaint);
        const float tw = 58.0f;
        g.drawText (it.text, juce::Rectangle<float> (x + 9.0f, legend.getY(), tw, legend.getHeight()),
                    juce::Justification::centredLeft, false);
        x += tw + 14.0f;
    }

    g.setColour (colours::textFaint);
    g.drawText ("CLICK TO CYCLE  /  RIGHT-CLICK RESETS",
                legend.withTrimmedLeft (legend.getWidth() * 0.55f),
                juce::Justification::centredRight, false);
}

} // namespace helix::ui
