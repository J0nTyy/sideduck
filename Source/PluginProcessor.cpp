#include "PluginProcessor.h"
#include "PluginEditor.h"

SideDuckAudioProcessor::SideDuckAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    for (auto& g : gainHistory)
        g.store (1.0f);

    bakeCustomCurve();
    ensureFactoryPresets();
}

juce::StringArray SideDuckAudioProcessor::divisionNames()
{
    return { "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32" };
}

double SideDuckAudioProcessor::beatsForDivision (int divisionIndex)
{
    static constexpr double beats[] = { 32.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125 };

    if (divisionIndex >= 0 && divisionIndex < (int) std::size (beats))
        return beats[divisionIndex];

    return 4.0;
}

juce::AudioProcessorValueTreeState::ParameterLayout SideDuckAudioProcessor::createParameterLayout()
{
    using namespace juce;

    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "wave", 1 }, "Waveform",
        StringArray { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Custom", "Exp Pump", "Pulse", "Steps" },
        (int) LFO::Shape::sawUp));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "power", 1 }, "Power", true));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "sync", 1 }, "Tempo Sync", true));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "rateSync", 1 }, "Sync Rate", divisionNames(), 5)); // default 1/4

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "rateHz", 1 }, "Rate",
        NormalisableRange<float> (0.02f, 50.0f, 0.0f, 0.3f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return String (v, 2) + " Hz"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "depth", 1 }, "Depth",
        NormalisableRange<float> (0.0f, 1.0f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "phase", 1 }, "Phase",
        NormalisableRange<float> (0.0f, 360.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return String (roundToInt (v)) + " deg"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "smooth", 1 }, "Smooth",
        NormalisableRange<float> (0.0f, 200.0f, 0.0f, 0.5f), 10.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return String (v, 1) + " ms"; })));

    NormalisableRange<float> punchRange (0.25f, 4.0f);
    punchRange.setSkewForCentre (1.0f);

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "punch", 1 }, "Punch",
        punchRange, 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return String (v, 2) + "x"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "stereo", 1 }, "Stereo",
        NormalisableRange<float> (0.0f, 180.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return String (roundToInt (v)) + " deg"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "output", 1 }, "Output",
        NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return String (v, 1) + " dB"; })));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "scMode", 1 }, "Sidechain",
        StringArray { "Off", "Duck", "Trigger" }, 0));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "lookahead", 1 }, "Lookahead",
        NormalisableRange<float> (0.0f, 10.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return String (v, 1) + " ms"; })));

    // detection threshold for sidechain Trigger mode (was fixed at -20 dB)
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "scSens", 1 }, "Trigger Sense",
        NormalisableRange<float> (-40.0f, -6.0f, 0.5f), -20.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return String (v, 1) + " dB"; })));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "midiTrig", 1 }, "MIDI Retrigger", false));

    // second LFO: modulates a low-pass filter cutoff or auto-pans
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "lfo2Target", 1 }, "LFO2 Target",
        StringArray { "Off", "Filter", "Pan" }, 0));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "lfo2Wave", 1 }, "LFO2 Waveform",
        StringArray { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Custom", "Exp Pump", "Pulse", "Steps" },
        (int) LFO::Shape::sine));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "lfo2Sync", 1 }, "LFO2 Tempo Sync", true));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "lfo2Div", 1 }, "LFO2 Sync Rate", divisionNames(), 5)); // default 1/4

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "lfo2RateHz", 1 }, "LFO2 Rate",
        NormalisableRange<float> (0.02f, 50.0f, 0.0f, 0.3f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return String (v, 2) + " Hz"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "lfo2Depth", 1 }, "LFO2 Depth",
        NormalisableRange<float> (0.0f, 1.0f), 0.5f,
        AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; })));

    return layout;
}

//==============================================================================
// custom curve breakpoints

// Bends the segment leaving `a` with a power curve: curve > 0 holds near a.y
// longer, curve < 0 moves toward the target early. curve == 0 is linear.
static float segmentInterp (const CurvePoint& a, float targetY, float t)
{
    if (std::abs (a.curve) > 0.001f)
        t = std::pow (t, std::pow (2.0f, a.curve * 3.0f));

    return a.y + (targetY - a.y) * t;
}

