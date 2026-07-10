#pragma once

#include <cmath>

// Stateless LFO shape evaluation.
//
// All shapes are "volume curves": the returned value is in [0, 1] where
// 1 means full volume and 0 means fully ducked. Phase 0 is the start of
// a cycle (the beat), so every shape starts ducked and recovers — the
// classic sidechain pump.
namespace LFO
{
    // Note: new shapes are appended after `custom` so saved projects keep
    // their wave indices.
    enum class Shape
    {
        sine = 0,
        triangle,
        sawUp,   // instant duck on the beat, linear recovery ("pump")
        sawDown, // full volume on the beat, fades out
        square,
        custom,  // user-drawn, evaluated via CustomCurve instead of value()
        expPump, // exponential recovery, like a real compressor release
        pulse,   // short hard duck on the first quarter of the cycle
        steps    // 4-step staircase recovery
    };

    constexpr int numShapes = 9;

    inline float value (Shape shape, float phase) noexcept
    {
        switch (shape)
        {
            case Shape::sine:     return 0.5f - 0.5f * std::cos (6.283185307179586f * phase);
            case Shape::triangle: return 1.0f - std::abs (2.0f * phase - 1.0f);
            case Shape::sawUp:    return phase;
            case Shape::sawDown:  return 1.0f - phase;
            case Shape::square:   return phase < 0.5f ? 0.0f : 1.0f;
            case Shape::custom:   break;
            case Shape::expPump:  return (1.0f - std::exp (-6.0f * phase)) / (1.0f - std::exp (-6.0f));
            case Shape::pulse:    return phase < 0.25f ? 0.0f : 1.0f;
            case Shape::steps:    return std::floor (phase * 4.0f) / 3.0f;
        }

        return 1.0f;
    }
}
