#include "adf/mapper.h"

#include <windows.h>
#include <timeapi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#include "adf/curves.h"
#include "adf/pad.h"
#include "adf/sink_kbdmouse.h"

#pragma comment(lib, "winmm.lib")

namespace adf {
namespace {

// Slot layout:
//   [0,32)   gamepad buttons (SDL_GamepadButton ordinals)
//   [32,40)  left stick digital directions (U,UR,R,DR,D,DL,L,UL)
//   [40,48)  right stick digital directions
//   48,49    left/right trigger
constexpr int kButtonSlots = kMaxPadButtons;
constexpr int kLeftDirBase = kButtonSlots;
constexpr int kRightDirBase = kButtonSlots + 8;
constexpr int kLeftTriggerSlot = kButtonSlots + 16;
constexpr int kRightTriggerSlot = kButtonSlots + 17;
constexpr int kSlotCount = kButtonSlots + 18;

enum GestureState {
    kGIdle = 0,       // nothing pressed / no gesture pending
    kGPending = 1,    // button down, hold/tap outcome not decided yet
    kGActive = 2,     // hold or long-press output is currently held
    kGWaitDouble = 3, // quick release, waiting for a possible second tap
    kGDoubleHeld = 4 // second tap fired the double action; button still down
};

enum RadialState {
    kRIdle = 0, // no radial interaction
    kRHeld = 1, // button down, hold threshold not reached yet
    kRMenu = 2  // menu engaged: stick tilt picks a sector, release fires it
};

struct Slot {
    bool active = false;   // output held via the non-gesture (mirror/toggle) path
    bool phys = false;     // physical source state during the previous tick
    KeyAction action;

    // gesture state machine
    int g = kGIdle;
    bool gHeld = false;          // hold/long output currently pressed
    KeyAction gHeldAction;
    bool pulsing = false;        // momentary tap/double output
    KeyAction pulseAction;
    std::chrono::steady_clock::time_point tDown;
    std::chrono::steady_clock::time_point tPulse;
    std::chrono::steady_clock::time_point tDoubleUntil;

    // radial menu state machine
    int r = kRIdle;
    int rSector = -1; // highlighted sector while r == kRMenu (-1 = cancel)
    // 选择触发 (radialTrigger == 2) hold-style output: the sector key stays
    // pressed like a real keyboard key for as long as the stick holds it.
    bool rHeldActive = false;
    KeyAction rHeldAction;
};

bool AnyRadialBound(const KeyAction& a) {
    for (const KeyAction::Part& p : a.radialDirs)
        if (p.type != KeyAction::None) return true;
    return false;
}

// Heuristic for "the game has captured the mouse". The proxy DLL lives
// inside the game process, so these queries reflect the game's own doing:
// shooters either ClipCursor to their window (or even a point) or hide the
// cursor and re-center it every frame, while menus free the cursor again.
// Multi-monitor caveat: a clip confined to one monitor also counts.
bool QueryMouseLocked() {
    RECT clip{};
    if (GetClipCursor(&clip)) {
        const int sw = GetSystemMetrics(SM_CXSCREEN);
        const int sh = GetSystemMetrics(SM_CYSCREEN);
        if (clip.right - clip.left < sw - 8 || clip.bottom - clip.top < sh - 8)
            return true;
    }
    CURSORINFO ci{sizeof(ci)};
    if (GetCursorInfo(&ci) && !(ci.flags & CURSOR_SHOWING)) return true;
    return false;
}

// Sector index for a raw stick vector: 0 U, 1 UR, ... 7 UL (clockwise from
// up, matching the digital-stick direction order); -1 when centered.
int RadialSector(Vec2 v) {
    const float mag = std::sqrt(v.x * v.x + v.y * v.y);
    if (mag < 0.5f) return -1;
    constexpr float kPi = 3.14159265358979f;
    // Engine stick vectors are math-convention: up = +y (pad_sdl.cpp
    // negates SDL's down-positive axis), so up must yield angle 0 here.
    float deg = std::atan2(v.x, v.y) * 180.0f / kPi; // 0 = up, clockwise
    if (deg < 0.0f) deg += 360.0f;
    return static_cast<int>((deg + 22.5f) / 45.0f) % 8;
}

bool IsForegroundPid(unsigned long pid) {
    DWORD owner = 0;
    HWND hw = GetForegroundWindow();
    if (hw) GetWindowThreadProcessId(hw, &owner);
    return owner == pid;
}

class Engine {
public:
    Engine(const Config& cfg, unsigned long fgPid,
           const std::function<bool()>& alive)
        : cfg_(cfg), fgPid_(fgPid), alive_(alive) {
        slots_.resize(kSlotCount);
    }

