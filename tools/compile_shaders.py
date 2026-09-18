#!/usr/bin/env python
"""Compile the SDL3 GPU shaders and emit them as C byte arrays.

SDL3's GPU API takes compiled shader bytecode, not source, and a different
format per platform:

  DXIL   -> D3D12  (Windows)
  SPIR-V -> Vulkan (Linux, Windows)
  MSL    -> Metal  (macOS)

The HLSL in src/rendering/shaders is compiled ahead of time and the result
checked in as shaders_generated.h, so building the runtime needs no shader
compiler; only editing a shader does.

Preferred toolchain is SDL_shadercross, which is what SDL itself uses and
already knows the resource-binding rules each backend expects:

    git clone https://github.com/libsdl-org/SDL_shadercross
    # build its CLI against SPIRV-Cross and a prebuilt DirectXShaderCompiler,
    # then keep dxcompiler.dll, dxil.dll and spirv-cross-c-shared.dll beside
    # the resulting shadercross executable.
    python tools/compile_shaders.py --shadercross /path/to/shadercross

Without it the script falls back to dxc alone, which can only produce DXIL
(and SPIR-V if that dxc was built with the SPIR-V backend, which Microsoft's
is not). Formats that cannot be produced are left out of the header, and the
runtime asks SDL for whichever ones are present.
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
STAGE_NAME = {"vert": "vertex", "frag": "fragment"}

SDK_DXC_GLOB = r"C:\Program Files (x86)\Windows Kits\10\bin\*\x64\dxc.exe"

# Emitted in this order; the runtime prefers the first one the device supports.
FORMATS = ("spirv", "dxil", "msl")


def which(name, explicit=None):
    if explicit:
        return explicit if os.path.isfile(explicit) else None
    from shutil import which as _which
    return _which(name)


def find_dxc(explicit):
    found = which("dxc", explicit)
    if found:
        return found
    candidates = sorted(glob.glob(SDK_DXC_GLOB))
    return candidates[-1] if candidates else None


def dxc_supports_spirv(dxc):
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


def run(cmd, what):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write("%s failed:\n%s\n%s\n" % (what, r.stdout, r.stderr))
        return False
    return True


def compile_shadercross(tool, src, stage, fmt, out):
    ok = run([tool, src, "-s", "HLSL", "-t", STAGE_NAME[stage],
              "-d", {"spirv": "SPIRV", "dxil": "DXIL", "msl": "MSL"}[fmt],
              "-o", out], "shadercross %s -> %s" % (os.path.basename(src), fmt))
    if not ok or not os.path.isfile(out):
        return None
    with open(out, "rb") as f:
        return f.read()


def compile_dxc(dxc, src, stage, fmt, out):
    cmd = [dxc, "-T", STAGE_PROFILE[stage], "-E", "main", "-O3", "-Fo", out]
    if fmt == "spirv":
        # Mirrors the flags SDL_shadercross passes, plus DX constant-buffer
        # packing so one C struct describes the uniforms for every format.
        cmd += ["-spirv", "-fspv-target-env=vulkan1.0", "-fvk-use-dx-layout",
                "-fspv-flatten-resource-arrays", "-fspv-preserve-bindings",
                "-fspv-preserve-interface"]
    elif fmt != "dxil":
        return None
    cmd.append(src)
    if not run(cmd, "dxc %s -> %s" % (os.path.basename(src), fmt)):
        return None
    with open(out, "rb") as f:
        return f.read()


def c_array(name, data):
    lines = ["static const unsigned char %s[] = {" % name]
    for i in range(0, len(data), 12):
        lines.append("    %s," % ", ".join("0x%02x" % b for b in data[i:i + 12]))
    lines.append("};")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shadercross", default=os.environ.get("SHADERCROSS"),
                    help="SDL_shadercross CLI (produces every format)")
    ap.add_argument("--dxc", default=os.environ.get("DXC"),
                    help="dxc, used only when shadercross is unavailable")
    args = ap.parse_args()

    tool = which("shadercross", args.shadercross)
    dxc = None
    want = []
    if tool:
        print("shadercross: %s" % tool)
        want = list(FORMATS)
    else:
        dxc = find_dxc(args.dxc)
        if not dxc:
            sys.stderr.write("neither shadercross nor dxc found; "
                             "pass --shadercross or --dxc\n")
            return 1
        print("shadercross: not found, falling back to dxc")
        print("dxc: %s" % dxc)
        want = ["dxil"]
        if dxc_supports_spirv(dxc):
            want.insert(0, "spirv")
        else:
            print("  this dxc has no SPIR-V backend; DXIL only")
        print("  MSL needs shadercross, so macOS will be unsupported")

    sources = sorted(glob.glob(os.path.join(SHADER_DIR, "*.hlsl")))
    if not sources:
        sys.stderr.write("no shaders found in %s\n" % SHADER_DIR)
        return 1

    blocks = []
    entries = []
    produced = {f: 0 for f in FORMATS}

    with tempfile.TemporaryDirectory() as tmp:
        for src in sources:
            base = os.path.basename(src)[: -len(".hlsl")]   # e.g. sprite.vert
            stem, stage = base.rsplit(".", 1)                # sprite, vert
            sym = ("%s_%s" % (stem, stage)).replace(".", "_")

            got = {}
            for fmt in want:
                out = os.path.join(tmp, "%s.%s" % (sym, fmt))
                data = (compile_shadercross(tool, src, stage, fmt, out) if tool
                        else compile_dxc(dxc, src, stage, fmt, out))
                if data is None:
                    return 1
                got[fmt] = data
                produced[fmt] += 1
                blocks.append(c_array("%s_%s" % (sym, fmt), data))

            entries.append((sym, stage, got))
            print("  %-22s %s" % (base, "  ".join(
                "%s %d" % (f, len(got[f])) for f in want)))

    def ref(sym, fmt, got):
        return ("%s_%s" % (sym, fmt), "sizeof(%s_%s)" % (sym, fmt)) \
            if fmt in got else ("NULL", "0")

    with open(OUT_HEADER, "w", encoding="utf-8", newline="\n") as f:
        f.write("/*\n")
        f.write(" * Copyright (c) 2026 General Arcade (Pte. Ltd.)\n")
        f.write(" * SPDX-License-Identifier: GPL-2.0-only OR"
                " LicenseRef-GeneralArcade-Commercial\n")
        f.write(" */\n\n")
        f.write("/* Generated by tools/compile_shaders.py -- do not edit.\n")
        f.write("   Regenerate after changing any file in"
                " src/rendering/shaders/*.hlsl.\n\n")
        f.write("   Formats present in this build:%s */\n\n" %
                "".join(" %s" % f for f in FORMATS if produced[f]))
        f.write("#ifndef RMMZ_SHADERS_GENERATED_H\n")
        f.write("#define RMMZ_SHADERS_GENERATED_H\n\n")
        f.write("#include <stddef.h>\n\n")
        f.write("\n\n".join(blocks))
        f.write("\n\n")
        f.write("typedef struct {\n")
        f.write("    const char          *name;\n")
        f.write("    int                  is_vertex;\n")
        f.write("    const unsigned char *spirv;\n")
        f.write("    size_t               spirv_size;\n")
        f.write("    const unsigned char *dxil;\n")
        f.write("    size_t               dxil_size;\n")
        f.write("    /* MSL is source text rather than bytecode; SDL's Metal\n")
        f.write("       backend takes it by pointer and length. */\n")
        f.write("    const unsigned char *msl;\n")
        f.write("    size_t               msl_size;\n")
        f.write("} RmmzShaderBlob;\n\n")
        f.write("static const RmmzShaderBlob RMMZ_SHADER_BLOBS[] = {\n")
        for sym, stage, got in entries:
            cells = []
            for fmt in FORMATS:
                sym_ref, size_ref = ref(sym, fmt, got)
                cells.append("%s, %s" % (sym_ref, size_ref))
            f.write('    { "%s", %d, %s },\n'
                    % (sym, 1 if stage == "vert" else 0, ", ".join(cells)))
        f.write("};\n\n")
        f.write("#endif /* RMMZ_SHADERS_GENERATED_H */\n")

    print("wrote %s" % OUT_HEADER)
    print("formats: %s" % ", ".join("%s (%d shaders)" % (f, produced[f])
                                    for f in FORMATS if produced[f]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
