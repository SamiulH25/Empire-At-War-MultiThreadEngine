#include "core/object_model.h"

#include <cmath>
#include <limits>

namespace eaw {

Player& SimState::addPlayer(const std::string& name, const std::string& faction) {
    Player p;
    p.id = nextPlayerId_++;
    p.name = name;
    p.factionName = faction;
    players_.push_back(p);
    return players_.back();
}

const Player* SimState::player(int id) const {
    for (const auto& p : players_) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

const Player* SimState::findPlayer(const std::string& name) const {
    for (const auto& p : players_) {
        if (p.name == name || p.factionName == name) return &p;
    }
    return nullptr;
}

ObjectType& SimState::addType(ObjectType t) {
    return types_[t.name] = std::move(t);
}

const ObjectType* SimState::type(const std::string& name) const {
    auto it = types_.find(name);
    return it == types_.end() ? nullptr : &it->second;
}

GameObject& SimState::addObject(const std::string& typeName, int playerId, const Vec3& pos) {
    GameObject o;
    o.id = nextObjectId_++;
    o.typeName = typeName;
    o.playerId = playerId;
    o.position = pos;
    return objects_[o.id] = std::move(o);
}

const GameObject* SimState::object(int id) const {
    auto it = objects_.find(id);
    return it == objects_.end() ? nullptr : &it->second;
}

GameObject* SimState::object(int id) {
    auto it = objects_.find(id);
    return it == objects_.end() ? nullptr : &it->second;
}

void SimState::removeObject(int id) {
    objects_.erase(id);
}

std::vector<const GameObject*> SimState::allObjects() const {
    std::vector<const GameObject*> out;
    out.reserve(objects_.size());
    for (const auto& [id, o] : objects_) out.push_back(&o);
    return out;
}

std::vector<const GameObject*> SimState::objectsOfType(const std::string& typeName) const {
    std::vector<const GameObject*> out;
    for (const auto& [id, o] : objects_) {
        if (o.typeName == typeName) out.push_back(&o);
    }
    return out;
}

std::vector<const GameObject*> SimState::objectsOfPlayer(int playerId) const {
    std::vector<const GameObject*> out;
    for (const auto& [id, o] : objects_) {
        if (o.playerId == playerId) out.push_back(&o);
    }
    return out;
}

std::vector<const GameObject*> SimState::objectsOfCategory(const std::string& category) const {
    std::vector<const GameObject*> out;
    for (const auto& [id, o] : objects_) {
        const ObjectType* t = type(o.typeName);
        if (!t) continue;
        for (const auto& c : t->categories) {
            if (c == category) { out.push_back(&o); break; }
        }
    }
    return out;
}

const GameObject* SimState::nearestObject(const Vec3& pos, const std::string& typeName) const {
    const GameObject* best = nullptr;
    double bestDist = std::numeric_limits<double>::max();
    for (const auto& [id, o] : objects_) {
        if (o.typeName != typeName || !o.alive) continue;
        double d = pos.distanceTo(o.position);
        if (d < bestDist) { bestDist = d; best = &o; }
    }
    return best;
}

std::vector<std::string> SimState::typeNames() const {
    std::vector<std::string> out;
    out.reserve(types_.size());
    for (const auto& [name, t] : types_) out.push_back(name);
    return out;
}

} // namespace eaw
