#ifndef EMP_DETERMINISTIC_MODE_H__
#define EMP_DETERMINISTIC_MODE_H__

// Deterministic mode, for wire-byte determinism. In a deterministic-mode build,
// default-constructed randomness sources yield a deterministic byte stream, so
// two runs of the same protocol produce byte-identical wire output.
//
// Deterministic mode is a build option, not a runtime switch: configuring with
// -DEMP_DETERMINISTIC_MODE=ON defines EMP_DETERMINISTIC_MODE for emp-tool and
// for everything that links it. A normal build compiles the deterministic paths
// out, so no environment variable or call can make its randomness predictable.
// Every library and object linked into one program must be built the same way;
// mixing them fails to link (DeterministicModeAbiMarker below).
//
// Determinism model: a seed is the triple (party, lane, ordinal). The party is
// the one this process plays in a multi-process run (set_deterministic_party;
// parse_party sets it), so that its parties draw different streams, as they do
// with real randomness. The lane names the logical unit of work — the main
// thread is lane 0, and a spawned worker runs under a lane chosen by its
// CREATOR (whose program order is deterministic), never discovered by the
// worker itself. The ordinal counts seed draws within the lane and is
// thread-local. None of the three depends on cross-thread scheduling, so
// multi-threaded runs reproduce. ThreadPool::enqueue derives and installs a
// lane per task automatically; hand-spawned threads wrap their body in
// deterministic_lane_scope. A second thread using lane 0 to draw a seed or
// derive a child lane aborts: two threads sharing a lane would replay
// identical "random" streams — silently wrong rather than merely
// nondeterministic.

#include "emp-tool/runtime/core/abi_guard.h"
#include "emp-tool/runtime/core/error.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <thread>

// A build that still defines the option's old name gets the new one.
#if defined(EMP_TEST_MODE) && !defined(EMP_DETERMINISTIC_MODE)
#define EMP_DETERMINISTIC_MODE
#endif

#ifdef EMP_DETERMINISTIC_MODE
#define EMP_DETERMINISTIC_MODE_ENABLED true
#else
#define EMP_DETERMINISTIC_MODE_ENABLED false
#endif

