#include "NeonKnob.h"

namespace helix::ui
{

static constexpr int captionHeight = 14;
static constexpr int valueHeight   = 15;

NeonKnob::NeonKnob (const juce::String& c, juce::Colour a)
    : caption (c), accent (a)
{
    slider.setColour (juce::Slider::thumbColourId, accent);
    slider.setDoubleClickReturnValue (true, 0.0);   // replaced by the attachment's default
    slider.setVelocityBasedMode (false);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f, true);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
}

NeonKnob::~NeonKnob() = default;

void NeonKnob::attach (juce::AudioProcessorValueTreeState& state, const juce::String& id)
{
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id, slider);

    // Double-click restores the parameter's own default rather than zero.
    if (auto* p = state.getParameter (id))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
}

void NeonKnob::setAccent (juce::Colour c)
{
    accent = c;
    slider.setColour (juce::Slider::thumbColourId, accent);
    repaint();
}

void NeonKnob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (captionHeight);
    r.removeFromBottom (valueHeight);
    slider.setBounds (r);
}

void NeonKnob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();

    auto capArea = r.removeFromTop (captionHeight);
    g.setColour (colours::textDim);
    g.setFont (FuturisticLookAndFeel::uiFont (10.5f, true));
    g.drawText (caption.toUpperCase(), capArea, juce::Justification::centred, false);

    auto valArea = r.removeFromBottom (valueHeight);
    g.setColour (accent.withAlpha (slider.isEnabled() ? 0.95f : 0.35f));
    g.setFont (FuturisticLookAndFeel::monoFont (11.5f, true));
    g.drawText (slider.getTextFromValue (slider.getValue()), valArea,
                juce::Justification::centred, false);
}

// ---------------------------------------------------------------------------

NeonSelector::NeonSelector (const juce::String& c, juce::Colour a)
    : caption (c), accent (a)
{
    box.setColour (juce::ComboBox::arrowColourId, accent);
    addAndMakeVisible (box);
}

NeonSelector::~NeonSelector() = default;

void NeonSelector::attach (juce::AudioProcessorValueTreeState& state, const juce::String& id)
{
    // ComboBoxAttachment only syncs the selected index - it does not fill the
    // box. Populate from the parameter's own choice list first, or the control
    // renders as an empty dropdown that can never be opened.
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (id)))
    {
        box.clear (juce::dontSendNotification);
        box.addItemList (choice->choices, 1);
    }

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, id, box);
}

void NeonSelector::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (captionHeight);
    box.setBounds (r.reduced (0, 1));
}

void NeonSelector::paint (juce::Graphics& g)
{
    g.setColour (colours::textDim);
    g.setFont (FuturisticLookAndFeel::uiFont (10.5f, true));
    g.drawText (caption.toUpperCase(), getLocalBounds().removeFromTop (captionHeight),
                juce::Justification::centredLeft, false);
}

// ---------------------------------------------------------------------------

NeonToggle::NeonToggle (const juce::String& text, juce::Colour accent)
{
    button.setButtonText (text);
    button.setColour (juce::TextButton::buttonOnColourId, accent);
    addAndMakeVisible (button);
}

NeonToggle::~NeonToggle() = default;

void NeonToggle::attach (juce::AudioProcessorValueTreeState& state, const juce::String& id)
{
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id, button);
}

void NeonToggle::resized()
{
    button.setBounds (getLocalBounds());
}

} // namespace helix::ui
