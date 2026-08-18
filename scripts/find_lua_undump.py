"""Find .text references to the 'bad code' string RVA and disassemble the
enclosing function (the game's luaU_undump / LoadFunction)."""
import sys

import capstone
import pefile

GAME = r"C:\Program Files (x86)\Steam\steamapps\common\Star Wars Empire at War\corruption\StarWarsG.exe"
BADCODE_RVA = 0x008FD8F8
MAGIC_RVA = 0x008FD714


def main() -> int:
    pe = pefile.PE(GAME, fast_load=True)
    data = open(GAME, "rb").read()
    text = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
    start = text.PointerToRawData
    size = max(text.Misc_VirtualSize, text.SizeOfRawData)

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True

    refs = []
    for ins in md.disasm(data[start : start + size], text.VirtualAddress):
        for op in ins.operands:
            if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                target = (ins.address + ins.size + op.mem.disp) & 0xFFFFFFFFFFFFFFFF
                if target == BADCODE_RVA or target == MAGIC_RVA:
                    refs.append((ins.address, target))
    print(f"code refs to badcode/magic: {len(refs)}")
    for a, t in refs:
        print(f"  0x{a:08x} -> 0x{t:08x}")

    # For the first ref to bad code, disassemble the enclosing function
    # (walk back to the last ret/int3, forward to the next ret).
    if not refs:
        return 1
    anchor = refs[0][0]
    insns = list(md.disasm(data[start : start + size], text.VirtualAddress))
    fn_start = anchor
    for ins in insns:
        if ins.address >= anchor:
            break
        if ins.mnemonic in ("ret", "int3"):
            fn_start = ins.address + ins.size
    print(f"\nfunction containing 0x{anchor:08x} starts near 0x{fn_start:08x}")
    for ins in insns:
        if ins.address < fn_start:
            continue
        if ins.address > anchor + 400:
            break
        print(f"  0x{ins.address:08x}: {ins.mnemonic:8s} {ins.op_str}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
