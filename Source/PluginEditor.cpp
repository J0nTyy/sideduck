#include "PluginEditor.h"

namespace
{
    constexpr int         scalePercents[] = { 80, 90, 100, 110, 125, 150 };
    constexpr const char* scaleNames[]    = { "Compact", "Reduced", "Default", "Comfort", "Large", "Extra Large" };
    constexpr int         snapDivisions[] = { 0, 32, 16, 8, 4 };
    constexpr float handleRadius     = 6.0f;
    constexpr float hitRadius        = 10.0f;
    constexpr float segmentHitRadius = 9.0f;
    constexpr float minPointGap      = 0.004f; // min phase distance between breakpoints

    // Maps a raw curve phase to its displayed position (the display is
    // shifted by the phase offset). Keeps x == 1 at the right edge instead of
    // wrapping it to 0.
    float displayXFor (float rawX, float phaseOffset)
    {
        float x = rawX - phaseOffset;

        if (x < 0.0f)
            x += 1.0f;

        return x;
    }

    // Inverse of displayXFor for click positions.
    float rawXFor (float displayX, float phaseOffset)
    {
        float x = displayX + phaseOffset;

        if (x > 1.0f)
            x -= 1.0f;

        return x;
    }

    juce::String curveToString (const std::vector<CurvePoint>& points)
    {
        juce::StringArray values;

        for (const auto& pt : points)
            values.add (juce::String (pt.x, 4) + ":" + juce::String (pt.y, 4) + ":" + juce::String (pt.curve, 3));

        return "SDCURVE|" + values.joinIntoString (",");
    }

    std::vector<CurvePoint> curveFromString (juce::String text)
    {
        text = text.trim();

        if (text.startsWith ("SDCURVE|"))
            text = text.fromFirstOccurrenceOf ("|", false, false);

        std::vector<CurvePoint> points;

        for (const auto& token : juce::StringArray::fromTokens (text, ",", ""))
        {
            auto parts = juce::StringArray::fromTokens (token, ":", "");

            if (parts.size() < 2)
                continue;

            CurvePoint pt;
            pt.x     = juce::jlimit (0.0f, 1.0f, parts[0].getFloatValue());
            pt.y     = juce::jlimit (0.0f, 1.0f, parts[1].getFloatValue());
            pt.curve = parts.size() > 2 ? juce::jlimit (-1.0f, 1.0f, parts[2].getFloatValue()) : 0.0f;
            points.push_back (pt);
        }

        std::sort (points.begin(), points.end(),
                   [] (const auto& a, const auto& b) { return a.x < b.x; });

        return points;
    }

    // Breakpoint equivalents of the built-in shapes, used when the display is
    // clicked while a built-in wave is selected: the visible shape converts
    // into editable points, so editing always starts from what is shown.
    std::vector<CurvePoint> pointsForShape (LFO::Shape shape)
    {
        switch (shape)
        {
            case LFO::Shape::sine:
            {
                std::vector<CurvePoint> points;

                for (int i = 0; i <= 8; ++i)
                {
                    const float x = (float) i / 8.0f;
                    points.push_back ({ x, LFO::value (LFO::Shape::sine, x), 0.0f });
                }

                return points;
            }

            case LFO::Shape::triangle: return { { 0.0f, 0.0f }, { 0.5f, 1.0f }, { 1.0f, 0.0f } };
            case LFO::Shape::sawDown:  return { { 0.0f, 1.0f }, { 1.0f, 0.0f } };
            case LFO::Shape::square:   return { { 0.0f, 0.0f }, { 0.4999f, 0.0f }, { 0.5f, 1.0f }, { 1.0f, 1.0f } };
            case LFO::Shape::expPump:  return { { 0.0f, 0.0f, -1.0f }, { 1.0f, 1.0f } };
            case LFO::Shape::pulse:    return { { 0.0f, 0.0f }, { 0.2499f, 0.0f }, { 0.25f, 1.0f }, { 1.0f, 1.0f } };
            case LFO::Shape::steps:    return { { 0.0f, 0.0f },       { 0.2499f, 0.0f },
                                                { 0.25f, 0.3333f },   { 0.4999f, 0.3333f },
                                                { 0.5f, 0.6667f },    { 0.7499f, 0.6667f },
                                                { 0.75f, 1.0f },      { 1.0f, 1.0f } };

            case LFO::Shape::sawUp:
            case LFO::Shape::custom:
            default:                   return { { 0.0f, 0.0f }, { 1.0f, 1.0f } };
        }
    }
}

//==============================================================================
juce::Rectangle<float> LfoVisualizer::plotArea() const
{
    return getLocalBounds().toFloat().reduced (1.0f).reduced (12.0f, 14.0f);
}

float LfoVisualizer::currentPhaseOffset() const
{
    return processor.apvts.getRawParameterValue ("phase")->load() / 360.0f;
}

float LfoVisualizer::snapDisplayX (float displayX, bool bypassSnap) const
{
    if (snapDivision > 0 && ! bypassSnap)
        displayX = std::round (displayX * (float) snapDivision) / (float) snapDivision;

    return juce::jlimit (0.0f, 1.0f, displayX);
}

juce::Point<float> LfoVisualizer::pointToScreen (juce::Point<float> rawPoint, float phaseOffset) const
{
    const auto plot = plotArea();

    return { plot.getX() + displayXFor (rawPoint.x, phaseOffset) * plot.getWidth(),
             plot.getBottom() - rawPoint.y * plot.getHeight() };
}

juce::Point<float> LfoVisualizer::screenToPoint (juce::Point<float> screenPos, float phaseOffset,
                                                 bool bypassSnap) const
{
    const auto plot = plotArea();

    float displayX = juce::jlimit (0.0f, 1.0f, (screenPos.x - plot.getX()) / plot.getWidth());
    displayX = snapDisplayX (displayX, bypassSnap);

    const float gain = juce::jlimit (0.0f, 1.0f, (plot.getBottom() - screenPos.y) / plot.getHeight());

    return { rawXFor (displayX, phaseOffset), gain };
}

bool LfoVisualizer::customWaveActive() const
{
    return (int) processor.apvts.getRawParameterValue ("wave")->load() == (int) LFO::Shape::custom;
}

int LfoVisualizer::hitTestPoint (juce::Point<float> screenPos) const
{
    const float phaseOffset = currentPhaseOffset();

    const juce::ScopedLock sl (processor.curveModel.lock);
    const auto& points = processor.curveModel.points;

    int   best     = -1;
    float bestDist = hitRadius;

    for (int i = 0; i < (int) points.size(); ++i)
    {
        const float dist = pointToScreen ({ points[(size_t) i].x, points[(size_t) i].y }, phaseOffset)
                               .getDistanceFrom (screenPos);

        if (dist < bestDist)
        {
            bestDist = dist;
            best = i;
        }
    }

    return best;
}

bool LfoVisualizer::segmentHandlePosition (int segmentIndex, float phaseOffset,
                                           juce::Point<float>& outScreen) const
{
    const juce::ScopedLock sl (processor.curveModel.lock);
    const auto& points = processor.curveModel.points;
    const int n = (int) points.size();

    if (n < 2 || segmentIndex < 0 || segmentIndex >= n)
        return false;

    const auto& a = points[(size_t) segmentIndex];
    const auto& b = points[(size_t) ((segmentIndex + 1) % n)];

    const float span = segmentIndex == n - 1 ? (1.0f - a.x + b.x) : (b.x - a.x);

    // nothing to bend on tiny or flat segments
    if (span < 0.02f || std::abs (b.y - a.y) < 0.02f)
        return false;

    float midX = a.x + span * 0.5f;
    midX -= std::floor (midX);

    outScreen = pointToScreen ({ midX, processor.customCurve.valueAt (midX) }, phaseOffset);
    return true;
}

