#ifndef UTILS_H__
#define UTILS_H__
#include <string>
#include "block.h"
#include <sstream>
#include <cstddef>//https://gcc.gnu.org/gcc-4.9/porting_to.html
#include <gmp.h>
#include "prg.h"
#include <chrono>
#define macro_xstr(a) macro_str(a)
#define macro_str(a) #a

#define PUBLIC 0
#define ALICE 1
#define BOB 2
using std::string;

template<typename T>
void inline delete_array_null(T * ptr){
	if(ptr != nullptr) {
		delete[] ptr;
		ptr = nullptr;
	}
}

inline void error(const char * s, int line = 0, const char * file = nullptr) {
	fprintf(stderr, s, "\n");
	if(file != nullptr) {
		fprintf(stderr, "at %d, %s\n", line, file);
	}
	exit(1);
}
template<class... Ts>
void run_function(void *function, const Ts&... args) {	
	reinterpret_cast<void(*)(Ts...)>(function)(args...);
}

void inline parse_party_and_port(char ** arg, int argc, int * party, int * port);

//deprecate soon
void inline parse_party_and_port(char ** arg, int * party, int * port) {
	parse_party_and_port(arg, 2, party, port);
}

std::string Party(int p);
template <typename T = uint64_t>
std::string m128i_to_string(const __m128i var) {
	std::stringstream sstr;
	const T* values = (const T*) &var;
	for (unsigned int i = 0; i < sizeof(__m128i) / sizeof(T); i++) {
		sstr <<"0x"<<std::hex<< values[i] << " ";
	}
	return sstr.str();
}
double wallClock();
uint64_t timeStamp();

inline std::chrono::time_point<std::chrono::high_resolution_clock> clock_start() { 
	return std::chrono::high_resolution_clock::now();
}
inline long long time_from(const std::chrono::time_point<std::chrono::high_resolution_clock>& s) {
	return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - s).count();
}
template<typename T>
T bool_to_int(const bool * data, size_t len = 0);
block bool_to128(const bool * data);
void int64_to_bool(bool * data, uint64_t input, int length);
std::string hex_to_binary(std::string hex);

inline string change_base(string str, int old_base, int new_base) {
	mpz_t tmp;
	mpz_init_set_str (tmp, str.c_str(), old_base);
	char * b = new char[mpz_sizeinbase(tmp, new_base) + 2];
	mpz_get_str(b, new_base, tmp);
	mpz_clear(tmp);
	string res(b);
	delete[]b;
	return res;
}

inline string dec_to_bin(const string& dec) {
	string bin = change_base(dec, 10, 2);
	if(dec[0] != '-')
		return '0' + bin;
	bin[0] = '1';
	bool flip = false;
	for(int i = bin.size()-1; i>=1; --i) {
		if(flip)
			bin[i] = (bin[i] == '1' ? '0': '1');
		if(bin[i] == '1')
			flip = true;
	}
	return bin;
}
inline string bin_to_dec(const string& bin2) {
	if(bin2[0] == '0')
		return change_base(bin2, 2, 10);
	string bin = bin2;
	bin[0] = '0';
	bool flip = false;
	for(int i = bin.size()-1; i>=1; --i) {
		if(flip)
			bin[i] = (bin[i] == '1' ? '0': '1');
		if(bin[i] == '1')
			flip = true;
	}
	return "-"+change_base(bin, 2, 10);
}

#include "utils.hpp"
#endif// UTILS_H__
