# EAW MultiThreadEngine — Phased Research & Build Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **Target executor:** DeepSeek V4 Flash (low-context model). Every task below is self-contained: it repeats the paths, commands, and facts it needs. You do not need to remember anything from a previous task.

**Goal:** Complete the research phase of the EAW multi-threaded engine project and produce a working prototype DLL that hooks the game loop, per the approved design spec.

**Architecture:** Research → findings docs → .meg parser (Python) → threading design → C++20 proxy DLL (d3d9.dll) injected into `StarWarsG.exe` that hooks the sim tick and proves one parallel slice.

**Tech Stack:** Ghidra 11.x (static analysis), x64dbg + Windows Performance Recorder (dynamic), Python 3 + pefile (research tooling), C++20 + CMake + MinGW-w64 (DLL), MinHook (detours), git + GitHub.

## Global Constraints

- Repo: `https://github.com/SamiulH25/Empire-At-War-MultiThreadEngine`, branch `main`
- **Never commit** the game's exe files, .meg files, or any game asset to the repo.
- Game files are reference data, not repo content. Read from disk, document findings in `docs/research/`.
- All research findings go under `docs/research/`. All research code goes under `scripts/` (Python) or `src/` (C++).
- Spec that this plan implements: `docs/superpowers/specs/2026-08-15-eaw-multithread-engine-design.md`
- **Machines:** the game files live on a Linux box; the game *runs* on a Windows box. Both have a clone of this repo. Which machine a task uses is stated in the task header:
  - Phase 0: Windows (toolchain), Linux (game-file verification)
  - Phase 1: Linux (game files + Ghidra headless)
  - Phase 2: Windows (the game runs there)
  - Phase 3: Linux (game files)
  - Phase 4: either (doc edits only) — use Windows
  - Phase 5: Windows (game runtime)
  - Phase 6: Windows