namespace emp {

// Build-consistency backstop (runtime/core/abi_guard.h): libemp-tool defines
// the marker for the mode it was built in and every translation unit
// references the marker for its own, so linking deterministic-mode and normal
// objects together fails with an undefined DeterministicModeAbiMarker symbol
// instead of mixing predictable and real seeds in one process.
template <bool> struct DeterministicModeAbiMarker { static const char sym; };
template <> const char DeterministicModeAbiMarker<EMP_DETERMINISTIC_MODE_ENABLED>::sym;
namespace detail {
EMP_ABI_GUARD(emp_deterministic_mode_abi_guard,
              DeterministicModeAbiMarker<EMP_DETERMINISTIC_MODE_ENABLED>::sym);
}

namespace detail {

// A deterministic-mode build deliberately replaces cryptographic randomness
// with predictable streams. Say so once, loudly, at the first deterministic
// draw rather than adding logging to every random draw.
inline void warn_insecure_deterministic_mode_once() {
    static const bool warned = []() {
        std::fputs(
            "\n"
            "=======================================================================\n"
            "WARNING: EMP DETERMINISTIC-MODE BUILD - RANDOMNESS IS PREDICTABLE\n"
            "Default PRG seeds and EC scalars are deterministic in this process.\n"
            "Use only for testing; never process real secrets with this build.\n"
            "=======================================================================\n",
            stderr);
        std::fflush(stderr);
        return true;
    }();
    (void)warned;
}

// Determinism epoch, bumped by reset_deterministic_seeds(). Every thread's
// seed state records the epoch it was last rewound at and rewinds again when
// the epoch changes, so a reset reaches every thread's seed state.
inline std::atomic<uint64_t>& deterministic_epoch() {
    static std::atomic<uint64_t> ep(0);
    return ep;
}

// The party this process plays (set_deterministic_party), or 0, and whether it
// has drawn a seed as that party.
inline std::atomic<uint64_t>& deterministic_party() {
    static std::atomic<uint64_t> party(0);
    return party;
}
inline std::atomic<bool>& deterministic_party_drew() {
    static std::atomic<bool> drew(false);
    return drew;
}

// Per-thread seed state. `ctr` numbers seed draws within this lane;
// `child_ctr` numbers lanes derived here for spawned work; `epoch` is
// the reset generation the state was last rewound at.
struct DeterministicSeedTls {
    uint64_t lane = 0;
    uint64_t ctr = 0;
    uint64_t child_ctr = 0;
    uint64_t epoch = 0;
};
inline DeterministicSeedTls& deterministic_seed_tls() {
    thread_local DeterministicSeedTls tls;
    return tls;
}

// Rewind this thread's state if a reset happened since it last drew.
inline void sync_deterministic_epoch(DeterministicSeedTls& s) {
    const uint64_t ep = deterministic_epoch().load();
    if (s.epoch != ep) {
        s.ctr = 0;
        s.child_ctr = 0;
        s.epoch = ep;
    }
}

// Owner token of lane 0: the one thread allowed to draw main-lane seeds or
// derive child lanes. Cleared by reset_deterministic_seeds(), so sequential
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
    // Tokens count from 1, so 0 stays the "unowned" sentinel.
    thread_local const uint64_t token =
        next_thread_token().fetch_add(1, std::memory_order_relaxed);
    return token;
}
inline void claim_lane0() {
    auto& owner = lane0_owner();
    const uint64_t token = this_thread_token();
    uint64_t expected = 0;
    expecting(owner.compare_exchange_strong(expected, token) ||
                  expected == token,
              "deterministic mode: a second thread used lane 0; run spawned "
              "work under emp::deterministic_lane_scope (see "
              "docs/deterministic_mode.md)");
}

// splitmix64 finalizer: full-avalanche 64-bit mix for deriving child
// lane ids and party offsets. Statistical distinctness is all that's
// needed (test determinism, not security).
inline uint64_t mix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

}  // namespace detail

// True in a deterministic-mode build (see the top of this file). A
// compile-time constant, so `if constexpr (is_deterministic_mode())` keeps
// the deterministic paths out of a normal build.
constexpr bool is_deterministic_mode() { return EMP_DETERMINISTIC_MODE_ENABLED; }

// Makes this process draw the seeds of `party`. The processes of a
// multi-process run all start from the same (lane, ordinal) schedule, so
// without a party each would draw what every other draws -- the same keys and
// masks -- and a test could pass that real randomness fails. parse_party()
// calls this. Seeds drawn before any party is set are party 0's. Once the
// process has drawn a seed as one party, setting another aborts: it would mix
// two parties' streams in one process. Does nothing in a normal build.
inline void set_deterministic_party(int party) {
    if constexpr (is_deterministic_mode()) {
        expecting(party > 0, "set_deterministic_party: a party id is positive");
        auto& p = detail::deterministic_party();
        const uint64_t cur = p.load();
        if (cur == (uint64_t)party) return;
        expecting(cur == 0 || !detail::deterministic_party_drew().load(),
                  "set_deterministic_party: this process already drew seeds as "
                  "another party");
        p.store((uint64_t)party);
    } else {
        (void)party;
    }
}

// A deterministic seed: the party, the lane, and the draw ordinal within the
// lane. PRG() packs it with deterministic_seed_block.
struct DeterministicSeed {
    uint64_t party;
    uint64_t lane;
    uint64_t ordinal;
};

// The PRG seed of `s`: the lane in the high half, offset by the party, and
// the ordinal in the low half. Distinct lanes of one party never collide, a
// lane's sequence depends only on its own program order, and party 0 (none
// set) gives the lane itself.
inline uint64_t deterministic_seed_high(const DeterministicSeed& s) {
    return s.party == 0 ? s.lane
                        : s.lane ^ detail::mix64(0x7061727479ULL ^ s.party);  // "party"
}

