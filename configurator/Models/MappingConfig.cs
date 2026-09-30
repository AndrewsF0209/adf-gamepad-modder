// C# mirrors of the adf_mapper.json schema consumed by src/core/config.cpp.
// Field names / defaults must stay in sync with the C++ structs.

using System.Text.Json.Serialization;

namespace AdfConfigurator.Models;

public class KeyActionDto
{
    public string Type { get; set; } = "none"; // none | key | mouse | wheel
    public int Scancode { get; set; }
    public bool Extended { get; set; }
    public string? Name { get; set; }
    public int Button { get; set; } = 1;      // mouse button 1..5
    public int Wheel { get; set; } = 120;     // +/- 120

    // Toggle (latch) mode: tap the pad button to hold the output, tap again
    // to release. Interrupts lists other pad button indices (0..31) whose
    // press force-releases the latched output.
    public bool Toggle { get; set; }
    public List<int> Interrupts { get; set; } = new();

    // Key combination (e.g. Ctrl+C): extra actions fired together with the
    // primary one, in press order. Entries are flat (their own Chord unused).
    public List<KeyActionDto> Chord { get; set; } = new();

    // Gesture outputs (flat, null = unconfigured). Nullable on purpose: a
    // non-null nested KeyActionDto graph would recurse forever.
    public KeyActionDto? Tap { get; set; }
    public KeyActionDto? DoubleTap { get; set; }
    public KeyActionDto? LongPress { get; set; }
    public int LongMs { get; set; } = 300;
    public int DoubleMs { get; set; } = 260;

    // Radial ("wheel") menu: hold the pad button past RadialMs, tilt the
    // chosen stick toward one of 8 sectors, release to fire that sector as a
    // pulse; centered stick on release cancels. A quick tap fires the
    // primary action as a pulse. Sector entries must stay FLAT (their own
    // Chord/gesture/radial fields are ignored by the engine); the list
    // defaults to empty so the DTO graph cannot recurse.
    public bool Radial { get; set; }
    public List<KeyActionDto> RadialDirs { get; set; } = new();
    public int RadialMs { get; set; } = 300;
    public int RadialStick { get; set; } = 1; // 0 = left stick, 1 = right
    // 0 = fire on wheel-button release, 1 = fire on stick re-centering,
    // 2 = fire immediately when the stick enters a sector.
    public int RadialTrigger { get; set; }

    // Sector accessor used by the UI: pads the list to 8 entries on demand.
    public KeyActionDto RadialAt(int i)
    {
        while (RadialDirs.Count < 8) RadialDirs.Add(new KeyActionDto());
        return RadialDirs[i];
    }

    public KeyActionDto Clone()
    {
        var c = (KeyActionDto)MemberwiseClone();
        c.Interrupts = new List<int>(Interrupts);
        c.Chord = Chord.Select(x => x.Clone()).ToList();
        c.Tap = Tap?.Clone();
        c.DoubleTap = DoubleTap?.Clone();
        c.LongPress = LongPress?.Clone();
        c.RadialDirs = RadialDirs.Select(x => x.Clone()).ToList();
        return c;
    }

    [JsonIgnore]
    public bool IsBound => Type != "none";

    [JsonIgnore]
    public bool HasGesture =>
        Tap?.IsBound == true || DoubleTap?.IsBound == true || LongPress?.IsBound == true;

    [JsonIgnore]
    public bool HasRadial => Radial && RadialDirs.Any(d => d.IsBound);

    [JsonIgnore]
    public int RadialBoundCount => RadialDirs.Count(d => d.IsBound);

    [JsonIgnore]
    public string PrimaryDisplay => PrimaryDisplayCore;

    [JsonIgnore]
    private string PrimaryDisplayCore => Type switch
    {
        "key" => string.IsNullOrEmpty(Name) ? $"0x{Scancode:X2}" : Name!,
        "mouse" => Button switch
        {
            1 => "鼠标左键",
            2 => "鼠标右键",
            3 => "鼠标中键",
            4 => "鼠标侧键1",
            5 => "鼠标侧键2",
            _ => $"鼠标键{Button}"
        },
        "wheel" => Wheel >= 0 ? "滚轮上" : "滚轮下",
        _ => "—"
    };

    [JsonIgnore]
    public string Display
    {
        get
        {
            if (Chord.Count == 0) return PrimaryDisplayCore;
            return string.Join(" + ",
                new[] { PrimaryDisplayCore }.Concat(Chord.Select(c => c.PrimaryDisplayCore)));
        }
    }

