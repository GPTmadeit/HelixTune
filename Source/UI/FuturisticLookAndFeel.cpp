#include "FuturisticLookAndFeel.h"

namespace helix::ui
{

void glowPath (juce::Graphics& g, const juce::Path& p, juce::Colour c,
               float thickness, float intensity)
{
    // Three widening strokes at falling alpha read as a bloom at a fraction of
    // the cost of an actual blur, and stay crisp at any UI scale.
    struct Layer { float mul, alpha; };
    static constexpr Layer layers[] = { { 5.0f, 0.07f }, { 2.6f, 0.14f }, { 1.0f, 1.0f } };

    for (const auto& l : layers)
    {
        g.setColour (c.withMultipliedAlpha (l.alpha * intensity));
        g.strokePath (p, juce::PathStrokeType (thickness * l.mul,
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }
}

void glowEllipse (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c,
                  float thickness, float intensity)
{
    juce::Path p;
    p.addEllipse (r);
    glowPath (g, p, c, thickness, intensity);
}

void drawGlassPanel (juce::Graphics& g, juce::Rectangle<float> r,
                     juce::Colour accent, float cornerSize, bool active)
{
    juce::ColourGradient grad (colours::bgRaised.withAlpha (0.92f), r.getCentreX(), r.getY(),
                               colours::bgPanel.withAlpha (0.92f),  r.getCentreX(), r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, cornerSize);

    // A single bright hairline along the top edge sells the "pane of glass"
    // read far better than an even border does.
    g.setColour (accent.withAlpha (active ? 0.55f : 0.16f));
    g.drawRoundedRectangle (r.reduced (0.5f), cornerSize, 1.0f);

    juce::Path top;
    top.startNewSubPath (r.getX() + cornerSize, r.getY() + 0.75f);
    top.lineTo (r.getRight() - cornerSize, r.getY() + 0.75f);
    g.setColour (accent.withAlpha (active ? 0.35f : 0.10f));
    g.strokePath (top, juce::PathStrokeType (1.0f));
}

// ---------------------------------------------------------------------------

FuturisticLookAndFeel::FuturisticLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colours::bg);
    setColour (juce::Label::textColourId,                 colours::text);
    setColour (juce::Slider::textBoxTextColourId,         colours::text);
    setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId,   juce::Colours::transparentBlack);
    setColour (juce::ComboBox::backgroundColourId,        colours::bgRaised);
    setColour (juce::ComboBox::textColourId,              colours::text);
    setColour (juce::ComboBox::outlineColourId,           colours::outline);
    setColour (juce::ComboBox::arrowColourId,             colours::cyan);
    setColour (juce::PopupMenu::backgroundColourId,       colours::bgPanel);
    setColour (juce::PopupMenu::textColourId,             colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::cyan.withAlpha (0.18f));
    setColour (juce::PopupMenu::highlightedTextColourId,  juce::Colours::white);
    setColour (juce::TextButton::buttonColourId,          colours::bgRaised);
    setColour (juce::TextButton::textColourOffId,         colours::textDim);
    setColour (juce::TextButton::textColourOnId,          juce::Colours::white);
    setColour (juce::TooltipWindow::backgroundColourId,   colours::bgPanel);
    setColour (juce::TooltipWindow::textColourId,         colours::text);
    setColour (juce::TooltipWindow::outlineColourId,      colours::cyan.withAlpha (0.3f));
    setColour (juce::CaretComponent::caretColourId,       colours::cyan);
}

juce::Font FuturisticLookAndFeel::monoFont (float height, bool bold)
{
    juce::Font f (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height,
                                     bold ? juce::Font::bold : juce::Font::plain));
    return f;
}

juce::Font FuturisticLookAndFeel::uiFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font FuturisticLookAndFeel::getLabelFont (juce::Label& l)
{
    return uiFont ((float) juce::jmin (16, l.getHeight() - 2));
}

juce::Font FuturisticLookAndFeel::getComboBoxFont (juce::ComboBox& c)
{
    return uiFont ((float) juce::jmin (15, c.getHeight() - 8));
}

juce::Font FuturisticLookAndFeel::getPopupMenuFont()
{
    return uiFont (14.0f);
}

