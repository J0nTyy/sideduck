#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

// Custom rendering for knobs, buttons, combos and toggles. All colours come
// from the active Theme; call setTheme() and repaint to restyle live.
class SideDuckLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SideDuckLookAndFeel() { setTheme (Theme::byIndex (0)); }

    void setTheme (const Theme& newTheme);
    const Theme& getTheme() const noexcept { return theme; }

    // The plugin's typeface (Space Grotesk, embedded) so every surface uses
    // the same face regardless of what the host machine has installed.
    // Falls back to the platform default if the binary data is missing.
    static juce::Font uiFont (float height, bool bold = false);

    // Small-caps faceplate legend style shared by knob labels, selector
    // captions and toggle text.
    static juce::Font legendFont (float height);

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getLabelFont (juce::Label&) override;

    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

private:
    static juce::TextLayout layoutTooltipText (const juce::String& text, juce::Colour colour);

    Theme theme;
};