    int Run(std::string& err) {
        if (!pad_.Init(err)) return 1;

        const int hz = std::clamp(cfg_.pollRateHz, 60, 4000);
        const auto interval = std::chrono::microseconds(1000000 / hz);

        timeBeginPeriod(1);

        bool suspended = false;
        auto next = std::chrono::steady_clock::now();
        auto last = next;

        while (!alive_ || alive_()) {
            next += interval;
            const auto now = std::chrono::steady_clock::now();
            float dt = std::chrono::duration<float>(now - last).count();
            last = now;
            if (dt > 0.1f) dt = 0.1f;

            pad_.Pump();

            PadState ps;
            std::string guid;
            const bool havePad = pad_.Snapshot("", ps, &guid);
            const bool fg = !cfg_.foregroundOnly || IsForegroundPid(fgPid_);

            if (!havePad || !fg) {
                if (!suspended) {
                    ReleaseAll();
                    rightAccel_.Reset();
                    accX_ = accY_ = 0.0f;
                    suspended = true;
                }
                std::this_thread::sleep_until(next);
                continue;
            }
            const bool prime = suspended; // first tick after a suspend gap
            suspended = false;

            const DeviceConfig* dev = MatchDevice(cfg_, guid);
            if (dev) Tick(dt, ps, *dev, prime);

            std::this_thread::sleep_until(next);
        }

        ReleaseAll();
        timeEndPeriod(1);
        pad_.Shutdown();
        return 0;
    }

private:
    const Config& cfg_;
    unsigned long fgPid_;
    const std::function<bool()>& alive_;
    PadSource pad_;
    KbdMouseSink sink_;
    std::vector<Slot> slots_;
    EdgeAccel rightAccel_;
    float accX_ = 0.0f, accY_ = 0.0f;
    bool prevModL_ = false, prevModR_ = false;
    bool prevButtons_[kButtonSlots] = {};
    bool priming_ = false; // suppress toggle/interrupt edges for one tick
    int layerIdx_ = -1;   // active layer from the previous tick (-1 = base)
    bool autoLock_ = false;         // cached mouse-lock detection
    std::chrono::steady_clock::time_point lockCheckAt_{}; // last 10 Hz poll

    void Press(const KeyAction& a) {
        PressOne(a);
        // Combo ("chord"): primary is first in capture order, extras follow.
        for (const KeyAction& c : a.chord) PressOne(c);
    }

    void Release(const KeyAction& a) {
        // Release in exact reverse of press order.
        for (auto it = a.chord.rbegin(); it != a.chord.rend(); ++it)
            ReleaseOne(*it);
        ReleaseOne(a);
    }

    void PressOne(const KeyAction& a) {
        switch (a.type) {
            case KeyAction::Key:
                sink_.Key(a.scancode, a.extended, true);
                break;
            case KeyAction::MouseButton:
                sink_.MouseButton(a.button, true);
                break;
            case KeyAction::MouseWheel:
                sink_.Wheel(a.wheel);   // one-shot, nothing to release
                break;
            default: break;
        }
    }

    void ReleaseOne(const KeyAction& a) {
        switch (a.type) {
            case KeyAction::Key:
                sink_.Key(a.scancode, a.extended, false);
                break;
            case KeyAction::MouseButton:
                sink_.MouseButton(a.button, false);
                break;
            default: break; // wheel / none have no release
        }
    }

