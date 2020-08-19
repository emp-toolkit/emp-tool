#ifndef UTILS_H__
#define UTILS_H__
#include <string>
#include "emp-tool/utils/block.h"
#include <sstream>
#include <cstddef>//https://gcc.gnu.org/gcc-4.9/porting_to.html
#include <gmp.h>
#include "emp-tool/utils/prg.h"
#include <chrono>
#define macro_xstr(a) macro_str(a)
#define macro_str(a) #a

using std::string;
using std::chrono::time_point;
using std::chrono::high_resolution_clock;

namespace emp {
inline void error(const char * s, int line = 0, const char * file = nullptr);

template<class... Ts>
void run_function(void *function, const Ts&... args);

inline void parse_party_and_port(char ** arg, int argc, int * party, int * port);

std::string Party(int p);

// Timing related
inline time_point<high_resolution_clock> clock_start();
inline double time_from(const time_point<high_resolution_clock>& s);

//block conversions
template <typename T = uint64_t>
std::string m128i_to_string(const __m128i var);
block bool_to128(const bool * data);
void int64_to_bool(bool * data, uint64_t input, int length);

//Other conversions
template<typename T>
T bool_to_int(const bool * data, size_t len = 0);
std::string hex_to_binary(std::string hex);
inline string change_base(string str, int old_base, int new_base);
inline string dec_to_bin(const string& dec);
inline string bin_to_dec(const string& bin2);
inline const char* hex_char_to_bin(char c);

//deprecate soon
void inline parse_party_and_port(char ** arg, int * party, int * port) {
	parse_party_and_port(arg, 2, party, port);
}

#include "emp-tool/utils/utils.hpp"
}
#endif// UTILS_H__