int LfoVisualizer::hitTestSegment (juce::Point<float> screenPos) const
{
    const float phaseOffset = currentPhaseOffset();

    const int n = [this]
    {
        const juce::ScopedLock sl (processor.curveModel.lock);
        return (int) processor.curveModel.points.size();
    }();

    for (int i = 0; i < n; ++i)
    {
        juce::Point<float> handle;

        if (segmentHandlePosition (i, phaseOffset, handle)
            && handle.getDistanceFrom (screenPos) < segmentHitRadius)
            return i;
    }

    return -1;
}

//==============================================================================
// painting

void LfoVisualizer::paintGrid (juce::Graphics& g, juce::Rectangle<float> plot) const
{
    const int divisions = snapDivision > 0 ? snapDivision : 16;

    for (int i = 1; i < divisions; ++i)
    {
        const bool  strong = divisions >= 4 && (i * 4) % divisions == 0;
        const float x      = plot.getX() + plot.getWidth() * (float) i / (float) divisions;

        g.setColour (strong ? theme.grid.withMultipliedAlpha (2.0f) : theme.grid);
        g.fillRect (juce::Rectangle<float> (x - 0.5f, plot.getY(), 1.0f, plot.getHeight()));
    }

    for (int i = 1; i < 4; ++i)
    {
        const float y = plot.getY() + plot.getHeight() * (float) i / 4.0f;
        g.setColour (theme.grid);
        g.fillRect (juce::Rectangle<float> (plot.getX(), y - 0.5f, plot.getWidth(), 1.0f));
    }
}

void LfoVisualizer::paintDuckView (juce::Graphics& g, juce::Rectangle<float> plot) const
{
    auto gainToY = [&plot] (float gain)
    {
        return plot.getBottom() - juce::jlimit (0.0f, 1.0f, gain) * plot.getHeight();
    };

    // scrolling trace of the actual gain reduction, oldest on the left
    constexpr int n   = SideDuckAudioProcessor::gainHistorySize;
    const int     pos = processor.gainHistoryPos.load();

    juce::Path trace;

    for (int i = 0; i < n; ++i)
    {
        const float gain = processor.gainHistory[(size_t) ((pos + i) % n)].load();
        const float x    = plot.getX() + plot.getWidth() * (float) i / (float) (n - 1);
        const float y    = gainToY (gain);

        if (i == 0)
            trace.startNewSubPath (x, y);
        else
            trace.lineTo (x, y);
    }

    juce::Path fill (trace);
    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.lineTo (plot.getX(), plot.getBottom());
    fill.closeSubPath();

    juce::ColourGradient fillGradient (theme.accent.withAlpha (0.30f), plot.getX(), plot.getY(),
                                       theme.accent.withAlpha (0.02f), plot.getX(), plot.getBottom(), false);
    g.setGradientFill (fillGradient);
    g.fillPath (fill);

    g.setColour (theme.accent.withAlpha (0.20f));
    g.strokePath (trace, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved));
    g.setColour (theme.accent);
    g.strokePath (trace, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));

    // live gain marker at the right edge
    const float gain = processor.currentGain.load();
    const float gy   = gainToY (gain);

    g.setColour (theme.accent.withAlpha (0.30f));
    g.fillEllipse (plot.getRight() - 8.0f, gy - 8.0f, 16.0f, 16.0f);
    g.setColour (theme.playhead);
    g.fillEllipse (plot.getRight() - 3.5f, gy - 3.5f, 7.0f, 7.0f);

    g.setFont (SideDuckLookAndFeel::uiFont (10.5f, true).withExtraKerningFactor (0.06f));
    g.setColour (theme.dimText);
    g.drawText ("SIDECHAIN DUCK  /  LIVE GAIN REDUCTION",
                plot.toNearestInt().removeFromTop (14), juce::Justification::topLeft);

    const float db = juce::Decibels::gainToDecibels (juce::jmax (gain, 0.001f));
    g.drawText (juce::String (db, 1) + " dB",
                plot.toNearestInt().removeFromTop (14), juce::Justification::topRight);
}

void LfoVisualizer::paintPlayhead (juce::Graphics& g, juce::Rectangle<float> plot) const
{
    const float phaseOffset = currentPhaseOffset();

    float ph = processor.currentPhase.load() - phaseOffset;
    ph -= std::floor (ph);

    const float x = plot.getX() + ph * plot.getWidth();

    juce::ColourGradient lineGradient (theme.playhead.withAlpha (0.0f), x, plot.getY(),
                                       theme.playhead.withAlpha (0.55f), x, plot.getBottom(), false);
    g.setGradientFill (lineGradient);
    g.fillRect (juce::Rectangle<float> (x - 0.75f, plot.getY(), 1.5f, plot.getHeight()));
}