    static constexpr auto kPulseDur = std::chrono::milliseconds(20);

    void ReleaseGestureOutputs(Slot& s) {
        if (s.gHeld) {
            Release(s.gHeldAction);
            s.gHeld = false;
        }
        if (s.pulsing) {
            Release(s.pulseAction);
            s.pulsing = false;
        }
        s.g = kGIdle;
    }

    // Momentary tap/double action: press now, auto-release a few ticks later
    // so old games that poll the keyboard still see the key as held.
    void StartPulse(Slot& s, const KeyAction& a,
                    std::chrono::steady_clock::time_point now) {
        if (a.type == KeyAction::None) return;
        if (s.pulsing) {
            Release(s.pulseAction);
            s.pulsing = false;
        }
        Press(a);
        s.pulseAction = a;
        s.tPulse = now;
        s.pulsing = true;
    }

    void ApplySlot(int idx, bool on, const KeyAction& desired, Vec2 sel) {
        if (idx < 0 || idx >= kSlotCount) return;
        Slot& s = slots_[idx];
        const auto now = std::chrono::steady_clock::now();
        const bool bound = desired.type != KeyAction::None;
        // Wheel notches are one-shot events: there is nothing to hold, so a
        // toggle flag on a wheel binding behaves like a normal bind.
        const bool toggling = bound && desired.toggle &&
                              desired.type != KeyAction::MouseWheel;
        const bool hasTap = desired.tap.type != KeyAction::None;
        const bool hasDouble = desired.doubleTap.type != KeyAction::None;
        const bool hasLong = desired.longPress.type != KeyAction::None;
        const bool gesture = bound && (hasTap || hasDouble || hasLong);
        // Radial menu: only meaningful on real pad buttons (a selection
        // stick is required). Takes priority over gestures and toggle.
        const bool rad = idx < kButtonSlots && desired.radial &&
                         AnyRadialBound(desired);

        // Expire a momentary pulse regardless of state.
        if (s.pulsing && now - s.tPulse >= kPulseDur) {
            Release(s.pulseAction);
            s.pulsing = false;
        }

        // Binding changed underneath any active output: drop the old ones.
        const bool sameBinding = s.action == desired;
        if (!sameBinding) {
            if (s.active) Release(s.action);
            ReleaseGestureOutputs(s);
            if (s.rHeldActive) {
                Release(s.rHeldAction);
                s.rHeldActive = false;
            }
            s.active = false;
            s.r = kRIdle;
            s.rSector = -1;
        }

        if (priming_) {
            // Resume tick after a suspend gap: adopt the physical baseline,
            // never treat "still held" as a fresh press or a second tap.
            if (s.active) Release(s.action);
            ReleaseGestureOutputs(s);
            if (s.rHeldActive) {
                Release(s.rHeldAction);
                s.rHeldActive = false;
            }
            s.active = false;
            s.r = kRIdle;
            s.rSector = -1;
            s.phys = on;
            if (on && bound) {
                // Mirror a still-held button on return, no gesture timing.
                if (!toggling && !rad) {
                    Press(desired);
                    s.action = desired;
                    s.active = true;
                }
            }
            return;
        }

        if (rad) {
            s.action = desired;
            if (on && !s.phys) {
                // Press edge: wait out the hold threshold before deciding.
                s.tDown = now;
                s.r = kRHeld;
                s.rSector = -1;
            } else if (on) {
                if (s.r == kRHeld &&
                    now - s.tDown >= std::chrono::milliseconds(desired.radialMs)) {
                    s.r = kRMenu;
                    s.rSector = -1;
                }
                if (s.r == kRMenu) {
                    const int sec = RadialSector(sel);
                    if (desired.radialTrigger == 2) {
                        // 选择触发(按住式): the entered sector's key goes
                        // DOWN and stays down while the stick holds it —
                        // centering releases, sweeping to another sector
                        // releases the old key and presses the new one.
                        const bool valid =
                            sec >= 0 &&
                            desired.radialDirs[sec].type != KeyAction::None;
                        if (s.rHeldActive && (!valid || sec != s.rSector)) {
                            Release(s.rHeldAction);
                            s.rHeldActive = false;
                        }
                        if (valid && !s.rHeldActive) {
                            s.rHeldAction = KeyAction(desired.radialDirs[sec]);
                            Press(s.rHeldAction);
                            s.rHeldActive = true;
                        }
                        s.rSector = sec;
                    } else if (desired.radialTrigger == 1) {
                        // 回中触发: fire the tracked selection when the
                        // stick returns to center; clearing rSector after
                        // firing keeps a centered stick from re-firing.
                        if (sec >= 0) {
                            s.rSector = sec;
                        } else if (s.rSector >= 0) {
                            if (desired.radialDirs[s.rSector].type !=
                                KeyAction::None)
                                StartPulse(
                                    s, KeyAction(desired.radialDirs[s.rSector]),
                                    now);
                            s.rSector = -1;
                        }
                    } else {
                        s.rSector = sec;
                    }
                }
            } else if (!on && s.phys) {
                // Release edge: mode 0 fires the highlighted sector, every
                // mode pulses the primary on a quick tap, else cancels. A
                // hold-style sector key always goes up with the button.
                if (s.rHeldActive) {
                    Release(s.rHeldAction);
                    s.rHeldActive = false;
                }
                if (s.r == kRMenu) {
                    if (desired.radialTrigger == 0 && s.rSector >= 0 &&
                        desired.radialDirs[s.rSector].type != KeyAction::None)
                        StartPulse(s, KeyAction(desired.radialDirs[s.rSector]),
                                   now);
                } else if (s.r == kRHeld && bound) {
                    StartPulse(s, desired, now);
                }
                s.r = kRIdle;
                s.rSector = -1;
            }
        } else if (gesture) {
            s.action = desired;
            // Double-tap wait window can expire while the button is up.
            if (s.g == kGWaitDouble && now >= s.tDoubleUntil) {
                StartPulse(s, desired.tap, now); // no-op when unbound
                s.g = kGIdle;
            }

            if (on && !s.phys) {
                // Press edge.
                if (s.g == kGWaitDouble) {
                    StartPulse(s, desired.doubleTap, now);
                    s.g = kGDoubleHeld;
                } else if (s.g == kGIdle) {
                    s.tDown = now;
                    s.g = kGPending;
                }
            } else if (on) {
                // Held: once the long-press threshold passes, either the
                // long action or (if none defined) the primary hold wins.
                if (s.g == kGPending &&
                    now - s.tDown >= std::chrono::milliseconds(desired.longMs)) {
                    KeyAction picked = hasLong
                        ? KeyAction(desired.longPress) : desired;
                    if (picked.type != KeyAction::None) {
                        Press(picked);
                        s.gHeldAction = picked;
                        s.gHeld = true;
                    }
                    s.g = kGActive;
                }
            } else if (!on && s.phys) {
                // Release edge.
                if (s.g == kGPending) {
                    if (hasDouble) {
                        s.g = kGWaitDouble;
                        s.tDoubleUntil =
                            now + std::chrono::milliseconds(desired.doubleMs);
                    } else {
                        StartPulse(s, desired.tap, now);
                        s.g = kGIdle;
                    }
                } else if (s.g == kGActive) {
                    ReleaseGestureOutputs(s); // releases the held long/hold
                } else if (s.g == kGDoubleHeld) {
                    s.g = kGIdle;
                }
            }
        } else if (toggling) {
            // Latch flips on each physical press edge; releasing the pad
            // button does nothing. interruptLatches() force-releases it.
            if (on && !s.phys) {
                if (s.active) {
                    Release(desired);
                    s.active = false;
                } else {
                    Press(desired);
                    s.action = desired;
                    s.active = true;
                }
            }
        } else if (bound) {
            if (on) {
                if (!s.active) {
                    Press(desired);
                    s.action = desired;
                    s.active = true;
                }
            } else if (s.active) {
                Release(s.action);
                s.active = false;
            }
        } else if (s.active) {
            Release(s.action);
            s.active = false;
        }

        s.phys = on;
    }

