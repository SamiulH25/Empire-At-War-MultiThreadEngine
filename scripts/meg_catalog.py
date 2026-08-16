"""Catalog the contents of a .meg archive by category.

Usage: python scripts/meg_catalog.py <file.meg> [output.md]
Prints (and optionally writes) a categorized inventory: XML configs, Lua
scripts, maps, models, textures, shaders, etc. with counts and sizes.
"""
import sys, os, collections
sys.path.insert(0, os.path.dirname(__file__))
from meg_reader import MegaFile

def categorize(name):
    n = name.upper()
    if n.endswith(".LUA"): return "lua"
    if n.endswith(".XML"): return "xml"
    if n.endswith(".ALO"): return "model"
    if n.endswith(".DDS"): return "texture"
    if n.endswith(".FX"): return "shader"
    if n.endswith(".TGA"): return "image"
    if n.endswith(".WAV") or n.endswith(".MP3"): return "audio"
    if n.endswith(".BINK") or n.endswith(".BIK"): return "video"
    if ".MAP" in n or n.endswith(".MEG"): return "map"
    if n.endswith(".TXT"): return "text"
    return "other"

def main():
    path = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else None
    mf = MegaFile(path)
    cats = collections.defaultdict(list)
    for name in mf.names():
        cats[categorize(name)].append(name)
    lines = [f"# Catalog: {os.path.basename(path)}", "",
             f"{len(mf.names())} files, {mf.num_files} file table entries", ""]
    for cat in sorted(cats):
        names = sorted(cats[cat])
        total = 0
        for n in names:
            try:
                total += len(mf.read(n))
            except KeyError:
                pass
        lines.append(f"## {cat} ({len(names)} files, {total/1024:.0f} KB)")
        for n in names[:30]:
            lines.append(f"- {n}")
        if len(names) > 30:
            lines.append(f"- ... and {len(names)-30} more")
        lines.append("")
    text = "\n".join(lines)
    print(text)
    if out:
        with open(out, "w") as f:
            f.write(text)
        print(f"wrote {out}")

if __name__ == "__main__":
    main()
