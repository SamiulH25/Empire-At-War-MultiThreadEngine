#include "core/meg_manager.h"

#include <algorithm>
#include <cctype>

namespace eaw {

namespace {

// Normalizes a virtual file key to the toolchain convention: uppercase,
// forward slashes ("DATA/XML/FOO.XML"). Meg entries store the game's
// original form (backslashes, mixed case); loose files and discovery queries
// use this normalized form so everything matches.
std::string normalizeKey(const std::string& name) {
    std::string k = name;
    std::replace(k.begin(), k.end(), '\\', '/');
    std::transform(k.begin(), k.end(), k.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return k;
}

} // namespace

void MegaFileManager::addArchive(const std::string& label,
                                 const std::vector<uint8_t>& bytes,
                                 const MegFile& meg) {
    int order = nextOrder_++;
    auto ownedBytes = std::make_shared<const std::vector<uint8_t>>(bytes);
    auto ownedMeg = std::make_shared<MegFile>(meg); // manager keeps a copy alive
    for (const auto& e : meg.entries()) {
        std::string name = normalizeKey(meg.nameOf(e));
        ArchiveRef ref{ownedBytes, ownedMeg, order};
        auto it = master_.find(name);
        if (it == master_.end() || it->second.order < order) {
            master_[name] = ref;
        }
    }
    archives_.push_back(ArchiveRef{std::move(ownedBytes), std::move(ownedMeg), order});
}

void MegaFileManager::addLooseFile(const std::string& name, std::vector<uint8_t> bytes) {
    loose_[normalizeKey(name)] = std::move(bytes);
}

bool MegaFileManager::exists(const std::string& name) const {
    std::string k = normalizeKey(name);
    return loose_.count(k) > 0 || master_.count(k) > 0;
}

std::vector<uint8_t> MegaFileManager::read(const std::string& name) const {
    std::string k = normalizeKey(name);
    auto li = loose_.find(k);
    if (li != loose_.end()) return li->second;

    auto mi = master_.find(k);
    if (mi == master_.end()) throw MegError("not found: " + name);
    const ArchiveRef& ref = mi->second;
    // The stored name for this entry is the original (non-normalized) form;
    // find the entry by scanning for the normalized match.
    const MegFile::Entry* found = nullptr;
    for (const auto& ent : ref.meg->entries()) {
        if (normalizeKey(ref.meg->nameOf(ent)) == k) { found = &ent; break; }
    }
    if (!found) throw MegError("internal: entry missing for " + name);
    return ref.meg->read(*found, *ref.bytes);
}

} // namespace eaw
