// Minimal P/Invoke surface of SDL3.dll used by the configurator.

using System;
using System.Runtime.InteropServices;
using System.Text;

namespace AdfConfigurator.Sdl;

[StructLayout(LayoutKind.Sequential, Size = 16)]
public struct SdlGuid : IEquatable<SdlGuid>
{
    public long A;
    public long B;

    public bool Equals(SdlGuid other) => A == other.A && B == other.B;
    public override bool Equals(object? obj) => obj is SdlGuid g && Equals(g);
    public override int GetHashCode() => HashCode.Combine(A, B);
}

internal static class Sdl3Native
{
    private const string Dll = "SDL3.dll";

    public const uint InitGamepad = 0x00002000;
    public const uint EventGamepadAdded = 0x653;
    public const uint EventGamepadRemoved = 0x654;

    public const int AxisLeftX = 0;
    public const int AxisLeftY = 1;
    public const int AxisRightX = 2;
    public const int AxisRightY = 3;
    public const int AxisLeftTrigger = 4;
    public const int AxisRightTrigger = 5;

    public const int ButtonCount = 32;

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    public static extern bool SDL_Init(uint flags);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern void SDL_Quit();

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr SDL_GetError();

    public static string GetError()
    {
        IntPtr p = SDL_GetError();
        return p == IntPtr.Zero ? "" : Marshal.PtrToStringUTF8(p) ?? "";
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr SDL_GetGamepads(out int count);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern void SDL_free(IntPtr mem);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr SDL_OpenGamepad(int instanceId);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr SDL_GetGamepadFromID(int instanceId);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern void SDL_CloseGamepad(IntPtr gamepad);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern SdlGuid SDL_GetGamepadGUIDForID(int instanceId);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern void SDL_GUIDToString(SdlGuid guid, byte[] buffer, int size);

    public static string GuidToString(SdlGuid guid)
    {
        var buf = new byte[33];
        SDL_GUIDToString(guid, buf, buf.Length);
        return Encoding.ASCII.GetString(buf).TrimEnd('\0');
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr SDL_GetGamepadName(IntPtr gamepad);

    public static string GamepadName(IntPtr gamepad)
    {
        IntPtr p = SDL_GetGamepadName(gamepad);
        return p == IntPtr.Zero ? "Controller" : Marshal.PtrToStringUTF8(p) ?? "Controller";
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern void SDL_PumpEvents();

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    public static extern bool SDL_PollEvent(IntPtr ev);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern short SDL_GetGamepadAxis(IntPtr gamepad, int axis);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.U1)]
    public static extern bool SDL_GetGamepadButton(IntPtr gamepad, int button);
}
