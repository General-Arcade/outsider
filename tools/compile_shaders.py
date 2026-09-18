#!/usr/bin/env python
"""Compile the SDL3 GPU shaders and emit them as C byte arrays.

SDL3's GPU API takes compiled shader bytecode, not source, so the HLSL in
src/rendering/shaders is compiled ahead of time and the result checked in as
shaders_generated.h. Building the runtime therefore needs no shader compiler;
only editing a shader does.

Each shader is compiled to every bytecode format the available dxc supports:

  DXIL   -> SDL's D3D12 backend (Windows)
  SPIR-V -> SDL's Vulkan backend (Windows, Linux)

Microsoft's dxc (shipped in the Windows SDK) is built without the SPIR-V
backend, so a SPIR-V-capable dxc -- from the Vulkan SDK or a DirectX Shader
Compiler release -- must be on PATH or in DXC to emit SPIR-V. Formats that
cannot be produced are simply left out of the generated header, and the
runtime asks SDL for whichever formats are present.

Usage: python tools/compile_shaders.py [--dxc PATH]
"""

import argparse
import glob
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHADER_DIR = os.path.join(ROOT, "src", "rendering", "shaders")
OUT_HEADER = os.path.join(SHADER_DIR, "shaders_generated.h")

# Shader model 6.0 is the minimum SDL's D3D12 backend accepts.
STAGE_PROFILE = {"vert": "vs_6_0", "frag": "ps_6_0"}

SDK_DXC_GLOB = r"C:\Program Files (x86)\Windows Kits\10\bin\*\x64\dxc.exe"


def find_dxc(explicit):
    if explicit:
        return explicit
    from shutil import which
    found = which("dxc")
    if found:
        return found
    candidates = sorted(glob.glob(SDK_DXC_GLOB))
    if candidates:
        return candidates[-1]
    return None


def supports_spirv(dxc):
    """dxc lists -spirv in its help even when built without the backend, so
    probe it with a trivial shader instead."""
    src = "float4 main() : SV_Target { return float4(0,0,0,0); }\n"
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "probe.hlsl")
        with open(path, "w", encoding="utf-8") as f:
            f.write(src)
        r = subprocess.run([dxc, "-T", "ps_6_0", "-E", "main", "-spirv",
                            "-Fo", os.path.join(tmp, "probe.spv"), path],
                           capture_output=True, text=True)
        return r.returncode == 0


def compile_one(dxc, src, profile, out, spirv):
    cmd = [dxc, "-T", profile, "-E", "main", "-O3", "-Fo", out]
    if spirv:
        # -fvk-use-dx-layout keeps constant-buffer packing identical to DXIL,
        # so one C struct describes the uniforms for both formats.
        cmd += ["-spirv", "-fspv-target-env=vulkan1.0", "-fvk-use-dx-layout"]
    cmd.append(src)
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write("%s failed:\n%s\n%s\n" % (os.path.basename(src), r.stdout, r.stderr))
        return None
    with open(out, "rb") as f:
        return f.read()


def c_array(name, data):
    lines = ["static const unsigned char %s[] = {" % name]
    for i in range(0, len(data), 12):
        chunk = ", ".join("0x%02x" % b for b in data[i:i + 12])
        lines.append("    %s," % chunk)
    lines.append("};")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dxc", default=os.environ.get("DXC"))
    args = ap.parse_args()

    dxc = find_dxc(args.dxc)
    if not dxc:
        sys.stderr.write("dxc not found; pass --dxc PATH or set DXC\n")
        return 1
    print("dxc: %s" % dxc)

    want_spirv = supports_spirv(dxc)
    print("SPIR-V backend: %s" % ("yes" if want_spirv else
                                  "no (this dxc was built without it)"))

    sources = sorted(glob.glob(os.path.join(SHADER_DIR, "*.hlsl")))
    if not sources:
        sys.stderr.write("no shaders found in %s\n" % SHADER_DIR)
        return 1

    blocks = []
    entries = []
    with tempfile.TemporaryDirectory() as tmp:
        for src in sources:
            base = os.path.basename(src)[: -len(".hlsl")]      # e.g. sprite.vert
            stem, stage = base.rsplit(".", 1)                   # sprite, vert
            profile = STAGE_PROFILE[stage]
            sym = ("%s_%s" % (stem, stage)).replace(".", "_")

            dxil = compile_one(dxc, src, profile, os.path.join(tmp, sym + ".dxil"), False)
            if dxil is None:
                return 1
            blocks.append(c_array(sym + "_dxil", dxil))
            spv_sym = "NULL"
            spv_len = "0"
            if want_spirv:
                spv = compile_one(dxc, src, profile, os.path.join(tmp, sym + ".spv"), True)
                if spv is None:
                    return 1
                blocks.append(c_array(sym + "_spv", spv))
                spv_sym = sym + "_spv"
                spv_len = "sizeof(%s_spv)" % sym
            entries.append((sym, stage, spv_sym, spv_len))
            print("  %-22s dxil %6d bytes%s" %
                  (base, len(dxil), "" if not want_spirv else "  spv %d bytes" % len(spv)))

    with open(OUT_HEADER, "w", encoding="utf-8", newline="\n") as f:
        f.write("/*\n")
        f.write(" * Copyright (c) 2026 General Arcade (Pte. Ltd.)\n")
        f.write(" * SPDX-License-Identifier: GPL-2.0-only OR"
                " LicenseRef-GeneralArcade-Commercial\n")
        f.write(" */\n\n")
        f.write("/* Generated by tools/compile_shaders.py -- do not edit.\n")
        f.write("   Regenerate after changing any file in"
                " src/rendering/shaders/*.hlsl. */\n\n")
        f.write("#ifndef RMMZ_SHADERS_GENERATED_H\n")
        f.write("#define RMMZ_SHADERS_GENERATED_H\n\n")
        f.write("#include <stddef.h>\n\n")
        f.write("\n\n".join(blocks))
        f.write("\n\n")
        f.write("typedef struct {\n")
        f.write("    const char          *name;\n")
        f.write("    int                  is_vertex;\n")
        f.write("    const unsigned char *dxil;\n")
        f.write("    size_t               dxil_size;\n")
        f.write("    const unsigned char *spirv;\n")
        f.write("    size_t               spirv_size;\n")
        f.write("} RmmzShaderBlob;\n\n")
        f.write("static const RmmzShaderBlob RMMZ_SHADER_BLOBS[] = {\n")
        for sym, stage, spv_sym, spv_len in entries:
            f.write('    { "%s", %d, %s_dxil, sizeof(%s_dxil), %s, %s },\n'
                    % (sym, 1 if stage == "vert" else 0, sym, sym, spv_sym, spv_len))
        f.write("};\n\n")
        f.write("#endif /* RMMZ_SHADERS_GENERATED_H */\n")

    print("wrote %s" % OUT_HEADER)
    return 0


if __name__ == "__main__":
    sys.exit(main())
