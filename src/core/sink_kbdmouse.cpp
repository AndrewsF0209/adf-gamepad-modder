#include "adf/sink_kbdmouse.h"

#include <windows.h>

namespace adf {

void KbdMouseSink::Key(int scancode, bool extended, bool down) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wScan = static_cast<WORD>(scancode & 0xFF);
    in.ki.dwFlags = KEYEVENTF_SCANCODE;
    if (extended) in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    if (!down) in.ki.dwFlags |= KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(INPUT));
}

void KbdMouseSink::MouseMove(int dx, int dy) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = dx;
    in.mi.dy = dy;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &in, sizeof(INPUT));
}

void KbdMouseSink::MouseButton(int button, bool down) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    DWORD downFlag = 0, upFlag = 0;
    switch (button) {
        case 1: downFlag = MOUSEEVENTF_LEFTDOWN;  upFlag = MOUSEEVENTF_LEFTUP;   break;
        case 2: downFlag = MOUSEEVENTF_RIGHTDOWN; upFlag = MOUSEEVENTF_RIGHTUP;  break;
        case 3: downFlag = MOUSEEVENTF_MIDDLEDOWN;upFlag = MOUSEEVENTF_MIDDLEUP; break;
        case 4:
        case 5:
            downFlag = MOUSEEVENTF_XDOWN;
            upFlag = MOUSEEVENTF_XUP;
            in.mi.mouseData = (button == 4) ? XBUTTON1 : XBUTTON2;
            break;
        default: return;
    }
    in.mi.dwFlags = down ? downFlag : upFlag;
    SendInput(1, &in, sizeof(INPUT));
}

void KbdMouseSink::Wheel(int delta) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_WHEEL;
    in.mi.mouseData = static_cast<DWORD>(delta);
    SendInput(1, &in, sizeof(INPUT));
}

} // namespace adf