void LfoVisualizer::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);

    juce::ColourGradient panelGradient (theme.panel.brighter (theme.light ? 0.01f : 0.05f),
                                        bounds.getX(), bounds.getY(),
                                        theme.panel.darker (theme.light ? 0.02f : 0.08f),
                                        bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill (panelGradient);
    g.fillRoundedRectangle (bounds, 8.0f);

    auto plot = plotArea();
    paintGrid (g, plot);

    const int activeMode = processor.activeScMode.load();

    if (activeMode == 1)
    {
        // external duck: show the real gain reduction, not the unused LFO
        paintDuckView (g, plot);

        g.setColour (theme.panelBorder);
        g.drawRoundedRectangle (bounds, 8.0f, 1.0f);
        return;
    }

    auto& apvts = processor.apvts;

    const auto  shape       = static_cast<LFO::Shape> ((int) apvts.getRawParameterValue ("wave")->load());
    const float depth       = apvts.getRawParameterValue ("depth")->load();
    const float phaseOffset = currentPhaseOffset();

    auto gainToY = [&plot] (float gain)
    {
        return plot.getBottom() - gain * plot.getHeight();
    };

    auto gainAt = [&] (float ph)
    {
        const float wrapped = std::fmod (ph + phaseOffset, 1.0f);
        const float v = shape == LFO::Shape::custom
                            ? processor.customCurve.valueAt (wrapped)
                            : LFO::value (shape, wrapped);

        return 1.0f - depth * (1.0f - v);
    };

    constexpr int numPoints = 256;

    juce::Path curve;
    curve.startNewSubPath (plot.getX(), gainToY (gainAt (0.0f)));

    for (int i = 1; i <= numPoints; ++i)
    {
        const float ph = (float) i / (float) numPoints;
        curve.lineTo (plot.getX() + ph * plot.getWidth(), gainToY (gainAt (ph)));
    }

    juce::Path fill (curve);
    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.lineTo (plot.getX(), plot.getBottom());
    fill.closeSubPath();

    juce::ColourGradient fillGradient (theme.accent.withAlpha (0.30f), plot.getX(), plot.getY(),
                                       theme.accent.withAlpha (0.02f), plot.getX(), plot.getBottom(), false);
    g.setGradientFill (fillGradient);
    g.fillPath (fill);

    g.setColour (theme.accent.withAlpha (0.20f));
    g.strokePath (curve, juce::PathStrokeType (5.5f, juce::PathStrokeType::curved));
    g.setColour (theme.accent);
    g.strokePath (curve, juce::PathStrokeType (2.25f, juce::PathStrokeType::curved));

    if (shape == LFO::Shape::custom)
    {
        const juce::ScopedLock sl (processor.curveModel.lock);
        const auto& points = processor.curveModel.points;

        // The main curve shows the result with Depth applied; breakpoints are
        // edited in raw curve space, so show a dashed guide through them when
        // the two differ.
        if (depth < 0.999f && points.size() > 1)
        {
            juce::Path raw;

            raw.startNewSubPath (plot.getX(), gainToY (processor.customCurve.valueAt (phaseOffset)));

            for (int i = 1; i <= numPoints; ++i)
            {
                const float ph = (float) i / (float) numPoints;
                raw.lineTo (plot.getX() + ph * plot.getWidth(),
                            gainToY (processor.customCurve.valueAt (std::fmod (ph + phaseOffset, 1.0f))));
            }

            juce::Path dashed;
            const float dashes[] = { 4.0f, 4.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, raw, dashes, 2);

            g.setColour (theme.playhead.withAlpha (0.22f));
            g.fillPath (dashed);
        }

        // segment tension handles (diamonds)
        for (int i = 0; i < (int) points.size(); ++i)
        {
            juce::Point<float> handle;

            if (! segmentHandlePosition (i, phaseOffset, handle))
                continue;

            const bool active = i == hoveredSegment || (dragMode == Drag::tension && i == draggedIndex);
            const float r     = active ? 5.0f : 4.0f;

            juce::Path diamond;
            diamond.addRectangle (-r, -r, r * 2.0f, r * 2.0f);
            diamond.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::pi / 4.0f)
                                        .translated (handle));

            g.setColour (theme.panel);
            g.fillPath (diamond);
            g.setColour (active ? theme.playhead : theme.accent.withAlpha (0.75f));
            g.strokePath (diamond, juce::PathStrokeType (1.6f));
        }

        // breakpoint handles
        for (int i = 0; i < (int) points.size(); ++i)
        {
            const auto centre = pointToScreen ({ points[(size_t) i].x, points[(size_t) i].y }, phaseOffset);
            const bool active = i == draggedIndex || i == hoveredIndex;
            const float r     = active ? handleRadius + 1.5f : handleRadius;

            if (i == selectedIndex)
            {
                g.setColour (theme.accent.withAlpha (0.35f));
                g.fillEllipse (centre.x - r - 3.5f, centre.y - r - 3.5f, (r + 3.5f) * 2.0f, (r + 3.5f) * 2.0f);
            }

            g.setColour (theme.panel);
            g.fillEllipse (centre.x - r, centre.y - r, r * 2.0f, r * 2.0f);
            g.setColour (active ? theme.playhead : theme.accent);
            g.drawEllipse (centre.x - r, centre.y - r, r * 2.0f, r * 2.0f, 2.0f);
            g.setColour ((active ? theme.playhead : theme.accent).withAlpha (0.5f));
            g.fillEllipse (centre.x - r * 0.45f, centre.y - r * 0.45f, r * 0.9f, r * 0.9f);
        }
    }

    if (activeMode == 2 || activeMode == 3)
    {
        g.setFont (SideDuckLookAndFeel::uiFont (10.5f, true).withExtraKerningFactor (0.06f));
        g.setColour (theme.dimText);
        g.drawText (activeMode == 2 ? "SIDECHAIN TRIGGER  /  ONE-SHOT ON TRANSIENT"
                                    : "MIDI RETRIGGER  /  ONE-SHOT ON NOTE",
                    plot.toNearestInt().removeFromTop (14), juce::Justification::topLeft);
    }

    paintPlayhead (g, plot);

    g.setColour (theme.panelBorder);
    g.drawRoundedRectangle (bounds, 8.0f, 1.0f);
}

//==============================================================================
// interaction

void LfoVisualizer::switchToCustomWave()
{
    if (auto* param = processor.apvts.getParameter ("wave"))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 ((float) LFO::Shape::custom));
        param->endChangeGesture();
    }
}

void LfoVisualizer::removePoint (int index)
{
    {
        const juce::ScopedLock sl (processor.curveModel.lock);
        auto& points = processor.curveModel.points;

        if (points.size() <= 1 || index < 0 || index >= (int) points.size())
            return;

        points.erase (points.begin() + index);
    }

    draggedIndex  = -1;
    hoveredIndex  = -1;
    selectedIndex = -1;
    dragMode      = Drag::none;
    processor.bakeCustomCurve();
    repaint();
}

void LfoVisualizer::showContextMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Copy curve");
    menu.addItem (2, "Paste curve");
    menu.addSeparator();
    menu.addItem (3, "Reverse");
    menu.addItem (4, "Flip vertical");
    menu.addItem (5, "Normalize");
    menu.addSeparator();
    menu.addItem (6, "Reset curve");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [this] (int result)
    {
        if (result > 0)
            applyCurveMenu (result);
    });
}

void LfoVisualizer::applyCurveMenu (int result)
{
    auto& model = processor.curveModel;

    if (result == 1) // copy
    {
        const juce::ScopedLock sl (model.lock);
        juce::SystemClipboard::copyTextToClipboard (curveToString (model.points));
        return;
    }

    if (result == 2) // paste
    {
        auto pasted = curveFromString (juce::SystemClipboard::getTextFromClipboard());

        if (pasted.empty())
            return;

        processor.pushCurveUndo();

        {
            const juce::ScopedLock sl (model.lock);
            model.points = std::move (pasted);
        }

        switchToCustomWave();
    }
    else if (result == 3) // reverse
    {
        processor.pushCurveUndo();

        const juce::ScopedLock sl (model.lock);
        auto& pts = model.points;
        const int n = (int) pts.size();

        std::vector<CurvePoint> reversed;
        reversed.reserve ((size_t) n);

        for (int j = 0; j < n; ++j)
        {
            const auto& src = pts[(size_t) (n - 1 - j)];

            CurvePoint pt;
            pt.x = 1.0f - src.x;
            pt.y = src.y;
            // the segment leaving the new point mirrors the segment that
            // entered the source point
            pt.curve = -pts[(size_t) (((n - 2 - j) % n + n) % n)].curve;
            reversed.push_back (pt);
        }

        pts = std::move (reversed);
    }
    else if (result == 4) // flip vertical
    {
        processor.pushCurveUndo();

        const juce::ScopedLock sl (model.lock);
        for (auto& pt : model.points)
            pt.y = 1.0f - pt.y;
    }
    else if (result == 5) // normalize
    {
        processor.pushCurveUndo();

        const juce::ScopedLock sl (model.lock);
        auto& pts = model.points;

        float minY = 1.0f, maxY = 0.0f;

        for (const auto& pt : pts)
        {
            minY = juce::jmin (minY, pt.y);
            maxY = juce::jmax (maxY, pt.y);
        }

        if (maxY - minY > 0.001f)
            for (auto& pt : pts)
                pt.y = (pt.y - minY) / (maxY - minY);
    }
    else if (result == 6) // reset
    {
        processor.pushCurveUndo();

        const juce::ScopedLock sl (model.lock);
        model.points = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } };
        selectedIndex = -1;
        switchToCustomWave();
    }

    processor.bakeCustomCurve();
    repaint();
}

