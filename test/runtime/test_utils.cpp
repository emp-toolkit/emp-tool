// core/utils.h, core/utils.hpp — small free-standing helpers. Read example()
// first; the rest is verification.
//
// What's in utils.h/utils.hpp (the parts worth testing):
//   clock_start(), time_from(start) monotonic elapsed-time helpers
//   joinNcleanCheat(futures)        wait for all futures and OR their results
//   parse_party(argv, max_party)    strictly parse a bounded party number
//   peer_port()                     strictly parse EMP_PORT or use 12345
//   bool_to_int<T>(const bool *)   pack 8*sizeof(T) bools (LSB-first) into T
//   bool_to_block(const bool *)    pack 128 bools into a block

#include "emp-tool/emp-tool.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <type_traits>
#include <unistd.h>
#include <vector>

using namespace emp;
using namespace std;

template <class F>
static bool dies(F&& f) {
	pid_t pid = fork();
	if (pid == 0) {
		close(STDERR_FILENO);
		f();
		_exit(0);
	}
	if (pid < 0) return false;
	int status = 0;
	if (waitpid(pid, &status, 0) != pid) return false;
	return !WIFEXITED(status) || WEXITSTATUS(status) != 0;
}

static void example() {
	cout << "=== example ===\n";

	// (1) bool_to_int<T>: 8*sizeof(T) bools (LSB-first) -> T.
	bool b8[8] = {1,0,1,1, 0,0,1,0};
	uint8_t v8 = bool_to_int<uint8_t>(b8);
	cout << "  bool_to_int<u8>([1,0,1,1, 0,0,1,0]) = 0x"
	     << hex << setw(2) << setfill('0') << (int)v8 << dec << setfill(' ') << "\n";

	bool b16[16] = {1,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,1};
	uint16_t v16 = bool_to_int<uint16_t>(b16);
	cout << "  bool_to_int<u16>(..)                = 0x"
	     << hex << setw(4) << setfill('0') << v16 << dec << setfill(' ') << "\n";

	// (2) bool_to_block: 128 bools -> block.
	bool b128[128] = {0};
	b128[0] = 1; b128[7] = 1; b128[64] = 1;
	cout << "  bool_to_block(bit0|bit7|bit64)      = " << bool_to_block(b128) << "\n";
}

// ---------- correctness ----------

static bool check_elapsed_clock() {
	static_assert(is_same_v<decltype(clock_start()),
	                        chrono::steady_clock::time_point>);
	auto start = clock_start();
	return time_from(start) >= 0.0;
}

static bool check_joinNcleanCheat_drains() {
	int completed = 0;
	vector<future<bool>> results;
	results.push_back(async(launch::deferred, [] { return true; }));
	results.push_back(async(launch::deferred, [&completed] {
		++completed;
		return false;
	}));
	return joinNcleanCheat(results) && completed == 1 && results.empty();
}

static bool check_parse_party() {
	const char *alice[] = {"test_utils", "1", nullptr};
	const char *bob[] = {"test_utils", "2", nullptr};
	const char *third[] = {"test_utils", "3", nullptr};
	if (parse_party(alice) != ALICE || parse_party(bob) != BOB ||
	    parse_party(third, 3) != 3)
		return false;

	const char *trailing[] = {"test_utils", "2x", nullptr};
	const char *spaced[] = {"test_utils", " 2", nullptr};
	const char *overflow[] = {"test_utils", "999999999999999999999", nullptr};
	const char *out_of_range[] = {"test_utils", "3", nullptr};
	const char *missing[] = {"test_utils", nullptr};
	return dies([&] { parse_party(trailing); }) &&
	       dies([&] { parse_party(spaced); }) &&
	       dies([&] { parse_party(overflow); }) &&
	       dies([&] { parse_party(out_of_range); }) &&
	       dies([&] { parse_party(missing); });
}

static bool check_peer_port() {
	unsetenv("EMP_PORT");
	if (peer_port() != 12345) return false;
	setenv("EMP_PORT", "1", 1);
	if (peer_port() != 1) return false;
	setenv("EMP_PORT", "65535", 1);
	if (peer_port() != 65535) return false;
	setenv("EMP_PORT", "", 1);
	if (peer_port() != 12345) return false;

	const char *invalid[] = {"0", "65536", "12x", " 12345",
	                         "999999999999999999999"};
	for (const char *value : invalid) {
		setenv("EMP_PORT", value, 1);
		if (!dies([] { (void)peer_port(); })) return false;
	}
	unsetenv("EMP_PORT");
	return true;
}

