"""Compiles Xenia's Vulkan shaders (xesl and glsl, gpu/shaders and ui/shaders) to
SPIR-V headers with the Android NDK's shader tools (glslc, spirv-opt, spirv-dis),
as Xenia's tools/build/compile_shader_spirv.py does with glslangValidator.

    build_xenia_shaders_spirv.py --tools <NDK/shader-tools/<host>> [--include DIR]... SRC_DIR OUT_DIR [SRC_DIR OUT_DIR ...]

The output shaders include FidelityFX CAS and FSR 1 from ../../../../third_party
relative to the shader directory: --include <xenia-canary>/src/xenia/ui/shaders
resolves them in the xenia-canary checkout (its FidelityFX-CAS and -FSR submodules).

Each <name>.<stage>.xesl or .glsl becomes OUT_DIR/<name>_<stage>.h holding
`const uint32_t <name>_<stage>[]`. DAORecomp's own dao_* shaders are HLSL for
Direct3D 12 only and are skipped. Up-to-date headers are kept."""
import argparse
import concurrent.futures
import os
import pathlib
import struct
import subprocess
import sys

STAGES = {"vs": "vert", "hs": "tesc", "ds": "tese", "gs": "geom", "ps": "frag", "cs": "comp"}
XESL_WRAPPER = (
    "#version 460\n"
    "#extension GL_EXT_control_flow_attributes : require\n"
    "#extension GL_EXT_samplerless_texture_functions : require\n"
    "#extension GL_GOOGLE_include_directive : require\n"
    '#include "%s"\n'
)
EXE = ".exe" if sys.platform == "win32" else ""


def jobs(src_dir, out_dir):
    includes = list(src_dir.glob("*.xesli")) + list(src_dir.glob("*.glsli"))
    newest_include = max((p.stat().st_mtime for p in includes), default=0)
    for src in sorted(list(src_dir.glob("*.xesl")) + list(src_dir.glob("*.glsl"))):
        if src.name.startswith("dao_"):
            continue
        ident = src.name.rsplit(".", 1)[0].replace(".", "_")
        if ident[-2:] not in STAGES:
            continue
        out = out_dir / f"{ident}.h"
        if out.exists() and out.stat().st_mtime >= max(src.stat().st_mtime, newest_include):
            continue
        yield src, out, ident


def compile_one(tools, includes, job):
    src, out, ident = job
    out.parent.mkdir(parents=True, exist_ok=True)
    stage = STAGES[ident[-2:]]
    raw = out.with_suffix(".glslc.spv")
    opt = out.with_suffix(".spv")
    dis = out.with_suffix(".txt")
    try:
        args = [str(tools / f"glslc{EXE}"), f"-fshader-stage={stage}", "-DSHADING_LANGUAGE_GLSL_XE=1",
                f"-I{src.parent}"] + [f"-I{d}" for d in includes] + ["-o", str(raw)]
        if src.suffix == ".xesl":
            r = subprocess.run(args + ["-"], input=XESL_WRAPPER % src.name, text=True, capture_output=True)
        else:
            r = subprocess.run(args + [str(src)], capture_output=True, text=True)
        if r.returncode:
            return src.name, f"glslc failed\n{r.stderr}"
        r = subprocess.run([str(tools / f"spirv-opt{EXE}"), "-O", str(raw), "-o", str(opt)], capture_output=True, text=True)
        if r.returncode:
            return src.name, f"spirv-opt failed\n{r.stderr}"
        r = subprocess.run([str(tools / f"spirv-dis{EXE}"), "-o", str(dis), str(opt)], capture_output=True, text=True)
        if r.returncode:
            return src.name, f"spirv-dis failed\n{r.stderr}"
        words = opt.read_bytes()
        if len(words) % 4:
            return src.name, "SPIR-V binary is misaligned"
        with open(out, "w", newline="\n") as f:
            f.write("// Generated with scripts/build_xenia_shaders_spirv.py.\n#if 0\n")
            text = dis.read_text()
            f.write(text if text.endswith("\n") else text + "\n")
            f.write(f"#endif\n\nconst uint32_t {ident}[] = {{")
            for i in range(0, len(words), 4):
                f.write("\n    " if (i // 4) % 6 == 0 else " ")
                f.write("0x%08X," % struct.unpack("<I", words[i:i + 4])[0])
            f.write("\n};\n")
        return src.name, None
    finally:
        for p in (raw, opt, dis):
            if p.exists():
                p.unlink()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tools", required=True, help="directory holding glslc, spirv-opt and spirv-dis")
    parser.add_argument("--include", action="append", default=[], help="extra include directory")
    parser.add_argument("dirs", nargs="+", metavar="SRC_DIR OUT_DIR")
    args = parser.parse_args()
    if len(args.dirs) % 2:
        parser.error("SRC_DIR OUT_DIR pairs expected")
    tools = pathlib.Path(args.tools)
    todo = []
    for i in range(0, len(args.dirs), 2):
        todo += list(jobs(pathlib.Path(args.dirs[i]), pathlib.Path(args.dirs[i + 1])))
    failed = 0
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as pool:
        for name, err in pool.map(lambda j: compile_one(tools, args.include, j), todo):
            if err:
                failed += 1
                print(f"FAILED {name}: {err}", file=sys.stderr)
    print(f"{len(todo) - failed}/{len(todo)} SPIR-V shaders compiled")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