- **Repo coordination:** before starting work on a machine, run `git pull`. After committing, run `git push origin main` so the other machine stays in sync. Phase headers repeat this.
- **Commit recipe (use for every task):** on Linux/macOS:
  ```bash
  git add <files>
  git commit -F - <<'EOF'
  <type>: <short description>
  
  <one or two lines of detail>
  
  Co-authored-by: CommandCodeBot <noreply@commandcode.ai>
  EOF
  ```
  On Windows PowerShell (heredocs don't work the same; write the message to a file):
  ```powershell
  Set-Content -Path .gitmessage -Value "feat: short description`n`ndetail line`n`nCo-authored-by: CommandCodeBot <noreply@commandcode.ai>"
  git add <files>
  git commit -F .gitmessage
  Remove-Item .gitmessage
  ```

## Shared Facts (repeat these wherever a task needs them)

**Game install paths:**
- Linux: `/home/bob2142/.local/share/Steam/steamapps/common/Star Wars Empire at War`
- Windows: find via Steam → right-click game → Manage → Browse local files (usually `C:\Program Files (x86)\Steam\steamapps\common\Star Wars Empire at War`). The path contains spaces — always quote it in commands.
- Repo clone (Linux): `/home/bob2142/Dev/Le Passion/Empire At War MultiTHreadEngine`

**Key binaries (paths relative to game root):**
- Base game exe: `GameData/StarWarsG.exe` — x86-64 PE32+, image base 0x140000000, entry VA 0x1406CC318, image size 0xBD8000, 7 sections
- FoCs exe: `corruption/StarWarsG.exe` — x86-64 PE32+, image base 0x140000000, entry VA 0x14076A428, image size 0xCC0000, 7 sections
- FoCs launcher: `corruption/swfoc.exe` — links Intel TBB (`tbbR.dll`) and libcurl
- Perception DLL (both dirs): `PerceptionFunctionG.dll` — unmangled C++ exports, the engine class map
- TBB runtime: `tbbR.dll` (in both `GameData/` and `corruption/`)
- Data archives: `corruption/Data/*.meg` and `GameData/Data/*.meg`

**Threading evidence already found in `corruption/StarWarsG.exe` (file offsets):**
| File offset | String / evidence |
|---|---|
| 0x008505F0 | `ThreadLockMutexClass -- %s failed to obtain mutex within 10 seconds (current owner is %s)` |
| 0x00850650 | `ThreadLockMutexClass -- Failed to obtain mutex within 10 seconds` |
| 0x00803B54 | `LoadThread` |
| 0x00855858 | `LuaCreateThread: _Name -- Expected a thread id parameter` |
| 0x008558A3 | `LuaCreateThread::Ki...` (second error string) |

**Exact string bytes to search for (hex):**
- `ThreadLockMutexClass` = `54 68 72 65 61 64 4C 6F 63 6B 4D 75 74 65 78 43 6C 61 73 73`
- `LuaCreateThread` = `4C 75 61 43 72 65 61 74 65 54 68 72 65 61 64`
- `LoadThread` = `4C 6F 61 64 54 68 72 65 61 64`

**File-offset → VA conversion** (corruption/StarWarsG.exe): VA = 0x140000000 + (file_offset − 0x400) when the offset is inside the first section's raw range. `.text` starts at raw 0x400 and VA 0x1000, so raw 0x400 = VA 0x140001000. Verify any conversion in Ghidra rather than trusting arithmetic.

**.meg header facts (from hex dump of `corruption/Data/config.meg`):**
```
Offset 0x00: u32 = 1046 (entry count — same value twice: bytes 0x00 and 0x04)
Offset 0x08: 0x42 ('B' — type/flags byte)
Offset 0x09: entry name in UTF-16LE, e.g. "DATA\SCRIPTS\AI\AI_PLAN_EXPANSIONGENERIC_GENERATEMAGICCASHDROP.LUA"
              then 0x3D ('='), then another UTF-16LE name, and so on
```
`64Patch.meg` starts with u32 = 324 (`0x144`), `shaders.meg` starts with u32 = 82 (`0x52`). Same structure.

---

## Phase 0 — Toolchain & Workspace Setup

**Machine:** Windows (game-files checks in Task 0.3 also run on Linux). `git pull` on both machines before starting.

### Task 0.1: Repo Setup on Windows

**Files:**
- Create: `<windows workspace dir>\Empire-At-War-MultiThreadEngine\` (clone of the GitHub repo)

**Interfaces:**
- Produces: local git clone on the Windows machine, identity configured

- [ ] **Step 1: Install git for Windows** if not present. Download from https://git-scm.com/download/win, install with defaults. Verify:
  ```powershell
  git --version
  ```
  Expected: prints `git version 2.4x.x` or newer.

- [ ] **Step 2: Clone the repo.** Pick a workspace folder (e.g. `C:\Dev`):
  ```powershell
  mkdir C:\Dev
  cd C:\Dev
  git clone https://github.com/SamiulH25/Empire-At-War-MultiThreadEngine.git
  cd Empire-At-War-MultiThreadEngine
  ```
  Expected: `README.md` and `docs\` exist after clone.

- [ ] **Step 3: Configure identity** (same as GitHub account):
  ```powershell
  git config --global user.name "SamiulH25"
  git config --global user.email "194833810+SamiulH25@users.noreply.github.com"
  ```

- [ ] **Step 4: Verify.** Run:
  ```powershell
  git log --oneline -3
  ```
  Expected: shows the initial commit `5459659 Initial research setup: design spec, binary findings, and 7 research docs`.

- [ ] **Step 5: Done.** This task only configures; nothing to commit.

### Task 0.2: Research Toolchain Install (Python, Ghidra, x64dbg, WPR, MinGW, CMake)

**Files:**
- None in repo (system installs only)

**Interfaces:**
- Produces: working toolchain that all later tasks invoke by name

- [ ] **Step 1: Install Python 3.12** from https://www.python.org/downloads/windows/ — check "Add python.exe to PATH" during install. Verify:
  ```powershell
  python --version
  ```
  Expected: `Python 3.12.x`.

- [ ] **Step 2: Install pefile:**
  ```powershell
  pip install pefile
  python -c "import pefile; print('pefile ok')"
  ```
  Expected: `pefile ok`.

- [ ] **Step 3: Install Ghidra 11.x.** Download the latest release zip from https://github.com/NationalSecurityAgency/ghidra/releases (file name like `ghidra_11.x_PUBLIC_YYYYMMDD.zip`). Extract to `C:\Tools\ghidra`. If the machine lacks Java 21, install the JDK from https://adoptium.net/ first. Verify:
  ```powershell
  C:\Tools\ghidra\support\analyzeHeadless.bat
  ```
  Expected: prints usage text (no error).

- [ ] **Step 4: Install x64dbg.** Download the snapshot release from https://x64dbg.com/ and extract to `C:\Tools\x64dbg`. Verify `C:\Tools\x64dbg\x64\x64dbg.exe` exists.

- [ ] **Step 5: Install Windows Performance Toolkit** (for CPU sampling in Phase 2). Open PowerShell **as Administrator**:
  ```powershell
  winget install Microsoft.WindowsPerformanceToolkit
  ```
  Verify: `wpr -help` prints usage.

- [ ] **Step 6: Install the C++ toolchain — MinGW-w64, CMake, Ninja:**
  ```powershell
  winget install BrechtSanders.WinLibs.POSIX.UCRT
  winget install Kitware.CMake
  winget install Ninja-build.Ninja
  ```
  Then open a NEW PowerShell (so PATH picks up the installs) and verify:
  ```powershell
  g++ --version
  cmake --version
  ninja --version
  ```
  Expected: g++ 13.x or newer (MinGW-w64), cmake 3.2x+, ninja 1.x. If `g++` is not found, add the WinLibs `bin` directory to PATH manually (installer shows the path, e.g. `C:\Program Files\WinLibs\...\mingw64\bin`).

- [ ] **Step 7: Install Process Explorer** (thread census in Phase 2):
  ```powershell
  winget install Microsoft.Sysinternals.ProcessExplorer
  ```

- [ ] **Step 8: Record toolchain in the repo docs.** Modify `docs/research/07-toolchain.md`: add a "Windows host" section listing every tool installed in this task with its version (from the verify outputs) and install path. Then commit:
  ```powershell
  Set-Content -Path .gitmessage -Value "docs: record Windows toolchain setup`n`nVersions and paths for Python, Ghidra, x64dbg, WPR, MinGW-w64, CMake, Ninja, Process Explorer.`n`nCo-authored-by: CommandCodeBot <noreply@commandcode.ai>"
  git add docs/research/07-toolchain.md
  git commit -F .gitmessage
  Remove-Item .gitmessage
  ```

### Task 0.3: Baseline Fact-Check Script

**Files:**
- Create: `scripts/verify_install.py`

**Interfaces:**
- Consumes: Python + pefile (Task 0.2)
- Produces: `scripts/verify_install.py` — prints a fact report; every later task can run it to confirm its inputs are present

- [ ] **Step 1: Write the script.** Create `scripts/verify_install.py`:

```python
"""Verify the EAW game install matches the facts in the research docs.

Usage: python scripts/verify_install.py "<path to game root>"
Prints PE facts for both StarWarsG.exe binaries and the meg header counts.
Exits 0 if all facts match, 1 otherwise.
"""
import sys, struct
import pefile

EXPECTED = {
    "GameData/StarWarsG.exe": {"ImageBase": 0x140000000, "EntryVA": 0x1406CC318,
                                "ImageSize": 0xBD8000, "Sections": 7},
    "corruption/StarWarsG.exe": {"ImageBase": 0x140000000, "EntryVA": 0x14076A428,
                                  "ImageSize": 0xCC0000, "Sections": 7},
    "corruption/Data/config.meg": {"FirstU32": 1046},
    "corruption/Data/64Patch.meg": {"FirstU32": 324},
    "corruption/Data/shaders.meg": {"FirstU32": 82},
}

def check_exe(path, facts):
    pe = pefile.PE(path, fast_load=True)
    entry_va = pe.OPTIONAL_HEADER.ImageBase + pe.OPTIONAL_HEADER.AddressOfEntryPoint
    got = {
        "ImageBase": pe.OPTIONAL_HEADER.ImageBase,
        "EntryVA": entry_va,
        "ImageSize": pe.OPTIONAL_HEADER.SizeOfImage,
        "Sections": pe.FILE_HEADER.NumberOfSections,
    }
    ok = got == facts
    print(f"[{'OK' if ok else 'MISMATCH'}] {path}")
    if not ok:
        for k in facts:
            if got.get(k) != facts[k]:
                print(f"    {k}: expected {hex(facts[k])}, got {hex(got[k])}")
    return ok

def check_meg(path, facts):
    with open(path, "rb") as f:
        first = struct.unpack_from("<I", f.read(4), 0)[0]
    ok = first == facts["FirstU32"]
    print(f"[{'OK' if ok else 'MISMATCH'}] {path} first u32 = {first}")
    return ok

def main():
    root = sys.argv[1].rstrip("\\/")
    all_ok = True
    for rel, facts in EXPECTED.items():
        path = f"{root}/{rel}"
        if rel.endswith(".exe"):
            all_ok &= check_exe(path, facts)
        else:
            all_ok &= check_meg(path, facts)
    sys.exit(0 if all_ok else 1)

if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run it on the Linux machine** (game files live there):
  ```bash
  python3 scripts/verify_install.py "/home/bob2142/.local/share/Steam/steamapps/common/Star Wars Empire at War"
  ```
  Expected: every line starts with `[OK]`, exit code 0. If any `MISMATCH`: STOP — the game files differ from the research docs; update `docs/research/01-binary-map.md` with the real values first.

- [ ] **Step 3: Run it on Windows** (after copying the script over via the repo — `git pull` first):
  ```powershell
  python scripts\verify_install.py "C:\Program Files (x86)\Steam\steamapps\common\Star Wars Empire at War"
  ```
  Expected: same `[OK]` lines, exit code 0. If the path is wrong, find the real one via Steam → Manage → Browse local files.

- [ ] **Step 4: Commit** (from whichever machine has the committed file — do this on Linux):
  ```bash
  git add scripts/verify_install.py
  git commit -F - <<'EOF'
  feat: add install verification script

  Checks PE facts and meg header counts against documented values, on any OS.

  Co-authored-by: CommandCodeBot <noreply@commandcode.ai>
  EOF
  git push origin main
  ```

---

## Phase 1 — Static Analysis

**Machine:** Linux (game files + Ghidra headless live there). `git pull` before starting.

### Task 1.1: String Census of All Binaries

**Files:**
- Create: `scripts/string_census.py`
- Modify: `docs/research/01-binary-map.md` (append "String Census" section)

**Interfaces:**
- Consumes: Python 3 (Task 0.2), game install verified (Task 0.3)
- Produces: `docs/research/01-binary-map.md` string census table — consumed by Tasks 1.4, 1.5, 1.6, 1.7

- [ ] **Step 1: Write `scripts/string_census.py`:**

```python
"""Extract printable ASCII/UTF-16LE strings from EAW binaries and classify
threading/engine-relevant ones. Usage:
  python scripts/string_census.py "<game root>" "<output .md path>"
"""
import re, sys, os

BINARIES = [
    "corruption/StarWarsG.exe",
    "GameData/StarWarsG.exe",
    "corruption/PerceptionFunctionG.dll",
    "GameData/PerceptionFunctionG.dll",
    "corruption/swfoc.exe",
]

# (regex, category label) — applied to each extracted string
INTERESTING = [
    (r"[Tt]hread", "threading"),
    (r"[Mm]utex|CriticalSection|Semaphore|Event", "sync primitives"),
    (r"[Ll]ua", "lua"),
    (r"[Mm]ega", "megafile"),
    (r"[Pp]athfind", "pathfinding"),
    (r"[Pp]erception", "perception"),
    (r"[Jj]ob|[Ww]orker|[Tt]ask", "job/worker"),
    (r"[Aa]ffinity|[Cc]ore", "affinity"),
]

def extract_ascii(data, minlen=8):
    out = []
    for m in re.finditer(rb"[\x20-\x7e]{%d,}" % minlen, data):
        out.append((m.start(), m.group().decode()))
    return out

def extract_utf16le(data, minlen=8):
    out = []
    for m in re.finditer(rb"(?:[\x20-\x7e]\x00){%d,}" % minlen, data):
        out.append((m.start(), m.group().decode("utf-16le")))
    return out

def main():
    root = sys.argv[1].rstrip("\\/")
    out_path = sys.argv[2]
    lines = ["# String Census\n", "Auto-generated by scripts/string_census.py\n"]
    for rel in BINARIES:
        path = f"{root}/{rel}"
        if not os.path.exists(path):
            lines.append(f"\n## {rel}\n\nMISSING — skipped\n")
            continue
        with open(path, "rb") as f:
            data = f.read()
        strings = extract_ascii(data) + extract_utf16le(data)
        # unique, sorted by offset
        seen, uniq = set(), []
        for off, s in sorted(strings):
            if s not in seen:
                seen.add(s)
                uniq.append((off, s))
        hits = {}
        for off, s in uniq:
            for pat, cat in INTERESTING:
                if re.search(pat, s):
                    hits.setdefault(cat, []).append((off, s))
        lines.append(f"\n## {rel}\n\nTotal strings: {len(uniq)}\n")
        for cat in sorted(hits):
            lines.append(f"\n### {cat} ({len(hits[cat])} hits)\n")
            for off, s in hits[cat][:40]:
                lines.append(f"- `0x{off:08X}` {s}")
    with open(out_path, "w") as f:
        f.write("\n".join(lines))
    print(f"wrote {out_path}")

if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run it:**
  ```bash
  cd /home/bob2142/Dev/Le\ Passion/Empire\ At\ War\ MultiTHreadEngine
  python3 scripts/string_census.py "/home/bob2142/.local/share/Steam/steamapps/common/Star Wars Empire at War" "$(pwd)/docs/research/01-string-census.md"
  ```
  Expected: prints `wrote .../01-string-census.md`; the file contains a `### threading` section for `corruption/StarWarsG.exe` with at least these entries: `ThreadLockMutexClass ...`, `LoadThread`, `LuaCreateThread`.

- [ ] **Step 3: Merge into the binary map doc.** Append a "## String Census" section to `docs/research/01-binary-map.md` with a link to the generated file and a hand-written table of the 15 most relevant threading strings across all binaries (copy from the generated md). No TBDs — only strings that were actually found.

- [ ] **Step 4: Commit:**
  ```bash
  git add scripts/string_census.py docs/research/01-string-census.md docs/research/01-binary-map.md
  git commit -F - <<'EOF'
  feat: string census of all game binaries

  Classified threading, lua, megafile, pathfinding, perception, and job/worker
  strings with file offsets for both 64-bit exes, the perception DLLs, and swfoc.exe.

  Co-authored-by: CommandCodeBot <noreply@commandcode.ai>
  EOF
  git push origin main
  ```

### Task 1.2: PE Import/Export Deep Dive Script

**Files:**
- Create: `scripts/pe_deepdive.py`
- Modify: `docs/research/01-binary-map.md` (formalize the import/export tables)

**Interfaces:**
- Consumes: Python + pefile (Task 0.2)
- Produces: import/export tables for all 6 binaries in `docs/research/01-binary-map.md` — consumed by Tasks 1.7, 2.4, 5.2

- [ ] **Step 1: Write `scripts/pe_deepdive.py`:**

```python
"""Dump PE imports and exports for EAW binaries. Usage:
  python scripts/pe_deepdive.py "<game root>"
"""
import sys, pefile

BINARIES = [
    "corruption/StarWarsG.exe",
    "GameData/StarWarsG.exe",
    "corruption/PerceptionFunctionG.dll",
    "GameData/PerceptionFunctionG.dll",
    "corruption/swfoc.exe",
    "corruption/tbbR.dll",
]

def main():
    root = sys.argv[1].rstrip("\\/")
    for rel in BINARIES:
        path = f"{root}/{rel}"
        print("=" * 78)
        print(rel)
        pe = pefile.PE(path, fast_load=True)
        pe.parse_data_directories()
        print(f"  Machine: {hex(pe.FILE_HEADER.Machine)}  "
              f"Sections: {pe.FILE_HEADER.NumberOfSections}  "
              f"TimeDateStamp: {pe.FILE_HEADER.TimeDateStamp}")
        if hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
            print(f"  Imports ({len(pe.DIRECTORY_ENTRY_IMPORT)} DLLs):")
            for entry in pe.DIRECTORY_ENTRY_IMPORT:
                print(f"    {entry.dll.decode()} ({len(entry.imports)} funcs)")
                if entry.dll.decode().upper().startswith(("TBB", "D3D", "MSS", "BINK")):
                    for imp in entry.imports[:30]:
                        name = imp.name.decode() if imp.name else f"ordinal_{imp.ordinal}"
                        print(f"      -> {name}")
        if hasattr(pe, "DIRECTORY_ENTRY_EXPORT"):
            print(f"  Exports ({len(pe.DIRECTORY_ENTRY_EXPORT.symbols)}):")
            for exp in pe.DIRECTORY_ENTRY_EXPORT.symbols:
                name = exp.name.decode() if exp.name else f"ordinal_{exp.ordinal}"
                print(f"    {name} @ RVA {hex(exp.address)}")

if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run it:**
  ```bash
  python3 scripts/pe_deepdive.py "/home/bob2142/.local/share/Steam/steamapps/common/Star Wars Empire at War" > /tmp/pe_dive.txt 2>&1
  ```
  Expected: `/tmp/pe_dive.txt` shows 6 binaries; `corruption/StarWarsG.exe` imports `d3d9.dll (1 funcs)` and `PerceptionFunctionG.dll (10 funcs)`; `swfoc.exe` imports `tbbR.dll (16 funcs)`; `PerceptionFunctionG.dll` exports `Init_Perception_DLL`.

- [ ] **Step 3: Update the doc.** In `docs/research/01-binary-map.md`, replace the current "Imports (18 DLLs)" list with a full formatted table per binary (DLL, func count, notes) copied from the script output. Add the exact `d3d9.dll` import name (expected: `Direct3DCreate9`) and the 10 `PerceptionFunctionG.dll` import names — write the real ones from the output, don't guess.

- [ ] **Step 4: Commit:**
  ```bash
  git add scripts/pe_deepdive.py docs/research/01-binary-map.md
  git commit -F - <<'EOF'
  feat: PE deep-dive script and formalized import/export tables

  Exact import/export surface for both exes, perception DLLs, swfoc.exe, tbbR.dll.
  Confirms d3d9 proxy surface and TBB linkage facts.

  Co-authored-by: CommandCodeBot <noreply@commandcode.ai>
  EOF
  git push origin main
  ```

### Task 1.3: Ghidra Headless Project + Auto-Analysis

**Files:**
- Create: `scripts/ghidra_import.py` (headless import script)
- Modify: `docs/research/01-binary-map.md` (add "Ghidra Project Layout" section)

**Interfaces:**
- Consumes: Ghidra (install on Linux: `apt install ghidra` or download zip from https://github.com/NationalSecurityAgency/ghidra/releases), game install (Task 0.3)
- Produces: a Ghidra project at `<repo>/ghidra/` containing `corruption_StarWarsG`, `GameData_StarWarsG`, `PerceptionFunctionG` — consumed by Tasks 1.4–1.7

- [ ] **Step 1: Create the headless import script `scripts/ghidra_import.py`:**

```python
"""Import EAW binaries into a Ghidra project, run auto-analysis, save.
Run via:
  analyzeHeadless <project_dir> eawea -scriptPath <repo>/scripts \
      -postScript ghidra_import.py <game_root>
Ghidra runs this as Jython; getScriptArgs() is available globally.
"""
import java.io.File
from ghidra.app.util.importer import AutoImporter
from ghidra.program.util import DefaultProgramContext
from ghidra.app.services import ProgramManager
from ghidra.util.task import ConsoleTaskMonitor

TARGETS = [
    ("corruption/StarWarsG.exe", "corruption_StarWarsG"),
    ("GameData/StarWarsG.exe", "GameData_StarWarsG"),
    ("corruption/PerceptionFunctionG.dll", "PerceptionFunctionG"),
]

def main():
    root = getScriptArgs()[0].rstrip("\\/")
    monitor = ConsoleTaskMonitor()
    for rel, prog_name in TARGETS:
        path = "%s/%s" % (root, rel)
        program = AutoImporter.importByUsingBestGuess(java.io.File(path), None, monitor)
        if program is None:
            print("FAILED import: %s" % rel)
            continue
        program.setName(prog_name)
        AutoAnalysisManager(program).analyze(monitor)
        print("imported: %s -> %s" % (rel, prog_name))
        # Programs imported via AutoImporter are stored in the project by the
        # headless harness on script exit; no manual save call needed.

if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run it:**
  ```bash
  cd /home/bob2142/Dev/Le\ Passion/Empire\ At\ War\ MultiTHreadEngine
  mkdir -p ghidra
  /path/to/ghidra/support/analyzeHeadless ghidra eawea -scriptPath scripts -postScript ghidra_import.py "/home/bob2142/.local/share/Steam/steamapps/common/Star Wars Empire at War"
  ```
  Expected: three `imported:` lines, no `FAILED`, no exception. Analysis takes several minutes for a 12 MB exe — don't interrupt it.

- [ ] **Step 3: Add the project dir to .gitignore** (Ghidra projects are huge binary blobs; the scripts stay in git, the project stays local):
  ```bash
  echo "ghidra/" >> .gitignore
  ```

- [ ] **Step 4: Document.** Add a "## Ghidra Project Layout" section to `docs/research/01-binary-map.md`: project path, program names, the analyzeHeadless command used, and a note that the project is local-only (gitignored), regenerable via `scripts/ghidra_import.py`.

- [ ] **Step 5: Commit:**
  ```bash
  git add scripts/ghidra_import.py docs/research/01-binary-map.md .gitignore
  git commit -F - <<'EOF'
  feat: ghidra headless import script

  Imports both StarWarsG.exe binaries and PerceptionFunctionG.dll with
  auto-analysis; project kept local, scripts in repo.

  Co-authored-by: CommandCodeBot <noreply@commandcode.ai>
  EOF
  git push origin main
  ```

### Task 1.4: Locate the Main Loop

**Files:**
- Modify: `docs/research/04-simulation-architecture.md` (replace "Hypotheses only" header with findings)

**Interfaces:**
- Consumes: Ghidra project (Task 1.3), string census offsets (Task 1.1)
- Produces: documented main loop address + phase call structure in doc 04 — consumed by Tasks 2.4, 5.3

- [ ] **Step 1: Open the Ghidra project** on Linux (GUI via `ghidraRun` if a display is available, otherwise use headless analysis queries with `analyzeHeadless -process -noanalysis` plus scripts). Navigate to program `corruption_StarWarsG`, jump to the entry point VA `0x14076A428`.

- [ ] **Step 2: Trace from entry.** The entry is CRT startup → `main`/`WinMain`. Steps to take, recording each function address you pass through:
  1. From the entry, follow the first call into `__scrt_common_main` (identifiable by calls to `GetStartupInfoW`, `GetCommandLineW`).
  2. Inside, find the call to the real `WinMain` (5th arg to `__scrt_common_main` is the main address).
  3. In `WinMain`, look for: `RegisterClassW`/`CreateWindowExW` (window setup), `GetMessageW`/`PeekMessageW` (message loop), and the big per-frame call chain after message dispatch.
  4. The per-frame function that calls many subsystems is the game loop. Mark it with a label `EAW_GameLoop` (press `L` in Ghidra).

- [ ] **Step 3: Identify loop phases.** Inside `EAW_GameLoop`, name each notable call: input (`GetAsyncKeyState`/`GetCursorPos` users), update (large function with many calls), render (call chain ending at `d3d9` imports or the one `Direct3DCreate9` user), vsync (`IDirect3DDevice9::Present`). Use the Decompiler window (Ctrl+E) to sanity-check each.

- [ ] **Step 4: Record findings in `docs/research/04-simulation-architecture.md`.** Replace the header line `**Status:** Hypotheses only...` with `**Status:** Main loop located (date)`. Add a new section "## Main Loop (confirmed)" containing:
  - `EAW_GameLoop` address (VA, e.g. `0x140XXXXXX`)
  - The ordered call list (address + your label + 1-line purpose)
  - Which call is the sim tick, which is render, which is input
  - Whether any loop phases spawn threads (call `CreateThread` or wrap work in `LoadThread`)

- [ ] **Step 5: Verify the address is real.** Cross-check one phase: the render path should reach a function that calls the single `d3d9.dll` import `Direct3DCreate9` (address listed in `docs/research/01-binary-map.md` import table). If the path never reaches it, your loop is mislabeled — redo Step 3.

- [ ] **Step 6: Commit:**
  ```bash
  git add docs/research/04-simulation-architecture.md
  git commit -F - <<'EOF'
  research: locate and document the main game loop

  Entry -> CRT -> WinMain -> EAW_GameLoop with phase call structure,
  labels, and the sim tick / render / input split confirmed.

  Co-authored-by: CommandCodeBot <noreply@commandcode.ai>
  EOF
  git push origin main
  ```

### Task 1.5: Map ThreadLockMutexClass

**Files:**
- Modify: `docs/research/01-binary-map.md` (add mutex map section)

**Interfaces:**
- Consumes: Ghidra project (Task 1.3), string offsets (Shared Facts)
- Produces: full mutex class method map + call sites in doc 01 — consumed by Task 4.1 (threading design)

- [ ] **Step 1: Find the class methods.** In Ghidra, search for the string at file offset `0x008505F0` (bytes `ThreadLockMutexClass`). Every xref to that string is inside a `ThreadLockMutexClass` method. List all xrefs (right-click → References → Show References to Address).

- [ ] **Step 2: Name and classify each method.** For each xref site, decompile the enclosing function. Based on the error text and logic, label:
  - constructor (likely takes no args or a name string)
  - `Acquire` / `Lock` (the one with the 10-second timeout text)
  - `Release` / `Unlock`
  - any `TryAcquire` variant

- [ ] **Step 3: Find the underlying primitive.** Decompile the constructor: does it call `InitializeCriticalSectionAndSpinCount`, `CreateMutexW`, `SRWLock`, or a custom spinlock (`InterlockedCompareExchange` loops)? Record which — this determines whether the class is process-global or per-object and how it behaves under contention.

- [ ] **Step 4: Find all call sites.** For the Acquire method, list every xref. For each caller, record: caller address, decompiled 2-3 line context, and what data it protects (e.g. "guards the unit object array during sim tick", "guards megafile open table"). Group them into a table.

- [ ] **Step 5: Document.** Add "## ThreadLockMutexClass Map" to `docs/research/01-binary-map.md`:
  - class method addresses (ctor, Acquire, Release, ...)
  - underlying OS primitive used
  - the full call-site table (address + what it guards)

- [ ] **Step 6: Verify.** Pick the call site that guards the most critical data (likely sim tick) and note it — it will be referenced by the threading design (doc 06). Sanity check: the count of call sites should be at least 5 for a game with load threading + megafile + object DB.

- [ ] **Step 7: Commit:**
  ```bash
  git add docs/research/01-binary-map.md
  git commit -F - <<'EOF'
  research: map ThreadLockMutexClass methods and call sites

  Constructor, acquire/release, underlying OS primitive, and every
  call site with the data it guards.

  Co-authored-by: CommandCodeBot <noreply@commandcode.ai>
  EOF
  git push origin main
  ```

### Task 1.6: Map Thread Creation Points (LoadThread and Friends)

**Files:**
- Modify: `docs/research/01-binary-map.md` (add thread creation map)

**Interfaces:**
- Consumes: Ghidra project (Task 1.3), string offsets (Shared Facts)
- Produces: thread creation map in doc 01 — consumed by Task 2.1 (runtime thread census) and Task 4.1

- [ ] **Step 1: Find OS thread APIs.** In Ghidra, open the imports table (`Window → Symbol Table → Imports`) and locate `CreateThread` and `_beginthreadex` (if present) in the KERNEL32 import list. Show xrefs to each.

- [ ] **Step 2: Find `LoadThread`.** Search for the string at file offset `0x00803B54` (`LoadThread`). Its xrefs reveal what "load thread" means in the 64-bit port: a named thread proc, a thread that loads megafiles, or a UI thread for the loading screen. Decompile and record the thread proc address and what it loads.

- [ ] **Step 3: Enumerate every thread creation site.** Combine the xrefs from Step 1 into a table: caller address, thread proc address, passed parameter, stack size (the `dwStackSize` argument to `CreateThread` — 0 means default), and inferred purpose from decompilation.

- [ ] **Step 4: Check TBB linkage in StarWarsG.exe itself.** In the Ghidra program for `corruption/StarWarsG.exe`, search memory for `tbbR` and for TBB mangled names (e.g. `concurrent_queue`). If absent: TBB is only used by `swfoc.exe` (launcher), not the game process — record that conclusion. If present: find the xrefs and record what uses TBB.

- [ ] **Step 5: Document.** Add "## Thread Creation Map" to `docs/research/01-binary-map.md`: the full table from Step 3 plus the `LoadThread` findings plus the TBB conclusion with evidence (found/not-found).

- [ ] **Step 6: Verify.** Count matches: every `CreateThread` xref must appear in the table. Compare your count against the number of `CreateThread` xrefs Ghidra shows — they must be equal.

- [ ] **Step 7: Commit:**
  ```bash
  git add docs/research/01-binary-map.md
  git commit -F - <<'EOF'
  research: map all thread creation sites

  Every CreateThread/_beginthreadex xref, the LoadThread mechanism,
  and whether StarWarsG.exe itself links TBB (with evidence).

  Co-authored-by: CommandCodeBot <noreply@commandcode.ai>
  EOF
  git push origin main
  ```

### Task 1.7: Map the Lua Surface

**Files:**
- Modify: `docs/research/03-lua-surface.md` (replace initial findings with confirmed map)

**Interfaces:**
- Consumes: Ghidra project (Task 1.3), string census (Task 1.1)
- Produces: Lua version + binding surface + state count in doc 03 — consumed by Task 4.1 and Phase 5

- [ ] **Step 1: Identify the Lua version.** In Ghidra, search for the string `Lua` and for version markers. Standard Lua builds embed `Lua 5.x` / `LUA_VERSION` / `$LuaVersion:`. Also check for the exact byte pattern of Lua opcodes if needed (Lua 5.0/5.1/5.4 differ). Record the version found with the evidence (string + address).

- [ ] **Step 2: Find the Lua states.** Look for `luaL_newstate`-equivalent behavior: a function that mallocs a large struct and initializes it with a known Lua signature, or xrefs to `lua_newstate` if symbol names survived. Record how many states are created and where they live (global pointers in `.data`).

- [ ] **Step 3: Map `LuaCreateThread`.** Find xrefs to the strings at file offsets `0x00855858` and `0x008558A3`. Decompile the function. Determine: does it create an OS thread, a Lua coroutine (`lua_newthread`/`lua_resume` pattern), or a script scheduler entry? Record the exact mechanism and the C++→Lua call flow.

- [ ] **Step 4: Enumerate registered bindings.** Find the Lua function registration tables: arrays of `{name_string_ptr, c_func_ptr}` pairs (Ghidra shows them as structured data if you press `P` and create an array). Search for known EAW binding names as strings (e.g. `Create_Thread` — the Lua-side name of `LuaCreateThread`, or any string ending in `_Thread`). Record every binding name + C++ function address in a table. Expect dozens to hundreds — capture all found.

- [ ] **Step 5: Document in `docs/research/03-lua-surface.md`.** Replace the "What We Know" bullets with confirmed findings: Lua version (with evidence), state count + global locations, `LuaCreateThread` mechanism, and the binding table. Keep the "Thread Safety Concerns" section but update it with facts (e.g. "states are guarded by ThreadLockMutexClass at VA 0x..." if found).

- [ ] **Step 6: Verify.** Pick 3 binding names from your table and confirm each string's xref actually points into a registration table (not a log message). If xrefs are ambiguous, mark the binding "uncertain" rather than asserting it.

- [ ] **Step 7: Commit:**
  ```bash
  git add docs/research/03-lua-surface.md
  git commit -F - <<'EOF'
  research: map the Lua surface of StarWarsG.exe

  Lua version with evidence, state locations, LuaCreateThread mechanism,
  and the registered binding table.

  Co-authored-by: CommandCodeBot <noreply@commandcode.ai>
  EOF
  git push origin main
  ```

### Task 1.8: Map the Perception DLL Interop

**Files:**
- Modify: `docs/research/01-binary-map.md` (add interop section)

**Interfaces:**
- Consumes: Ghidra project (Task 1.3), PerceptionFunctionG.dll exports (Task 1.2)
- Produces: perception interop map in doc 01 — consumed by Task 4.1 (parallelism candidate analysis)

- [ ] **Step 1: Find the `Init_Perception_DLL` call.** In the Ghidra program for `corruption/StarWarsG.exe`, find the import of `Init_Perception_DLL` from `PerceptionFunctionG.dll`. Show xrefs to the import thunk.

- [ ] **Step 2: Decode the 11 callback arguments.** Decompile the call site. The exe passes 11 function pointers. For each, label what the engine-side function does (from its decompiled body): the token-matcher, the evaluator, the allocator, the megafile name resolver, etc. Match them against the parameter list documented in `docs/research/01-binary-map.md` "PerceptionFunctionG.dll — Class Map" section.

- [ ] **Step 3: Analyze thread-safety of the callbacks.** For each callback, check: does it write to global state, take a mutex, or only read? Decompile enough to classify each as THREAD_SAFE (pure/read-only), LOCKED (takes a mutex), or UNSAFE (writes shared state without lock). This determines whether perception evaluation can run on worker threads.

- [ ] **Step 4: Document.** Add "## Perception Interop Map" to `docs/research/01-binary-map.md`: the 11 callbacks with address + purpose + safety classification, the init call address, and a conclusion sentence: "Perception evaluation is parallelizable as-is: YES/NO/PARTIALLY (which callbacks block it)".

- [ ] **Step 5: Verify.** Re-read the init call in the decompiler and confirm all 11 register arguments are accounted for in your table (count them).

- [ ] **Step 6: Commit:**
  ```bash
  git add docs/research/01-binary-map.md
  git commit -F - <<'EOF'
  research: map perception DLL interop and callback safety

  All 11 engine callbacks identified and classified thread-safe/locked/unsafe;
  parallelism verdict for perception evaluation.

  Co-authored-by: CommandCodeBot <noreply@commandcode.ai>
  EOF
  git push origin main
  ```

---

## Phase 2 — Dynamic Analysis (Windows)

**Machine:** Windows — the game runs there. `git pull` before starting. All tasks launch the FoCs exe via Steam (select Forces of Corruption → Play) or directly `corruption\StarWarsG.exe` if it runs without Steam.

### Task 2.1: Baseline Boot + Thread Snapshot

**Files:**
- Modify: `docs/research/04-simulation-architecture.md` (add "Runtime Baseline" section)

**Interfaces:**
- Consumes: game installed (Task 0.3), Process Explorer (Task 0.2)
- Produces: baseline runtime facts in doc 04 — consumed by Tasks 2.2–2.4

- [ ] **Step 1: Boot the game.** Launch via Steam to the main menu, then start a skirmish (any map, medium AI) and let it run 30 seconds. Note: the game must reach the battle screen, not stay in menus.

- [ ] **Step 2: Find the process.** Open Process Explorer (as Administrator), locate `StarWarsG.exe` in the process tree. If the game was launched via Steam, it may be a child of `steam.exe`. Record the PID.

- [ ] **Step 3: Open properties → Threads tab.** Record:
  - total thread count
  - per-thread: TID, start address module+offset (e.g. `StarWarsG.exe+0x7a1c40`), CPU time, state
  - which threads are consuming CPU (sort by CPU time)

- [ ] **Step 4: Repeat twice: in main menu, then during a heavy battle.** Spawn a large battle (or just play normally with lots of units on screen) and take a second snapshot. Record both snapshots.

- [ ] **Step 5: Document.** Add "## Runtime Baseline" to `docs/research/04-simulation-architecture.md`: date, build used, both thread tables, and answers to: how many threads total? which are game threads vs system DLL threads? does thread count grow during battle?

- [ ] **Step 6: Verify.** Cross-check the thread start addresses against the thread creation map in `docs/research/01-binary-map.md` — each game thread's start address should match a thread proc from that table. Note any that don't match (unknown threads).

- [ ] **Step 7: Commit:**
  ```powershell
  Set-Content -Path .gitmessage -Value "research: runtime thread baseline in menu and battle`n`nThread counts, start addresses, CPU consumers, matched against static thread map.`n`nCo-authored-by: CommandCodeBot <noreply@commandcode.ai>"
  git add docs/research/04-simulation-architecture.md
  git commit -F .gitmessage
  Remove-Item .gitmessage
  git push origin main
  ```

### Task 2.2: Confirm the Main Loop Address at Runtime

**Files:**
- Modify: `docs/research/04-simulation-architecture.md`

**Interfaces:**
- Consumes: `EAW_GameLoop` address (Task 1.4), x64dbg (Task 0.2)
- Produces: confirmed main loop address in doc 04 — consumed by Task 5.3

- [ ] **Step 1: Launch the game and attach x64dbg** (File → Attach → `StarWarsG.exe`). If ASLR moved the exe, the base differs from 0x140000000 — in x64dbg, open the Memory Map tab and note the module base.

- [ ] **Step 2: Compute the runtime address:** runtime = module base + (documented VA − 0x140000000). Set a breakpoint there (`bp StarWarsG.exe+<offset>`).

- [ ] **Step 3: Resume** (F9). Expected: the breakpoint hits within a frame or two. If it never hits, the address is wrong (or it's a function called only in menus/battle) — re-check Task 1.4 findings in Ghidra.

- [ ] **Step 4: Confirm it's the loop.** Step over (F8) a few frames — it should repeat. Count hits per second by checking breakpoint hit count in the Breakpoints tab: expect tens per second (matches frame rate).

- [ ] **Step 5: Set breakpoints on the phase calls** you labeled in Task 1.4 (sim tick, render). Confirm they all fire every iteration, in order.

- [ ] **Step 6: Document.** Update the "Main Loop (confirmed)" section in `docs/research/04-simulation-architecture.md`: add "Runtime-confirmed on <date> via x64dbg" plus the module base observed and the exact `StarWarsG.exe+offset` expressions.

- [ ] **Step 7: Commit:**
  ```powershell
  Set-Content -Path .gitmessage -Value "research: runtime-confirm main loop and phase addresses`n`nx64dbg breakpoints hit every frame at documented loop address.`n`nCo-authored-by: CommandCodeBot <noreply@commandcode.ai>"
  git add docs/research/04-simulation-architecture.md
  git commit -F .gitmessage
  Remove-Item .gitmessage
  git push origin main
  ```

### Task 2.3: CPU Sampling Profile During Battle

**Files:**
- Modify: `docs/research/04-simulation-architecture.md` (add "CPU Profile (WPR)" section)

**Interfaces:**
- Consumes: WPR (Task 0.2), game running (Task 2.1)
- Produces: hotspot list in doc 04 — consumed by Task 4.1 (which subsystems to parallelize first)

- [ ] **Step 1: Start recording** as Administrator:
  ```powershell
  wpr -start CPU -filemode
  ```
  Expected: prints "Recording..." and returns.

- [ ] **Step 2: Generate load.** Play a heavy battle for 60–120 seconds: large fleet fights, many units, abilities firing. Keep the game in the foreground.

- [ ] **Step 3: Stop and save:**
  ```powershell
  wpr -stop C:\Dev\eaw_battle.etl
  ```
  Expected: trace file created.

- [ ] **Step 4: Analyze.** Open the .etl in Windows Performance Analyzer (`wpa C:\Dev\eaw_battle.etl`). Use the CPU Usage (Sampled) graph, filter to the `StarWarsG.exe` process, expand the call stack. Identify:
  - top 10 hottest functions by inclusive samples (module + offset + your label from Ghidra if available)
  - what fraction of time is sim tick vs render vs Lua vs perception vs audio (mss64)

- [ ] **Step 5: Document.** Add "## CPU Profile (WPR)" to `docs/research/04-simulation-architecture.md`: date, scenario description, top-10 table (rank, `StarWarsG.exe+offset`, label, % of samples), and the subsystem split.

- [ ] **Step 6: Verify.** The top hotspot must be inside a function reachable from `EAW_GameLoop` (Task 1.4). If the top consumer is a system DLL (e.g. D3D driver), note it — it means the game is render-bound, not sim-bound, which changes the threading priority.

- [ ] **Step 7: Commit:**
  ```powershell
  Set-Content -Path .gitmessage -Value "research: CPU sampling profile of heavy battle`n`nWPR trace analysis: top 10 hotspots and sim/render/lua/perception split.`n`nCo-authored-by: CommandCodeBot <noreply@commandcode.ai>"
  git add docs/research/04-simulation-architecture.md
  git commit -F .gitmessage
  Remove-Item .gitmessage
  git push origin main
  ```

### Task 2.4: Sim Tick Rate and Frame Pacing

**Files:**
- Modify: `docs/research/04-simulation-architecture.md`

**Interfaces:**
- Consumes: x64dbg breakpoints (Task 2.2)
- Produces: tick rate + pacing facts in doc 04 — consumed by Task 4.1

- [ ] **Step 1: Measure tick rate.** In x64dbg with a breakpoint on the sim tick function, open the breakpoint hit counter and note hits over exactly 10 seconds of real time (use a stopwatch or the Trace tab timestamps). Compute ticks/sec.

- [ ] **Step 2: Measure frame pacing.** Same for the render call: hits over 10 seconds. Compute FPS. Compare: is the sim tick fixed-rate (e.g. 30 Hz) while render is variable? Does render run multiple times per tick or vice versa?

- [ ] **Step 3: Check sleep/wait usage.** In the gap between iterations, is the game calling `Sleep`, waiting on vsync (`Present` with `D3DPRESENT_INTERVAL_ONE`), or spinning? Check by pausing the debugger randomly during idle and looking at the call stack (Ctrl+F9 or break manually).

- [ ] **Step 4: Document.** Add "## Tick Rate and Pacing" to `docs/research/04-simulation-architecture.md`: measured sim Hz, measured FPS, sync mechanism, and what this implies for thread scheduling (e.g. "sim budget = 33 ms per tick at 30 Hz, currently consuming X ms → Y ms free for parallel work").

- [ ] **Step 5: Verify.** Numbers must be consistent: if sim is 30 Hz and FPS is 60, render fires ~2x per tick. If measurements contradict, redo the timing.

- [ ] **Step 6: Commit:**
  ```powershell
  Set-Content -Path .gitmessage -Value "research: measure sim tick rate and frame pacing`n`nFixed/variable rate confirmed, sync mechanism identified.`n`nCo-authored-by: CommandCodeBot <noreply@commandcode.ai>"
  git add docs/research/04-simulation-architecture.md
  git commit -F .gitmessage
  Remove-Item .gitmessage
  git push origin main
  ```

---

## Phase 3 — .meg Format

**Machine:** Linux (game files live there). `git pull` before starting.

### Task 3.1: Decode the .meg Header and Entry Table

**Files:**
- Modify: `docs/research/02-meg-format.md`

**Interfaces:**
- Consumes: game install (Task 0.3), header facts in Shared Facts
- Produces: byte-level header spec in doc 02 — consumed by Tasks 3.2, 3.3

- [ ] **Step 1: Hex-dump the first 512 bytes of three megas:**
  ```bash
  cd "/home/bob2142/.local/share/Steam/steamapps/common/Star Wars Empire at War"
  xxd -l 512 "corruption/Data/config.meg"  > /tmp/config.hdr
  xxd -l 512 "corruption/Data/64Patch.meg" > /tmp/64patch.hdr
  xxd -l 512 "corruption/Data/shaders.meg" > /tmp/shaders.hdr
  ```

- [ ] **Step 2: Decode the structure.** Known so far: bytes 0–3 u32 count (1046 for config.meg), bytes 4–7 same count again, byte 8 = 0x42 ('B'), then UTF-16LE strings separated by 0x3D ('='). Answer these questions from the hex dumps:
  - Are bytes 4–7 always equal to bytes 0–3? (check all three files)
  - Is there a UTF-16LE string terminator (0x0000) at the end of each name, or does 0x3D ('=') separate key from value?
  - After the name table, where does file data start? Find the pattern: does an entry reference offset/size (look for u32 values that look like offsets into the file right after each name)?
  - Are names stored as `internal_name=DATA\path` pairs (i.e. hash-less format)? Compare against the loose-file names in `GameData/Data/*.txt` — do the meg names match those files?

- [ ] **Step 3: Verify with the count fields.** Write a tiny check:
  ```python
  import struct
  for path in ["corruption/Data/config.meg", "corruption/Data/64Patch.meg", "corruption/Data/shaders.meg"]:
      data = open(path, "rb").read(16)
      c1, c2 = struct.unpack_from("<II", data, 0)
      print(path, c1, c2, "match" if c1 == c2 else "MISMATCH")
  ```
  Expected: all three print `match`.

- [ ] **Step 4: Document the header spec.** Rewrite the "What We Know" section of `docs/research/02-meg-format.md` into a byte-level spec: field offsets, types, name encoding, separator, and the offset/size storage scheme you found. Include hex-dump excerpts as evidence. Write exactly what the bytes show, and explicitly list what is still unknown as "Open Questions" at the bottom.

- [ ] **Step 5: Verify your spec by re-deriving it.** From your spec, predict the byte offset of the 3rd entry name in config.meg, then check with `xxd` that the name is exactly there.

- [ ] **Step 6: Commit:**
  ```bash
  git add docs/research/02-meg-format.md
  git commit -F - <<'EOF'
  research: byte-level meg header spec

  Count fields, UTF-16LE name table, separator, and offset/size storage
  verified across config, 64Patch, and shaders megas.

  Co-authored-by: CommandCodeBot <noreply@commandcode.ai>
  EOF
  git push origin main
  ```

### Task 3.2: Python .meg Reader

**Files:**
- Create: `scripts/meg_reader.py`

**Interfaces:**
- Consumes: header spec (Task 3.1)
- Produces: `meg_reader.py` with `MegaFile` class — consumed by Tasks 3.3, 4.2

- [ ] **Step 1: Write the reader.** `scripts/meg_reader.py`:

```python
"""Read Petroglyph .meg archives. Usage:
  from meg_reader import MegaFile
  mf = MegaFile("path/to/config.meg")
  print(mf.names())          # all entry names
  data = mf.read("DATA\\SCRIPTS\\AI\\AI_PLAN_EXPANSIONGENERIC.LUA")
"""
import struct

class MegaFile:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self._data = f.read()
        self._parse_header()

    def _parse_header(self):
        # Layout per the Task 3.1 spec (verify the offset/size scheme against
        # your hex dumps and update these lines to the real layout):
        self.count = struct.unpack_from("<I", self._data, 0)[0]
        self._names = []      # list of (internal_name, stored_path)
        self._entries = []    # list of (offset, size)
        pos = 8               # after two u32 count fields
        for _ in range(self.count):
            end = self._data.index(b"\x00\x00", pos)
            raw = self._data[pos:end].decode("utf-16le")
            internal, _, stored = raw.partition("=")
            pos = end + 2
            self._names.append((internal, stored))
            off, size = struct.unpack_from("<II", self._data, pos)
            pos += 8
            self._entries.append((off, size))

    def names(self):
        return [stored for _, stored in self._names]

    def read(self, name):
        for (internal, stored), (off, size) in zip(self._names, self._entries):
            if stored == name or internal == name:
                return self._data[off:off + size]
        raise KeyError(name)
```

- [ ] **Step 2: Replace the guess with the real layout.** The `pos`/offset-size lines above are a starting hypothesis. After your Task 3.1 hex analysis, rewrite `_parse_header` to match the real byte layout (the committed code must reflect the verified spec — remove the "verify/update" comments).

- [ ] **Step 3: Smoke test.** Run:
  ```bash
  cd /home/bob2142/Dev/Le\ Passion/Empire\ At\ War\ MultiTHreadEngine
  python3 -c "
  import sys; sys.path.insert(0, 'scripts')
  from meg_reader import MegaFile
  mf = MegaFile('/home/bob2142/.local/share/Steam/steamapps/common/Star Wars Empire at War/corruption/Data/shaders.meg')
  print('entries:', len(mf.names()))
  print('first 3:', mf.names()[:3])
  "
  ```
  Expected: `entries: 82` and three `DATA\ART\SHADERS\...` names.

- [ ] **Step 4: Verify against the header spec.** The entry count must equal the u32 at offset 0. Reading every entry must not raise and must return non-empty bytes for at least 90% of entries.

- [ ] **Step 5: Commit:**
  ```bash
  git add scripts/meg_reader.py
  git commit -F - <<'EOF'
  feat: python meg archive reader

  MegaFile class: parse header, list names, read entries by name.
  Verified against shaders.meg (82 entries).

  Co-authored-by: CommandCodeBot <noreply@commandcode.ai>
  EOF
  git push origin main
  ```

### Task 3.3: .meg Extractor CLI + Loose-File Verification

**Files:**
- Create: `scripts/meg_extract.py`
- Modify: `docs/research/02-meg-format.md`

**Interfaces:**
- Consumes: `meg_reader.MegaFile` (Task 3.2)
- Produces: extractor CLI; format spec doc updated with verification results

- [ ] **Step 1: Write `scripts/meg_extract.py`:**

```python
"""Extract files from .meg archives. Usage:
  python scripts/meg_extract.py <file.meg> <output_dir> [name_filter]
If name_filter is given, only entries whose stored name contains it are extracted.
"""
import sys, os
from meg_reader import MegaFile

def main():
    meg_path, out_dir = sys.argv[1], sys.argv[2]
    filt = sys.argv[3] if len(sys.argv) > 3 else None
    mf = MegaFile(meg_path)
    os.makedirs(out_dir, exist_ok=True)
    n = 0
    for name in mf.names():
        if filt and filt not in name:
            continue
        data = mf.read(name)
        safe = name.replace("\\", "/")
        out_path = os.path.join(out_dir, safe)
        os.makedirs(os.path.dirname(out_path), exist_ok=True)
        with open(out_path, "wb") as f:
            f.write(data)
        n += 1
    print(f"extracted {n} files from {meg_path} to {out_dir}")

if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Extract entries containing a known name.** The loose files `GameData/Data/*.txt` (e.g. `acclamator_assault_ship.txt`) should also exist inside `config.meg`:
  ```bash
  cd /home/bob2142/Dev/Le\ Passion/Empire\ At\ War\ MultiTHreadEngine/scripts
  python3 meg_extract.py "/home/bob2142/.local/share/Steam/steamapps/common/Star Wars Empire at War/corruption/Data/config.meg" /tmp/meg_out acclamator
  ```
  Expected: extracts at least one file containing `acclamator` in the name.

- [ ] **Step 3: Compare extracted vs loose.** Pick one extracted file that has a loose counterpart in `GameData/Data/` and diff them:
  ```bash
  diff /tmp/meg_out/<extracted_path> "/home/bob2142/.local/share/Steam/steamapps/common/Star Wars Empire at War/GameData/Data/<loose_name>.txt"
  ```
  If identical: the extraction is byte-perfect. If different but similar: the meg version is newer/older — note the difference. If completely different: the name mapping is wrong; fix the reader.

- [ ] **Step 4: Document verification results.** Add a "## Verification" section to `docs/research/02-meg-format.md`: which file was extracted, which loose file compared, diff result, and conclusion ("format spec confirmed byte-perfect" or what differed).

- [ ] **Step 5: Verify round-trip.** Extract a file, re-parse the extracted bytes, confirm content sanity (e.g. an XML file starts with `<`, a Lua file contains `function`).

- [ ] **Step 6: Commit:**
  ```bash
  git add scripts/meg_extract.py docs/research/02-meg-format.md
  git commit -F - <<'EOF'
  feat: meg extractor CLI with loose-file verification

  Extracts entries by name; verified byte-perfect against loose GameData
  files. Format spec confirmed.

  Co-authored-by: CommandCodeBot <noreply@commandcode.ai>
  EOF
  git push origin main
  ```

---

## Phase 4 — Design Synthesis

**Machine:** Windows (doc edits). `git pull` before starting.

### Task 4.1: Consolidate Findings into the Threading Design

**Files:**
- Modify: `docs/research/06-threading-design.md` (major rewrite)

**Interfaces:**
- Consumes: docs 01, 03, 04 findings from Phases 1–3
- Produces: threading design v2 — consumed by Tasks 5.4 and Phase 6

- [ ] **Step 1: Re-read the findings.** Open `docs/research/01-binary-map.md`, `03-lua-surface.md`, `04-simulation-architecture.md` and list every fact that constrains threading: mutex call sites, thread creation points, Lua state layout, callback safety classifications, CPU hotspots, tick rate, pacing.

- [ ] **Step 2: Rewrite doc 06 sections** "Current State Assessment" and "Candidate Job Graph" using real facts. For each subsystem (perception, pathfinding, AI, particles, render, loading), write:
  - evidence: what the analysis found (addresses, hotspots, mutexes)
  - parallelism verdict: NOW (safe today), AFTER_REFACTOR (needs a guard/lock change), or NOT_NOW (serial dependency too deep)
  - the exact data it touches and who writes it

- [ ] **Step 3: Write the job graph.** Draw the per-tick parallelization graph as a mermaid diagram in the doc, using the phase addresses from Task 1.4/2.2 as the serial anchors.

- [ ] **Step 4: Pick the first parallel slice** for the Phase 5 spike. Requirements: highest CPU hotspot that is NOW-parallelizable, minimal shared state, deterministic merge. Write the choice + justification in a new section "First Parallel Slice (Phase 5 target)".

- [ ] **Step 5: Verify coherence.** Every claim in doc 06 must cite a doc 01/03/04 fact. No uncited claims. Read through once and add citations.

- [ ] **Step 6: Commit:**
  ```powershell
  Set-Content -Path .gitmessage -Value "research: threading design v2 grounded in findings`n`nPer-subsystem parallelism verdicts with evidence, mermaid job graph,`n`nand the chosen first parallel slice for the prototype spike.`n`nCo-authored-by: CommandCodeBot <noreply@commandcode.ai>"
  git add docs/research/06-threading-design.md
  git commit -F .gitmessage
  Remove-Item .gitmessage
  git push origin main
  ```

### Task 4.2: Mod Compatibility Field Check

**Files:**
- Modify: `docs/research/05-mod-compatibility.md`

**Interfaces:**
- Consumes: meg extractor (Task 3.3), Lua findings (Task 1.7)
- Produces: verified mod contract facts in doc 05 — consumed by Phase 5 design choices

- [ ] **Step 1: Install a real mod.** In Steam, subscribe to Thrawn's Revenge (Steam Workshop). Find where the 64-bit port places workshop mods: check `steamapps/workshop/content/32470/` (EAW app id) for the mod folder. Record the exact path.

- [ ] **Step 2: Inspect the mod layout.** List its files: does it ship loose XML/Lua in a `Data\` folder? Its own megas? A `megafiles.xml`? Record the structure.

- [ ] **Step 3: Verify the game loads it.** Launch the game, activate the mod (Mods menu), start a battle, confirm mod content appears (units/factions different from vanilla). Record which launch path works.

- [ ] **Step 4: Extract and catalog the mod's Lua.** Use `scripts/meg_extract.py` on the mod's megas (or read loose files) and grep for binding calls:
  ```powershell
  findstr /s /i "Create_Thread" <mod_dir>\Data\Scripts
  ```
  Compare the bindings the mod uses against the binding table in `docs/research/03-lua-surface.md` — every mod binding must exist in the table. List any that don't.

- [ ] **Step 5: Document.** Update `docs/research/05-mod-compatibility.md`: workshop install path, mod file structure, load verification result, and the mod-binding-vs-engine-binding comparison table. Answer the open question "Do current mods work on the 64-bit port?" with evidence.

- [ ] **Step 6: Verify.** If the mod fails to load or uses bindings not in the engine table, STOP and record the gap — it changes Phase 5/6 priorities. If it works, proceed.

- [ ] **Step 7: Commit:**
  ```powershell
  Set-Content -Path .gitmessage -Value "research: mod compatibility field check with Thrawn's Revenge`n`nWorkshop install path, mod layout, load test, and binding surface comparison.`n`nCo-authored-by: CommandCodeBot <noreply@commandcode.ai>"
  git add docs/research/05-mod-compatibility.md
  git commit -F .gitmessage
  Remove-Item .gitmessage
  git push origin main
  ```

---

## Phase 5 — Prototype Spike

**Machine:** Windows (the game runs there). `git pull` before starting.

### Task 5.1: Build a Hello-World DLL

**Files:**
- Create: `src/CMakeLists.txt`
- Create: `src/hello/main.cpp`

**Interfaces:**
- Consumes: CMake + MinGW-w64 + Ninja (Task 0.2)
- Produces: buildable `hello.dll` — proves the toolchain; consumed by Task 5.2 as the build skeleton

- [ ] **Step 1: Write `src/CMakeLists.txt`:**

```cmake
cmake_minimum_required(VERSION 3.20)
project(eaw_engine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MINGW)
    add_compile_options(-ffunction-sections -fdata-sections)
    add_link_options(-Wl,--gc-sections)
endif()

add_library(hello SHARED hello/main.cpp)
set_target_properties(hello PROPERTIES PREFIX "")
```

- [ ] **Step 2: Write `src/hello/main.cpp`:**

```cpp
#include <windows.h>
#include <cstdio>

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved) {
    (void)hinst; (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        // Proof-of-life: write a marker next to the exe.
        FILE* f = fopen("eaw_hello_loaded.txt", "w");
        if (f) { fprintf(f, "EAW hello DLL loaded\n"); fclose(f); }
    }
    return TRUE;
}
```

- [ ] **Step 3: Build** (PowerShell with MinGW bin on PATH):
  ```powershell
  cd C:\Dev\Empire-At-War-MultiTHreadEngine\src
  cmake -B build -G "Ninja" -DCMAKE_CXX_COMPILER=g++
  cmake --build build
  ```
  Expected: `hello.dll` produced in `src\build\`.

- [ ] **Step 4: Verify it loads.** Force-load it into Python (which links the same CRT world):
  ```powershell
  python -c "import ctypes; ctypes.WinDLL(r'C:\Dev\Empire-At-War-MultiTHreadEngine\src\build\hello.dll'); print('loaded')"
  ```
  Expected: prints `loaded` and `eaw_hello_loaded.txt` appears in the current directory.

- [ ] **Step 5: Commit:**
  ```powershell
  Set-Content -Path .gitmessage -Value "feat: hello-world DLL build skeleton`n`nCMake project that builds a Windows DLL with proof-of-life marker.`n`nCo-authored-by: CommandCodeBot <noreply@commandcode.ai>"
  git add src/CMakeLists.txt src/hello/main.cpp
  git commit -F .gitmessage
  Remove-Item .gitmessage
  git push origin main
  ```

### Task 5.2: d3d9 Proxy DLL Skeleton

**Files:**
- Create: `src/proxy/d3d9.cpp`
- Create: `src/proxy/d3d9.def`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Consumes: hello build skeleton (Task 5.1), `d3d9.dll` import name from Task 1.2
- Produces: `d3d9.dll` proxy that forwards to the real system d3d9 and logs attach — consumed by Task 5.3

- [ ] **Step 1: Dump the real d3d9 export list** (names needed for forwarding):
  ```powershell
  python -c "
  import pefile
  pe = pefile.PE(r'C:\Windows\System32\d3d9.dll', fast_load=True)
  pe.parse_data_directories()
  for exp in pe.DIRECTORY_ENTRY_EXPORT.symbols:
      if exp.name: print(exp.name.decode())
  "
  ```
  Expected: roughly 30–50 names starting with `Direct3DCreate9`, `Direct3DCreate9Ex`, `D3DPERF_...`. Save the list.

- [ ] **Step 2: Write `src/proxy/d3d9.cpp`** — runtime forwarding via naked stubs (MinGW-w64 supports `__attribute__((naked))` on x86-64; MSVC does not — this is why we build with MinGW):

```cpp
// d3d9.dll proxy: forwards every export to the real system d3d9.
// Drop into the game folder; StarWarsG.exe loads us instead.
// Each stub is a naked jump through a global slot filled at attach time.
#include <windows.h>
#include <cstdio>

static HMODULE real_d3d9 = nullptr;

extern "C" void InstallLoopHook();   // from hook.cpp (Task 5.3)

// One global slot per forwarded export, filled in DllMain.
extern "C" FARPROC real_Direct3DCreate9 = nullptr;
extern "C" FARPROC real_Direct3DCreate9Ex = nullptr;
// (add one line per export from the Step 1 list)

#define PROXY(name)                                          \
    extern "C" FARPROC real_##name;                          \
    extern "C" __attribute__((naked)) void name(void) {      \
        __asm__ volatile("jmp *real_" #name "(%rip)");       \
    }

PROXY(Direct3DCreate9)
PROXY(Direct3DCreate9Ex)
// (add one PROXY line per export from the Step 1 list)

static void LoadRealD3D9() {
    if (real_d3d9) return;
    char sys[MAX_PATH];
    GetSystemDirectoryA(sys, MAX_PATH);
    strcat_s(sys, "\\d3d9.dll");
    real_d3d9 = LoadLibraryA(sys);
    if (!real_d3d9) return;
    FILE* f = fopen("eaw_proxy_loaded.txt", "w");
    if (f) { fprintf(f, "EAW d3d9 proxy attached\n"); fclose(f); }
    // Fill every slot:
    real_Direct3DCreate9 = GetProcAddress(real_d3d9, "Direct3DCreate9");
    real_Direct3DCreate9Ex = GetProcAddress(real_d3d9, "Direct3DCreate9Ex");
    // (one line per export)
}

// Entry point for the game's one d3d9 import. Install the hook here,
// NOT in DllMain — DllMain runs under the loader lock.
extern "C" __declspec(dllexport) void* Direct3DCreate9Hook(UINT sdk_ver) {
    LoadRealD3D9();
    InstallLoopHook();   // safe: called after loader lock released
    using Fn = void* (*)(UINT);
    return reinterpret_cast<Fn>(real_Direct3DCreate9)(sdk_ver);
}
```

- [ ] **Step 3: Resolve the conflict: the stub named `Direct3DCreate9` and `Direct3DCreate9Hook` must be one function.** The game imports `Direct3DCreate9`; the hook must run before the real one. Merge them: the PROXY stub for `Direct3DCreate9` becomes the hook body (load real dll, install hook, tail-jump to the real proc). Remove `Direct3DCreate9Hook`. The committed code must export exactly one `Direct3DCreate9`.

- [ ] **Step 4: Write `src/proxy/d3d9.def`** listing every export name from Step 1 (one name per line under `EXPORTS`), so the linker emits exactly the surface the game expects.

- [ ] **Step 5: Add to CMake:**
  ```cmake
  add_library(proxy SHARED proxy/d3d9.cpp proxy/d3d9.def)
  set_target_properties(proxy PROPERTIES PREFIX "" OUTPUT_NAME "d3d9")
  target_compile_options(proxy PRIVATE -mno-red-zone)
  ```

- [ ] **Step 6: Build and verify forwarding.**
  ```powershell
  cd C:\Dev\Empire-At-War-MultiTHreadEngine\src
  cmake --build build
  python -c "
  import ctypes
  dll = ctypes.WinDLL(r'C:\Dev\Empire-At-War-MultiTHreadEngine\src\build\d3d9.dll')
  print('proxy loaded')
  print('Direct3DCreate9 ->', dll.Direct3DCreate9)
  "
  ```
  Expected: prints `proxy loaded` and a non-null function address.

- [ ] **Step 7: Commit:**
  ```powershell
  Set-Content -Path .gitmessage -Value "feat: d3d9 proxy DLL skeleton`n`nForwards all d3d9 exports to the real system DLL via naked stubs;`n`nmarker file on attach; hook install point after loader lock.`n`nCo-authored-by: CommandCodeBot <noreply@commandcode.ai>"
  git add src/proxy/d3d9.cpp src/proxy/d3d9.def src/CMakeLists.txt
  git commit -F .gitmessage
  Remove-Item .gitmessage
  git push origin main
  ```

### Task 5.3: Hook the Game Loop

**Files:**
- Create: `src/proxy/hook.cpp`, `src/proxy/hook.h`
- Modify: `src/proxy/d3d9.cpp` (call InstallLoopHook from the Direct3DCreate9 export)
- Modify: `docs/research/04-simulation-architecture.md`

**Interfaces:**
- Consumes: runtime-confirmed loop/tick address (Task 2.2), proxy skeleton (Task 5.2)
- Produces: proxy DLL that logs a line every sim tick — proves injection + hooking end-to-end

- [ ] **Step 1: Vendor MinHook.** Detours are easy to get wrong by hand; MinHook does them correctly. Clone it into the repo:
  ```powershell
  cd C:\Dev\Empire-At-War-MultiTHreadEngine
  git clone https://github.com/TsudaKageyu/minhook third_party/minhook
  ```
  (Committing MinHook's source into the repo is acceptable — it is BSD-licensed.)

- [ ] **Step 2: Write `src/proxy/hook.h`:**

```cpp
#pragma once
void InstallLoopHook();   // called once from the Direct3DCreate9 export
```

- [ ] **Step 3: Write `src/proxy/hook.cpp`** — hook the per-frame sim tick:

```cpp
#include "hook.h"
#include "MinHook.h"
#include <windows.h>
#include <cstdio>

// Offset of the per-iteration function from Task 2.2 findings:
// <confirmed tick VA> - 0x140000000. ASLR-safe: computed at runtime from module base.
static constexpr DWORD64 kTickOffset = 0x0;   // set from Task 2.2 (Step 5)

static DWORD64 g_hits = 0;
static void (*g_originalTick)() = nullptr;

static void HookedTick() {
    g_hits++;
    if ((g_hits % 1000) == 0) {
        FILE* f = fopen("eaw_tick_hits.txt", "a");
        if (f) { fprintf(f, "tick %llu\n", (unsigned long long)g_hits); fclose(f); }
    }
    g_originalTick();   // run the real sim tick
}

void InstallLoopHook() {
    HMODULE base = GetModuleHandleW(L"StarWarsG.exe");
    if (!base) return;
    void* target = reinterpret_cast<void*>(
        reinterpret_cast<DWORD64>(base) + kTickOffset);
    MH_Initialize();
    if (MH_CreateHook(target, reinterpret_cast<void*>(&HookedTick),
                      reinterpret_cast<void**>(&g_originalTick)) != MH_OK)
        return;
    MH_EnableHook(target);
}
```

- [ ] **Step 4: Choose the right hook target.** `kTickOffset` must be the offset of a function that is *called once per iteration and returns* (the sim tick phase from Task 1.4/2.2), NOT the top of a loop that never returns. If the sim tick is inlined into the loop head, pick the next per-iteration call (e.g. the render call). Set `kTickOffset` to the real value from Task 2.2 Step 5.

- [ ] **Step 5: Call InstallLoopHook from the proxy.** In `src/proxy/d3d9.cpp`, inside the exported `Direct3DCreate9` (before tail-jumping to the real one), call `InstallLoopHook()`. Add `hook.cpp` and `third_party/minhook/src/*.c` to the proxy target in CMake:
  ```cmake
  target_sources(proxy PRIVATE proxy/hook.cpp
      third_party/minhook/src/buffer.c
      third_party/minhook/src/hook.c
      third_party/minhook/src/trampoline.c
      third_party/minhook/src/hde/hde32.c
      third_party/minhook/src/hde/hde64.c)
  target_include_directories(proxy PRIVATE third_party/minhook/include)
  ```

- [ ] **Step 6: Build and drop into the game folder.**
  ```powershell
  cd C:\Dev\Empire-At-War-MultiTHreadEngine\src
  cmake --build build
  copy build\d3d9.dll "C:\Program Files (x86)\Steam\steamapps\common\Star Wars Empire at War\corruption\d3d9.dll"
  ```

- [ ] **Step 7: Launch the game and verify.** Start a battle, run 60 seconds, quit. Check `corruption\eaw_proxy_loaded.txt` and `corruption\eaw_tick_hits.txt` both exist. Tick hits should be thousands (tick rate x seconds). If the game crashes at launch: remove our d3d9.dll, restart, and debug (likely wrong offset or a bad tail-jump).

- [ ] **Step 8: Document.** Add "## Injection Spike" to `docs/research/04-simulation-architecture.md`: date, proxy filename, hook offset used, hit count observed, and confirmation that the game ran normally with the hook active.

- [ ] **Step 9: Commit:**
  ```powershell
  Set-Content -Path .gitmessage -Value "feat: hook the sim tick via d3d9 proxy + MinHook`n`nDetour at runtime-confirmed tick address; logs iteration count. Game runs normally.`n`nCo-authored-by: CommandCodeBot <noreply@commandcode.ai>"
  git add src/proxy/hook.cpp src/proxy/hook.h src/proxy/d3d9.cpp src/CMakeLists.txt third_party/minhook docs/research/04-simulation-architecture.md
  git commit -F .gitmessage
  Remove-Item .gitmessage
  git push origin main
  ```

### Task 5.4: First Parallel Slice

**Files:**
- Create: `src/proxy/parallel.cpp`, `src/proxy/parallel.h`
- Modify: `src/proxy/hook.cpp` (dispatch the slice from the hook)
- Modify: `docs/research/06-threading-design.md`

**Interfaces:**
- Consumes: first-slice choice (Task 4.1), hook (Task 5.3)
- Produces: a worker-thread-parallelized slice proven at runtime with determinism check

- [ ] **Step 1: Read the chosen slice spec.** Open `docs/research/06-threading-design.md` → "First Parallel Slice (Phase 5 target)". This task implements exactly that slice. If the chosen slice is the perception evaluation: the plan is to intercept the perception batch call and split unit ranges across N worker threads.

- [ ] **Step 2: Write `src/proxy/parallel.h`:**

```cpp
#pragma once
// Minimal job runner: fixed worker pool, contiguous range split.
// Worker threads are created lazily on first use (NEVER in DllMain —
// that runs under the loader lock and can deadlock).
void RunParallel(int count, void (*fn)(int start, int end));
int  WorkerCount();
```

- [ ] **Step 3: Write `src/proxy/parallel.cpp`.** Implement:
  - worker threads created on first `RunParallel` call, `std::thread::hardware_concurrency() - 1` workers (min 1), main thread participates as the last worker
  - a `std::atomic<int>` job index counter; each worker atomically grabs the next range `[i*chunk, (i+1)*chunk)` where `chunk = max(1, count / workers)`
  - completion: an atomic done-counter + condition variable so the caller blocks until all ranges finish
  - no allocation in the hot path (preallocate per-worker scratch arrays once)
  - on `DLL_PROCESS_DETACH`: set a shutdown flag, notify, join workers

- [ ] **Step 4: Wire the slice into the hook.** In `hook.cpp`'s `HookedTick`, instead of only counting: invoke the intercepted subsystem for range `[0, N)` through `RunParallel`, then merge per-range results exactly as the original code merged the single-threaded results. This requires the hook to replicate the original call — reference the Ghidra decompilation of the subsystem from doc 04/06. Comment every line that mirrors original behavior.

- [ ] **Step 5: Build, drop in, and run the game.** Same procedure as Task 5.3 steps 6–7. Success criteria:
  - game runs, battle completes, no crash in 10 minutes
  - `eaw_tick_hits.txt` shows normal iteration count
  - visual result identical to unpatched (no unit jitter, no logic errors)

- [ ] **Step 6: Measure.** Compare frametime with proxy + parallel slice vs. without proxy (record both): use the game's FPS (Steam overlay or x64dbg timing from Task 2.4 method). Record numbers.

- [ ] **Step 7: Determinism check.** Run the same scripted battle twice with the patch; verify outcomes match (win/loss, unit counts at end). If outcomes differ, the merge is nondeterministic — fix before committing.

- [ ] **Step 8: Document.** Update the "First Parallel Slice" section of doc 06 with: what was implemented, worker count, timing before/after, determinism result, and lessons learned.

- [ ] **Step 9: Commit:**
  ```powershell
  Set-Content -Path .gitmessage -Value "feat: first parallel slice via worker pool`n`nParallelized <subsystem> across N workers with deterministic merge; measured before/after.`n`nCo-authored-by: CommandCodeBot <noreply@commandcode.ai>"
  git add src/proxy/parallel.cpp src/proxy/parallel.h src/proxy/hook.cpp docs/research/06-threading-design.md
  git commit -F .gitmessage
  Remove-Item .gitmessage
  git push origin main
  ```

---

## Phase 6 — Wrap-up

**Machine:** Windows. `git pull` before starting.

### Task 6.1: Final Research Report

**Files:**
- Create: `docs/research/00-research-report.md`
- Modify: `README.md`

**Interfaces:**
- Consumes: all research docs
- Produces: the summary report — the artifact that closes the research phase

- [ ] **Step 1: Write `docs/research/00-research-report.md`.** Sections:
  - Summary (10 sentences max): what the 64-bit remaster is, what threading exists, what we added
  - Key findings: the 10 most important facts with doc references
  - Threading verdict: what's parallelizable now, what needs refactoring, what stays serial
  - Prototype results: numbers from Task 5.4
  - What's next: recommended next-phase tasks for the full engine work

- [ ] **Step 2: Update `README.md`.** Add a "Research Phase Complete" status line, link to `docs/research/00-research-report.md`, and update the docs index with all new files.

- [ ] **Step 3: Verify all doc links resolve.** Check every relative link in README and the report points to a file that exists in the repo (`git ls-files`).

- [ ] **Step 4: Commit and push:**
  ```powershell
  Set-Content -Path .gitmessage -Value "docs: final research report and README update`n`nCloses the research phase; prototype numbers and next-phase recommendations.`n`nCo-authored-by: CommandCodeBot <noreply@commandcode.ai>"
  git add docs/research/00-research-report.md README.md
  git commit -F .gitmessage
  Remove-Item .gitmessage
  git push origin main
  ```

### Task 6.2: Tag the Research Release

**Files:**
- None (git tag only)

**Interfaces:**
- Consumes: Task 6.1 committed and pushed
- Produces: tag `v0.1.0-research` on GitHub

- [ ] **Step 1: Tag:**
  ```powershell
  git tag -a v0.1.0-research -m "Research phase complete: binary map, meg format, threading design, injection spike, first parallel slice"
  git push origin v0.1.0-research
  ```

- [ ] **Step 2: Verify on GitHub.** Open `https://github.com/SamiulH25/Empire-At-War-MultiThreadEngine/releases` — the tag must appear. If it doesn't, re-run the push.

- [ ] **Step 3: Done.** The research phase is complete. The design doc's success criteria (section 6 of the spec) can now be checked off: every checkbox corresponds to a completed task above.
