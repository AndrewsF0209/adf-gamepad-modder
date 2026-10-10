using System;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Shapes;
using System.Windows.Threading;
using AdfConfigurator.Models;
using AdfConfigurator.Sdl;
using AdfConfigurator.ViewModels;

namespace AdfConfigurator;

public partial class MainWindow : Window
{
    private readonly MainViewModel _vm;
    private readonly DispatcherTimer _timer;
    private DispatcherTimer? _escHoldTimer;
    private DispatcherTimer? _captureSettleTimer;
    private bool _escWaiting;
    private bool _padKindChosen; // user manually picked Xbox/DualSense diagram

    [DllImport("user32.dll")]
    private static extern uint MapVirtualKeyW(uint uCode, uint uMapType);

    private const uint MapkVkToVsc = 0;
    private const uint MapkVkToChar = 2;
    private const uint MapkVscToVkEx = 3;

    private const int WmKeyDown = 0x0100;
    private const int WmKeyUp = 0x0101;
    private const int WmSysKeyDown = 0x0104;
    private const int WmSysKeyUp = 0x0105;
    private const int VkEscape = 0x1B;

    public MainWindow()
    {
        InitializeComponent();
        _vm = (MainViewModel)DataContext;
        _timer = new DispatcherTimer(DispatcherPriority.Normal)
        {
            Interval = TimeSpan.FromMilliseconds(33),
        };
        _timer.Tick += (_, _) => OnTimer();
        _timer.Start();
        Loaded += (_, _) => UpdateStickGroupVisibility();
        _vm.ConnectedPads.CollectionChanged += (_, e) =>
        {
            if (_padKindChosen || e.NewItems == null || e.NewItems.Count == 0) return;
            if (e.NewItems[0] is PadDevice d)
                Dispatcher.BeginInvoke(new Action(() => AutoSelectPadKind(d.Name)));
        };
        Closing += (_, _) =>
        {
            _timer.Stop();
            _escHoldTimer?.Stop();
            _vm.OnWindowClosing();
            _captureSettleTimer?.Stop();
            if (_llHook != IntPtr.Zero) UnhookWindowsHookEx(_llHook);
            if (_mouseHook != IntPtr.Zero) UnhookWindowsHookEx(_mouseHook);
            _vm.DisposePad();
        };
    }

    protected override void OnSourceInitialized(EventArgs e)
    {
        base.OnSourceInitialized(e);
        // A low-level hook is required: WPF preprocesses arrow keys for focus
        // navigation, so they never reach a normal HwndSource hook.
        _llProc = LowLevelKeyCallback;
        _llHook = SetWindowsHookExW(WhKeyboardLl, _llProc, IntPtr.Zero, 0);
        // Same for mouse buttons: the engine's release-action pulse (e.g. an
        // injected left click) only lasts ~20 ms, the linger render makes it
        // visible; the global hook also captures injected button events.
        _mouseProc = LowLevelMouseCallback;
        _mouseHook = SetWindowsHookExW(WhMouseLl, _mouseProc, IntPtr.Zero, 0);
    }

    private void PadKindChanged(object sender, RoutedEventArgs e)
    {
        if (DiagramXbox == null || DiagramDs == null) return; // pre-init XAML event
        _padKindChosen = true;
        bool ds = ReferenceEquals(sender, PadKindDs);
        DiagramDs.Visibility = ds ? Visibility.Visible : Visibility.Collapsed;
        DiagramXbox.Visibility = ds ? Visibility.Collapsed : Visibility.Visible;
    }

    private static bool IsPlayStationName(string name)
    {
        var n = (name ?? string.Empty).ToLowerInvariant();
        return n.Contains("dualsense") || n.Contains("dualshock")
            || n.Contains("playstation") || n.Contains("wireless controller");
    }

    private void AutoSelectPadKind(string name)
    {
        if (IsPlayStationName(name)) PadKindDs.IsChecked = true;
        else PadKindXbox.IsChecked = true;
        // Automatic preselection must not lock out future auto-detection:
        // only an actual user click flips _padKindChosen.
        _padKindChosen = false;
    }

    private const int WhKeyboardLl = 13;
    private const int WhMouseLl = 14;
    private const int HcAction = 0;
    private const uint LlkhfExtended = 0x01;

    private const int WmLButtonDown = 0x0201, WmLButtonUp = 0x0202;
    private const int WmRButtonDown = 0x0204, WmRButtonUp = 0x0205;
    private const int WmMButtonDown = 0x0207, WmMButtonUp = 0x0208;
    private const int WmXButtonDown = 0x020B, WmXButtonUp = 0x020C;

