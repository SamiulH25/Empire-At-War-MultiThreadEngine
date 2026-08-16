// MegaFileManager — multi-archive file lookup with load-order override.
//
// Mirrors the game's documented mechanism (Petrolution docs): megas listed in
// MegaFiles.xml are loaded in order and merged into a master file table; a
// file in a later meg overrides earlier ones. Loose files on disk override
// everything (the mod mechanism).
#pragma once

#include "core/meg_file.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eaw {

class MegaFileManager {
public:
    // Adds an archive; later archives override earlier ones for duplicate names.
    // `bytes` must outlive the manager (or be copied — we copy the entries but
    // reference the bytes for reads).
    void addArchive(const std::string& label, const std::vector<uint8_t>& bytes,
                    const MegFile& meg);

    // Registers a loose file override: name -> raw bytes.
    void addLooseFile(const std::string& name, std::vector<uint8_t> bytes);

    // Returns true if `name` resolves (loose override or any archive).
    bool exists(const std::string& name) const;

    // Reads `name` with override precedence: loose > later archives > earlier.
    // Throws MegError if not found.
    std::vector<uint8_t> read(const std::string& name) const;

    size_t archiveCount() const { return archives_.size(); }
    size_t looseCount() const { return loose_.size(); }

    // Loose file names (for tooling/reporting).
    const std::unordered_map<std::string, std::vector<uint8_t>>& loose() const {
        return loose_;
    }

private:
    struct ArchiveRef {
        const std::vector<uint8_t>* bytes = nullptr;
        const MegFile* meg = nullptr;
        int order = 0; // higher = later (overrides)
    };

    std::unordered_map<std::string, ArchiveRef> master_; // name -> archive holding it
    std::unordered_map<std::string, std::vector<uint8_t>> loose_;
    std::vector<ArchiveRef> archives_;
    int nextOrder_ = 0;
};

} // namespace eaw
