/*  Renders the repository artwork.

    Generating the banner and icon from the plugin's own palette and drawing
    primitives keeps the branding and the product in sync - there is no
    separate asset to go stale when the colour language changes.

    Usage: HelixTuneBanner <output directory>
*/

#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/FuturisticLookAndFeel.h"

using namespace helix::ui;

namespace
{

void paintBackdrop (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0d1826), area.getCentreX(), area.getY(),
                                             juce::Colour (0xff04060b), area.getCentreX(), area.getBottom(), false));
    g.fillAll();

    // A faint grid, the same one the plugin sits on.
    g.setColour (colours::grid.withAlpha (0.35f));
    for (float x = 0.0f; x < area.getWidth(); x += 32.0f)
        g.fillRect (x, 0.0f, 1.0f, area.getHeight());
    for (float y = 0.0f; y < area.getHeight(); y += 32.0f)
        g.fillRect (0.0f, y, area.getWidth(), 1.0f);

    // Corner bloom, so the artwork is not a flat rectangle of grid.
    juce::ColourGradient bloom (colours::cyan.withAlpha (0.16f), area.getX() + area.getWidth() * 0.18f,
                                area.getBottom(),
                                juce::Colours::transparentBlack, area.getX() + area.getWidth() * 0.18f,
                                area.getY(), true);
    g.setGradientFill (bloom);
    g.fillAll();

    juce::ColourGradient bloom2 (colours::magenta.withAlpha (0.10f), area.getRight(), area.getY(),
                                 juce::Colours::transparentBlack, area.getRight() - area.getWidth() * 0.45f,
                                 area.getBottom(), true);
    g.setGradientFill (bloom2);
    g.fillAll();
}

/** The signature image of the product: a sung contour and the corrected one. */
void paintPitchTrace (juce::Graphics& g, juce::Rectangle<float> area, float thickness, bool withGrid)
{
    if (withGrid)
    {
        for (int i = 0; i <= 6; ++i)
        {
            const float y = area.getY() + area.getHeight() * (float) i / 6.0f;
            g.setColour ((i % 2 == 0 ? colours::cyan.withAlpha (0.10f) : colours::grid.withAlpha (0.5f)));
            g.fillRect (area.getX(), y, area.getWidth(), 1.0f);
        }
    }

    juce::Path sung, corrected;

    const int steps = 340;
    const float midY = area.getCentreY();
    const float amp = area.getHeight() * 0.46f;

    for (int i = 0; i <= steps; ++i)
    {
        const float t = (float) i / (float) steps;
        const float x = area.getX() + t * area.getWidth();

        // A melodic line with drift, a scoop and vibrato on the held notes.
        const float phrase = std::sin (t * juce::MathConstants<float>::twoPi * 0.85f)
                           + 0.45f * std::sin (t * juce::MathConstants<float>::twoPi * 1.9f + 1.1f);

        const float vibrato = 0.10f * std::sin (t * juce::MathConstants<float>::twoPi * 22.0f)
                            * juce::jlimit (0.0f, 1.0f, std::sin (t * juce::MathConstants<float>::pi * 3.0f));

        const float drift = 0.09f * std::sin (t * juce::MathConstants<float>::twoPi * 0.33f + 2.0f);

        const float sungValue = phrase + vibrato + drift;

        // The corrected line is the same phrase quantised to a scale.
        const float quantised = std::round (phrase * 3.0f) / 3.0f;

        const float ys = midY - sungValue * amp * 0.62f;
        const float yc = midY - quantised * amp * 0.62f;

        if (i == 0)
        {
            sung.startNewSubPath (x, ys);
            corrected.startNewSubPath (x, yc);
        }
        else
        {
            sung.lineTo (x, ys);
            corrected.lineTo (x, yc);
        }
    }

    glowPath (g, sung, colours::cyan, thickness, 1.0f);
    glowPath (g, corrected, colours::magenta, thickness * 1.15f, 1.0f);
}

