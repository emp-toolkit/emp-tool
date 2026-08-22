#ifndef EMP_UTILS_H__
#define EMP_UTILS_H__
#include "emp-tool/runtime/core/error.h"
#include <string>
#include "emp-tool/runtime/core/block.h"
#include "emp-tool/runtime/core/simd_tier.h"
#include <sstream>
#include <cstddef>//https://gcc.gnu.org/gcc-4.9/porting_to.html
#include <charconv>
#include <cstdint>
#include <cstdlib>   // std::_Exit (fatal abort without running destructors)
#include <cstring>
#include "emp-tool/runtime/core/constants.h"
#include <chrono>
#include <type_traits>
#include <future>    // joinNclean / joinNcleanCheat: fold a batch of ThreadPool tasks back together
#include <vector>
#define macro_xstr(a) macro_str(a)
#define macro_str(a) #a

namespace emp {

using std::chrono::time_point;
using std::chrono::high_resolution_clock;

inline int parse_party(const char * const * arg, int max_party = BOB);   // argv[1] in [1, max_party]; default ALICE=1 / BOB=2
inline int peer_port();                              // $EMP_PORT, default 12345
inline const char * peer_ip();                       // $EMP_PEER_IP, default 127.0.0.1

// Timing related
inline std::chrono::steady_clock::time_point clock_start();
inline double time_from(const std::chrono::steady_clock::time_point& s);


// --- Bool / bit packing -------------------------------------------------

// Pack 32 bool/byte values (any nonzero -> 1) into 32 bits. Inverse of bits32_to_bytes.
static inline uint32_t bytes_to_bits32(const void* in);

// Expand 32 bits into 32 bytes (each 0 or 1). Inverse of bytes_to_bits32.
static inline void bits32_to_bytes(uint32_t bits, void* out);

// Pack `len` bools into the first `len` bits of `out`,
// LSB-first within each byte. Tail-preserve: bits beyond position (len-1)
// in the last destination byte are preserved unmodified — callers that
// pack into a stack buffer must zero the trailing partial byte before the
// call if they care about its contents (e.g. wire-format determinism).
// out must hold at least ⌈len/8⌉ bytes.
inline void bools_to_bits(void* out, const bool* bools, int64_t len);
template<typename T>
requires std::is_same_v<T, uint8_t>
inline void bools_to_bits(void* out, const T* bools, int64_t len);

// Unpack the first `len` bits of `in` (LSB-first within each byte) into
// `len` bools (each 0/1). bools must hold at least `len` elements.
inline void bits_to_bools(bool* bools, const void* in, int64_t len);
template<typename T>
requires std::is_same_v<T, uint8_t>
inline void bits_to_bools(T* bools, const void* in, int64_t len);

// Value-returning conveniences for fixed-size targets.
template<typename T>
inline T bool_to_int(const bool * data);

block bool_to_block(const bool * data);

}

#include "emp-tool/runtime/core/utils.hpp"
#endif// UTILS_H__
