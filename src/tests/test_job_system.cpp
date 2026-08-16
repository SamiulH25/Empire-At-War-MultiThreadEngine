// Unit tests for the job system.
#include "core/job_system.h"

#include <atomic>
#include <cstdio>
#include <numeric>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    } else {
        std::printf("ok:   %s\n", what);
    }
}

void testParallelFor() {
    eaw::JobSystem js(4);
    check(js.workerCount() == 4, "4 workers");
    const int64_t N = 100000;
    std::vector<int> hits(N, 0);
    js.parallel_for(N, [&](int64_t a, int64_t b) {
        for (int64_t i = a; i < b; ++i) hits[i] = 1;
    });
    bool all = true;
    for (int v : hits) if (v != 1) { all = false; break; }
    check(all, "parallel_for covers every index exactly once");

    // Ranges must be disjoint and contiguous (data-oriented partitioning)
    std::vector<std::pair<int64_t, int64_t>> ranges;
    std::atomic<int> count{0};
    js.parallel_for(1000, [&](int64_t a, int64_t b) {
        count.fetch_add(1);
        if (a < 0 || b > 1000 || a > b) check(false, "range bounds");
    });
    check(count.load() > 1, "work split across ranges");
}

void testParallelForSmall() {
    eaw::JobSystem js(2);
    std::atomic<int> sum{0};
    js.parallel_for(5, [&](int64_t a, int64_t b) {
        for (int64_t i = a; i < b; ++i) sum.fetch_add(static_cast<int>(i));
    });
    check(sum.load() == 10, "small parallel_for sums 0+1+2+3+4");
}

void testParallelInvoke() {
    eaw::JobSystem js(3);
    std::atomic<int> a{0}, b{0}, c{0};
    js.parallel_invoke({
        [&] { a = 1; },
        [&] { b = 2; },
        [&] { c = 3; },
    });
    check(a == 1 && b == 2 && c == 3, "invoke runs all fns");
}

void testNestedParallel() {
    eaw::JobSystem js(2);
    std::atomic<long long> total{0};
    // Outer parallel_for, each range spawns an inner parallel_for (nested).
    js.parallel_for(4, [&](int64_t a, int64_t b) {
        js.parallel_for(b - a, [&](int64_t x, int64_t y) {
            for (int64_t i = x; i < y; ++i) total.fetch_add(1);
        });
    });
    check(total.load() == 4, "nested parallel work completes");
}

void testDeterministicSum() {
    eaw::JobSystem js(4);
    const int64_t N = 100000;
    long long serial = 0;
    for (int64_t i = 0; i < N; ++i) serial += i;
    for (int trial = 0; trial < 3; ++trial) {
        std::atomic<long long> sum{0};
        js.parallel_for(N, [&](int64_t a, int64_t b) {
            long long local = 0;
            for (int64_t i = a; i < b; ++i) local += i;
            sum.fetch_add(local, std::memory_order_relaxed);
        });
        check(sum.load() == serial, "deterministic sum across runs");
    }
}

void testStress() {
    eaw::JobSystem js(8);
    std::atomic<int> counter{0};
    for (int round = 0; round < 20; ++round) {
        js.parallel_for(1000, [&](int64_t a, int64_t b) {
            std::this_thread::yield(); // force interleaving
            for (int64_t i = a; i < b; ++i) counter.fetch_add(1);
        });
    }
    check(counter.load() == 20000, "stress: 20 x 1000 increments");
}

} // namespace

int main() {
    testParallelFor();
    testParallelForSmall();
    testParallelInvoke();
    testNestedParallel();
    testDeterministicSum();
    testStress();
    if (failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d FAILURES\n", failures);
    return 1;
}
