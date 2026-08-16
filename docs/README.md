# EAW MultiTHreadEngine — Research artifacts

## File layout

- `*.md` — research documents (see README for index)
- `scripts/` — research tooling (Python PE analysis, .meg extraction)
- Future: `src/` for the C++ patch DLL (phase 5)

## Game install location (Windows host)

```
C:\Program Files (x86)\Steam\steamapps\common\Star Wars Empire at War
├── corruption/          # Forces of Corruption (64-bit exe here)
│   ├── StarWarsG.exe
│   ├── swfoc.exe
│   ├── PerceptionFunctionG.dll
│   ├── tbbR.dll
│   └── Data/            # megas: config, maps, models, textures, 64Patch...
├── GameData/            # Base game (64-bit exe here too)
│   ├── StarWarsG.exe
│   ├── PerceptionFunctionG.dll
│   └── Data/
├── runme.exe            # 32-bit Steam launcher stub
└── runme2.exe
```

Development is native on Windows (the game runs here). The portable core (job system,
sim logic, .meg tooling) is also kept buildable as a native Linux executable — no
cross-compiling.

## Useful commands

```powershell
# PE analysis (Python)
python -c "import pefile; pe = pefile.PE('path/to/exe'); pe.print_info()"

# Hex-dump a meg header
Format-Hex -Path path\to\config.meg -Count 256
```