void LfoVisualizer::mouseDown (const juce::MouseEvent& e)
{
    if (processor.activeScMode.load() == 1)
        return; // duck view shows live gain reduction; the curve isn't in use

    if (! customWaveActive())
    {
        if (e.mods.isRightButtonDown())
        {
            showContextMenu();
            return;
        }

        // convert the visible built-in shape into breakpoints so editing
        // starts from exactly what is shown (undoable)
        const auto shape = static_cast<LFO::Shape> (
            (int) processor.apvts.getRawParameterValue ("wave")->load());

        processor.pushCurveUndo();

        {
            const juce::ScopedLock sl (processor.curveModel.lock);
            processor.curveModel.points = pointsForShape (shape);
        }

        selectedIndex = -1;
        processor.bakeCustomCurve();
        switchToCustomWave();
        repaint();
        return;
    }

    const int hitP = hitTestPoint (e.position);

    if (e.mods.isRightButtonDown())
    {
        if (hitP >= 0)
        {
            processor.pushCurveUndo();
            removePoint (hitP);
        }
        else if (const int hitS = hitTestSegment (e.position); hitS >= 0)
        {
            processor.pushCurveUndo();

            {
                const juce::ScopedLock sl (processor.curveModel.lock);

                if (hitS < (int) processor.curveModel.points.size())
                    processor.curveModel.points[(size_t) hitS].curve = 0.0f;
            }

            processor.bakeCustomCurve();
            repaint();
        }
        else
        {
            showContextMenu();
        }

        return;
    }

    // snapshot for a single undo step recorded on first drag movement
    {
        const juce::ScopedLock sl (processor.curveModel.lock);
        dragSnapshot = processor.curveModel.points;
    }
    dragChanged = false;

    if (hitP >= 0)
    {
        selectedIndex = hitP;
        draggedIndex  = hitP;
        dragMode      = Drag::point;
        repaint();
        return;
    }

    if (const int hitS = hitTestSegment (e.position); hitS >= 0)
    {
        draggedIndex = hitS;
        dragMode     = Drag::tension;
        return;
    }

    // add a new point where clicked and start dragging it
    processor.pushCurveUndo();
    dragChanged = true;

    const auto curvePos = screenToPoint (e.position, currentPhaseOffset(), e.mods.isShiftDown());

    {
        const juce::ScopedLock sl (processor.curveModel.lock);
        auto& points = processor.curveModel.points;

        auto insertAt = std::lower_bound (points.begin(), points.end(), curvePos.x,
                                          [] (const CurvePoint& p, float x) { return p.x < x; });

        draggedIndex = (int) std::distance (points.begin(), insertAt);
        points.insert (insertAt, { curvePos.x, curvePos.y, 0.0f });
    }

    selectedIndex = draggedIndex;
    dragMode      = Drag::point;
    processor.bakeCustomCurve();
    repaint();
}

void LfoVisualizer::mouseDrag (const juce::MouseEvent& e)
{
    if (dragMode == Drag::none || draggedIndex < 0)
        return;

    if (! dragChanged)
    {
        processor.pushCurveUndoState (dragSnapshot);
        dragChanged = true;
    }

    if (dragMode == Drag::point)
    {
        const auto curvePos = screenToPoint (e.position, currentPhaseOffset(), e.mods.isShiftDown());

        {
            const juce::ScopedLock sl (processor.curveModel.lock);
            auto& points = processor.curveModel.points;

            if (draggedIndex >= (int) points.size())
                return;

            // clamp between neighbours so the point order never changes
            const float minX = draggedIndex > 0
                                   ? points[(size_t) draggedIndex - 1].x + minPointGap : 0.0f;
            const float maxX = draggedIndex < (int) points.size() - 1
                                   ? points[(size_t) draggedIndex + 1].x - minPointGap : 1.0f;

            auto& pt = points[(size_t) draggedIndex];
            pt.x = juce::jlimit (minX, juce::jmax (minX, maxX), curvePos.x);
            pt.y = curvePos.y;
        }
    }
    else // tension
    {
        const auto plot = plotArea();
        const float m   = juce::jlimit (0.0f, 1.0f, (plot.getBottom() - e.position.y) / plot.getHeight());

        const juce::ScopedLock sl (processor.curveModel.lock);
        auto& points = processor.curveModel.points;
        const int n  = (int) points.size();

        if (draggedIndex >= n)
            return;

        auto& a       = points[(size_t) draggedIndex];
        const auto& b = points[(size_t) ((draggedIndex + 1) % n)];

        if (std::abs (b.y - a.y) < 0.02f)
            return;

        // solve the tension so the segment midpoint lands on the mouse
        const float r = juce::jlimit (0.02f, 0.98f, (m - a.y) / (b.y - a.y));
        const float k = std::log (r) / std::log (0.5f);

        a.curve = juce::jlimit (-1.0f, 1.0f, std::log2 (k) / 3.0f);
    }

    processor.bakeCustomCurve();
    repaint();
}

void LfoVisualizer::mouseUp (const juce::MouseEvent&)
{
    dragMode     = Drag::none;
    draggedIndex = -1;
}

void LfoVisualizer::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! customWaveActive() || processor.activeScMode.load() == 1)
        return;

    if (const int hitP = hitTestPoint (e.position); hitP >= 0)
    {
        processor.pushCurveUndo();
        removePoint (hitP);
    }
    else if (const int hitS = hitTestSegment (e.position); hitS >= 0)
    {
        processor.pushCurveUndo();

        {
            const juce::ScopedLock sl (processor.curveModel.lock);

            if (hitS < (int) processor.curveModel.points.size())
                processor.curveModel.points[(size_t) hitS].curve = 0.0f;
        }

        processor.bakeCustomCurve();
        repaint();
    }
}

