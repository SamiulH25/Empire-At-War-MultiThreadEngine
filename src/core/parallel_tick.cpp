#include "core/parallel_tick.h"

#include "core/job_system.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

namespace eaw {
namespace {

std::mutex g_mtx;
std::vector<ObjectList> g_lists;
bool g_serial = true; // until a worker pool is attached

// Per-object update via the game's vtable+0x50 slot. Mirrors what the sim
// tick does per object (doc 04: "calls Update(dt) on each object in the
// list"). The vtable pointer is read once per object; a null vtable or a
// vtable without a mapped Update slot leaves the object untouched.
void UpdateObject(GameObj* o, float dt) {
    if (!o) return;
    void* vt = o->vtable;
    if (!vt) return;
    UpdateFn fn;
    std::memcpy(&fn, static_cast<const char*>(vt) + 0x50, sizeof(fn));
    if (fn) fn(o, dt);
}

struct TickState {
    std::vector<GameObj*> all; // flattened object pointers (snapshot)
    int64_t updated = 0;
    float dt = 0.0f;
};

// Runs on the calling thread; the workers pull ranges and call this.
void TickRange(const TickState* st, int64_t start, int64_t end) {
    for (int64_t i = start; i < end; ++i) {
        UpdateObject(st->all[static_cast<size_t>(i)], st->dt);
    }
}

JobSystem* g_jobs = nullptr; // attached by the proxy (one global pool)
std::atomic<int64_t> g_lastCount{0};

} // namespace

int RegisterObjectLists(const ObjectListCandidate* candidates, int n) {
    std::lock_guard<std::mutex> lock(g_mtx);
    g_lists.clear();
    int kept = 0;
    if (!candidates) return 0;
    for (int i = 0; i < n; ++i) {
        const ObjectListCandidate& c = candidates[i];
        if (!c.items || c.count <= 0 || c.count > 1 << 22) continue;
        ObjectList l;
        l.items = c.items;
        l.count = c.count;
        g_lists.push_back(l);
        ++kept;
    }
    return kept;
}

int RegisteredObjectListCount() {
    std::lock_guard<std::mutex> lock(g_mtx);
    return static_cast<int>(g_lists.size());
}

// The proxy calls this once during init to attach the engine's pool.
void AttachJobSystem(JobSystem* jobs) {
    std::lock_guard<std::mutex> lock(g_mtx);
    g_jobs = jobs;
    g_serial = !jobs || jobs->workerCount() == 0;
}

void ParallelTick(float dt) {
    // Snapshot the registered lists once per frame (cheap: 6 pointers).
    std::vector<ObjectList> lists;
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        lists = g_lists;
    }
    if (lists.empty()) {
        g_lastCount = 0;
        return;
    }
    // Flatten into a contiguous snapshot so ranges are data-oriented and
    // each object is updated by exactly one worker (no shared writes).
    TickState st;
    for (const ObjectList& l : lists) {
        for (int i = 0; i < l.count; ++i) {
            GameObj* o = l.items[i];
            if (o) st.all.push_back(o);
        }
    }
    st.dt = dt;
    int64_t n = static_cast<int64_t>(st.all.size());
    if (n == 0) {
        g_lastCount = 0;
        return;
    }
    JobSystem* jobs;
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        jobs = g_jobs;
    }
    if (g_serial || !jobs) {
        TickRange(&st, 0, n);
    } else {
        jobs->parallel_for(n, [&st](int64_t a, int64_t b) {
            TickRange(&st, a, b);
        });
    }
    st.updated = n;
    g_lastCount = n;
}

int64_t LastParallelObjectCount() {
    return g_lastCount.load(std::memory_order_relaxed);
}

} // namespace eaw