static float curveValueAt (const std::vector<CurvePoint>& pts, float p)
{
    const int n = (int) pts.size();

    if (n == 0)
        return 1.0f;

    if (n == 1)
        return pts[0].y;

    // outside the first/last breakpoint: the curve wraps across the cycle
    // boundary from the last point back to the first
    if (p < pts.front().x || p >= pts.back().x)
    {
        const auto& a = pts.back();
        const auto& b = pts.front();

        const float span = 1.0f - a.x + b.x;
        const float dist = p >= a.x ? p - a.x : p + 1.0f - a.x;
        const float t    = span > 0.0001f ? juce::jlimit (0.0f, 1.0f, dist / span) : 0.0f;

        return segmentInterp (a, b.y, t);
    }

    for (int i = 0; i < n - 1; ++i)
    {
        if (p >= pts[(size_t) i].x && p < pts[(size_t) i + 1].x)
        {
            const float span = pts[(size_t) i + 1].x - pts[(size_t) i].x;
            const float t    = span > 0.0001f ? (p - pts[(size_t) i].x) / span : 0.0f;

            return segmentInterp (pts[(size_t) i], pts[(size_t) i + 1].y, t);
        }
    }

    return pts.back().y;
}

void SideDuckAudioProcessor::bakeCustomCurve()
{
    const juce::ScopedLock sl (curveModel.lock);

    for (int i = 0; i < CustomCurve::resolution; ++i)
        customCurve.points[(size_t) i].store (
            curveValueAt (curveModel.points, (float) i / (float) CustomCurve::resolution));
}

void SideDuckAudioProcessor::pushCurveUndo()
{
    std::vector<CurvePoint> snapshot;

    {
        const juce::ScopedLock sl (curveModel.lock);
        snapshot = curveModel.points;
    }

    pushCurveUndoState (std::move (snapshot));
}

void SideDuckAudioProcessor::pushCurveUndoState (std::vector<CurvePoint> state)
{
    curveUndoStack.push_back (std::move (state));

    if (curveUndoStack.size() > 64)
        curveUndoStack.erase (curveUndoStack.begin());

    curveRedoStack.clear();
}

bool SideDuckAudioProcessor::undoCurve()
{
    if (curveUndoStack.empty())
        return false;

    {
        const juce::ScopedLock sl (curveModel.lock);
        curveRedoStack.push_back (curveModel.points);
        curveModel.points = std::move (curveUndoStack.back());
    }

    curveUndoStack.pop_back();
    bakeCustomCurve();
    return true;
}

bool SideDuckAudioProcessor::redoCurve()
{
    if (curveRedoStack.empty())
        return false;

    {
        const juce::ScopedLock sl (curveModel.lock);
        curveUndoStack.push_back (curveModel.points);
        curveModel.points = std::move (curveRedoStack.back());
    }

    curveRedoStack.pop_back();
    bakeCustomCurve();
    return true;
}

//==============================================================================
void SideDuckAudioProcessor::prepareToPlay (double sampleRate, int)
{
    sampleRateHz  = sampleRate;
    smoothedGainL = 1.0f;
    smoothedGainR = 1.0f;
    scEnvelope    = 0.0f;
    scTriggerEnv  = 0.0f;
    scArmed       = true;
    scShotActive  = false;

    freePhase2   = 0.0;
    lfo2Smoothed = 1.0f;
    filterS1[0] = filterS1[1] = 0.0f;
    filterS2[0] = filterS2[1] = 0.0f;

    historyMin     = 1.0f;
    historyCounter = 0;
    historyStride  = juce::jmax (32, (int) (sampleRate / 180.0)); // ~1.4 s of history

    maxDelaySamples = (int) std::ceil (0.010 * sampleRate) + 8;
    delayRing.setSize (4, maxDelaySamples + 1);
    delayRing.clear();

    // gain channel must start at unity, not silence
    juce::FloatVectorOperations::fill (delayRing.getWritePointer (3), 1.0f, delayRing.getNumSamples());

    delayWritePos   = 0;
    reportedLatency = -1;
}

