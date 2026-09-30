#!/usr/bin/env python3
"""Generate a lazy-forwarding proxy DLL skeleton for a Windows system DLL.

Reads the export table of a real system DLL (System32 for x64, SysWOW64 for
x86) and emits:
  adf_proxy_names.h - export name table + target DLL name
  proxy_stub.asm    - one jmp stub per export plus the lazy resolver
  proxy.def         - export aliases preserving names and ordinals

The generated proxy initially resolves every export on first call to the
real system DLL loaded by absolute path, avoiding loader-lock issues.
"""

import argparse
import os
import struct
import sys


def parse_pe_exports(data):
    if data[:2] != b"MZ":
        raise ValueError("not an MZ image")
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
        raise ValueError("not a PE image")

    coff = e_lfanew + 4
    machine, num_sections = struct.unpack_from("<HH", data, coff)
    size_opt = struct.unpack_from("<H", data, coff + 16)[0]
    opt = coff + 20
    magic = struct.unpack_from("<H", data, opt)[0]

    if magic == 0x20B:      # PE32+
        dir_off = opt + 112
    elif magic == 0x10B:    # PE32
        dir_off = opt + 96
    else:
        raise ValueError("unexpected optional header magic 0x%x" % magic)

    exp_rva, exp_size = struct.unpack_from("<II", data, dir_off)
    if exp_rva == 0:
        raise ValueError("DLL has no export directory")

    sec_off = opt + size_opt
    sections = []
    for i in range(num_sections):
        s = sec_off + i * 40
        name = data[s:s + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", data, s + 8)
        sections.append((vaddr, vsize, rawptr, rawsize, name))

    def rva2off(rva):
        for vaddr, vsize, rawptr, rawsize, _ in sections:
            if vaddr <= rva < vaddr + max(vsize, rawsize):
                return rawptr + (rva - vaddr)
        raise ValueError("RVA 0x%x not in any section" % rva)

    eo = rva2off(exp_rva)
    (_chars, _ts, _maj, _min, _name_rva, base, n_funcs, n_names,
     addr_funcs, addr_names, addr_ord) = struct.unpack_from("<IIHHIIIIIII", data, eo)

    result = []
    for i in range(n_names):
        name_rva = struct.unpack_from("<I", data, rva2off(addr_names) + i * 4)[0]
        p = rva2off(name_rva)
        end = data.index(b"\0", p)
        name = data[p:end].decode("ascii", "replace")
        bias = struct.unpack_from("<H", data, rva2off(addr_ord) + i * 2)[0]
        ordinal = base + bias
        result.append((name, ordinal))
    return result, machine


def emit_names(out, target, exports):
    lines = [
        "#pragma once",
        "",
        "namespace adf::proxy {",
        "",
        'inline constexpr const char* kTargetName = "%s";' % target,
        "inline constexpr const char* kExportNames[] = {",
    ]
    for name, _ord in exports:
        safe = name.replace("\\", "\\\\").replace('"', '\\"')
        lines.append('    "%s",' % safe)
    lines += [
        "};",
        "inline constexpr int kExportCount = %d;" % len(exports),
        "",
        "} // namespace adf::proxy",
        "",
    ]
    with open(os.path.join(out, "adf_proxy_names.h"), "w", newline="\n") as f:
        f.write("\n".join(lines))


def emit_asm_x64(out, n):
    a = ["; AUTO-GENERATED. DO NOT EDIT.", ".code", "",
         "EXTERN ResolveName:PROC", ""]
    a += [
        "ResolverCommon PROC",
        "    push rcx",
        "    push rdx",
        "    push r8",
        "    push r9",
        "    sub  rsp, 28h",
        "    mov  rcx, r10",
        "    call ResolveName",
        "    add  rsp, 28h",
        "    mov  r10, rax",
        "    pop  r9",
        "    pop  r8",
        "    pop  rdx",
        "    pop  rcx",
        "    jmp  r10",
        "ResolverCommon ENDP",
        "",
        "FailStub PROC",
        "    xor  eax, eax",
        "    ret",
        "FailStub ENDP",
        "",
    ]
    for i in range(n):
        a += [
            "stub_%d PROC PUBLIC" % i,
            "    mov  rax, QWORD PTR [g_slots + %d*8]" % i,
            "    test rax, rax",
            "    jnz  adf_jmp_%d" % i,
            "    mov  r10d, %d" % i,
            "    jmp  ResolverCommon",
            "adf_jmp_%d:" % i,
            "    jmp  rax",
            "stub_%d ENDP" % i,
            "",
        ]
    a += [
        ".data?",
        "PUBLIC g_slots",
        "g_slots QWORD %d DUP (?)" % n,
        "END",
    ]
    with open(os.path.join(out, "proxy_stub.asm"), "w", newline="\r\n") as f:
        f.write("\n".join(a))


def emit_asm_x86(out, n):
    a = [
        "; AUTO-GENERATED. DO NOT EDIT.",
        ".686",
        ".model flat",
        ".code",
        "",
        "EXTERN _ResolveName:PROC",
        "",
        "ResolverCommon PROC",
        "    push eax",
        "    call _ResolveName",
        "    add  esp, 4",
        "    jmp  eax",
        "ResolverCommon ENDP",
        "",
        "_FailStub PROC",
        "    xor  eax, eax",
        "    ret  12",
        "_FailStub ENDP",
        "",
    ]
    for i in range(n):
        a += [
            "stub_%d PROC PUBLIC" % i,
            "    mov  eax, DWORD PTR [_g_slots + %d*4]" % i,
            "    test eax, eax",
            "    jnz  adf_jmp_%d" % i,
            "    mov  eax, %d" % i,
            "    jmp  ResolverCommon",
            "adf_jmp_%d:" % i,
            "    jmp  eax",
            "stub_%d ENDP" % i,
            "",
        ]
    a += [
        ".data?",
        "PUBLIC _g_slots",
        "_g_slots DWORD %d DUP (?)" % n,
        "END",
    ]
    with open(os.path.join(out, "proxy_stub.asm"), "w", newline="\r\n") as f:
        f.write("\n".join(a))


def emit_def(out, target, exports):
    lines = ["EXPORTS"]
    for i, (name, ordinal) in enumerate(exports):
        # A very small number of system exports contain characters illegal in
        # a .def identifier; they are forwarded by ordinal only.
        safe = all(c.isalnum() or c in "._-?" for c in name) and not name[0].isdigit()
        if safe:
            lines.append("    %s=stub_%d @%d" % (name, i, ordinal))
        else:
            lines.append("    stub_%d @%d NONAME" % (i, ordinal))
    with open(os.path.join(out, "proxy.def"), "w", newline="\n") as f:
        f.write("\n".join(lines))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", required=True, help="e.g. winmm.dll")
    ap.add_argument("--arch", required=True, choices=["x86", "x64"])
    ap.add_argument("--outdir", required=True)
    args = ap.parse_args()

    windir = os.environ.get("WINDIR", r"C:\Windows")
    sysdir = "System32" if args.arch == "x64" else "SysWOW64"
    path = os.path.join(windir, sysdir, args.target)
    if not os.path.isfile(path):
        print("system DLL not found: %s" % path, file=sys.stderr)
        return 2

    with open(path, "rb") as f:
        data = f.read()
    exports, machine = parse_pe_exports(data)

    expect_machine = 0x8664 if args.arch == "x64" else 0x14C
    if machine != expect_machine:
        print("warning: %s machine=0x%x but arch=%s"
              % (path, machine, args.arch), file=sys.stderr)

    os.makedirs(args.outdir, exist_ok=True)
    emit_names(args.outdir, args.target, exports)
    emit_def(args.outdir, args.target, exports)
    if args.arch == "x64":
        emit_asm_x64(args.outdir, len(exports))
    else:
        emit_asm_x86(args.outdir, len(exports))
    print("%s %s: %d exports" % (args.target, args.arch, len(exports)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
