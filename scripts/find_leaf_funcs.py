#!/usr/bin/env python3
"""Lists "leaf" functions in XenonRecomp's generated code: bodies with no guest memory access,
no calls (direct, indirect or kernel imports) and no control flow out of the function, so they
compute only on the PPCContext. Those are the ones an ARM64-vs-x64 harness can call with the
same input context and compare bit for bit (phase 2 of the plan).

    python -I scripts/find_leaf_funcs.py [--vmx] [--min-lines N] [--max N] [--csv out.csv]

--vmx keeps only functions that use vector (simde_mm_*) or floating-point (f0..f31 / fpscr)
operations, the interesting ones for SIMDe-on-NEON. Read-only.
"""
import argparse
import re
import sys
from pathlib import Path

PPC = Path(__file__).resolve().parent.parent / "recomp" / "ppc"

FUNC_RE = re.compile(r"^PPC_FUNC_IMPL\(__imp__(sub_[0-9A-F]+)\) \{$")
# Anything that leaves the context: memory (every access goes through `base`, with or without
# a PPC_LOAD/STORE macro: vector loads are `simde_mm_load_si128((simde__m128i*)(base + ...))`),
# calls, indirect calls, kernel imports. The prologue line (`__builtin_assume` on base) is
# skipped before matching.
NOT_LEAF = re.compile(r"PPC_CALL_INDIRECT|\(ctx, base\)|__imp__|ctx\.fn\[|longjmp|setjmp|__builtin_unreachable|__builtin_trap"
                      # Integer division by a register: the console gives an undefined result for a zero
                      # divisor, the generated `/` crashes the host. Random inputs (and zeroed guest
                      # memory) do produce zeros, so these functions are left out of the harness.
                      r"|[/%] (ctx\.\w+|temp)\.")
# Guest memory access: through the PPC_LOAD/STORE macros, or vector loads/stores spelled out
# with `base`. Such functions can still run in the harness when it provides a zeroed guest
# address space (column `memory`), but they are kept apart from the pure register ones.
MEMORY = re.compile(r"PPC_(LOAD|STORE|MM_STORE)_|\bbase\b")
VMX_OR_FP = re.compile(r"simde_mm_|ctx\.v\d+|ctx\.f\d+|fpscr")


def has_backward_goto(body):
    """A goto to a label defined earlier in the body: a loop. With zeroed guest memory (or
    random registers) a loop over memory may never end, so loops are kept out of the
    memory-touching set."""
    seen = set()
    for line in body.split("\n"):
        if line.startswith("loc_") and line.rstrip().endswith(":"):
            seen.add(line.rstrip()[:-1])
            continue
        for target in re.findall(r"goto (loc_[0-9A-F]+);", line):
            if target in seen:
                return True
    return False


def scan(path):
    name, lines, depth = None, [], 0
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if name is None:
                m = FUNC_RE.match(line.rstrip("\n"))
                if m:
                    name, lines = m.group(1), []
                continue
            if line.startswith("}"):
                yield name, lines
                name = None
                continue
            lines.append(line)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--vmx", action="store_true")
    ap.add_argument("--min-lines", type=int, default=8)
    ap.add_argument("--max", type=int, default=40)
    ap.add_argument("--csv")
    args = ap.parse_args()

    found = []
    total = 0
    for path in sorted(PPC.glob("ppc_recomp.*.cpp")):
        for name, lines in scan(path):
            total += 1
            body = "".join(l for l in lines if "PPC_FUNC_PROLOGUE" not in l)
            if NOT_LEAF.search(body):
                continue
            if len(lines) < args.min_lines:
                continue
            vmx = bool(VMX_OR_FP.search(body))
            if args.vmx and not vmx:
                continue
            memory = bool(MEMORY.search(body))
            loop = has_backward_goto(body)
            ops = len(re.findall(r"^\t// ", body, re.M))
            found.append((name, path.name, ops, vmx, memory, loop))
    found.sort(key=lambda t: -t[2])
    pure = sum(1 for t in found if not t[4])
    print(f"{total} functions scanned, {len(found)} call-free candidates"
          f"{' with VMX/FP' if args.vmx else ''} (>= {args.min_lines} lines), {pure} without memory access")
    for name, file, ops, vmx, memory, loop in found[:args.max]:
        print(f"  {name:16} {file:22} {ops:5d} instructions {'VMX/FP' if vmx else '      '} "
              f"{'memory' if memory else '      '} {'loop' if loop else ''}")
    if args.csv:
        with open(args.csv, "w", encoding="utf-8") as f:
            f.write("function,file,instructions,vmx_or_fp,memory,loop\n")
            for name, file, ops, vmx, memory, loop in found:
                f.write(f"{name},{file},{ops},{int(vmx)},{int(memory)},{int(loop)}\n")
        print(f"all {len(found)} written to {args.csv}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