// Yields the next deterministic seed for this thread's lane. Used internally
// by PRG() in a deterministic-mode build.
inline DeterministicSeed next_deterministic_seed() {
    detail::warn_insecure_deterministic_mode_once();
    auto& s = detail::deterministic_seed_tls();
    detail::sync_deterministic_epoch(s);
    if (s.lane == 0) detail::claim_lane0();
    const uint64_t party = detail::deterministic_party().load();
    if (party != 0 && !detail::deterministic_party_drew().load(std::memory_order_relaxed))
        detail::deterministic_party_drew().store(true);
    return {party, s.lane, s.ctr++};
}

// Derives a fresh lane id for a unit of work about to be spawned. Call
// on the SPAWNING thread — its program order makes the value
// deterministic — pass the result to the worker, and install it there
// with deterministic_lane_scope. ThreadPool::enqueue does both
// automatically. Hierarchical (child of the caller's lane), so workers may
// spawn grandchildren deterministically too.
inline uint64_t next_deterministic_child_lane() {
    detail::warn_insecure_deterministic_mode_once();
    auto& s = detail::deterministic_seed_tls();
    detail::sync_deterministic_epoch(s);
    if (s.lane == 0) detail::claim_lane0();
    uint64_t lane = detail::mix64(detail::mix64(s.lane) ^ s.child_ctr++);
    if (lane == 0) lane = 1;  // 0 is reserved for the main thread
    return lane;
}

// RAII lane installer for the body of spawned work. Seed draws inside
// the scope come from (lane, 0), (lane, 1), ...; the thread's previous
// state is restored on exit (a pool worker returns to its idle state
// between tasks).
class deterministic_lane_scope {
public:
    explicit deterministic_lane_scope(uint64_t lane)
        : saved_(detail::deterministic_seed_tls()) {
        expecting(lane != 0,
                  "deterministic_lane_scope: lane 0 is the unscoped lane; use "
                  "a nonzero lane");
        auto& s = detail::deterministic_seed_tls();
        s.lane = lane;
        s.ctr = 0;
        s.child_ctr = 0;
        s.epoch = detail::deterministic_epoch().load();
    }
    ~deterministic_lane_scope() { detail::deterministic_seed_tls() = saved_; }
    deterministic_lane_scope(const deterministic_lane_scope&) = delete;
    deterministic_lane_scope& operator=(const deterministic_lane_scope&) = delete;

private:
    detail::DeterministicSeedTls saved_;
};

// The current determinism epoch: how many times reset_deterministic_seeds()
// has run in this process.
inline uint64_t current_deterministic_epoch() {
    return detail::deterministic_epoch().load();
}

// Rewind every lane's draw ordinal (lazily, when each thread next draws) and
// release lane 0. All work that can draw seeds or derive child lanes must be
// quiescent first. Use before each independent unit (e.g. each protocol in a
// trace) to make that unit's randomness -- and thus its wire bytes --
// independent of whatever consumed seeds before it. The party stays set.
inline void reset_deterministic_seeds() {
    detail::deterministic_epoch().fetch_add(1);
    detail::lane0_owner().store(0);
}

// The names of this mode before it was called deterministic mode. They will
// be removed in a later release.
[[deprecated("use is_deterministic_mode")]] constexpr bool is_test_mode() {
    return is_deterministic_mode();
}
using TestSeed [[deprecated("use DeterministicSeed")]] = DeterministicSeed;
[[deprecated("use next_deterministic_seed")]] inline DeterministicSeed next_test_seed() {
    return next_deterministic_seed();
}
[[deprecated("use next_deterministic_child_lane")]] inline uint64_t next_test_child_lane() {
    return next_deterministic_child_lane();
}
using test_lane_scope [[deprecated("use deterministic_lane_scope")]] = deterministic_lane_scope;
[[deprecated("use current_deterministic_epoch")]] inline uint64_t current_test_seed_epoch() {
    return current_deterministic_epoch();
}
[[deprecated("use reset_deterministic_seeds")]] inline void reset_test_seed_counter() {
    reset_deterministic_seeds();
}

}  // namespace emp

#endif  // EMP_DETERMINISTIC_MODE_H__
