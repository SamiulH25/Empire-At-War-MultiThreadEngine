#include "core/meg_file.h"

#include <cstring>
#include <limits>

namespace eaw {

namespace {

uint16_t rd_u16(const std::vector<uint8_t>& b, size_t pos) {
    if (pos + 2 > b.size()) throw MegError("truncated header");
    return static_cast<uint16_t>(b[pos] | (b[pos + 1] << 8));
}

uint32_t rd_u32(const std::vector<uint8_t>& b, size_t pos) {
    if (pos + 4 > b.size()) throw MegError("truncated header");
    return static_cast<uint32_t>(b[pos]) |
           (static_cast<uint32_t>(b[pos + 1]) << 8) |
           (static_cast<uint32_t>(b[pos + 2]) << 16) |
           (static_cast<uint32_t>(b[pos + 3]) << 24);
}

} // namespace

MegFile MegFile::Parse(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 8) throw MegError("file too small to be a .meg archive");

    MegFile mf;
    size_t pos = 0;
    uint32_t numFilenames = rd_u32(bytes, pos); pos += 4;
    uint32_t numFiles = rd_u32(bytes, pos); pos += 4;

    // Filename table: u16 length + ASCII name each
    mf.filenames_.reserve(numFilenames);
    for (uint32_t i = 0; i < numFilenames; ++i) {
        uint16_t len = rd_u16(bytes, pos); pos += 2;
        if (pos + len > bytes.size()) throw MegError("filename table overruns file");
        mf.filenames_.emplace_back(reinterpret_cast<const char*>(bytes.data() + pos), len);
        pos += len;
    }

    // File table: 5 u32s per record, sorted by CRC
    mf.entries_.reserve(numFiles);
    for (uint32_t i = 0; i < numFiles; ++i) {
        Entry e;
        e.crc = rd_u32(bytes, pos); pos += 4;
        e.index = rd_u32(bytes, pos); pos += 4;
        e.size = rd_u32(bytes, pos); pos += 4;
        e.start = rd_u32(bytes, pos); pos += 4;
        e.nameIndex = rd_u32(bytes, pos); pos += 4;
        if (e.nameIndex >= mf.filenames_.size()) {
            throw MegError("file table references out-of-range filename index");
        }
        mf.entries_.push_back(e);
    }

    return mf;
}

std::vector<uint8_t> MegFile::read(const Entry& e, const std::vector<uint8_t>& archive) const {
    if (static_cast<size_t>(e.start) + e.size > archive.size()) {
        throw MegError("entry data overruns archive");
    }
    return std::vector<uint8_t>(archive.begin() + e.start,
                                archive.begin() + e.start + e.size);
}

const MegFile::Entry* MegFile::find(const std::string& name) const {
    for (const Entry& e : entries_) {
        if (nameOf(e) == name) return &e;
    }
    return nullptr;
}

} // namespace eaw
