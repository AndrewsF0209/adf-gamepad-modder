using System;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Windows;
using System.Windows.Input;
using AdfConfigurator.Deploy;
using AdfConfigurator.Models;
using AdfConfigurator.Pe;
using AdfConfigurator.Sdl;
using Microsoft.Win32;

namespace AdfConfigurator.ViewModels;

public sealed class MainViewModel : ObservableObject
{
    private readonly PadMonitor? _pad;
    private ConfigDto _config = new();
    private int _currentIndex;
    private PeInfo? _pe;
    private DeployKind _deployKind = DeployKind.Launcher;
    private BindingRow? _pending;
    private BindingRow? _interruptRow;
    private PadDevice? _selectedLivePad;
    private bool[] _lastButtons = new bool[32];

    public MainViewModel()
    {
        try
        {
            _pad = new PadMonitor();
        }
        catch (DllNotFoundException)
        {
            Status = "未找到 SDL3.dll：请先编译原生组件（手柄测试不可用，其余功能正常）";
        }
        catch (Exception ex)
        {
            Status = $"手柄后端不可用：{ex.Message}";
        }

        _config.Devices.Add(DeviceConfigDto.CreateDefault());
        Profiles = new ObservableCollection<DeviceConfigDto>(_config.Devices);
        RebuildRows();
        TryAutoLoadLast();

        BrowseGame = new RelayCommand(BrowseGameExecutable);
        OpenJson = new RelayCommand(OpenJsonFile);
        SaveJson = new RelayCommand(SaveJsonFile);
        Deploy = new RelayCommand(DeployNow, () => _pe != null);
        NewProfile = new RelayCommand(CreateProfileForSelectedPad,
                                      () => _selectedLivePad != null);
        RemoveProfile = new RelayCommand(RemoveCurrentProfile,
                                         () => Profiles.Count > 1);
        StartCapture = new RelayCommand(p => BeginCapture(p as BindingRow));
        CancelCapture = new RelayCommand(CancelCaptureNow);
        CommitCapture = new RelayCommand(CommitCaptureNow);
        ClearBinding = new RelayCommand(p => (p as BindingRow)?.Clear());
        StartInterrupts = new RelayCommand(p => BeginInterrupts(p as BindingRow));
        FinishInterrupts = new RelayCommand(() => InterruptRow = null);
        ClearInterrupts = new RelayCommand(p => (p as BindingRow)?.ClearInterrupts());
        ToggleMapping = new RelayCommand(_ => ToggleMappingNow());
    }

    // ---- game / deployment -------------------------------------------------
    public ConfigDto Config => _config;
    public PeInfo? Pe => _pe;

    private string _gamePath = "";
    public string GamePath
    {
        get => _gamePath;
        set => SetField(ref _gamePath, value);
    }

    private string _gameInfo = "尚未选择游戏 exe";
    public string GameInfo
    {
        get => _gameInfo;
        set => SetField(ref _gameInfo, value);
    }

    public DeployKind[] DeployKinds { get; } =
        { DeployKind.Launcher, DeployKind.Auto, DeployKind.Winmm,
          DeployKind.Version, DeployKind.Dinput8 };

    public DeployKind DeployKind
    {
        get => _deployKind;
        set => SetField(ref _deployKind, value);
    }

    public ICommand BrowseGame { get; }
    public ICommand OpenJson { get; }
    public ICommand SaveJson { get; }
    public ICommand Deploy { get; }
    public ICommand NewProfile { get; }
    public ICommand RemoveProfile { get; }
    public ICommand StartCapture { get; }
    public ICommand CancelCapture { get; }
    public ICommand CommitCapture { get; }
    public ICommand ClearBinding { get; }
    public ICommand StartInterrupts { get; }
    public ICommand FinishInterrupts { get; }
    public ICommand ClearInterrupts { get; }

    // ---- device profiles ---------------------------------------------------
    public ObservableCollection<DeviceConfigDto> Profiles { get; }