    // A rising edge on any configured interruptor force-releases every
    // latched (toggle) output naming that button.
    void InterruptLatches(const bool edges[kButtonSlots]) {
        for (Slot& s : slots_) {
            if (!s.active || !s.action.toggle) continue;
            for (int b : s.action.interruptButtons) {
                if (b >= 0 && b < kButtonSlots && edges[b]) {
                    Release(s.action);
                    s.active = false;
                    break;
                }
            }
        }
    }

    void ReleaseAll() {
        for (Slot& s : slots_) {
            if (s.active) {
                Release(s.action);
                s.active = false;
            }
            ReleaseGestureOutputs(s);
            if (s.rHeldActive) {
                Release(s.rHeldAction);
                s.rHeldActive = false;
            }
            s.r = kRIdle;
            s.rSector = -1;
            s.phys = false;
        }
        for (int i = 0; i < kButtonSlots; ++i) prevButtons_[i] = false;
    }

    const KeyAction* ButtonAction(const DeviceConfig& d, int idx,
                                  int layerIdx) const {
        if (layerIdx >= 0) {
            // The layer wins when it mentions the slot at all — an explicit
            // "none" there unbinds the base action for its duration.
            auto it = d.layers[layerIdx].buttons.find(idx);
            if (it != d.layers[layerIdx].buttons.end()) return &it->second;
        }
        auto it = d.buttons.find(idx);
        return it == d.buttons.end() ? nullptr : &it->second;
    }

