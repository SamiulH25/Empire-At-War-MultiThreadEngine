#include "core/job_system.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace eaw {

// (partitionRanges removed: parallel_for now uses cooperative range stealing
// via a shared atomic index — no per-range task allocation.)

JobSystem::JobSystem(unsigned threads) {
    unsigned hw = std::thread::hardware_concurrency();
    poolSize_ = (threads == 0) ? (hw > 1 ? hw - 1 : 1) : threads;
    for (unsigned i = 0; i < poolSize_; ++i) {
        auto w = std::make_unique<Worker>();
        w->id = i;
        w->owner = this;
        workers_.push_back(std::move(w));
    }
    for (auto& w : workers_) {
        w->thread = std::thread([this, &w = *w] { workerLoop(w); });
    }
}

JobSystem::~JobSystem() {
    shutdown_.store(true);
    for (auto& w : workers_) w->stop.store(true);
    for (auto& w : workers_) {
        if (w->thread.joinable()) w->thread.join();
    }
}

void JobSystem::pushTask(Task* t) {
    // Push to the least-loaded worker (round-robin over a shared atomic).
    static std::atomic<unsigned> rr{0};
    unsigned idx = rr.fetch_add(1, std::memory_order_relaxed) % poolSize_;
    auto& w = *workers_[idx];
    {
        std::lock_guard<std::mutex> lk(w.mtx);
        w.deque.push_back(t);
    }
    // No CV needed: workers spin-sleep briefly; but wake via a flag would be
    // nicer. We use a short sleep in workerLoop when idle.
}

bool JobSystem::tryRunLocal(Worker& w) {
    Task* t = nullptr;
    {
        std::lock_guard<std::mutex> lk(w.mtx);
        if (!w.deque.empty()) {
            t = w.deque.back(); // LIFO for cache locality
            w.deque.pop_back();
        }
    }
    if (!t) return false;
    t->fn();
    if (t->doneCounter) {
        if (t->doneCounter->fetch_sub(1, std::memory_order_acq_rel) == 1) {
            // last one: notify waiters (handled via counter polling in waitAll)
        }
    }
    delete t;
    return true;
}

bool JobSystem::trySteal(Worker& w) {
    // Steal from a random other worker (front of their deque).
    unsigned n = poolSize_;
    for (unsigned i = 1; i < n; ++i) {
        unsigned victim = (w.id + i) % n;
        auto& v = *workers_[victim];
        Task* t = nullptr;
        {
            std::lock_guard<std::mutex> lk(v.mtx);
            if (!v.deque.empty()) {
                t = v.deque.front(); // FIFO steal (oldest task)
                v.deque.pop_front();
            }
        }
        if (t) {
            t->fn();
            if (t->doneCounter) {
                t->doneCounter->fetch_sub(1, std::memory_order_acq_rel);
            }
            delete t;
            return true;
        }
    }
    return false;
}

bool JobSystem::runOne(Worker* self) {
    if (self) {
        if (tryRunLocal(*self)) return true;
        if (trySteal(*self)) return true;
        return false;
    }
    // Called from the owner (main) thread: try to steal from any worker.
    for (auto& w : workers_) {
        Task* t = nullptr;
        {
            std::lock_guard<std::mutex> lk(w->mtx);
            if (!w->deque.empty()) {
                t = w->deque.front();
                w->deque.pop_front();
            }
        }
        if (t) {
            t->fn();
            if (t->doneCounter) t->doneCounter->fetch_sub(1, std::memory_order_acq_rel);
            delete t;
            return true;
        }
    }
    return false;
}

void JobSystem::waitAll(std::atomic<int>& counter) {
    // Spin with short yields until the counter hits 0 (work-stealing makes
    // this fast in practice; we also run tasks while waiting).
    while (counter.load(std::memory_order_acquire) > 0) {
        if (runOne(nullptr)) continue;
        std::this_thread::yield();
    }
}