template <typename T>
static bool check_bool_to_int_random(int trials) {
	PRG prg;
	const int W = sizeof(T) * 8;
	for (int t = 0; t < trials; ++t) {
		uint64_t x = 0;
		prg.random_data_unaligned(&x, sizeof(T));
		bool bits[64];
		for (int i = 0; i < W; ++i) bits[i] = (x >> i) & 1;
		T got = bool_to_int<T>(bits);
		T want = (T)x;
		if (got != want) return false;
	}
	return true;
}

static bool check_bool_to_int_known() {
	bool all1[64];
	for (int i = 0; i < 64; ++i) all1[i] = 1;
	if (bool_to_int<uint8_t>(all1)  != (uint8_t)~0)  return false;
	if (bool_to_int<uint16_t>(all1) != (uint16_t)~0) return false;
	if (bool_to_int<uint32_t>(all1) != (uint32_t)~0) return false;
	if (bool_to_int<uint64_t>(all1) != (uint64_t)~0) return false;

	bool all0[64] = {0};
	if (bool_to_int<uint64_t>(all0) != 0) return false;
	return true;
}

static bool check_bool_to_block_random(int trials) {
	PRG prg;
	for (int t = 0; t < trials; ++t) {
		block want;
		prg.random_block(&want, 1);
		bool bits[128];
		uint8_t bytes[16];
		_mm_storeu_si128(reinterpret_cast<__m128i *>(bytes), want);
		for (int i = 0; i < 128; ++i) bits[i] = (bytes[i / 8] >> (i % 8)) & 1;
		block got = bool_to_block(bits);
		__m128i d = _mm_xor_si128(got, want);
		if (!_mm_testz_si128(d, d)) return false;
	}
	return true;
}

static bool run_correctness() {
	cout << "=== correctness ===\n";
	bool clock = check_elapsed_clock();
	bool joins = check_joinNcleanCheat_drains();
	bool party = check_parse_party();
	bool port = check_peer_port();
	auto u8  = []{ return check_bool_to_int_random<uint8_t>(64); };
	auto u16 = []{ return check_bool_to_int_random<uint16_t>(64); };
	auto u32 = []{ return check_bool_to_int_random<uint32_t>(64); };
	auto u64 = []{ return check_bool_to_int_random<uint64_t>(64); };
	auto blk = []{ return check_bool_to_block_random(64); };
	auto kn  = []{ return check_bool_to_int_known(); };
	cout << "  elapsed clock is monotonic             " << (clock ? "OK" : "FAIL") << "\n";
	cout << "  joinNcleanCheat drains every future    " << (joins ? "OK" : "FAIL") << "\n";
	cout << "  parse_party rejects malformed input    " << (party ? "OK" : "FAIL") << "\n";
	cout << "  peer_port validates EMP_PORT            " << (port ? "OK" : "FAIL") << "\n";
	bool a = u8();    cout << "  bool_to_int<uint8_t>  random          " << (a ? "OK" : "FAIL") << "\n";
	bool b = u16();   cout << "  bool_to_int<uint16_t> random          " << (b ? "OK" : "FAIL") << "\n";
	bool c = u32();   cout << "  bool_to_int<uint32_t> random          " << (c ? "OK" : "FAIL") << "\n";
	bool d = u64();   cout << "  bool_to_int<uint64_t> random          " << (d ? "OK" : "FAIL") << "\n";
	bool e = blk();   cout << "  bool_to_block         random          " << (e ? "OK" : "FAIL") << "\n";
	bool f = kn();    cout << "  bool_to_int<*>        known answers   " << (f ? "OK" : "FAIL") << "\n";
	return clock && joins && party && port && a && b && c && d && e && f;
}

int main(int /*argc*/, char ** /*argv*/) {
	example();
	cout << "\n";
	if (!run_correctness()) {
		cerr << "CORRECTNESS FAILURE\n";
		return 1;
	}
	return 0;
}