bool SideDuckAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();

    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    if (mainOut != layouts.getMainInputChannelSet())
        return false;

    const auto& sidechain = layouts.getChannelSet (true, 1);

    return sidechain.isDisabled()
        || sidechain == juce::AudioChannelSet::mono()
        || sidechain == juce::AudioChannelSet::stereo();
}

void SideDuckAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    auto mainBuffer = getBusBuffer (buffer, false, 0);
    auto scBuffer   = getBusBuffer (buffer, true, 1);

    const int numSamples  = mainBuffer.getNumSamples();
    const int numChannels = mainBuffer.getNumChannels();
    const int scChannels  = scBuffer.getNumChannels();

    const auto  shape        = static_cast<LFO::Shape> ((int) apvts.getRawParameterValue ("wave")->load());
    const bool  power        = apvts.getRawParameterValue ("power")->load() > 0.5f;
    const bool  sync         = apvts.getRawParameterValue ("sync")->load() > 0.5f;
    const int   divIndex     = (int) apvts.getRawParameterValue ("rateSync")->load();
    const float rateHz       = apvts.getRawParameterValue ("rateHz")->load();
    const float depth        = apvts.getRawParameterValue ("depth")->load();
    const float phaseOffset  = apvts.getRawParameterValue ("phase")->load() / 360.0f;
    const float smoothMs     = apvts.getRawParameterValue ("smooth")->load();
    const float punch        = apvts.getRawParameterValue ("punch")->load();
    const float stereoOffset = apvts.getRawParameterValue ("stereo")->load() / 360.0f;
    const float outputDb     = apvts.getRawParameterValue ("output")->load();
    const auto  scMode       = static_cast<SidechainMode> ((int) apvts.getRawParameterValue ("scMode")->load());
    const float lookaheadMs  = apvts.getRawParameterValue ("lookahead")->load();
    const float scSensDb     = apvts.getRawParameterValue ("scSens")->load();
    const bool  midiTrig     = apvts.getRawParameterValue ("midiTrig")->load() > 0.5f;

    const int   lfo2Target   = (int) apvts.getRawParameterValue ("lfo2Target")->load(); // 0 off, 1 filter, 2 pan
    const auto  lfo2Shape    = static_cast<LFO::Shape> ((int) apvts.getRawParameterValue ("lfo2Wave")->load());
    const bool  lfo2Sync     = apvts.getRawParameterValue ("lfo2Sync")->load() > 0.5f;
    const int   lfo2DivIndex = (int) apvts.getRawParameterValue ("lfo2Div")->load();
    const float lfo2RateHz   = apvts.getRawParameterValue ("lfo2RateHz")->load();
    const float lfo2Depth    = apvts.getRawParameterValue ("lfo2Depth")->load();

    double bpm           = 120.0;
    bool   hostIsPlaying = false;
    bool   haveTransport = false;
    double hostPpq       = 0.0;
    bool   havePpq       = false;

    if (auto* head = getPlayHead())
    {
        if (auto pos = head->getPosition())
        {
            haveTransport = true;

            if (auto b = pos->getBpm())
                bpm = *b;

            hostIsPlaying = pos->getIsPlaying();

            if (auto ppq = pos->getPpqPosition())
            {
                hostPpq = *ppq;
                havePpq = true;
            }
        }
    }

    const double beatsPerCycle     = beatsForDivision (divIndex);
    const double cyclesPerSecond   = sync ? (bpm / 60.0) / beatsPerCycle : (double) rateHz;
    const double phaseIncPerSample = cyclesPerSecond / sampleRateHz;

    // Effective modes (fall back to plain LFO when nothing is routed into the
    // sidechain input). MIDI retrigger shares the one-shot machinery with
    // sidechain Trigger mode: either source restarts the shape.
    const bool duckModeEff    = scMode == SidechainMode::duck && scChannels > 0;
    const bool triggerModeEff = scMode == SidechainMode::trigger && scChannels > 0;
    const bool oneShotMode    = triggerModeEff || midiTrig;

    // In LFO mode the phase only advances while the transport rolls
    // (standalone free-runs). Duck mode freezes the LFO entirely — the
    // envelope drives the gain. One-shot modes advance only while a pass of
    // the shape is playing.
    const bool lfoAdvancing = ! duckModeEff && ! oneShotMode && (hostIsPlaying || ! haveTransport);
    const bool useHostPhase = sync && hostIsPlaying && havePpq && ! oneShotMode && ! duckModeEff;

    double phase = useHostPhase ? std::fmod (hostPpq / beatsPerCycle, 1.0) : freePhase;

    if (phase < 0.0)
        phase += 1.0;

    const float smoothCoeff = smoothMs <= 0.01f
                                  ? 0.0f
                                  : (float) std::exp (-1.0 / (smoothMs * 0.001 * sampleRateHz));

    // sidechain envelope coefficients: 1 ms attack, 120 ms release
    const float envAttack   = 1.0f - (float) std::exp (-1.0 / (0.001 * sampleRateHz));
    const float envRelease  = (float) std::exp (-1.0 / (0.120 * sampleRateHz));
    const float trigRelease = (float) std::exp (-1.0 / (0.030 * sampleRateHz));

    // Trigger detection fires at the Sense threshold and re-arms 6 dB below
    // it, so one transient produces exactly one shot.
    const float trigThreshold  = juce::Decibels::decibelsToGain (scSensDb);
    const float rearmThreshold = trigThreshold * 0.5f;

    const float outGain    = juce::Decibels::decibelsToGain (outputDb);
    const bool  applyPunch = std::abs (punch - 1.0f) > 0.01f;
    const bool  stereoized = numChannels > 1 && stereoOffset > 0.0001f;

    // LFO 2 (filter / pan). Free-runs on the same transport rules as LFO 1
    // but is never frozen by the sidechain modes. When idle it relaxes to its
    // neutral value: filter fully open, pan centred.
    const double lfo2Beats        = beatsForDivision (lfo2DivIndex);
    const double lfo2CyclesPerSec = lfo2Sync ? (bpm / 60.0) / lfo2Beats : (double) lfo2RateHz;
    const double lfo2PhaseInc     = lfo2CyclesPerSec / sampleRateHz;
    const bool   lfo2Advancing    = hostIsPlaying || ! haveTransport;
    const bool   lfo2UseHostPhase = lfo2Sync && hostIsPlaying && havePpq;
    const bool   lfo2Filter       = lfo2Target == 1;
    const bool   lfo2Pan          = lfo2Target == 2 && numChannels > 1;
    const float  lfo2Neutral      = lfo2Target == 2 ? 0.5f : 1.0f;
    const float  lfo2Coeff        = (float) std::exp (-1.0 / (0.004 * sampleRateHz)); // ~4 ms anti-zipper

    double lfo2Phase = lfo2UseHostPhase ? std::fmod (hostPpq / lfo2Beats, 1.0) : freePhase2;

    if (lfo2Phase < 0.0)
        lfo2Phase += 1.0;

    if (lfo2Target == 0)
    {
        // inactive: park at a clean state so re-enabling starts at the top of
        // a cycle with the filter open and pan centred
        lfo2Phase    = 0.0;
        lfo2Smoothed = 1.0f;
        filterS1[0] = filterS1[1] = 0.0f;
        filterS2[0] = filterS2[1] = 0.0f;
    }

    // MIDI note retrigger: note-ons restart the shape at their exact sample
    // position within the block
    auto midiIt        = midiMessages.begin();
    const auto midiEnd = midiMessages.end();

    // lookahead: main signal is delayed, detection runs on the live input
    const int delaySamples = juce::jlimit (0, maxDelaySamples,
                                           (int) std::round (lookaheadMs * 0.001 * sampleRateHz));

    if (delaySamples != reportedLatency)
    {
        reportedLatency = delaySamples;
        setLatencySamples (delaySamples);
    }

    const int ringSize = delayRing.getNumSamples();

    auto* ring0     = delayRing.getWritePointer (0);
    auto* ring1     = delayRing.getWritePointer (1);
    auto* phaseRing = delayRing.getWritePointer (2);
    auto* gainRing  = delayRing.getWritePointer (3);

    float visPhase = currentPhase.load();
    float visGain  = currentGain.load();

    auto shapeValueAt = [&] (float ph)
    {
        float v = shape == LFO::Shape::custom ? customCurve.valueAt (ph)
                                              : LFO::value (shape, ph);

        if (applyPunch)
            v = std::pow (v, punch);

        return v;
    };

    for (int i = 0; i < numSamples; ++i)
    {
        // MIDI note retrigger: restart the shape from the top on note-on
        while (midiIt != midiEnd && (*midiIt).samplePosition <= i)
        {
            if (midiTrig && (*midiIt).getMessage().isNoteOn())
            {
                phase        = 0.0;
                scShotActive = true;
            }

            ++midiIt;
        }

        // sidechain detection
        if (scChannels > 0 && (duckModeEff || triggerModeEff))
        {
            float scAbs = 0.0f;

            for (int c = 0; c < scChannels; ++c)
                scAbs = juce::jmax (scAbs, std::abs (scBuffer.getReadPointer (c)[i]));

            if (duckModeEff)
            {
                scEnvelope = scAbs > scEnvelope
                                 ? scEnvelope + (scAbs - scEnvelope) * envAttack
                                 : scEnvelope * envRelease;
            }
            else
            {
                scTriggerEnv = juce::jmax (scAbs, scTriggerEnv * trigRelease);

                if (scArmed && scTriggerEnv > trigThreshold)
                {
                    // transient: restart the shape from the top
                    phase        = 0.0;
                    scShotActive = true;
                    scArmed      = false;
                }
                else if (scTriggerEnv < rearmThreshold)
                {
                    scArmed = true;
                }
            }
        }

        // Paused or powered off: relax to unity gain so audio auditioned
        // while stopped isn't stuck at a ducked level.
        float targetL = 1.0f;
        float targetR = 1.0f;

        if (power)
        {
            if (duckModeEff)
            {
                targetL = targetR = 1.0f - depth * juce::jmin (1.0f, scEnvelope * 4.0f);
            }
            else if (oneShotMode ? scShotActive : lfoAdvancing)
            {
                const auto lfoPhase = (float) std::fmod (phase + phaseOffset, 1.0);

                targetL = 1.0f - depth * (1.0f - shapeValueAt (lfoPhase));
                targetR = stereoized
                              ? 1.0f - depth * (1.0f - shapeValueAt (std::fmod (lfoPhase + stereoOffset, 1.0f)))
                              : targetL;
            }
        }

        smoothedGainL = targetL + (smoothedGainL - targetL) * smoothCoeff;
        smoothedGainR = targetR + (smoothedGainR - targetR) * smoothCoeff;

        const int readPos = (delayWritePos - delaySamples + ringSize) % ringSize;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data   = mainBuffer.getWritePointer (ch);
            auto* ring   = ch == 0 ? ring0 : ring1;
            float sample = data[i];

            const float delayed = ring[readPos];
            ring[delayWritePos] = sample;

            if (delaySamples > 0)
                sample = delayed;

            data[i] = sample * (ch == 1 ? smoothedGainR : smoothedGainL) * outGain;
        }

        // LFO 2: modulates a low-pass cutoff or auto-pans. Stays in the
        // signal path while relaxing to neutral so mode changes are
        // click-free (a fully open filter / centred pan is transparent).
        if (lfo2Filter || lfo2Pan)
        {
            const float v2 = (power && lfo2Advancing)
                                 ? (lfo2Shape == LFO::Shape::custom
                                        ? customCurve.valueAt ((float) lfo2Phase)
                                        : LFO::value (lfo2Shape, (float) lfo2Phase))
                                 : lfo2Neutral;

            lfo2Smoothed = v2 + (lfo2Smoothed - v2) * lfo2Coeff;

            if (lfo2Filter)
            {
                // 20 kHz sweeping down up to 7 octaves with the curve
                const float cutoff = juce::jmin (20000.0f * std::exp2 (-7.0f * lfo2Depth * (1.0f - lfo2Smoothed)),
                                                 0.45f * (float) sampleRateHz);

                // TPT state-variable low-pass (Zavalishin), k = light resonance
                const float g  = std::tan (juce::MathConstants<float>::pi * cutoff / (float) sampleRateHz);
                const float k  = 1.2f;
                const float a1 = 1.0f / (1.0f + g * (g + k));
                const float a2 = g * a1;
                const float a3 = g * a2;

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    auto* data = mainBuffer.getWritePointer (ch);

                    const float v3 = data[i] - filterS2[ch];
                    const float v1 = a1 * filterS1[ch] + a2 * v3;
                    const float lp = filterS2[ch] + a2 * filterS1[ch] + a3 * v3;

                    filterS1[ch] = 2.0f * v1 - filterS1[ch];
                    filterS2[ch] = 2.0f * lp - filterS2[ch];

                    data[i] = lp;
                }
            }
            else // pan, balance law: centre stays at unity, no overall boost
            {
                const float p = lfo2Depth * (2.0f * lfo2Smoothed - 1.0f);

                if (p > 0.0f)
                    mainBuffer.getWritePointer (0)[i] *= 1.0f - p;
                else
                    mainBuffer.getWritePointer (1)[i] *= 1.0f + p;
            }
        }

        if (lfo2Target != 0 && lfo2Advancing)
        {
            lfo2Phase += lfo2PhaseInc;

            if (lfo2Phase >= 1.0)
                lfo2Phase -= 1.0;
        }

        // Display state travels through the same delay line as the audio so
        // the playhead/gain animation stays in sync with what is heard.
        const float dispPhase = (float) std::fmod (
            ((oneShotMode && ! scShotActive) ? 0.9999 : phase) + phaseOffset, 1.0);

        phaseRing[delayWritePos] = dispPhase;
        gainRing[delayWritePos]  = smoothedGainL;

        if (delaySamples > 0)
        {
            visPhase = phaseRing[readPos];
            visGain  = gainRing[readPos];
        }
        else
        {
            visPhase = dispPhase;
            visGain  = smoothedGainL;
        }

        delayWritePos = (delayWritePos + 1) % ringSize;

        // gain-reduction history (decimated) for the duck display
        historyMin = juce::jmin (historyMin, visGain);

        if (++historyCounter >= historyStride)
        {
            historyCounter = 0;

            const int pos = gainHistoryPos.load();
            gainHistory[(size_t) pos].store (historyMin);
            gainHistoryPos.store ((pos + 1) % gainHistorySize);
            historyMin = 1.0f;
        }

        // phase advance
        if (oneShotMode)
        {
            if (scShotActive)
            {
                phase += phaseIncPerSample;

                if (phase >= 1.0)
                {
                    // one-shot complete: hold at the end until the next
                    // transient or note restarts the shape
                    phase        = 1.0;
                    scShotActive = false;
                }
            }
        }
        else if (lfoAdvancing)
        {
            phase += phaseIncPerSample;

            if (phase >= 1.0)
                phase -= 1.0;
        }
    }

    freePhase  = phase;
    freePhase2 = lfo2Phase;

    // Publish the (lookahead-compensated) DSP state for the UI. In the
    // one-shot modes the playhead parks at the end between shots.
    currentPhase.store (visPhase);
    currentGain.store (visGain);
    activeScMode.store (duckModeEff ? 1 : (triggerModeEff ? 2 : (midiTrig ? 3 : 0)));
}

