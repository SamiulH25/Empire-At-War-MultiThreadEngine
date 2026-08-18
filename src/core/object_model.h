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
    // Abilities (the game's <Unit_Abilities_Data> surface).
    struct Ability {
        std::string name;
        double cooldown = 0.0;    // seconds between uses
        double range = 0.0;       // max range to target (0 = self/untargeted)
        double damage = 0.0;      // effect damage (fraction of target hull)
        bool requiresTarget = false;
    };
    std::vector<Ability> abilities;
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
    // Economy: income per second (tribute/planet output), and the set of
    // explicitly locked type names (everything else is buildable, matching
    // the game's tech model where units default to unlocked).
    double incomePerSecond = 10.0;
    std::vector<std::string> lockedTypes;
    // Fog of war: seconds since each object was last seen (object id ->
    // game time of last sighting; -1 = never seen). Filled by the sim's fog
    // step; read by perception (TimeLastSeen).
    std::unordered_map<int, double> lastSeen;
    // Revealed positions (permanent reveal from FogOfWar.Reveal): a coarse
    // grid of revealed cells (cell key -> game time revealed).
    std::unordered_map<int64_t, double> revealedCells;
    // True after FogOfWar.Reveal_All: everything is always visible.
    bool revealAll = false;
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
    // Targeting priority tables (Set_Targeting_Priorities /
    // Set_Land_AI_Targeting_Priorities): ordered list of category names the
    // unit prefers to engage. Empty = default (attack nearest).
    std::vector<std::string> targetingPriorities;
    std::vector<std::string> landTargetingPriorities;
    // Path-following state (filled by the pathfinding step).
    std::vector<Vec3> path;        // remaining waypoints (world coords)
    size_t pathIndex = 0;
    int pathSearchId = 0;          // active pathfinding search (0 = none)
    // Combat state.
    double attackCooldown = 0.0;   // seconds until the next shot is allowed
    double pendingDamage = 0.0;    // damage accumulated this tick (apply pass)
    bool wasDamagedThisTick = false; // for the attacked-event edge detection
    // Ability runtime state (parallel to ObjectType::abilities by name).
    struct AbilityState {
        double cooldownRemaining = 0.0; // seconds until ready
        bool active = false;            // sustained abilities
    };
    std::vector<std::pair<std::string, AbilityState>> abilityStates;
    std::vector<int> garrisonedUnits; // ids of units inside
    // Hero / unique state (Set_Hero / Is_Unique / Get_Unique_ID). The game
    // marks specific unit instances as heroes and gives them a unique id
    // (the type's hero flag is the static default; these are per-instance).
    bool hero = false;         // Set_Hero(true/false)
    bool unique = false;       // Is_Unique (heroes are unique)
    int uniqueId = 0;          // Get_Unique_ID (0 = none)
    // Free store: units parked in the force's free store (the game's
    // Get_Free_Store / Get_Units_In_Free_Store surface). A unit is in the
    // free store when it has no orders and is not part of a taskforce.
    bool inFreeStore = false;
    int freeStoreForceId = 0;  // the force whose free store holds this unit
};

// A formation: a leader object plus member ids that follow it (the game's
// Set_Formation / Formation_Attack / Formation_Move surface).
struct Formation {
    int leaderId = 0;
    std::vector<int> members;  // object ids (the leader is not a member)
};

// A planet in galactic mode.
struct Planet {
    int id = 0;
    std::string name;          // e.g. "Tatooine"
    std::string factionName;   // owner faction ("" = neutral)
    Vec3 position;
    double garrisonHull = 0;   // total hull of orbiting forces (simple model)
};

// A taskforce — the AI's unit group (PGTASKFORCE model).
// collective orders (attack/move/garrison) that fan out to the units.
struct TaskForce {
    int id = 0;
    int playerId = 0;
    std::string name;          // goal-type name (e.g. "AttackPlan")
    int stage = 0;
    bool planResult = false;   // Set_Plan_Result
    bool goalSystemRemovable = true;
    int planetId = -1;         // galactic mode: planet the force is at (-1 none)
    std::vector<int> units;    // object ids in the force
    // Hyperspace transit (galactic mode).
    bool inTransit = false;
    int fromPlanetId = -1;
    int toPlanetId = -1;
    double travelSeconds = 0;  // total trip duration
    double elapsedSeconds = 0; // time since departure
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

