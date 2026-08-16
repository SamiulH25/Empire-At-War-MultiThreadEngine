#include "core/object_model.h"

#include <algorithm>
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

Player* SimState::player(int id) {
    for (auto& p : players_) {
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

// --- diplomacy ------------------------------------------------------------

void SimState::makeAlly(int a, int b) {
    if (a == b) return;
    auto addAlly = [](Player& p, int other) {
        if (std::find(p.allies.begin(), p.allies.end(), other) == p.allies.end()) {
            p.allies.push_back(other);
        }
    };
    if (Player* pa = player(a)) addAlly(*pa, b);
    if (Player* pb = player(b)) addAlly(*pb, a);
}

void SimState::makeEnemy(int a, int b) {
    if (a == b) return;
    auto removeAlly = [](Player& p, int other) {
        p.allies.erase(std::remove(p.allies.begin(), p.allies.end(), other),
                       p.allies.end());
    };
    if (Player* pa = player(a)) removeAlly(*pa, b);
    if (Player* pb = player(b)) removeAlly(*pb, a);
}

bool SimState::isAlly(int a, int b) const {
    if (a == b) return true;
    const Player* pa = player(a);
    if (!pa) return false;
    return std::find(pa->allies.begin(), pa->allies.end(), b) != pa->allies.end();
}

bool SimState::isEnemy(int a, int b) const {
    return a != b && !isAlly(a, b);
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

int SimState::spawnUnit(const std::string& typeName, int playerId, const Vec3& pos) {
    if (!type(typeName)) return 0;
    Vec3 p = pos;
    // Trivial collision check: if something already occupies the exact spot,
    // nudge along +x until free.
    bool occupied = true;
    while (occupied) {
        occupied = false;
        for (const auto& [id, o] : objects_) {
            if (o.typeName == typeName && o.position.x == p.x &&
                o.position.y == p.y && o.position.z == p.z) {
                occupied = true;
                break;
            }
        }
        if (occupied) p.x += 1.0;
    }
    return addObject(typeName, playerId, p).id;
}

bool SimState::garrisonUnit(int unitId, int containerId) {
    GameObject* unit = object(unitId);
    GameObject* container = object(containerId);
    if (!unit || !container || !unit->alive || !container->alive) return false;
    if (unit->inGarrison) ungarrisonUnit(unitId);
    container->garrisonedUnits.push_back(unitId);
    unit->inGarrison = true;
    return true;
}

void SimState::ungarrisonUnit(int unitId) {
    GameObject* unit = object(unitId);
    if (!unit) return;
    unit->inGarrison = false;
    for (auto& [id, o] : objects_) {
        auto& g = o.garrisonedUnits;
        g.erase(std::remove(g.begin(), g.end(), unitId), g.end());
    }
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
