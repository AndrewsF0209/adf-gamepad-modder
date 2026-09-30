#pragma once

#include <string>

namespace adf {

struct ScanCode {
    int code = 0;
    bool extended = false;
};

// Human-readable name ("W", "Space", "Up", ...) -> set-1 scancode.
// Returns {0,false} for unknown names (case-insensitive).
ScanCode ScanCodeFromName(const std::string& name);

// scancode -> display name ("" if unknown).
std::string NameFromScanCode(int code, bool extended);

} // namespace adf
