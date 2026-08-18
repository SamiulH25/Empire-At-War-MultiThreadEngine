"""Walk with constants = [type byte][4-byte length][data].
The first proto's source may be null (0-length) or the file name."""
import struct
import sys

from meg_reader import MegaFile

MEG = r"C:\Program Files (x86)\Steam\steamapps\common\Star Wars Empire at War\corruption\Data\config.meg"
HEADER = 22


def rd_s4(data, p):
    n = struct.unpack_from("<i", data, p)[0]
    p += 4
    if n < 0 or p + n > len(data):
        return False, p
    return True, p + n


def walk(data, p=HEADER, depth=0):
    try:
        if depth == 0:
            ok, p = rd_s4(data, p)  # top function has a plain source string
            if not ok: return False, "source", p
        else:
            # Nested proto source is a TValue-like: [type byte][4-byte len][str]
            t = data[p]; p += 1
            if t == 4:
                ok, p = rd_s4(data, p)
                if not ok: return False, "proto source", p
            elif t == 0:
                pass  # nil source
            else:
                return False, f"proto source type {t}", p
        for _ in range(2):
            v = struct.unpack_from("<i", data, p)[0]; p += 4
            if v < 0: return False, "linedef", p
        p += 4
        ncode = struct.unpack_from("<i", data, p)[0]; p += 4
        if ncode < 0: return False, "neg code", p
        p += ncode * 4
        if p > len(data): return False, "code over", p
        # constants: count + [type + 4-byte len + data]
        nk = struct.unpack_from("<i", data, p)[0]; p += 4
        if nk < 0: return False, "neg const", p
        for _ in range(nk):
            t = data[p]; p += 1
            if t == 4:
                ok, p = rd_s4(data, p)
                if not ok: return False, "const str", p
            elif t == 3:
                p += 8
            elif t == 1:
                p += 1
            elif t != 0:
                return False, f"const type {t}", p
        # upvalues: count + [str + 2 ints] each
        nu = struct.unpack_from("<i", data, p)[0]; p += 4
        if nu < 0: return False, "neg upvals", p
        for _ in range(nu):
            ok, p = rd_s4(data, p)
            if not ok: return False, "upval str", p
            p += 8
        # protos
        np = struct.unpack_from("<i", data, p)[0]; p += 4
        if np < 0: return False, "neg proto", p
        for _ in range(np):
            good, why, p = walk(data, p, depth + 1)
            if not good: return False, f"proto: {why}", p
        return True, "", p
    except Exception as e:
        return False, str(e), p


def main() -> int:
    mf = MegaFile(MEG)
    ok = fail = 0
    consumed = 0
    fails = []
    for name in mf.names():
        if not name.upper().endswith(".LUA"):
            continue
        data = mf.read(name)
        good, why, endp = walk(data)
        if good:
            ok += 1
            if endp == len(data):
                consumed += 1
        else:
            fail += 1
            if len(fails) < 8:
                fails.append((name, why, endp))
    print(f"parsed {ok}, failed {fail}, fully consumed {consumed}")
    for name, why, endp in fails:
        print(f"  FAIL {name}: {why} @ 0x{endp:x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
