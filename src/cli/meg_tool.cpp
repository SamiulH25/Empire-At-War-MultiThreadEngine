// meg_tool — CLI for reading Petroglyph .meg archives.
//
// Usage:
//   meg_tool list <file.meg>                 — list all files (name, size, crc)
//   meg_tool extract <file.meg> <name> <out> — extract one entry by name
//   meg_tool verify <file.meg>               — verify CRC-32 of every filename
//
#include "core/meg_file.h"

#include <cstdio>
#include <cstring>
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

uint32_t crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

int cmdList(const char* megPath) {
    auto bytes = readAll(megPath);
    auto mf = eaw::MegFile::Parse(bytes);
    std::printf("%zu files\n", mf.fileCount());
    for (const auto& e : mf.entries()) {
        std::printf("%10u  %08x  %s\n", e.size, e.crc, mf.nameOf(e).c_str());
    }
    return 0;
}

int cmdExtract(const char* megPath, const char* name, const char* outPath) {
    auto bytes = readAll(megPath);
    auto mf = eaw::MegFile::Parse(bytes);
    const auto* e = mf.find(name);
    if (!e) {
        std::fprintf(stderr, "not found: %s\n", name);
        return 1;
    }
    auto data = mf.read(*e, bytes);
    std::ofstream out(outPath, std::ios::binary);
    if (!out) { std::fprintf(stderr, "cannot write %s\n", outPath); return 1; }
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
    std::printf("extracted %s (%u bytes)\n", outPath, e->size);
    return 0;
}

int cmdVerify(const char* megPath) {
    auto bytes = readAll(megPath);
    auto mf = eaw::MegFile::Parse(bytes);
    int bad = 0;
    for (const auto& e : mf.entries()) {
        const std::string& n = mf.nameOf(e);
        uint32_t want = crc32(reinterpret_cast<const uint8_t*>(n.data()), n.size());
        if (want != e.crc) {
            std::printf("CRC MISMATCH %s: stored %08x computed %08x\n",
                        n.c_str(), e.crc, want);
            ++bad;
        }
    }
    std::printf("%zu entries, %d CRC mismatches\n", mf.fileCount(), bad);
    return bad ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: meg_tool <list|extract|verify> <file.meg> [name] [out]\n");
        return 1;
    }
    try {
        const char* cmd = argv[1];
        if (std::strcmp(cmd, "list") == 0) return cmdList(argv[2]);
        if (std::strcmp(cmd, "extract") == 0) {
            if (argc < 5) { std::fprintf(stderr, "extract needs <name> <out>\n"); return 1; }
            return cmdExtract(argv[2], argv[3], argv[4]);
        }
        if (std::strcmp(cmd, "verify") == 0) return cmdVerify(argv[2]);
        std::fprintf(stderr, "unknown command: %s\n", cmd);
        return 1;
    } catch (const eaw::MegError& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