juce::Font FuturisticLookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return uiFont ((float) juce::jmin (14, h - 8), true);
}

// ---------------------------------------------------------------------------

void FuturisticLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                              float sliderPos, float startAngle, float endAngle,
                                              juce::Slider& slider)
{
    // Square the area before drawing. Using the raw bounds paints the seat as
    // a flattened oval whenever the slot is wider than it is tall, which is
    // most of them once captions and readouts have taken their share.
    const auto slot = juce::Rectangle<int> (x, y, w, h).toFloat();
    const float side = juce::jmin (slot.getWidth(), slot.getHeight());
    const auto bounds = juce::Rectangle<float> (side, side).withCentre (slot.getCentre()).reduced (3.0f);

    const float radius = bounds.getWidth() * 0.5f;
    const auto centre = bounds.getCentre();
    const float track = juce::jmax (2.5f, radius * 0.13f);
    const float arcR = radius - track * 0.9f;

    const auto accent = slider.findColour (juce::Slider::thumbColourId, true);
    const juce::Colour accentColour = accent.isTransparent() ? colours::cyan : accent;

    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    // Bipolar controls read from the centre outwards; unipolar from the start.
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float originAngle = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

    // seat
    g.setColour (colours::bgSunken);
    g.fillEllipse (bounds.reduced (track * 0.4f));

    // unfilled track
    juce::Path back;
    back.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (colours::outline);
    g.strokePath (back, juce::PathStrokeType (track, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

    // value arc
    if (std::abs (angle - originAngle) > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                             juce::jmin (originAngle, angle),
                             juce::jmax (originAngle, angle), true);
        glowPath (g, value, accentColour, track, slider.isEnabled() ? 1.0f : 0.35f);
    }

    // Tick marks around the dial. A stepped control gets one mark per step,
    // drawn as a dot, so its detents are visible before it is touched; a
    // continuous one gets an even scale of eleven.
    const double interval = slider.getInterval();
    const int steps = interval > 0.0
                    ? (int) std::lround ((slider.getMaximum() - slider.getMinimum()) / interval) + 1
                    : 0;
    const bool stepped = steps >= 2 && steps <= 13;
    const int ticks = stepped ? steps : 11;

    for (int i = 0; i < ticks; ++i)
    {
        const float t = (float) i / (float) (ticks - 1);
        const float a = startAngle + t * (endAngle - startAngle);
        const float inner = arcR + track * 0.75f;
        const float outer = inner + radius * 0.09f;

        if (stepped)
        {
            const float ringR = (inner + outer) * 0.5f;
            const bool current = std::abs (a - angle) < 0.01f;
            const float dotR = current ? 2.6f : 1.7f;

            g.setColour (current     ? accentColour.brighter (0.4f)
                         : a < angle ? accentColour.withAlpha (0.6f)
                                     : colours::textFaint.withAlpha (0.6f));
            g.fillEllipse (juce::Rectangle<float> (dotR * 2.0f, dotR * 2.0f)
                               .withCentre ({ centre.x + std::sin (a) * ringR,
                                              centre.y - std::cos (a) * ringR }));
            continue;
        }

        const juce::Point<float> p1 (centre.x + std::sin (a) * inner, centre.y - std::cos (a) * inner);
        const juce::Point<float> p2 (centre.x + std::sin (a) * outer, centre.y - std::cos (a) * outer);

        g.setColour (a <= angle ? accentColour.withAlpha (0.5f) : colours::textFaint.withAlpha (0.45f));
        g.drawLine ({ p1, p2 }, 1.0f);
    }

    // pointer
    const float pointerLen = arcR - track * 0.9f;
    juce::Path pointer;
    pointer.startNewSubPath (centre.x + std::sin (angle) * (pointerLen * 0.32f),
                             centre.y - std::cos (angle) * (pointerLen * 0.32f));
    pointer.lineTo (centre.x + std::sin (angle) * pointerLen,
                    centre.y - std::cos (angle) * pointerLen);
    glowPath (g, pointer, accentColour.brighter (0.4f), juce::jmax (1.6f, radius * 0.055f));

    // hub
    const float hubR = radius * 0.16f;
    g.setColour (colours::bgRaised);
    g.fillEllipse (juce::Rectangle<float> (hubR * 2.0f, hubR * 2.0f).withCentre (centre));
    g.setColour (accentColour.withAlpha (0.45f));
    g.drawEllipse (juce::Rectangle<float> (hubR * 2.0f, hubR * 2.0f).withCentre (centre), 1.0f);
}

void FuturisticLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h,
                                              float sliderPos, float, float,
                                              juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const auto accent = slider.findColour (juce::Slider::thumbColourId, true);
    const juce::Colour c = accent.isTransparent() ? colours::cyan : accent;
    const bool vertical = (style == juce::Slider::LinearVertical || style == juce::Slider::LinearBarVertical);

    auto area = juce::Rectangle<int> (x, y, w, h).toFloat();

    if (vertical)
    {
        const float cx = area.getCentreX();
        g.setColour (colours::bgSunken);
        g.fillRoundedRectangle (juce::Rectangle<float> (5.0f, area.getHeight()).withCentre (area.getCentre()), 2.5f);

        juce::Path fill;
        fill.startNewSubPath (cx, area.getBottom());
        fill.lineTo (cx, sliderPos);
        glowPath (g, fill, c, 4.0f);

        juce::Path thumb;
        thumb.addRoundedRectangle (juce::Rectangle<float> (area.getWidth(), 6.0f)
                                       .withCentre ({ cx, sliderPos }), 3.0f);
        glowPath (g, thumb, c.brighter (0.5f), 1.4f);
    }
    else
    {
        const float cy = area.getCentreY();
        g.setColour (colours::bgSunken);
        g.fillRoundedRectangle (juce::Rectangle<float> (area.getWidth(), 5.0f).withCentre (area.getCentre()), 2.5f);

        juce::Path fill;
        fill.startNewSubPath (area.getX(), cy);
        fill.lineTo (sliderPos, cy);
        glowPath (g, fill, c, 4.0f);

        juce::Path thumb;
        thumb.addRoundedRectangle (juce::Rectangle<float> (6.0f, area.getHeight())
                                       .withCentre ({ sliderPos, cy }), 3.0f);
        glowPath (g, thumb, c.brighter (0.5f), 1.4f);
    }
}

void FuturisticLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                                              bool highlighted, bool)
{
    const auto area = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();
    const bool enabled = b.isEnabled();

    const auto accent = b.findColour (juce::TextButton::buttonOnColourId, true);
    const juce::Colour c = accent.isTransparent() ? colours::cyan : accent;

    // A pill switch rather than a checkbox: state is legible at a glance from
    // across a room, which is what matters while tracking.
    const float switchW = juce::jmin (34.0f, area.getWidth() * 0.42f);
    const float switchH = juce::jmin (18.0f, area.getHeight() - 2.0f);
    const auto sw = juce::Rectangle<float> (switchW, switchH)
                        .withY (area.getCentreY() - switchH * 0.5f)
                        .withX (area.getX());

    if (on && enabled)
    {
        juce::Path glow;
        glow.addRoundedRectangle (sw, switchH * 0.5f);
        g.setColour (c.withAlpha (0.14f));
        g.strokePath (glow, juce::PathStrokeType (4.0f));
    }

    g.setColour (! enabled ? colours::bgPanel
                 : on      ? c.withAlpha (0.34f)
                           : colours::bgSunken);
    g.fillRoundedRectangle (sw, switchH * 0.5f);

    // The off state needs a visible track outline too, or a disabled control
    // and an off control look identical.
    g.setColour (! enabled ? colours::outline.withAlpha (0.4f)
                 : on      ? c
                           : (highlighted ? c.withAlpha (0.6f) : colours::outline));
    g.drawRoundedRectangle (sw.reduced (0.5f), switchH * 0.5f, on ? 1.5f : 1.2f);

    const float knobR = switchH * 0.5f - 3.0f;
    const float knobX = on ? sw.getRight() - knobR - 3.0f : sw.getX() + knobR + 3.0f;
    const auto knob = juce::Rectangle<float> (knobR * 2.0f, knobR * 2.0f)
                          .withCentre ({ knobX, sw.getCentreY() });

    g.setColour (! enabled ? colours::textFaint.withAlpha (0.5f)
                 : on      ? c.brighter (0.7f)
                           : colours::textDim);
    g.fillEllipse (knob);

    const auto textArea = area.withTrimmedLeft (switchW + 9.0f);
    if (textArea.getWidth() > 4.0f)
    {
        g.setColour (! enabled ? colours::textFaint
                     : on      ? colours::text
                               : colours::text.withAlpha (0.72f));
        g.setFont (uiFont (juce::jmin (13.0f, area.getHeight() * 0.62f), on));
        g.drawText (b.getButtonText(), textArea, juce::Justification::centredLeft, true);
    }
}

void FuturisticLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                                  const juce::Colour&, bool highlighted, bool down)
{
    const auto area = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    const bool enabled = b.isEnabled();
    const float corner = 5.0f;

    const auto accent = b.findColour (juce::TextButton::buttonOnColourId, true);
    const juce::Colour c = accent.isTransparent() ? colours::cyan : accent;

    if (! enabled)
    {
        // Unmistakably inert: flat fill, no border light, no highlight.
        g.setColour (colours::bgPanel);
        g.fillRoundedRectangle (area, corner);
        g.setColour (colours::outline.withAlpha (0.35f));
        g.drawRoundedRectangle (area, corner, 1.0f);
        return;
    }

    // A vertical gradient is what makes a control read as a raised, pressable
    // object rather than a flat patch of panel. Pressed inverts it.
    if (on)
    {
        g.setGradientFill (juce::ColourGradient (c.withAlpha (0.38f), area.getCentreX(), area.getY(),
                                                 c.withAlpha (0.20f), area.getCentreX(), area.getBottom(), false));
    }
    else if (down)
    {
        g.setGradientFill (juce::ColourGradient (colours::bgSunken, area.getCentreX(), area.getY(),
                                                 colours::bgRaised.darker (0.2f), area.getCentreX(), area.getBottom(), false));
    }
    else
    {
        const float lift = highlighted ? 0.20f : 0.0f;
        g.setGradientFill (juce::ColourGradient (colours::bgRaised.brighter (0.14f + lift), area.getCentreX(), area.getY(),
                                                 colours::bgRaised.darker (0.16f), area.getCentreX(), area.getBottom(), false));
    }

    g.fillRoundedRectangle (area, corner);

    g.setColour (on ? c
                    : (highlighted ? c.withAlpha (0.80f) : colours::outline));
    g.drawRoundedRectangle (area, corner, on ? 1.6f : 1.2f);

    if (! down)
    {
        // A single bright pixel along the top edge reads as a lit bevel.
        g.setColour (juce::Colours::white.withAlpha (on ? 0.14f : 0.07f));
        g.drawLine (area.getX() + corner, area.getY() + 1.2f,
                    area.getRight() - corner, area.getY() + 1.2f, 1.0f);
    }

    if (on)
    {
        juce::Path p;
        p.addRoundedRectangle (area, corner);
        g.setColour (c.withAlpha (0.16f));
        g.strokePath (p, juce::PathStrokeType (4.0f));
    }
}

void FuturisticLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b,
                                            bool highlighted, bool down)
{
    const bool on = b.getToggleState();
    const bool enabled = b.isEnabled();

    g.setFont (getTextButtonFont (b, b.getHeight()));

    // Label contrast is the whole point of a button. Dim grey on dark grey
    // reads as decoration; these steps keep every state clearly legible.
    g.setColour (! enabled  ? colours::textFaint
                 : on       ? juce::Colours::white
                 : highlighted ? juce::Colours::white
                              : colours::text.withAlpha (0.86f));

    auto area = b.getLocalBounds();
    if (down)
        area.translate (0, 1);

    g.drawText (b.getButtonText(), area, juce::Justification::centred, true);
}

void FuturisticLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool down,
                                          int, int, int, int, juce::ComboBox& box)
{
    auto area = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
    const bool hover = box.isMouseOver() || box.hasKeyboardFocus (false) || down;
    const bool enabled = box.isEnabled();

    g.setGradientFill (juce::ColourGradient (colours::bgRaised.brighter (hover ? 0.16f : 0.06f),
                                             area.getCentreX(), area.getY(),
                                             colours::bgSunken, area.getCentreX(), area.getBottom(), false));
    g.fillRoundedRectangle (area, 5.0f);

    g.setColour (! enabled ? colours::outline.withAlpha (0.4f)
                           : (hover ? colours::cyan.withAlpha (0.8f) : colours::outline));
    g.drawRoundedRectangle (area, 5.0f, hover ? 1.5f : 1.2f);

    // A separated chevron well makes it obvious the field opens a list rather
    // than accepting typing.
    const float wellW = 22.0f;
    auto well = area.removeFromRight (wellW);

    g.setColour (colours::bgSunken.withAlpha (0.65f));
    g.fillRoundedRectangle (well.reduced (1.5f), 3.0f);

    juce::Path arrow;
    const float cx = well.getCentreX();
    const float cy = well.getCentreY();
    arrow.startNewSubPath (cx - 4.0f, cy - 2.0f);
    arrow.lineTo (cx, cy + 3.0f);
    arrow.lineTo (cx + 4.0f, cy - 2.0f);

    g.setColour (! enabled ? colours::textFaint : (hover ? colours::cyan : colours::cyan.withAlpha (0.8f)));
    g.strokePath (arrow, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
}

void FuturisticLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (10, 1, box.getWidth() - 34, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
}

void FuturisticLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    if (! label.isBeingEdited())
    {
        g.setColour (label.findColour (juce::Label::textColourId));
        g.setFont (getLabelFont (label));
        g.drawFittedText (label.getText(), label.getLocalBounds(),
                          label.getJustificationType(), 2, 0.9f);
    }
}

void FuturisticLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    const auto area = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h);
    g.setColour (colours::bgPanel);
    g.fillRoundedRectangle (area, 5.0f);
    g.setColour (colours::cyan.withAlpha (0.28f));
    g.drawRoundedRectangle (area.reduced (0.5f), 5.0f, 1.0f);
}

void FuturisticLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                               bool isSeparator, bool isActive, bool isHighlighted,
                                               bool isTicked, bool hasSubMenu, const juce::String& text,
                                               const juce::String& shortcutText, const juce::Drawable*,
                                               const juce::Colour*)
{
    if (isSeparator)
    {
        g.setColour (colours::outline);
        g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }

    auto r = area.reduced (3, 1);

    if (isHighlighted && isActive)
    {
        g.setColour (colours::cyan.withAlpha (0.16f));
        g.fillRoundedRectangle (r.toFloat(), 3.0f);

        g.setColour (colours::cyan);
        g.fillRect (r.removeFromLeft (2).toFloat());
    }

    g.setColour (isActive ? (isHighlighted ? juce::Colours::white : colours::text)
                          : colours::textFaint);
    g.setFont (getPopupMenuFont());

    auto textArea = r.reduced (10, 0);

    if (isTicked)
    {
        g.setColour (colours::cyan);
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f)
                           .withCentre ({ (float) textArea.getX() - 1.0f, (float) textArea.getCentreY() }));
        g.setColour (isHighlighted ? juce::Colours::white : colours::text);
    }

    g.drawFittedText (text, textArea.withTrimmedLeft (8), juce::Justification::centredLeft, 1);

    if (shortcutText.isNotEmpty())
    {
        g.setColour (colours::textFaint);
        g.drawFittedText (shortcutText, textArea, juce::Justification::centredRight, 1);
    }

    if (hasSubMenu)
    {
        juce::Path p;
        const float cx = (float) textArea.getRight() - 6.0f;
        const float cy = (float) textArea.getCentreY();
        p.startNewSubPath (cx - 3.0f, cy - 4.0f);
        p.lineTo (cx + 1.0f, cy);
        p.lineTo (cx - 3.0f, cy + 4.0f);
        g.setColour (colours::textDim);
        g.strokePath (p, juce::PathStrokeType (1.4f));
    }
}

} // namespace helix::ui
