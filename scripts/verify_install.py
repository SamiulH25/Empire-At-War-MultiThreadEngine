"""Verify the EAW game install matches the facts in the research docs.

Usage: python scripts/verify_install.py "<path to game root>"
Prints PE facts for both StarWarsG.exe binaries and the meg header counts.
Exits 0 if all facts match, 1 otherwise.
"""
import sys, struct
import pefile

EXPECTED = {
    "GameData/StarWarsG.exe": {"ImageBase": 0x140000000, "EntryVA": 0x1406CC318,
                                "ImageSize": 0xBD8000, "Sections": 7},
    "corruption/StarWarsG.exe": {"ImageBase": 0x140000000, "EntryVA": 0x14076A428,
                                  "ImageSize": 0xCC0000, "Sections": 7},
    "corruption/Data/config.meg": {"FirstU32": 1046},
    "corruption/Data/64Patch.meg": {"FirstU32": 324},
    "corruption/Data/shaders.meg": {"FirstU32": 82},
}

def check_exe(path, facts):
    pe = pefile.PE(path, fast_load=True)
    entry_va = pe.OPTIONAL_HEADER.ImageBase + pe.OPTIONAL_HEADER.AddressOfEntryPoint
    got = {
        "ImageBase": pe.OPTIONAL_HEADER.ImageBase,
        "EntryVA": entry_va,
        "ImageSize": pe.OPTIONAL_HEADER.SizeOfImage,
        "Sections": pe.FILE_HEADER.NumberOfSections,
    }
    ok = got == facts
    print(f"[{'OK' if ok else 'MISMATCH'}] {path}")
    if not ok:
        for k in facts:
            if got.get(k) != facts[k]:
                print(f"    {k}: expected {hex(facts[k])}, got {hex(got[k])}")
    return ok

def check_meg(path, facts):
    with open(path, "rb") as f:
        first = struct.unpack_from("<I", f.read(4), 0)[0]
    ok = first == facts["FirstU32"]
    print(f"[{'OK' if ok else 'MISMATCH'}] {path} first u32 = {first}")
    return ok

def main():
    root = sys.argv[1].rstrip("\\/")
    all_ok = True
    for rel, facts in EXPECTED.items():
        path = f"{root}/{rel}"
        if rel.endswith(".exe"):
            all_ok &= check_exe(path, facts)
        else:
            all_ok &= check_meg(path, facts)
    sys.exit(0 if all_ok else 1)

if __name__ == "__main__":
    main()
