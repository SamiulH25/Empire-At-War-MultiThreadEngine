"""Trace proto 1 specifically with uniform 4-byte-length source."""
import struct
import sys

from meg_reader import MegaFile

MEG = r"C:\Program Files (x86)\Steam\steamapps\common\Star Wars Empire at War\corruption\Data\config.meg"


def main() -> int:
    mf = MegaFile(MEG)
    name = "DATA\\SCRIPTS\\AI\\AI_PLAN_EXPANSIONGENERIC_GENERATEMAGICCASHDROP.LUA"
    data = mf.read(name)
    # Proto 1 starts at 0x1c7 (from the trace).
    p = 0x1C7
    print(f"proto 1 at 0x{p:04x}: {' '.join(f'{b:02x}' for b in data[p:p+48])}")
    n = struct.unpack_from("<i", data, p)[0]
    print(f"  source len={n}")
    p += 4
    if n > 0:
        print(f"  source = {data[p:p+n]!r}")
        p += n
    for label in ("linedefined", "lastlinedefined"):
        v = struct.unpack_from("<i", data, p)[0]
        print(f"  0x{p:04x}: {label}={v}")
        p += 4
    print(f"  0x{p:04x}: 4 bytes = {' '.join(f'{b:02x}' for b in data[p:p+4])}")
    p += 4
    ncode = struct.unpack_from("<i", data, p)[0]
    print(f"  0x{p:04x}: code count={ncode}")
    p += 4 + ncode * 4
    nlv = struct.unpack_from("<i", data, p)[0]
    print(f"  0x{p:04x}: locvars={nlv}")
    p += 4
    for _ in range(nlv):
        n = struct.unpack_from("<i", data, p)[0]; p += 4 + n + 8
    nu = struct.unpack_from("<i", data, p)[0]
    print(f"  0x{p:04x}: upvalues={nu}")
    p += 4
    for _ in range(nu):
        n = struct.unpack_from("<i", data, p)[0]; p += 4 + n
    nk = struct.unpack_from("<i", data, p)[0]
    print(f"  0x{p:04x}: constants={nk}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
