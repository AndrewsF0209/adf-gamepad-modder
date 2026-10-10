using System;
using System.Linq;
using AdfConfigurator.Models;

namespace AdfConfigurator.ViewModels;

// One bindable output slot (pad button, stick direction, trigger action).
public sealed class BindingRow : ObservableObject
{
    private readonly Func<KeyActionDto> _get;
    private readonly Action<KeyActionDto> _set;

    public BindingRow(string label, Func<KeyActionDto> get, Action<KeyActionDto> set,
                      int? selfPadButton = null, bool withRadial = false)
        : this(label, get, set, selfPadButton, true, withRadial)
    {
    }

    private BindingRow(string label, Func<KeyActionDto> get, Action<KeyActionDto> set,
                       int? selfPadButton, bool withGestures, bool withRadial = false)
    {
        Label = label;
        _get = get;
        _set = set;
        SelfPadButton = selfPadButton;
        if (!withGestures) return;
        // Sub-row labels stay short on purpose: the popup's label column is
        // narrow, so a "Button · suffix" prefix would ellipsize the part
        // that actually distinguishes the rows.
        TapRow = MakeGestureRow("短按", a => a.Tap, (a, v) => a.Tap = v);
        DoubleRow = MakeGestureRow("双击", a => a.DoubleTap, (a, v) => a.DoubleTap = v);
        LongRow = MakeGestureRow("长按", a => a.LongPress, (a, v) => a.LongPress = v);
        PressRow = MakeGestureRow("按下", a => a.Press, (a, v) => a.Press = v);
        ReleaseRow = MakeGestureRow("松开", a => a.Release, (a, v) => a.Release = v);
        if (!withRadial) return;
        string[] dirLabels = { "上", "右上", "右", "右下", "下", "左下", "左", "左上" };
        var rows = new BindingRow[8];
        for (int i = 0; i < 8; i++)
        {
            int sector = i;
            rows[i] = MakeGestureRow(dirLabels[i],
                a => a.RadialAt(sector),
                (a, v) =>
                {
                    // Sectors stay flat: copy only the primary fields so a
                    // captured combination can never leak into the menu.
                    var c = a.RadialAt(sector);
                    c.Type = v.Type;
                    c.Scancode = v.Scancode;
                    c.Extended = v.Extended;
                    c.Name = v.Name;
                    c.Button = v.Button;
                    c.Wheel = v.Wheel;
                });
        }
        RadialRows = rows;
    }

    private BindingRow MakeGestureRow(string label,
                                      Func<KeyActionDto, KeyActionDto?> pick,
                                      Action<KeyActionDto, KeyActionDto> assign)
    {
        BindingRow? row = null;
        row = new BindingRow(label,
            () => pick(_get()) ?? new KeyActionDto(),
            v =>
            {
                var a = _get();
                assign(a, v);
                _set(a);
                row?.Refresh();
                RaiseGesture();
                RaiseRadial();
            }, null, false, false);
        return row;
    }

    public BindingRow? TapRow { get; }
    public BindingRow? DoubleRow { get; }
    public BindingRow? LongRow { get; }
    public BindingRow? PressRow { get; }
    public BindingRow? ReleaseRow { get; }

    // Radial menu editor (button rows only): 8 sector sub-rows + parameters.
    public IReadOnlyList<BindingRow>? RadialRows { get; }
    public bool HasRadialSupport => RadialRows != null;

    private bool _isRadialOpen;
    public bool IsRadialOpen
    {
        get => _isRadialOpen;
        set { if (_isRadialOpen != value) { _isRadialOpen = value; Raise(nameof(IsRadialOpen)); } }
    }

    public bool IsRadial
    {
        get => _get().Radial;
        set
        {
            var a = _get();
            if (a.Radial == value) return;
            a.Radial = value;
            _set(a); // persist even if nothing is bound yet
            RaiseRadial();
        }
    }

    public int RadialMs
    {
        get => Math.Clamp(_get().RadialMs, 50, 5000);
        set
        {
            var a = _get();
            value = Math.Clamp(value, 50, 5000);
            if (a.RadialMs == value) return;
            a.RadialMs = value;
            _set(a);
            Raise(nameof(RadialMs));
        }
    }

