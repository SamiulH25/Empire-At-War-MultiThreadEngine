// Parallel tick tests: the proxy's payload module. Verifies that
// RegisterObjectLists validates, ParallelTick updates every registered
// object exactly once, and the serial fallback works without a pool.
#include "core/job_system.h"
#include "core/parallel_tick.h"

#include <cstdio>
#include <cstring>

namespace {
int failures = 0;

void check(bool cond, const char* msg) {
    if (!cond) {
        std::printf("FAIL: %s\n", msg);
        ++failures;
    } else {
        std::printf("ok: %s\n", msg);
    }
}

struct Obj {
    void* vtable;
    int hits = 0;
};

struct VTable {
    unsigned char pad[0x50];
    void (*update)(void*, float);
};

void Update(void* obj, float) {
    static_cast<Obj*>(obj)->hits++;
}

void testParallelTick() {
    VTable vt;
    vt.update = &Update;

    constexpr int N = 10000;
    static Obj objs[N];
    static Obj* list[N];
    for (int i = 0; i < N; ++i) {
        objs[i].vtable = &vt;
        objs[i].hits = 0;
        list[i] = &objs[i];
    }

    // Invalid candidates must be rejected.
    eaw::ObjectListCandidate bad{nullptr, 100, 0};
    check(eaw::RegisterObjectLists(&bad, 1) == 0, "null items rejected");
    auto asGame = reinterpret_cast<eaw::GameObj**>(list);
    eaw::ObjectListCandidate badCount{asGame, -5, 0};
    check(eaw::RegisterObjectLists(&badCount, 1) == 0, "negative count rejected");

    // Valid registration (Obj is layout-compatible with GameObj: vtable
    // pointer first).
    eaw::ObjectListCandidate good{asGame, N, 0};
    check(eaw::RegisterObjectLists(&good, 1) == 1, "valid list registered");
    check(eaw::RegisteredObjectListCount() == 1, "count reflects registration");

    // Serial fallback (no pool attached): still updates every object.
    eaw::AttachJobSystem(nullptr);
    eaw::ParallelTick(0.016f);
    check(eaw::LastParallelObjectCount() == N, "serial tick updated all objects");
    for (int i = 0; i < N; ++i) check(objs[i].hits == 1, "object updated once");

    // With a pool: same result, run via workers. Each object now has hits==1
    // from the serial run; the parallel run adds exactly one more.
    {
        eaw::JobSystem jobs(4);
        eaw::AttachJobSystem(&jobs);
        eaw::ParallelTick(0.016f);
        check(eaw::LastParallelObjectCount() == N, "parallel tick updated all objects");
        int exact = 0;
        for (int i = 0; i < N; ++i) if (objs[i].hits == 2) ++exact;
        check(exact == N, "parallel adds exactly one update to every object");
        eaw::AttachJobSystem(nullptr);
    }

    // Re-registering replaces the lists.
    check(eaw::RegisterObjectLists(nullptr, 0) == 0, "clear lists");
    check(eaw::RegisteredObjectListCount() == 0, "cleared");
    eaw::ParallelTick(0.016f);
    check(eaw::LastParallelObjectCount() == 0, "no lists -> no work");
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    testParallelTick();
    if (failures == 0) { std::printf("ALL TESTS PASSED\n"); return 0; }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