//==============================================================================
// state + presets

std::unique_ptr<juce::XmlElement> SideDuckAudioProcessor::createStateXml()
{
    auto state = apvts.copyState();

    {
        const juce::ScopedLock sl (curveModel.lock);

        juce::StringArray values;
        for (const auto& pt : curveModel.points)
            values.add (juce::String (pt.x, 4) + ":" + juce::String (pt.y, 4) + ":" + juce::String (pt.curve, 3));

        state.setProperty ("curvePoints", values.joinIntoString (","), nullptr);
    }

    return state.createXml();
}

void SideDuckAudioProcessor::applyStateXml (const juce::XmlElement& xml, bool preserveUiScale)
{
    if (! xml.hasTagName (apvts.state.getType()))
        return;

    // UI scale is a workstation preference, not a sound setting — presets
    // shouldn't change it
    const auto currentScale = apvts.state.getProperty ("uiScale", 100);

    apvts.replaceState (juce::ValueTree::fromXml (xml));

    if (preserveUiScale)
        apvts.state.setProperty ("uiScale", currentScale, nullptr);

    const auto pointString = apvts.state.getProperty ("curvePoints").toString();

    if (pointString.isNotEmpty())
    {
        std::vector<CurvePoint> loaded;

        for (const auto& token : juce::StringArray::fromTokens (pointString, ",", ""))
        {
            auto parts = juce::StringArray::fromTokens (token, ":", "");

            if (parts.size() < 2)
                continue;

            CurvePoint pt;
            pt.x     = juce::jlimit (0.0f, 1.0f, parts[0].getFloatValue());
            pt.y     = juce::jlimit (0.0f, 1.0f, parts[1].getFloatValue());
            pt.curve = parts.size() > 2 ? juce::jlimit (-1.0f, 1.0f, parts[2].getFloatValue()) : 0.0f;
            loaded.push_back (pt);
        }

        if (! loaded.empty())
        {
            std::sort (loaded.begin(), loaded.end(),
                       [] (const auto& a, const auto& b) { return a.x < b.x; });

            const juce::ScopedLock sl (curveModel.lock);
            curveModel.points = std::move (loaded);
        }
    }

    bakeCustomCurve();
}

void SideDuckAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = createStateXml())
        copyXmlToBinary (*xml, destData);
}

void SideDuckAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        applyStateXml (*xml, false);
}

juce::File SideDuckAudioProcessor::getPresetDirectory() const
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("SideDuck")
                   .getChildFile ("Presets");

    dir.createDirectory();
    return dir;
}

bool SideDuckAudioProcessor::savePresetToFile (const juce::File& file)
{
    if (auto xml = createStateXml())
        return xml->writeTo (file);

    return false;
}

bool SideDuckAudioProcessor::loadPresetFromFile (const juce::File& file)
{
    if (auto xml = juce::parseXML (file))
    {
        applyStateXml (*xml, true);
        return true;
    }

    return false;
}

void SideDuckAudioProcessor::switchToSlot (int slot)
{
    slot = juce::jlimit (0, 1, slot);

    if (slot == activeSlot)
        return;

    slotStates[activeSlot] = createStateXml();
    activeSlot = slot;

    // a slot that was never stored inherits the current sound, so comparing
    // starts from two identical states
    if (slotStates[slot] != nullptr)
        applyStateXml (*slotStates[slot], true);
}

void SideDuckAudioProcessor::copyCurrentToOtherSlot()
{
    slotStates[1 - activeSlot] = createStateXml();
}