    public int CurrentIndex
    {
        get => _currentIndex;
        set
        {
            if (SetField(ref _currentIndex, value))
            {
                RebuildRows();
                Raise(nameof(Current));
                Raise(nameof(SelectedLayer));
                Raise(nameof(SelectedLayerTriggerIndex));
                Raise(nameof(LeftModIndex));
                Raise(nameof(RightModIndex));
                Raise(nameof(LayerLeftModIndex));
                Raise(nameof(LayerRightModIndex));
            }
        }
    }

    public DeviceConfigDto Current => Profiles[_currentIndex];

    public ObservableCollection<BindingRow> ButtonRows { get; } = new();
    public ObservableCollection<BindingRow> VisibleButtonRows { get; } = new();
    public ObservableCollection<BindingRow> LeftDirRows { get; } = new();
    public ObservableCollection<BindingRow> RightDirRows { get; } = new();
    public BindingRow? LeftTriggerRow { get; private set; }
    public BindingRow? RightTriggerRow { get; private set; }

    public ObservableCollection<PadDevice> ConnectedPads { get; } = new();

    public PadDevice? SelectedLivePad
    {
        get => _selectedLivePad;
        set
        {
            if (SetField(ref _selectedLivePad, value))
                ((RelayCommand)NewProfile).RaiseCanExecuteChanged();
        }
    }

    // ---- capture state -----------------------------------------------------
    public BindingRow? Pending
    {
        get => _pending;
        private set
        {
            if (SetField(ref _pending, value))
                Raise(nameof(IsCapturing));
        }
    }

    public bool IsCapturing => _pending != null;

    private string _capturePrompt = "";
    public string CapturePrompt
    {
        get => IsCapturing
            ? $"正在为「{_pending?.Label}」捕获：可依次按多个键组成组合（如先 Ctrl 再 C），全部松开后自动完成，也可点右侧“完成”；短按 Esc 绑定 Esc，长按 Esc 取消。当前：{CapturePreview}"
            : "";
        private set => SetField(ref _capturePrompt, value);
    }

    // ---- interruptor learning ----------------------------------------------
    public BindingRow? InterruptRow
    {
        get => _interruptRow;
        private set
        {
            if (SetField(ref _interruptRow, value))
            {
                Raise(nameof(IsCapturingInterrupts));
                Raise(nameof(InterruptPrompt));
            }
        }
    }

    public bool IsCapturingInterrupts => _interruptRow != null;

    public string InterruptPrompt => _interruptRow == null
        ? ""
        : $"正在为「{_interruptRow.Label}」设置中断键：按下手柄按钮即可加入，再按一次移除；当前：{_interruptRow.InterruptSummary}";

    private void CancelCaptureNow()
    {
        Pending = null;
        _captureParts.Clear();
        _captureHeld.Clear();
    }

    private void BeginInterrupts(BindingRow? row)
    {
        if (row == null) return;
        CancelCaptureNow(); // leave keyboard capture if it was active
        InterruptRow = row;
    }

    private string _status = "";
    public string Status
    {
        get => _status;
        set => SetField(ref _status, value);
    }

    // ---- resident mapper (常驻映射) ----------------------------------------
    // Runs the full native engine inside this process so gamepad input is
    // mapped to key/mouse output in any application — DS4Windows style.
    public ICommand ToggleMapping { get; }

    private bool _isMappingActive;
    public bool IsMappingActive
    {
        get => _isMappingActive;
        private set
        {
            if (SetField(ref _isMappingActive, value))
            {
                Raise(nameof(MappingButtonText));
                Raise(nameof(MappingButtonColor));
            }
        }
    }

    public string MappingButtonText => IsMappingActive ? "■ 停止映射" : "▶ 开始映射";
    public string MappingButtonColor => IsMappingActive ? "#E04040" : "#2D8C4A";

