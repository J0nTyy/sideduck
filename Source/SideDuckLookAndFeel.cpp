#include "SideDuckLookAndFeel.h"
#include "BinaryData.h"

juce::Font SideDuckLookAndFeel::uiFont (float height, bool bold)
{
    static const juce::Typeface::Ptr medium = juce::Typeface::createSystemTypefaceFor (
        BinaryData::SpaceGroteskMedium_ttf, (size_t) BinaryData::SpaceGroteskMedium_ttfSize);
    static const juce::Typeface::Ptr heavy = juce::Typeface::createSystemTypefaceFor (
        BinaryData::SpaceGroteskBold_ttf, (size_t) BinaryData::SpaceGroteskBold_ttfSize);

    const auto& typeface = bold ? heavy : medium;

    if (typeface != nullptr)
        return juce::Font (juce::FontOptions().withTypeface (typeface).withHeight (height));

    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font SideDuckLookAndFeel::legendFont (float height)
{
    return uiFont (height, true).withExtraKerningFactor (0.12f);
}

void SideDuckLookAndFeel::setTheme (const Theme& newTheme)
{
    theme = newTheme;

    // fallback face for any stray default-constructed fonts
    setDefaultSansSerifTypefaceName ("Segoe UI");

    setColour (juce::Slider::textBoxTextColourId, theme.dimText);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, theme.accentSoft);
    setColour (juce::Slider::rotarySliderFillColourId, theme.accent);

    setColour (juce::Label::textColourId, theme.dimText);

    setColour (juce::TextButton::buttonColourId, theme.control);
    setColour (juce::TextButton::buttonOnColourId, theme.accentSoft);
    setColour (juce::TextButton::textColourOffId, theme.text);
    setColour (juce::TextButton::textColourOnId, theme.light ? theme.text : juce::Colours::white);

    setColour (juce::ComboBox::backgroundColourId, theme.control);
    setColour (juce::ComboBox::textColourId, theme.text);
    setColour (juce::ComboBox::outlineColourId, theme.panelBorder);
    setColour (juce::ComboBox::arrowColourId, theme.accent);

    setColour (juce::PopupMenu::backgroundColourId, theme.panel);
    setColour (juce::PopupMenu::textColourId, theme.text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme.accentSoft);
    setColour (juce::PopupMenu::highlightedTextColourId, theme.light ? theme.text : juce::Colours::white);

    setColour (juce::ToggleButton::textColourId, theme.text);
    setColour (juce::ToggleButton::tickColourId, theme.accent);

    setColour (juce::TextEditor::textColourId, theme.text);
    setColour (juce::TextEditor::highlightColourId, theme.accentSoft);
    setColour (juce::TextEditor::focusedOutlineColourId, theme.accent);
    setColour (juce::CaretComponent::caretColourId, theme.accent);
}

void SideDuckLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                            float sliderPos, float rotaryStartAngle,
                                            float rotaryEndAngle, juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (5.0f);

    const float radius    = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto  centre    = bounds.getCentre();
    const float lineW     = juce::jmax (2.5f, radius * 0.11f);
    const float arcRadius = radius - lineW / 2.0f - 2.0f;
    const bool  hover     = slider.isMouseOverOrDragging() && slider.isEnabled();
    const float angle     = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // legend tick marks around the dial, like a printed faceplate scale
    g.setColour (theme.dimText.withAlpha (0.45f));

    for (int i = 0; i <= 10; ++i)
    {
        const bool  major = i % 5 == 0;
        const float a     = rotaryStartAngle + (rotaryEndAngle - rotaryStartAngle) * (float) i / 10.0f;
        const float inner = radius - 1.0f;
        const float outer = radius + (major ? 3.0f : 1.5f);

        g.drawLine (centre.x + inner * std::sin (a), centre.y - inner * std::cos (a),
                    centre.x + outer * std::sin (a), centre.y - outer * std::cos (a),
                    major ? 1.2f : 0.8f);
    }

    // track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (theme.light ? theme.control.darker (0.12f) : theme.control);
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // value arc, with a soft glow on hover
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         rotaryStartAngle, angle, true);

    if (hover)
    {
        g.setColour (theme.accent.withAlpha (0.25f));
        g.strokePath (value, juce::PathStrokeType (lineW + 3.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    g.setColour (hover ? theme.accent.brighter (0.15f) : theme.accent);
    g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // knob body: drop shadow, machined cap, rim
    const float knobRadius = arcRadius - lineW * 1.35f;
    const auto  knobArea   = juce::Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (centre);

    g.setColour (juce::Colours::black.withAlpha (theme.light ? 0.20f : 0.50f));
    g.fillEllipse (knobArea.translated (0.0f, knobRadius * 0.12f).expanded (0.5f));

    juce::ColourGradient bodyGradient (theme.knobBody.brighter (hover ? 0.18f : 0.09f),
                                       centre.x, centre.y - knobRadius,
                                       theme.knobBody.darker (theme.light ? 0.06f : 0.30f),
                                       centre.x, centre.y + knobRadius, false);
    g.setGradientFill (bodyGradient);
    g.fillEllipse (knobArea);

    // knurled grip band that rotates with the knob
    g.setColour (juce::Colours::black.withAlpha (theme.light ? 0.10f : 0.30f));

    for (int i = 0; i < 24; ++i)
    {
        const float a     = angle + juce::MathConstants<float>::twoPi * (float) i / 24.0f;
        const float inner = knobRadius * 0.80f;
        const float outer = knobRadius * 0.96f;

        g.drawLine (centre.x + inner * std::sin (a), centre.y - inner * std::cos (a),
                    centre.x + outer * std::sin (a), centre.y - outer * std::cos (a), 1.0f);
    }

    // machined groove separating the grip band from the centre cap
    g.setColour (juce::Colours::black.withAlpha (theme.light ? 0.12f : 0.35f));
    g.drawEllipse (juce::Rectangle<float> (knobRadius * 1.56f, knobRadius * 1.56f).withCentre (centre), 1.0f);

    // centre cap with a slight dome
    juce::ColourGradient capGradient (theme.knobBody.brighter (theme.light ? 0.10f : 0.16f),
                                      centre.x, centre.y - knobRadius * 0.7f,
                                      theme.knobBody.darker (theme.light ? 0.02f : 0.18f),
                                      centre.x, centre.y + knobRadius * 0.7f, false);
    g.setGradientFill (capGradient);
    g.fillEllipse (juce::Rectangle<float> (knobRadius * 1.44f, knobRadius * 1.44f).withCentre (centre));

    // specular highlight, upper-left, like overhead studio lighting
    {
        auto highlight = knobArea.reduced (knobRadius * 0.18f)
                             .translated (-knobRadius * 0.12f, -knobRadius * 0.22f);
        juce::ColourGradient spec (juce::Colours::white.withAlpha (theme.light ? 0.35f : 0.13f),
                                   highlight.getX(), highlight.getY(),
                                   juce::Colours::white.withAlpha (0.0f),
                                   highlight.getCentreX(), highlight.getBottom(), false);
        g.setGradientFill (spec);
        g.fillEllipse (highlight);
    }

    g.setColour (theme.knobOutline);
    g.drawEllipse (knobArea, 1.2f);

    // painted pointer line, accent tipped
    const float pointerLen = knobRadius * 0.52f;
    const float pointerW   = juce::jmax (2.2f, knobRadius * 0.11f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-pointerW / 2.0f, -knobRadius + 2.5f,
                                 pointerW, pointerLen, pointerW / 2.0f);

    const auto transform = juce::AffineTransform::rotation (angle).translated (centre);

    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillPath (pointer, juce::AffineTransform::translation (0.0f, 1.0f).followedBy (transform));

    g.setColour (hover ? theme.accent.brighter (0.2f) : theme.accent);
    g.fillPath (pointer, transform);
}

void SideDuckLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                const juce::Colour& backgroundColour,
                                                bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const float corner = 6.0f;

    auto base = backgroundColour;

    if (down)
        base = base.darker (0.18f);
    else if (highlighted)
        base = base.brighter (theme.light ? 0.03f : 0.14f);

    // soft drop shadow
    if (! down)
    {
        g.setColour (juce::Colours::black.withAlpha (theme.light ? 0.10f : 0.30f));
        g.fillRoundedRectangle (bounds.translated (0.0f, 1.2f), corner);
    }

    juce::ColourGradient gradient (base.brighter (theme.light ? 0.02f : 0.08f),
                                   bounds.getX(), bounds.getY(),
                                   base.darker (theme.light ? 0.03f : 0.10f),
                                   bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill (gradient);
    g.fillRoundedRectangle (bounds, corner);

    // thin top-edge highlight for a machined, physical feel
    if (! down)
    {
        g.setColour (juce::Colours::white.withAlpha (theme.light ? 0.45f : 0.07f));
        g.fillRoundedRectangle (bounds.withHeight (1.5f).reduced (2.5f, 0.0f), 1.0f);
    }

    g.setColour (theme.panelBorder.withAlpha (highlighted ? 1.0f : 0.8f));
    g.drawRoundedRectangle (bounds, corner, 1.0f);
}

void SideDuckLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                            bool highlighted, bool)
{
    // caption on the left in the shared faceplate legend style, switch on the
    // right so it sits between the caption and whatever control follows
    auto bounds = button.getLocalBounds().toFloat();

    const float trackHeight = 16.0f;
    const float trackWidth  = 34.0f;
    const bool  on          = button.getToggleState();

    g.setColour ((on ? theme.text : theme.dimText)
                     .withAlpha (button.isEnabled() ? 1.0f : 0.5f));
    g.setFont (legendFont (11.0f));
    g.drawText (button.getButtonText().toUpperCase(),
                bounds.withTrimmedRight (trackWidth + 7.0f).toNearestInt(),
                juce::Justification::centredLeft);

    auto track = juce::Rectangle<float> (bounds.getRight() - trackWidth,
                                         (bounds.getHeight() - trackHeight) / 2.0f,
                                         trackWidth, trackHeight);

    auto trackColour = on ? theme.accent : (theme.light ? theme.control.darker (0.15f) : theme.control);

    if (highlighted)
        trackColour = trackColour.brighter (0.12f);

    g.setColour (trackColour);
    g.fillRoundedRectangle (track, trackHeight / 2.0f);

    // thumb
    const float thumbSize = trackHeight - 4.0f;
    const float thumbX    = on ? track.getRight() - thumbSize - 2.0f : track.getX() + 2.0f;

    g.setColour (juce::Colours::black.withAlpha (0.2f));
    g.fillEllipse (thumbX, track.getY() + 2.8f, thumbSize, thumbSize);
    g.setColour (juce::Colours::white.withAlpha (on ? 1.0f : 0.85f));
    g.fillEllipse (thumbX, track.getY() + 2.0f, thumbSize, thumbSize);
}

void SideDuckLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                        int, int, int, int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    const float corner = 6.0f;

    auto bg = box.findColour (juce::ComboBox::backgroundColourId);

    if (box.isEnabled() && box.isMouseOver (true))
        bg = bg.brighter (theme.light ? 0.03f : 0.10f);

    g.setColour (juce::Colours::black.withAlpha (theme.light ? 0.08f : 0.25f));
    g.fillRoundedRectangle (bounds.translated (0.0f, 1.0f), corner);

    juce::ColourGradient gradient (bg.brighter (theme.light ? 0.02f : 0.06f),
                                   bounds.getX(), bounds.getY(),
                                   bg.darker (theme.light ? 0.02f : 0.08f),
                                   bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill (gradient);
    g.fillRoundedRectangle (bounds, corner);

    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds, corner, 1.0f);

    // chevron
    const float cx = (float) width - 13.0f;
    const float cy = (float) height / 2.0f;

    juce::Path chevron;
    chevron.startNewSubPath (cx - 4.0f, cy - 2.0f);
    chevron.lineTo (cx, cy + 2.5f);
    chevron.lineTo (cx + 4.0f, cy - 2.0f);

    g.setColour (box.findColour (juce::ComboBox::arrowColourId)
                    .withAlpha (box.isEnabled() ? 0.9f : 0.4f));
    g.strokePath (chevron, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
}

juce::Font SideDuckLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return uiFont (13.5f);
}

