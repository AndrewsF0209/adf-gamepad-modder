#pragma once

#include <string>
#include <vector>

#include "adf/config.h"

namespace adf {

inline constexpr int kPadButtons = kMaxPadButtons;

struct PadState {
    bool connected = false;
    float lx = 0.0f, ly = 0.0f;   // -1..1 (y points up/forward)
    float rx = 0.0f, ry = 0.0f;
    float lt = 0.0f, rt = 0.0f;   // 0..1
    bool buttons[kPadButtons] = {};
};

struct PadInfo {
    std::string guid;
    std::string name;
};

// SDL3 gamepad backend. One instance lives on a single thread.
class PadSource {
public:
    PadSource();
    ~PadSource();

    bool Init(std::string& err);
    void Shutdown();

    // Process SDL events (hotplug) and update device list.
    void Pump();

    int Count() const;
    const std::vector<PadInfo>& Infos() const { return infos_; }

    // Snapshot the first pad matching preferGuid (or any pad if no entry
    // matches / preferGuid is empty). Returns false when nothing is open.
    bool Snapshot(const std::string& preferGuid, PadState& out,
                  std::string* outGuid = nullptr) const;

private:
    struct Pad;
    std::vector<Pad> pads_;
    std::vector<PadInfo> infos_;

    void RefreshInfos();
};

} // namespace adf
