"""Disassemble a window of the installed exe at a given RVA.

Usage: python scripts/disasm_at.py <rva-hex> [count] [path-to-exe]
"""
import sys

import capstone
import pefile

DEFAULT = r"C:\Program Files (x86)\Steam\steamapps\common\Star Wars Empire at War\GameData\StarWarsG.exe"


def main() -> int:
    rva = int(sys.argv[1], 16)
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 48
    path = sys.argv[3] if len(sys.argv) > 3 else DEFAULT
    pe = pefile.PE(path, fast_load=True)
    data = open(path, "rb").read()
    file_off = None
    for s in pe.sections:
        va = s.VirtualAddress
        size = max(s.Misc_VirtualSize, s.SizeOfRawData)
        if va <= rva < va + size:
            file_off = rva - va + s.PointerToRawData
            break
    if file_off is None:
        print(f"RVA 0x{rva:X} outside sections")
        return 1
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    raw = data[file_off : file_off + count * 16]
    for ins in md.disasm(raw, rva):
        print(f"  0x{ins.address:08X}: {ins.mnemonic:8s} {ins.op_str}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
