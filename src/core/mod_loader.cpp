#include "core/mod_loader.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

namespace eaw {

namespace {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Locates the data folder (holding megafiles.xml) under a root: the root
// itself, root\Data, or root\GameData\Data (the game's actual layout).
std::string resolveDataDir(const std::string& root) {
    if (fs::exists(fs::path(root) / "megafiles.xml")) return root;
    for (const char* sub : {"Data", "GameData\\Data", "GameData/Data"}) {
        std::string cand = (fs::path(root) / sub).string();
        if (fs::exists(fs::path(cand) / "megafiles.xml")) return cand;
    }
    return root; // no megafiles.xml; caller will treat it as data dir anyway
}

// Case-insensitive lookup in the manager's loose table / meg table by trying
// exact first, then scanning. The manager exposes loose() but not the master
// table, so we check existence via read() in a try.
bool tryRead(const MegaFileManager& files, const std::string& name,
             std::vector<uint8_t>& out) {
    if (!files.exists(name)) return false;
    try {
        out = files.read(name);
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<uint8_t> readDiskFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
}

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    size_t e = s.find_last_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    return s.substr(b, e - b + 1);
}

} // namespace

void ModLoader::walkDir(const std::string& root, const std::string& dir,
                        void (*onFile)(const std::string& rel, void* ctx),
                        void* ctx) {
    std::error_code ec;
    fs::path full = fs::path(root) / dir;
    fs::directory_iterator it(full, ec);
    if (ec) return;
    for (const auto& e : it) {
        if (e.is_directory(ec)) {
            walkDir(root, (fs::path(dir) / e.path().filename()).string(), onFile, ctx);
        } else if (e.is_regular_file(ec)) {
            fs::path rel = fs::path(dir) / e.path().filename();
            onFile(rel.string(), ctx);
        }
    }
}

namespace {

// Directory walk callback: register a loose file into the manager.
struct LooseCtx {
    MegaFileManager* files;
    std::string root;
    ModReport* report;
};

void onLooseFile(const std::string& rel, void* pctx) {
    LooseCtx* ctx = static_cast<LooseCtx*>(pctx);
    std::string full = (fs::path(ctx->root) / rel).string();
    auto bytes = readDiskFile(full);
    if (bytes.empty()) return;
    // Manager keys are the game's virtual names, normalized to the
    // toolchain's convention: "DATA/XML/FOO.XML" (uppercase, forward slashes).
    std::string key = "DATA/" + rel;
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    std::replace(key.begin(), key.end(), '\\', '/');
    ctx->files->addLooseFile(key, std::move(bytes));
    ++ctx->report->looseFiles;
}

} // namespace

ModReport ModLoader::load(MegaFileManager& files, const std::string& modRoot,
                          const std::string& gameRoot) {
    ModReport report;

    // Locate the mod's Data folder (accept the mod root, Data, or the game's
    // GameData\Data layout).
    std::string dataDir = resolveDataDir(modRoot);

    // 1. Game megas first (optional game install).
    if (!gameRoot.empty()) {
        std::string gameData = resolveDataDir(gameRoot);
        std::string megaList = (fs::path(gameData) / "megafiles.xml").string();
        auto listBytes = readDiskFile(megaList);
        if (!listBytes.empty()) {
            std::string xml(listBytes.begin(), listBytes.end());
            XmlNode root = ParseXml(xml);
            for (const XmlNode& f : root.children) {
                if (f.name != "File") continue;
                std::string rel = trim(f.text);
                if (rel.find("Data\\") != 0 && rel.find("Data/") != 0) continue;
                std::string relOnly = rel.substr(5);
                std::string path = (fs::path(gameData) / relOnly).string();
                auto bytes = readDiskFile(path);
                if (bytes.empty()) { report.warnings.push_back("game meg missing: " + rel); continue; }
                try {
                    MegFile meg = MegFile::Parse(bytes);
                    files.addArchive(rel, bytes, meg);
                    report.megNames.push_back(rel);
                } catch (const MegError& e) {
                    report.warnings.push_back(std::string("game meg parse: ") + e.what());
                }
            }
        }
    }

    // 2. Mod megas (from the mod's megafiles.xml).
    std::string modMegaList = (fs::path(dataDir) / "megafiles.xml").string();
    auto listBytes = readDiskFile(modMegaList);
    if (!listBytes.empty()) {
        std::string xml(listBytes.begin(), listBytes.end());
        XmlNode root = ParseXml(xml);
        for (const XmlNode& f : root.children) {
            if (f.name != "File") continue;
            std::string rel = trim(f.text);
            std::string relOnly = rel;
            if (rel.find("Data\\") == 0 || rel.find("Data/") == 0) relOnly = rel.substr(5);
            std::string path = (fs::path(dataDir) / relOnly).string();
            auto bytes = readDiskFile(path);
            if (bytes.empty()) { report.warnings.push_back("mod meg missing: " + rel); continue; }
            try {
                MegFile meg = MegFile::Parse(bytes);
                files.addArchive(rel, bytes, meg);
                report.megNames.push_back(rel);
                ++report.megasLoaded;
            } catch (const MegError& e) {
                report.warnings.push_back(std::string("mod meg parse: ") + e.what());
            }
        }
    }

    // 3. Loose files: everything under Data/ overrides archives.
    LooseCtx ctx{&files, dataDir, &report};
    walkDir(dataDir, "", onLooseFile, &ctx);

    return report;
}

std::vector<uint8_t> ModLoader::readInsensitive(const MegaFileManager& files,
                                                const std::string& name) {
    std::vector<uint8_t> out;
    if (tryRead(files, name, out)) return out;
    std::string lower = toLower(name);
    // Scan loose files (small map).
    for (const auto& [k, v] : files.loose()) {
        if (toLower(k) == lower) return v;
    }
    // Archives: not enumerable via public API; do a direct read attempt with
    // the exact name only (most game/meg entries are already uppercase, and
    // mod loose files carry the true case).
    return {};
}

std::vector<ObjectType> ModLoader::loadUnitTypes(const MegaFileManager& files) {
    UnitDataLoader loader;
    std::vector<ObjectType> all;
    // Known unit XML names in the game's Data\XML directory (any case).
    const char* names[] = {
        "Data\\XML\\SpaceUnitsFighters.xml", "Data\\XML\\SpaceUnitsCapital.xml",
        "Data\\XML\\SpaceUnitsCorvettes.xml", "Data\\XML\\SpaceUnitsFrigates.xml",
        "Data\\XML\\SpaceUnitsSupers.xml",    "Data\\XML\\GroundVehicles.xml",
        "Data\\XML\\GroundInfantry.xml",      "Data\\XML\\GroundStructures.xml",
        "Data\\XML\\SpecialStructures.xml",
    };
    // Mods also ship UNITS_*.XML files — load those too.
    // (File table scan not exposed; these cover the TR set.)
    const char* extra[] = {
        "Data\\XML\\Units_Space_Empire_TIE_Defender.xml",
        "Data\\XML\\Units_Space_Empire_TIE_Interceptor.xml",
        "Data\\XML\\Units_Space_Empire_TIE_Phantom.xml",
        "Data\\XML\\Units_Space_Empire_Executor.xml",
        "Data\\XML\\Units_Space_Rebel_BWing.xml",
        "Data\\XML\\Units_Space_Rebel_YKL-37R.xml",
        "Data\\XML\\Units_Land_Empire_DarkTroopers.xml",
        "Data\\XML\\Units_Land_Empire_Death_Trooper.xml",
        "Data\\XML\\Units_Land_Empire_Juggernaut.xml",
        "Data\\XML\\Units_Land_Empire_Striker.xml",
        "Data\\XML\\Units_Land_Rebel_AT-TE.xml",
        "Data\\XML\\Units_Land_Underworld_Canderous_Tank.xml",
        "Data\\XML\\Units_Land_Underworld_DestroyerDroids.xml",
        "Data\\XML\\Units_Land_Underworld_Pirate_Speeder_2.xml",
        "Data\\XML\\Units_Space_Underworld_Crusader_Gunship.xml",
        "Data\\XML\\Units_Space_Underworld_Interceptor4.xml",
        "Data\\XML\\Units_Space_Underworld_Krayt_Destroyer.xml",
        "Data\\XML\\Units_Space_Underworld_Skipray.xml",
        "Data\\XML\\Units_Space_Underworld_StarViper.xml",
        "Data\\XML\\Units_Space_Underworld_Vengeance.xml",
        "Data\\XML\\Units_Space_Underworld_Vulture_Droid.xml",
        "Data\\XML\\Units_Space_Underworld_Buzz_Droids.xml",
        "Data\\XML\\Units_Space_Underworld_Kadalbe_Battleship.xml",
        "Data\\XML\\Units_Space_Hyena.xml",
        "Data\\XML\\Units_Space_Rebel_MC30_Frigate.xml",
    };
    for (const char* n : names) {
        auto bytes = readInsensitive(files, n);
        if (bytes.empty()) continue;
        try {
            std::string text(bytes.begin(), bytes.end());
            auto types = loader.loadXml(text);
            all.insert(all.end(), types.begin(), types.end());
        } catch (...) {
            // Skip malformed unit XML.
        }
    }
    for (const char* n : extra) {
        auto bytes = readInsensitive(files, n);
        if (bytes.empty()) continue;
        try {
            std::string text(bytes.begin(), bytes.end());
            auto types = loader.loadXml(text);
            all.insert(all.end(), types.begin(), types.end());
        } catch (...) {
        }
    }
    return all;
}

std::vector<Planet> ModLoader::loadPlanets(const MegaFileManager& files) {
    auto bytes = readInsensitive(files, "Data\\XML\\PLANETS.XML");
    if (bytes.empty()) return {};
    try {
        std::string text(bytes.begin(), bytes.end());
        UnitDataLoader loader;
        return loader.loadPlanets(text);
    } catch (...) {
        return {};
    }
}

} // namespace eaw
