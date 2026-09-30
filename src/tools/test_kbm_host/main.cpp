// Minimal fake "old game": a plain Win32 window that reports
//  - raw keyboard scancodes (E0 extended flag visible)
//  - raw mouse relative deltas
//  - DirectInput8 keyboard (DIK_W) and mouse (DIMOUSESTATE2) state
// Deploying a dinput8.dll proxy next to this exe validates both export
// forwarding and SendInput injection into a DirectInput consumer.

#include <windows.h>
#include <windowsx.h>

#define INITGUID
#include <dinput.h>

#include <array>
#include <deque>
#include <string>
#include <vector>

#pragma comment(lib, "dinput8.lib")

namespace {

struct State {
    IDirectInput8W* di = nullptr;
    IDirectInputDevice8W* kb = nullptr;
    IDirectInputDevice8W* mouse = nullptr;
    std::deque<std::wstring> events;
    long rawMouseX = 0, rawMouseY = 0;
    long diMouseX = 0, diMouseY = 0;
    bool diW = false;
};

State g;

void AddEvent(const std::wstring& s) {
    g.events.push_back(s);
    if (g.events.size() > 8) g.events.pop_front();
}

bool InitDirectInput(HWND hwnd) {
    if (FAILED(DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION,
                                 IID_IDirectInput8W,
                                 reinterpret_cast<void**>(&g.di), nullptr)))
        return false;

    if (SUCCEEDED(g.di->CreateDevice(GUID_SysKeyboard, &g.kb, nullptr))) {
        g.kb->SetDataFormat(&c_dfDIKeyboard);
        g.kb->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
        g.kb->Acquire();
    }
    if (SUCCEEDED(g.di->CreateDevice(GUID_SysMouse, &g.mouse, nullptr))) {
        g.mouse->SetDataFormat(&c_dfDIMouse2);
        g.mouse->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
        {
            DIPROPDWORD bufSize{};
            bufSize.diph.dwSize = sizeof(DIPROPDWORD);
            bufSize.diph.dwHeaderSize = sizeof(DIPROPHEADER);
            bufSize.diph.dwObj = 0;
            bufSize.diph.dwHow = DIPH_DEVICE;
            bufSize.dwData = 64;
            g.mouse->SetProperty(DIPROP_BUFFERSIZE, &bufSize.diph);
        }
        g.mouse->Acquire();
    }
    return true;
}

void PollInput() {
    if (g.kb) {
        BYTE ks[256] = {};
        HRESULT hr = g.kb->GetDeviceState(sizeof(ks), ks);
        if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
            g.kb->Acquire();
            g.kb->GetDeviceState(sizeof(ks), ks);
        }
        g.diW = (ks[DIK_W] & 0x80) != 0;
    }
    if (g.mouse) {
        DIMOUSESTATE2 ms{};
        HRESULT hr = g.mouse->GetDeviceState(sizeof(ms), &ms);
        if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
            g.mouse->Acquire();
            g.mouse->GetDeviceState(sizeof(ms), &ms);
        }
        g.diMouseX += ms.lX;
        g.diMouseY += ms.lY;
    }
}

void HandleRawInput(HRAWINPUT h) {
    UINT size = 0;
    GetRawInputData(h, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
    std::vector<BYTE> buf(size);
    auto* raw = reinterpret_cast<RAWINPUT*>(buf.data());
    if (GetRawInputData(h, RID_INPUT, raw, &size, sizeof(RAWINPUTHEADER)) != size)
        return;

    if (raw->header.dwType == RIM_TYPEKEYBOARD) {
        wchar_t msg[96];
        swprintf_s(msg, L"Raw KB scancode=0x%02X E0=%d make=%d vk=0x%02X",
                   raw->data.keyboard.MakeCode,
                   (raw->data.keyboard.Flags & RI_KEY_E0) ? 1 : 0,
                   (raw->data.keyboard.Flags & RI_KEY_BREAK) ? 0 : 1,
                   raw->data.keyboard.VKey);
        AddEvent(msg);
    } else if (raw->header.dwType == RIM_TYPEMOUSE) {
        g.rawMouseX += raw->data.mouse.lLastX;
        g.rawMouseY += raw->data.mouse.lLastY;
    }
}

void Paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    FillRect(hdc, &rc, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));

    int y = 16;
    auto line = [&](const std::wstring& s) {
        TextOutW(hdc, 12, y, s.c_str(), static_cast<int>(s.size()));
        y += 22;
    };

    line(L"ADF proxy/mapping test host - move mouse / press keys");
    line(L"");
    line(L"--- Raw input ---");
    wchar_t buf[128];
    swprintf_s(buf, L"raw mouse accum:  X=%ld  Y=%ld", g.rawMouseX, g.rawMouseY);
    line(buf);
    for (const std::wstring& e : g.events) line(e);

    line(L"");
    line(L"--- DirectInput8 (through proxy if deployed) ---");
    swprintf_s(buf, L"DI keyboard W held: %s", g.diW ? L"YES" : L"no");
    line(buf);
    swprintf_s(buf, L"DI mouse accum:    X=%ld  Y=%ld", g.diMouseX, g.diMouseY);
    line(buf);

    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            std::array<RAWINPUTDEVICE, 2> rid{};
            rid[0].usUsagePage = 0x01;
            rid[0].usUsage = 0x06; // keyboard
            rid[0].hwndTarget = hwnd;
            rid[1].usUsagePage = 0x01;
            rid[1].usUsage = 0x02; // mouse
            rid[1].hwndTarget = hwnd;
            RegisterRawInputDevices(rid.data(), static_cast<UINT>(rid.size()),
                                    sizeof(RAWINPUTDEVICE));
            InitDirectInput(hwnd);
            SetTimer(hwnd, 1, 16, nullptr);
            return 0;
        }
        case WM_TIMER:
            PollInput();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_INPUT:
            HandleRawInput(reinterpret_cast<HRAWINPUT>(lp));
            return 0;
        case WM_ACTIVATE:
            if (g.kb) g.kb->Acquire();
            if (g.mouse) g.mouse->Acquire();
            return 0;
        case WM_PAINT:
            Paint(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int show) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"AdfTestHost";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowW(L"AdfTestHost", L"ADF test host",
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              640, 480, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, show);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