    private void ToggleMappingNow()
    {
        // Guard against a classic footgun: the configurator starts with a
        // blank "Default" profile until a JSON is opened. Starting the
        // resident engine with it maps nothing and looks like "mapping is
        // dead".
        bool blank = _config.Devices.All(d =>
            d.Name == "Default" && string.IsNullOrEmpty(d.Guid));
        if (blank)
        {
            Status = "当前是空白默认档案——请先点右上角「打开已有 JSON」载入配置，再启动常驻映射";
            return;
        }

        if (IsMappingActive)
        {
            ResidentMapper.Stop();
            IsMappingActive = false;
            Status = "已停止常驻映射";
            return;
        }

        // Save current config to a temp file, then start the engine.
        string tmp = Path.Combine(Path.GetTempPath(), "adf_resident.json");
        try
        {
            SaveConfig(tmp);
        }
        catch (Exception ex)
        {
            Status = $"保存临时配置失败：{ex.Message}";
            return;
        }

        int rc = ResidentMapper.Start(tmp);
        if (rc == 0)
        {
            IsMappingActive = true;
            Status = "常驻映射已启动——手柄输入将对所有应用生效（配置中的 foregroundOnly 已忽略）";
        }
        else
        {
            string err = ResidentMapper.LastError();
            Status = rc == 1 ? "映射已在运行" : $"启动失败：{err}";
        }
    }

    public void OnWindowClosing()
    {
        if (IsMappingActive)
            ResidentMapper.Stop();
    }

    private void BeginCapture(BindingRow? row)
    {
        if (row == null) return;
        _captureParts.Clear();
        _captureHeld.Clear();
        Pending = row;
        CapturePrompt = "";
        Raise(nameof(CapturePrompt));
        Raise(nameof(CapturePreview));
    }

    // ---- combination capture -----------------------------------------------
    private readonly List<KeyActionDto> _captureParts = new();
    private readonly HashSet<string> _captureHeld = new();

    public int CaptureHeldCount => _captureHeld.Count;

    public string CapturePreview =>
        _captureParts.Count == 0
            ? "（等待按键）"
            : string.Join("  +  ", _captureParts.Select(p => p.PrimaryDisplay));

    // Key/mouse down while capturing. Auto-repeat presses are ignored.
    public void AddCapturePart(KeyActionDto action)
    {
        _captureHeld.Add(action.PartId);
        if (_captureParts.Any(p => p.PartId == action.PartId)) return;
        _captureParts.Add(action);
        Raise(nameof(CapturePrompt));
        Raise(nameof(CapturePreview));
    }

    public void ReleaseCapturePart(KeyActionDto action)
    {
        _captureHeld.Remove(action.PartId);
    }

    // Build primary + chord from the recorded press order and assign it.
    public void CommitCaptureNow()
    {
        if (_pending == null) return;
        if (_captureParts.Count > 0)
        {
            var composite = _captureParts[0];
            composite.Chord = _captureParts.Skip(1).ToList();
            _pending.Assign(composite);
        }
        Pending = null;
        _captureParts.Clear();
        _captureHeld.Clear();
    }

    public void ApplyCaptured(KeyActionDto action)
    {
        if (_pending == null) return;
        _pending.Assign(action);
        Pending = null;
        _captureParts.Clear();
        _captureHeld.Clear();
        Raise(nameof(CapturePrompt));
    }

    // ---- live pad test -----------------------------------------------------
    private float _padLx, _padLy, _padRx, _padRy, _padLt, _padRt;
    public float PadLx { get => _padLx; private set { if (SetField(ref _padLx, value)) Raise(nameof(PadLxText)); } }
    public float PadLy { get => _padLy; private set { if (SetField(ref _padLy, value)) Raise(nameof(PadLyText)); } }
    public float PadRx { get => _padRx; private set { if (SetField(ref _padRx, value)) Raise(nameof(PadRxText)); } }
    public float PadRy { get => _padRy; private set { if (SetField(ref _padRy, value)) Raise(nameof(PadRyText)); } }
    public float PadLt { get => _padLt; private set { if (SetField(ref _padLt, value)) Raise(nameof(PadLtText)); } }
    public float PadRt { get => _padRt; private set { if (SetField(ref _padRt, value)) Raise(nameof(PadRtText)); } }
    public string PadLxText => _padLx.ToString("0.00");
    public string PadLyText => _padLy.ToString("0.00");
    public string PadRxText => _padRx.ToString("0.00");
    public string PadRyText => _padRy.ToString("0.00");
    public string PadLtText => _padLt.ToString("0.00");
    public string PadRtText => _padRt.ToString("0.00");

    private string _pressedText = "";
    public string PressedText
    {
        get => _pressedText;
        private set => SetField(ref _pressedText, value);
    }

    public void DisposePad() => _pad?.Dispose();