void SideDuckAudioProcessor::resetToDefaults()
{
    for (auto* p : getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            ranged->beginChangeGesture();
            ranged->setValueNotifyingHost (ranged->getDefaultValue());
            ranged->endChangeGesture();
        }
    }

    pushCurveUndo();

    {
        const juce::ScopedLock sl (curveModel.lock);
        curveModel.points = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } };
    }

    bakeCustomCurve();
}

void SideDuckAudioProcessor::ensureFactoryPresets()
{
    struct Override
    {
        const char* id;
        float value;
    };

    struct Factory
    {
        const char* name;
        std::vector<Override> overrides;
        const char* curve;
    };

    // every preset starts from these defaults, then applies its overrides
    static const std::vector<Override> defaults = {
        { "wave", 2 }, { "power", 1 }, { "sync", 1 }, { "rateSync", 5 }, { "rateHz", 1 },
        { "depth", 1 }, { "phase", 0 }, { "smooth", 10 }, { "punch", 1 }, { "stereo", 0 },
        { "output", 0 }, { "scMode", 0 }, { "lookahead", 0 }, { "scSens", -20 }, { "midiTrig", 0 },
        { "lfo2Target", 0 }, { "lfo2Wave", 0 }, { "lfo2Sync", 1 }, { "lfo2Div", 5 },
        { "lfo2RateHz", 1 }, { "lfo2Depth", 0.5f }
    };

    static const std::vector<Factory> factories = {
        { "Classic Pump",
          { { "wave", 2 }, { "rateSync", 5 }, { "smooth", 15 }, { "punch", 1.8f } }, nullptr },
        { "Analog Pump (Exp)",
          { { "wave", 6 }, { "rateSync", 5 }, { "smooth", 8 } }, nullptr },
        { "Deep House Sine",
          { { "wave", 0 }, { "rateSync", 5 }, { "depth", 0.85f }, { "smooth", 35 }, { "punch", 1.2f } }, nullptr },
        { "Trance Gate 16ths",
          { { "wave", 4 }, { "rateSync", 7 }, { "smooth", 4 } }, nullptr },
        { "Stereo Wobble 8ths",
          { { "wave", 1 }, { "rateSync", 6 }, { "depth", 0.7f }, { "stereo", 90 }, { "smooth", 12 } }, nullptr },
        { "Stair Gate",
          { { "wave", 8 }, { "rateSync", 4 }, { "smooth", 6 } }, nullptr },
        { "4-Bar Riser",
          { { "wave", 2 }, { "rateSync", 1 }, { "smooth", 25 }, { "punch", 2.5f } }, nullptr },
        { "Tremolo 8 Hz",
          { { "wave", 0 }, { "sync", 0 }, { "rateHz", 8 }, { "depth", 0.5f } }, nullptr },
        { "Double Pump (Custom)",
          { { "wave", 5 }, { "rateSync", 5 }, { "punch", 1.3f } },
          "0.0000:0.0000,0.2000:1.0000,0.4800:1.0000,0.5000:0.0000,0.7000:1.0000,1.0000:1.0000" },
        { "SC Kick Duck (route kick to sidechain)",
          { { "scMode", 1 }, { "smooth", 30 }, { "lookahead", 3 } }, nullptr },
        { "SC Kick Retrigger (route kick to sidechain)",
          { { "wave", 6 }, { "scMode", 2 }, { "sync", 0 }, { "rateHz", 2 }, { "smooth", 8 } }, nullptr },
        { "Smooth Operator",
          { { "wave", 5 }, { "rateSync", 5 }, { "smooth", 18 } },
          "0.0000:0.0000:-0.700,0.4000:1.0000:0.000,1.0000:1.0000:0.000" },
        { "Bounce Groove",
          { { "wave", 5 }, { "rateSync", 5 }, { "smooth", 12 } },
          "0.0000:0.0000:-0.500,0.3000:1.0000:0.400,0.5000:0.2500:-0.500,0.7500:1.0000:0.000,1.0000:1.0000:0.000" },
        { "Half-Time Sway",
          { { "wave", 0 }, { "rateSync", 4 }, { "depth", 0.6f }, { "smooth", 45 } }, nullptr },
        { "Gated Quads (Stereo)",
          { { "wave", 4 }, { "rateSync", 5 }, { "stereo", 180 }, { "smooth", 6 } }, nullptr },
        { "8-Bar Fade Out",
          { { "wave", 3 }, { "rateSync", 0 }, { "smooth", 30 } }, nullptr },
        { "Tight Chop 1-32",
          { { "wave", 4 }, { "rateSync", 8 }, { "depth", 0.9f }, { "smooth", 2 } }, nullptr },
        { "Pump + Filter Sweep",
          { { "wave", 2 }, { "rateSync", 5 }, { "punch", 1.5f }, { "smooth", 12 },
            { "lfo2Target", 1 }, { "lfo2Wave", 0 }, { "lfo2Div", 3 }, { "lfo2Depth", 0.55f } }, nullptr },
        { "Auto-Pan 8ths",
          { { "wave", 0 }, { "depth", 0 },
            { "lfo2Target", 2 }, { "lfo2Wave", 0 }, { "lfo2Div", 6 }, { "lfo2Depth", 0.85f } }, nullptr },
    };

    const auto dir = getPresetDirectory();

    for (const auto& factory : factories)
    {
        auto file = dir.getChildFile (juce::String (factory.name) + ".sdpreset");

        if (file.existsAsFile())
            continue;

        juce::ValueTree tree (apvts.state.getType());

        auto values = defaults;

        for (const auto& override_ : factory.overrides)
            for (auto& v : values)
                if (juce::String (v.id) == override_.id)
                    v.value = override_.value;

        for (const auto& v : values)
        {
            juce::ValueTree param ("PARAM");
            param.setProperty ("id", v.id, nullptr);
            param.setProperty ("value", v.value, nullptr);
            tree.appendChild (param, nullptr);
        }

        if (factory.curve != nullptr)
            tree.setProperty ("curvePoints", factory.curve, nullptr);

        if (auto xml = tree.createXml())
            xml->writeTo (file);
    }
}

juce::AudioProcessorEditor* SideDuckAudioProcessor::createEditor()
{
    return new SideDuckAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SideDuckAudioProcessor();
}
