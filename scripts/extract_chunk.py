"""Extract a named Lua chunk from config.meg to a file."""
import sys

from meg_reader import MegaFile

MEG = r"C:\Program Files (x86)\Steam\steamapps\common\Star Wars Empire at War\corruption\Data\config.meg"


def main() -> int:
    filt = sys.argv[1] if len(sys.argv) > 1 else "BUILDGROUNDFORCESPLAN"
    out = sys.argv[2] if len(sys.argv) > 2 else r"C:\Dev\Empire-At-War-MultiThreadEngine\src\build\chunk.lua"
    mf = MegaFile(MEG)
    name = [n for n in mf.names() if filt.upper() in n.upper() and n.upper().endswith(".LUA")][0]
    data = mf.read(name)
    with open(out, "wb") as f:
        f.write(data)
    print(f"{name} ({len(data)} bytes) -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
