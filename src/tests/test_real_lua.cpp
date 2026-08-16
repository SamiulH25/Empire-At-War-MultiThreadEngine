// Real-world smoke test: load an actual game AI plan bytecode from config.meg.
#include "core/lua_host.h"
#include "core/meg_file.h"

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
        // The bytecode references the PG* engine bindings (ReserveForce etc.)
        // which we don't register yet — so loading is what we test (the chunk
        // defines functions; running it may call missing globals at exec time).
        bool loaded = false;
        try {
            lua.runScript(chunk, argv[2]);
            loaded = true;
        } catch (const eaw::LuaError& ex) {
            std::printf("load error: %s\n", ex.what());
        }
        check(loaded, "game bytecode loads in Lua 5.1 host");
        return failures ? 1 : 0;
    } catch (const std::exception& ex) {
        std::printf("error: %s\n", ex.what());
        return 1;
    }
}
