#ifndef EMP_TEST_MODE_H__
#define EMP_TEST_MODE_H__

// Test-mode toggle for wire-byte determinism. When enabled, default-
// constructed randomness sources yield a deterministic byte stream so
// two runs of the same protocol produce byte-identical wire output.
//
// Toggle:
//   - $EMP_TEST_MODE=1 in the environment, OR
//   - emp::set_test_mode(true) at program start.
// First call to is_test_mode() caches the env-var read.
//
// Determinism model: a seed is the pair (lane, ordinal). The lane names
// the logical unit of work — the main thread is lane 0, and a spawned
// worker runs under a lane chosen by its CREATOR (whose program order is
// deterministic), never discovered by the worker itself. The ordinal
// counts seed draws within the lane and is thread-local. Both halves
// depend only on per-lane program order, never on cross-thread
// scheduling, so multi-threaded runs reproduce. ThreadPool::enqueue
// derives and installs a lane per task automatically; hand-spawned
// threads wrap their body in test_lane_scope. A second thread using lane
// 0 to draw a seed or derive a child lane aborts: two threads sharing a
// lane would replay identical "random" streams — silently wrong rather
// than merely nondeterministic.

#include "emp-tool/runtime/core/error.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <thread>

namespace emp {

namespace detail {

// Test mode deliberately replaces cryptographic randomness with predictable
// streams. Say so once, loudly, at activation time rather than adding logging
// or another check to every random draw.
inline void warn_insecure_test_mode_once() {
    static const bool warned = []() {
        std::fputs(
            "\n"
            "=======================================================================\n"
            "WARNING: EMP TEST MODE ENABLED - RANDOMNESS IS DETERMINISTIC AND INSECURE\n"
            "Default PRG seeds and EC scalars are predictable in this process.\n"
            "Use only for testing; never process real secrets in this process.\n"
            "=======================================================================\n",
            stderr);
        std::fflush(stderr);
        return true;
    }();
    (void)warned;
}

inline std::atomic<bool>& test_mode_flag() {
    static std::atomic<bool> flag(
        []() {
            const char* v = std::getenv("EMP_TEST_MODE");
            const bool enabled = v != nullptr && std::strcmp(v, "1") == 0;
            if (enabled) warn_insecure_test_mode_once();
            return enabled;
        }());
    return flag;
}

// Determinism epoch, bumped by reset_test_seed_counter(). Every
// thread's seed state records the epoch it was last rewound at and
// rewinds again when the epoch changes, so a reset reaches *every*
// randomness source -- long-lived thread_local PRGs included (e.g.
// ECGroup::rand_scalar's) -- keeping them in step across the reset.
inline std::atomic<uint64_t>& test_seed_epoch() {
    static std::atomic<uint64_t> ep(0);
    return ep;
}

// Per-thread seed state. `ctr` numbers seed draws within this lane;
// `child_ctr` numbers lanes derived here for spawned work; `epoch` is
// the reset generation the state was last rewound at.
struct TestSeedTls {
    uint64_t lane = 0;
    uint64_t ctr = 0;
    uint64_t child_ctr = 0;
    uint64_t epoch = 0;
};
inline TestSeedTls& test_seed_tls() {
    thread_local TestSeedTls tls;
    return tls;
}

// Rewind this thread's state if a reset happened since it last drew.
inline void sync_test_epoch(TestSeedTls& s) {
    const uint64_t ep = test_seed_epoch().load();
    if (s.epoch != ep) {
        s.ctr = 0;
        s.child_ctr = 0;
        s.epoch = ep;
    }
}

// Owner token of lane 0: the one thread allowed to draw main-lane seeds or
// derive child lanes. Cleared by reset_test_seed_counter(), so sequential
// independent units may run on different threads.
inline std::atomic<uint64_t>& lane0_owner() {
    static std::atomic<uint64_t> owner(0);
    return owner;
}
inline std::atomic<uint64_t>& next_thread_token() {
    static std::atomic<uint64_t> next(1);
    return next;
}
inline uint64_t this_thread_token() {
    thread_local const uint64_t token =
        next_thread_token().fetch_add(1, std::memory_order_relaxed);
    expecting(token != 0, "test mode: thread token space exhausted");
    return token;
}
inline void claim_lane0() {
    auto& owner = lane0_owner();
    const uint64_t token = this_thread_token();
    uint64_t expected = 0;
    expecting(owner.compare_exchange_strong(expected, token) ||
                  expected == token,
              "test mode: a second thread used lane 0; run spawned work "
              "under emp::test_lane_scope (see docs/test_mode.md)");
}

// splitmix64 finalizer: full-avalanche 64-bit mix for deriving child
// lane ids. Statistical distinctness is all that's needed (test
// determinism, not security).
inline uint64_t mix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

}  // namespace detail

// Programmatic toggle. Affects all subsequent PRG()-default-
// constructions and ECGroup::rand_scalar calls. Has no effect on
// PRG instances already constructed.
inline void set_test_mode(bool on) {
    if (on) detail::warn_insecure_test_mode_once();
    detail::test_mode_flag().store(on);
}

inline bool is_test_mode() {
    return detail::test_mode_flag().load();
}

// A test-mode seed: the lane plus the draw ordinal within it. PRG()
// packs it as makeBlock(lane, ordinal): distinct lanes can never
// collide, and a lane's sequence depends only on its own program
// order.
struct TestSeed {
    uint64_t lane;
    uint64_t ordinal;
};

// Yields the next deterministic seed for this thread's lane. Used
// internally by PRG() in test mode.
inline TestSeed next_test_seed() {
    auto& s = detail::test_seed_tls();
    detail::sync_test_epoch(s);
    if (s.lane == 0) detail::claim_lane0();
    return {s.lane, s.ctr++};
}

// Derives a fresh lane id for a unit of work about to be spawned. Call
// on the SPAWNING thread — its program order makes the value
// deterministic — pass the result to the worker, and install it there
// with test_lane_scope. ThreadPool::enqueue does both automatically.
// Hierarchical (child of the caller's lane), so workers may spawn
// grandchildren deterministically too.
inline uint64_t next_test_child_lane() {
    auto& s = detail::test_seed_tls();
    detail::sync_test_epoch(s);
    if (s.lane == 0) detail::claim_lane0();
    uint64_t lane = detail::mix64(detail::mix64(s.lane) ^ s.child_ctr++);
    if (lane == 0) lane = 1;  // 0 is reserved for the main thread
    return lane;
}

// RAII lane installer for the body of spawned work. Seed draws inside
// the scope come from (lane, 0), (lane, 1), ...; the thread's previous
// state is restored on exit (a pool worker returns to its idle state
// between tasks).
class test_lane_scope {
public:
    explicit test_lane_scope(uint64_t lane) : saved_(detail::test_seed_tls()) {
        expecting(lane != 0,
                  "test_lane_scope: lane 0 is reserved for the main thread");
        auto& s = detail::test_seed_tls();
        s.lane = lane;
        s.ctr = 0;
        s.child_ctr = 0;
        s.epoch = detail::test_seed_epoch().load();
    }
    ~test_lane_scope() { detail::test_seed_tls() = saved_; }
    test_lane_scope(const test_lane_scope&) = delete;
    test_lane_scope& operator=(const test_lane_scope&) = delete;

private:
    detail::TestSeedTls saved_;
};

// The current determinism epoch. A test-mode PRG that outlives a single
// unit of work compares against this to learn when a reset_test_seed_counter()
// means it must re-seed.
inline uint64_t current_test_seed_epoch() {
    return detail::test_seed_epoch().load();
}

// Rewind every lane's draw ordinal (lazily, when each thread next draws) and
// release lane 0. All work that can draw seeds or derive child lanes must be
// quiescent first. Use before each independent unit (e.g. each protocol in a
// trace) to make that unit's randomness -- and thus its wire bytes --
// independent of whatever consumed seeds before it.
inline void reset_test_seed_counter() {
    detail::test_seed_epoch().fetch_add(1);
    detail::lane0_owner().store(0);
}

}  // namespace emp

#endif  // EMP_TEST_MODE_H__