    private IntPtr _llHook;
    private LowLevelKeyboardProc? _llProc;
    private IntPtr _mouseHook;
    private LowLevelMouseProc? _mouseProc;

    private delegate IntPtr LowLevelKeyboardProc(int nCode, IntPtr wParam, IntPtr lParam);
    private delegate IntPtr LowLevelMouseProc(int nCode, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern IntPtr SetWindowsHookExW(int idHook, LowLevelKeyboardProc lpfn,
                                                   IntPtr hMod, uint dwThreadId);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern IntPtr SetWindowsHookExW(int idHook, LowLevelMouseProc lpfn,
                                                   IntPtr hMod, uint dwThreadId);

    [DllImport("user32.dll")]
    private static extern bool UnhookWindowsHookEx(IntPtr hhk);

    [DllImport("user32.dll")]
    private static extern IntPtr CallNextHookEx(IntPtr hhk, int nCode,
                                                IntPtr wParam, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)]
    private struct KeyboardLlHookStruct
    {
        public int VkCode;
        public int ScanCode;
        public uint Flags;
        public int Time;
        public IntPtr ExtraInfo;
    }

    private IntPtr LowLevelKeyCallback(int nCode, IntPtr wParam, IntPtr lParam)
    {
        if (nCode == HcAction)
        {
            int msg = (int)(long)wParam;
            bool up = msg is WmKeyUp or WmSysKeyUp;
            var k = Marshal.PtrToStructure<KeyboardLlHookStruct>(lParam);
            // Engine-injected keys use KEYEVENTF_SCANCODE, which arrives at
            // the hook with vkCode = 0 — resolve the VK from the scancode.
            int vk = k.VkCode;
            if (vk == 0)
            {
                uint vsc = (uint)k.ScanCode |
                           ((k.Flags & LlkhfExtended) != 0 ? 0xE000u : 0u);
                vk = (int)MapVirtualKeyW(vsc, MapkVscToVkEx);
            }
            MonitorSet(vk, VkName(vk), !up);
            if (_vm.IsCapturing)
            {
                HandleRawKey(k.VkCode, k.ScanCode, (k.Flags & LlkhfExtended) != 0, up);
                return (IntPtr)1; // swallow every key while capturing
            }
        }
        return CallNextHookEx(_llHook, nCode, wParam, lParam);
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct MousePoint
    {
        public int X;
        public int Y;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct MouseLlHookStruct
    {
        public MousePoint Pt;
        public uint MouseData;
        public uint Flags;
        public uint Time;
        public IntPtr ExtraInfo;
    }

    private IntPtr LowLevelMouseCallback(int nCode, IntPtr wParam, IntPtr lParam)
    {
        if (nCode == HcAction)
        {
            int msg = (int)(long)wParam;
            int id;
            bool up;
            switch (msg)
            {
                case WmLButtonDown: case WmLButtonUp:
                    id = MonMouseBase; up = msg == WmLButtonUp; break;
                case WmRButtonDown: case WmRButtonUp:
                    id = MonMouseBase + 1; up = msg == WmRButtonUp; break;
                case WmMButtonDown: case WmMButtonUp:
                    id = MonMouseBase + 2; up = msg == WmMButtonUp; break;
                case WmXButtonDown: case WmXButtonUp:
                {
                    var m = Marshal.PtrToStructure<MouseLlHookStruct>(lParam);
                    id = MonMouseBase + (((m.MouseData >> 16) & 0xFFFF) == 2 ? 4 : 3);
                    up = msg == WmXButtonUp;
                    break;
                }
                default:
                    return CallNextHookEx(_mouseHook, nCode, wParam, lParam);
            }
            MonitorSet(id, MouseName(id), !up);
        }
        return CallNextHookEx(_mouseHook, nCode, wParam, lParam);
    }

    private static string MouseName(int id) => (id - MonMouseBase) switch
    {
        0 => "鼠标左键",
        1 => "鼠标右键",
        2 => "鼠标中键",
        3 => "鼠标侧键1",
        _ => "鼠标侧键2",
    };

    // ---- live held-keys monitor (diagram bottom-right) ----------------------
    // The global low-level hooks also see events injected by the mapping
    // engine (SendInput), so this shows exactly what the game receives —
    // keyboard AND mouse buttons, multiple at once, each with its own state.
    private const int MonMouseBase = 256; // monitor ids for mouse buttons

    private sealed class MonitorKey
    {
        public int Id;
        public string Name = "";
        public bool Held;                    // currently physically held
        public DateTime ReleasedAt;          // UtcNow of the release edge
    }

    // One independent entry per key (never overwrite a shared string —
    // composites like Ctrl+Shift+LMB must coexist, each lingering on its own).
    private readonly List<MonitorKey> _monKeys = new();
    private DispatcherTimer? _monTimer;
    private static readonly TimeSpan MonLinger = TimeSpan.FromMilliseconds(800);

    private void MonitorSet(int id, string name, bool held)
    {
        if (HeldKeysText == null || id < 0) return;
        try
        {
            var k = _monKeys.FirstOrDefault(x => x.Id == id);
            if (held)
            {
                if (k == null)
                {
                    k = new MonitorKey { Id = id, Name = name };
                    _monKeys.Add(k);
                }
                k.Held = true;
                k.Name = name;
            }
            else
            {
                if (k == null || !k.Held) return; // stray release / auto-repeat
                k.Held = false;
                k.ReleasedAt = DateTime.UtcNow;
                EnsureMonTimer();
            }
            RenderMonitor();
        }
        catch (Exception ex)
        {
            // A hook callback must never throw: Windows silently removes a
            // misbehaving low-level hook, killing all monitoring.
            System.Diagnostics.Debug.WriteLine("MonitorSet: " + ex);
        }
    }

    private void EnsureMonTimer()
    {
        if (_monTimer != null) return;
        _monTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(40) };
        _monTimer.Tick += (_, _) => { try { RenderMonitor(); } catch { } };
        _monTimer.Start();
    }

    private void RenderMonitor()
    {
        if (HeldKeysText == null) return;
        var now = DateTime.UtcNow;
        _monKeys.RemoveAll(k => !k.Held && now - k.ReleasedAt >= MonLinger);
        HeldKeysText.Inlines.Clear();
        HeldKeysText.Inlines.Add(new Run("按键：") { Foreground = Brushes.DimGray });
        if (_monKeys.Count == 0)
        {
            _monTimer?.Stop();
            HeldKeysText.Inlines.Add(new Run("—") { Foreground = Brushes.DarkGray });
            System.Windows.Automation.AutomationProperties.SetName(
                HeldKeysText, "按键：—");
            return;
        }
        // Re-render the composite: held keys black, released-but-lingering
        // keys grey, in press order.
        var plain = new System.Text.StringBuilder("按键：");
        bool first = true;
        foreach (var k in _monKeys)
        {
            if (!first)
            {
                HeldKeysText.Inlines.Add(new Run(" + ") { Foreground = Brushes.Gray });
                plain.Append(" + ");
            }
            first = false;
            HeldKeysText.Inlines.Add(new Run(k.Name)
            {
                Foreground = k.Held ? Brushes.Black : Brushes.DarkGray
            });
            plain.Append(k.Name);
        }
        // Mirror the visible text onto the automation name (Inlines are not
        // always aggregated into the default UIA name).
        System.Windows.Automation.AutomationProperties.SetName(
            HeldKeysText, plain.ToString());
    }

    private static string VkName(int vk)
    {
        var key = KeyInterop.KeyFromVirtualKey(vk);
        return key == Key.None ? $"0x{vk:X2}" : key.ToString();
    }

    private void OnTimer()
    {
        _vm.TickPad();
        UpdateStickGroupVisibility();
        if (LeftCurveCanvas != null)
            DrawCurve(LeftCurveCanvas, _vm.Current.LeftStick.Mouse);
        if (RightCurveCanvas != null)
            DrawCurve(RightCurveCanvas, _vm.Current.RightStick.Mouse);
    }

    // ---- keyboard capture ---------------------------------------------------
    // Runs on the WH_KEYBOARD_LL callback. The low-level hook gives the real
    // hardware scancode + E0 extended flag, which is the only way to tell an
    // arrow key (E0 48..) from the numpad key sharing its base scancode.
    private void HandleRawKey(int vk, int scan, bool extended, bool up)
    {
        // Esc: short press = bind Esc, hold >= 600ms = cancel capture.
        if (vk == VkEscape)
        {
            if (up)
            {
                if (_escWaiting)
                {
                    _escWaiting = false;
                    _escHoldTimer?.Stop();
                    _captureSettleTimer?.Stop();
                    _vm.ApplyCaptured(new KeyActionDto
                    {
                        Type = "key",
                        Scancode = 0x01,
                        Extended = false,
                        Name = "Esc",
                    });
                }
                return;
            }

            if (_escWaiting) return; // auto-repeat while held

            _escWaiting = true;
            _escHoldTimer ??= new DispatcherTimer(DispatcherPriority.Normal)
            {
                Interval = TimeSpan.FromMilliseconds(600),
            };
            _escHoldTimer.Tick -= EscHoldTick;
            _escHoldTimer.Tick += EscHoldTick;
            _escHoldTimer.Start();
            return;
        }

        if (scan == 0)
            scan = (int)(MapVirtualKeyW((uint)vk, MapkVkToVsc) & 0xFF);
        if (scan == 0) return;

        var action = BuildKeyDto(scan, extended, vk);
        if (up)
        {
            _vm.ReleaseCapturePart(action);
            ArmCaptureSettle();
        }
        else
        {
            _escWaiting = false;
            _escHoldTimer?.Stop();
            _vm.AddCapturePart(action); // auto-repeat ignored via held set
            ArmCaptureSettle();
        }
    }

    private static KeyActionDto BuildKeyDto(int scan, bool extended, int vk)
    {
        string name = ScanNames.Name(scan, extended);
        if (string.IsNullOrEmpty(name))
        {
            uint ch = MapVirtualKeyW((uint)vk, MapkVkToChar);
            name = ch != 0 && (ch & 0x7F) >= 0x20
                ? ((char)(ch & 0x7F)).ToString().ToUpperInvariant()
                : $"0x{scan:X2}";
        }
        return new KeyActionDto
        {
            Type = "key",
            Scancode = scan,
            Extended = extended,
            Name = name,
        };
    }

    // After every press/release, wait briefly: when the user releases all
    // keys of a chord the combination commits; a new press before the timeout
    // simply re-arms it (used for Ctrl then C sequences).
    private void ArmCaptureSettle()
    {
        _captureSettleTimer ??= new DispatcherTimer(DispatcherPriority.Normal)
        {
            Interval = TimeSpan.FromMilliseconds(300),
        };
        _captureSettleTimer.Tick -= CaptureSettleTick;
        _captureSettleTimer.Tick += CaptureSettleTick;
        _captureSettleTimer.Stop();
        _captureSettleTimer.Start();
    }

    private void CaptureSettleTick(object? sender, EventArgs e)
    {
        _captureSettleTimer!.Stop();
        if (_vm.IsCapturing && _vm.CaptureHeldCount == 0)
            _vm.CommitCaptureNow();
    }

    private void EscHoldTick(object? sender, EventArgs e)
    {
        _escHoldTimer!.Stop();
        _captureSettleTimer?.Stop();
        _escWaiting = false;
        if (_vm.IsCapturing)
            _vm.CancelCapture.Execute(null);
    }

    // ---- mouse capture ------------------------------------------------------
    private int MouseButtonId(MouseButton b) => b switch
    {
        MouseButton.Left => 1,
        MouseButton.Right => 2,
        MouseButton.Middle => 3,
        MouseButton.XButton1 => 4,
        MouseButton.XButton2 => 5,
        _ => 0,
    };

    private void Window_PreviewMouseDown(object sender, MouseButtonEventArgs e)
    {
        if (!_vm.IsCapturing) return;
        // Let real buttons (e.g. the capture banner's 取消/完成) receive the click.
        if (e.OriginalSource is DependencyObject src &&
            FindParent<ButtonBase>(src) != null)
        {
            return;
        }

        e.Handled = true;
        int button = MouseButtonId(e.ChangedButton);
        if (button == 0) return;
        _vm.AddCapturePart(new KeyActionDto { Type = "mouse", Button = button });
        ArmCaptureSettle();
    }

    private void Window_PreviewMouseUp(object sender, MouseButtonEventArgs e)
    {
        if (!_vm.IsCapturing) return;
        int button = MouseButtonId(e.ChangedButton);
        if (button == 0) return;
        e.Handled = true;
        _vm.ReleaseCapturePart(new KeyActionDto { Type = "mouse", Button = button });
        ArmCaptureSettle();
    }

    private static T? FindParent<T>(DependencyObject? start) where T : DependencyObject
    {
        var node = start;
        while (node != null)
        {
            if (node is T match) return match;
            node = VisualTreeHelper.GetParent(node);
        }
        return null;
    }

    private void Window_PreviewMouseWheel(object sender, MouseWheelEventArgs e)
    {
        if (!_vm.IsCapturing) return;
        // Avoid hijacking wheel scrolling over actual controls.
        if (e.OriginalSource is DependencyObject src &&
            FindParent<ButtonBase>(src) != null)
        {
            return;
        }

        e.Handled = true;
        _vm.ApplyCaptured(new KeyActionDto
        {
            Type = "wheel",
            Wheel = e.Delta >= 0 ? 120 : -120,
        });
    }

    // ---- stick mode panels --------------------------------------------------
    private void LeftModeChanged(object sender, SelectionChangedEventArgs e) =>
        UpdateStickGroupVisibility();

    private void RightModeChanged(object sender, SelectionChangedEventArgs e) =>
        UpdateStickGroupVisibility();

    private void LayerLeftModeChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_vm == null) return;
        _vm.SelectedLayer.LeftStick.Keys.Enabled = _vm.SelectedLayer.LeftStick.Mode == 0;
        _vm.SelectedLayer.LeftStick.Mouse.Enabled = _vm.SelectedLayer.LeftStick.Mode == 1;
    }

    private void LayerRightModeChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_vm == null) return;
        _vm.SelectedLayer.RightStick.Keys.Enabled = _vm.SelectedLayer.RightStick.Mode == 0;
        _vm.SelectedLayer.RightStick.Mouse.Enabled = _vm.SelectedLayer.RightStick.Mode == 1;
    }

    private void UpdateStickGroupVisibility()
    {
        if (!IsLoaded) return;
        if (LeftKeysGroup != null && LeftMouseGroup != null)
        {
            LeftKeysGroup.Visibility =
                _vm.Current.LeftStick.Mode == 0 ? Visibility.Visible : Visibility.Collapsed;
            LeftMouseGroup.Visibility =
                _vm.Current.LeftStick.Mode == 1 ? Visibility.Visible : Visibility.Collapsed;
        }
        if (RightKeysGroup != null && RightMouseGroup != null)
        {
            RightKeysGroup.Visibility =
                _vm.Current.RightStick.Mode == 0 ? Visibility.Visible : Visibility.Collapsed;
            RightMouseGroup.Visibility =
                _vm.Current.RightStick.Mode == 1 ? Visibility.Visible : Visibility.Collapsed;
        }
    }

    // ---- response curve preview ---------------------------------------------
    private static void DrawCurve(System.Windows.Controls.Canvas canvas, StickMouseDto m)
    {
        double w = canvas.ActualWidth > 10 ? canvas.ActualWidth : 300;
        double h = canvas.ActualHeight > 10 ? canvas.ActualHeight : 160;
        const double pad = 18;

        canvas.Children.Clear();

        double X(double raw) => pad + raw * (w - 2 * pad);
        double Y(double v) => h - pad - v * (h - 2 * pad);

        // Grid / axes
        canvas.Children.Add(new Line
        {
            X1 = pad, Y1 = h - pad, X2 = w - pad, Y2 = h - pad,
            Stroke = Brushes.Gray,
        });
        canvas.Children.Add(new Line
        {
            X1 = pad, Y1 = pad, X2 = pad, Y2 = h - pad,
            Stroke = Brushes.Gray,
        });

        // Deadzone shading
        double dzX = X(Math.Min(m.Deadzone, 0.99));
        var shade = new Rectangle
        {
            Width = dzX - pad,
            Height = h - 2 * pad,
            Fill = new SolidColorBrush(Color.FromArgb(40, 220, 60, 60)),
        };
        Canvas.SetLeft(shade, pad);
        Canvas.SetTop(shade, pad);
        canvas.Children.Add(shade);

        // Curve: remap after inner deadzone (span also reduced by outer),
        // then response exponent - mirrors curves.cpp + mapper.cpp.
        double span = Math.Max(0.0001, 1.0 - m.Deadzone -
                                           Math.Clamp(m.OuterDeadzone, 0, 0.9));
        double exponent = m.Curve switch { 1 => 2.0, 2 => 3.0, _ => 1.0 };
        double Fn(double raw)
        {
            if (raw <= m.Deadzone) return 0;
            double f = Math.Clamp((raw - m.Deadzone) / span, 0, 1);
            return Math.Pow(f, exponent);
        }

        var poly = new Polyline { Stroke = Brushes.RoyalBlue, StrokeThickness = 2 };
        for (int i = 0; i <= 100; i++)
        {
            double raw = i / 100.0;
            poly.Points.Add(new Point(X(raw), Y(Fn(raw))));
        }
        canvas.Children.Add(poly);

        // Edge threshold marker
        if (m.EdgeThreshold is > 0 and < 1)
        {
            canvas.Children.Add(new Line
            {
                X1 = X(m.EdgeThreshold), Y1 = pad,
                X2 = X(m.EdgeThreshold), Y2 = h - pad,
                Stroke = Brushes.DarkOrange,
                StrokeDashArray = new DoubleCollection { 3, 2 },
            });
        }
    }
}
