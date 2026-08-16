// gameconfig_tool — read GameConstants.xml from a .meg and print the parsed knobs.
//
// Usage:
//   gameconfig_tool <file.meg> [entry-name]
// Defaults to DATA\XML\GAMECONSTANTS.XML. Prints the performance-relevant
// settings parsed from the archive, plus total tag count.
#include "core/meg_file.h"
#include "core/game_constants.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace {

std::vector<uint8_t> readAll(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw eaw::MegError(std::string("cannot open ") + path);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),
                                std::istreambuf_iterator<char>());
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: gameconfig_tool <file.meg> [entry-name]\n");
        return 1;
    }
    try {
        auto bytes = readAll(argv[1]);
        auto mf = eaw::MegFile::Parse(bytes);
        const char* entryName = (argc > 2) ? argv[2] : "DATA\\XML\\GAMECONSTANTS.XML";
        const auto* e = mf.find(entryName);
        if (!e) { std::fprintf(stderr, "entry not found: %s\n", entryName); return 1; }
        auto data = mf.read(*e, bytes);
        std::string xml(data.begin(), data.end());
        auto gc = eaw::GameConstants::Parse(xml);
        std::printf("GameConstants (%s):\n", entryName);
        std::printf("  SpacePathfindMaxExpansions              = %d\n", gc.spacePathfindMaxExpansions);
        std::printf("  SpacePathfindFrameDelayDelta            = %d\n", gc.spacePathfindFrameDelayDelta);
        std::printf("  SpacePathFailureMaxExpansionsCoefficient = %.2f\n", gc.spacePathFailureMaxExpansionsCoefficient);
        std::printf("  FramesPerCollisionCheck                  = %d\n", gc.framesPerCollisionCheck);
        std::printf("  (total tags: %zu)\n", gc.values.size() + 4);
        return 0;
    } catch (const eaw::MegError& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    } catch (const eaw::XmlError& e) {
        std::fprintf(stderr, "xml error: %s\n", e.what());
        return 1;
    }
}