    private bool _sdlErrorShown;

    public void TickPad()
    {
        if (_pad == null) return;
        if (!_pad.IsAvailable)
        {
            if (!_sdlErrorShown && _pad.InitError != null)
            {
                Status = $"SDL 初始化失败：{_pad.InitError}（手柄测试不可用，其余功能正常）";
                _sdlErrorShown = true;
            }
            return;
        }

        var devices = _pad.GetDevices();
        if (devices.Count != ConnectedPads.Count ||
            devices.Any(d => !ConnectedPads.Any(c => c.Guid == d.Guid)))
        {
            ConnectedPads.Clear();
            foreach (var d in devices) ConnectedPads.Add(d);
            SelectedLivePad ??= devices.FirstOrDefault();
        }

        _pad.PreferGuid = SelectedLivePad?.Guid ?? Current.Guid;
        var s = _pad.Snapshot();
        if (!s.Connected)
        {
            Array.Clear(_lastButtons);
            return;
        }

        PadLx = s.Lx; PadLy = s.Ly; PadRx = s.Rx; PadRy = s.Ry;
        PadLt = s.Lt; PadRt = s.Rt;

        // Learn latch interruptors from physical pad-button rising edges.
        if (_interruptRow != null)
        {
            for (int i = 0; i < s.Buttons.Length && i < _lastButtons.Length; i++)
            {
                if (!s.Buttons[i] || _lastButtons[i]) continue;
                if (_interruptRow.SelfPadButton is int self && self == i)
                {
                    Status = "不能把切换键自身设为中断键";
                    continue;
                }
                bool added = _interruptRow.ToggleInterrupt(i);
                Status = added
                    ? $"已加入中断键：{PadLayout.ButtonNames[i]}"
                    : $"已移除中断键：{PadLayout.ButtonNames[i]}";
                Raise(nameof(InterruptPrompt));
            }
        }

        var pressed = Enumerable.Range(0, s.Buttons.Length)
            .Where(i => s.Buttons[i])
            .Select(i => PadLayout.ButtonNames[i]);
        PressedText = string.Join("  ", pressed);

        Array.Copy(s.Buttons, _lastButtons,
                   Math.Min(s.Buttons.Length, _lastButtons.Length));
    }

    // ---- layer editing -----------------------------------------------------
    private int _editingLayerIndex; // 0 = base, 1..3 = layers

    // Which binding set the button list (and the layer stick panel) edits.
    public int EditingLayerIndex
    {
        get => _editingLayerIndex;
        set
        {
            if (SetField(ref _editingLayerIndex, value))
            {
                Raise(nameof(LayerSelected));
                Raise(nameof(SelectedLayer));
                Raise(nameof(SelectedLayerTriggerIndex));
                Raise(nameof(LayerLeftModIndex));
                Raise(nameof(LayerRightModIndex));
                Raise(nameof(BindingListTitle));
                RebuildRows();
                RaiseTriggerProxies();
            }
        }
    }

    public bool LayerSelected => _editingLayerIndex > 0;

    // The layer currently selected in the diagram dropdown (Layers[0] while
    // the base set is selected, so bindings never hit null; the UI is
    // disabled in that state via LayerSelected).
    public LayerDto SelectedLayer
    {
        get
        {
            Current.EnsureLayers();
            return Current.Layers[Math.Max(0, _editingLayerIndex - 1)];
        }
    }

    public string BindingListTitle =>
        _editingLayerIndex == 0
            ? "按键绑定"
            : $"按键绑定（层 {_editingLayerIndex}：仅列出差异，未设置的键回退到基础层）";

    // ---- trigger layer editing ---------------------------------------------
    // Trigger scalars proxy the base or the selected layer's override so the
    // 扳机页 follows the same 配置组 selection as the button list. The layer
    // stores the trigger wholesale (scalars + binding); the override switch
    // alone decides whether it is active in the engine.
    private LayerDto? TrigLayer =>
        _editingLayerIndex > 0 ? Current.Layers[_editingLayerIndex - 1] : null;

