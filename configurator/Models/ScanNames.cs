// PS/2 set-1 scan code names, mirrored from src/core/scancodes.cpp.

using System.Collections.Generic;

namespace AdfConfigurator.Models;

public static class ScanNames
{
    private sealed record Entry(string Name, int Code, bool Ext);

    private static readonly List<Entry> Table = new()
    {
        new("Esc", 0x01, false),
        new("1", 0x02, false), new("2", 0x03, false), new("3", 0x04, false),
        new("4", 0x05, false), new("5", 0x06, false), new("6", 0x07, false),
        new("7", 0x08, false), new("8", 0x09, false), new("9", 0x0A, false),
        new("0", 0x0B, false), new("-", 0x0C, false), new("=", 0x0D, false),
        new("Backspace", 0x0E, false),
        new("Tab", 0x0F, false),
        new("Q", 0x10, false), new("W", 0x11, false), new("E", 0x12, false),
        new("R", 0x13, false), new("T", 0x14, false), new("Y", 0x15, false),
        new("U", 0x16, false), new("I", 0x17, false), new("O", 0x18, false),
        new("P", 0x19, false), new("[", 0x1A, false), new("]", 0x1B, false),
        new("Enter", 0x1C, false),
        new("LCtrl", 0x1D, false),
        new("A", 0x1E, false), new("S", 0x1F, false), new("D", 0x20, false),
        new("F", 0x21, false), new("G", 0x22, false), new("H", 0x23, false),
        new("J", 0x24, false), new("K", 0x25, false), new("L", 0x26, false),
        new(";", 0x27, false), new("'", 0x28, false), new("`", 0x29, false),
        new("LShift", 0x2A, false),
        new("\\", 0x2B, false),
        new("Z", 0x2C, false), new("X", 0x2D, false), new("C", 0x2E, false),
        new("V", 0x2F, false), new("B", 0x30, false), new("N", 0x31, false),
        new("M", 0x32, false), new(",", 0x33, false), new(".", 0x34, false),
        new("/", 0x35, false),
        new("RShift", 0x36, false),
        new("NumPad*", 0x37, false),
        new("LAlt", 0x38, false),
        new("Space", 0x39, false),
        new("CapsLock", 0x3A, false),
        new("F1", 0x3B, false), new("F2", 0x3C, false), new("F3", 0x3D, false),
        new("F4", 0x3E, false), new("F5", 0x3F, false), new("F6", 0x40, false),
        new("F7", 0x41, false), new("F8", 0x42, false), new("F9", 0x43, false),
        new("F10", 0x44, false),
        new("NumLock", 0x45, false),
        new("ScrollLock", 0x46, false),
        new("NumPad7", 0x47, false), new("NumPad8", 0x48, false),
        new("NumPad9", 0x49, false), new("NumPad-", 0x4A, false),
        new("NumPad4", 0x4B, false), new("NumPad5", 0x4C, false),
        new("NumPad6", 0x4D, false), new("NumPad+", 0x4E, false),
        new("NumPad1", 0x4F, false), new("NumPad2", 0x50, false),
        new("NumPad3", 0x51, false), new("NumPad0", 0x52, false),
        new("NumPad.", 0x53, false),
        new("F11", 0x57, false), new("F12", 0x58, false),
        new("RCtrl", 0x1D, true),
        new("RAlt", 0x38, true),
        new("Up", 0x48, true), new("Down", 0x50, true),
        new("Left", 0x4B, true), new("Right", 0x4D, true),
        new("Home", 0x47, true), new("End", 0x4F, true),
        new("PgUp", 0x49, true), new("PgDn", 0x51, true),
        new("Insert", 0x52, true), new("Delete", 0x53, true),
        new("NumPadEnter", 0x1C, true),
        new("NumPad/", 0x35, true),
        new("LWin", 0x5B, true), new("RWin", 0x5C, true),
        new("Apps", 0x5D, true),
    };

    public static string Name(int code, bool ext)
    {
        foreach (var e in Table)
            if (e.Code == code && e.Ext == ext) return e.Name;
        return "";
    }
}