    public bool IsRadialStickRight
    {
        get => _get().RadialStick != 0;
        set
        {
            var a = _get();
            int v = value ? 1 : 0;
            if (a.RadialStick == v) return;
            a.RadialStick = v;
            _set(a);
            Raise(nameof(IsRadialStickRight));
            Raise(nameof(IsRadialStickLeft));
        }
    }

    public bool IsRadialStickLeft
    {
        get => _get().RadialStick == 0;
        set => IsRadialStickRight = !value;
    }

    // Wheel trigger mode: 0 = on button release, 1 = on stick re-centering,
    // 2 = the moment a sector is entered. The three bool properties exist
    // only to drive the popup's radio buttons.
    public int RadialTrigger
    {
        get => _get().RadialTrigger;
        set
        {
            var a = _get();
            value = Math.Clamp(value, 0, 2);
            if (a.RadialTrigger == value) return;
            a.RadialTrigger = value;
            _set(a);
            Raise(nameof(RadialTrigger));
            Raise(nameof(RadialTriggerRelease));
            Raise(nameof(RadialTriggerCenter));
            Raise(nameof(RadialTriggerSelect));
            Raise(nameof(RadialSummary));
        }
    }

    public bool RadialTriggerRelease
    {
        get => RadialTrigger == 0;
        set { if (value) RadialTrigger = 0; }
    }

    public bool RadialTriggerCenter
    {
        get => RadialTrigger == 1;
        set { if (value) RadialTrigger = 1; }
    }

    public bool RadialTriggerSelect
    {
        get => RadialTrigger == 2;
        set { if (value) RadialTrigger = 2; }
    }

    public string RadialButtonText => _get().HasRadial ? "轮盘●" : "轮盘";

    public string RadialSummary
    {
        get
        {
            var a = _get();
            if (!a.Radial) return "未启用；开启后按住该键并拨动摇杆选方向";
            var parts = new System.Collections.Generic.List<string>();
            string[] dirLabels = { "上", "右上", "右", "右下", "下", "左下", "左", "左上" };
            for (int i = 0; i < a.RadialDirs.Count && i < 8; i++)
                if (a.RadialDirs[i].IsBound)
                    parts.Add($"{dirLabels[i]} → {a.RadialDirs[i].PrimaryDisplay}");
            string mode = a.RadialTrigger switch
            {
                1 => "回中触发",
                2 => "选择触发",
                _ => "关闭触发",
            };
            return (parts.Count == 0
                ? "已启用但尚未绑定任何方向"
                : string.Join("　", parts)) + $"　[{mode}]";
        }
    }

    private void RaiseRadial()
    {
        Raise(nameof(IsRadial));
        Raise(nameof(RadialButtonText));
        Raise(nameof(RadialSummary));
    }

    private bool _isGestureOpen;
    public bool IsGestureOpen
    {
        get => _isGestureOpen;
        set { if (_isGestureOpen != value) { _isGestureOpen = value; Raise(nameof(IsGestureOpen)); } }
    }

    public int LongMs
    {
        get => Math.Clamp(_get().LongMs, 20, 10000);
        set
        {
            var a = _get();
            value = Math.Clamp(value, 20, 10000);
            if (a.LongMs == value) return;
            a.LongMs = value;
            _set(a);
            Raise(nameof(LongMs));
        }
    }

    // Edge-split timing: release pulse length / press linger after release.
    public int ReleasePulseMs
    {
        get => Math.Clamp(_get().ReleasePulseMs, 1, 10000);
        set
        {
            var a = _get();
            value = Math.Clamp(value, 1, 10000);
            if (a.ReleasePulseMs == value) return;
            a.ReleasePulseMs = value;
            _set(a);
            Raise(nameof(ReleasePulseMs));
        }
    }

    public int PressHoldMs
    {
        get => Math.Clamp(_get().PressHoldMs, 0, 10000);
        set
        {
            var a = _get();
            value = Math.Clamp(value, 0, 10000);
            if (a.PressHoldMs == value) return;
            a.PressHoldMs = value;
            _set(a);
            Raise(nameof(PressHoldMs));
        }
    }

    public int DoubleMs
    {
        get => Math.Clamp(_get().DoubleMs, 20, 2000);
        set
        {
            var a = _get();
            value = Math.Clamp(value, 20, 2000);
            if (a.DoubleMs == value) return;
            a.DoubleMs = value;
            _set(a);
            Raise(nameof(DoubleMs));
        }
    }

