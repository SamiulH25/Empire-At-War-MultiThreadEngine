"""Brute-force: header size 12..30, optional extra function-header byte,
then vanilla walk with 6-byte instructions. Report which consumes the file."""
import struct
import sys

from meg_reader import MegaFile

MEG = r"C:\Program Files (x86)\Steam\steamapps\common\Star Wars Empire at War\corruption\Data\config.meg"
FILTER = "BUILDGROUNDFORCESPLAN"


class Reader:
    def __init__(self, data, pos):
        self.d = data
        self.p = pos
        self.ok = True
        self.failwhy = ""

    def fail(self, why):
        if self.ok:
            self.failwhy = why
        self.ok = False

    def byte(self):
        if self.p >= len(self.d): self.fail("eof"); return 0
        v = self.d[self.p]; self.p += 1; return v

    def int_(self):
        if self.p + 4 > len(self.d): self.fail("eof"); return 0
        v = struct.unpack_from("<i", self.d, self.p)[0]
        self.p += 4
        if v < 0: self.fail(f"neg {v}")
        return v

    def str_(self):
        size = self.int_()
        if not self.ok: return ""
        if size == 0: return None
        if self.p + size > len(self.d): self.fail("eof str"); return ""
        s = self.d[self.p:self.p+size]; self.p += size
        return s

    def code(self):
        n = self.int_(); self.p += n * 6
        if self.p > len(self.d): self.fail("code over")

    def constants(self):
        n = self.int_()
        for _ in range(n):
            t = self.byte()
            if t == 0: pass
            elif t == 1: self.byte()
            elif t == 3: self.p += 8
            elif t == 4: self.str_()
            else: self.fail(f"const {t}"); return
            if not self.ok: return

    def debug(self):
        n = self.int_(); self.p += n * 4
        n = self.int_()
        for _ in range(n):
            self.str_(); self.int_(); self.int_()
            if not self.ok: return
        n = self.int_()
        for _ in range(n):
            self.str_()
            if not self.ok: return

    def func(self, extra_byte):
        self.str_(); self.int_(); self.int_()
        self.byte(); self.byte(); self.byte(); self.byte()
        if extra_byte: self.byte()
        self.code(); self.constants()
        n = self.int_()
        for _ in range(n):
            self.func(extra_byte)
            if not self.ok: return
        self.debug()


def main() -> int:
    mf = MegaFile(MEG)
    found = [n for n in mf.names() if FILTER.upper() in n.upper() and n.upper().endswith(".LUA")]
    data = mf.read(found[0])
    print(f"entry: {found[0]} ({len(data)} bytes)")
    for hlen in range(12, 31):
        for extra in (0, 1):
            r = Reader(data, hlen)
            r.func(extra)
            if r.ok:
                st = "CONSUMED-ALL" if r.p == len(data) else f"ended-{r.p}/{len(data)}"
                print(f"header={hlen:2d} extra={extra}: {st}  <== POSSIBLE")
            else:
                print(f"header={hlen:2d} extra={extra}: fail@{r.failwhy}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
