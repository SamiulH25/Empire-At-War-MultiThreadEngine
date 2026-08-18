"""Summarize the slot diagnostics in eaw_patch_debug.txt: for each slot,
what (pointer, count) values appear and how often, so we can see the
menu vs battle state difference."""
import re
import sys
from collections import Counter

p = sys.argv[1] if len(sys.argv) > 1 else (
    r"C:\Program Files (x86)\Steam\steamapps\common\Star Wars Empire at War"
    r"\corruption\eaw_patch_debug.txt"
)

pat = re.compile(r"\[hook\] slot (\d) @ \+0x([0-9A-Fa-f]+): ptr=([0-9A-Fa-f]+) count=(-?\d+)")
combos = Counter()
ptr_count = {}
kept = Counter()
with open(p, encoding="utf-8", errors="replace") as f:
    for line in f:
        m = pat.match(line)
        if m:
            i, off, ptr, cnt = m.groups()
            combos[(int(i), int(cnt))] += 1
            if ptr != "0" * 16:
                d = ptr_count.setdefault(int(i), {})
                d[ptr] = d.get(ptr, 0) + 1
            continue
        m2 = re.match(r"\[hook\] scanned (\d+) lists, kept (\d+)", line)
        if m2:
            kept[m2.group(2)] += 1

print("== (slot, count) value frequency ==")
for (i, cnt), n in sorted(combos.items(), key=lambda kv: (kv[0][0], kv[0][1])):
    print(f"  slot {i}: count={cnt}  x{n}")

print("\n== non-null pointers per slot (distinct addresses) ==")
for i in sorted(ptr_count):
    d = ptr_count[i]
    print(f"  slot {i}: {len(d)} distinct ptr(s)")
    for ptr, n in sorted(d.items(), key=lambda kv: -kv[1])[:3]:
        print(f"    {ptr}  x{n}")

print("\n== kept-list frequency ==")
for k, n in sorted(kept.items()):
    print(f"  kept {k} lists x{n}")
