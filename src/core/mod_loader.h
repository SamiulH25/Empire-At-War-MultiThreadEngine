// ModLoader — mounts a mod's Data folder (loose files + megas) into a
// MegaFileManager with game-override semantics.
//
// Mirrors how Empire at War boots mods (docs/research/05-mods.md):
//   1. The game loads its own megas first (Config.meg etc.).
//   2. A mod ships a Data/ folder; its megafiles.xml lists the megas to
//      load on top (later meg overrides earlier).
//   3. Loose files on disk override everything — the standard mod mechanism
//      for replacing a single XML/Lua file without repacking a meg.
//
// The loader also discovers unit/planet XMLs from the *file table* (loose +
// meg entries, case-insensitive), so mods that ship `SpaceUnitsFighters.xml`
// as a loose file or inside a custom meg both work.
#pragma once

#include "core/meg_file.h"
#include "core/meg_manager.h"
#include "core/object_model.h"
#include "core/unit_data_loader.h"

#include <string>
#include <vector>

namespace eaw {

struct ModReport {
    int megasLoaded = 0;
    int looseFiles = 0;
    int unitTypes = 0;
    int planets = 0;
    std::vector<std::string> megNames;
    std::vector<std::string> warnings;
};

class ModLoader {
public:
    // `modRoot` = path to the mod folder (containing Data/), or to the Data
    // folder itself. `gameRoot` = path to the game install (containing its
    // own Data/), optional — when given, the game's megas are loaded first so
    // the mod overrides them.
    static ModReport load(MegaFileManager& files, const std::string& modRoot,
                          const std::string& gameRoot = "");

    // Loads unit ObjectTypes from every known unit XML found in the file
    // table (case-insensitive; loose and meg entries).
    static std::vector<ObjectType> loadUnitTypes(const MegaFileManager& files);

    // Loads planets from PLANETS.XML in the file table (if present).
    static std::vector<Planet> loadPlanets(const MegaFileManager& files);

    // Reads a file table entry by name (case-insensitive); empty if absent.
    static std::vector<uint8_t> readInsensitive(const MegaFileManager& files,
                                                const std::string& name);

private:
    // Walks a directory recursively; calls `onFile` for each regular file
    // with its path relative to `root`.
    static void walkDir(const std::string& root, const std::string& dir,
                        void (*onFile)(const std::string& rel, void* ctx),
                        void* ctx);
};

} // namespace eaw