void LfoVisualizer::mouseMove (const juce::MouseEvent& e)
{
    const bool editable = customWaveActive() && processor.activeScMode.load() != 1;

    const int hitP = editable ? hitTestPoint (e.position) : -1;
    const int hitS = editable && hitP < 0 ? hitTestSegment (e.position) : -1;

    if (hitP != hoveredIndex || hitS != hoveredSegment)
    {
        hoveredIndex   = hitP;
        hoveredSegment = hitS;
        setMouseCursor (hitP >= 0 || hitS >= 0 ? juce::MouseCursor::PointingHandCursor
                                               : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void LfoVisualizer::mouseExit (const juce::MouseEvent&)
{
    hoveredIndex   = -1;
    hoveredSegment = -1;
    repaint();
}

//==============================================================================
ContentComponent::ContentComponent (SideDuckAudioProcessor& p,
                                    std::function<void (int)> scaleCallback,
                                    std::function<void (int)> themeCallback)
    : processor (p),
      onScaleSelected (std::move (scaleCallback)),
      onThemeSelected (std::move (themeCallback)),
      theme (Theme::byIndex (Theme::loadSavedIndex())),
      visualizer (p)
{
    setWantsKeyboardFocus (true);

    visualizer.theme = theme;
    addAndMakeVisible (visualizer);

    powerButton.setClickingTogglesState (true);
    addAndMakeVisible (powerButton);

    for (int i = 0; i < (int) std::size (scalePercents); ++i)
        scaleBox.addItem (juce::String (scalePercents[i]) + " %  -  " + scaleNames[i], i + 1);

    scaleBox.onChange = [this]
    {
        const int index = scaleBox.getSelectedItemIndex();

        if (index >= 0 && onScaleSelected)
            onScaleSelected (scalePercents[index]);
    };
    addAndMakeVisible (scaleBox);

    for (int i = 0; i < (int) Theme::all().size(); ++i)
        themeBox.addItem (Theme::all()[(size_t) i].name, i + 1);

    themeBox.onChange = [this]
    {
        const int index = themeBox.getSelectedItemIndex();

        if (index >= 0 && onThemeSelected)
            onThemeSelected (index);
    };
    addAndMakeVisible (themeBox);

    // small-caps faceplate captions shared by every row label
    auto setLegend = [this] (juce::Label& label, const juce::String& text)
    {
        label.setText (text.toUpperCase(), juce::dontSendNotification);
        label.setFont (SideDuckLookAndFeel::legendFont (11.0f));
        label.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (label);
    };

    // preset bar
    setLegend (presetLabel, "Preset");

    presetBox.setTextWhenNothingSelected ("(none)");
    presetBox.onChange = [this] { loadSelectedPreset(); };
    addAndMakeVisible (presetBox);

    for (auto* b : { &savePresetButton, &importPresetButton, &exportPresetButton, &undoButton, &redoButton,
                     &slotAButton, &slotBButton, &copySlotButton })
        addAndMakeVisible (*b);

    savePresetButton.onClick   = [this] { launchSaveChooser(); };
    importPresetButton.onClick = [this] { launchImportChooser(); };
    exportPresetButton.onClick = [this] { launchExportChooser(); };
    undoButton.onClick         = [this] { doUndo(); };
    redoButton.onClick         = [this] { doRedo(); };

    slotAButton.onClick    = [this] { processor.switchToSlot (0); visualizer.repaint(); };
    slotBButton.onClick    = [this] { processor.switchToSlot (1); visualizer.repaint(); };
    copySlotButton.onClick = [this] { processor.copyCurrentToOtherSlot(); };

    refreshPresetList();

    waveBox.addItemList ({ "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Custom", "Exp Pump", "Pulse", "Steps" }, 1);
    addAndMakeVisible (waveBox);
    setLegend (waveLabel, "Shape");

    addAndMakeVisible (syncButton);
    addAndMakeVisible (midiButton);

    rateSyncBox.addItemList (SideDuckAudioProcessor::divisionNames(), 1);
    addAndMakeVisible (rateSyncBox);

    setLegend (scLabel, "Sidechain");

    scModeBox.addItemList ({ "Off", "Duck", "Trigger" }, 1);
    addAndMakeVisible (scModeBox);

    // LFO 2 row
    setLegend (lfo2Label, "LFO 2");
    setLegend (lfo2RateLabel, "Rate");
    setLegend (lfo2DepthLabel, "Depth");

    lfo2TargetBox.addItemList ({ "Off", "Filter", "Pan" }, 1);
    addAndMakeVisible (lfo2TargetBox);

    lfo2WaveBox.addItemList ({ "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Custom", "Exp Pump", "Pulse", "Steps" }, 1);
    addAndMakeVisible (lfo2WaveBox);

    addAndMakeVisible (lfo2SyncButton);

    lfo2DivBox.addItemList (SideDuckAudioProcessor::divisionNames(), 1);
    addAndMakeVisible (lfo2DivBox);

    for (auto* s : { &lfo2RateSlider, &lfo2DepthSlider })
    {
        s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 15);
        s->setRepaintsOnMouseActivity (true);
        addAndMakeVisible (*s);
    }

    attachKnobDefaults (lfo2RateSlider, "lfo2RateHz");
    attachKnobDefaults (lfo2DepthSlider, "lfo2Depth");

    setLegend (snapLabel, "Snap");

    snapBox.addItemList ({ "Off", "1/32", "1/16", "1/8", "1/4" }, 1);
    snapBox.setSelectedItemIndex (0, juce::dontSendNotification);
    snapBox.onChange = [this]
    {
        const int index = snapBox.getSelectedItemIndex();

        if (index >= 0)
            visualizer.snapDivision = snapDivisions[index];
    };
    addAndMakeVisible (snapBox);

    hintLabel.setText ("Hold Shift to bypass snapping. Right-click the graph for curve options, or a knob to reset it.",
                       juce::dontSendNotification);
    hintLabel.setFont (SideDuckLookAndFeel::uiFont (11.0f));
    hintLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (hintLabel);

    setupKnob (rateHzSlider,    rateLabel,      "Rate",      "rateHz");
    setupKnob (depthSlider,     depthLabel,     "Depth",     "depth");
    setupKnob (phaseSlider,     phaseLabel,     "Phase",     "phase");
    setupKnob (smoothSlider,    smoothLabel,    "Smooth",    "smooth");
    setupKnob (punchSlider,     punchLabel,     "Punch",     "punch");
    setupKnob (stereoSlider,    stereoLabel,    "Stereo",    "stereo");
    setupKnob (outputSlider,    outputLabel,    "Output",    "output");
    setupKnob (lookaheadSlider, lookaheadLabel, "Lookahead", "lookahead");
    setupKnob (senseSlider,     senseLabel,     "Sense",     "scSens");

    auto& apvts = processor.apvts;
    waveAttachment       = std::make_unique<ComboBoxAttachment> (apvts, "wave", waveBox);
    rateSyncAttachment   = std::make_unique<ComboBoxAttachment> (apvts, "rateSync", rateSyncBox);
    scModeAttachment     = std::make_unique<ComboBoxAttachment> (apvts, "scMode", scModeBox);
    lfo2TargetAttachment = std::make_unique<ComboBoxAttachment> (apvts, "lfo2Target", lfo2TargetBox);
    lfo2WaveAttachment   = std::make_unique<ComboBoxAttachment> (apvts, "lfo2Wave", lfo2WaveBox);
    lfo2DivAttachment    = std::make_unique<ComboBoxAttachment> (apvts, "lfo2Div", lfo2DivBox);
    powerAttachment      = std::make_unique<ButtonAttachment>   (apvts, "power", powerButton);
    syncAttachment       = std::make_unique<ButtonAttachment>   (apvts, "sync", syncButton);
    midiTrigAttachment   = std::make_unique<ButtonAttachment>   (apvts, "midiTrig", midiButton);
    lfo2SyncAttachment   = std::make_unique<ButtonAttachment>   (apvts, "lfo2Sync", lfo2SyncButton);
    rateHzAttachment     = std::make_unique<SliderAttachment>   (apvts, "rateHz", rateHzSlider);
    depthAttachment      = std::make_unique<SliderAttachment>   (apvts, "depth", depthSlider);
    phaseAttachment      = std::make_unique<SliderAttachment>   (apvts, "phase", phaseSlider);
    smoothAttachment     = std::make_unique<SliderAttachment>   (apvts, "smooth", smoothSlider);
    punchAttachment      = std::make_unique<SliderAttachment>   (apvts, "punch", punchSlider);
    stereoAttachment     = std::make_unique<SliderAttachment>   (apvts, "stereo", stereoSlider);
    outputAttachment     = std::make_unique<SliderAttachment>   (apvts, "output", outputSlider);
    lookaheadAttachment  = std::make_unique<SliderAttachment>   (apvts, "lookahead", lookaheadSlider);
    senseAttachment      = std::make_unique<SliderAttachment>   (apvts, "scSens", senseSlider);
    lfo2RateAttachment   = std::make_unique<SliderAttachment>   (apvts, "lfo2RateHz", lfo2RateSlider);
    lfo2DepthAttachment  = std::make_unique<SliderAttachment>   (apvts, "lfo2Depth", lfo2DepthSlider);

    applyThemeToControls();
    applyTooltips();

    startTimerHz (10);
    timerCallback(); // set initial enablement + power text

    setSize (baseWidth, baseHeight);
}

//==============================================================================
// theme + undo

void ContentComponent::setTheme (const Theme& newTheme)
{
    theme = newTheme;
    visualizer.theme = newTheme;
    applyThemeToControls();
    repaint();
}

void ContentComponent::applyThemeToControls()
{
    powerButton.setColour (juce::TextButton::buttonColourId, theme.control);
    powerButton.setColour (juce::TextButton::buttonOnColourId, theme.accentSoft);
}

void ContentComponent::setThemeSelection (int index)
{
    themeBox.setSelectedItemIndex (index, juce::dontSendNotification);
}

void ContentComponent::doUndo()
{
    if (processor.undoCurve())
        visualizer.repaint();
}

void ContentComponent::doRedo()
{
    if (processor.redoCurve())
        visualizer.repaint();
}

bool ContentComponent::keyPressed (const juce::KeyPress& key)
{
    const auto ctrl      = juce::ModifierKeys::ctrlModifier;
    const auto ctrlShift = juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier;

    if (key == juce::KeyPress ('z', ctrl, 0))
    {
        doUndo();
        return true;
    }

    if (key == juce::KeyPress ('y', ctrl, 0) || key == juce::KeyPress ('z', ctrlShift, 0))
    {
        doRedo();
        return true;
    }

    return false;
}

void ContentComponent::mouseDown (const juce::MouseEvent&)
{
    grabKeyboardFocus();
}

//==============================================================================
// presets

void ContentComponent::refreshPresetList (const juce::String& selectName)
{
    presetFiles.clear();
    presetBox.clear (juce::dontSendNotification);

    // item 1 is always the built-in init state; files follow from item 2
    presetBox.addItem ("Init (Default)", 1);
    presetBox.addSeparator();

    auto files = processor.getPresetDirectory().findChildFiles (juce::File::findFiles, false, "*.sdpreset");
    files.sort();

    int selectId = 0;

    for (int i = 0; i < files.size(); ++i)
    {
        presetFiles.add (files[i]);
        presetBox.addItem (files[i].getFileNameWithoutExtension(), i + 2);

        if (files[i].getFileNameWithoutExtension() == selectName)
            selectId = i + 2;
    }

    if (selectId > 0)
        presetBox.setSelectedId (selectId, juce::dontSendNotification);
}

void ContentComponent::loadSelectedPreset()
{
    const int id = presetBox.getSelectedId();

    if (id == 1)
    {
        processor.resetToDefaults();
        visualizer.repaint();
        return;
    }

    const int fileIndex = id - 2;

    if (fileIndex >= 0 && fileIndex < presetFiles.size())
        processor.loadPresetFromFile (presetFiles[fileIndex]);
}

void ContentComponent::launchSaveChooser()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Save preset", processor.getPresetDirectory().getChildFile ("MyPreset.sdpreset"), "*.sdpreset");

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();

        if (file == juce::File{})
            return;

        file = file.withFileExtension ("sdpreset");

        if (processor.savePresetToFile (file))
            refreshPresetList (file.getFileNameWithoutExtension());
    });
}