    // Stable identity used to dedupe parts while capturing a combination.
    [JsonIgnore]
    public string PartId => Type switch
    {
        "key" => $"k:{Scancode}:{Extended}",
        "mouse" => $"m:{Button}",
        "wheel" => $"w:{Wheel}",
        _ => "none",
    };
}

public class StickKeysDto
{
    public bool Enabled { get; set; }
    public double Deadzone { get; set; } = 0.20;
    public bool EightWay { get; set; } = true;

    // Direction order matches C++: U, UR, R, DR, D, DL, L, UL
    public KeyActionDto Up { get; set; } = new();
    public KeyActionDto UpRight { get; set; } = new();
    public KeyActionDto Right { get; set; } = new();
    public KeyActionDto DownRight { get; set; } = new();
    public KeyActionDto Down { get; set; } = new();
    public KeyActionDto DownLeft { get; set; } = new();
    public KeyActionDto Left { get; set; } = new();
    public KeyActionDto UpLeft { get; set; } = new();

    // Direction order matches C++: U, UR, R, DR, D, DL, L, UL
    public KeyActionDto DirAt(int i) => i switch
    {
        0 => Up, 1 => UpRight, 2 => Right, 3 => DownRight,
        4 => Down, 5 => DownLeft, 6 => Left, 7 => UpLeft,
        _ => new KeyActionDto(),
    };

    public void SetDir(int i, KeyActionDto a)
    {
        switch (i)
        {
            case 0: Up = a; break;
            case 1: UpRight = a; break;
            case 2: Right = a; break;
            case 3: DownRight = a; break;
            case 4: Down = a; break;
            case 5: DownLeft = a; break;
            case 6: Left = a; break;
            case 7: UpLeft = a; break;
        }
    }
}

public class StickMouseDto
{
    public bool Enabled { get; set; }
    public double Deadzone { get; set; } = 0.15;
    public double OuterDeadzone { get; set; }
    public double BaseSpeed { get; set; } = 800.0;
    public int Curve { get; set; } // 0 linear, 1 quadratic, 2 cubic
    public double EdgeThreshold { get; set; } = 0.85;
    public double EdgeMultiplier { get; set; } = 2.5;
    public int AccelTimeMs { get; set; } = 150;
}

// Alternate stick behavior while the modifier control is held
// ("摇杆修饰键"). Mode -1 keeps the current mode; direction bindings
// always come from the stick's own Keys.
public class StickAltDto
{
    public int Mode { get; set; } = -1; // -1 keep, 0 keys, 1 mouse
    public double KeysDeadzone { get; set; } = 0.20;
    public bool EightWay { get; set; } = true;
    public StickMouseDto Mouse { get; set; } = new();
}

public class StickDto
{
    public int Mode { get; set; } // 0 keys, 1 mouse
    public StickKeysDto Keys { get; set; } = new();
    public StickMouseDto Mouse { get; set; } = new();

    // Hold-to-modify: -1 none; 0..31 pad button; 100 LT; 101 RT.
    public int Mod { get; set; } = -1;
    public bool ModOverride { get; set; }
    public StickAltDto Alt { get; set; } = new();
}

public class TriggerDto
{
    public bool Enabled { get; set; }
    public double Deadzone { get; set; } = 0.05;
    public double Threshold { get; set; } = 0.5;
    public KeyActionDto Action { get; set; } = new();
}

public class LayerDto
{
    // Hold-to-layer ("按住切层"): while the activator is held, button
    // bindings resolve from Buttons first (slots absent here fall back to
    // the base bindings) and overridden sticks use these configs.
    public int Hold { get; set; } = -1; // -1 none; 0..15 button; 100 LT; 101 RT; 102 mouse-lock
    // Legacy single-layer JSON field; migrated to Hold == 102 on load.
    public bool AutoMouseLock { get; set; }
    public Dictionary<int, KeyActionDto> Buttons { get; set; } = new();
    public bool RightOverride { get; set; }
    public StickDto RightStick { get; set; } = new();
    public bool LeftOverride { get; set; }
    public StickDto LeftStick { get; set; } = new();

    public KeyActionDto ButtonAt(int i) =>
        Buttons.TryGetValue(i, out var a) ? a : new KeyActionDto();

    public void SetButton(int i, KeyActionDto a) => Buttons[i] = a;

    public LayerDto()
    {
        // Sane starting point for the aim-down-sights use case: enabling the
        // right-stick override must never silently kill the stick.
        RightStick.Mode = 1;
        RightStick.Mouse.Enabled = true;
        LeftStick.Mode = 0;
        LeftStick.Keys.Enabled = true;
    }
}