void JobSystem::workerLoop(Worker& w) {
    while (!w.stop.load(std::memory_order_acquire)) {
        if (tryRunLocal(w)) continue;
        if (trySteal(w)) continue;
        // Help any in-flight parallel_for (cooperative range stealing).
        if (pullRange(w)) continue;
        if (shutdown_.load(std::memory_order_acquire)) break;
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
}

unsigned JobSystem::partsFor(int64_t count) const {
    unsigned parts = poolSize_ + 1;
    if (static_cast<int64_t>(parts) > count) parts = static_cast<unsigned>(count);
    return parts;
}

void JobSystem::parallel_for(int64_t count,
                             const std::function<void(int64_t, int64_t)>& fn) {
    if (count <= 0) return;
    unsigned parts = partsFor(count);
    if (parts <= 1 || count <= 8) {
        // Tiny workload: serial is cheaper than coordination.
        fn(0, count);
        return;
    }
    // Ranges of roughly equal size.
    int64_t chunk = count / parts;
    int64_t rem = count % parts;
    auto rangeOf = [&](int64_t r) -> std::pair<int64_t, int64_t> {
        int64_t start = r * chunk + std::min<int64_t>(r, rem);
        int64_t len = chunk + (r < rem ? 1 : 0);
        return {start, start + len};
    };

    std::unique_ptr<RangeWork> work = std::make_unique<RangeWork>();
    work->fn = fn;
    work->count = count;
    work->parts = parts;
    work->next.store(0, std::memory_order_relaxed);
    work->active.store(static_cast<int>(parts), std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lk(rangeMtx_);
        rangeWork_ = std::move(work);
    }
    rangeCv_.notify_all();

    // The caller participates: pull ranges until none remain or the work is
    // gone (a worker may have finished the last range and reset it).
    for (;;) {
        RangeWork* w;
        int64_t r;
        {
            std::lock_guard<std::mutex> lk(rangeMtx_);
            w = rangeWork_.get();
            if (!w) break; // workers finished everything
            r = w->next.fetch_add(1, std::memory_order_relaxed);
            if (r >= w->parts) break; // all claimed; wait for stragglers
        }
        auto [a, b] = rangeOf(r);
        w->fn(a, b);
        if (w->active.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            std::lock_guard<std::mutex> lk(rangeMtx_);
            rangeWork_.reset();
            rangeCv_.notify_all();
            break;
        }
    }

    // Wait for workers to finish their ranges (if the caller ran out first).
    {
        std::unique_lock<std::mutex> lk(rangeMtx_);
        rangeCv_.wait(lk, [&] { return rangeWork_ == nullptr; });
    }
}

bool JobSystem::pullRange(Worker&) {
    RangeWork* w = nullptr;
    int64_t r = 0;
    {
        std::lock_guard<std::mutex> lk(rangeMtx_);
        w = rangeWork_.get();
        if (!w) return false;
        // Claim the range under the lock: once claimed, this range's
        // decrement keeps `active` > 0, so `w` cannot be freed until we
        // run fn and decrement (the reset only happens at active == 0).
        r = w->next.fetch_add(1, std::memory_order_relaxed);
        if (r >= w->parts) return false; // no ranges left
    }
    // Compute the range (same math as the caller).
    int64_t chunk = w->count / w->parts;
    int64_t rem = w->count % w->parts;
    int64_t start = r * chunk + std::min<int64_t>(r, rem);
    int64_t len = chunk + (r < rem ? 1 : 0);
    w->fn(start, start + len);
    if (w->active.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        std::lock_guard<std::mutex> lk(rangeMtx_);
        rangeWork_.reset();
        rangeCv_.notify_all();
    }
    return true;
}

void JobSystem::parallel_invoke(std::vector<std::function<void()>> fns) {
    if (fns.empty()) return;
    if (fns.size() == 1) {
        fns[0]();
        return;
    }
    std::atomic<int> remaining{static_cast<int>(fns.size())};
    for (size_t i = 0; i + 1 < fns.size(); ++i) {
        auto* t = new Task;
        t->doneCounter = &remaining;
        t->fn = std::move(fns[i]);
        pushTask(t);
    }
    fns.back()();
    remaining.fetch_sub(1, std::memory_order_acq_rel);
    waitAll(remaining);
}

void JobSystem::run_serial(int64_t count,
                           const std::function<void(int64_t, int64_t)>& fn) {
    if (count > 0) fn(0, count);
}

} // namespace eaw
