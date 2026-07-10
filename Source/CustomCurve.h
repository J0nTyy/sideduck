#pragma once

#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>

// The user's custom LFO volume curve, baked from the breakpoint model into a
// fixed-resolution table.
//
// The audio thread reads while the UI thread rebakes after edits; per-element
// atomics keep this race benign (a torn update across neighbouring steps is
// inaudible).
class CustomCurve
{
public:
    static constexpr int resolution = 128;

    CustomCurve()
    {
        // default to a saw-up pump so "Custom" is never silent-flat
        for (int i = 0; i < resolution; ++i)
            points[(size_t) i].store ((float) i / (float) resolution);
    }

    float valueAt (float phase) const noexcept
    {
        phase -= std::floor (phase);

        const float fpos = phase * (float) resolution;
        const int   i0   = std::min ((int) fpos, resolution - 1);
        const int   i1   = (i0 + 1) % resolution;
        const float frac = fpos - (float) i0;

        return points[(size_t) i0].load() * (1.0f - frac)
             + points[(size_t) i1].load() * frac;
    }

    std::array<std::atomic<float>, resolution> points;
};
