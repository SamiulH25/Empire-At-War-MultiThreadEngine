"""Print PE facts (timestamp, sections) for both StarWarsG.exe binaries and
check the documented tick RVA (0x25ca30) in each.

The Ghidra tick decompile (FUN_14025ca30) matches the CORRUPTION exe
(Forces of Corruption, launched via swfoc.exe). The GameData exe (base game)
uses a different build and different code at that RVA. This script makes the
per-exe status explicit.
"""
import datetime
import sys

import pefile

GAME_ROOT = r"C:\Program Files (x86)\Steam\steamapps\common\Star Wars Empire at War"
TICK_RVA = 0x25CA30

# From the corruption exe disassembly, the tick at 0x25CA30 begins:
#   mov qword ptr [rsp+0x10], rbx
#   mov qword ptr [rsp+0x18], rsi
#   push rdi
#   sub rsp, 0x30
#   movaps xmm0, xmm1                 ; dt
#   movaps xmmword ptr [rsp+0x20], xmm6
#   addss  xmm0, dword ptr [rcx+8]    ; accumulate elapsed
#   movaps xmm6, xmm1
#   mov    rbx, rcx
#   movss  xmm1, dword ptr [rip+disp] ; wrap constant
#   comiss xmm0, xmm1
#   movss  dword ptr [rcx+8], xmm0
# Signature (through addss): 48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 30
#                            0F 28 C1 0F 29 74 24 20 F3 0F 58 41 08
PROLOGUE = bytes.fromhex("48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 30 0F 28 C1 0F 29 74 24 20 F3 0F 58 41 08")


def check(path: str) -> None:
    print(f"== {path}")
    pe = pefile.PE(path, fast_load=True)
    ts = pe.FILE_HEADER.TimeDateStamp
    when = datetime.datetime.fromtimestamp(ts, datetime.UTC).isoformat()
    print(
        f"  image base {hex(pe.OPTIONAL_HEADER.ImageBase)}  "
        f"sections {pe.FILE_HEADER.NumberOfSections}  stamp {when}"
    )
    data = open(path, "rb").read()
    file_off = None
    for s in pe.sections:
        va = s.VirtualAddress
        size = max(s.Misc_VirtualSize, s.SizeOfRawData)
        if va <= TICK_RVA < va + size:
            file_off = TICK_RVA - va + s.PointerToRawData
            break
    if file_off is None:
        print(f"  RVA 0x{TICK_RVA:X} outside sections")
        return
    prologue = data[file_off : file_off + len(PROLOGUE)]
    match = prologue == PROLOGUE
    print(
        f"  tick 0x{TICK_RVA:X} (file 0x{file_off:X}): "
        f"{'MATCH' if match else 'DRIFT'}"
    )
    if not match:
        print("  bytes: " + " ".join(f"{b:02X}" for b in data[file_off : file_off + 24]))


def main() -> int:
    check(f"{GAME_ROOT}\\GameData\\StarWarsG.exe")
    check(f"{GAME_ROOT}\\corruption\\StarWarsG.exe")
    return 0


if __name__ == "__main__":
    sys.exit(main())
