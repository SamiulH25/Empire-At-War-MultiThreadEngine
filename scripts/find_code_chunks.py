"""Scan all config.meg Lua chunks and report which have code (instructions)
in any proto, using the confirmed walker."""
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


def walk(data, p, stats):
    s, p = rd_s4(data, p)
    if s is None: return False, p
    for _ in range(2):
        v = struct.unpack_from("<i", data, p)[0]; p += 4
        if v < 0: return False, p
    p += 4
    ncode = struct.unpack_from("<i", data, p)[0]; p += 4
    if ncode < 0: return False, p
    stats["code"] += ncode
    if ncode > 0: stats["code_chunks"] += 1
    p += ncode * 4
    if p > len(data): return False, p
    nlv = struct.unpack_from("<i", data, p)[0]; p += 4
    for _ in range(nlv):
        s, p = rd_s4(data, p)
        if s is None: return False, p
        p += 8
    nu = struct.unpack_from("<i", data, p)[0]; p += 4
    for _ in range(nu):
        s, p = rd_s4(data, p)
        if s is None: return False, p
    nk = struct.unpack_from("<i", data, p)[0]; p += 4
    for _ in range(nk):
        t = data[p]; p += 1
        if t == 4:
            s, p = rd_s4(data, p)
            if s is None: return False, p
        elif t == 3:
            p += 8
        elif t == 1:
            p += 1
        elif t != 0:
            return False, p
    np = struct.unpack_from("<i", data, p)[0]; p += 4
    for _ in range(np):
        ok, p = walk(data, p, stats)
        if not ok: return False, p
    nli = struct.unpack_from("<i", data, p)[0]; p += 4
    p += nli * 4
    return True, p


def main() -> int:
    mf = MegaFile(MEG)
    with_code = []
    total = 0
    for name in mf.names():
        if not name.upper().endswith(".LUA"):
            continue
        total += 1
        stats = {"code": 0, "code_chunks": 0}
        try:
            ok, _ = walk(mf.read(name), HEADER, stats)
        except Exception:
            ok = False
        if ok and stats["code"] > 0:
            with_code.append((name, stats["code"]))
    print(f"total chunks: {total}")
    print(f"chunks with code: {len(with_code)}")
    for name, c in with_code[:15]:
        print(f"  {c:6d} insns  {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
