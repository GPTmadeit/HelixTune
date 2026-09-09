#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace helix::ui
{

/** The plugin's colour language.

    Hue carries meaning here rather than decoration: cyan is the input / the
    performance as it was sung, magenta is the target, lime is a note the user
    placed, amber is a warning state. Anything the user can edit glows; anything
    read-only does not.
*/
namespace colours
{
    inline const juce::Colour bg        { 0xff05070c };
    inline const juce::Colour bgPanel   { 0xff0a0f1a };
    inline const juce::Colour bgRaised  { 0xff111a29 };
    inline const juce::Colour bgSunken  { 0xff03050a };
    inline const juce::Colour grid      { 0xff17202f };
    inline const juce::Colour gridBold  { 0xff223047 };
    inline const juce::Colour outline   { 0xff1e2b3f };

    inline const juce::Colour text      { 0xffdce9f5 };
    inline const juce::Colour textDim   { 0xff657c93 };
    inline const juce::Colour textFaint { 0xff3d4d61 };

    inline const juce::Colour cyan      { 0xff00e5ff };
    inline const juce::Colour magenta   { 0xffff2d95 };
    inline const juce::Colour lime      { 0xff7cff4f };
    inline const juce::Colour amber     { 0xffffb020 };
    inline const juce::Colour violet    { 0xff9d6bff };
    inline const juce::Colour danger    { 0xffff4d5e };
}

/** Multi-pass stroke that fakes a bloom without a blur buffer. Cheap enough to
    run on every repaint of every knob. */
void glowPath (juce::Graphics& g, const juce::Path& p, juce::Colour c,
               float thickness, float intensity = 1.0f);

void glowEllipse (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c,
                  float thickness, float intensity = 1.0f);

/** Panel with a hairline border, inner darkening and a faint top highlight. */
void drawGlassPanel (juce::Graphics& g, juce::Rectangle<float> r,
                     juce::Colour accent, float cornerSize = 6.0f, bool active = false);

class FuturisticLookAndFeel : public juce::LookAndFeel_V4
{
public:
    FuturisticLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool highlighted, bool down) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;

    void drawComboBox (juce::Graphics&, int w, int h, bool down,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawLabel (juce::Graphics&, juce::Label&) override;

    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;

    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutText, const juce::Drawable* icon,
                            const juce::Colour* textColour) override;

    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    /** Tabular figures for anything that updates continuously, so digits do
        not jitter horizontally as the value changes. */
    static juce::Font monoFont (float height, bool bold = false);
    static juce::Font uiFont (float height, bool bold = false);
};

} // namespace helix::ui