    public bool LtOverrideEnabled
    {
        get => TrigLayer?.LeftTriggerOverride ?? false;
        set
        {
            var l = TrigLayer; if (l == null || l.LeftTriggerOverride == value) return;
            l.LeftTriggerOverride = value;
            if (value && !l.LeftTrigger.Enabled) { l.LeftTrigger.Enabled = true; Raise(nameof(LtEnabled)); }
            Raise(nameof(LtOverrideEnabled));
        }
    }

    public bool RtOverrideEnabled
    {
        get => TrigLayer?.RightTriggerOverride ?? false;
        set
        {
            var l = TrigLayer; if (l == null || l.RightTriggerOverride == value) return;
            l.RightTriggerOverride = value;
            // Enabling the override implies a live layer trigger; leaving it
            // disabled would force-stop RT whenever the layer is held.
            if (value && !l.RightTrigger.Enabled) { l.RightTrigger.Enabled = true; Raise(nameof(RtEnabled)); }
            Raise(nameof(RtOverrideEnabled));
        }
    }

    public bool LtEnabled
    {
        get => (TrigLayer?.LeftTrigger ?? Current.LeftTrigger).Enabled;
        set { var t = TrigLayer != null ? TrigLayer.LeftTrigger : Current.LeftTrigger; if (t.Enabled != value) { t.Enabled = value; Raise(nameof(LtEnabled)); } }
    }

    public bool RtEnabled
    {
        get => (TrigLayer?.RightTrigger ?? Current.RightTrigger).Enabled;
        set { var t = TrigLayer != null ? TrigLayer.RightTrigger : Current.RightTrigger; if (t.Enabled != value) { t.Enabled = value; Raise(nameof(RtEnabled)); } }
    }

    public double LtDeadzone
    {
        get => (TrigLayer?.LeftTrigger ?? Current.LeftTrigger).Deadzone;
        set { var t = TrigLayer != null ? TrigLayer.LeftTrigger : Current.LeftTrigger; if (t.Deadzone != value) { t.Deadzone = value; Raise(nameof(LtDeadzone)); } }
    }

    public double RtDeadzone
    {
        get => (TrigLayer?.RightTrigger ?? Current.RightTrigger).Deadzone;
        set { var t = TrigLayer != null ? TrigLayer.RightTrigger : Current.RightTrigger; if (t.Deadzone != value) { t.Deadzone = value; Raise(nameof(RtDeadzone)); } }
    }

    public double LtThreshold
    {
        get => (TrigLayer?.LeftTrigger ?? Current.LeftTrigger).Threshold;
        set { var t = TrigLayer != null ? TrigLayer.LeftTrigger : Current.LeftTrigger; if (t.Threshold != value) { t.Threshold = value; Raise(nameof(LtThreshold)); } }
    }

    public double RtThreshold
    {
        get => (TrigLayer?.RightTrigger ?? Current.RightTrigger).Threshold;
        set { var t = TrigLayer != null ? TrigLayer.RightTrigger : Current.RightTrigger; if (t.Threshold != value) { t.Threshold = value; Raise(nameof(RtThreshold)); } }
    }

    private void RaiseTriggerProxies()
    {
        foreach (var n in new[]
        {
            nameof(LtOverrideEnabled), nameof(RtOverrideEnabled),
            nameof(LtEnabled), nameof(RtEnabled),
            nameof(LtDeadzone), nameof(RtDeadzone),
            nameof(LtThreshold), nameof(RtThreshold),
        }) Raise(n);
    }

    // Trigger combo: 无触发 / every visible pad button (d-pad, face, menu,
    // sticks, shoulders, paddles, touchpad) / LT / RT / 鼠标锁定.
    // LayerConfig.hold encodes: -1 none, 0..31 pad button, 100 LT, 101 RT,
    // 102 mouse-lock — the engine reads ps.buttons[hold] for 0..31, so any
    // button slot works as a layer activator.
    public string[] LayerTriggerItems { get; } =
        new[] { "无触发" }
            .Concat(PadLayout.VisibleButtons.Select(i => PadLayout.ButtonNames[i]))
            .Concat(new[] { "LT 扳机", "RT 扳机", "鼠标锁定" })
            .ToArray();

