"""Check whether the installed StarWarsG.exe still matches the hardcoded
tick offset (0x25ca30) used by the proxy DLL. Dumps the prologue bytes at the
documented offset and, if the prologue doesn't match the Ghidra decompile
(movss xmm1,[rcx+8]; addss xmm1,xmm2; movss [rcx+8],xmm1), prints a hint that
the offset has drifted.
"""
import sys

import pefile

GAME = r"C:\Program Files (x86)\Steam\steamapps\common\Star Wars Empire at War\GameData\StarWarsG.exe"
TICK_RVA = 0x25CA30


def main() -> int:
    pe = pefile.PE(GAME, fast_load=True)
    data = open(GAME, "rb").read()

    # Find which section contains the RVA, then map to file offset.
    file_off = None
    for s in pe.sections:
        va = s.VirtualAddress
        size = max(s.Misc_VirtualSize, s.SizeOfRawData)
        if va <= TICK_RVA < va + size:
            file_off = TICK_RVA - va + s.PointerToRawData
            break

    if file_off is None:
        print(f"FAIL: RVA 0x{TICK_RVA:X} not inside any section")
        return 1

    print(f"tick RVA 0x{TICK_RVA:X} -> file offset 0x{file_off:X}")
    print(f"exe size: {len(data)} bytes")
    prologue = data[file_off : file_off + 48]
    print("bytes:", " ".join(f"{b:02X}" for b in prologue))

    # Decompile expected: movss xmm1, [rcx+8]; addss xmm1, xmm2; movss [rcx+8], xmm1
    # Encoding: F3 0F 10 49 08 ; F3 0F 58 CA ; F3 0F 11 49 08
    expected = bytes.fromhex("F3 0F 10 49 08 F3 0F 58 CA F3 0F 11 49 08")
    if prologue[: len(expected)] == expected:
        print("MATCH: prologue matches the documented tick (no drift at this offset)")
        return 0
    print("DRIFT: prologue does NOT match the documented tick pattern")
    return 1


if __name__ == "__main__":
    sys.exit(main())
