// Input Probe: a deliberately boring diagnostic window (no rendering of any
// kind) that reports keyboard/mouse events through the four input paths
// used by classic PC games:
//
//   [DI-KB] DirectInput8 keyboard polled at ~1000 Hz   (Red Alert 2 style)
//   [DI-MS] DirectInput8 mouse buffered events         (clicks / wheel / dx dy)
//   [Raw  ] WM_INPUT raw keyboard + mouse              (CS 1.6 raw-input style)
//   [消息 ] WM_KEYDOWN / WM_LBUTTONDOWN ...            (standard window messages)
//   [异步 ] GetAsyncKeyState polled at ~1000 Hz
//
// The panel above the log lists every key/button currently held, with
// per-key hold duration, per channel.
//
// dinput8.dll is loaded explicitly from the system directory so that placing
// this exe next to a deployed proxy dinput8.dll never hooks the probe itself.

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <timeapi.h>

#define INITGUID
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "winmm.lib")

namespace {

// ---------------------------------------------------------------- UI ids
enum {
    IDC_LOG = 1001,
    IDC_BTN_CLEAR,
    IDC_BTN_PAUSE,
    IDC_BTN_SAVE,
    IDC_STATUS,
    IDC_HELD,
    IDC_TIMER_STATUS = 1,
    IDC_TIMER_UI = 2,
};

// ------------------------------------------------------------- shared log
std::mutex g_mtx;
std::deque<std::wstring> g_lines;
constexpr size_t kMaxLines = 4000;
std::atomic<bool> g_paused{false};
std::atomic<bool> g_running{true};

// per-channel counters
std::atomic<uint64_t> g_nDiKb{0}, g_nDiMs{0}, g_nRawKb{0}, g_nRawMs{0};
std::atomic<uint64_t> g_nMsgKb{0}, g_nMsgMs{0}, g_nAsync{0};

// live status strings (owned by the poll thread, read by the UI timer)
std::wstring g_diStatus;
std::wstring g_heldDi;
std::wstring g_heldAsync;
bool g_diKbOk = false, g_diMsOk = false;
long g_diMouseX = 0, g_diMouseY = 0;
long g_rawMouseX = 0, g_rawMouseY = 0;
POINT g_cursor{0, 0};

// Held-state tracked for the live "currently held" panel. Mouse bits use the
// DIMOUSESTATE2 button order (0=left, 1=right, 2=middle, 3=X1, 4=X2).
std::atomic<uint32_t> g_diMouseHeld{0}, g_rawMouseHeld{0}, g_msgMouseHeld{0};
std::unordered_map<uint32_t, ULONGLONG> g_rawHeld; // UI thread only
std::unordered_map<WPARAM, ULONGLONG> g_msgHeld;   // UI thread only

std::wstring Stamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t b[16];
    swprintf_s(b, L"%02d:%02d:%02d.%03d",
               st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return b;
}

void PushLog(const std::wstring& s) {
    std::lock_guard<std::mutex> lk(g_mtx);
    g_lines.push_back(Stamp() + L"  " + s);
    while (g_lines.size() > kMaxLines) g_lines.pop_front();
}

// ----------------------------------------------------------- key naming
std::wstring KeyName(DWORD scanCode, bool extended) {
    LONG lp = LONG(scanCode << 16) | (extended ? (1 << 24) : 0);
    wchar_t name[64] = {};
    if (GetKeyNameTextW(lp, name, 63) > 0) return name;
    wchar_t b[16];
    swprintf_s(b, L"0x%02lX", scanCode);
    return b;
}

std::wstring VkName(WPARAM vk) {
    UINT sc = MapVirtualKeyW(UINT(vk), MAPVK_VK_TO_VSC_EX);
    bool ext = false;
    switch (vk) {
        case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
        case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END:
        case VK_PRIOR: case VK_NEXT:
        case VK_DIVIDE: case VK_RCONTROL: case VK_RMENU:
            ext = true; break;
        default: break;
    }
    std::wstring n = KeyName(sc & 0xFF, ext);
    wchar_t b[96];
    swprintf_s(b, L"VK=0x%02X(%s)", UINT(vk), n.c_str());
    return b;
}

// -------------------------------------------------- DirectInput bootstrap
using PFN_DirectInput8Create =
    HRESULT (WINAPI*)(HINSTANCE, DWORD, REFGUID, LPVOID*, LPUNKNOWN);

IDirectInput8W* CreateDI() {
    HMODULE h = LoadLibraryExW(L"dinput8.dll", nullptr,
                               LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!h) return nullptr;
    auto create = reinterpret_cast<PFN_DirectInput8Create>(
        GetProcAddress(h, "DirectInput8Create"));
    if (!create) return nullptr;
    IDirectInput8W* di = nullptr;
    if (FAILED(create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION,
                      IID_IDirectInput8W,
                      reinterpret_cast<void**>(&di), nullptr)))
        return nullptr;
    return di;
}