    void ApplyDigitalStick(const Stick& sc, Vec2 raw, int slotBase) {
        static const KeyAction kNoAction{};
        static const Vec2 kZero{};
        if (!sc.keys.enabled) {
            for (int i = 0; i < 8; ++i)
                ApplySlot(slotBase + i, false, kNoAction, kZero);
            return;
        }
        Vec2 v = ApplyRadialDeadzone(raw, sc.keys.deadzone, 0.0f);
        bool dirs[8];
        DigitalDirections(v, sc.keys.eightWay, dirs);
        // Dir order: 0 U, 1 UR, 2 R, 3 DR, 4 D, 5 DL, 6 L, 7 UL.
        // An unbound diagonal must not interrupt output: fall back to both
        // adjacent cardinals (the classic keyboard-diagonal), so movement
        // stays continuous while sweeping the stick through an empty octant.
        if (sc.keys.eightWay) {
            static const int kDiagAdj[4][2] = {
                {0, 2},   // UR: U + R
                {2, 4},   // DR: R + D
                {4, 6},   // DL: D + L
                {6, 0},   // UL: L + U
            };
            for (int k = 0; k < 4; ++k) {
                const int d = 2 * k + 1; // 1,3,5,7
                if (dirs[d] && sc.keys.dirs[d].type == KeyAction::None) {
                    dirs[kDiagAdj[k][0]] = true;
                    dirs[kDiagAdj[k][1]] = true;
                }
            }
        }
        for (int i = 0; i < 8; ++i)
            ApplySlot(slotBase + i, dirs[i], sc.keys.dirs[i], kZero);
    }

    void ApplyTrigger(const Trigger& t, float value, int slot) {
        static const KeyAction kNoAction{};
        static const Vec2 kZero{};
        if (!t.enabled || t.action.type == KeyAction::None) {
            ApplySlot(slot, false, kNoAction, kZero);
            return;
        }
        bool on = false;
        if (value > t.deadzone) {
            const float v = (value - t.deadzone) /
                            std::max(0.0001f, 1.0f - t.deadzone);
            on = v >= t.threshold;
        }
        ApplySlot(slot, on, t.action, kZero);
    }

