// Minimal PE reader: architecture (x86/x64) and statically imported DLL names.

using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace AdfConfigurator.Pe;

public sealed class PeInfo
{
    public required string Architecture { get; init; }   // "x86" | "x64"
    public required List<string> Imports { get; init; }  // lowercase dll names
}

public static class PeReader
{
    public static PeInfo Read(string path)
    {
        byte[] data = File.ReadAllBytes(path);

        ushort DosSignature = BitConverter.ToUInt16(data, 0);
        if (DosSignature != 0x5A4D) // "MZ"
            throw new InvalidDataException("不是有效的 PE 文件（缺少 MZ 头）");

        int peOff = BitConverter.ToInt32(data, 0x3C);
        if (peOff + 4 > data.Length ||
            BitConverter.ToUInt32(data, peOff) != 0x00004550) // "PE\0\0"
            throw new InvalidDataException("不是有效的 PE 文件（缺少 PE 签名）");

        int coff = peOff + 4;
        ushort machine = BitConverter.ToUInt16(data, coff);
        ushort optSize = BitConverter.ToUInt16(data, coff + 16);
        int opt = coff + 20;

        ushort magic = BitConverter.ToUInt16(data, opt);
        string arch = (machine, magic) switch
        {
            (0x8664, 0x20B) => "x64",
            (0x014C, 0x10B) => "x86",
            (0xAA64, 0x20B) => "arm64",
            _ => $"未知(0x{machine:X4})",
        };
        if (arch != "x86" && arch != "x64")
            throw new InvalidDataException($"暂不支持的游戏架构：{arch}（本项目仅提供 x86/x64）");

        // Import directory is data-directory index 1.
        bool is64 = magic == 0x20B;
        int dirOff = opt + (is64 ? 112 : 96);
        int importRva = BitConverter.ToInt32(data, dirOff + 8);

        var imports = new List<string>();
        if (importRva != 0)
        {
            int sectionTable = opt + optSize;
            ushort numSections = BitConverter.ToUInt16(data, coff + 2);

            int RvaToOffset(int rva)
            {
                for (int s = 0; s < numSections; s++)
                {
                    int sec = sectionTable + s * 40;
                    int vsize = BitConverter.ToInt32(data, sec + 8);
                    int vaddr = BitConverter.ToInt32(data, sec + 12);
                    int rawSize = BitConverter.ToInt32(data, sec + 16);
                    int rawPtr = BitConverter.ToInt32(data, sec + 20);
                    if (rva >= vaddr && rva < vaddr + Math.Max(vsize, rawSize))
                        return rva - vaddr + rawPtr;
                }
                return -1;
            }

            int desc = RvaToOffset(importRva);
            while (desc >= 0 && desc + 20 <= data.Length)
            {
                int nameRva = BitConverter.ToInt32(data, desc + 12);
                if (nameRva == 0) break;
                int nameOff = RvaToOffset(nameRva);
                if (nameOff >= 0)
                {
                    int end = nameOff;
                    while (end < data.Length && data[end] != 0) end++;
                    string name = Encoding.ASCII.GetString(data, nameOff, end - nameOff);
                    if (!string.IsNullOrEmpty(name))
                        imports.Add(name.ToLowerInvariant());
                }
                desc += 20;
            }
        }

        return new PeInfo { Architecture = arch, Imports = imports };
    }
}