void ContentComponent::launchImportChooser()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Import preset",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.sdpreset;*.xml");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();

        if (file == juce::File{} || ! file.existsAsFile())
            return;

        // copy into the preset library, then load it
        auto dest = processor.getPresetDirectory()
                        .getChildFile (file.getFileNameWithoutExtension() + ".sdpreset");

        file.copyFileTo (dest);

        if (processor.loadPresetFromFile (dest))
            refreshPresetList (dest.getFileNameWithoutExtension());
    });
}

void ContentComponent::launchExportChooser()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Export preset",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("MyPreset.sdpreset"),
        "*.sdpreset");

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();

        if (file != juce::File{})
            processor.savePresetToFile (file.withFileExtension ("sdpreset"));
    });
}

//==============================================================================

void ContentComponent::updateScaleDisplay (int percent)
{
    // show the nearest step; free resizing can land between the steps
    int best = 0;

    for (int i = 1; i < (int) std::size (scalePercents); ++i)
        if (std::abs (scalePercents[i] - percent) < std::abs (scalePercents[best] - percent))
            best = i;

    scaleBox.setSelectedItemIndex (best, juce::dontSendNotification);
}

void ContentComponent::attachKnobDefaults (KnobSlider& slider, const juce::String& paramID)
{
    if (auto* param = processor.apvts.getParameter (paramID))
    {
        const auto defaultValue = param->convertFrom0to1 (param->getDefaultValue());

        slider.setDoubleClickReturnValue (true, defaultValue);
        slider.onRightClick = [param]
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->getDefaultValue());
            param->endChangeGesture();
        };
    }
}

void ContentComponent::setupKnob (KnobSlider& slider, juce::Label& label,
                                  const juce::String& text, const juce::String& paramID)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 16);
    slider.setRepaintsOnMouseActivity (true);
    addAndMakeVisible (slider);

    attachKnobDefaults (slider, paramID);

    // faceplate-style legend: small caps, letter-spaced
    label.setText (text.toUpperCase(), juce::dontSendNotification);
    label.setFont (SideDuckLookAndFeel::legendFont (11.0f));
    label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (label);
}

void ContentComponent::applyTooltips()
{
    powerButton.setTooltip ("Enables or bypasses the effect with a click-free gain ramp. Automatable from the host.");
    scaleBox.setTooltip ("Resizes the interface. The choice is saved with the project.");
    themeBox.setTooltip ("Colour theme. A workstation preference shared by every SideDuck instance; presets never change it.");

    presetBox.setTooltip ("Loads a preset from your library. \"Init (Default)\" restores the factory state.");
    savePresetButton.setTooltip ("Saves the current settings, including the custom curve, to your preset library.");
    importPresetButton.setTooltip ("Adds a preset file to your library and loads it.");
    exportPresetButton.setTooltip ("Saves the current settings to a file you can share.");

    slotAButton.setTooltip ("Compare slot A. Switching slots stores the current sound in the slot you leave.");
    slotBButton.setTooltip ("Compare slot B. Switching slots stores the current sound in the slot you leave.");
    copySlotButton.setTooltip ("Copies the current sound into the other compare slot.");

    waveBox.setTooltip ("The volume curve applied every cycle. Click the graph to convert the selected shape into editable points.");
    syncButton.setTooltip ("Locks the LFO to the host tempo and timeline. When off, the Rate knob sets the speed in Hz.");
    rateSyncBox.setTooltip ("Cycle length in musical divisions, from 8 bars down to 1/32.");
    scModeBox.setTooltip ("External sidechain: Duck follows the sidechain signal's envelope; Trigger restarts the shape on each transient. Route audio into the plugin's sidechain input first.");
    midiButton.setTooltip ("Restarts the shape as a one-shot on every MIDI note the plugin receives. Route MIDI to the plugin in your host first.");

    snapBox.setTooltip ("Snaps curve points to a musical grid while editing. Hold Shift to bypass temporarily.");
    undoButton.setTooltip ("Undoes the last curve edit (Ctrl+Z).");
    redoButton.setTooltip ("Redoes an undone curve edit (Ctrl+Y).");

    rateHzSlider.setTooltip ("LFO speed in Hz, used when tempo sync is off. Up to 50 Hz for tremolo effects.");
    depthSlider.setTooltip ("How far the volume dips. At 100% the lowest point of the curve is silence.");
    phaseSlider.setTooltip ("Shifts the curve's start point within the cycle, moving the dip earlier or later relative to the beat.");
    smoothSlider.setTooltip ("Smooths gain changes to remove clicks from hard edges like Square or steep custom curves.");
    punchSlider.setTooltip ("Bends the curve with one control: above 1x the duck digs deeper and recovers late; below 1x it recovers early and gently.");
    stereoSlider.setTooltip ("Offsets the right channel's curve phase, sweeping the pump across the stereo field. 180 degrees alternates left and right.");
    outputSlider.setTooltip ("Output trim in dB, applied after the ducking.");
    lookaheadSlider.setTooltip ("Delays the audio so detection can act before a transient lands, preventing clicked kick attacks. Reported to the host as latency and compensated automatically.");
    senseSlider.setTooltip ("Detection threshold for sidechain Trigger mode. Lower values fire on quieter transients; re-arms 6 dB below the threshold.");

    lfo2TargetBox.setTooltip ("Second LFO destination: sweep a low-pass filter or auto-pan the signal. Off disables LFO 2 entirely.");
    lfo2WaveBox.setTooltip ("LFO 2 shape. Custom shares the curve drawn in the main editor.");
    lfo2SyncButton.setTooltip ("Locks LFO 2 to the host tempo. When off, its Rate knob sets the speed in Hz.");
    lfo2DivBox.setTooltip ("LFO 2 cycle length in musical divisions.");
    lfo2RateSlider.setTooltip ("LFO 2 speed in Hz, used when its tempo sync is off.");
    lfo2DepthSlider.setTooltip ("LFO 2 amount: how far the filter sweeps down, or how wide the auto-pan swings.");
}

