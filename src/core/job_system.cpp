#include "core/job_system.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace eaw {

namespace {

// Contiguous range partition: split [0, count) into ~n ranges of roughly
// equal size (data-oriented partitioning keeps caches warm).
void partitionRanges(int64_t count, unsigned parts,
                     std::vector<std::pair<int64_t, int64_t>>& out) {
    out.clear();
    if (count <= 0 || parts == 0) return;
    unsigned n = static_cast<unsigned>(std::min<int64_t>(parts, count));
    int64_t chunk = count / n;
    int64_t rem = count % n;
    int64_t start = 0;
    for (unsigned i = 0; i < n; ++i) {
        int64_t len = chunk + (static_cast<int64_t>(i) < rem ? 1 : 0);
        out.emplace_back(start, start + len);
        start += len;
    }
}

} // namespace

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
        if (shutdown_.load(std::memory_order_acquire)) break;
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

void JobSystem::parallel_for(int64_t count,
                             const std::function<void(int64_t, int64_t)>& fn) {
    if (count <= 0) return;
    unsigned parts = poolSize_ + 1; // workers + this thread
    std::vector<std::pair<int64_t, int64_t>> ranges;
    partitionRanges(count, parts, ranges);
    if (ranges.size() <= 1) {
        fn(0, count);
        return;
    }
    std::atomic<int> remaining{static_cast<int>(ranges.size())};
    // Launch all but one range as tasks; run the last on this thread.
    for (size_t i = 0; i + 1 < ranges.size(); ++i) {
        auto [a, b] = ranges[i];
        auto* t = new Task;
        t->doneCounter = &remaining;
        t->fn = [fn, a, b] { fn(a, b); };
        pushTask(t);
    }
    // This thread runs the final range (participates in the work).
    {
        auto [a, b] = ranges.back();
        fn(a, b);
        remaining.fetch_sub(1, std::memory_order_acq_rel);
    }
    waitAll(remaining);
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
