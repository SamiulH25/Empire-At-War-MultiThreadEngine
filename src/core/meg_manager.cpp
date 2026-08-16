#include "core/meg_manager.h"

namespace eaw {

void MegaFileManager::addArchive(const std::string& label,
                                 const std::vector<uint8_t>& bytes,
                                 const MegFile& meg) {
    int order = nextOrder_++;
    for (const auto& e : meg.entries()) {
        const std::string& name = meg.nameOf(e);
        ArchiveRef ref{&bytes, &meg, order};
        auto it = master_.find(name);
        if (it == master_.end() || it->second.order < order) {
            master_[name] = ref;
        }
    }
    archives_.push_back(ArchiveRef{&bytes, &meg, order});
}

void MegaFileManager::addLooseFile(const std::string& name, std::vector<uint8_t> bytes) {
    loose_[name] = std::move(bytes);
}

bool MegaFileManager::exists(const std::string& name) const {
    return loose_.count(name) > 0 || master_.count(name) > 0;
}

std::vector<uint8_t> MegaFileManager::read(const std::string& name) const {
    auto li = loose_.find(name);
    if (li != loose_.end()) return li->second;

    auto mi = master_.find(name);
    if (mi == master_.end()) throw MegError("not found: " + name);
    const ArchiveRef& ref = mi->second;
    const auto* e = ref.meg->find(name);
    if (!e) throw MegError("internal: entry missing for " + name);
    return ref.meg->read(*e, *ref.bytes);
}

} // namespace eaw
