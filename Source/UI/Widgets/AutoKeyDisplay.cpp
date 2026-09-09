#include "AutoKeyDisplay.h"

namespace helix::ui
{

static const char* kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

AutoKeyDisplay::AutoKeyDisplay (HelixTuneProcessor& p) : processor (p)
{
    followButton.setColour (juce::TextButton::buttonOnColourId, colours::violet);
    addAndMakeVisible (followButton);

    followAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, params::autoKey, followButton);

    applyButton.setColour (juce::TextButton::buttonOnColourId, colours::violet);
    applyButton.onClick = [this] { applyDetectedKey(); };
    addAndMakeVisible (applyButton);

    startTimerHz (20);
}

AutoKeyDisplay::~AutoKeyDisplay() { stopTimer(); }

void AutoKeyDisplay::applyDetectedKey()
{
    const auto k = processor.getKeyEstimate();
    if (! k.valid)
        return;

    if (auto* p = processor.apvts.getParameter (params::key))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) k.rootPitchClass));

    // Index 1 is Major and 2 is Minor in the scale table.
    if (auto* p = processor.apvts.getParameter (params::scale))
        p->setValueNotifyingHost (p->convertTo0to1 (k.minor ? 2.0f : 1.0f));
}

void AutoKeyDisplay::timerCallback()
{
    last = processor.getKeyEstimate();

    for (int i = 0; i < 12; ++i)
    {
        const float target = processor.getChroma (i);
        smoothedChroma[i] += (target - smoothedChroma[i]) * 0.25f;
    }

    applyButton.setEnabled (last.valid);
    repaint();
}

void AutoKeyDisplay::resized()
{
    auto r = getLocalBounds().reduced (10);
    r.removeFromTop (20);          // section header

    auto buttons = r.removeFromBottom (24);
    followButton.setBounds (buttons.removeFromLeft (86));
    buttons.removeFromLeft (8);
    applyButton.setBounds (buttons.removeFromLeft (74));
}

void AutoKeyDisplay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (10);
    r.removeFromTop (20);
    r.removeFromBottom (30);

    // --- the answer -------------------------------------------------------
    auto headline = r.removeFromTop (34);

    if (last.valid)
    {
        g.setColour (colours::text);
        g.setFont (FuturisticLookAndFeel::uiFont (24.0f, true));
        g.drawText (juce::String (kNoteNames[last.rootPitchClass])
                        + (last.minor ? " min" : " maj"),
                    headline.removeFromLeft (110), juce::Justification::centredLeft, false);

        // Confidence as a short bar rather than a number: the exact value is
        // meaningless, the "is this trustworthy yet" reading is not.
        auto meter = headline.reduced (4, 12);
        g.setColour (colours::bgSunken);
        g.fillRoundedRectangle (meter.toFloat(), 2.0f);

        const auto c = last.confidence > 0.6f ? colours::lime
                     : last.confidence > 0.3f ? colours::amber : colours::danger;

        g.setColour (c);
        g.fillRoundedRectangle (meter.toFloat().withWidth (meter.getWidth() * last.confidence), 2.0f);
    }
    else
    {
        g.setColour (colours::textFaint);
        g.setFont (FuturisticLookAndFeel::uiFont (13.0f));
        g.drawText ("LISTENING...", headline, juce::Justification::centredLeft, false);
    }

    r.removeFromTop (6);

    // --- pitch-class histogram -------------------------------------------
    auto bars = r;
    if (bars.getHeight() < 8)
        return;

    float peak = 1.0e-4f;
    for (float v : smoothedChroma)
        peak = juce::jmax (peak, v);

    const float w = bars.getWidth() / 12.0f;

    for (int i = 0; i < 12; ++i)
    {
        const float h = juce::jlimit (0.0f, 1.0f, smoothedChroma[i] / peak) * (bars.getHeight() - 12.0f);
        const auto cell = juce::Rectangle<float> (bars.getX() + i * w, (float) bars.getY(),
                                                  w - 2.0f, (float) bars.getHeight());

        const bool isRoot = last.valid && i == last.rootPitchClass;

        g.setColour (isRoot ? colours::violet.withAlpha (0.85f) : colours::cyan.withAlpha (0.35f));
        g.fillRoundedRectangle (cell.withHeight (juce::jmax (1.0f, h))
                                    .withY (cell.getBottom() - 12.0f - h), 1.5f);

        g.setColour (isRoot ? colours::text : colours::textFaint);
        g.setFont (FuturisticLookAndFeel::monoFont (8.5f, isRoot));
        g.drawText (kNoteNames[i], cell.withHeight (11.0f).withY (cell.getBottom() - 11.0f),
                    juce::Justification::centred, false);
    }
}

} // namespace helix::ui
