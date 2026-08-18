"""Map a crash RVA in the proxy DLL to the enclosing function.

The WER report said the crash is in d3d9.dll at offset 0xc5d2. This script
disassembles the DLL, finds the function (the nearest preceding
prologue/ret) containing that RVA, and prints it.

Usage: python scripts/map_crash_rva.py <dll-path> <rva-hex>
"""
import sys

import capstone
import pefile

DLL = r"C:\Program Files (x86)\Steam\steamapps\common\Star Wars Empire at War\corruption\d3d9.dll.bak"
RVA = 0xC5D2


def main() -> int:
    path = sys.argv[1] if len(sys.argv) > 1 else DLL
    rva = int(sys.argv[2], 16) if len(sys.argv) > 2 else RVA
    pe = pefile.PE(path, fast_load=True)
    data = open(path, "rb").read()

    # The whole image is small; map any RVA to a raw offset.
    def raw(r):
        for s in pe.sections:
            if s.VirtualAddress <= r < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
                return r - s.VirtualAddress + s.PointerToRawData
        return None

    # Disassemble from the start of .text and find function starts.
    text = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
    start = text.PointerToRawData
    size = max(text.Misc_VirtualSize, text.SizeOfRawData)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

    fn_start = None
    fn_end = None
    for ins in md.disasm(data[start : start + size], text.VirtualAddress):
        if ins.address > rva:
            fn_end = ins.address
            break
        if ins.mnemonic == "ret":
            fn_start = ins.address  # a function ends at ret; next is a start
        # crude: track last ret before rva as function start
    # Simpler: find the nearest ret before the crash; the function spans from
    # the last ret+padding to the next ret. Print the window around rva.
    off = raw(rva)
    if off is None:
        print(f"RVA 0x{rva:X} outside sections")
        return 1
    print(f"crash RVA 0x{rva:X} -> raw 0x{off:X}")
    # Print 24 bytes before and 24 after as bytes (the fault is a deref).
    lo = max(0, off - 24)
    hi = min(len(data), off + 24)
    print("bytes before/after:")
    print(" ".join(f"{b:02X}" for b in data[lo:hi]))

    # Disassemble the window
    print("\ndisassembly around crash:")
    for ins in md.disasm(data[lo:hi], rva - (off - lo)):
        mark = "  <== CRASH" if ins.address == rva else ""
        print(f"  0x{ins.address:08X}: {ins.mnemonic:8s} {ins.op_str}{mark}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
