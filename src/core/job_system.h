// Job system — portable task-stealing thread pool for the engine.
//
// Design (grounded in docs/research/06-threading-design.md):
//  - Fixed worker pool sized to hardware (minus 1, main thread participates)
//  - Work-stealing: workers pull tasks from a shared queue and steal from
//    each other for load balance
//  - parallel_for partitions [0, n) into contiguous ranges (data-oriented
//    partitioning per the design doc)
//  - parallel_invoke fans out N functions
//  - Deterministic by construction: per-range work writes to disjoint slots
//
// Thread-safe: RunParallel* may be called from any worker (nested parallel
// work is supported).
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace eaw {

class JobSystem {
public:
    // Creates a pool with (hardware_concurrency - 1) workers (min 1).
    // If threads==0, hardware_concurrency is used. The calling thread is not
    // part of the pool; it participates in parallel_for/parallel_invoke.
    explicit JobSystem(unsigned threads = 0);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    unsigned workerCount() const { return workers_.size(); }

    // Invokes fn(start, end) for contiguous ranges partitioning [0, count).
    // Returns when all ranges complete on this thread's call. Zero task
    // allocation on the hot path: workers pull ranges from a shared atomic
    // index.
    void parallel_for(int64_t count, const std::function<void(int64_t, int64_t)>& fn);

    // Runs each fn[i] (on worker threads, this thread runs one too).
    void parallel_invoke(std::vector<std::function<void()>> fns);

    // Runs fn on the calling thread (helper for serial fallback).
    static void run_serial(int64_t count,
                           const std::function<void(int64_t, int64_t)>& fn);

    // Number of range partitions used by parallel_for (workers + caller).
    unsigned partsFor(int64_t count) const;

private:
    struct Task {
        std::function<void()> fn;
        std::atomic<int> remaining{0}; // for invoke fan-out; 0 = plain task
        std::atomic<int>* doneCounter = nullptr;
    };

    struct Worker {
        unsigned id = 0;
        JobSystem* owner = nullptr;
        std::thread thread;
        std::atomic<bool> stop{false};
        // Deque of tasks owned by this worker (work stealing).
        std::deque<Task*> deque;
        std::mutex mtx;
    };

    void workerLoop(Worker& w);
    bool tryRunLocal(Worker& w);
    bool trySteal(Worker& w);
    // Pulls one range from the in-flight parallel_for (cooperative stealing).
    // Returns true if a range was run.
    bool pullRange(Worker& w);
    void pushTask(Task* t);
    bool runOne(Worker* self); // runs one task (from this worker or a steal)
    void waitAll(std::atomic<int>& counter);

    // Cooperative range-stealing state for a parallel_for in flight.
    struct RangeWork {
        std::function<void(int64_t, int64_t)> fn;
        std::atomic<int64_t> next{0};
        int64_t count = 0;
        unsigned parts = 1;
        std::atomic<int> active{0};
    };
    std::unique_ptr<RangeWork> rangeWork_; // one in flight (caller-serialized)
    std::mutex rangeMtx_;
    std::condition_variable rangeCv_;

    std::vector<std::unique_ptr<Worker>> workers_;
    std::atomic<bool> shutdown_{false};
    unsigned poolSize_ = 0;
};

} // namespace eaw
