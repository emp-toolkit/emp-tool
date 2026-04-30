#ifndef EMP_UTILS_H__
#define EMP_UTILS_H__
#include <string>
#include "emp-tool/core/block.h"
#include <sstream>
#include <cstddef>//https://gcc.gnu.org/gcc-4.9/porting_to.html
#include "emp-tool/core/constants.h"
#include <chrono>
#include <type_traits>
#define macro_xstr(a) macro_str(a)
#define macro_str(a) #a

using std::string;
using std::chrono::time_point;
using std::chrono::high_resolution_clock;

namespace emp {
template<typename T>
void inline delete_array_null(T * ptr);

inline void error(const char * s, int line = 0, const char * file = nullptr);

template<class... Ts>
void run_function(void *function, const Ts&... args);

inline void parse_party_and_port(const char * const * arg, int * party, int * port);

// Timing related
inline time_point<high_resolution_clock> clock_start();
inline double time_from(const time_point<high_resolution_clock>& s);


//Conversions. Bulk packing/unpacking lives in block.h as
// bools_to_bits / bits_to_bools. The two helpers below are
// value-returning conveniences for fixed-size targets.
template<typename T>
inline T bool_to_int(const bool * data);

block bool_to_block(const bool * data);

bool file_exists(const std::string &name);

#include "emp-tool/core/utils.hpp"
}
#endif// UTILS_H__
