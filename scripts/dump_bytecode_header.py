"""Extract a game Lua bytecode chunk from config.meg and dump its header.

Usage: python scripts/dump_bytecode_header.py [entry-name-filter]
"""
import sys

from meg_reader import MegaFile

MEG = r"C:\Program Files (x86)\Steam\steamapps\common\Star Wars Empire at War\corruption\Data\config.meg"
FILTER = sys.argv[1] if len(sys.argv) > 1 else "BUILDGROUNDFORCESPLAN"


def main() -> int:
    mf = MegaFile(MEG)
    found = []
    for n in mf.names():
        if FILTER.upper() in n.upper() and n.upper().endswith(".LUA"):
            found.append(n)
    if not found:
        print(f"no entries matching {FILTER}")
        print("sample names:", mf.names()[:5])
        return 1
    name = found[0]
    data = mf.read(name)
    print(f"entry: {name}  ({len(data)} bytes)")
    print("first 64 bytes (hex):")
    for i in range(0, min(64, len(data)), 16):
        chunk = data[i : i + 16]
        print(f"  {i:04x}: " + " ".join(f"{b:02x}" for b in chunk))
    # Parse the Lua 5.1 binary chunk header.
    if len(data) >= 12:
        sig = data[:4]
        ver = data[4]
        fmt = data[5]
        # Lua 5.1: byte 6 = endianness, byte 7 = sizeof(int),
        # byte 8 = sizeof(size_t), byte 9 = sizeof(Instruction),
        # byte 10 = sizeof(lua_Number), byte 11 = integral flag
        endian = data[6]
        sz_int = data[7]
        sz_size = data[8]
        sz_insn = data[9]
        sz_num = data[10]
        integral = data[11]
        print(f"\nsignature: {sig!r}")
        print(f"version: 0x{ver:02x}  format: 0x{fmt:02x}  endianness: {endian}")
        print(f"sizeof(int): {sz_int}  sizeof(size_t): {sz_size}")
        print(f"sizeof(Instruction): {sz_insn}  sizeof(lua_Number): {sz_num}")
        print(f"integral lua_Number: {integral}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
