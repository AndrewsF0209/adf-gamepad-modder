#include "adf/pad.h"

#include <algorithm>
#include <cmath>

#include <SDL3/SDL.h>

namespace adf {

struct PadSource::Pad {
    SDL_Gamepad* gamepad = nullptr;
    SDL_JoystickID id = 0;
    std::string guid;
    std::string name;
};

PadSource::PadSource() = default;

PadSource::~PadSource() {
    Shutdown();
}

bool PadSource::Init(std::string& err) {
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        err = SDL_GetError();
        return false;
    }

    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids) {
        for (int i = 0; i < count; ++i) {
            if (SDL_OpenGamepad(ids[i])) {
                Pad p;
                p.id = ids[i];
                p.gamepad = SDL_GetGamepadFromID(ids[i]);
                char guidBuf[33] = {};
                SDL_GUID g = SDL_GetGamepadGUIDForID(ids[i]);
                SDL_GUIDToString(g, guidBuf, sizeof(guidBuf));
                p.guid = guidBuf;
                const char* nm = SDL_GetGamepadName(p.gamepad);
                p.name = nm ? nm : "Controller";
                pads_.push_back(std::move(p));
            }
        }
        SDL_free(ids);
    }
    RefreshInfos();
    return true;
}

void PadSource::Shutdown() {
    for (Pad& p : pads_) {
        if (p.gamepad) SDL_CloseGamepad(p.gamepad);
    }
    pads_.clear();
    infos_.clear();
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

int PadSource::Count() const {
    return static_cast<int>(pads_.size());
}

void PadSource::RefreshInfos() {
    infos_.clear();
    for (const Pad& p : pads_)
        infos_.push_back({p.guid, p.name});
}

void PadSource::Pump() {
    SDL_PumpEvents();
    SDL_Event ev;
    bool changed = false;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_EVENT_GAMEPAD_ADDED) {
            const SDL_JoystickID id = ev.gdevice.which;
            bool known = std::any_of(pads_.begin(), pads_.end(),
                                     [id](const Pad& p) { return p.id == id; });
            if (!known && SDL_OpenGamepad(id)) {
                Pad p;
                p.id = id;
                p.gamepad = SDL_GetGamepadFromID(id);
                char guidBuf[33] = {};
                SDL_GUIDToString(SDL_GetGamepadGUIDForID(id), guidBuf,
                                 sizeof(guidBuf));
                p.guid = guidBuf;
                const char* nm = SDL_GetGamepadName(p.gamepad);
                p.name = nm ? nm : "Controller";
                pads_.push_back(std::move(p));
                changed = true;
            }
        } else if (ev.type == SDL_EVENT_GAMEPAD_REMOVED) {
            const SDL_JoystickID id = ev.gdevice.which;
            for (auto it = pads_.begin(); it != pads_.end(); ++it) {
                if (it->id == id) {
                    if (it->gamepad) SDL_CloseGamepad(it->gamepad);
                    pads_.erase(it);
                    changed = true;
                    break;
                }
            }
        }
    }
    if (changed) RefreshInfos();
}

bool PadSource::Snapshot(const std::string& preferGuid, PadState& out,
                         std::string* outGuid) const {
    if (pads_.empty()) return false;

    const Pad* chosen = nullptr;
    if (!preferGuid.empty()) {
        for (const Pad& p : pads_)
            if (p.guid == preferGuid) { chosen = &p; break; }
    }
    if (!chosen) chosen = &pads_.front();
    if (!chosen->gamepad) return false;

    out.connected = true;

    auto normAxis = [](Sint16 v) {
        float f = std::clamp(static_cast<float>(v) / 32768.0f, -1.0f, 1.0f);
        if (std::fabs(f) < 0.001f) f = 0.0f;
        return f;
    };

    out.lx = normAxis(SDL_GetGamepadAxis(chosen->gamepad, SDL_GAMEPAD_AXIS_LEFTX));
    out.ly = -normAxis(SDL_GetGamepadAxis(chosen->gamepad, SDL_GAMEPAD_AXIS_LEFTY));
    out.rx = normAxis(SDL_GetGamepadAxis(chosen->gamepad, SDL_GAMEPAD_AXIS_RIGHTX));
    out.ry = -normAxis(SDL_GetGamepadAxis(chosen->gamepad, SDL_GAMEPAD_AXIS_RIGHTY));
    out.lt = std::clamp(
        static_cast<float>(SDL_GetGamepadAxis(chosen->gamepad,
                                              SDL_GAMEPAD_AXIS_LEFT_TRIGGER)) /
            32767.0f,
        0.0f, 1.0f);
    out.rt = std::clamp(
        static_cast<float>(SDL_GetGamepadAxis(chosen->gamepad,
                                              SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) /
            32767.0f,
        0.0f, 1.0f);

    const int n = std::min<int>(kPadButtons, SDL_GAMEPAD_BUTTON_COUNT);
    for (int i = 0; i < n; ++i)
        out.buttons[i] =
            SDL_GetGamepadButton(chosen->gamepad, static_cast<SDL_GamepadButton>(i));

    if (outGuid) *outGuid = chosen->guid;
    return true;
}

} // namespace adf