void ContentComponent::timerCallback()
{
    const bool sync  = processor.apvts.getRawParameterValue ("sync")->load() > 0.5f;
    const bool power = processor.apvts.getRawParameterValue ("power")->load() > 0.5f;

    rateSyncBox.setEnabled (sync);
    rateSyncBox.setAlpha (sync ? 1.0f : 0.4f);
    rateHzSlider.setEnabled (! sync);
    rateHzSlider.setAlpha (sync ? 0.4f : 1.0f);

    powerButton.setButtonText (power ? "ON" : "OFF");

    undoButton.setEnabled (processor.canUndoCurve());
    redoButton.setEnabled (processor.canRedoCurve());
    undoButton.setAlpha (undoButton.isEnabled() ? 1.0f : 0.45f);
    redoButton.setAlpha (redoButton.isEnabled() ? 1.0f : 0.45f);

    // Sense only matters in sidechain Trigger mode
    const bool trigger = (int) processor.apvts.getRawParameterValue ("scMode")->load() == 2;
    senseSlider.setEnabled (trigger);
    senseSlider.setAlpha (trigger ? 1.0f : 0.4f);

    // LFO 2 row follows its target and sync switches
    const bool lfo2On   = (int) processor.apvts.getRawParameterValue ("lfo2Target")->load() != 0;
    const bool lfo2Sync = processor.apvts.getRawParameterValue ("lfo2Sync")->load() > 0.5f;

    for (auto* c : std::initializer_list<juce::Component*> { &lfo2WaveBox, &lfo2SyncButton, &lfo2DepthSlider })
    {
        c->setEnabled (lfo2On);
        c->setAlpha (lfo2On ? 1.0f : 0.4f);
    }

    lfo2DivBox.setEnabled (lfo2On && lfo2Sync);
    lfo2DivBox.setAlpha (lfo2On && lfo2Sync ? 1.0f : 0.4f);
    lfo2RateSlider.setEnabled (lfo2On && ! lfo2Sync);
    lfo2RateSlider.setAlpha (lfo2On && ! lfo2Sync ? 1.0f : 0.4f);

    // A/B slot indicator
    const int slot = processor.getActiveSlot();
    slotAButton.setToggleState (slot == 0, juce::dontSendNotification);
    slotBButton.setToggleState (slot == 1, juce::dontSendNotification);
}

void ContentComponent::paint (juce::Graphics& g)
{
    juce::ColourGradient backgroundGradient (theme.backgroundTop, 0.0f, 0.0f,
                                             theme.background, 0.0f, (float) getHeight(), false);
    g.setGradientFill (backgroundGradient);
    g.fillAll();

    // title: accent "SIDE" over full-text draw, same origin so glyphs align
    auto titleFont = SideDuckLookAndFeel::uiFont (24.0f, true).withExtraKerningFactor (0.05f);
    g.setFont (titleFont);
    g.setColour (theme.text);
    g.drawText ("SIDEDUCK", titleBounds, juce::Justification::centredLeft);
    g.setColour (theme.accent);
    g.drawText ("SIDE", titleBounds, juce::Justification::centredLeft);

    // subtitle after the wordmark
    juce::GlyphArrangement measure;
    measure.addLineOfText (titleFont, "SIDEDUCK", 0.0f, 0.0f);
    const int titleWidth = (int) std::ceil (measure.getBoundingBox (0, -1, true).getWidth());

    g.setFont (SideDuckLookAndFeel::uiFont (10.0f, true).withExtraKerningFactor (0.18f));
    g.setColour (theme.dimText);
    g.drawText ("VOLUME SHAPER", titleBounds.withTrimmedLeft (titleWidth + 12).withTrimmedTop (8),
                juce::Justification::centredLeft);

    // soft shadow behind the visualizer
    g.setColour (juce::Colours::black.withAlpha (theme.light ? 0.12f : 0.30f));
    g.fillRoundedRectangle (visualizer.getBounds().toFloat().translated (0.0f, 2.5f).expanded (1.0f), 9.0f);

    // control deck card
    g.setColour (theme.panel.withAlpha (theme.light ? 0.65f : 0.45f));
    g.fillRoundedRectangle (deckBounds.toFloat(), 10.0f);
    g.setColour (theme.panelBorder.withAlpha (0.6f));
    g.drawRoundedRectangle (deckBounds.toFloat(), 10.0f, 1.0f);

    // divider between the modulation knobs and the character/output knobs
    if (! knobRowBounds.isEmpty())
    {
        const float x = (float) knobRowBounds.getX() + (float) knobRowBounds.getWidth() * 4.0f / 9.0f;

        juce::ColourGradient dividerGradient (theme.panelBorder.withAlpha (0.0f), x, (float) knobRowBounds.getY(),
                                              theme.panelBorder.withAlpha (0.0f), x, (float) knobRowBounds.getBottom(), false);
        dividerGradient.addColour (0.5, theme.panelBorder.withAlpha (0.9f));
        g.setGradientFill (dividerGradient);
        g.fillRect (juce::Rectangle<float> (x - 0.5f, (float) knobRowBounds.getY() + 4.0f,
                                            1.0f, (float) knobRowBounds.getHeight() - 8.0f));
    }

    // horizontal separator above the LFO 2 row
    if (! lfo2RowBounds.isEmpty())
    {
        const float y = (float) lfo2RowBounds.getY() - 5.0f;

        juce::ColourGradient separatorGradient (theme.panelBorder.withAlpha (0.0f), (float) lfo2RowBounds.getX(), y,
                                                theme.panelBorder.withAlpha (0.0f), (float) lfo2RowBounds.getRight(), y, false);
        separatorGradient.addColour (0.5, theme.panelBorder.withAlpha (0.9f));
        g.setGradientFill (separatorGradient);
        g.fillRect (juce::Rectangle<float> ((float) lfo2RowBounds.getX() + 4.0f, y - 0.5f,
                                            (float) lfo2RowBounds.getWidth() - 8.0f, 1.0f));
    }
}

