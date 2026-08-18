"""Locate the sim tick in the installed StarWarsG.exe by signature.

The Ghidra decompile of FUN_14025ca30 (docs/research/04) shows the tick:
  1. accumulates elapsed time (float add on [ctx+8], wraps by a global)
  2. iterates SIX object lists, each: for each object, call vtable+0x50
     (i.e. `call qword ptr [rax + 0x50]` after loading the vtable)

We disassemble the whole .text with capstone and look for functions that
contain at least N occurrences of the vtable+0x50 dispatch pattern:
    mov rcx, qword ptr [<ptr>]      ; load object pointer
    ... test rcx, rcx / je ...
    add rcx, 0x18                    ; object vtable at +0x18 (per installed build)
    mov rax, qword ptr [rcx]
    call qword ptr [rax + 0x50]      ; Update(dt)

Usage: python scripts/find_tick_signature.py [path-to-exe]
"""
import sys

import capstone
import pefile

DEFAULT = r"C:\Program Files (x86)\Steam\steamapps\common\Star Wars Empire at War\GameData\StarWarsG.exe"
MIN_CALLS = 5  # the tick has 6 lists; allow for compiler merging


def main() -> int:
    path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT
    pe = pefile.PE(path, fast_load=True)
    data = open(path, "rb").read()

    text = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
    start = text.PointerToRawData
    size = max(text.Misc_VirtualSize, text.SizeOfRawData)
    text_raw = data[start : start + size]

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    insns = [(ins.address, ins.mnemonic, ins.op_str) for ins in md.disasm(text_raw, text.VirtualAddress)]

    # Collect addresses of every `call qword ptr [rax + 0x50]` (vtable dispatch).
    dispatch_addrs = []
    for addr, mn, op in insns:
        if mn == "call" and op == "qword ptr [rax + 0x50]":
            dispatch_addrs.append(addr)

    print(f"exe: {path}")
    print(f".text raw {hex(start)} size {hex(size)}  RVA base {hex(text.VirtualAddress)}")
    print(f"vtable+0x50 dispatch sites: {len(dispatch_addrs)}")

    # Group into functions: a function boundary is a ret/int3 padding, or a
    # large gap (>64 bytes) between consecutive dispatch sites.
    if not dispatch_addrs:
        return 1
    groups = []
    cur = [dispatch_addrs[0]]
    for a in dispatch_addrs[1:]:
        if a - cur[-1] > 64:
            groups.append(cur)
            cur = [a]
        else:
            cur.append(a)
    groups.append(cur)

    candidates = sorted(groups, key=len, reverse=True)
    print(f"function groups: {len(groups)}; top candidates:")
    for g in candidates[:10]:
        print(f"  RVA 0x{g[0]:08X}  calls {len(g)}  span {g[-1]-g[0]:#x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
