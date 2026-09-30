#include "adf/scancodes.h"

#include <cctype>
#include <unordered_map>

namespace adf {
namespace {

struct ScEntry {
    const char* name;
    int code;
    bool extended;
};

// PS/2 scan code set 1 make codes (the encoding SendInput wScan expects).
// Reference: USB/PS2 HID scan code tables, "Scan Code 1".
const ScEntry kTable[] = {
    {"Esc", 0x01, false},
    {"1", 0x02, false}, {"2", 0x03, false}, {"3", 0x04, false},
    {"4", 0x05, false}, {"5", 0x06, false}, {"6", 0x07, false},
    {"7", 0x08, false}, {"8", 0x09, false}, {"9", 0x0A, false},
    {"0", 0x0B, false}, {"-", 0x0C, false}, {"=", 0x0D, false},
    {"Backspace", 0x0E, false},
    {"Tab", 0x0F, false},
    {"Q", 0x10, false}, {"W", 0x11, false}, {"E", 0x12, false},
    {"R", 0x13, false}, {"T", 0x14, false}, {"Y", 0x15, false},
    {"U", 0x16, false}, {"I", 0x17, false}, {"O", 0x18, false},
    {"P", 0x19, false}, {"[", 0x1A, false}, {"]", 0x1B, false},
    {"Enter", 0x1C, false},
    {"LCtrl", 0x1D, false},
    {"A", 0x1E, false}, {"S", 0x1F, false}, {"D", 0x20, false},
    {"F", 0x21, false}, {"G", 0x22, false}, {"H", 0x23, false},
    {"J", 0x24, false}, {"K", 0x25, false}, {"L", 0x26, false},
    {";", 0x27, false}, {"'", 0x28, false}, {"`", 0x29, false},
    {"LShift", 0x2A, false},
    {"\\", 0x2B, false},
    {"Z", 0x2C, false}, {"X", 0x2D, false}, {"C", 0x2E, false},
    {"V", 0x2F, false}, {"B", 0x30, false}, {"N", 0x31, false},
    {"M", 0x32, false}, {",", 0x33, false}, {".", 0x34, false},
    {"/", 0x35, false},
    {"RShift", 0x36, false},
    {"NumPad*", 0x37, false},
    {"LAlt", 0x38, false},
    {"Space", 0x39, false},
    {"CapsLock", 0x3A, false},
    {"F1", 0x3B, false}, {"F2", 0x3C, false}, {"F3", 0x3D, false},
    {"F4", 0x3E, false}, {"F5", 0x3F, false}, {"F6", 0x40, false},
    {"F7", 0x41, false}, {"F8", 0x42, false}, {"F9", 0x43, false},
    {"F10", 0x44, false},
    {"NumLock", 0x45, false},
    {"ScrollLock", 0x46, false},
    {"NumPad7", 0x47, false}, {"NumPad8", 0x48, false},
    {"NumPad9", 0x49, false}, {"NumPad-", 0x4A, false},
    {"NumPad4", 0x4B, false}, {"NumPad5", 0x4C, false},
    {"NumPad6", 0x4D, false}, {"NumPad+", 0x4E, false},
    {"NumPad1", 0x4F, false}, {"NumPad2", 0x50, false},
    {"NumPad3", 0x51, false}, {"NumPad0", 0x52, false},
    {"NumPad.", 0x53, false},
    {"F11", 0x57, false}, {"F12", 0x58, false},

    // E0-prefixed (extended) keys
    {"RCtrl", 0x1D, true},
    {"RAlt", 0x38, true},
    {"Up", 0x48, true}, {"Down", 0x50, true},
    {"Left", 0x4B, true}, {"Right", 0x4D, true},
    {"Home", 0x47, true}, {"End", 0x4F, true},
    {"PgUp", 0x49, true}, {"PgDn", 0x51, true},
    {"Insert", 0x52, true}, {"Delete", 0x53, true},
    {"NumPadEnter", 0x1C, true},
    {"NumPad/", 0x35, true},
    {"LWin", 0x5B, true}, {"RWin", 0x5C, true},
    {"Apps", 0x5D, true},
};

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

ScanCode ScanCodeFromName(const std::string& name) {
    const std::string key = Lower(name);
    for (const ScEntry& e : kTable) {
        if (Lower(e.name) == key) return {e.code, e.extended};
    }
    return {0, false};
}

std::string NameFromScanCode(int code, bool extended) {
    for (const ScEntry& e : kTable) {
        if (e.code == code && e.extended == extended) return e.name;
    }
    return "";
}

} // namespace adf