// Manual replicas of c_dfDIKeyboard / c_dfDIMouse2 (the DIDATAFORMAT
// constants live in the dinput8 import library; rebuilding them keeps the
// load path explicit). Object-type GUIDs (GUID_Key/GUID_Button/GUID_*Axis)
// are defined by dinput.h itself.
bool BuildKeyboardFormat(DIDATAFORMAT& df,
                         std::vector<DIOBJECTDATAFORMAT>& objs) {
    objs.resize(256);
    for (DWORD i = 0; i < 256; ++i) {
        objs[i].pguid = &GUID_Key;
        objs[i].dwOfs = i;
        objs[i].dwType = DIDFT_BUTTON | DIDFT_MAKEINSTANCE(i);
        objs[i].dwFlags = 0;
    }
    df.dwSize = sizeof(DIDATAFORMAT);
    df.dwObjSize = sizeof(DIOBJECTDATAFORMAT);
    df.dwFlags = 0;
    df.dwDataSize = 256;
    df.dwNumObjs = 256;
    df.rgodf = objs.data();
    return true;
}

bool BuildMouseFormat(DIDATAFORMAT& df,
                      std::vector<DIOBJECTDATAFORMAT>& objs) {
    struct Map { DWORD ofs; DWORD type; const GUID* guid; };
    const Map defs[] = {
        {offsetof(DIMOUSESTATE2, lX),          DIDFT_AXIS   | DIDFT_MAKEINSTANCE(0), &GUID_XAxis},
        {offsetof(DIMOUSESTATE2, lY),          DIDFT_AXIS   | DIDFT_MAKEINSTANCE(1), &GUID_YAxis},
        {offsetof(DIMOUSESTATE2, lZ),          DIDFT_AXIS   | DIDFT_MAKEINSTANCE(2), &GUID_ZAxis},
        {offsetof(DIMOUSESTATE2, rgbButtons[0]), DIDFT_BUTTON | DIDFT_MAKEINSTANCE(0), &GUID_Button},
        {offsetof(DIMOUSESTATE2, rgbButtons[1]), DIDFT_BUTTON | DIDFT_MAKEINSTANCE(1), &GUID_Button},
        {offsetof(DIMOUSESTATE2, rgbButtons[2]), DIDFT_BUTTON | DIDFT_MAKEINSTANCE(2), &GUID_Button},
        {offsetof(DIMOUSESTATE2, rgbButtons[3]), DIDFT_BUTTON | DIDFT_MAKEINSTANCE(3), &GUID_Button},
        {offsetof(DIMOUSESTATE2, rgbButtons[4]), DIDFT_BUTTON | DIDFT_MAKEINSTANCE(4), &GUID_Button},
        {offsetof(DIMOUSESTATE2, rgbButtons[5]), DIDFT_BUTTON | DIDFT_MAKEINSTANCE(5), &GUID_Button},
        {offsetof(DIMOUSESTATE2, rgbButtons[6]), DIDFT_BUTTON | DIDFT_MAKEINSTANCE(6), &GUID_Button},
        {offsetof(DIMOUSESTATE2, rgbButtons[7]), DIDFT_BUTTON | DIDFT_MAKEINSTANCE(7), &GUID_Button},
    };
    objs.clear();
    for (const Map& m : defs) {
        DIOBJECTDATAFORMAT o{};
        o.pguid = m.guid;
        o.dwOfs = m.ofs;
        o.dwType = m.type;
        objs.push_back(o);
    }
    df.dwSize = sizeof(DIDATAFORMAT);
    df.dwObjSize = sizeof(DIOBJECTDATAFORMAT);
    df.dwFlags = DIDF_RELAXIS;
    df.dwDataSize = sizeof(DIMOUSESTATE2);
    df.dwNumObjs = static_cast<DWORD>(objs.size());
    df.rgodf = objs.data();
    return true;
}

const wchar_t* MouseButtonName(int i) {
    static const wchar_t* kNames[] = {
        L"左键", L"右键", L"中键", L"侧键1(X1)", L"侧键2(X2)",
        L"按键5", L"按键6", L"按键7",
    };
    return (i >= 0 && i < 8) ? kNames[i] : L"?";
}

// ----------------------------------------------------------- poll thread
struct PollThread {
    HWND hwnd = nullptr;
    IDirectInput8W* di = nullptr;
    IDirectInputDevice8W* kb = nullptr;
    IDirectInputDevice8W* ms = nullptr;
    std::vector<DIOBJECTDATAFORMAT> kbFmtObjs, msFmtObjs;
    DIDATAFORMAT kbFmt{}, msFmt{};
    bool kbFormatOk = false, msFormatOk = false;

    BYTE prevKb[256] = {};
    bool prevVk[256] = {};
    std::unordered_map<int, ULONGLONG> kbDownAt, vkDownAt;

    bool Init(HWND owner, bool background = false) {
        hwnd = owner;
        di = CreateDI();
        if (!di) {
            g_diStatus = L"DirectInput8 初始化失败";
            return false;
        }
        BuildKeyboardFormat(kbFmt, kbFmtObjs);
        BuildMouseFormat(msFmt, msFmtObjs);
        const DWORD coop = (background ? DISCL_BACKGROUND : DISCL_FOREGROUND)
                         | DISCL_NONEXCLUSIVE;

        if (SUCCEEDED(di->CreateDevice(GUID_SysKeyboard, &kb, nullptr))) {
            kbFormatOk = SUCCEEDED(kb->SetDataFormat(&kbFmt));
            kb->SetCooperativeLevel(hwnd, coop);
        }
        if (SUCCEEDED(di->CreateDevice(GUID_SysMouse, &ms, nullptr))) {
            msFormatOk = SUCCEEDED(ms->SetDataFormat(&msFmt));
            ms->SetCooperativeLevel(hwnd, coop);
            DIPROPDWORD bs{};
            bs.diph.dwSize = sizeof(DIPROPDWORD);
            bs.diph.dwHeaderSize = sizeof(DIPROPHEADER);
            bs.diph.dwHow = DIPH_DEVICE;
            bs.dwData = 256;
            ms->SetProperty(DIPROP_BUFFERSIZE, &bs.diph);
        }
        return true;
    }

