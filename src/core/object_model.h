// Sim object model — the data the Lua bindings query and mutate.
//
// Mirrors the documented engine surface (Alamo Engine Tools): GameObjects
// (units/structures/projectiles) with type/player/hull/shield/position,
// Players with faction/difficulty, and ObjectTypes with build cost/tech level.
// A SimState owns the registries; the Lua bindings read from it via wrapper
// userdata (GameObjectWrapper etc.).
//
// Thread safety: the sim runs on the sim thread; the object DB is not
// shared across threads. Parallel subsystems write per-object slots owned
// by their worker (design doc 06).
#pragma once

#include <cmath>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eaw {

struct Vec3 {
    double x = 0, y = 0, z = 0;

    double distanceTo(const Vec3& o) const {
        double dx = x - o.x, dy = y - o.y, dz = z - o.z;
        return sqrt(dx * dx + dy * dy + dz * dz);
    }
};

// A unit/planet/projectile type (from XML; we model the queried subset).
struct ObjectType {
    std::string name;        // XML name
    bool hero = false;
    double buildCost = 0;
    int techLevel = 0;
    double maxRange = 0, minRange = 0;
    // Category membership (Is_Category); pipe-separated in XML like "Frigate | Capital".
    std::vector<std::string> categories;
    // Property flags (Has_Property): "Unit", "Structure", "Hero", ...
    std::vector<std::string> properties;
    // Faction names this type is affiliated with (Is_Affiliated_With).
    std::vector<std::string> affiliatedFactions;
    bool affectedByMissileShield = false;
    bool affectedByLaserDefense = false;
};

// A player (faction) in the sim.
struct Player {
    int id = 0;
    std::string name;         // display name
    std::string factionName;  // XML faction name
    std::string difficulty = "Normal";
    bool human = false;
    int techLevel = 0;
    double credits = 0;
};

// A live game object (unit, structure, planet, projectile).
struct GameObject {
    int id = 0;
    std::string typeName;      // -> ObjectType lookup
    int playerId = 0;
    Vec3 position;
    double hull = 1.0;         // normalized 0..1 (Get_Hull/Get_Health)
    double shield = 1.0;       // normalized 0..1
    double energy = 1.0;       // normalized 0..1
    bool alive = true;
    bool hidden = false;
    bool selectable = true;
    bool invulnerable = false;
    bool inGarrison = false;
    bool ordersLocked = false;
    int attackTargetId = 0;
    std::vector<int> garrisonedUnits; // ids of units inside
};

class SimState {
public:
    // --- players ---
    Player& addPlayer(const std::string& name, const std::string& faction);
    const Player* player(int id) const;
    const Player* findPlayer(const std::string& name) const; // by display or faction name

    // --- types ---
    ObjectType& addType(ObjectType t);
    const ObjectType* type(const std::string& name) const;

    // --- objects ---
    GameObject& addObject(const std::string& typeName, int playerId, const Vec3& pos);
    const GameObject* object(int id) const;
    GameObject* object(int id);
    void removeObject(int id);

    // Spawns a unit of `typeName` for `playerId` near `pos` (respecting a
    // trivial collision check: if an object occupies the exact spot, offset).
    // Returns the new object's id.
    int spawnUnit(const std::string& typeName, int playerId, const Vec3& pos);

    // Garrison `unitId` inside `containerId` (both must exist and be alive).
    // Returns false if either is invalid.
    bool garrisonUnit(int unitId, int containerId);

    // Removes `unitId` from its container's garrison list.
    void ungarrisonUnit(int unitId);

    // Queries used by the Lua bindings.
    std::vector<const GameObject*> allObjects() const;
    std::vector<const GameObject*> objectsOfType(const std::string& typeName) const;
    std::vector<const GameObject*> objectsOfPlayer(int playerId) const;
    // Finds objects whose type has `category` in its categories.
    std::vector<const GameObject*> objectsOfCategory(const std::string& category) const;
    // Nearest object to `pos` whose type name matches `typeName` (nil if none).
    const GameObject* nearestObject(const Vec3& pos, const std::string& typeName) const;

    // Registered type names (for seeding the Lua type-wrapper tables).
    std::vector<std::string> typeNames() const;

    int nextObjectId() const { return nextObjectId_; }

private:
    // deque: element references stay valid across additions (the Lua bindings
    // hold Player* via id lookups; the fixture mutates via the returned ref).
    std::deque<Player> players_;
    std::unordered_map<std::string, ObjectType> types_;
    std::unordered_map<int, GameObject> objects_;
    int nextObjectId_ = 1;
    int nextPlayerId_ = 1;
};

} // namespace eaw
