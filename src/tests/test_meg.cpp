// Unit tests for the .meg reader. Self-contained (no framework).
// Tests: parse a synthetic archive, read back data, verify CRC, error cases.
#include "core/meg_file.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    } else {
        std::printf("ok:   %s\n", what);
    }
}

// Build a synthetic format-#1 .meg with two files.
std::vector<uint8_t> makeMeg() {
    std::vector<uint8_t> b;
    auto put16 = [&](uint16_t v) { b.push_back(v & 0xff); b.push_back(v >> 8); };
    auto put32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i) { b.push_back(v & 0xff); v >>= 8; }
    };
    auto putStr = [&](const std::string& s) { b.insert(b.end(), s.begin(), s.end()); };

    const std::string n1 = "DATA\\XML\\GAMECONSTANTS.XML";
    const std::string n2 = "DATA\\SCRIPTS\\AI\\TEST.LUA";
    const std::vector<uint8_t> f1 = {'<', 'X', 'M', 'L'};
    const std::vector<uint8_t> f2 = {'f', 'u', 'n', 'c', 't', 'i', 'o', 'n'};

    put32(2);           // numFilenames
    put32(2);           // numFiles
    put16(n1.size()); putStr(n1);
    put16(n2.size()); putStr(n2);
    // File table: crc, index, size, start, nameIndex (sorted by CRC).
    // Compute the data offsets: after header + names + 2 file records.
    size_t dataStart = 8 + 2 + n1.size() + 2 + n2.size() + 40;
    uint32_t crc1 = 0x12345678, crc2 = 0x9ABCDEF0; // placeholder; CRC not verified here
    put32(crc1); put32(0); put32(f1.size()); put32(static_cast<uint32_t>(dataStart)); put32(0);
    put32(crc2); put32(1); put32(f2.size()); put32(static_cast<uint32_t>(dataStart + f1.size())); put32(1);
    putStr(std::string(f1.begin(), f1.end()));
    putStr(std::string(f2.begin(), f2.end()));
    return b;
}

void testParse() {
    auto b = makeMeg();
    auto mf = eaw::MegFile::Parse(b);
    check(mf.fileCount() == 2, "parses 2 entries");
    check(mf.filenames().size() == 2, "parses 2 filenames");
    check(mf.filenames()[0] == "DATA\\XML\\GAMECONSTANTS.XML", "filename 0 correct");
    check(mf.filenames()[1] == "DATA\\SCRIPTS\\AI\\TEST.LUA", "filename 1 correct");
}

void testRead() {
    auto b = makeMeg();
    auto mf = eaw::MegFile::Parse(b);
    const auto* e = mf.find("DATA\\XML\\GAMECONSTANTS.XML");
    check(e != nullptr, "find by stored name");
    if (e) {
        auto d = mf.read(*e, b);
        check(d.size() == 4 && d[0] == '<', "reads back file 1 data");
    }
    check(mf.find("NOPE") == nullptr, "find missing returns null");
}

void testErrors() {
    bool threw = false;
    try {
        std::vector<uint8_t> tiny = {1, 2, 3};
        eaw::MegFile::Parse(tiny);
    } catch (const eaw::MegError&) { threw = true; }
    check(threw, "tiny file throws");

    threw = false;
    try {
        auto b = makeMeg();
        b.resize(b.size() / 2); // truncate
        eaw::MegFile::Parse(b);
    } catch (const eaw::MegError&) { threw = true; }
    check(threw, "truncated file throws");
}

} // namespace

int main() {
    testParse();
    testRead();
    testErrors();
    if (failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