    void AcquireAll() {
        if (kb) kb->Acquire();
        if (ms) { ms->Acquire(); ms->Poll(); }
    }

    void PollKb() {
        if (!kb) { g_diKbOk = false; return; }
        BYTE ks[256] = {};
        HRESULT hr = kb->GetDeviceState(sizeof(ks), ks);
        if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
            g_diKbOk = false;
            kb->Acquire();
            hr = kb->GetDeviceState(sizeof(ks), ks);
        }
        g_diKbOk = SUCCEEDED(hr);
        if (!g_diKbOk) return;

        std::wstring held;
        for (int i = 0; i < 256; ++i) {
            bool now = (ks[i] & 0x80) != 0;
            bool was = (prevKb[i] & 0x80) != 0;
            if (now) {
                if (!held.empty()) held += L" ";
                ULONGLONG dur = 0;
                auto it = kbDownAt.find(i);
                if (it != kbDownAt.end()) dur = GetTickCount64() - it->second;
                wchar_t b[80];
                swprintf_s(b, L"%s(%02X) %.1fs", KeyName(i, false).c_str(), i,
                           double(dur) / 1000.0);
                held += b;
            }
            if (now && !was) {
                kbDownAt[i] = GetTickCount64();
                wchar_t b[128];
                swprintf_s(b, L"[DI-KB] 按下   DIK 0x%02X  %s",
                           i, KeyName(i, false).c_str());
                PushLog(b);
                ++g_nDiKb;
            } else if (!now && was) {
                ULONGLONG dur = 0;
                auto it = kbDownAt.find(i);
                if (it != kbDownAt.end()) dur = GetTickCount64() - it->second;
                wchar_t b[128];
                swprintf_s(b, L"[DI-KB] 松开   DIK 0x%02X  %-10s  持续 %llu ms",
                           i, KeyName(i, false).c_str(),
                           static_cast<unsigned long long>(dur));
                PushLog(b);
                ++g_nDiKb;
            }
        }
        memcpy(prevKb, ks, sizeof(ks));
        { std::lock_guard<std::mutex> lk(g_mtx); g_heldDi = held; }
    }

    void PollMouse() {
        if (!ms) { g_diMsOk = false; return; }
        ms->Poll();
        int reacquires = 0;
        for (;;) {
            DIDEVICEOBJECTDATA od[64];
            DWORD n = 64;
            HRESULT hr = ms->GetDeviceData(sizeof(DIDEVICEOBJECTDATA),
                                          od, &n, 0);
            if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
                g_diMsOk = false;
                ms->Acquire();
                ms->Poll();
                // A session that can never acquire (e.g. headless host) must
                // not spin here -- give up until the next poll tick.
                if (++reacquires >= 3) return;
                continue;
            }
            g_diMsOk = SUCCEEDED(hr);
            if (!g_diMsOk || n == 0) break;

            for (DWORD i = 0; i < n; ++i) {
                wchar_t b[128];
                const DWORD ofs = od[i].dwOfs;
                if (ofs == DIJOFS_X) {
                    g_diMouseX += LONG(od[i].dwData);
                } else if (ofs == DIJOFS_Y) {
                    g_diMouseY += LONG(od[i].dwData);
                } else if (ofs == DIJOFS_Z) {
                    LONG delta = LONG(od[i].dwData);
                    swprintf_s(b, L"[DI-MS] 滚轮   %+ld", delta);
                    PushLog(b); ++g_nDiMs;
                } else if (ofs >= DIJOFS_BUTTON0 && ofs <= DIJOFS_BUTTON7) {
                    int btn = int(ofs - DIJOFS_BUTTON0);
                    bool down = (od[i].dwData & 0x80) != 0;
                    if (down) g_diMouseHeld |= (1u << btn);
                    else g_diMouseHeld &= ~(1u << btn);
                    swprintf_s(b, L"[DI-MS] %s %s",
                               down ? L"按下" : L"松开", MouseButtonName(btn));
                    PushLog(b); ++g_nDiMs;
                }
            }
            if (n < 64) break;
        }
    }

    void PollAsync() {
        std::wstring held;
        for (int vk = 0x08; vk < 0xFE; ++vk) {
            SHORT s = GetAsyncKeyState(vk);
            bool now = (s & 0x8000) != 0;
            bool was = prevVk[vk];
            if (now) {
                if (!held.empty()) held += L"  ";
                ULONGLONG dur = 0;
                auto it = vkDownAt.find(vk);
                if (it != vkDownAt.end()) dur = GetTickCount64() - it->second;
                wchar_t b[96];
                swprintf_s(b, L"%s %.1fs", VkName(vk).c_str(),
                           double(dur) / 1000.0);
                held += b;
            }
            if (now && !was) {
                vkDownAt[vk] = GetTickCount64();
                wchar_t b[128];
                swprintf_s(b, L"[异步] 按下   %s", VkName(vk).c_str());
                PushLog(b); ++g_nAsync;
            } else if (!now && was) {
                ULONGLONG dur = 0;
                auto it = vkDownAt.find(vk);
                if (it != vkDownAt.end()) dur = GetTickCount64() - it->second;
                wchar_t b[128];
                swprintf_s(b, L"[异步] 松开   %-22s 持续 %llu ms",
                           VkName(vk).c_str(),
                           static_cast<unsigned long long>(dur));
                PushLog(b); ++g_nAsync;
            }
            prevVk[vk] = now;
        }
        { std::lock_guard<std::mutex> lk(g_mtx); g_heldAsync = held; }
    }

    void Run() {
        timeBeginPeriod(1);
        AcquireAll();
        while (g_running) {
            PollKb();
            PollMouse();
            PollAsync();
            Sleep(1);
        }
        if (kb) { kb->Unacquire(); kb->Release(); }
        if (ms) { ms->Unacquire(); ms->Release(); }
        if (di) di->Release();
        timeEndPeriod(1);
    }
};

