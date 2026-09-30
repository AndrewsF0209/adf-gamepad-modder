#pragma once

namespace adf {

// Thin wrapper around SendInput. All calls happen from the mapper thread.
class KbdMouseSink {
public:
    void Key(int scancode, bool extended, bool down);
    void MouseMove(int dx, int dy);
    void MouseButton(int button, bool down); // 1 L 2 R 3 M 4 X1 5 X2
    void Wheel(int delta);                   // typically +/-120
};

} // namespace adf
