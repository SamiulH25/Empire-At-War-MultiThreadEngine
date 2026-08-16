#include "core/game_constants.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>

namespace eaw {

namespace {

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

bool parseInt(const std::string& s, int& out) {
    std::string t = trim(s);
    if (t.empty()) return false;
    auto res = std::from_chars(t.data(), t.data() + t.size(), out);
    return res.ec == std::errc() && res.ptr == t.data() + t.size();
}

bool parseDouble(const std::string& s, double& out) {
    std::string t = trim(s);
    if (t.empty()) return false;
    char* end = nullptr;
    out = std::strtod(t.c_str(), &end);
    return end == t.c_str() + t.size();
}

} // namespace

GameConstants GameConstants::Parse(const std::string& xmlText) {
    XmlNode root = ParseXml(xmlText);
    if (root.name != "GameConstants") {
        throw XmlError("expected <GameConstants> root, got <" + root.name + ">");
    }

    GameConstants gc;
    for (const XmlNode& child : root.children) {
        std::string key = child.name;
        std::string val = trim(child.text);

        if (key == "SpacePathfindMaxExpansions") {
            if (parseInt(val, gc.spacePathfindMaxExpansions)) continue;
        } else if (key == "SpacePathfindFrameDelayDelta") {
            if (parseInt(val, gc.spacePathfindFrameDelayDelta)) continue;
        } else if (key == "SpacePathFailureMaxExpansionsCoefficient") {
            if (parseDouble(val, gc.spacePathFailureMaxExpansionsCoefficient)) continue;
        } else if (key == "FramesPerCollisionCheck") {
            if (parseInt(val, gc.framesPerCollisionCheck)) continue;
        }

        // Generic pass-through (first occurrence)
        gc.values.try_emplace(key, val);
    }
    return gc;
}

} // namespace eaw
