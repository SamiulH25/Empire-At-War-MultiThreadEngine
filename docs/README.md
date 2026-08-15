# EAW MultiTHreadEngine — Research artifacts

## File layout

- `*.md` — research documents (see README for index)
- `scripts/` — research tooling (Python PE analysis, .meg extraction)
- Future: `src/` for the C++ patch DLL (phase 5)

## Game install location (Linux host)

```
/home/bob2142/.local/share/Steam/steamapps/common/Star Wars Empire at War
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

## Useful commands

```bash
# PE analysis (Python)
python3 -c "import pefile; pe = pefile.PE('path/to/exe'); pe.print_info()"

# Hex-dump a meg header
xxd -l 256 path/to/config.meg
```
