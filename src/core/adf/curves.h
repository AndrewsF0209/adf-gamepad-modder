#pragma once

namespace adf {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

float Clampf(float v, float lo, float hi);

// Radial inner/outer deadzone. Output magnitude is rescaled so the output
// starts at 0 right outside the inner zone and reaches 1 at the outer limit.
Vec2 ApplyRadialDeadzone(Vec2 v, float inner, float outer);

// Response curve applied to a 0..1 magnitude.
// type: 0 linear, 1 quadratic, 2 cubic.
float ApplyResponseCurve(float x, int type);

// Given a post-deadzone stick vector, fill 8 direction flags.
// dirs order: U, UR, R, DR, D, DL, L, UL.
// If eightWay is false only cardinal directions are ever active.
void DigitalDirections(Vec2 v, bool eightWay, bool out[8]);

// Time-smoothed edge acceleration multiplier.
struct EdgeAccel {
    float multiplier = 1.0f;
    bool wasOver = false;

    // dt in seconds; returns the new multiplier.
    float Update(float magnitude, float threshold, float maxMultiplier,
                 float accelTimeSec, float dt);
    void Reset() { multiplier = 1.0f; wasOver = false; }
};

} // namespace adf
