#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include "LFO.h"
#include "CustomCurve.h"

// One breakpoint of the custom curve. `curve` is the tension of the segment
// leaving this point (towards the next point, wrapping from the last point
// back to the first): -1..1, 0 = straight line.
struct CurvePoint
{
    float x     = 0.0f;
    float y     = 0.0f;
    float curve = 0.0f;
};

class SideDuckAudioProcessor : public juce::AudioProcessor
{
public:
    enum class SidechainMode { off = 0, duck, trigger };

    SideDuckAudioProcessor();
    ~SideDuckAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }   // MIDI note retrigger
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Read by the editor's visualizer (audio thread writes, UI thread reads).
    std::atomic<float> currentPhase { 0.0f };
    std::atomic<float> currentGain  { 1.0f };

    // Effective trigger/sidechain mode after the "no signal routed" fallback:
    // 0 = LFO, 1 = external duck, 2 = external trigger, 3 = MIDI retrigger.
    // The UI mirrors this so it always represents the real DSP state.
    std::atomic<int> activeScMode { 0 };

    // Rolling gain-reduction history for the external-duck display.
    static constexpr int gainHistorySize = 256;
    std::array<std::atomic<float>, gainHistorySize> gainHistory;
    std::atomic<int> gainHistoryPos { 0 };

    // Breakpoint model for the custom curve. Only the UI thread and state
    // load/save touch this (under the lock); the audio thread reads the
    // baked customCurve table instead.
    struct CurveModel
    {
        juce::CriticalSection lock;
        std::vector<CurvePoint> points { { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } };
    };

    CurveModel curveModel;
    CustomCurve customCurve;

    // Resamples the breakpoint model into the lock-free table the audio
    // thread reads. Call after any change to curveModel.
    void bakeCustomCurve();

    // Curve edit history (UI thread only). Call pushCurveUndo() BEFORE
    // mutating curveModel, or pushCurveUndoState() with a snapshot taken
    // earlier (used so drags record one undo step on first movement).
    void pushCurveUndo();
    void pushCurveUndoState (std::vector<CurvePoint> state);
    bool undoCurve();
    bool redoCurve();
    bool canUndoCurve() const noexcept { return ! curveUndoStack.empty(); }
    bool canRedoCurve() const noexcept { return ! curveRedoStack.empty(); }

    juce::File getPresetDirectory() const;
    bool savePresetToFile (const juce::File&);
    bool loadPresetFromFile (const juce::File&);

    // A/B compare slots (UI thread only, session-scoped). Switching stores
    // the current sound in the slot being left; a never-used slot inherits
    // the current sound, so A/B starts identical.
    void switchToSlot (int slot);
    void copyCurrentToOtherSlot();
    int  getActiveSlot() const noexcept { return activeSlot; }

    // Restores every parameter to its default and resets the custom curve
    // (the curve reset is undoable). Used by the "Init" preset entry.
    void resetToDefaults();

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static double beatsForDivision (int divisionIndex);
    static juce::StringArray divisionNames();

private:
    std::unique_ptr<juce::XmlElement> createStateXml();
    void applyStateXml (const juce::XmlElement&, bool preserveUiScale);
    void ensureFactoryPresets();

    double sampleRateHz  = 44100.0;
    double freePhase     = 0.0;    // free-running phase in [0, 1)
    float  smoothedGainL = 1.0f;
    float  smoothedGainR = 1.0f;

    // sidechain detection
    float scEnvelope   = 0.0f;
    float scTriggerEnv = 0.0f;
    bool  scArmed      = true;
    bool  scShotActive = false;   // trigger modes: shape is mid-playthrough

    // LFO 2 (filter cutoff / pan modulation)
    double freePhase2   = 0.0;
    float  lfo2Smoothed = 1.0f;
    float  filterS1[2]  = { 0.0f, 0.0f };  // TPT SVF integrator states
    float  filterS2[2]  = { 0.0f, 0.0f };

    // gain history decimation
    float historyMin     = 1.0f;
    int   historyCounter = 0;
    int   historyStride  = 256;

    // lookahead delay line: channels 0-1 audio, 2 display phase, 3 display
    // gain — the visual state is delayed with the audio so the animation
    // matches what is heard
    juce::AudioBuffer<float> delayRing;
    int delayWritePos   = 0;
    int maxDelaySamples = 0;
    int reportedLatency = -1;

    // curve undo/redo (UI thread only)
    std::vector<std::vector<CurvePoint>> curveUndoStack, curveRedoStack;

    // A/B compare (UI thread only)
    int activeSlot = 0;
    std::unique_ptr<juce::XmlElement> slotStates[2];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SideDuckAudioProcessor)
};