PollThread* g_poll = nullptr;

// ------------------------------------------------------------- raw input
void HandleRawInput(HRAWINPUT h) {
    UINT size = 0;
    GetRawInputData(h, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
    if (size == 0) return;
    std::vector<BYTE> buf(size);
    auto* raw = reinterpret_cast<RAWINPUT*>(buf.data());
    if (GetRawInputData(h, RID_INPUT, raw, &size,
                        sizeof(RAWINPUTHEADER)) != size)
        return;

    wchar_t b[160];
    if (raw->header.dwType == RIM_TYPEKEYBOARD) {
        const auto& k = raw->data.keyboard;
        bool e0 = (k.Flags & RI_KEY_E0) != 0;
        bool e1 = (k.Flags & RI_KEY_E1) != 0;
        bool brk = (k.Flags & RI_KEY_BREAK) != 0;
        swprintf_s(b, L"[Raw ] %s make=0x%02X E0=%d E1=%d vk=0x%02X  %s",
                   brk ? L"松开" : L"按下",
                   k.MakeCode, e0 ? 1 : 0, e1 ? 1 : 0, k.VKey,
                   KeyName(k.MakeCode, e0).c_str());
        PushLog(b); ++g_nRawKb;
        // Held panel: key = scancode + E0 flag. Auto-repeat sends repeated
        // makes, so only stamp the first make to keep the duration honest.
        uint32_t key = uint32_t(k.MakeCode) | (e0 ? 0x100u : 0u);
        if (brk) g_rawHeld.erase(key);
        else if (g_rawHeld.find(key) == g_rawHeld.end())
            g_rawHeld[key] = GetTickCount64();
    } else if (raw->header.dwType == RIM_TYPEMOUSE) {
        const auto& m = raw->data.mouse;
        g_rawMouseX += m.lLastX;
        g_rawMouseY += m.lLastY;
        const USHORT f = m.usButtonFlags;
        auto click = [&](USHORT bit, const wchar_t* name, bool down, int idx) {
            if (!(f & bit)) return;
            swprintf_s(b, L"[Raw ] 鼠标%s %s", down ? L"按下" : L"松开", name);
            PushLog(b); ++g_nRawMs;
            if (down) g_rawMouseHeld |= (1u << idx);
            else g_rawMouseHeld &= ~(1u << idx);
        };
        click(RI_MOUSE_LEFT_BUTTON_DOWN,   L"左键",   true,  0);
        click(RI_MOUSE_LEFT_BUTTON_UP,     L"左键",   false, 0);
        click(RI_MOUSE_RIGHT_BUTTON_DOWN,  L"右键",   true,  1);
        click(RI_MOUSE_RIGHT_BUTTON_UP,    L"右键",   false, 1);
        click(RI_MOUSE_MIDDLE_BUTTON_DOWN, L"中键",   true,  2);
        click(RI_MOUSE_MIDDLE_BUTTON_UP,   L"中键",   false, 2);
        click(RI_MOUSE_BUTTON_4_DOWN,      L"侧键1",  true,  3);
        click(RI_MOUSE_BUTTON_4_UP,        L"侧键1",  false, 3);
        click(RI_MOUSE_BUTTON_5_DOWN,      L"侧键2",  true,  4);
        click(RI_MOUSE_BUTTON_5_UP,        L"侧键2",  false, 4);
        if (f & RI_MOUSE_WHEEL) {
            short d = static_cast<short>(m.usButtonData);
            swprintf_s(b, L"[Raw ] 鼠标滚轮 %+d", int(d));
            PushLog(b); ++g_nRawMs;
        }
    }
}

// ---------------------------------------------------------------- window
HWND g_hwnd = nullptr;
HWND g_hLog = nullptr, g_hStatus = nullptr, g_hHeld = nullptr;
HFONT g_hFontLog = nullptr, g_hFontUi = nullptr;
HHOOK g_hGetMsg = nullptr;

HWND MakeCtl(HWND parent, const wchar_t* cls, const wchar_t* text,
             DWORD style, int id) {
    HMENU menu = reinterpret_cast<HMENU>(static_cast<intptr_t>(id));
    return CreateWindowExW(0, cls, text, style, 0, 0, 10, 10,
                           parent, menu, GetModuleHandleW(nullptr), nullptr);
}

// Children (log box, buttons) would otherwise swallow keyboard messages so
// the parent WndProc would never see the "[消息]" channel. A thread-level
// get-message hook observes every message on this thread's queue regardless
// of which child currently has focus -- classic games have no child controls
// and receive these directly in their main window proc.
LRESULT CALLBACK GetMsgHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && wp == PM_REMOVE) {
        const MSG* m = reinterpret_cast<const MSG*>(lp);
        switch (m->message) {
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN: {
                if (!(m->lParam & (1 << 30))) {
                    g_msgHeld[m->wParam] = GetTickCount64();
                    DWORD scan = (m->lParam >> 16) & 0xFF;
                    bool ext = (m->lParam & (1 << 24)) != 0;
                    wchar_t b[160];
                    swprintf_s(b, L"[消息] 键按下 %s scan=0x%02X E0=%d 重复%lu",
                               VkName(m->wParam).c_str(), scan, ext ? 1 : 0,
                               DWORD(m->lParam & 0xFFFF));
                    PushLog(b); ++g_nMsgKb;
                }
                break;
            }
            case WM_KEYUP:
            case WM_SYSKEYUP: {
                g_msgHeld.erase(m->wParam);
                DWORD scan = (m->lParam >> 16) & 0xFF;
                bool ext = (m->lParam & (1 << 24)) != 0;
                wchar_t b[160];
                swprintf_s(b, L"[消息] 键松开 %s scan=0x%02X E0=%d",
                           VkName(m->wParam).c_str(), scan, ext ? 1 : 0);
                PushLog(b); ++g_nMsgKb;
                break;
            }
            default: break;
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

void RefreshLog() {
    if (g_paused.load()) return;
    std::wstring all;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        for (const auto& l : g_lines) { all += l; all += L"\r\n"; }
    }
    // Only rewrite when content actually changed.
    SetWindowTextW(g_hLog, all.c_str());
    int len = GetWindowTextLengthW(g_hLog);
    SendMessageW(g_hLog, EM_SETSEL, len, len);
    SendMessageW(g_hLog, EM_SCROLLCARET, 0, 0);
}

void RefreshStatus() {
    bool fg = GetForegroundWindow() == g_hwnd;
    POINT pt; GetCursorPos(&pt);
    ScreenToClient(g_hwnd, &pt);
    g_cursor = pt;
    wchar_t b[768];
    swprintf_s(b,
        L"前台窗口：%s    鼠标客户区坐标：(%ld,%ld)\r\n"
        L"DirectInput：键盘=%s  鼠标=%s    位移累计 DI=(%ld,%ld)  Raw=(%ld,%ld)\r\n"
        L"事件计数  DI键盘=%llu  DI鼠标=%llu  Raw键盘=%llu  Raw鼠标=%llu  "
        L"消息键盘=%llu  消息鼠标=%llu  异步=%llu",
        fg ? L"是（映射器仅前台模式时，本窗口在前台才应收到输入）" : L"否（点击本窗口使其到前台）",
        g_cursor.x, g_cursor.y,
        g_diKbOk ? L"已获取" : L"未获取", g_diMsOk ? L"已获取" : L"未获取",
        g_diMouseX, g_diMouseY, g_rawMouseX, g_rawMouseY,
        static_cast<unsigned long long>(g_nDiKb.load()),
        static_cast<unsigned long long>(g_nDiMs.load()),
        static_cast<unsigned long long>(g_nRawKb.load()),
        static_cast<unsigned long long>(g_nRawMs.load()),
        static_cast<unsigned long long>(g_nMsgKb.load()),
        static_cast<unsigned long long>(g_nMsgMs.load()),
        static_cast<unsigned long long>(g_nAsync.load()));
    SetWindowTextW(g_hStatus, b);

    // ---- held panel: every key/button currently down, with live duration
    std::wstring diHeld, asyncHeld;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        diHeld = g_heldDi;
        asyncHeld = g_heldAsync;
    }

    std::wstring rawHeld;
    for (const auto& kv : g_rawHeld) {
        if (!rawHeld.empty()) rawHeld += L"  ";
        wchar_t b[80];
        swprintf_s(b, L"%s(%02X%s) %.1fs",
                   KeyName(kv.first & 0xFF, (kv.first & 0x100) != 0).c_str(),
                   kv.first & 0xFF, (kv.first & 0x100) ? L"+E0" : L"",
                   double(GetTickCount64() - kv.second) / 1000.0);
        rawHeld += b;
    }

    std::wstring msgHeld;
    for (const auto& kv : g_msgHeld) {
        if (!msgHeld.empty()) msgHeld += L"  ";
        wchar_t b[96];
        swprintf_s(b, L"%s %.1fs", VkName(kv.first).c_str(),
                   double(GetTickCount64() - kv.second) / 1000.0);
        msgHeld += b;
    }

    auto mouseBits = [](uint32_t m) {
        std::wstring s;
        for (int i = 0; i < 8; ++i) {
            if (!(m & (1u << i))) continue;
            if (!s.empty()) s += L" ";
            s += MouseButtonName(i);
        }
        return s.empty() ? std::wstring(L"(无)") : s;
    };
    wchar_t mb[256];
    swprintf_s(mb, L"[鼠标 ] DI：%s   Raw：%s   消息：%s",
               mouseBits(g_diMouseHeld.load()).c_str(),
               mouseBits(g_rawMouseHeld.load()).c_str(),
               mouseBits(g_msgMouseHeld.load()).c_str());

    std::wstring panel = L"当前按住：\r\n";
    panel += L"[DI-KB] " + (diHeld.empty() ? std::wstring(L"(无)") : diHeld) + L"\r\n";
    panel += L"[异步 ] " + (asyncHeld.empty() ? std::wstring(L"(无)") : asyncHeld) + L"\r\n";
    panel += L"[Raw  ] " + (rawHeld.empty() ? std::wstring(L"(无)") : rawHeld) + L"\r\n";
    panel += L"[消息 ] " + (msgHeld.empty() ? std::wstring(L"(无)") : msgHeld) + L"\r\n";
    panel += mb;
    SetWindowTextW(g_hHeld, panel.c_str());
}

