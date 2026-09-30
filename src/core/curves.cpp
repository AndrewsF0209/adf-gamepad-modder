#include "adf/curves.h"

#include <algorithm>
#include <cmath>

namespace adf {

float Clampf(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

Vec2 ApplyRadialDeadzone(Vec2 v, float inner, float outer) {
    const float mag = std::sqrt(v.x * v.x + v.y * v.y);
    if (mag <= inner || mag <= 0.00001f) return {0.0f, 0.0f};

    const float span = 1.0f - inner - Clampf(outer, 0.0f, 0.9f);
    float f = (mag - inner) / std::max(span, 0.0001f);
    f = Clampf(f, 0.0f, 1.0f);

    const float inv = 1.0f / mag;
    return {v.x * inv * f, v.y * inv * f};
}

float ApplyResponseCurve(float x, int type) {
    x = Clampf(x, 0.0f, 1.0f);
    switch (type) {
        case 1: return x * x;
        case 2: return x * x * x;
        default: return x;
    }
}

void DigitalDirections(Vec2 v, bool eightWay, bool out[8]) {
    for (int i = 0; i < 8; ++i) out[i] = false;

    const float mag = std::sqrt(v.x * v.x + v.y * v.y);
    if (mag < 0.00001f) return;

    // U, UR, R, DR, D, DL, L, UL
    static const int kOctantToDir[8] = {2, 1, 0, 7, 6, 5, 4, 3};

    if (!eightWay) {
        if (std::fabs(v.x) >= std::fabs(v.y))
            out[v.x >= 0.0f ? 2 : 6] = true;
        else
            out[v.y >= 0.0f ? 0 : 4] = true;
        return;
    }

    // Angle measured from +X axis, counter-clockwise, centered octants at
    // 0(=R), 45(=UR), 90(=U), ...
    float deg = std::atan2(v.y, v.x) * 57.2957795f;
    if (deg < 0.0f) deg += 360.0f;
    int oct = static_cast<int>(std::floor((deg + 22.5f) / 45.0f)) & 7;
    out[kOctantToDir[oct]] = true;
}

float EdgeAccel::Update(float magnitude, float threshold, float maxMultiplier,
                        float accelTimeSec, float dt) {
    const bool over = magnitude >= threshold;
    const float target = over ? std::max(1.0f, maxMultiplier) : 1.0f;

    if (accelTimeSec <= 0.0001f) {
        multiplier = target;
    } else {
        float k = dt / accelTimeSec;
        if (k > 1.0f) k = 1.0f;
        multiplier += (target - multiplier) * k;
    }
    wasOver = over;
    return multiplier;
}

} // namespace adf
