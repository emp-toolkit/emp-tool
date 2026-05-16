#ifndef EMP_UTILS_H__
#define EMP_UTILS_H__
#include <string>
#include "emp-tool/core/block.h"
#include <sstream>
#include <cstddef>//https://gcc.gnu.org/gcc-4.9/porting_to.html
#include <cstring>
#include "emp-tool/core/constants.h"
#include <chrono>
#include <type_traits>
#define macro_xstr(a) macro_str(a)
#define macro_str(a) #a

namespace emp {
using std::chrono::time_point;
using std::chrono::high_resolution_clock;

// Defaults capture the caller's source location via __builtin_LINE /
// __builtin_FILE — both evaluate at the call site, so an unadorned
// `error("msg")` records where it fired.
[[noreturn]] inline void error(const char * s,
                               int line = __builtin_LINE(),
                               const char * file = __builtin_FILE());

inline void parse_party_and_port(const char * const * arg, int * party, int * port);

// Timing related
inline time_point<high_resolution_clock> clock_start();
inline double time_from(const time_point<high_resolution_clock>& s);


// --- Bool / bit packing -------------------------------------------------

// Pack 32 bool/byte values (any nonzero -> 1) into 32 bits. Inverse of bits32_to_bytes.
static inline uint32_t bytes_to_bits32(const void* in);

// Expand 32 bits into 32 bytes (each 0 or 1). Inverse of bytes_to_bits32.
static inline void bits32_to_bytes(uint32_t bits, void* out);

// Pack `len` bools (each byte 0/1) into the first `len` bits of `out`,
// LSB-first within each byte. Tail-preserve: bits beyond position (len-1)
// in the last destination byte are preserved unmodified — callers that
// pack into a stack buffer must zero the trailing partial byte before the
// call if they care about its contents (e.g. wire-format determinism).
// out must hold at least ⌈len/8⌉ bytes.
inline void bools_to_bits(void* out, const bool* bools, size_t len);

// Unpack the first `len` bits of `in` (LSB-first within each byte) into
// `len` bools (each 0/1, byte-sized). bools must hold at least `len` bytes.
inline void bits_to_bools(bool* bools, const void* in, size_t len);

// Value-returning conveniences for fixed-size targets.
template<typename T>
inline T bool_to_int(const bool * data);

block bool_to_block(const bool * data);

#include "emp-tool/core/utils.hpp"
}
#endif// UTILS_H__
