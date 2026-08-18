"""Parse a game Lua bytecode chunk far enough to reveal the custom layout.

The game's fork uses magic \\x1bLup, version 0x51, and a 6-byte Instruction
(vanilla: 4). The vanilla header is 12 bytes; the game's has extra size
fields. This script dumps the raw bytes after the header and walks the
structure heuristically to expose where the layout diverges.

Usage: python scripts/analyze_game_bytecode.py [entry-filter]
"""
import struct
import sys

from meg_reader import MegaFile

MEG = r"C:\Program Files (x86)\Steam\steamapps\common\Star Wars Empire at War\corruption\Data\config.meg"
FILTER = sys.argv[1] if len(sys.argv) > 1 else "BUILDGROUNDFORCESPLAN"


def main() -> int:
    mf = MegaFile(MEG)
    found = [n for n in mf.names() if FILTER.upper() in n.upper() and n.upper().endswith(".LUA")]
    if not found:
        print(f"no entries matching {FILTER}")
        return 1
    name = found[0]
    data = mf.read(name)
    print(f"entry: {name}  ({len(data)} bytes)")

    # Header: 4 sig + 1 ver + 1 fmt + 6 size fields + 1 integral = 13 bytes
    # (the fork added a field: the vanilla 12-byte header is followed by 09).
    header = data[:16]
    print("header hex:", " ".join(f"{b:02x}" for b in header))
    print("sig:", header[:4], "ver:", hex(header[4]), "fmt:", hex(header[5]))
    print("sizes: endian=%d int=%d size_t=%d insn=%d num=%d integral=%d extra=%02x" % (
        header[6], header[7], header[8], header[9], header[10], header[11], header[12]))

    # After the header, vanilla reads: source string, then 4 ints + 4 bytes.
    pos = 13
    print("\nbytes after header (first 64):")
    for i in range(0, min(64, len(data) - pos), 16):
        chunk = data[pos + i : pos + i + 16]
        print(f"  +{i:04x}: " + " ".join(f"{b:02x}" for b in chunk))

    # Dump the first ~200 bytes as individual bytes with offsets, so we can
    # identify the field boundaries by hand.
    print("\nbyte-by-byte (offset: value):")
    for i in range(pos, min(pos + 128, len(data))):
        print(f"  {i:04x}: {data[i]:02x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
