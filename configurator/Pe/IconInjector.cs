// Copies the icon resource of a game exe onto the deployed launcher exe
// (deployed as "<game-stem>_gamepad.exe").
// using the Win32 resource update API (BeginUpdateResource / UpdateResource).

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace AdfConfigurator.Pe;

internal static class IconInjector
{
    private const uint LoadLibraryAsDatafile = 0x00000002;
    private static readonly IntPtr RtIcon = (IntPtr)3;
    private static readonly IntPtr RtGroupIcon = (IntPtr)14;
    private const ushort LangNeutral = 0;

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern IntPtr LoadLibraryExW(string lpFileName, IntPtr hFile, uint dwFlags);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool FreeLibrary(IntPtr hModule);

    private delegate bool EnumResNameProc(IntPtr hModule, IntPtr type, IntPtr name, IntPtr lParam);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool EnumResourceNamesW(IntPtr hModule, IntPtr type,
                                                  EnumResNameProc lpEnumFunc, IntPtr lParam);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern IntPtr FindResourceExW(IntPtr hModule, IntPtr type,
                                                 IntPtr name, ushort wLanguage);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr LoadResource(IntPtr hModule, IntPtr hResInfo);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr LockResource(IntPtr hResData);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern uint SizeofResource(IntPtr hModule, IntPtr hResInfo);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern IntPtr BeginUpdateResourceW(string pFileName,
                                                      [MarshalAs(UnmanagedType.Bool)] bool bDeleteExistingResources);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool UpdateResourceW(IntPtr hUpdate, IntPtr type, IntPtr name,
                                               ushort wLanguage, byte[]? lpData, uint cbData);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool EndUpdateResourceW(IntPtr hUpdate,
                                                  [MarshalAs(UnmanagedType.Bool)] bool fDiscard);

    /// <summary>Overwrites dstExe's icon with the first icon group found in srcExe.</summary>
    public static void CopyIcon(string srcExe, string dstExe)
    {
        IntPtr hSrc = LoadLibraryExW(srcExe, IntPtr.Zero, LoadLibraryAsDatafile);
        if (hSrc == IntPtr.Zero)
            throw new InvalidOperationException(
                $"无法以资源方式加载 {srcExe}（错误 {Marshal.GetLastWin32Error()}）");

        IntPtr hUpdate = IntPtr.Zero;
        try
        {
            var names = new List<IntPtr>();
            var cb = new EnumResNameProc((_, _, name, _) => { names.Add(name); return true; });
            if (!EnumResourceNamesW(hSrc, RtGroupIcon, cb, IntPtr.Zero) || names.Count == 0)
                throw new InvalidOperationException("游戏 exe 中没有图标资源（RT_GROUP_ICON）");

            // Prefer the lowest numeric ID; Explorer uses that for the file icon.
            IntPtr groupName = names[0];
            foreach (var n in names)
            {
                long a = (long)n, b = (long)groupName;
                if (a is > 0 and <= 0xFFFF && (b > 0xFFFF || a < b))
                    groupName = n;
            }

            byte[] group = LoadResourceBytes(hSrc, RtGroupIcon, groupName);
            if (group.Length < 6)
                throw new InvalidOperationException("图标组数据异常");

            ushort count = BitConverter.ToUInt16(group, 4);
            var icons = new List<(ushort Id, byte[] Data)>();
            for (int i = 0; i < count; i++)
            {
                int off = 6 + i * 14;
                if (off + 14 > group.Length) break;
                ushort iconId = BitConverter.ToUInt16(group, off + 12);
                icons.Add((iconId, LoadResourceBytes(hSrc, RtIcon, (IntPtr)iconId)));
            }
            if (icons.Count == 0)
                throw new InvalidOperationException("图标组内没有图像");

            hUpdate = BeginUpdateResourceW(dstExe, false);
            if (hUpdate == IntPtr.Zero)
                throw new InvalidOperationException(
                    $"无法更新启动器资源（错误 {Marshal.GetLastWin32Error()}），文件可能被占用");

            foreach (var (id, data) in icons)
            {
                if (!UpdateResourceW(hUpdate, RtIcon, (IntPtr)id, LangNeutral, data, (uint)data.Length))
                    throw new InvalidOperationException(
                        $"写入 RT_ICON {id} 失败（错误 {Marshal.GetLastWin32Error()}）");
            }

            // Write under the original name, and also under ID 1 so the shell
            // reliably picks it for the exe icon.
            if (!UpdateResourceW(hUpdate, RtGroupIcon, groupName, LangNeutral, group, (uint)group.Length))
                throw new InvalidOperationException(
                    $"写入图标组失败（错误 {Marshal.GetLastWin32Error()}）");
            if ((long)groupName != 1 &&
                !UpdateResourceW(hUpdate, RtGroupIcon, (IntPtr)1, LangNeutral, group, (uint)group.Length))
            {
                throw new InvalidOperationException(
                    $"写入图标组 ID 1 失败（错误 {Marshal.GetLastWin32Error()}）");
            }

            if (!EndUpdateResourceW(hUpdate, false))
                throw new InvalidOperationException(
                    $"提交图标资源失败（错误 {Marshal.GetLastWin32Error()}）");
            hUpdate = IntPtr.Zero;
        }
        finally
        {
            if (hUpdate != IntPtr.Zero)
                EndUpdateResourceW(hUpdate, true); // discard
            FreeLibrary(hSrc);
        }
    }

    private static byte[] LoadResourceBytes(IntPtr hModule, IntPtr type, IntPtr name)
    {
        IntPtr hInfo = FindResourceExW(hModule, type, name, LangNeutral);
        if (hInfo == IntPtr.Zero)
            throw new InvalidOperationException(
                $"找不到资源 type={(long)type} name={(long)name}（错误 {Marshal.GetLastWin32Error()}）");
        uint size = SizeofResource(hModule, hInfo);
        IntPtr hData = LoadResource(hModule, hInfo);
        if (hData == IntPtr.Zero || size == 0)
            throw new InvalidOperationException("资源数据为空");
        IntPtr p = LockResource(hData);
        if (p == IntPtr.Zero)
            throw new InvalidOperationException("锁定资源失败");

        var buf = new byte[size];
        Marshal.Copy(p, buf, 0, (int)size);
        return buf;
    }
}