juce::Font SideDuckLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return uiFont (13.5f);
}

juce::Font SideDuckLookAndFeel::getPopupMenuFont()
{
    return uiFont (14.0f);
}

juce::Font SideDuckLookAndFeel::getLabelFont (juce::Label& label)
{
    // slider value boxes get the embedded face at a size that fits the box
    if (dynamic_cast<juce::Slider*> (label.getParentComponent()) != nullptr)
        return uiFont (12.5f);

    return label.getFont();
}

juce::TextLayout SideDuckLookAndFeel::layoutTooltipText (const juce::String& text, juce::Colour colour)
{
    juce::AttributedString attributed;
    attributed.setJustification (juce::Justification::centredLeft);
    attributed.append (text, uiFont (13.5f), colour);

    juce::TextLayout layout;
    layout.createLayout (attributed, 250.0f);
    return layout;
}

juce::Rectangle<int> SideDuckLookAndFeel::getTooltipBounds (const juce::String& tipText,
                                                            juce::Point<int> screenPos,
                                                            juce::Rectangle<int> parentArea)
{
    const auto layout = layoutTooltipText (tipText, theme.text);

    const int w = (int) std::ceil (layout.getWidth()) + 18;
    const int h = (int) std::ceil (layout.getHeight()) + 14;

    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 20,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 8)  : screenPos.y + 22,
                                 w, h)
        .constrainedWithin (parentArea);
}

void SideDuckLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);

    g.setColour (theme.panel);
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (theme.accent.withAlpha (0.4f));
    g.drawRoundedRectangle (bounds, 6.0f, 1.0f);

    layoutTooltipText (text, theme.text)
        .draw (g, bounds.reduced (9.0f, 7.0f));
}
