// Parallel tick dispatch — the Tier 3 hook's payload.
//
// The real game's sim tick (FUN_14025ca30) iterates its object lists and
// calls Update(dt) on every live object (doc 04). This module gives the
// proxy a parallel version of that loop: the game registers its live object
// lists once (via a scanner that verifies the layout), and ParallelTick
// partitions the total object count across the engine's job-system workers.
// Each object is updated by exactly one worker (deterministic per object);
// the game's own serial tick still runs, so this is additive work — it
// exercises the engine's parallel loop on the real game's object set.
#pragma once

#include <cstdint>

namespace eaw {

// An object as the game sees it: vtable pointer followed by object state.
// Update(dt) lives at vtable+0x50 (the slot doc 04 maps for the sim tick's
// per-object call).
struct GameObj {
    void* vtable; // slot 0
};
using UpdateFn = void (*)(void* obj, float dt);

// One of the game's object lists (the sim tick iterates 6 of these).
struct ObjectList {
    GameObj** items = nullptr;
    int count = 0;
};

// The scanner's view of a candidate list. Filled in by the caller (the
// proxy, from its Ghidra-derived scan of the tick) then verified by
// RegisterObjectLists.
struct ObjectListCandidate {
    GameObj** items = nullptr;
    int count = 0;
    int tag = 0; // caller-side id (e.g. list index)
};

// Registers the game's live object lists. `candidates` may be empty (the
// caller found nothing); RegisterObjectLists validates each list's items
// pointer and count, keeps the valid ones, and returns how many were kept.
int RegisterObjectLists(const ObjectListCandidate* candidates, int n);

// Returns the number of registered lists (0 = none).
int RegisteredObjectListCount();

// Attaches the engine's job-system pool to the tick dispatcher. Called once
// by the proxy during init; passing null keeps ParallelTick serial.
void AttachJobSystem(class JobSystem* jobs);

// Runs the parallel tick over the registered lists. `dt` is the frame's
// delta. Serial fallback when the lists aren't registered or the pool has a
// single worker. Never throws; safe to call from the hook on every frame.
void ParallelTick(float dt);

// Fills `out` with the number of objects updated in the most recent
// ParallelTick call (diagnostics for the hook's telemetry).
int64_t LastParallelObjectCount();

} // namespace eaw