    void ApplyMouseStick(const StickMouse& m, Vec2 raw, float dt) {
        const float rawMag = std::sqrt(raw.x * raw.x + raw.y * raw.y);
        Vec2 v = ApplyRadialDeadzone(raw, m.deadzone, m.outerDeadzone);
        const float mag = std::sqrt(v.x * v.x + v.y * v.y);
        if (mag <= 0.00001f || !m.enabled) {
            rightAccel_.Update(rawMag, m.edgeThreshold, m.edgeMultiplier,
                               m.accelTimeMs * 0.001f, dt);
            accX_ = accY_ = 0.0f;
            return;
        }

        const float mult = rightAccel_.Update(
            rawMag, m.edgeThreshold, m.edgeMultiplier,
            m.accelTimeMs * 0.001f, dt);

        const float speed = m.baseSpeed * ApplyResponseCurve(mag, m.curve) * mult;
        const float inv = 1.0f / mag;

        accX_ += v.x * inv * speed * dt;
        accY_ += -v.y * inv * speed * dt; // screen Y grows downward

        int ix = static_cast<int>(accX_);
        int iy = static_cast<int>(accY_);
        accX_ -= static_cast<float>(ix);
        accY_ -= static_cast<float>(iy);
        if (ix != 0 || iy != 0) sink_.MouseMove(ix, iy);
    }

