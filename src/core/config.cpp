#include "adf/config.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "adf/scancodes.h"

namespace adf {

using json = nlohmann::json;

namespace {

bool SameFlat(const KeyAction& a, const KeyAction& b) {
    return a.type == b.type && a.scancode == b.scancode &&
           a.extended == b.extended && a.button == b.button && a.wheel == b.wheel;
}

bool SameFlat(const KeyAction::Part& a, const KeyAction::Part& b) {
    return a.type == b.type && a.scancode == b.scancode &&
           a.extended == b.extended && a.button == b.button && a.wheel == b.wheel;
}

KeyAction::Part ToPart(const KeyAction& a) {
    KeyAction::Part p;
    p.type = static_cast<KeyAction::Type>(a.type);
    p.scancode = a.scancode;
    p.extended = a.extended;
    p.button = a.button;
    p.wheel = a.wheel;
    return p;
}

} // namespace

bool KeyAction::operator==(const KeyAction& o) const {
    if (!(type == o.type && scancode == o.scancode && extended == o.extended &&
          button == o.button && wheel == o.wheel && toggle == o.toggle &&
          interruptButtons == o.interruptButtons && longMs == o.longMs &&
          doubleMs == o.doubleMs && SameFlat(tap, o.tap) &&
          SameFlat(doubleTap, o.doubleTap) && SameFlat(longPress, o.longPress) &&
          radial == o.radial && radialMs == o.radialMs &&
          radialStick == o.radialStick && radialTrigger == o.radialTrigger))
        return false;
    for (size_t i = 0; i < radialDirs.size(); ++i)
        if (!SameFlat(radialDirs[i], o.radialDirs[i])) return false;
    if (chord.size() != o.chord.size()) return false;
    for (size_t i = 0; i < chord.size(); ++i)
        if (!SameFlat(chord[i], o.chord[i])) return false;
    return true;
}

namespace {

json ActionToJson(const KeyAction& a, bool flat = false) {
    json j;
    switch (a.type) {
        case KeyAction::Key:
            j["type"] = "key";
            j["scancode"] = a.scancode;
            j["extended"] = a.extended;
            if (!a.keyName.empty()) j["name"] = a.keyName;
            break;
        case KeyAction::MouseButton:
            j["type"] = "mouse";
            j["button"] = a.button;
            break;
        case KeyAction::MouseWheel:
            j["type"] = "wheel";
            j["wheel"] = a.wheel;
            break;
        default:
            j["type"] = "none";
            break;
    }
    if (flat || a.type == KeyAction::None) return j;

    if (a.toggle) j["toggle"] = true;
    if (!a.interruptButtons.empty())
        j["interrupts"] = a.interruptButtons;
    if (!a.chord.empty()) {
        json arr = json::array();
        for (const KeyAction& c : a.chord) arr.push_back(ActionToJson(c, true));
        j["chord"] = std::move(arr);
    }
    if (a.tap.type != KeyAction::None)
        j["tap"] = ActionToJson(a.tap, true);
    if (a.doubleTap.type != KeyAction::None)
        j["doubleTap"] = ActionToJson(a.doubleTap, true);
    if (a.longPress.type != KeyAction::None)
        j["longPress"] = ActionToJson(a.longPress, true);
    if (a.longMs != 300) j["longMs"] = a.longMs;
    if (a.doubleMs != 260) j["doubleMs"] = a.doubleMs;
    if (a.radial) {
        j["radial"] = true;
        json arr = json::array();
        for (const KeyAction::Part& p : a.radialDirs)
            arr.push_back(ActionToJson(KeyAction(p), true));
        j["radialDirs"] = std::move(arr);
        if (a.radialMs != 300) j["radialMs"] = a.radialMs;
        if (a.radialStick != 1) j["radialStick"] = a.radialStick;
        if (a.radialTrigger != 0) j["radialTrigger"] = a.radialTrigger;
    }
    return j;
}

KeyAction ActionFromJson(const json& j, bool flat = false) {
    KeyAction a;
    if (!j.is_object()) return a;
    const std::string t = j.value("type", "none");
    if (t == "key") {
        a.type = KeyAction::Key;
        a.scancode = j.value("scancode", 0);
        a.extended = j.value("extended", false);
        a.keyName = j.value("name", std::string{});
        if (a.keyName.empty()) a.keyName = NameFromScanCode(a.scancode, a.extended);
    } else if (t == "mouse") {
        a.type = KeyAction::MouseButton;
        a.button = j.value("button", 1);
    } else if (t == "wheel") {
        a.type = KeyAction::MouseWheel;
        a.wheel = j.value("wheel", 120);
    }
    if (flat) return a;

    a.toggle = j.value("toggle", false);
    if (j.contains("interrupts") && j["interrupts"].is_array()) {
        for (const auto& v : j["interrupts"]) {
            if (v.is_number_integer()) {
                const int b = v.get<int>();
                if (b >= 0 && b < kMaxPadButtons)
                    a.interruptButtons.push_back(b);
            }
        }
    }
    if (j.contains("chord") && j["chord"].is_array()) {
        for (const auto& cj : j["chord"]) {
            KeyAction c = ActionFromJson(cj, true);
            if (c.type != KeyAction::None) a.chord.push_back(std::move(c));
        }
    }
    if (j.contains("tap"))
        a.tap = ToPart(ActionFromJson(j["tap"], true));
    if (j.contains("doubleTap"))
        a.doubleTap = ToPart(ActionFromJson(j["doubleTap"], true));
    if (j.contains("longPress"))
        a.longPress = ToPart(ActionFromJson(j["longPress"], true));
    a.longMs = std::clamp(j.value("longMs", a.longMs), 20, 10000);
    a.doubleMs = std::clamp(j.value("doubleMs", a.doubleMs), 20, 2000);
    a.radial = j.value("radial", false);
    if (j.contains("radialDirs") && j["radialDirs"].is_array()) {
        int i = 0;
        for (const auto& rj : j["radialDirs"]) {
            if (i >= 8) break;
            a.radialDirs[i++] = ToPart(ActionFromJson(rj, true));
        }
    }
    a.radialMs = std::clamp(j.value("radialMs", a.radialMs), 50, 5000);
    a.radialStick = std::clamp(j.value("radialStick", a.radialStick), 0, 1);
    a.radialTrigger = std::clamp(j.value("radialTrigger", a.radialTrigger), 0, 2);
    return a;
}

json KeysStickToJson(const StickKeys& k) {
    static const char* names[8] = {"up","upRight","right","downRight",
                                   "down","downLeft","left","upLeft"};
    json j;
    j["enabled"] = k.enabled;
    j["deadzone"] = k.deadzone;
    j["eightWay"] = k.eightWay;
    for (int i = 0; i < 8; ++i) j[names[i]] = ActionToJson(k.dirs[i]);
    return j;
}

StickKeys KeysStickFromJson(const json& j) {
    StickKeys k;
    if (!j.is_object()) return k;
    static const char* names[8] = {"up","upRight","right","downRight",
                                   "down","downLeft","left","upLeft"};
    k.enabled = j.value("enabled", false);
    k.deadzone = j.value("deadzone", k.deadzone);
    k.eightWay = j.value("eightWay", k.eightWay);
    for (int i = 0; i < 8; ++i)
        if (j.contains(names[i])) k.dirs[i] = ActionFromJson(j[names[i]]);
    return k;
}

json MouseStickToJson(const StickMouse& m) {
    return json{
        {"enabled", m.enabled},
        {"deadzone", m.deadzone},
        {"outerDeadzone", m.outerDeadzone},
        {"baseSpeed", m.baseSpeed},
        {"curve", m.curve},
        {"edgeThreshold", m.edgeThreshold},
        {"edgeMultiplier", m.edgeMultiplier},
        {"accelTimeMs", m.accelTimeMs},
    };
}

StickMouse MouseStickFromJson(const json& j) {
    StickMouse m;
    if (!j.is_object()) return m;
    m.enabled = j.value("enabled", m.enabled);
    m.deadzone = j.value("deadzone", m.deadzone);
    m.outerDeadzone = j.value("outerDeadzone", m.outerDeadzone);
    m.baseSpeed = j.value("baseSpeed", m.baseSpeed);
    m.curve = j.value("curve", m.curve);
    m.edgeThreshold = j.value("edgeThreshold", m.edgeThreshold);
    m.edgeMultiplier = j.value("edgeMultiplier", m.edgeMultiplier);
    m.accelTimeMs = j.value("accelTimeMs", m.accelTimeMs);
    return m;
}

json StickToJson(const Stick& s) {
    json j{{"mode", s.mode},
           {"keys", KeysStickToJson(s.keys)},
           {"mouse", MouseStickToJson(s.mouse)}};
    if (s.mod >= 0) {
        j["mod"] = s.mod;
        j["modOverride"] = s.modOverride;
        if (s.modOverride)
            j["alt"] = json{{"mode", s.alt.mode},
                            {"keysDeadzone", s.alt.keysDeadzone},
                            {"eightWay", s.alt.eightWay},
                            {"mouse", MouseStickToJson(s.alt.mouse)}};
    }
    return j;
}

Stick StickFromJson(const json& j) {
    Stick s;
    if (!j.is_object()) return s;
    s.mode = j.value("mode", s.mode);
    if (j.contains("keys")) s.keys = KeysStickFromJson(j["keys"]);
    if (j.contains("mouse")) s.mouse = MouseStickFromJson(j["mouse"]);
    int mod = j.value("mod", -1);
    const bool modOk = mod == 100 || mod == 101 || (mod >= 0 && mod <= 31);
    s.mod = modOk ? mod : -1;
    s.modOverride = j.value("modOverride", false);
    if (j.contains("alt") && j["alt"].is_object()) {
        const json& a = j["alt"];
        s.alt.mode = std::clamp(a.value("mode", -1), -1, 1);
        s.alt.keysDeadzone =
            std::clamp(a.value("keysDeadzone", s.alt.keysDeadzone), 0.0f, 0.9f);
        s.alt.eightWay = a.value("eightWay", s.alt.eightWay);
        if (a.contains("mouse")) s.alt.mouse = MouseStickFromJson(a["mouse"]);
    }
    return s;
}

json TriggerToJson(const Trigger& t) {
    return json{{"enabled", t.enabled},
                {"deadzone", t.deadzone},
                {"threshold", t.threshold},
                {"action", ActionToJson(t.action)}};
}

Trigger TriggerFromJson(const json& j) {
    Trigger t;
    if (!j.is_object()) return t;
    t.enabled = j.value("enabled", t.enabled);
    t.deadzone = j.value("deadzone", t.deadzone);
    t.threshold = j.value("threshold", t.threshold);
    if (j.contains("action")) t.action = ActionFromJson(j["action"]);
    return t;
}

KeyAction Key(int code, bool ext = false) {
    KeyAction a;
    a.type = KeyAction::Key;
    a.scancode = code;
    a.extended = ext;
    a.keyName = NameFromScanCode(code, ext);
    return a;
}

KeyAction MouseBtn(int b) {
    KeyAction a;
    a.type = KeyAction::MouseButton;
    a.button = b;
    return a;
}

} // namespace

DeviceConfig::DeviceConfig() = default;

DeviceConfig MakeDefaultDevice() {
    DeviceConfig d;
    d.name = "Default";

    // Left stick: WASD with diagonals
    d.leftStick.mode = 0;
    d.leftStick.keys.enabled = true;
    d.leftStick.keys.deadzone = 0.20f;
    d.leftStick.keys.eightWay = true;
    d.leftStick.keys.dirs[0] = Key(0x11); // W
    d.leftStick.keys.dirs[1] = KeyAction{};
    d.leftStick.keys.dirs[2] = Key(0x20); // D
    d.leftStick.keys.dirs[3] = KeyAction{};
    d.leftStick.keys.dirs[4] = Key(0x1F); // S
    d.leftStick.keys.dirs[5] = KeyAction{};
    d.leftStick.keys.dirs[6] = Key(0x1E); // A
    d.leftStick.keys.dirs[7] = KeyAction{};

    // Right stick: mouse with Steam-Input-like defaults
    d.rightStick.mode = 1;
    d.rightStick.mouse.enabled = true;
    d.rightStick.mouse.deadzone = 0.15f;
    d.rightStick.mouse.baseSpeed = 900.0f;
    d.rightStick.mouse.curve = 1;
    d.rightStick.mouse.edgeThreshold = 0.85f;
    d.rightStick.mouse.edgeMultiplier = 2.5f;
    d.rightStick.mouse.accelTimeMs = 150;

    // Face buttons / shoulders / system
    d.buttons[0]  = Key(0x39);        // A -> Space
    d.buttons[1]  = Key(0x12);        // B -> E
    d.buttons[2]  = Key(0x2A);        // X -> LShift
    d.buttons[3]  = Key(0x21);        // Y -> F
    d.buttons[4]  = Key(0x0F);        // Back -> Tab
    d.buttons[6]  = Key(0x1C);        // Start -> Enter
    d.buttons[9]  = Key(0x10);        // LB -> Q
    d.buttons[10] = Key(0x13);        // RB -> R
    d.buttons[11] = Key(0x48, true);  // D-Pad Up
    d.buttons[12] = Key(0x50, true);  // D-Pad Down
    d.buttons[13] = Key(0x4B, true);  // D-Pad Left
    d.buttons[14] = Key(0x4D, true);  // D-Pad Right

    // Triggers
    d.leftTrigger.enabled = true;
    d.leftTrigger.deadzone = 0.05f;
    d.leftTrigger.threshold = 0.5f;
    d.leftTrigger.action = Key(0x1D); // LCtrl

    d.rightTrigger.enabled = true;
    d.rightTrigger.deadzone = 0.05f;
    d.rightTrigger.threshold = 0.5f;
    d.rightTrigger.action = MouseBtn(1); // LMB

    return d;
}

const DeviceConfig* MatchDevice(const Config& cfg, const std::string& guid) {
    const DeviceConfig* fallback = nullptr;
    for (const DeviceConfig& d : cfg.devices) {
        if (d.guid.empty()) {
            if (!fallback) fallback = &d;
        } else if (d.guid == guid) {
            return &d;
        }
    }
    return fallback;
}

bool Config::Load(const std::wstring& path, std::string& err) {
    try {
        std::ifstream f(path, std::ios::binary);
        if (!f) { err = "cannot open config file"; return false; }
        std::stringstream ss;
        ss << f.rdbuf();
        json root = json::parse(ss.str());

        version = root.value("version", 1);
        gameExe = root.value("gameExe", std::string{});
        gameArgs = root.value("gameArgs", std::string{});
        pollRateHz = root.value("pollRateHz", pollRateHz);
        foregroundOnly = root.value("foregroundOnly", foregroundOnly);
        devices.clear();
        if (root.contains("devices") && root["devices"].is_array()) {
            for (const auto& dj : root["devices"]) {
                DeviceConfig d;
                d.guid = dj.value("guid", std::string{});
                d.name = dj.value("name", std::string{});
                d.swapSticks = dj.value("swapSticks", false);
                if (dj.contains("buttons") && dj["buttons"].is_object()) {
                    for (auto it = dj["buttons"].begin(); it != dj["buttons"].end(); ++it) {
                        try {
                            int idx = std::stoi(it.key());
                            if (idx >= 0 && idx < kMaxPadButtons)
                                d.buttons[idx] = ActionFromJson(it.value());
                        } catch (...) {
                            // skip malformed key
                        }
                    }
                }
                if (dj.contains("leftStick")) d.leftStick = StickFromJson(dj["leftStick"]);
                if (dj.contains("rightStick")) d.rightStick = StickFromJson(dj["rightStick"]);
                if (dj.contains("leftTrigger")) d.leftTrigger = TriggerFromJson(dj["leftTrigger"]);
                if (dj.contains("rightTrigger")) d.rightTrigger = TriggerFromJson(dj["rightTrigger"]);
                const auto parseLayer = [](const json& lj, LayerConfig& lc) {
                    int h = lj.value("hold", -1);
                    // -1 none, 0..15 pad button, 100 LT, 101 RT, 102 mouse-lock.
                    lc.hold = (h >= 0 && h <= 15) || (h >= 100 && h <= 102) ? h : -1;
                    lc.autoMouseLock = lj.value("autoMouseLock", false);
                    if (lj.contains("buttons") && lj["buttons"].is_object()) {
                        for (auto it = lj["buttons"].begin(); it != lj["buttons"].end(); ++it) {
                            try {
                                int idx = std::stoi(it.key());
                                if (idx >= 0 && idx < kMaxPadButtons)
                                    lc.buttons[idx] = ActionFromJson(it.value());
                            } catch (...) {
                                // skip malformed key
                            }
                        }
                    }
                    lc.rightOverride = lj.value("rightOverride", false);
                    if (lj.contains("rightStick"))
                        lc.right = StickFromJson(lj["rightStick"]);
                    lc.leftOverride = lj.value("leftOverride", false);
                    if (lj.contains("leftStick"))
                        lc.left = StickFromJson(lj["leftStick"]);
                };
                if (dj.contains("layers") && dj["layers"].is_array()) {
                    int L = 0;
                    for (const json& lj : dj["layers"]) {
                        if (L >= DeviceConfig::kMaxLayers) break;
                        if (lj.is_object()) parseLayer(lj, d.layers[L]);
                        ++L;
                    }
                }
                if (!dj.contains("layers") && dj.contains("layer") &&
                    dj["layer"].is_object()) {
                    // Legacy single-layer JSON (pre-multi-layer builds).
                    LayerConfig legacy;
                    parseLayer(dj["layer"], legacy);
                    if (legacy.autoMouseLock && legacy.hold == -1)
                        legacy.hold = 102;
                    legacy.autoMouseLock = false;
                    d.layers[0] = legacy;
                }
                devices.push_back(std::move(d));
            }
        }
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

bool Config::Save(const std::wstring& path, std::string& err) const {
    try {
        json root;
        root["version"] = version;
        root["gameExe"] = gameExe;
        root["gameArgs"] = gameArgs;
        root["pollRateHz"] = pollRateHz;
        root["foregroundOnly"] = foregroundOnly;

        json arr = json::array();
        for (const DeviceConfig& d : devices) {
            json dj;
            dj["guid"] = d.guid;
            dj["name"] = d.name;
            dj["swapSticks"] = d.swapSticks;
            json btns = json::object();
            for (const auto& kv : d.buttons)
                btns[std::to_string(kv.first)] = ActionToJson(kv.second);
            dj["buttons"] = std::move(btns);
            dj["leftStick"] = StickToJson(d.leftStick);
            dj["rightStick"] = StickToJson(d.rightStick);
            dj["leftTrigger"] = TriggerToJson(d.leftTrigger);
            dj["rightTrigger"] = TriggerToJson(d.rightTrigger);
            bool anyLayer = false;
            for (const auto& l : d.layers)
                if (l.hold != -1 || !l.buttons.empty() || l.rightOverride ||
                    l.leftOverride)
                    anyLayer = true;
            if (anyLayer) {
                json arr = json::array();
                for (const auto& l : d.layers) {
                    json lj;
                    lj["hold"] = l.hold;
                    json btns = json::object();
                    for (const auto& kv : l.buttons)
                        btns[std::to_string(kv.first)] = ActionToJson(kv.second);
                    lj["buttons"] = std::move(btns);
                    // Sticks serialize even before their override is enabled
                    // so UI edits survive a round trip.
                    if (l.rightOverride) lj["rightOverride"] = true;
                    lj["rightStick"] = StickToJson(l.right);
                    if (l.leftOverride) lj["leftOverride"] = true;
                    lj["leftStick"] = StickToJson(l.left);
                    arr.push_back(std::move(lj));
                }
                dj["layers"] = std::move(arr);
            }
            arr.push_back(std::move(dj));
        }
        root["devices"] = std::move(arr);

        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) { err = "cannot write config file"; return false; }
        f << root.dump(2);
        return f.good();
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

} // namespace adf
