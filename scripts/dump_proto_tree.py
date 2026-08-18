"""Dump the proto tree stats for one chunk to confirm the data-only finding."""
import struct
import sys

from meg_reader import MegaFile

MEG = r"C:\Program Files (x86)\Steam\steamapps\common\Star Wars Empire at War\corruption\Data\config.meg"
HEADER = 22


def rd_s4(data, p):
    n = struct.unpack_from("<i", data, p)[0]
    p += 4
    if n < 0 or p + n > len(data):
        return None, p
    return data[p : p + n], p + n


def walk(data, p, depth, out):
    s, p = rd_s4(data, p)
    if s is None: return False, p
    src = s.decode("utf-8", "replace") if s else "(nil)"
    for _ in range(2):
        v = struct.unpack_from("<i", data, p)[0]; p += 4
    p += 4
    ncode = struct.unpack_from("<i", data, p)[0]; p += 4
    p += ncode * 4
    nlv = struct.unpack_from("<i", data, p)[0]; p += 4
    for _ in range(nlv):
        s, p = rd_s4(data, p); p += 8
    nu = struct.unpack_from("<i", data, p)[0]; p += 4
    for _ in range(nu):
        s, p = rd_s4(data, p)
    nk = struct.unpack_from("<i", data, p)[0]; p += 4
    consts = []
    for _ in range(nk):
        t = data[p]; p += 1
        if t == 4:
            s, p = rd_s4(data, p)
            consts.append(s.decode("utf-8", "replace") if s else "?")
        elif t == 3:
            consts.append("<num>"); p += 8
        elif t == 1:
            consts.append("<bool>"); p += 1
        else:
            consts.append("<nil>")
    np = struct.unpack_from("<i", data, p)[0]; p += 4
    nli = struct.unpack_from("<i", data, p)[0]; p += 4
    p += nli * 4
    ind = "  " * depth
    out.append(f"{ind}proto src={src!r}: code={ncode} consts={nk} protos={np} lineinfo={nli}")
    for i in range(np):
        ok, p = walk(data, p, depth + 1, out)
        if not ok: return False, p
    return True, p


def main() -> int:
    mf = MegaFile(MEG)
    # Largest chunk (a story mission).
    sizes = [(len(mf.read(n)), n) for n in mf.names() if n.upper().endswith(".LUA")]
    sizes.sort(reverse=True)
    for size, name in sizes[:3]:
        print(f"== {name} ({size} bytes)")
        out = []
        ok, _ = walk(mf.read(name), HEADER, 0, out)
        for line in out:
            print(line)
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
