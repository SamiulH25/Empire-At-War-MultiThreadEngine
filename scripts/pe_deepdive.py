"""Dump PE imports and exports for EAW binaries. Usage:
  python scripts/pe_deepdive.py "<game root>"
"""
import sys, pefile

BINARIES = [
    "corruption/StarWarsG.exe",
    "GameData/StarWarsG.exe",
    "corruption/PerceptionFunctionG.dll",
    "GameData/PerceptionFunctionG.dll",
    "corruption/swfoc.exe",
    "corruption/tbbR.dll",
]

def main():
    root = sys.argv[1].rstrip("\\/")
    for rel in BINARIES:
        path = f"{root}/{rel}"
        print("=" * 78)
        print(rel)
        pe = pefile.PE(path, fast_load=True)
        pe.parse_data_directories()
        print(f"  Machine: {hex(pe.FILE_HEADER.Machine)}  "
              f"Sections: {pe.FILE_HEADER.NumberOfSections}  "
              f"TimeDateStamp: {pe.FILE_HEADER.TimeDateStamp}")
        if hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
            print(f"  Imports ({len(pe.DIRECTORY_ENTRY_IMPORT)} DLLs):")
            for entry in pe.DIRECTORY_ENTRY_IMPORT:
                print(f"    {entry.dll.decode()} ({len(entry.imports)} funcs)")
                if entry.dll.decode().upper().startswith(("TBB", "D3D", "MSS", "BINK")):
                    for imp in entry.imports[:30]:
                        name = imp.name.decode() if imp.name else f"ordinal_{imp.ordinal}"
                        print(f"      -> {name}")
        if hasattr(pe, "DIRECTORY_ENTRY_EXPORT"):
            print(f"  Exports ({len(pe.DIRECTORY_ENTRY_EXPORT.symbols)}):")
            for exp in pe.DIRECTORY_ENTRY_EXPORT.symbols:
                name = exp.name.decode() if exp.name else f"ordinal_{exp.ordinal}"
                print(f"    {name} @ RVA {hex(exp.address)}")

if __name__ == "__main__":
    main()
