// Petroglyph MegaFile (.meg) archive reader.
//
// Implements the documented Petrolution format:
//   https://modtools.petrolution.net/docs/MegFileFormat
//
// Format (little-endian):
//   Header:
//     +0000h numFilenames uint32
//     +0004h numFiles uint32
//   Filename Table record:
//     +0000h length uint16   ; length of filename in characters
//     +0004h name (ASCII, NOT zero-terminated)
//   File Table record (sorted by CRC ascending):
//     +0000h crc uint32      ; CRC-32 of the filename
//     +0004h index uint32
//     +0008h size uint32
//     +000Ch start uint32    ; offset of file data from start of .meg
//     +0010h name uint32     ; index into the Filename Table
//
// Format #3 adds a u32 flags + dataStart + filenamesSize header and optional
// AES-128-CBC encryption (flags == 0x8FFFFFFF). Encryption is not yet
// implemented; the reader throws for encrypted archives.
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>

namespace eaw {

class MegError : public std::runtime_error {
public:
    explicit MegError(const std::string& msg) : std::runtime_error(msg) {}
};

class MegFile {
public:
    struct Entry {
        uint32_t crc = 0;      // CRC-32 of the stored name
        uint32_t index = 0;    // position in the table
        uint32_t size = 0;     // decompressed size in bytes
        uint32_t start = 0;    // offset of data from start of .meg
        uint32_t nameIndex = 0; // index into filenames
    };

    // Loads and parses a .meg archive from raw bytes (format #1/#2).
    // Throws MegError on malformed or encrypted archives.
    static MegFile Parse(const std::vector<uint8_t>& bytes);

    const std::vector<std::string>& filenames() const { return filenames_; }
    const std::vector<Entry>& entries() const { return entries_; }
    size_t fileCount() const { return entries_.size(); }

    // Returns the stored (lookup) name for an entry.
    const std::string& nameOf(const Entry& e) const { return filenames_[e.nameIndex]; }

    // Reads the raw data for an entry from the archive bytes.
    std::vector<uint8_t> read(const Entry& e, const std::vector<uint8_t>& archive) const;

    // Finds an entry by stored name; returns nullptr if not present.
    const Entry* find(const std::string& name) const;

private:
    std::vector<std::string> filenames_;
    std::vector<Entry> entries_;
};

} // namespace eaw
