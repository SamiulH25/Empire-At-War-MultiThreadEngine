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
    // Raw game stats (from the unit XML): hull/shield in game points, damage
    // per shot in the same scale, attack rate in shots/sec, move speed.
    double hullPoints = 1.0;
    double shieldPoints = 0.0;
    double moveSpeed = 50.0;
    // Normalized shield fraction (0..1) of a full hull bar at spawn.
    double shieldFraction = 0.0;
    // Combat stats (modeled from the XML combat surface: weapon damage is
    // per-shot, attack rate is shots per second).
    double damage = 0.0;         // per-shot damage (fraction of target hull)
    double attackRate = 1.0;     // shots per second
    double shieldDamageMultiplier = 1.0; // damage vs shields
    double hullDamageMultiplier = 1.0;   // damage vs hull
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
    // Diplomatic relations (Make_Ally / Make_Enemy). Defaults:
    //  - a player is an ally of itself
    //  - all other players are enemies until allied
    std::vector<int> allies;
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
    bool hasMoveTarget = false;
    Vec3 moveTarget;               // set by Move_To; sim integrates toward it
    double moveSpeed = 50.0;       // units per second
    // Path-following state (filled by the pathfinding step).
    std::vector<Vec3> path;        // remaining waypoints (world coords)
    size_t pathIndex = 0;
    int pathSearchId = 0;          // active pathfinding search (0 = none)
    // Combat state.
    double attackCooldown = 0.0;   // seconds until the next shot is allowed
    double pendingDamage = 0.0;    // damage accumulated this tick (apply pass)
    bool wasDamagedThisTick = false; // for the attacked-event edge detection
    std::vector<int> garrisonedUnits; // ids of units inside
};

// A taskforce — the AI's unit group (PGTASKFORCE model).
//
// A named group of units owned by one player, with a current stage, goal
// type, and plan result. Units can be added/removed; the force follows
// collective orders (attack/move/garrison) that fan out to the units.
struct TaskForce {
    int id = 0;
    int playerId = 0;
    std::string name;          // goal-type name (e.g. "AttackPlan")
    int stage = 0;
    bool planResult = false;   // Set_Plan_Result
    bool goalSystemRemovable = true;
    std::vector<int> units;    // object ids in the force
};

class SimState {
public:
    // --- players ---
    Player& addPlayer(const std::string& name, const std::string& faction);
    const Player* player(int id) const;
    Player* player(int id);
    const Player* findPlayer(const std::string& name) const; // by display or faction name
    // All players (for iteration; references valid — deque storage).
    const std::deque<Player>& allPlayers() const { return players_; }

    // --- diplomacy ---
    // Makes `a` and `b` allies (symmetric; each stays allied to itself).
    void makeAlly(int a, int b);
    // Breaks the alliance between `a` and `b` (they become enemies).
    void makeEnemy(int a, int b);
    // True if `a` considers `b` an ally (including self).
    bool isAlly(int a, int b) const;
    // True if `a` considers `b` an enemy (any non-ally other than self).
    bool isEnemy(int a, int b) const;

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

    // --- taskforces ---
    // Creates a taskforce for `playerId` with the given goal-type name.
    int addTaskForce(int playerId, const std::string& name);
    const TaskForce* taskForce(int id) const;
    TaskForce* taskForce(int id);
    // Adds a unit to the force (idempotent). Returns false if either is
    // invalid.
    bool addUnitToForce(int forceId, int unitId);
    void removeUnitFromForce(int forceId, int unitId);
    // Removes dead units from all forces (called by the sim each tick).
    void pruneDeadUnits();
    // Sum of the force's units' hulls (a simple threat measure).
    double forceThreat(int forceId) const;
    // All taskforces owned by a player.
    std::vector<const TaskForce*> forcesOfPlayer(int playerId) const;

    int nextObjectId() const { return nextObjectId_; }

private:
    // deque: element references stay valid across additions (the Lua bindings
    // hold Player* via id lookups; the fixture mutates via the returned ref).
    std::deque<Player> players_;
    std::deque<TaskForce> forces_;
    std::unordered_map<std::string, ObjectType> types_;
    std::unordered_map<int, GameObject> objects_;
    int nextObjectId_ = 1;
    int nextPlayerId_ = 1;
    int nextForceId_ = 1;
};

} // namespace eaw
