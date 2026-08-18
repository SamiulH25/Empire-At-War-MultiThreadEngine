"""Trace the top function field-by-field for one chunk to see where the
proto count lands."""
import struct
import sys

from meg_reader import MegaFile

MEG = r"C:\Program Files (x86)\Steam\steamapps\common\Star Wars Empire at War\corruption\Data\config.meg"
HEADER = 22


def main() -> int:
    mf = MegaFile(MEG)
    name = "DATA\\SCRIPTS\\AI\\AI_PLAN_EXPANSIONGENERIC_GENERATEMAGICCASHDROP.LUA"
    data = mf.read(name)
    p = HEADER
    print(f"chunk {len(data)} bytes, start 0x{p:x}")

    n = struct.unpack_from("<i", data, p)[0]
    print(f"  0x{p:04x}: source len={n} (4-byte)")
    p += 4 + n
    for label in ("linedefined", "lastlinedefined"):
        v = struct.unpack_from("<i", data, p)[0]
        print(f"  0x{p:04x}: {label}={v}")
        p += 4
    print(f"  0x{p:04x}: 4 bytes = {' '.join(f'{b:02x}' for b in data[p:p+4])}")
    p += 4
    ncode = struct.unpack_from("<i", data, p)[0]
    print(f"  0x{p:04x}: code count={ncode}")
    p += 4
    print(f"  code at 0x{p:04x} ({ncode*4} bytes)")
    p += ncode * 4
    # upvalues
    nu = struct.unpack_from("<i", data, p)[0]
    print(f"  0x{p:04x}: upvalue count={nu}")
    p += 4
    for i in range(nu):
        n = struct.unpack_from("<i", data, p)[0]
        print(f"    upval[{i}] str len={n} @ 0x{p:04x}")
        p += 4 + n
        print(f"    upval[{i}] startpc/endpc = {struct.unpack_from('<i', data, p)[0]},{struct.unpack_from('<i', data, p+4)[0]}")
        p += 8
    # constants
    nk = struct.unpack_from("<i", data, p)[0]
    print(f"  0x{p:04x}: const count={nk}")
    p += 4
    for i in range(nk):
        t = data[p]; p += 1
        if t == 4:
            n = data[p]; p += 1
            print(f"    const[{i}] str len={n} @ 0x{p:04x} -> {data[p:p+n]!r}")
            p += n
        elif t == 3:
            print(f"    const[{i}] num"); p += 8
        elif t == 0:
            print(f"    const[{i}] nil")
        else:
            print(f"    const[{i}] type {t}")
    np_ = struct.unpack_from("<i", data, p)[0]
    print(f"  0x{p:04x}: proto count={np_}")
    p += 4
    for pi in range(1):
        print(f"  === proto {pi} @ 0x{p:04x} ===")
        print(f"    first 16 bytes: {' '.join(f'{b:02x}' for b in data[p:p+16])}")
        # Try: constants count first
        nk = struct.unpack_from("<i", data, p)[0]
        print(f"    @ 0x{p:04x}: if-const-count = {nk}")
        # Try: the 4 bytes are linedefined?
        print(f"    @ 0x{p:04x} as int = {struct.unpack_from('<i', data, p)[0]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
