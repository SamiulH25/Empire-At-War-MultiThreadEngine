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

// --- economy --------------------------------------------------------------

void SimState::giveMoney(int playerId, double amount) {
    if (Player* p = player(playerId)) p->credits += amount;
}

void SimState::setTechLevel(int playerId, int level) {
    if (Player* p = player(playerId)) p->techLevel = std::max(0, level);
}

void SimState::unlockType(int playerId, const std::string& typeName) {
    Player* p = player(playerId);
    if (!p) return;
    p->lockedTypes.erase(
        std::remove(p->lockedTypes.begin(), p->lockedTypes.end(), typeName),
        p->lockedTypes.end());
}

void SimState::lockType(int playerId, const std::string& typeName) {
    Player* p = player(playerId);
    if (!p) return;
    if (std::find(p->lockedTypes.begin(), p->lockedTypes.end(), typeName) ==
        p->lockedTypes.end()) {
        p->lockedTypes.push_back(typeName);
    }
}

bool SimState::canBuild(int playerId, const std::string& typeName) const {
    const Player* p = player(playerId);
    const ObjectType* t = type(typeName);
    if (!p || !t) return false;
    if (t->techLevel > p->techLevel) return false;
    if (std::find(p->lockedTypes.begin(), p->lockedTypes.end(), typeName) !=
        p->lockedTypes.end()) {
        return false;
    }
    return true;
}

bool SimState::canAfford(int playerId, const std::string& typeName) const {
    const Player* p = player(playerId);
    const ObjectType* t = type(typeName);
    if (!p || !t) return false;
    return p->credits >= t->buildCost;
}

int SimState::buildUnit(int playerId, const std::string& typeName, const Vec3& pos) {
    const ObjectType* t = type(typeName);
    Player* p = player(playerId);
    if (!t || !p) return 0;
    if (!canBuild(playerId, typeName) || p->credits < t->buildCost) return 0;
    p->credits -= t->buildCost;
    return spawnUnit(typeName, playerId, pos);
}

// --- abilities -----------------------------------------------------------

const ObjectType::Ability* SimState::abilityDef(int objectId,
                                                const std::string& name) const {
    const GameObject* o = object(objectId);
    if (!o) return nullptr;
    const ObjectType* t = type(o->typeName);
    if (!t) return nullptr;
    for (const auto& a : t->abilities) {
        if (a.name == name) return &a;
    }
    return nullptr;
}

bool SimState::hasAbility(int objectId, const std::string& name) const {
    return abilityDef(objectId, name) != nullptr;
}

// Finds (or lazily creates) the runtime state entry for an ability.
GameObject::AbilityState* abilityState(GameObject* o, const std::string& name) {
    for (auto& [n, st] : o->abilityStates) {
        if (n == name) return &st;
    }
    o->abilityStates.emplace_back(name, GameObject::AbilityState{});
    return &o->abilityStates.back().second;
}

bool SimState::isAbilityReady(int objectId, const std::string& name) const {
    const GameObject* o = object(objectId);
    if (!o || !abilityDef(objectId, name)) return false;
    for (const auto& [n, st] : o->abilityStates) {
        if (n == name) return st.cooldownRemaining <= 0.0;
    }
    return true; // never used -> ready
}

bool SimState::isAbilityActive(int objectId, const std::string& name) const {
    const GameObject* o = object(objectId);
    if (!o) return false;
    for (const auto& [n, st] : o->abilityStates) {
        if (n == name) return st.active;
    }
    return false;
}

double SimState::abilityCooldownLeft(int objectId, const std::string& name) const {
    const GameObject* o = object(objectId);
    if (!o) return 0.0;
    for (const auto& [n, st] : o->abilityStates) {
        if (n == name) return st.cooldownRemaining;
    }
    return 0.0;
}

bool SimState::activateAbility(int objectId, const std::string& name, int targetId) {
    GameObject* o = object(objectId);
    const ObjectType::Ability* def = abilityDef(objectId, name);
    if (!o || !def || !o->alive) return false;
    GameObject::AbilityState* st = abilityState(o, name);
    if (st->cooldownRemaining > 0.0) return false; // on cooldown
    // Target checks: must be a live enemy within range (if targeted).
    const GameObject* target = targetId ? object(targetId) : nullptr;
    if (def->requiresTarget) {
        if (!target || !target->alive) return false;
        if (isAlly(o->playerId, target->playerId)) return false;
        double d = o->position.distanceTo(target->position);
        if (def->range > 0.0 && d > def->range) return false;
    }
    // Apply the effect: damage the target (or self for untargeted).
    if (target && def->damage > 0.0 && !target->invulnerable) {
        GameObject* t = object(targetId);
        double shieldAbsorb = std::min(t->shield, def->damage);
        t->shield -= shieldAbsorb;
        t->hull = std::max(0.0, t->hull - (def->damage - shieldAbsorb));
        t->wasDamagedThisTick = true;
        if (t->hull == 0.0) t->alive = false;
    }
    st->cooldownRemaining = def->cooldown;
    st->active = true;
    return true;
}