    // --- economy ---
    // Adds credits to a player (negative = deduct; result may go below 0).
    void giveMoney(int playerId, double amount);
    // Sets the player's tech level (0..N).
    void setTechLevel(int playerId, int level);
    // Locks/unlocks a type name for a player (everything defaults to
    // unlocked; locked types are blocked until unlocked again).
    void lockType(int playerId, const std::string& typeName);
    void unlockType(int playerId, const std::string& typeName);
    // True if the player may build the type (not locked and tech sufficient).
    bool canBuild(int playerId, const std::string& typeName) const;
    // True if the player has enough credits to build the type.
    bool canAfford(int playerId, const std::string& typeName) const;
    // Spends the build cost and spawns the unit at `pos` for the player.
    // Returns the new object id (0 if not affordable/buildable).
    int buildUnit(int playerId, const std::string& typeName, const Vec3& pos);

    // --- abilities ---
    // The ability definition on an object's type (null if the type has no
    // such ability).
    const ObjectType::Ability* abilityDef(int objectId,
                                          const std::string& name) const;
    // True if the object's type has the ability.
    bool hasAbility(int objectId, const std::string& name) const;
    // True if the ability's cooldown has elapsed.
    bool isAbilityReady(int objectId, const std::string& name) const;
    // True if the ability is currently active (sustained abilities).
    bool isAbilityActive(int objectId, const std::string& name) const;
    // Seconds until the ability is ready again (0 if ready).
    double abilityCooldownLeft(int objectId, const std::string& name) const;
    // Activates the ability on `targetId` (0 = untargeted/self). Applies
    // range + cooldown checks and the ability's damage. Returns true on
    // success.
    bool activateAbility(int objectId, const std::string& name, int targetId);
    // Cancels a sustained ability.
    void cancelAbility(int objectId, const std::string& name);
    // Finishes the ability cooldown (used by Force_Ability_Recharge).
    void resetAbilityCooldown(int objectId, const std::string& name);
    // Tick: reduce ability cooldowns. Called from the sim update.
    void tickAbilities(double dt);

    // --- fog of war ---
    // Reveals a permanent circular area for a player (coarse grid cells).
    void revealArea(int playerId, const Vec3& center, double radius);
    // Reveals everything for a player (all current + future objects seen).
    void revealAll(int playerId);
    // Updates visibility: objects within the reveal range of the player's
    // units (or inside revealed cells) get their lastSeen bumped to `now`.
    // Called each tick.
    void updateVisibility(int playerId, double now);
    // Seconds since the player last saw the object (perception TimeLastSeen).
    // Returns 0 if currently visible, a growing value otherwise.
    double timeSinceSeen(int playerId, int objectId, double now) const;

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
    // All taskforces (for iteration; references valid — deque storage).
    const std::deque<TaskForce>& forces() const { return forces_; }
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

    // --- planets (galactic mode) ---
    int addPlanet(const std::string& name, const std::string& factionName,
                  const Vec3& pos);
    const Planet* planet(int id) const;
    Planet* planet(int id);
    const Planet* findPlanet(const std::string& name) const;
    Planet* findPlanet(const std::string& name);
    std::vector<const Planet*> allPlanets() const;
    // The planet a taskforce is currently assigned to (by name lookup of the
    // force's planetId; -1 if none).
    int forcePlanet(int forceId) const;
    // Starts hyperspace transit for a force to `toPlanetId`. Travel time is
    // derived from the planet distance (a fixed hyperspace speed). Returns
    // false if either planet is unknown or the force is already in transit.
    bool startTransit(int forceId, int toPlanetId);
    // True if the force is currently in hyperspace.
    bool forceInTransit(int forceId) const;
    // The destination planet of a force in transit (-1 if not in transit).
    int forceTransitTarget(int forceId) const;
    // Fraction of the trip completed (0..1); 0 if not in transit.
    double forceTransitProgress(int forceId) const;

    // --- formations ---
    // Sets (or replaces) the formation led by `leaderId` with `memberIds`
    // (the leader's id is never added). Returns false if the leader is
    // unknown.
    bool setFormation(int leaderId, const std::vector<int>& memberIds);
    // The formation led by `leaderId` (null if none).
    const Formation* formation(int leaderId) const;
    // All formations (for the per-tick follow step).
    const std::deque<Formation>& formations() const { return formations_; }
    // Removes the formation led by `leaderId` (e.g. leader death).
    void removeFormation(int leaderId);

    int nextObjectId() const { return nextObjectId_; }

private:
    // deque: element references stay valid across additions (the Lua bindings
    // hold Player* via id lookups; the fixture mutates via the returned ref).
    std::deque<Player> players_;
    std::deque<TaskForce> forces_;
    std::deque<Planet> planets_;
    std::deque<Formation> formations_;
    std::unordered_map<int, int> formationLeaders_; // leaderId -> formations_ index
    std::unordered_map<std::string, ObjectType> types_;
    std::unordered_map<int, GameObject> objects_;
    int nextObjectId_ = 1;
    int nextPlayerId_ = 1;
    int nextForceId_ = 1;
    int nextPlanetId_ = 1;
};

} // namespace eaw