void ContentComponent::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto header = area.removeFromTop (32);
    titleBounds = header.removeFromLeft (256);
    themeBox.setBounds (header.removeFromRight (110));
    header.removeFromRight (8);
    scaleBox.setBounds (header.removeFromRight (136));
    header.removeFromRight (8);
    powerButton.setBounds (header.removeFromRight (56));

    area.removeFromTop (8);

    auto presetRow = area.removeFromTop (26);
    presetLabel.setBounds (presetRow.removeFromLeft (56));
    exportPresetButton.setBounds (presetRow.removeFromRight (60));
    presetRow.removeFromRight (6);
    importPresetButton.setBounds (presetRow.removeFromRight (60));
    presetRow.removeFromRight (6);
    savePresetButton.setBounds (presetRow.removeFromRight (54));
    presetRow.removeFromRight (14);
    copySlotButton.setBounds (presetRow.removeFromRight (52));
    presetRow.removeFromRight (6);
    slotBButton.setBounds (presetRow.removeFromRight (26));
    presetRow.removeFromRight (4);
    slotAButton.setBounds (presetRow.removeFromRight (26));
    presetRow.removeFromRight (12);
    presetBox.setBounds (presetRow);

    area.removeFromTop (8);
    visualizer.setBounds (area.removeFromTop (236));
    area.removeFromTop (4);

    auto subRow = area.removeFromTop (22);
    snapLabel.setBounds (subRow.removeFromLeft (36));
    snapBox.setBounds (subRow.removeFromLeft (72));
    subRow.removeFromLeft (10);
    undoButton.setBounds (subRow.removeFromLeft (54));
    subRow.removeFromLeft (4);
    redoButton.setBounds (subRow.removeFromLeft (54));
    hintLabel.setBounds (subRow);

    area.removeFromTop (8);
    deckBounds = area;

    auto inner = area.reduced (12, 10);

    // selector row: shape + sync toggle + division + sidechain mode + midi
    auto selectorRow = inner.removeFromTop (26);
    waveLabel.setBounds (selectorRow.removeFromLeft (48));
    waveBox.setBounds (selectorRow.removeFromLeft (104));
    selectorRow.removeFromLeft (18);
    syncButton.setBounds (selectorRow.removeFromLeft (82));
    selectorRow.removeFromLeft (6);
    rateSyncBox.setBounds (selectorRow.removeFromLeft (88));
    selectorRow.removeFromLeft (18);
    scLabel.setBounds (selectorRow.removeFromLeft (76));
    scModeBox.setBounds (selectorRow.removeFromLeft (86));
    selectorRow.removeFromLeft (18);
    midiButton.setBounds (selectorRow.removeFromLeft (76));

    inner.removeFromTop (8);

    // LFO 2 row pinned to the bottom of the deck
    auto lfo2Row = inner.removeFromBottom (52);
    lfo2RowBounds = lfo2Row;
    inner.removeFromBottom (10);

    lfo2Label.setBounds (lfo2Row.removeFromLeft (48));
    lfo2TargetBox.setBounds (lfo2Row.removeFromLeft (82).withSizeKeepingCentre (82, 26));
    lfo2Row.removeFromLeft (10);
    lfo2WaveBox.setBounds (lfo2Row.removeFromLeft (100).withSizeKeepingCentre (100, 26));
    lfo2Row.removeFromLeft (16);
    lfo2SyncButton.setBounds (lfo2Row.removeFromLeft (82).withSizeKeepingCentre (82, 26));
    lfo2Row.removeFromLeft (6);
    lfo2DivBox.setBounds (lfo2Row.removeFromLeft (86).withSizeKeepingCentre (86, 26));
    lfo2Row.removeFromLeft (18);
    lfo2RateLabel.setBounds (lfo2Row.removeFromLeft (38));
    lfo2RateSlider.setBounds (lfo2Row.removeFromLeft (112));
    lfo2Row.removeFromLeft (14);
    lfo2DepthLabel.setBounds (lfo2Row.removeFromLeft (48));
    lfo2DepthSlider.setBounds (lfo2Row.removeFromLeft (112));

    // single row of nine knobs, mixer-channel style
    knobRowBounds = inner;

    auto placeKnob = [] (juce::Rectangle<int>& row, int width, juce::Slider& s, juce::Label& l)
    {
        auto cell = row.removeFromLeft (width);
        l.setBounds (cell.removeFromTop (14));
        s.setBounds (cell.reduced (8, 1));
    };

    const int knobWidth = inner.getWidth() / 9;

    placeKnob (inner, knobWidth, rateHzSlider,    rateLabel);
    placeKnob (inner, knobWidth, depthSlider,     depthLabel);
    placeKnob (inner, knobWidth, phaseSlider,     phaseLabel);
    placeKnob (inner, knobWidth, smoothSlider,    smoothLabel);
    placeKnob (inner, knobWidth, punchSlider,     punchLabel);
    placeKnob (inner, knobWidth, stereoSlider,    stereoLabel);
    placeKnob (inner, knobWidth, outputSlider,    outputLabel);
    placeKnob (inner, knobWidth, lookaheadSlider, lookaheadLabel);
    placeKnob (inner, knobWidth, senseSlider,     senseLabel);
}

//==============================================================================
SideDuckAudioProcessorEditor::SideDuckAudioProcessorEditor (SideDuckAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      content (p,
               [this] (int percent) { applyScale (percent); },
               [this] (int themeIndex) { applyTheme (themeIndex); })
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (content);
    content.setBounds (0, 0, ContentComponent::baseWidth, ContentComponent::baseHeight);

    // Read the saved scale before installing the resize limits: clamping the
    // initial 0x0 bounds fires resized(), which writes uiScale back into the
    // state and would clobber the saved value.
    const int savedScale = (int) processor.apvts.state.getProperty ("uiScale", 100);

    // free resizing: host window edges/corners and the bottom-right grip both
    // funnel through the constrainer, which keeps the aspect ratio locked
    setResizable (true, true);
    setResizeLimits (ContentComponent::baseWidth / 2,  ContentComponent::baseHeight / 2,
                     ContentComponent::baseWidth * 2,  ContentComponent::baseHeight * 2);

    if (auto* boundsConstrainer = getConstrainer())
        boundsConstrainer->setFixedAspectRatio ((double) ContentComponent::baseWidth
                                                / (double) ContentComponent::baseHeight);

    const int savedTheme = Theme::loadSavedIndex();
    content.setThemeSelection (savedTheme);
    applyTheme (savedTheme);

    applyScale (savedScale);
}

SideDuckAudioProcessorEditor::~SideDuckAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void SideDuckAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) ContentComponent::baseWidth;

    content.setTransform (juce::AffineTransform::scale (scale));

    const int percent = juce::roundToInt (scale * 100.0f);
    processor.apvts.state.setProperty ("uiScale", percent, nullptr);
    content.updateScaleDisplay (percent);
}

void SideDuckAudioProcessorEditor::applyScale (int percent)
{
    percent = juce::jlimit (50, 200, percent);

    setSize (juce::roundToInt (ContentComponent::baseWidth  * (float) percent / 100.0f),
             juce::roundToInt (ContentComponent::baseHeight * (float) percent / 100.0f));
}

void SideDuckAudioProcessorEditor::applyTheme (int themeIndex)
{
    const auto newTheme = Theme::byIndex (themeIndex);

    lookAndFeel.setTheme (newTheme);
    content.setTheme (newTheme);
    Theme::saveIndex (themeIndex);

    sendLookAndFeelChange();
    repaint();
}
