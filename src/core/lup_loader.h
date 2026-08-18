// Custom undump for the game's `\x1bLup` bytecode dialect.
//
// Reverse-engineered from the game's loader (FUN_1407c3cb0 etc., see
// ghidra/lua_undump_*.txt and docs/research/03-lua-surface.md). Confirmed
// format:
//   header(22): magic \x1bLup, version 0x51, format, 8 size bytes,
//               8-byte number-format constant
//   function: source(4-byte len, 0=nil), linedefined(int),
//             lastlinedefined(int), nups/numparams/is_vararg/maxstacksize,
//             code count + code(count*4), locvars, upvalues, constants,
//             nested protos, lineinfo
//   constants: count + [type byte + data] (4 = string w/ 4-byte len,
//              3 = number 8 bytes, 1 = bool, 0 = nil)
//
// This loader constructs real Lua Proto objects (via the internal C API,
// the same way lundump.c does) so loaded chunks are executable functions.
#pragma once

#include <string>

struct lua_State;

namespace eaw {

// Loads a `\x1bLup` chunk onto `L`'s stack as a Lua function (closure).
// Returns 0 on success; on failure pushes an error message and returns a
// non-zero status (Lua semantics, matching luaL_loadbuffer).
int loadLupChunk(lua_State* L, const char* data, size_t size,
                 const std::string& name);

} // namespace eaw
