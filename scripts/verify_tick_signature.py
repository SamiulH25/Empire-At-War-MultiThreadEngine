"""Verify the proxy's signature bytes appear exactly once in the corruption
exe's .text (the runtime scan should find the tick at the known offset)."""
import sys

from check_exe_stamps import GAME_ROOT

SIG = bytes.fromhex(
    "48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 30 0F 28 C1 0F 29 74 24 20"
    " F3 0F 58 41 08"
)


def main() -> int:
    import pefile

    path = GAME_ROOT + "\\corruption\\StarWarsG.exe"
    pe = pefile.PE(path, fast_load=True)
    data = open(path, "rb").read()
    text = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
    start = text.PointerToRawData
    size = max(text.Misc_VirtualSize, text.SizeOfRawData)
    window = data[start : start + size]
    hits = []
    i = window.find(SIG)
    while i >= 0:
        hits.append(i)
        i = window.find(SIG, i + 1)
    print(f"signature hits in .text: {len(hits)}")
    for off in hits:
        rva = text.VirtualAddress + off
        print(f"  rva 0x{rva:08x}  (expected tick rva 0x25ca30, match: {rva == 0x25ca30})")
    return 0 if hits else 1


if __name__ == "__main__":
    sys.exit(main())
