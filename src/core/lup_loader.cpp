// Custom undump for the game's `\x1bLup` bytecode dialect — see lup_loader.h.
#include "core/lup_loader.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "ldo.h"
#include "lfunc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "lzio.h"
}

namespace eaw {
namespace {

// Temporary: set via the LUP_DEBUG env var to trace loader progress.
bool dbg() { static const bool v = getenv("LUP_DEBUG") != nullptr; return v; }

// ---- reader ---------------------------------------------------------------

class Reader {
public:
    Reader(const char* d, size_t n) : data_(d), size_(n) {}

    bool eof() const { return pos_ >= size_; }
    size_t pos() const { return pos_; }
    size_t remaining() const { return size_ - pos_; }

    // Reads `n` raw bytes; returns false on truncation. `out` may be null
    // to skip.
    bool raw(void* out, size_t n) {
        if (remaining() < n) return false;
        if (out) std::memcpy(out, data_ + pos_, n);
        pos_ += n;
        return true;
    }

    bool byte(uint8_t& b) { return raw(&b, 1); }

    // 4-byte little-endian signed int.
    bool i32(int& v) {
        uint8_t b[4];
        if (!raw(b, 4)) return false;
        v = static_cast<int32_t>(
            static_cast<uint32_t>(b[0]) |
            (static_cast<uint32_t>(b[1]) << 8) |
            (static_cast<uint32_t>(b[2]) << 16) |
            (static_cast<uint32_t>(b[3]) << 24));
        return true;
    }

    // 8-byte little-endian double.
    bool f64(double& v) {
        uint64_t u;
        if (!raw(&u, 8)) return false;
        double d;
        std::memcpy(&d, &u, 8);
        v = d;
        return true;
    }

