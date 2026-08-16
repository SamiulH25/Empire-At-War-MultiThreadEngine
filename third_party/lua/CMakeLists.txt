# Lua 5.1.5 (vendored from lua.org) — static library.
# Excludes the standalone interpreter (lua.c), compiler (luac.c), and
# print.c — we only need the embeddable library.
set(LUA_SRC
    src/lapi.c
    src/lauxlib.c
    src/lbaselib.c
    src/lcode.c
    src/ldblib.c
    src/ldebug.c
    src/ldo.c
    src/ldump.c
    src/lfunc.c
    src/lgc.c
    src/linit.c
    src/liolib.c
    src/llex.c
    src/lmathlib.c
    src/lmem.c
    src/loadlib.c
    src/lobject.c
    src/lopcodes.c
    src/loslib.c
    src/lparser.c
    src/lstate.c
    src/lstring.c
    src/lstrlib.c
    src/ltable.c
    src/ltablib.c
    src/ltm.c
    src/lundump.c
    src/lvm.c
    src/lzio.c
)

add_library(lua STATIC ${LUA_SRC})
target_include_directories(lua PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Lua 5.1 uses C89-style code; silence warnings on modern compilers.
if(MSVC)
    target_compile_options(lua PRIVATE /W0)
else()
    target_compile_options(lua PRIVATE -w)
endif()