    void Tick(float dt, const PadState& ps, const DeviceConfig& dev, bool prime) {
        priming_ = prime;

        // Layer stack: every layer has its own trigger (pad button, LT/RT,
        // or mouse-lock); the lowest-numbered ACTIVE layer wins. On a layer
        // switch the mouse accumulators reset so a sensitivity jump never
        // surfaces as a cursor spike; changed button bindings drop their old
        // outputs through the sameBinding check in ApplySlot.
        bool anyAuto = false;
        for (const auto& l : dev.layers)
            if (l.hold == 102) anyAuto = true;
        // Mouse-lock polls are cheap but still throttled to 10 Hz.
        const auto nowClk = std::chrono::steady_clock::now();
        if (!anyAuto) {
            autoLock_ = false;
        } else if (nowClk - lockCheckAt_ >= std::chrono::milliseconds(100)) {
            lockCheckAt_ = nowClk;
            autoLock_ = QueryMouseLocked();
        }
        int layerIdx = -1;
        for (int L = 0; L < DeviceConfig::kMaxLayers; ++L) {
            const LayerConfig& lay = dev.layers[L];
            const bool on =
                lay.hold == 102                      ? autoLock_
                : lay.hold >= 0 && lay.hold < kButtonSlots
                                                   ? ps.buttons[lay.hold]
                : lay.hold == 100                    ? ps.lt > 0.5f
                : lay.hold == 101                    ? ps.rt > 0.5f
                                                     : false;
            if (on) { layerIdx = L; break; }
        }
        if (layerIdx != layerIdx_) {
            layerIdx_ = layerIdx;
            rightAccel_.Reset();
            accX_ = accY_ = 0.0f;
        }
        const LayerConfig* lay =
            layerIdx >= 0 ? &dev.layers[layerIdx] : nullptr;

        // Hold-to-modify stick swap ("摇杆修饰键"): while the pad control is
        // physically held the stick runs on its `alt` scalars. The switch
        // edge resets mouse state so a sensitivity jump never surfaces as a
        // cursor spike; digital-stick slots drop old outputs through the
        // sameBinding check in ApplySlot.
        auto modHeld = [&](int m) {
            return m >= 0 && m < kButtonSlots ? ps.buttons[m]
                 : m == 100                   ? ps.lt > 0.5f
                 : m == 101                   ? ps.rt > 0.5f
                                              : false;
        };
        auto resolveStick = [&](const Stick& in, bool isLeft, Stick& out) {
            out = in;
            const bool m = in.mod >= 0 && in.modOverride && modHeld(in.mod);
            bool& prev = isLeft ? prevModL_ : prevModR_;
            if (m != prev) {
                prev = m;
                rightAccel_.Reset();
                accX_ = accY_ = 0.0f;
            }
            if (!m) return;
            if (in.alt.mode >= 0) out.mode = in.alt.mode;
            out.keys.deadzone = in.alt.keysDeadzone;
            out.keys.eightWay = in.alt.eightWay;
            out.keys.enabled = out.mode == 0;
            out.mouse = in.alt.mouse;
            out.mouse.enabled = out.mode == 1;
        };

        // Rising edges of physical pad buttons (used for toggle latching and
        // for latch interruptors). Suppressed on the resume/prime tick.
        if (!priming_) {
            bool edges[kButtonSlots];
            for (int i = 0; i < kButtonSlots; ++i)
                edges[i] = ps.buttons[i] && !prevButtons_[i];
            InterruptLatches(edges);
        }

        // Sticks (physical side may be swapped). Resolved before the button
        // pass: radial-menu buttons read one of these vectors for sector
        // selection, and an open menu mutes that stick's normal output.
        Vec2 lv{ps.lx, ps.ly}, rv{ps.rx, ps.ry};
        const Stick& physLeft =
            lay && lay->leftOverride ? lay->left : dev.leftStick;
        const Stick& physRight =
            lay && lay->rightOverride ? lay->right : dev.rightStick;
        const Stick& leftCfg = dev.swapSticks ? physRight : physLeft;
        const Stick& rightCfg = dev.swapSticks ? physLeft : physRight;
        Vec2 leftVec = dev.swapSticks ? rv : lv;
        Vec2 rightVec = dev.swapSticks ? lv : rv;
        Stick leftR, rightR;
        resolveStick(leftCfg, true, leftR);
        resolveStick(rightCfg, false, rightR);

        // Buttons (face, shoulders, stick clicks, dpad, system)
        int squelchSticks = 0; // bit0 = left, bit1 = right muted by a menu
        for (int i = 0; i < kButtonSlots; ++i) {
            static const KeyAction kNoAction{};
            const KeyAction* a = ButtonAction(dev, i, layerIdx);
            const Vec2 sel = (a && a->radialStick == 0) ? leftVec : rightVec;
            ApplySlot(i, ps.buttons[i], a ? *a : kNoAction, sel);
            const Slot& s = slots_[i];
            if (s.r == kRMenu)
                squelchSticks |= (s.action.radialStick == 0) ? 1 : 2;
        }

        const Vec2 leftEff = (squelchSticks & 1) ? Vec2{0, 0} : leftVec;
        const Vec2 rightEff = (squelchSticks & 2) ? Vec2{0, 0} : rightVec;

        if (leftR.mode == 0)
            ApplyDigitalStick(leftR, leftEff, kLeftDirBase);
        else
            ApplyMouseStick(leftR.mouse, leftEff, dt);

        if (rightR.mode == 0)
            ApplyDigitalStick(rightR, rightEff, kRightDirBase);
        else
            ApplyMouseStick(rightR.mouse, rightEff, dt);

        ApplyTrigger(dev.leftTrigger, ps.lt, kLeftTriggerSlot);
        ApplyTrigger(dev.rightTrigger, ps.rt, kRightTriggerSlot);

        for (int i = 0; i < kButtonSlots; ++i)
            prevButtons_[i] = ps.buttons[i];
        priming_ = false;
    }
};

} // namespace

int RunMapper(const Config& cfg, unsigned long foregroundPid,
              const std::function<bool()>& alive, std::string& err) {
    Engine engine(cfg, foregroundPid, alive);
    return engine.Run(err);
}

int RunMapperFromFile(const std::wstring& jsonPath, unsigned long foregroundPid,
                      const std::function<bool()>& alive, std::string& err) {
    Config cfg;
    if (!cfg.Load(jsonPath, err)) return 1;
    return RunMapper(cfg, foregroundPid, alive, err);
}

} // namespace adf