    public string GestureButtonText =>
        _get().HasGesture ? "手势●" : "手势";

    public string GestureSummary
    {
        get
        {
            var a = _get();
            var parts = new System.Collections.Generic.List<string>();
            if (a.Press?.IsBound == true) parts.Add($"按下 → {a.Press.Display}");
            if (a.Release?.IsBound == true) parts.Add($"松开 → {a.Release.Display}");
            if (a.Tap?.IsBound == true) parts.Add($"短按 → {a.Tap.Display}");
            if (a.DoubleTap?.IsBound == true) parts.Add($"双击 → {a.DoubleTap.Display}");
            if (a.LongPress?.IsBound == true) parts.Add($"长按 → {a.LongPress.Display}");
            return parts.Count == 0 ? "未设置手势，整键即“按住”绑定" : string.Join("　", parts);
        }
    }

    private void RaiseGesture()
    {
        Raise(nameof(GestureButtonText));
        Raise(nameof(GestureSummary));
    }

    // Re-read every property from the backing DTO (used by gesture sub-rows
    // after their own assignment, and after profile switches).
    public void Refresh()
    {
        Raise(nameof(ActionText));
        Raise(nameof(IsBound));
        Raise(nameof(IsToggle));
        Raise(nameof(InterruptText));
        Raise(nameof(InterruptSummary));
        Raise(nameof(LongMs));
        Raise(nameof(DoubleMs));
        RaiseGesture();
        Raise(nameof(RadialMs));
        Raise(nameof(IsRadialStickRight));
        Raise(nameof(IsRadialStickLeft));
        Raise(nameof(RadialTrigger));
        Raise(nameof(RadialTriggerRelease));
        Raise(nameof(RadialTriggerCenter));
        Raise(nameof(RadialTriggerSelect));
        RaiseRadial();
    }

    public string Label { get; }

    // SDL_GamepadButton ordinal for button rows; used to keep a toggle's own
    // button out of its interruptor list.
    public int? SelfPadButton { get; }

    public string ActionText => _get().Display;

    public bool IsBound => _get().Type != "none";

    public bool IsToggle
    {
        get => _get().Toggle;
        set
        {
            var a = _get();
            if (a.Toggle == value) return;
            a.Toggle = value;
            _set(a); // persist even if the slot was unbound a moment ago
            Raise(nameof(IsToggle));
            Raise(nameof(ActionText));
        }
    }

    public string InterruptText
    {
        get
        {
            var n = _get().Interrupts.Count;
            return n > 0 ? $"中断({n})" : "中断";
        }
    }

    public string InterruptSummary
    {
        get
        {
            var ids = _get().Interrupts;
            return ids.Count == 0
                ? "无"
                : string.Join("、", ids.Select(i =>
                      i >= 0 && i < PadLayout.ButtonCount
                          ? PadLayout.ButtonNames[i]
                          : $"按钮{i}"));
        }
    }

    // Returns true when the button was added, false when it was removed.
    public bool ToggleInterrupt(int padButton)
    {
        var a = _get();
        bool added;
        if (a.Interrupts.Contains(padButton))
        {
            a.Interrupts.Remove(padButton);
            added = false;
        }
        else
        {
            a.Interrupts.Add(padButton);
            added = true;
        }
        Raise(nameof(InterruptText));
        Raise(nameof(InterruptSummary));
        return added;
    }

    public void ClearInterrupts()
    {
        _get().Interrupts.Clear();
        Raise(nameof(InterruptText));
        Raise(nameof(InterruptSummary));
    }

    public void Assign(KeyActionDto action)
    {
        _set(action);
        Refresh();
        TapRow?.Refresh();
        DoubleRow?.Refresh();
        LongRow?.Refresh();
        RefreshRadialRows();
    }

    public void Clear()
    {
        _set(new KeyActionDto());
        IsGestureOpen = false;
        IsRadialOpen = false;
        Refresh();
        TapRow?.Refresh();
        DoubleRow?.Refresh();
        LongRow?.Refresh();
        RefreshRadialRows();
    }

    private void RefreshRadialRows()
    {
        if (RadialRows == null) return;
        foreach (var r in RadialRows) r.Refresh();
    }
}