void writeBanner (const juce::File& out, int width, int height)
{
    // Explicitly software-backed. The default Image type is native, which on
    // Windows means Direct2D - and with no device available in a console tool
    // every draw silently no-ops and the file comes out fully transparent.
    juce::Image image (juce::SoftwareImageType().create (juce::Image::ARGB, width, height, true));
    juce::Graphics g (image);

    const auto area = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    paintBackdrop (g, area);

    // Trace across the lower two thirds, behind the wordmark.
    paintPitchTrace (g, area.withTrimmedTop (height * 0.42f).reduced (width * 0.04f, height * 0.10f),
                     2.4f, true);

    // --- wordmark ---------------------------------------------------------
    const float titleSize = height * 0.235f;
    auto title = area.withTrimmedTop (height * 0.13f).withHeight (titleSize * 1.25f)
                     .withTrimmedLeft (width * 0.055f);

    const auto bold = FuturisticLookAndFeel::uiFont (titleSize, true);
    const auto light = FuturisticLookAndFeel::uiFont (titleSize, false);

    const float helixW = juce::GlyphArrangement::getStringWidth (bold, "HELIX");

    g.setFont (bold);
    g.setColour (colours::cyan);
    g.drawText ("HELIX", title, juce::Justification::centredLeft, false);

    g.setFont (light);
    g.setColour (juce::Colours::white);
    g.drawText ("TUNE", title.withTrimmedLeft (helixW + titleSize * 0.16f),
                juce::Justification::centredLeft, false);

    // --- tagline ----------------------------------------------------------
    auto tagline = area.withTrimmedTop (height * 0.135f + titleSize * 1.2f)
                       .withHeight (height * 0.10f)
                       .withTrimmedLeft (width * 0.058f);

    g.setColour (colours::textDim);
    g.setFont (FuturisticLookAndFeel::uiFont (height * 0.052f, false));
    // Deliberately ASCII. A UTF-8 bullet written as hex escapes gets decoded
    // as Latin-1 somewhere in the toolchain and renders as mojibake.
    g.drawText ("REAL-TIME PITCH CORRECTION   |   GRAPHICAL NOTE EDITING   |   "
                "AUTO-KEY   |   4-VOICE HARMONY",
                tagline, juce::Justification::centredLeft, false);

    // A hairline under the header, like the plugin's own.
    g.setColour (colours::cyan.withAlpha (0.35f));
    g.fillRect (0.0f, area.getBottom() - 3.0f, area.getWidth(), 3.0f);

    if (auto stream = std::unique_ptr<juce::FileOutputStream> (out.createOutputStream()))
    {
        stream->setPosition (0);
        stream->truncate();
        juce::PNGImageFormat png;
        png.writeImageToStream (image, *stream);
    }
}

juce::Image renderIcon (int size)
{
    juce::Image image (juce::SoftwareImageType().create (juce::Image::ARGB, size, size, true));
    juce::Graphics g (image);

    const auto area = juce::Rectangle<float> (0.0f, 0.0f, (float) size, (float) size);

    juce::Path rounded;
    rounded.addRoundedRectangle (area.reduced (size * 0.03f), size * 0.20f);
    g.reduceClipRegion (rounded);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff12233a), area.getCentreX(), area.getY(),
                                             juce::Colour (0xff05080f), area.getCentreX(), area.getBottom(), false));
    g.fillAll();

    // A single helix turn: two glowing strands crossing, which is the mark.
    juce::Path a, b;
    const float margin = size * 0.22f;
    const int steps = 120;

    for (int i = 0; i <= steps; ++i)
    {
        const float t = (float) i / (float) steps;
        const float y = margin + t * (size - margin * 2.0f);
        const float phase = t * juce::MathConstants<float>::twoPi * 1.15f;
        const float w = (size * 0.5f - margin) * 0.95f;

        const float xa = size * 0.5f + std::sin (phase) * w;
        const float xb = size * 0.5f + std::sin (phase + juce::MathConstants<float>::pi) * w;

        if (i == 0) { a.startNewSubPath (xa, y); b.startNewSubPath (xb, y); }
        else        { a.lineTo (xa, y);          b.lineTo (xb, y); }
    }

    glowPath (g, a, colours::cyan, size * 0.045f, 1.0f);
    glowPath (g, b, colours::magenta, size * 0.045f, 1.0f);

    g.setColour (colours::cyan.withAlpha (0.35f));
    g.drawRoundedRectangle (area.reduced (size * 0.035f), size * 0.19f, size * 0.012f);

    return image;
}

/** Writes a Vista-style .ico whose single entry is a PNG payload. */
void writeIcon (const juce::File& out, const juce::Image& image)
{
    juce::MemoryOutputStream png;
    juce::PNGImageFormat format;
    format.writeImageToStream (image, png);

    juce::MemoryOutputStream ico;
    auto write16 = [&ico] (uint16_t v) { ico.writeShort ((short) v); };
    auto write32 = [&ico] (uint32_t v) { ico.writeInt ((int) v); };

    write16 (0);        // reserved
    write16 (1);        // type: icon
    write16 (1);        // one image

    // 0 in the width/height byte means 256.
    ico.writeByte (image.getWidth() >= 256 ? 0 : (char) image.getWidth());
    ico.writeByte (image.getHeight() >= 256 ? 0 : (char) image.getHeight());
    ico.writeByte (0);  // palette size
    ico.writeByte (0);  // reserved
    write16 (1);        // colour planes
    write16 (32);       // bits per pixel
    write32 ((uint32_t) png.getDataSize());
    write32 (22);       // offset: 6-byte header + 16-byte entry

    ico.write (png.getData(), png.getDataSize());

    out.replaceWithData (ico.getData(), ico.getDataSize());
}

} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::File dir = argc > 1 ? juce::File (juce::String (argv[1]))
                                    : juce::File::getCurrentWorkingDirectory();

    dir.createDirectory();

    writeBanner (dir.getChildFile ("banner.png"), 1400, 420);
    writeBanner (dir.getChildFile ("social-preview.png"), 1280, 640);

    const auto icon = renderIcon (512);

    if (auto stream = std::unique_ptr<juce::FileOutputStream> (
            dir.getChildFile ("icon.png").createOutputStream()))
    {
        stream->setPosition (0);
        stream->truncate();
        juce::PNGImageFormat png;
        png.writeImageToStream (icon, *stream);
    }

    writeIcon (dir.getChildFile ("icon.ico"), renderIcon (256));

    std::printf ("Wrote artwork to %s\n", dir.getFullPathName().toRawUTF8());
    return 0;
}