    private static int HoldToTriggerIndex(int hold) =>
        hold == 100 ? PadLayout.VisibleButtons.Length + 1
        : hold == 101 ? PadLayout.VisibleButtons.Length + 2
        : hold == 102 ? PadLayout.VisibleButtons.Length + 3
        : Array.IndexOf(PadLayout.VisibleButtons, hold) is int i && i >= 0 ? i + 1
        : 0;

    private static int TriggerIndexToHold(int index) =>
        index <= 0 ? -1
        : index <= PadLayout.VisibleButtons.Length ? PadLayout.VisibleButtons[index - 1]
        : index == PadLayout.VisibleButtons.Length + 1 ? 100
        : index == PadLayout.VisibleButtons.Length + 2 ? 101
        : 102;

    public int SelectedLayerTriggerIndex
    {
        get => HoldToTriggerIndex(SelectedLayer.Hold);
        set
        {
            SelectedLayer.Hold = TriggerIndexToHold(value);
            Raise(nameof(SelectedLayerTriggerIndex));
        }
    }

    // ---- stick modifier ("摇杆修饰键") ---------------------------------------
    // Combo items: 0 = none; then every visible pad button (d-pad, face,
    // sticks, shoulders, paddles, touchpad); finally LT / RT triggers.
    public string[] ModItems { get; } =
        new[] { "无" }
            .Concat(PadLayout.VisibleButtons.Select(i => PadLayout.ButtonNames[i]))
            .Concat(new[] { "LT 扳机", "RT 扳机" })
            .ToArray();

    private static int ModToIndex(int mod) =>
        mod == 100 ? PadLayout.VisibleButtons.Length + 1
        : mod == 101 ? PadLayout.VisibleButtons.Length + 2
        : Array.IndexOf(PadLayout.VisibleButtons, mod) is int i && i >= 0 ? i + 1
        : 0;

    private static int IndexToMod(int index) =>
        index <= 0 ? -1
        : index <= PadLayout.VisibleButtons.Length ? PadLayout.VisibleButtons[index - 1]
        : index == PadLayout.VisibleButtons.Length + 1 ? 100
        : 101;

    public int LeftModIndex
    {
        get => ModToIndex(Current.LeftStick.Mod);
        set { Current.LeftStick.Mod = IndexToMod(value); Raise(nameof(LeftModIndex)); }
    }

    public int RightModIndex
    {
        get => ModToIndex(Current.RightStick.Mod);
        set { Current.RightStick.Mod = IndexToMod(value); Raise(nameof(RightModIndex)); }
    }

    public int LayerLeftModIndex
    {
        get => ModToIndex(SelectedLayer.LeftStick.Mod);
        set { SelectedLayer.LeftStick.Mod = IndexToMod(value); Raise(nameof(LayerLeftModIndex)); }
    }

    public int LayerRightModIndex
    {
        get => ModToIndex(SelectedLayer.RightStick.Mod);
        set { SelectedLayer.RightStick.Mod = IndexToMod(value); Raise(nameof(LayerRightModIndex)); }
    }

    // ---- rows --------------------------------------------------------------
    private void RebuildRows()
    {
        var d = Current;
        d.EnsureLayers();
        LayerDto? lay = _editingLayerIndex > 0 ? d.Layers[_editingLayerIndex - 1] : null;
        InterruptRow = null;
        ButtonRows.Clear();
        VisibleButtonRows.Clear();
        for (int i = 0; i < PadLayout.ButtonCount; i++)
        {
            int idx = i;
            var row = new BindingRow(
                PadLayout.ButtonNames[i],
                () => lay != null ? lay.ButtonAt(idx) : d.ButtonAt(idx),
                a => { if (lay != null) lay.SetButton(idx, a); else d.SetButton(idx, a); },
                idx, withRadial: true);
            // Diagrams bind ButtonRows by absolute pad index — never filter
            // or reorder this collection; the visible list is separate.
            ButtonRows.Add(row);
            if (PadLayout.VisibleButtons.Contains(idx))
                VisibleButtonRows.Add(row);
        }

        var sk = d.LeftStick.Keys;
        string[] labels = { "上", "右上", "右", "右下", "下", "左下", "左", "左上" };
        LeftDirRows.Clear();
        for (int i = 0; i < 8; i++)
        {
            int idx = i;
            LeftDirRows.Add(new BindingRow(labels[i],
                () => sk.DirAt(idx), a => sk.SetDir(idx, a)));
        }

        var sk2 = d.RightStick.Keys;
        RightDirRows.Clear();
        for (int i = 0; i < 8; i++)
        {
            int idx = i;
            RightDirRows.Add(new BindingRow(labels[i],
                () => sk2.DirAt(idx), a => sk2.SetDir(idx, a)));
        }

        // Trigger rows follow the layer selection just like the button list:
        // RebuildRows re-runs on EditingLayerIndex changes, so the same row
        // targets the base or the layer's trigger action wholesale.
        LeftTriggerRow = new BindingRow("LT 扳机",
            () => lay != null ? lay.LeftTrigger.Action : d.LeftTrigger.Action,
            a => { if (lay != null) lay.LeftTrigger.Action = a;
                   else d.LeftTrigger.Action = a; });
        RightTriggerRow = new BindingRow("RT 扳机",
            () => lay != null ? lay.RightTrigger.Action : d.RightTrigger.Action,
            a => { if (lay != null) lay.RightTrigger.Action = a;
                   else d.RightTrigger.Action = a; });
        Raise(nameof(LeftTriggerRow));
        Raise(nameof(RightTriggerRow));
    }

