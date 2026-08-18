"""Authoritative walker from FUN_1407c5020 + FUN_1407c3cb0 + FUN_1407c3a50:
  TOP: header(22) + linedefined(int) + lastlinedefined(int) + proto count
       + protos(LoadFunction each)
  LoadFunction: source(4-byte len str) + linedefined(int) + lastlinedefined(int)
       + nups,numparams,is_vararg,maxstacksize(4 bytes)
       + code count(int) + code(count*4)
       + locvars: count + [str + 2 ints] each          (FUN_1407c4260)
       + upvalue count(int) + upvalue names(count*8-byte str)  (LoadString)
       + constants: count + [type byte + data]          (FUN_1407c3a50)
       + nested proto count + protos(recursive)          (same fn)
       + lineinfo count(int) + lineinfo(count*4)
NOTE: source strings are 4-byte length; constant strings are ALSO 4-byte
length (verified: `04 08 00 00 00 require`). The upvalue names come AFTER
locvars. Protos come after constants, before lineinfo.
"""
import struct
import sys

from meg_reader import MegaFile

MEG = r"C:\Program Files (x86)\Steam\steamapps\common\Star Wars Empire at War\corruption\Data\config.meg"
HEADER = 22


def rd_str4(data, p):
    n = struct.unpack_from("<i", data, p)[0]
    p += 4
    if n < 0 or p + n > len(data):
        return None, p
    return data[p : p + n], p + n


def load_function(data, p, depth=0):
    """FUN_1407c3cb0. All sources (top and nested) are 4-byte-length
    strings with 0 = nil. Returns (ok, why, p)."""
    s, p = rd_str4(data, p)
    if s is None: return False, "source", p
    for _ in range(2):
        v = struct.unpack_from("<i", data, p)[0]; p += 4
        if v < 0: return False, "linedef", p
    p += 4  # nups numparams is_vararg maxstacksize
    ncode = struct.unpack_from("<i", data, p)[0]; p += 4
    if ncode < 0: return False, "neg code", p
    p += ncode * 4
    if p > len(data): return False, "code over", p
    # locvars: count + [str + 2 ints] each
    nlv = struct.unpack_from("<i", data, p)[0]; p += 4
    if nlv < 0: return False, "neg locvars", p
    for _ in range(nlv):
        s, p = rd_str4(data, p)
        if s is None: return False, "locvar str", p
        p += 8
    # upvalue count + names (8-byte strings)
    nu = struct.unpack_from("<i", data, p)[0]; p += 4
    if nu < 0: return False, "neg upvals", p
    for _ in range(nu):
        s, p = rd_str4(data, p)
        if s is None: return False, "upval str", p
    # constants: count + [type byte + data]
    nk = struct.unpack_from("<i", data, p)[0]; p += 4
    if nk < 0: return False, "neg const", p
    for _ in range(nk):
        t = data[p]; p += 1
        if t == 4:
            s, p = rd_str4(data, p)
            if s is None: return False, "const str", p
        elif t == 3:
            p += 8
        elif t == 1:
            p += 1
        elif t != 0:
            return False, f"const type {t}", p
    # nested protos
    np = struct.unpack_from("<i", data, p)[0]; p += 4
    if np < 0: return False, "neg protos", p
    for _ in range(np):
        ok, why, p = load_function(data, p, depth + 1)
        if not ok: return False, f"proto: {why}", p
    return True, "", p


def walk_chunk(data):
    """The chunk is a single LoadFunction (FUN_1407c3cb0) with a source."""
    if len(data) < HEADER + 8:
        return False, "short", 0
    return load_function(data, HEADER)


def main() -> int:
    mf = MegaFile(MEG)
    ok = fail = 0
    consumed = 0
    fails = []
    stats = []
    for name in mf.names():
        if not name.upper().endswith(".LUA"):
            continue
        data = mf.read(name)
        good, why, endp = walk_chunk(data)
        if good:
            ok += 1
            if endp == len(data):
                consumed += 1
        else:
            fail += 1
            if len(fails) < 10:
                fails.append((name, why, endp))
    print(f"parsed {ok}, failed {fail}, fully consumed {consumed}/{ok}")
    for name, why, endp in fails:
        print(f"  FAIL {name}: {why} @ 0x{endp:x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
