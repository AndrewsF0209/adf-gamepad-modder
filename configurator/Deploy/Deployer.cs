// Deploys prebuilt proxy DLLs / launcher + adf_mapper.json next to a game exe.

using System;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using AdfConfigurator.Models;
using AdfConfigurator.Pe;

namespace AdfConfigurator.Deploy;

public enum DeployKind
{
    Auto,
    Winmm,
    Version,
    Dinput8,
    Launcher,
}

public sealed record Deployment(string Kind, string[] WrittenFiles, string Note);

public static class Deployer
{
    private static readonly string[] ProxyPreference = { "winmm.dll", "version.dll", "dinput8.dll" };

    public static string? FindArtifactsRoot(string arch) =>
        FindArtifactsRoot(AppContext.BaseDirectory, arch);

    /// <summary>
    /// Locates prebuilt native components for <paramref name="arch"/>.
    /// Two layouts are supported, portable first so a copied release folder
    /// keeps working on machines without the source tree:
    ///   1. &lt;configurator&gt;\native\&lt;arch&gt;\   (self-contained release)
    ///   2. &lt;repo&gt;\artifacts\&lt;arch&gt;\          (dev tree, walk up)
    /// </summary>
    public static string? FindArtifactsRoot(string baseDir, string arch)
    {
        string portable = Path.Combine(baseDir, "native", arch);
        if (File.Exists(Path.Combine(portable, "winmm.dll")))
            return portable;

        var dir = new DirectoryInfo(baseDir);
        for (int i = 0; i < 6 && dir.Parent != null; i++)
        {
            dir = dir.Parent;
            string check = Path.Combine(dir.FullName, "artifacts", arch, "winmm.dll");
            if (File.Exists(check))
                return Path.Combine(dir.FullName, "artifacts", arch);
        }
        return null;
    }

    /// <summary>"winmm.dll" / "version.dll" / "dinput8.dll" statically imported, else null.</summary>
    public static string? ChooseProxy(PeInfo pe)
    {
        foreach (var name in ProxyPreference)
            if (pe.Imports.Contains(name)) return name;
        return null;
    }

    public static Deployment Deploy(ConfigDto config, string gameExePath,
                                    PeInfo pe, DeployKind kind)
    {
        string artifacts = FindArtifactsRoot(pe.Architecture)
            ?? throw new InvalidOperationException(
                $"找不到 {pe.Architecture} 原生组件（native\\{pe.Architecture}\\winmm.dll）。" +
                "请使用完整发布包：配置器 exe 同级应带有 native\\x86 与 native\\x64 目录。");

        string? proxy = kind switch
        {
            DeployKind.Winmm => "winmm.dll",
            DeployKind.Version => "version.dll",
            DeployKind.Dinput8 => "dinput8.dll",
            DeployKind.Launcher => null,
            _ => ChooseProxy(pe),
        };
        if (kind != DeployKind.Launcher && proxy == null)
            throw new InvalidOperationException(
                "该游戏没有静态导入 winmm/version/dinput8 中的任何一个，无法使用代理 DLL，请改用启动器模式。");

        string gameDir = Path.GetDirectoryName(gameExePath)
            ?? throw new ArgumentException("无法解析游戏目录");
        var written = new System.Collections.Generic.List<string>();

        // JSON always stores the exe file name: everything sits in one folder.
        var toSave = config;
        toSave.GameExe = Path.GetFileName(gameExePath);
        string jsonPath = Path.Combine(gameDir, "adf_mapper.json");
        var jsonOpts = new JsonSerializerOptions
        {
            PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
            WriteIndented = true,
        };
        File.WriteAllText(jsonPath, JsonSerializer.Serialize(toSave, jsonOpts),
                          new UTF8Encoding(false));
        written.Add("adf_mapper.json");

        string note;
        string deployedMainFile;
        if (proxy != null)
        {
            string dst = Path.Combine(gameDir, proxy);
            File.Copy(Path.Combine(artifacts, proxy), dst, overwrite: true);
            written.Add(proxy);
            deployedMainFile = proxy;
            note = $"代理模式：已部署 {proxy}（{pe.Architecture}）。直接双击游戏 exe 即生效；" +
                   "若游戏不加载该 DLL，可改用启动器模式。";
        }
        else
        {
            // Launcher naming: "<game-exe-stem>_gamepad.exe" next to the game.
            string launcherName = Path.GetFileNameWithoutExtension(gameExePath) + "_gamepad.exe";
            string dst = Path.Combine(gameDir, launcherName);
            File.Copy(Path.Combine(artifacts, "adf_launcher.exe"), dst, overwrite: true);
            written.Add(launcherName);
            deployedMainFile = launcherName;
            string iconNote;
            try
            {
                IconInjector.CopyIcon(gameExePath, dst);
                iconNote = "已复制游戏图标。";
            }
            catch (Exception ex)
            {
                iconNote = $"图标复制失败（不影响使用）：{ex.Message}";
            }
            note = $"启动器模式（兼容性最好）：以后请双击 {launcherName} 启动游戏，" +
                   "它会拉起游戏并在进程内执行手柄映射。" + iconNote;
        }

        WriteUninstaller(gameDir, written);
        written.Add("卸载ADF映射.bat");
        return new Deployment(deployedMainFile, written.ToArray(), note);
    }

    private static void WriteUninstaller(string gameDir, System.Collections.Generic.List<string> files)
    {
        var sb = new StringBuilder();
        sb.AppendLine("@echo off");
        sb.AppendLine("rem ADF Gamepad Mapper - generated uninstaller");
        foreach (var f in files)
            sb.AppendLine($"del /q \"%~dp0{f}\" 2>nul");
        sb.AppendLine("(goto) 2>nul & del \"%~f0\"");
        File.WriteAllText(Path.Combine(gameDir, "卸载ADF映射.bat"),
                          sb.ToString(), new UTF8Encoding(false));
    }
}