    private void CreateProfileForSelectedPad()
    {
        if (_selectedLivePad == null) return;
        if (_config.Devices.Any(d => d.Guid == _selectedLivePad.Guid))
        {
            Status = "该手柄已有专用配置";
            return;
        }
        var dev = DeviceConfigDto.CreateDefault(_selectedLivePad.Guid,
                                                _selectedLivePad.Name);
        _config.Devices.Add(dev);
        Profiles.Add(dev);
        CurrentIndex = Profiles.Count - 1;
        Status = $"已为 {_selectedLivePad.Name} 新建配置";
    }

    private void RemoveCurrentProfile()
    {
        if (Profiles.Count <= 1) return;
        var d = Current;
        _config.Devices.Remove(d);
        Profiles.RemoveAt(_currentIndex);
        _currentIndex = Math.Clamp(_currentIndex - 1, 0, Profiles.Count - 1);
        Raise(nameof(CurrentIndex));
        RebuildRows();
        Raise(nameof(Current));
    }

    // ---- game selection ----------------------------------------------------
    private void BrowseGameExecutable()
    {
        var dlg = new OpenFileDialog
        {
            Title = "选择游戏 exe",
            Filter = "可执行文件 (*.exe)|*.exe|所有文件 (*.*)|*.*",
        };
        if (dlg.ShowDialog() != true) return;
        LoadGame(dlg.FileName);
    }

    private void LoadGame(string path)
    {
        try
        {
            _pe = PeReader.Read(path);
            GamePath = path;
            string imported = _pe.Imports
                .Where(n => n is "winmm.dll" or "version.dll" or "dinput8.dll")
                .Select(n => "  " + n)
                .DefaultIfEmpty("  （无 winmm/version/dinput8 静态导入）")
                .Aggregate((a, b) => a + "\n" + b);
            string proxy = Deployer.ChooseProxy(_pe) ?? "无（建议启动器模式）";
            GameInfo = $"架构：{_pe.Architecture}\n推荐注入：{proxy}\n静态导入：\n{imported}";
            _config.GameExe = Path.GetFileName(path);
            Status = "已读取游戏信息";
            ((RelayCommand)Deploy).RaiseCanExecuteChanged();
        }
        catch (Exception ex)
        {
            _pe = null;
            GameInfo = $"解析失败：{ex.Message}";
            MessageBox.Show(ex.Message, "PE 解析失败");
        }
    }

