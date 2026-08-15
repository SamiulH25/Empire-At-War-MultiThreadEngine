# 02 — MegaFile (.meg) Archive Format

**Status:** Initial observations — full spec pending byte-level analysis
**Last updated:** 2026-08-15

## What We Know

### Files Present (FoCs / corruption/Data)

| File | Size | Purpose (inferred) |
|---|---|---|
| `64Patch.meg` | 2.7 MB | 64-bit remaster data changes |
| `config.meg` | 18.4 MB | Gameplay XML configs (units, factions, planets) |
| `maps.meg` | 327 MB | Map data (GC maps + skirmish) |
| `models.meg` | 389 MB | 3D models (.alo format) |
| `textures.meg` | 226 MB | DDS textures |
| `shaders.meg` | 860 KB | Shader files (.fx) |
| `Patch.meg` / `Patch2.meg` | 32.7 / 76 MB | Official patch data |
| `englishspeech.meg` | 102 MB | Voice audio |
| `music.meg` | 37.6 MB | Music |
| `movies.meg` | 54.4 MB | Bink videos |

Base game (`GameData/Data`) has similar set but bigger models/textures/music megas (includes FoCs assets in a different split).

### Load Order

`megafiles.xml` lists megas in order. Both `corruption/Data` and `GameData/Data` have one.
Later files override earlier ones; loose files in `Data/` override everything (mod mechanism).

## Community Knowledge

The .meg format is known from modding tools (e.g. the community `MegExtractor`):

- Header contains a magic number and file count
- File entries have name hash, offset, size
- Compression is used on some entries (zlib)
- Names are stored hashed — extracting without the name table requires brute-forcing known names

**This needs byte-level verification** against the actual files in this repo's game install.

## Research Tasks

1. Hex-dump headers of `config.meg`, `64Patch.meg`, `shaders.meg`
2. Verify magic, entry table layout, compression method
3. Check whether `64Patch.meg` uses a different format version
4. Write Python extractor (first code deliverable)
5. Verify: extract a known file, compare with loose-file counterpart (e.g. `GameData/Data/*.txt` name lists)
6. Document how mods ship loose files vs. megas, and how the 64-bit port changed lookup

## Open Questions

- Does the 64-bit port use the same .meg format as 2006?
- Are megafiles memory-mapped at runtime? (PerceptionFunctionDLL gets `MegaFileManagerClass*` — suggests a manager with lookup)
- Compression: zlib? custom?
- Name hashing: CRC32? custom FNV?
