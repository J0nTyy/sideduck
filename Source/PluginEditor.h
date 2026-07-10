#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "Theme.h"
#include "SideDuckLookAndFeel.h"

//==============================================================================
// Rotary slider that resets to the parameter default on right-click.
class KnobSlider : public juce::Slider
{
public:
    std::function<void()> onRightClick;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown())
        {
            if (onRightClick)
                onRightClick();

            return;
        }

        juce::Slider::mouseDown (e);
    }
};

//==============================================================================
// Draws one cycle of the current LFO volume curve plus a moving playhead, or
// a live gain-reduction trace when external Duck mode is active.
//
// When the Custom wave is active it is a breakpoint editor: click empty space
// to add a point, drag points to move them (snap optional, Shift bypasses),
// drag the diamond handle in the middle of a segment to bend it, double-click
// or right-click a point to remove it, right-click empty space for the curve
// menu (copy/paste/reverse/flip/normalize/reset).
class LfoVisualizer : public juce::Component,
                      private juce::Timer
{
public:
    explicit LfoVisualizer (SideDuckAudioProcessor& p) : processor (p)
    {
        startTimerHz (30);
    }

    void paint (juce::Graphics&) override;

    void mouseDown        (const juce::MouseEvent&) override;
    void mouseDrag        (const juce::MouseEvent&) override;
    void mouseUp          (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove        (const juce::MouseEvent&) override;
    void mouseExit        (const juce::MouseEvent&) override;

    Theme theme;
    int   snapDivision = 0;   // 0 = off, otherwise steps per cycle (4/8/16/32)

private:
    enum class Drag { none, point, tension };

    void timerCallback() override { repaint(); }

    juce::Rectangle<float> plotArea() const;
    float currentPhaseOffset() const;
    float snapDisplayX (float displayX, bool bypassSnap) const;
    juce::Point<float> pointToScreen (juce::Point<float> rawPoint, float phaseOffset) const;
    juce::Point<float> screenToPoint (juce::Point<float> screenPos, float phaseOffset, bool bypassSnap) const;

    int  hitTestPoint (juce::Point<float> screenPos) const;
    int  hitTestSegment (juce::Point<float> screenPos) const;
    bool segmentHandlePosition (int segmentIndex, float phaseOffset, juce::Point<float>& outScreen) const;

    void removePoint (int index);
    void switchToCustomWave();
    bool customWaveActive() const;
    void showContextMenu();
    void applyCurveMenu (int result);

    void paintGrid (juce::Graphics&, juce::Rectangle<float> plot) const;
    void paintDuckView (juce::Graphics&, juce::Rectangle<float> plot) const;
    void paintPlayhead (juce::Graphics&, juce::Rectangle<float> plot) const;

    SideDuckAudioProcessor& processor;

    Drag dragMode       = Drag::none;
    int  draggedIndex   = -1;   // point index or segment index, per dragMode
    int  hoveredIndex   = -1;
    int  hoveredSegment = -1;
    int  selectedIndex  = -1;

    // snapshot taken at mouseDown; pushed as one undo step on first movement
    std::vector<CurvePoint> dragSnapshot;
    bool dragChanged = false;
};

//==============================================================================
// All controls live here at a fixed base size; the editor scales this whole
// component with an AffineTransform for the UI-scale option.
class ContentComponent : public juce::Component,
                         private juce::Timer
{
public:
    static constexpr int baseWidth  = 860;
    static constexpr int baseHeight = 624;

    ContentComponent (SideDuckAudioProcessor&,
                      std::function<void (int)> onScaleSelected,
                      std::function<void (int)> onThemeSelected);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;

    void updateScaleDisplay (int percent); // shows the nearest step, silently
    void setThemeSelection (int index);
    void setTheme (const Theme&);

private:
    void timerCallback() override;
    void setupKnob (KnobSlider&, juce::Label&, const juce::String& text, const juce::String& paramID);
    void attachKnobDefaults (KnobSlider&, const juce::String& paramID);
    void applyThemeToControls();
    void applyTooltips();
    void doUndo();
    void doRedo();

    void refreshPresetList (const juce::String& selectName = {});
    void loadSelectedPreset();
    void launchSaveChooser();
    void launchImportChooser();
    void launchExportChooser();

    SideDuckAudioProcessor& processor;
    std::function<void (int)> onScaleSelected, onThemeSelected;

    Theme theme;
    LfoVisualizer visualizer;

    juce::ComboBox waveBox, rateSyncBox, scaleBox, presetBox, scModeBox, themeBox, snapBox;
    juce::ComboBox lfo2TargetBox, lfo2WaveBox, lfo2DivBox;
    juce::TextButton powerButton { "ON" };
    juce::TextButton savePresetButton { "Save" }, importPresetButton { "Import" }, exportPresetButton { "Export" };
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" };
    juce::TextButton slotAButton { "A" }, slotBButton { "B" }, copySlotButton { "Copy" };
    juce::ToggleButton syncButton { "Sync" }, midiButton { "MIDI" }, lfo2SyncButton { "Sync" };
    KnobSlider rateHzSlider, depthSlider, phaseSlider, smoothSlider,
               punchSlider, stereoSlider, outputSlider, lookaheadSlider, senseSlider,
               lfo2RateSlider, lfo2DepthSlider;
    juce::Label rateLabel, depthLabel, phaseLabel, smoothLabel, punchLabel, stereoLabel, outputLabel,
                lookaheadLabel, senseLabel, waveLabel, scLabel, snapLabel, hintLabel, presetLabel,
                lfo2Label, lfo2RateLabel, lfo2DepthLabel;

    juce::Rectangle<int> titleBounds, deckBounds, knobRowBounds, lfo2RowBounds;

    juce::Array<juce::File> presetFiles;
    std::unique_ptr<juce::FileChooser> fileChooser;

    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<ComboBoxAttachment> waveAttachment, rateSyncAttachment, scModeAttachment,
                                        lfo2TargetAttachment, lfo2WaveAttachment, lfo2DivAttachment;
    std::unique_ptr<ButtonAttachment>   powerAttachment, syncAttachment, midiTrigAttachment, lfo2SyncAttachment;
    std::unique_ptr<SliderAttachment>   rateHzAttachment, depthAttachment, phaseAttachment, smoothAttachment,
                                        punchAttachment, stereoAttachment, outputAttachment, lookaheadAttachment,
                                        senseAttachment, lfo2RateAttachment, lfo2DepthAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ContentComponent)
};

//==============================================================================
// The editor is freely resizable: drag the host window's edges/corners or the
// bottom-right grip (aspect ratio is kept). The Scale dropdown jumps to preset
// sizes; both routes end up in resized(), which scales the fixed-size content.
class SideDuckAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit SideDuckAudioProcessorEditor (SideDuckAudioProcessor&);
    ~SideDuckAudioProcessorEditor() override;

    void resized() override;
    void applyScale (int percent);
    void applyTheme (int themeIndex);

private:
    SideDuckAudioProcessor& processor;

    SideDuckLookAndFeel lookAndFeel;
    ContentComponent content;
    juce::TooltipWindow tooltipWindow { this, 1000 }; // 1 s hover delay

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SideDuckAudioProcessorEditor)
};