    // ---- JSON --------------------------------------------------------------
    private static readonly JsonSerializerOptions JsonOpts = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = true,
    };

    // Remembers the user's json so a fresh instance auto-loads it — the
    // engine must never be toggled against the blank default profile again.
    private static string LastJsonPathFile => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "AdfConfigurator", "last_json.txt");

    private void RememberLastJson(string path)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(LastJsonPathFile)!);
            File.WriteAllText(LastJsonPathFile, path);
        }
        catch { /* best-effort */ }
    }

    private void TryAutoLoadLast()
    {
        try
        {
            if (!File.Exists(LastJsonPathFile)) return;
            string path = File.ReadAllText(LastJsonPathFile).Trim();
            if (path.Length == 0 || !File.Exists(path)) return;
            if (LoadConfigFrom(path, silent: true))
                Status = $"已自动载入上次配置：{path}";
        }
        catch { /* keep the blank default */ }
    }

    private bool LoadConfigFrom(string file, bool silent = false)
    {
        try
        {
            var cfg = JsonSerializer.Deserialize<ConfigDto>(
                File.ReadAllText(file), JsonOpts);
            if (cfg == null) throw new InvalidDataException("配置为空");
            if (cfg.Devices.Count == 0)
                cfg.Devices.Add(DeviceConfigDto.CreateDefault());
            foreach (var d in cfg.Devices)
            {
                foreach (var l in d.Layers)
                    l.MigrateLegacyTriggers();
                // Older configs never carried a profile Guid (the field was
                // added later and nothing assigned it). Stamp one on load so
                // the blank-profile guard in ToggleMappingNow can tell a real
                // file from the never-touched default.
                if (string.IsNullOrEmpty(d.Guid))
                    d.Guid = System.Guid.NewGuid().ToString("N");
            }
            _config = cfg;
            Profiles.Clear();
            foreach (var d in _config.Devices) Profiles.Add(d);
            _currentIndex = 0;
            RebuildRows();
            Raise(nameof(Current));
            Raise(nameof(Config));
            // Resolve the referenced game if it sits next to the json.
            if (!string.IsNullOrEmpty(cfg.GameExe))
            {
                string candidate = Path.Combine(
                    Path.GetDirectoryName(file)!, cfg.GameExe);
                if (File.Exists(candidate)) LoadGame(candidate);
                else GamePath = cfg.GameExe;
            }
            Status = $"已加载 {file}";
            return true;
        }
        catch (Exception ex)
        {
            if (!silent) MessageBox.Show(ex.Message, "配置加载失败");
            return false;
        }
    }

    private void OpenJsonFile()
    {
        var dlg = new OpenFileDialog
        {
            Title = "打开 adf_mapper.json",
            Filter = "ADF 配置 (*.json)|*.json|所有文件|*.*",
        };
        if (dlg.ShowDialog() != true) return;
        if (!LoadConfigFrom(dlg.FileName)) return;
        RememberLastJson(dlg.FileName);
    }

    private void SaveJsonFile()
    {
        var dlg = new SaveFileDialog
        {
            Title = "保存 adf_mapper.json",
            FileName = "adf_mapper.json",
            Filter = "JSON (*.json)|*.json",
        };
        if (dlg.ShowDialog() != true) return;
        SaveConfig(dlg.FileName);
        RememberLastJson(dlg.FileName);
        Status = $"已保存 {dlg.FileName}";
    }

    private void SaveConfig(string path)
    {
        SyncEnabledFlags();
        File.WriteAllText(path,
            JsonSerializer.Serialize(_config, JsonOpts));
    }

    private void SyncEnabledFlags()
    {
        foreach (var d in _config.Devices)
        {
            d.LeftStick.Keys.Enabled = d.LeftStick.Mode == 0;
            d.LeftStick.Mouse.Enabled = d.LeftStick.Mode == 1;
            d.RightStick.Keys.Enabled = d.RightStick.Mode == 0;
            d.RightStick.Mouse.Enabled = d.RightStick.Mode == 1;
        }
    }

    // ---- deploy ------------------------------------------------------------
    private void DeployNow()
    {
        if (_pe == null || string.IsNullOrEmpty(GamePath)) return;
        try
        {
            SyncEnabledFlags();
            var result = Deployer.Deploy(_config, GamePath, _pe, _deployKind);
            Status = "部署完成：" + result.Note;
            MessageBox.Show(result.Note + "\n\n已写入：\n" +
                            string.Join("\n", result.WrittenFiles.Select(f => "  " + f)),
                            "部署完成");
        }
        catch (Exception ex)
        {
            Status = "部署失败：" + ex.Message;
            MessageBox.Show(ex.Message, "部署失败");
        }
    }
}
