using System.Runtime.InteropServices;

namespace AdfConfigurator.Sdl;

// P/Invoke wrapper for adf_resident.dll — runs the full native mapper
// engine inside this process for real-time gamepad mapping (DS4Windows
// style). The engine is the exact same code that runs inside game
// processes via proxy DLLs.
public static class ResidentMapper
{
    private const string Dll = "adf_resident.dll";

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl,
        CharSet = CharSet.Unicode)]
    public static extern int adf_resident_start(string jsonPath);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern void adf_resident_stop();

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern int adf_resident_is_active();

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern string adf_resident_last_error();

    public static int Start(string jsonPath)
    {
        try { return adf_resident_start(jsonPath); }
        catch (DllNotFoundException) { return -1; }
    }

    public static void Stop()
    {
        try { adf_resident_stop(); }
        catch (DllNotFoundException) { }
    }

    public static string LastError()
    {
        try { return adf_resident_last_error() ?? ""; }
        catch (DllNotFoundException) { return "adf_resident.dll 未找到"; }
    }
}
