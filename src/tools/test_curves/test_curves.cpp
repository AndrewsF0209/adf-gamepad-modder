// Unit checks for deadzone / curve / digital direction / edge acceleration.
#include <cmath>
#include <cstdio>
#include <string>

#include "adf/curves.h"

using namespace adf;

namespace {

int g_failures = 0;

void Check(bool cond, const std::string& what) {
    std::printf("[%s] %s\n", cond ? "OK  " : "FAIL", what.c_str());
    if (!cond) ++g_failures;
}

bool Near(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) < eps;
}

void TestRadialDeadzone() {
    {
        Vec2 r = ApplyRadialDeadzone({0.1f, 0.0f}, 0.2f, 0.0f);
        Check(Near(r.x, 0.0f) && Near(r.y, 0.0f), "inner deadzone zeroes input");
    }
    {
        Vec2 r = ApplyRadialDeadzone({1.0f, 0.0f}, 0.2f, 0.0f);
        Check(Near(r.x, 1.0f), "full deflection stays at 1.0");
    }
    {
        Vec2 r = ApplyRadialDeadzone({0.6f, 0.0f}, 0.2f, 0.0f);
        Check(Near(r.x, 0.5f), "magnitude rescaled outside deadzone");
    }
    {
        Vec2 r = ApplyRadialDeadzone({0.0f, 1.2f}, 0.2f, 0.0f);
        Check(Near(r.y, 1.0f), "over-deflection clamped to 1.0");
    }
}

void TestCurves() {
    Check(Near(ApplyResponseCurve(0.5f, 0), 0.5f), "linear curve");
    Check(Near(ApplyResponseCurve(0.5f, 1), 0.25f), "quadratic curve");
    Check(Near(ApplyResponseCurve(0.5f, 2), 0.125f), "cubic curve");
}

void TestDigital() {
    struct Case { Vec2 v; bool eight; int expect; };
    bool d[8];

    DigitalDirections({1.0f, 0.0f}, true, d);
    Check(d[2] && !(d[0]), "east vector -> R");

    DigitalDirections({0.0f, 1.0f}, true, d);
    Check(d[0] && !(d[2]), "north vector -> U");

    DigitalDirections({0.7f, 0.7f}, true, d);
    Check(d[1], "northeast vector -> UR");

    DigitalDirections({-0.7f, -0.7f}, true, d);
    Check(d[5], "southwest vector -> DL");

    DigitalDirections({0.99f, 0.05f}, true, d);
    Check(d[2] && !d[1], "near-east vector snaps to R");

    DigitalDirections({0.7f, 0.7f}, false, d);
    int count = 0;
    for (bool x : d) count += x;
    Check(count == 1, "4-way mode yields one direction");
}

void TestEdgeAccel() {
    EdgeAccel a;
    // Below threshold stays at 1x.
    a.Update(0.5f, 0.85f, 3.0f, 0.1f, 0.05f);
    Check(Near(a.multiplier, 1.0f), "no acceleration below threshold");

    // Past threshold ramps up but never beyond target.
    for (int i = 0; i < 20; ++i)
        a.Update(1.0f, 0.85f, 3.0f, 0.1f, 0.05f);
    Check(a.multiplier > 2.9f && a.multiplier <= 3.0f,
          "acceleration ramps to target multiplier");

    // Release relaxes back.
    for (int i = 0; i < 20; ++i)
        a.Update(0.0f, 0.85f, 3.0f, 0.1f, 0.05f);
    Check(Near(a.multiplier, 1.0f, 0.05f), "acceleration relaxes after release");
}

} // namespace

int main() {
    TestRadialDeadzone();
    TestCurves();
    TestDigital();
    TestEdgeAccel();
    std::printf(g_failures == 0 ? "\nALL TESTS PASSED\n"
                                : "\n%d TEST(S) FAILED\n",
                g_failures);
    return g_failures ? 1 : 0;
}
