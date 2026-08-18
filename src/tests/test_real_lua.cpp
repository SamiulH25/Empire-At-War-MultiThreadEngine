// Real-world smoke test: load an actual game AI plan bytecode from config.meg.
#include "core/lua_host.h"
#include "core/meg_file.h"

extern "C" {
#include "lua.h"
}

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

int failures = 0;
void check(bool c, const char* w) {
    std::printf("%s: %s\n", c ? "ok" : "FAIL", w);
    if (!c) ++failures;
}

std::vector<uint8_t> readAll(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),
                                std::istreambuf_iterator<char>());
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: real_lua_test <meg> <entry>\n"); return 1; }
    try {
        auto bytes = readAll(argv[1]);
        auto mf = eaw::MegFile::Parse(bytes);
        const auto* e = mf.find(argv[2]);
        if (!e) { std::printf("FAIL: entry not found\n"); return 1; }
        auto data = mf.read(*e, bytes);
        std::string chunk(data.begin(), data.end());

        eaw::LuaHost lua;
        // The bytecode is the game's custom `\x1bLup` dialect. The engine's
        // loadChunk parses it via the reverse-engineered loader (lup_loader)
        // and pushes a Lua closure. `loaded` = the full chunk parsed.
        int status = lua.loadChunk(chunk, argv[2]);
        bool loaded = status == 0;
        if (!loaded) {
            const char* msg = lua_tostring(lua.state(), -1);
            std::printf("load status %d: %s\n", status, msg ? msg : "(no msg)");
            lua_pop(lua.state(), 1);
        }
        check(loaded, "game bytecode chunk parses and loads as a closure");
        return loaded ? 0 : 1;
    } catch (const std::exception& ex) {
        std::printf("error: %s\n", ex.what());
        return 1;
    }
}