void SimState::cancelAbility(int objectId, const std::string& name) {
    GameObject* o = object(objectId);
    if (!o) return;
    for (auto& [n, st] : o->abilityStates) {
        if (n == name) { st.active = false; return; }
    }
}

void SimState::resetAbilityCooldown(int objectId, const std::string& name) {
    GameObject* o = object(objectId);
    if (!o) return;
    for (auto& [n, st] : o->abilityStates) {
        if (n == name) { st.cooldownRemaining = 0.0; return; }
    }
}

void SimState::tickAbilities(double dt) {
    for (auto& [id, o] : objects_) {
        for (auto& [n, st] : o.abilityStates) {
            st.cooldownRemaining = std::max(0.0, st.cooldownRemaining - dt);
        }
    }
}

// --- fog of war ----------------------------------------------------------

namespace {
// Coarse reveal-cell size (game units). Keeps the revealed-cell set small.
constexpr double kRevealCell = 200.0;
constexpr double kRevealRange = 300.0; // a unit's sight radius

int64_t cellKey(double x, double y) {
    int64_t cx = static_cast<int64_t>(std::floor(x / kRevealCell));
    int64_t cy = static_cast<int64_t>(std::floor(y / kRevealCell));
    return (cx << 32) ^ (cy & 0xffffffffLL);
}
} // namespace

void SimState::revealArea(int playerId, const Vec3& center, double radius) {
    Player* p = player(playerId);
    if (!p) return;
    int cells = static_cast<int>(std::ceil(radius / kRevealCell)) + 1;
    for (int dx = -cells; dx <= cells; ++dx) {
        for (int dy = -cells; dy <= cells; ++dy) {
            double cx = center.x + dx * kRevealCell;
            double cy = center.y + dy * kRevealCell;
            if (std::hypot(cx - center.x, cy - center.y) <= radius) {
                p->revealedCells[cellKey(cx, cy)] = 0.0;
            }
        }
    }
}

void SimState::revealAll(int playerId) {
    if (Player* p = player(playerId)) p->revealAll = true;
}

void SimState::updateVisibility(int playerId, double now) {
    Player* p = player(playerId);
    if (!p) return;
    // Gather the player's unit positions (sight sources).
    std::vector<Vec3> sights;
    for (const auto& [id, o] : objects_) {
        if (o.playerId == playerId && o.alive) sights.push_back(o.position);
    }
    for (auto& [id, o] : objects_) {
        if (!o.alive) continue;
        bool visible = p->revealAll;
        if (!visible) {
            // In a permanently revealed cell?
            if (p->revealedCells.count(cellKey(o.position.x, o.position.y))) {
                visible = true;
            } else {
                // Within sight of any of the player's units?
                for (const Vec3& s : sights) {
                    if (s.distanceTo(o.position) <= kRevealRange) {
                        visible = true;
                        break;
                    }
                }
            }
        }
        if (visible) {
            p->lastSeen[id] = now;
        } else if (!p->lastSeen.count(id)) {
            p->lastSeen[id] = -1.0; // known but never seen
        }
    }
}