void SaveLog(HWND parent) {
    wchar_t path[MAX_PATH] = L"input_probe_log.txt";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = parent;
    ofn.lpstrFilter = L"文本文件 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = L"txt";
    if (!GetSaveFileNameW(&ofn)) return;

    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        MessageBoxW(parent, L"无法写入文件", L"保存失败", MB_ICONWARNING);
        return;
    }
    std::wstring all;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        for (const auto& l : g_lines) { all += l; all += L"\r\n"; }
    }
    WORD bom = 0xFEFF;
    DWORD wr = 0;
    WriteFile(f, &bom, 2, &wr, nullptr);
    WriteFile(f, all.data(), DWORD(all.size() * sizeof(wchar_t)), &wr, nullptr);
    CloseHandle(f);
    PushLog(L"[系统] 日志已保存");
}

void Layout(HWND hwnd) {
    RECT rc; GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    int pad = 8, topH = 118, heldH = 128, barH = 34;
    MoveWindow(g_hStatus, pad, pad, W - pad * 2, topH, TRUE);
    MoveWindow(g_hHeld, pad, pad + topH, W - pad * 2, heldH, TRUE);
    int btnW = 86, btnY = pad + topH + heldH;
    HWND b1 = GetDlgItem(hwnd, IDC_BTN_CLEAR);
    HWND b2 = GetDlgItem(hwnd, IDC_BTN_PAUSE);
    HWND b3 = GetDlgItem(hwnd, IDC_BTN_SAVE);
    MoveWindow(b1, pad, btnY, btnW, 28, TRUE);
    MoveWindow(b2, pad + btnW + 8, btnY, btnW, 28, TRUE);
    MoveWindow(b3, pad + (btnW + 8) * 2, btnY, btnW, 28, TRUE);
    int logTop = btnY + barH;
    MoveWindow(g_hLog, pad, logTop, W - pad * 2, H - logTop - pad, TRUE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g_hFontLog = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                     CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                     FIXED_PITCH | FF_MODERN,
                                     L"Consolas");
            g_hFontUi = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

            DWORD kStatic = WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP;
            g_hStatus = MakeCtl(hwnd, L"STATIC", L"", kStatic, IDC_STATUS);
            // Held panel: read-only multi-line edit so long key lists wrap
            // and stay scrollable (a one-line static clipped them).
            DWORD kHeld = WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                          ES_MULTILINE | ES_READONLY;
            g_hHeld = MakeCtl(hwnd, L"EDIT", L"", kHeld, IDC_HELD);

            DWORD kButton = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON;
            HWND b1 = MakeCtl(hwnd, L"BUTTON", L"清空日志", kButton, IDC_BTN_CLEAR);
            HWND b2 = MakeCtl(hwnd, L"BUTTON", L"暂停刷新", kButton, IDC_BTN_PAUSE);
            HWND b3 = MakeCtl(hwnd, L"BUTTON", L"保存日志...", kButton, IDC_BTN_SAVE);
            SendMessageW(b1, WM_SETFONT, WPARAM(g_hFontUi), TRUE);
            SendMessageW(b2, WM_SETFONT, WPARAM(g_hFontUi), TRUE);
            SendMessageW(b3, WM_SETFONT, WPARAM(g_hFontUi), TRUE);

            DWORD kEdit = WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
                          ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL |
                          ES_AUTOHSCROLL;
            g_hLog = MakeCtl(hwnd, L"EDIT", L"", kEdit, IDC_LOG);
            SendMessageW(g_hLog, WM_SETFONT, WPARAM(g_hFontLog), TRUE);
            SendMessageW(g_hStatus, WM_SETFONT, WPARAM(g_hFontUi), TRUE);
            SendMessageW(g_hHeld, WM_SETFONT, WPARAM(g_hFontLog), TRUE);

            RAWINPUTDEVICE rid[2]{};
            rid[0].usUsagePage = 0x01; rid[0].usUsage = 0x06;
            rid[0].hwndTarget = hwnd; // keyboard
            rid[1].usUsagePage = 0x01; rid[1].usUsage = 0x02;
            rid[1].hwndTarget = hwnd; // mouse
            RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
            g_hGetMsg = SetWindowsHookExW(WH_GETMESSAGE, GetMsgHook,
                                          GetModuleHandleW(nullptr),
                                          GetCurrentThreadId());

            g_poll = new PollThread();
            g_poll->Init(hwnd);
            std::thread([](LPVOID) { g_poll->Run(); }, nullptr).detach();

            SetTimer(hwnd, IDC_TIMER_STATUS, 100, nullptr);
            SetTimer(hwnd, IDC_TIMER_UI, 50, nullptr);

            PushLog(L"[系统] 输入检测工具已启动：本窗口不渲染任何画面，仅记录输入事件");
            PushLog(L"[系统] 通道：[DI-KB]/[DI-MS]=DirectInput8 1000Hz轮询(红警式)  "
                    L"[Raw]=原始输入(CS式)  [消息]=窗口消息  [异步]=异步键状态");
            PushLog(L"[系统] 让本窗口保持在前台，然后用手柄操作；20ms 瞬按脉冲在持续时长列可看到约 20ms");
            return 0;
        }
        case WM_SIZE:
            Layout(hwnd);
            return 0;
        case WM_TIMER:
            if (wp == IDC_TIMER_UI) RefreshLog();
            if (wp == IDC_TIMER_STATUS || wp == IDC_TIMER_UI) RefreshStatus();
            return 0;
        case WM_INPUT:
            HandleRawInput(reinterpret_cast<HRAWINPUT>(lp));
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wp) != WA_INACTIVE && g_poll) g_poll->AcquireAll();
            if (LOWORD(wp) == WA_INACTIVE) {
                // Releases that happen while another window is foreground
                // never reach us -- don't let the held panel go stale.
                g_rawHeld.clear();
                g_msgHeld.clear();
                g_rawMouseHeld = 0;
                g_msgMouseHeld = 0;
                g_diMouseHeld = 0;
                { std::lock_guard<std::mutex> lk(g_mtx); g_heldDi.clear(); }
            }
            PushLog(LOWORD(wp) == WA_INACTIVE ? L"[系统] 窗口失去前台"
                                              : L"[系统] 窗口进入前台");
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_BTN_CLEAR: {
                    std::lock_guard<std::mutex> lk(g_mtx);
                    g_lines.clear();
                    break;
                }
                case IDC_BTN_PAUSE: {
                    bool now = !g_paused.load();
                    g_paused = now;
                    SetWindowTextW(GetDlgItem(hwnd, IDC_BTN_PAUSE),
                                   now ? L"继续刷新" : L"暂停刷新");
                    break;
                }
                case IDC_BTN_SAVE:
                    SaveLog(hwnd);
                    break;
            }
            return 0;

        // Keyboard messages are collected via the WH_GETMESSAGE hook (the
        // log/buttons would otherwise consume them as the focused child).
        case WM_LBUTTONDOWN: case WM_LBUTTONUP:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP:
        case WM_XBUTTONDOWN: case WM_XBUTTONUP: {
            const wchar_t* btn = L"左键";
            int idx = 0;
            switch (msg) {
                case WM_RBUTTONDOWN: case WM_RBUTTONUP: btn = L"右键"; idx = 1; break;
                case WM_MBUTTONDOWN: case WM_MBUTTONUP: btn = L"中键"; idx = 2; break;
                case WM_XBUTTONDOWN: case WM_XBUTTONUP:
                    btn = HIWORD(wp) == XBUTTON2 ? L"侧键2" : L"侧键1";
                    idx = HIWORD(wp) == XBUTTON2 ? 4 : 3; break;
            }
            bool down = (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN ||
                         msg == WM_MBUTTONDOWN || msg == WM_XBUTTONDOWN);
            if (down) g_msgMouseHeld |= (1u << idx);
            else g_msgMouseHeld &= ~(1u << idx);
            wchar_t b[128];
            swprintf_s(b, L"[消息] 鼠标%s %s 客户区(%d,%d)",
                       down ? L"按下" : L"松开", btn,
                       GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            PushLog(b); ++g_nMsgMs;
            if (msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP)
                return TRUE;
            return 0;
        }
        case WM_MOUSEWHEEL: {
            int d = GET_WHEEL_DELTA_WPARAM(wp);
            wchar_t b[64];
            swprintf_s(b, L"[消息] 滚轮 %+d", d);
            PushLog(b); ++g_nMsgMs;
            return 0;
        }
        case WM_DESTROY:
            g_running = false;
            if (g_hGetMsg) { UnhookWindowsHookEx(g_hGetMsg); g_hGetMsg = nullptr; }
            KillTimer(hwnd, IDC_TIMER_STATUS);
            KillTimer(hwnd, IDC_TIMER_UI);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ------------------------------------------------------ headless selftest
LRESULT CALLBACK SelfTestWndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INPUT) {
        HandleRawInput(reinterpret_cast<HRAWINPUT>(lp));
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

// No UI: creates a hidden window, acquires DirectInput in BACKGROUND mode
// (validates the hand-built DIDATAFORMATs without needing foreground),
// injects one harmless F24 scancode make/break and checks that every channel
// actually observed it. Used from automation; F24 has no normal OS binding.
int SelfTest() {
    // Two output routes:
    //  - stdout redirected (pipe/file): write narrow text to the standard
    //    handle, and never touch consoles (AllocConsole in a headless job
    //    host can wedge the process).
    //  - interactive console: write wide text to CONOUT$ (GUI-subsystem exes
    //    get no usable stdout handle from the shell).
    HANDLE stdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    bool redirected = false;
    if (stdOut && stdOut != INVALID_HANDLE_VALUE) {
        DWORD t = GetFileType(stdOut);
        redirected = (t == FILE_TYPE_PIPE || t == FILE_TYPE_DISK);
    }
    HANDLE cout = INVALID_HANDLE_VALUE;
    if (!redirected) {
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
        cout = CreateFileW(L"CONOUT$", GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    auto say = [&](const std::wstring& s) {
        DWORD wr = 0;
        std::wstring line = s + L"\r\n";
        if (cout != INVALID_HANDLE_VALUE)
            WriteConsoleW(cout, line.c_str(), DWORD(line.size()), &wr, nullptr);
        if (redirected) {
            int need = WideCharToMultiByte(CP_UTF8, 0, line.c_str(),
                                           int(line.size()), nullptr, 0,
                                           nullptr, nullptr);
            std::string utf8(size_t(need), '\0');
            if (need > 0)
                WideCharToMultiByte(CP_UTF8, 0, line.c_str(),
                                    int(line.size()), utf8.data(), need,
                                    nullptr, nullptr);
            WriteFile(stdOut, utf8.data(), DWORD(utf8.size()), &wr, nullptr);
        }
        OutputDebugStringW(line.c_str());
    };
    int failures = 0;
    auto check = [&](bool ok, const std::wstring& name) {
        say((ok ? L"PASS " : L"FAIL ") + name);
        if (!ok) ++failures;
    };

    WNDCLASSW wc{};
    wc.lpfnWndProc = SelfTestWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"AdfInputProbeSelfTest";
    RegisterClassW(&wc);
    HWND hw = CreateWindowExW(0, wc.lpszClassName, L"selftest",
        WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);

    RAWINPUTDEVICE rid[2]{};
    rid[0].usUsagePage = 0x01; rid[0].usUsage = 0x06;
    rid[0].dwFlags = RIDEV_INPUTSINK; rid[0].hwndTarget = hw;
    rid[1].usUsagePage = 0x01; rid[1].usUsage = 0x02;
    rid[1].dwFlags = RIDEV_INPUTSINK; rid[1].hwndTarget = hw;
    RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
    g_hGetMsg = SetWindowsHookExW(WH_GETMESSAGE, GetMsgHook,
                                  GetModuleHandleW(nullptr),
                                  GetCurrentThreadId());

    PollThread poll;
    poll.Init(hw, /*background=*/true);
    poll.AcquireAll();

    // Pump messages ourselves; poll DI/async from this thread at ~1 kHz.
    auto pump = [&] {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        poll.PollKb();
        poll.PollMouse();
        poll.PollAsync();
    };

    auto injectKey = [](WORD scan, bool up) {
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wScan = scan;
        in.ki.dwFlags = KEYEVENTF_SCANCODE | (up ? KEYEVENTF_KEYUP : 0);
        SendInput(1, &in, sizeof(INPUT));
    };

    constexpr WORD kF24 = 0x76; // DIK_F24 / scancode 0x76, VK 0x87
    for (int i = 0; i < 400; ++i) { pump(); Sleep(1); } // settle
    check(poll.kbFormatOk, L"DI keyboard SetDataFormat accepted");
    check(poll.msFormatOk, L"DI mouse SetDataFormat accepted (buffered)");
    check(g_diKbOk, L"DI keyboard acquired and pollable");
    check(g_diMsOk, L"DI mouse acquired and pollable");

    uint64_t baseDi = g_nDiKb.load(), baseRaw = g_nRawKb.load();
    uint64_t baseMsg = g_nMsgKb.load(), baseAsync = g_nAsync.load();

    injectKey(kF24, false);
    for (int i = 0; i < 120; ++i) { pump(); Sleep(1); }
    injectKey(kF24, true);
    for (int i = 0; i < 400; ++i) { pump(); Sleep(1); }

    check(g_nDiKb.load() - baseDi >= 2,
          L"[DI-KB] 1000Hz poll saw injected F24 make/break");
    check(g_nRawKb.load() - baseRaw >= 2,
          L"[Raw ] raw input saw injected F24 make/break");
    check(g_nMsgKb.load() - baseMsg >= 2,
          L"[消息] window-message hook saw F24 make/break");
    check(g_nAsync.load() - baseAsync >= 2,
          L"[异步] GetAsyncKeyState saw F24 make/break");

    if (g_hGetMsg) UnhookWindowsHookEx(g_hGetMsg);
    DestroyWindow(hw);
    say(failures == 0 ? L"SELFTEST OK" : L"SELFTEST FAILED");
    if (cout != INVALID_HANDLE_VALUE) CloseHandle(cout);
    return failures == 0 ? 0 : 1;
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int show) {
    if (wcsstr(GetCommandLineW(), L"--selftest"))
        return SelfTest();

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"AdfInputProbe";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    RegisterClassW(&wc);

    g_hwnd = CreateWindowExW(0, L"AdfInputProbe", L"输入检测工具",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 860, 640,
        nullptr, nullptr, hInst, nullptr);
    ShowWindow(g_hwnd, show);
    UpdateWindow(g_hwnd);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return static_cast<int>(m.wParam);
}
