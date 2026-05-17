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
// Single-threaded determinism only: multiple threads racing for seeds
// from the global counter produce non-reproducible orderings.

#include <atomic>
#include <cstdint>
#include <cstdlib>

namespace emp {

namespace detail {

inline std::atomic<bool>& test_mode_flag() {
    static std::atomic<bool> flag(
        []() {
            const char* v = std::getenv("EMP_TEST_MODE");
            return v != nullptr && v[0] == '1';
        }());
    return flag;
}

// Monotonic seed counter consumed by PRG() and other randomness
// hooks in test mode. Each consumer gets a distinct seed; calls
// across consumers are interleaved by call order.
inline std::atomic<uint64_t>& test_seed_counter() {
    static std::atomic<uint64_t> ctr(0xC0FFEE12345ULL);
    return ctr;
}

}  // namespace detail

// Programmatic toggle. Affects all subsequent PRG()-default-
// constructions and ECGroup::rand_scalar calls. Has no effect on
// PRG instances already constructed.
inline void set_test_mode(bool on) {
    detail::test_mode_flag().store(on);
}

inline bool is_test_mode() {
    return detail::test_mode_flag().load();
}

// Yields the next deterministic seed counter value. Used internally
// by PRG() and ECGroup::rand_scalar in test mode.
inline uint64_t next_test_seed() {
    return detail::test_seed_counter().fetch_add(1);
}

// Reset the seed counter to its initial value. Use between trace
// runs to get reproducible PRG sequences.
inline void reset_test_seed_counter() {
    detail::test_seed_counter().store(0xC0FFEE12345ULL);
}

}  // namespace emp

#endif  // EMP_TEST_MODE_H__