public class DeviceConfigDto
{
    public string Guid { get; set; } = "";
    public string Name { get; set; } = "Default";
    public bool SwapSticks { get; set; }
    public Dictionary<int, KeyActionDto> Buttons { get; set; } = new();
    public StickDto LeftStick { get; set; } = new();
    public StickDto RightStick { get; set; } = new();
    public TriggerDto LeftTrigger { get; set; } = new();
    public TriggerDto RightTrigger { get; set; } = new();

    // Up to 3 switchable layers; the lowest-numbered active layer wins.
    public List<LayerDto> Layers { get; set; } = new()
        { new LayerDto(), new LayerDto(), new LayerDto() };

    // Reads (and migrates) the pre-multi-layer "layer" JSON node; never
    // written back out.
    [JsonPropertyName("layer")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public LayerDto? LegacyLayer
    {
        get => null;
        set
        {
            if (value == null) return;
            EnsureLayers();
            if (value.AutoMouseLock && value.Hold == -1) value.Hold = 102;
            value.AutoMouseLock = false;
            Layers[0] = value;
        }
    }

    public void EnsureLayers()
    {
        while (Layers.Count < 3) Layers.Add(new LayerDto());
    }

    public KeyActionDto ButtonAt(int i) =>
        Buttons.TryGetValue(i, out var a) ? a : new KeyActionDto();

    public void SetButton(int i, KeyActionDto a) => Buttons[i] = a;

    // Same defaults as adf::MakeDefaultDevice().
    public static DeviceConfigDto CreateDefault(string guid = "", string name = "Default")
    {
        var d = new DeviceConfigDto { Guid = guid, Name = name };
        KeyActionDto K(int code, bool ext = false) =>
            new() { Type = "key", Scancode = code, Extended = ext, Name = ScanNames.Name(code, ext) };
        KeyActionDto Mb(int b) => new() { Type = "mouse", Button = b };

        d.LeftStick.Mode = 0;
        d.LeftStick.Keys.Enabled = true;
        d.LeftStick.Keys.Deadzone = 0.20;
        d.LeftStick.Keys.EightWay = true;
        d.LeftStick.Keys.Up = K(0x11);
        d.LeftStick.Keys.Right = K(0x20);
        d.LeftStick.Keys.Down = K(0x1F);
        d.LeftStick.Keys.Left = K(0x1E);

        d.RightStick.Mode = 1;
        d.RightStick.Mouse.Enabled = true;
        d.RightStick.Mouse.Deadzone = 0.15;
        d.RightStick.Mouse.BaseSpeed = 900;
        d.RightStick.Mouse.Curve = 1;
        d.RightStick.Mouse.EdgeThreshold = 0.85;
        d.RightStick.Mouse.EdgeMultiplier = 2.5;
        d.RightStick.Mouse.AccelTimeMs = 150;

        d.Buttons[0] = K(0x39);       // A -> Space
        d.Buttons[1] = K(0x12);       // B -> E
        d.Buttons[2] = K(0x2A);       // X -> LShift
        d.Buttons[3] = K(0x21);       // Y -> F
        d.Buttons[4] = K(0x0F);       // Back -> Tab
        d.Buttons[6] = K(0x1C);       // Start -> Enter
        d.Buttons[9] = K(0x10);       // LB -> Q
        d.Buttons[10] = K(0x13);      // RB -> R
        d.Buttons[11] = K(0x48, true);
        d.Buttons[12] = K(0x50, true);
        d.Buttons[13] = K(0x4B, true);
        d.Buttons[14] = K(0x4D, true);

        d.LeftTrigger.Enabled = true;
        d.LeftTrigger.Deadzone = 0.05;
        d.LeftTrigger.Threshold = 0.5;
        d.LeftTrigger.Action = K(0x1D);

        d.RightTrigger.Enabled = true;
        d.RightTrigger.Deadzone = 0.05;
        d.RightTrigger.Threshold = 0.5;
        d.RightTrigger.Action = Mb(1);
        return d;
    }
}

public class ConfigDto
{
    public int Version { get; set; } = 1;
    public string GameExe { get; set; } = "";
    public string GameArgs { get; set; } = "";
    public int PollRateHz { get; set; } = 1000;
    public bool ForegroundOnly { get; set; } = true;
    public List<DeviceConfigDto> Devices { get; set; } = new();
}