    // String: 4-byte length (0 = nil), then `len` bytes.
    // Returns false on truncation or negative length.
    bool str(std::string& out, bool& isNil) {
        int len;
        if (!i32(len)) return false;
        if (len < 0) return false;
        if (len == 0) { isNil = true; out.clear(); return true; }
        if (static_cast<size_t>(len) > remaining()) return false;
        out.assign(data_ + pos_, static_cast<size_t>(len));
        pos_ += static_cast<size_t>(len);
        isNil = false;
        return true;
    }

private:
    const char* data_;
    size_t size_;
    size_t pos_ = 0;
};

// ---- proto building -------------------------------------------------------

// Fills a Proto's constants from the stream. The fork's constants:
// count + [type byte + data] each (4 = string, 3 = number, 1 = bool, 0 = nil).
bool loadConstants(lua_State* L, Reader& r, Proto* f) {
    int n;
    if (!r.i32(n) || n < 0) return false;
    f->k = luaM_newvector(L, n, TValue);
    f->sizek = n;
    for (int i = 0; i < n; ++i) {
        TValue* o = &f->k[i];
        setnilvalue(o);
        uint8_t t;
        if (!r.byte(t)) return false;
        switch (t) {
            case 0:  // nil
                break;
            case 1: {  // boolean
                uint8_t b;
                if (!r.byte(b)) return false;
                setbvalue(o, b != 0);
                break;
            }
            case 3: {  // number
                double d;
                if (!r.f64(d)) return false;
                setnvalue(o, d);
                break;
            }
            case 4: {  // string
                std::string s;
                bool nil;
                if (!r.str(s, nil)) return false;
                if (nil) { setnilvalue(o); break; }
                setsvalue2n(L, o, luaS_newlstr(L, s.data(), s.size()));
                break;
            }
            default:
                return false;  // unknown constant type
        }
    }
    return true;
}

// Fills a Proto's code from the stream: count + count*4 bytes.
bool loadCode(lua_State* L, Reader& r, Proto* f) {
    int n;
    if (!r.i32(n) || n < 0) return false;
    f->code = luaM_newvector(L, n, Instruction);
    f->sizecode = n;
    // The fork's instructions are 4 bytes in the stream but the VM's
    // Instruction is 6 bytes (sizeof(Instruction)=6 in the header). The
    // loader expands them (FUN_1407c0090). For now we store the raw 4-byte
    // values in the low 32 bits of each 6-byte Instruction; the high bytes
    // stay 0. Full opcode translation is the remaining work.
    for (int i = 0; i < n; ++i) {
        int v;
        if (!r.i32(v)) return false;
        std::memset(&f->code[i], 0, sizeof(Instruction));
        std::memcpy(&f->code[i], &v, 4);
    }
    return true;
}

// Loads one function (FUN_1407c3cb0). Recurses for nested protos.
bool loadFunction(lua_State* L, Reader& r, Proto** out, int depth) {
    if (depth > 200) return false;  // too deep
    luaD_checkstack(L, 1);
    Proto* f = luaF_newproto(L);
    if (!f) return false;
    luaC_checkGC(L);
    setptvalue2s(L, L->top, f); incr_top(L);  // protect from GC
    *out = f;

    // source (4-byte length, 0 = nil)
    std::string src;
    bool srcNil;
    if (!r.str(src, srcNil)) return false;
    if (!srcNil) f->source = luaS_newlstr(L, src.data(), src.size());

    int linedefined, lastlinedefined;
    if (!r.i32(linedefined) || linedefined < 0) return false;
    if (!r.i32(lastlinedefined) || lastlinedefined < 0) return false;
    f->linedefined = linedefined;
    f->lastlinedefined = lastlinedefined;

    uint8_t nups, numparams, is_vararg, maxstacksize;
    if (!r.byte(nups)) return false;
    if (!r.byte(numparams)) return false;
    if (!r.byte(is_vararg)) return false;
    if (!r.byte(maxstacksize)) return false;
    f->nups = nups;
    f->numparams = numparams;
    f->is_vararg = is_vararg;
    f->maxstacksize = maxstacksize;

    if (!loadCode(L, r, f)) return false;

    // locvars: count + [str + 2 ints] each
    int nlv;
    if (!r.i32(nlv) || nlv < 0) return false;
    f->locvars = luaM_newvector(L, nlv, LocVar);
    f->sizelocvars = nlv;
    for (int i = 0; i < nlv; ++i) {
        std::string s;
        bool nil;
        if (!r.str(s, nil)) return false;
        f->locvars[i].varname = nil ? nullptr
                                    : luaS_newlstr(L, s.data(), s.size());
        int a, b;
        if (!r.i32(a) || !r.i32(b)) return false;
        f->locvars[i].startpc = a;
        f->locvars[i].endpc = b;
    }

    // upvalues: count + names (strings)
    int nu;
    if (!r.i32(nu) || nu < 0) return false;
    f->upvalues = luaM_newvector(L, nu, TString*);
    f->sizeupvalues = nu;
    for (int i = 0; i < nu; ++i) {
        std::string s;
        bool nil;
        if (!r.str(s, nil)) return false;
        f->upvalues[i] = nil ? nullptr : luaS_newlstr(L, s.data(), s.size());
    }

    if (!loadConstants(L, r, f)) return false;

    // nested protos
    int np;
    if (!r.i32(np) || np < 0) return false;
    f->p = luaM_newvector(L, np, Proto*);
    f->sizep = np;
    for (int i = 0; i < np; ++i) {
        if (!loadFunction(L, r, &f->p[i], depth + 1)) return false;
    }

    // lineinfo: count + count*4
    int nli;
    if (!r.i32(nli) || nli < 0) return false;
    f->lineinfo = luaM_newvector(L, nli, int);
    f->sizelineinfo = nli;
    for (int i = 0; i < nli; ++i) {
        int v;
        if (!r.i32(v)) return false;
        f->lineinfo[i] = v;
    }

    L->top--;  // pop the temp proto ref
    return true;
}

} // namespace

int loadLupChunk(lua_State* L, const char* data, size_t size,
                 const std::string& name) {
    Reader r(data, size);

    // Header (22 bytes): magic(4) version(1) format(1) sizes(8) numfmt(8).
    if (size < 22 || std::memcmp(data, "\x1bLup", 4) != 0) {
        lua_pushfstring(L, "%s: bad \\x1bLup header", name.c_str());
        return LUA_ERRSYNTAX;
    }
    if (data[4] != 0x51) {
        lua_pushfstring(L, "%s: unsupported version 0x%02x", name.c_str(),
                        static_cast<unsigned char>(data[4]));
        return LUA_ERRSYNTAX;
    }
    r.raw(nullptr, 22);  // skip the header

    luaD_checkstack(L, 4);
    if (dbg()) std::fprintf(stderr, "[lup] header ok, parsing function\n");
    Proto* f = nullptr;
    if (!loadFunction(L, r, &f, 0)) {
        lua_pushfstring(L, "%s: failed to parse \\x1bLup function at byte %d",
                        name.c_str(), static_cast<int>(r.pos()));
        return LUA_ERRSYNTAX;
    }
    if (dbg()) std::fprintf(stderr, "[lup] function parsed, %d bytes consumed\n",
                            static_cast<int>(r.pos()));
    if (!r.eof()) {
        // Trailing bytes are tolerated (the game's loader may leave slack).
    }

    // Wrap the proto in a closure and push it (mirrors f_parser in ldo.c).
    luaD_checkstack(L, 2);
    Closure* cl = luaF_newLclosure(L, f->nups, hvalue(gt(L)));
    cl->l.p = f;
    for (int i = 0; i < f->nups; ++i)
        cl->l.upvals[i] = luaF_newupval(L);
    setclvalue(L, L->top, cl);
    incr_top(L);
    return 0;
}

} // namespace eaw
