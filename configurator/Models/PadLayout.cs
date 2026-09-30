// SDL3 gamepad button indices (SDL_GamepadButton order, mirrored in pad_sdl.cpp).

using System.Collections.Generic;

namespace AdfConfigurator.Models;

public static class PadLayout
{
    public const int ButtonCount = 32;

    public static readonly IReadOnlyList<string> ButtonNames = new[]
    {
        "A (South)", "B (East)", "X (West)", "Y (North)",
        "Back", "Guide", "Start", "左摇杆按下",
        "右摇杆按下", "LB", "RB",
        "十字键 上", "十字键 下", "十字键 左", "十字键 右",
        "Misc1", "背键1", "背键2", "背键3", "背键4",
        "触控板",
        "按钮21", "按钮22", "按钮23", "按钮24", "按钮25",
        "按钮26", "按钮27", "按钮28", "按钮29", "按钮30", "按钮31",
    };

    // Buttons a real gamepad actually has — the rows shown in the binding
    // list. Guide (5), Misc1 (15) and the 21..31 placeholders are hidden.
    public static readonly int[] VisibleButtons =
    {
        0, 1, 2, 3,       // 动作键
        4, 6,             // 菜单键
        7, 8,             // 摇杆按下
        9, 10,            // LB / RB
        11, 12, 13, 14,   // 十字键
        16, 17, 18, 19,   // 背键
        20,               // 触控板
    };
}