double SimState::timeSinceSeen(int playerId, int objectId, double now) const {
    const Player* p = player(playerId);
    if (!p) return 0.0;
    auto it = p->lastSeen.find(objectId);
    if (it == p->lastSeen.end()) return 1e18; // never seen
    if (it->second < 0.0) return 1e18;
    if (it->second >= now) return 0.0;
    return now - it->second;
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
    // Apply type-derived stats (shield fraction, move speed).
    const ObjectType* t = type(typeName);
    if (t) {
        o.shield = t->shieldFraction;
        o.moveSpeed = t->moveSpeed;
    }
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

// --- taskforces -----------------------------------------------------------

int SimState::addTaskForce(int playerId, const std::string& name) {
    TaskForce f;
    f.id = nextForceId_++;
    f.playerId = playerId;
    f.name = name;
    forces_.push_back(std::move(f));
    return forces_.back().id;
}

const TaskForce* SimState::taskForce(int id) const {
    for (const auto& f : forces_) {
        if (f.id == id) return &f;
    }
    return nullptr;
}

TaskForce* SimState::taskForce(int id) {
    for (auto& f : forces_) {
        if (f.id == id) return &f;
    }
    return nullptr;
}

bool SimState::addUnitToForce(int forceId, int unitId) {
    TaskForce* f = taskForce(forceId);
    if (!f || !object(unitId)) return false;
    if (std::find(f->units.begin(), f->units.end(), unitId) == f->units.end()) {
        f->units.push_back(unitId);
    }
    return true;
}

void SimState::removeUnitFromForce(int forceId, int unitId) {
    TaskForce* f = taskForce(forceId);
    if (!f) return;
    f->units.erase(std::remove(f->units.begin(), f->units.end(), unitId),
                   f->units.end());
}

void SimState::pruneDeadUnits() {
    for (auto& f : forces_) {
        for (size_t i = 0; i < f.units.size();) {
            const GameObject* o = object(f.units[i]);
            if (!o || !o->alive) {
                f.units.erase(f.units.begin() + i);
            } else {
                ++i;
            }
        }
    }
}

double SimState::forceThreat(int forceId) const {
    const TaskForce* f = taskForce(forceId);
    if (!f) return 0.0;
    double sum = 0.0;
    for (int id : f->units) {
        const GameObject* o = object(id);
        if (o && o->alive) sum += o->hull;
    }
    return sum;
}

std::vector<const TaskForce*> SimState::forcesOfPlayer(int playerId) const {
    std::vector<const TaskForce*> out;
    for (const auto& f : forces_) {
        if (f.playerId == playerId) out.push_back(&f);
    }
    return out;
}

// --- planets (galactic mode) ----------------------------------------------

int SimState::addPlanet(const std::string& name, const std::string& factionName,
                        const Vec3& pos) {
    Planet p;
    p.id = nextPlanetId_++;
    p.name = name;
    p.factionName = factionName;
    p.position = pos;
    planets_.push_back(std::move(p));
    return planets_.back().id;
}

const Planet* SimState::planet(int id) const {
    for (const auto& p : planets_) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

Planet* SimState::planet(int id) {
    for (auto& p : planets_) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

const Planet* SimState::findPlanet(const std::string& name) const {
    for (const auto& p : planets_) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

Planet* SimState::findPlanet(const std::string& name) {
    for (auto& p : planets_) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

std::vector<const Planet*> SimState::allPlanets() const {
    std::vector<const Planet*> out;
    out.reserve(planets_.size());
    for (const auto& p : planets_) out.push_back(&p);
    return out;
}

int SimState::forcePlanet(int forceId) const {
    const TaskForce* f = taskForce(forceId);
    return f ? f->planetId : -1;
}

bool SimState::startTransit(int forceId, int toPlanetId) {
    TaskForce* f = taskForce(forceId);
    if (!f || f->inTransit) return false;
    const Planet* from = planet(f->planetId);
    const Planet* to = planet(toPlanetId);
    if (!from || !to || from->id == to->id) return false;
    f->inTransit = true;
    f->fromPlanetId = f->planetId;
    f->toPlanetId = toPlanetId;
    // Hyperspace speed: 20 units of distance per second (a fleet crosses
    // the galaxy in seconds, like the game).
    constexpr double kHyperspaceSpeed = 20.0;
    double dist = from->position.distanceTo(to->position);
    f->travelSeconds = std::max(1.0, dist / kHyperspaceSpeed);
    f->elapsedSeconds = 0.0;
    // Units leave their visible position (go "into hyperspace").
    for (int uid : f->units) {
        GameObject* o = object(uid);
        if (o) o->hidden = true;
    }
    return true;
}

bool SimState::forceInTransit(int forceId) const {
    const TaskForce* f = taskForce(forceId);
    return f && f->inTransit;
}

int SimState::forceTransitTarget(int forceId) const {
    const TaskForce* f = taskForce(forceId);
    return (f && f->inTransit) ? f->toPlanetId : -1;
}

double SimState::forceTransitProgress(int forceId) const {
    const TaskForce* f = taskForce(forceId);
    if (!f || !f->inTransit || f->travelSeconds <= 0) return 0.0;
    return std::min(1.0, f->elapsedSeconds / f->travelSeconds);
}

// --- formations -----------------------------------------------------------

bool SimState::setFormation(int leaderId, const std::vector<int>& memberIds) {
    if (!object(leaderId)) return false;
    auto it = formationLeaders_.find(leaderId);
    Formation* f = nullptr;
    if (it != formationLeaders_.end()) {
        f = &formations_[it->second];
        f->members.clear();
    } else {
        Formation nf;
        nf.leaderId = leaderId;
        formationLeaders_[leaderId] = static_cast<int>(formations_.size());
        formations_.push_back(std::move(nf));
        f = &formations_.back();
    }
    for (int id : memberIds) {
        if (id == leaderId) continue;
        if (object(id) && std::find(f->members.begin(), f->members.end(), id) ==
                              f->members.end()) {
            f->members.push_back(id);
        }
    }
    return true;
}

const Formation* SimState::formation(int leaderId) const {
    auto it = formationLeaders_.find(leaderId);
    if (it == formationLeaders_.end()) return nullptr;
    return &formations_[it->second];
}

void SimState::removeFormation(int leaderId) {
    auto it = formationLeaders_.find(leaderId);
    if (it == formationLeaders_.end()) return;
    // Swap-remove keeps the deque dense; fix up the moved element's index.
    size_t idx = static_cast<size_t>(it->second);
    formations_[idx] = std::move(formations_.back());
    formations_.pop_back();
    if (idx < formations_.size()) {
        formationLeaders_[formations_[idx].leaderId] = static_cast<int>(idx);
    }
    formationLeaders_.erase(leaderId);
}

} // namespace eaw
