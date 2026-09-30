#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace adf {

inline constexpr int kMaxPadButtons = 32;

// A single output action triggered by a gamepad source.
struct KeyAction {
    enum Type {
        None = 0,       // unbound
        Key = 1,        // keyboard key (PS/2 set-1 scancode)
        MouseButton = 2,// mouse button 1..5
        MouseWheel = 3  // one-shot wheel notch (+120 / -120)
    };

    int type = None;
    int scancode = 0;        // wScan value for SendInput (set-1 make code)
    bool extended = false;   // E0 prefix (arrows, RCtrl, RAlt, Ins/Del/...)
    std::string keyName;     // optional human-readable name
    int button = 1;          // 1=left 2=right 3=middle 4=X1 5=X2
    int wheel = 120;         // WHEEL_DELTA positive/negative

    // Toggle (latch) mode: each tap flips the output between held/released
    // instead of mirroring the physical button. A rising edge on any of the
    // listed gamepad button ordinals (SDL_GamepadButton 0..31) force-releases
    // a latched output ("interruptors").
    bool toggle = false;
    std::vector<int> interruptButtons;

    // Key combination ("chord"): extra actions output together with the
    // primary one, e.g. Ctrl+C. Press order on activation is primary first
    // then chord[0..n]; release is the exact reverse. Entries are flat
    // (their own chord field is ignored).
    std::vector<KeyAction> chord;

    // Gesture actions (all optional, flat actions). Stored as Part (a class
    // cannot contain direct members of its own incomplete type):
    //   tap       quick press released before longMs (momentary pulse)
    //   doubleTap second press within doubleMs of a quick release (pulse)
    //   longPress held past longMs (held until the pad button is released)
    // When any gesture is configured the primary ("hold") action is delayed
    // until the gesture outcome is known.
    struct Part {
        KeyAction::Type type = None;
        int scancode = 0;
        bool extended = false;
        int button = 1;   // mouse button 1..5
        int wheel = 120;  // +/- 120
    };
    Part tap;
    Part doubleTap;
    Part longPress;
    int longMs = 300;
    int doubleMs = 260;

    // Radial ("wheel") menu: hold the pad button past radialMs, tilt the
    // chosen stick toward one of 8 sectors, release to fire that sector's
    // action as a momentary pulse; releasing with the stick centered
    // cancels. A quick tap (released before radialMs) fires the primary
    // action as a pulse instead. Sectors are flat (no chord/gesture).
    // Mutually exclusive with gestures/toggle: radial wins when enabled
    // and at least one sector is bound.
    bool radial = false;
    std::array<Part, 8> radialDirs; // U, UR, R, DR, D, DL, L, UL
    int radialMs = 300;             // hold threshold before the menu engages
    int radialStick = 1;            // 0 = left stick, 1 = right stick
    // 0 = fire on wheel-button release ("关闭触发"), 1 = fire when the
    // selection stick returns to center ("回中触发"), 2 = hold the sector
    // key down like a real keyboard key while the stick holds the sector
    // ("选择触发", released on re-center / sweep / button release).
    int radialTrigger = 0;

    KeyAction() = default;
    KeyAction(const Part& p)
        : type(p.type), scancode(p.scancode), extended(p.extended),
          button(p.button), wheel(p.wheel) {}

    bool operator==(const KeyAction& o) const;
};

// Analog stick used as a digital 8/4-direction hat.
struct StickKeys {
    bool enabled = false;
    float deadzone = 0.20f;
    bool eightWay = true;
    // direction order: U, UR, R, DR, D, DL, L, UL
    std::array<KeyAction, 8> dirs;
};

// Analog stick driving a relative mouse.
struct StickMouse {
    bool enabled = false;
    float deadzone = 0.15f;       // inner radial deadzone (0..1)
    float outerDeadzone = 0.0f;   // ignored outer ring fraction (0..1)
    float baseSpeed = 800.0f;     // pixels / second at full deflection
    int curve = 1;                // 0=linear 1=quadratic 2=cubic
    float edgeThreshold = 0.85f;  // magnitude where edge acceleration starts
    float edgeMultiplier = 2.5f;  // max speed multiplier
    int accelTimeMs = 150;        // ramp time toward the multiplier
};

struct Stick {
    int mode = 0;   // 0 = digital keys, 1 = relative mouse
    StickKeys keys;
    StickMouse mouse;

    // Hold-to-modify ("摇杆修饰键"): while the chosen pad control is
    // physically held the stick runs on `alt` scalars — e.g. a temporary
    // sensitivity swap or a switch to 8-way keys (FPS 快慢机). alt.mode < 0
    // keeps the current mode; direction bindings always come from `keys`.
    // -1 = none; 0..31 = pad button; 100 = LT; 101 = RT.
    int mod = -1;
    bool modOverride = false;
    struct Alt {
        int mode = -1;          // -1 = keep current mode
        float keysDeadzone = 0.20f;
        bool eightWay = true;
        StickMouse mouse;
    } alt;
};

struct Trigger {
    bool enabled = false;
    float deadzone = 0.05f;
    float threshold = 0.5f;
    KeyAction action;
};

// A switchable layer: while its trigger is active, button bindings resolve
// from `buttons` first (a slot absent from the map falls back to the base
// bindings; an explicitly unbound slot stays unbound) and each overridden
// stick uses the layer's config — the classic FPS aim-down-sights
// sensitivity swap. A button/trigger activator keeps its normal binding, so
// LT can e.g. hold right-mouse while its layer is live.
struct LayerConfig {
    // -1 = no trigger; 0..15 = pad button; 100 = LT; 101 = RT;
    // 102 = mouse-lock (game captured the cursor).
    int hold = -1;
    bool autoMouseLock = false; // legacy JSON only, migrated to hold == 102
    std::unordered_map<int, KeyAction> buttons;
    bool rightOverride = false;
    Stick right;
    bool leftOverride = false;
    Stick left;
};

struct DeviceConfig {
    std::string guid;   // empty matches any controller
    std::string name;
    // key = SDL_GamepadButton ordinal (0..31); covers face buttons,
    // shoulders, sticks clicks, Start/Back/Guide and the D-Pad.
    std::unordered_map<int, KeyAction> buttons;
    Stick leftStick;    // defaults to digital keys
    Stick rightStick;   // defaults to relative mouse
    Trigger leftTrigger;
    Trigger rightTrigger;
    bool swapSticks = false;
    // Optional layers with per-layer triggers; the lowest-numbered ACTIVE
    // layer wins. The UI shows the base bindings as "层 0" (not a layer).
    static constexpr int kMaxLayers = 3;
    std::array<LayerConfig, kMaxLayers> layers;

    DeviceConfig();
};

struct Config {
    int version = 1;
    std::string gameExe;      // used by the fallback launcher
    std::string gameArgs;
    int pollRateHz = 1000;
    bool foregroundOnly = true;
    std::vector<DeviceConfig> devices;

    bool Load(const std::wstring& path, std::string& err);
    bool Save(const std::wstring& path, std::string& err) const;
};

// Resolve the configuration to use for a connected pad: exact GUID match
// first, then the first device entry with an empty GUID.
const DeviceConfig* MatchDevice(const Config& cfg, const std::string& guid);

// A reasonable default profile (WASD + mouse + common bindings).
DeviceConfig MakeDefaultDevice();

} // namespace adf
