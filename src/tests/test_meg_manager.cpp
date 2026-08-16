// Tests for MegaFileManager override precedence.
#include "core/meg_file.h"
#include "core/meg_manager.h"

#include <cstdio>
#include <vector>

namespace {

int failures = 0;
void check(bool c, const char* w) {
    std::printf("%s: %s\n", c ? "ok" : "FAIL", w);
    if (!c) ++failures;
}

// Minimal synthetic meg with one file.
std::vector<uint8_t> makeMeg(const std::string& name, const std::string& content) {
    std::vector<uint8_t> b;
    auto put16 = [&](uint16_t v) { b.push_back(v & 0xff); b.push_back(v >> 8); };
    auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) { b.push_back(v & 0xff); v >>= 8; } };
    put32(1); put32(1);
    put16(static_cast<uint16_t>(name.size()));
    b.insert(b.end(), name.begin(), name.end());
    size_t dataStart = 8 + 2 + name.size() + 20;
    put32(0xdeadbeef); put32(0); put32(static_cast<uint32_t>(content.size()));
    put32(static_cast<uint32_t>(dataStart)); put32(0);
    b.insert(b.end(), content.begin(), content.end());
    return b;
}

void testOverride() {
    auto a = makeMeg("F.XML", "from-A");
    auto b = makeMeg("F.XML", "from-B");
    auto c = makeMeg("G.XML", "only-C");
    eaw::MegFile ma = eaw::MegFile::Parse(a);
    eaw::MegFile mb = eaw::MegFile::Parse(b);
    eaw::MegFile mc = eaw::MegFile::Parse(c);

    eaw::MegaFileManager m;
    m.addArchive("A", a, ma);
    m.addArchive("B", b, mb);
    m.addArchive("C", c, mc);

    check(m.read("F.XML") == std::vector<uint8_t>({'f','r','o','m','-','B'}),
          "later archive overrides earlier");
    check(m.read("G.XML") == std::vector<uint8_t>({'o','n','l','y','-','C'}),
          "unique file resolves");
    check(!m.exists("NOPE"), "missing file does not exist");
}

void testLooseOverride() {
    auto a = makeMeg("F.XML", "from-A");
    eaw::MegFile ma = eaw::MegFile::Parse(a);
    eaw::MegaFileManager m;
    m.addArchive("A", a, ma);
    m.addLooseFile("F.XML", {'l','o','o','s','e'});
    check(m.read("F.XML") == std::vector<uint8_t>({'l','o','o','s','e'}),
          "loose file overrides archive");
}

void testNotFound() {
    eaw::MegaFileManager m;
    bool threw = false;
    try { m.read("NOPE"); } catch (const eaw::MegError&) { threw = true; }
    check(threw, "read missing throws");
}

} // namespace

int main() {
    testOverride();
    testLooseOverride();
    testNotFound();
    if (failures == 0) { std::printf("ALL TESTS PASSED\n"); return 0; }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
