// circuits/bitvec.h — runtime-width vector of wires, no arithmetic.
// Read example() first; the rest is verification against std::bitset.
//
// What's in bitvec.h:
//   BitVec_T<Wire>(width, value, party)       feed an integral value
//   BitVec_T<Wire>(width, ptr, party)         feed raw bits from memory
//   operator& | ^ ~                           bitwise, equal width required
//   operator<< (size_t)  >> (size_t)          logical shifts (zero-fill)
//   equal(rhs)                                Bit, equal in every position
//   concat(hi)                                lo|hi, returns wider BitVec
//   slice(lo, hi)                             [lo, hi) sub-range
//   select(sel, rhs)                          oblivious bit-wise mux
//   operator[i]                               Bit reference
//   reveal<O>(party)                          O = std::string or integral
//   reveal(void* out, party)                  raw bytes out

#include "emp-tool/emp-tool.h"
#include <bitset>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace emp;
using std::cout;

static constexpr int W = 32;

// ---- example -------------------------------------------------------------

static void example() {
	BitVec a(W, (uint32_t)0xCAFEBABE, ALICE);
	BitVec b(W, (uint32_t)0xDEADBEEF, BOB);
	cout << "a^b = "  << std::hex << (a ^ b).reveal<uint32_t>(PUBLIC) << "\n";
	cout << "a&b = "  << (a & b).reveal<uint32_t>(PUBLIC) << "\n";
	cout << "~a  = "  << (~a   ).reveal<uint32_t>(PUBLIC) << "\n";
	cout << "a<<8 = " << (a << 8).reveal<uint32_t>(PUBLIC) << "\n";
	cout << "a[0:8]="  << a.slice(0, 8).reveal<uint8_t>(PUBLIC) + 0 << std::dec << "\n";
}

// ---- correctness ---------------------------------------------------------

static bool check_bitwise_random() {
	PRG prg;
	for (int i = 0; i < 1000; ++i) {
		uint32_t ia, ib;
		prg.random_data_unaligned(&ia, 4);
		prg.random_data_unaligned(&ib, 4);
		BitVec a(W, ia, ALICE);
		BitVec b(W, ib, BOB);
		if ((a & b).reveal<uint32_t>(PUBLIC) != (ia & ib)) return false;
		if ((a | b).reveal<uint32_t>(PUBLIC) != (ia | ib)) return false;
		if ((a ^ b).reveal<uint32_t>(PUBLIC) != (ia ^ ib)) return false;
		if ((~a   ).reveal<uint32_t>(PUBLIC) != (~ia    )) return false;
	}
	return true;
}

static bool check_shifts_static() {
	std::vector<uint32_t> vs = {0, 1, UINT32_MAX, 0x80000000u, 0xCAFEBABEu};
	for (auto v : vs)
		for (unsigned s = 0; s <= 33; ++s) {
			BitVec a(W, v, ALICE);
			uint32_t want_l = s >= 32 ? 0 : (v << s);
			uint32_t want_r = s >= 32 ? 0 : (v >> s);
			if ((a << (size_t)s).reveal<uint32_t>(PUBLIC) != want_l) return false;
			if ((a >> (size_t)s).reveal<uint32_t>(PUBLIC) != want_r) return false;
		}
	return true;
}

static bool check_concat_slice() {
	PRG prg;
	for (int i = 0; i < 200; ++i) {
		uint32_t lo, hi;
		prg.random_data_unaligned(&lo, 4);
		prg.random_data_unaligned(&hi, 4);
		BitVec L(W, lo, ALICE);
		BitVec H(W, hi, BOB);
		BitVec c = L.concat(H);  // lo at low indices, hi above
		if (c.size() != 2 * W) return false;
		uint64_t want = (uint64_t)lo | ((uint64_t)hi << 32);
		if (c.reveal<uint64_t>(PUBLIC) != want) {
			cout << "  FAIL concat\n";
			return false;
		}
		// slice arbitrary windows.
		for (size_t s = 0; s + 8 <= 2 * W; s += 8) {
			uint8_t got = c.slice(s, s + 8).reveal<uint8_t>(PUBLIC);
			uint8_t want_byte = (uint8_t)(want >> s);
			if (got != want_byte) {
				cout << "  FAIL slice  s=" << s
				     << " want=" << (int)want_byte << " got=" << (int)got << "\n";
				return false;
			}
		}
	}
	return true;
}

static bool check_equal() {
	PRG prg;
	for (int i = 0; i < 500; ++i) {
		uint32_t v;
		prg.random_data_unaligned(&v, 4);
		BitVec a(W, v, ALICE);
		BitVec b(W, v, BOB);
		BitVec c(W, v ^ 1, BOB);
		if (a.equal(b).reveal<bool>(PUBLIC) != true) return false;
		if (a.equal(c).reveal<bool>(PUBLIC) != false) return false;
	}
	return true;
}

static bool check_select() {
	PRG prg;
	for (int i = 0; i < 500; ++i) {
		uint32_t va, vb;
		prg.random_data_unaligned(&va, 4);
		prg.random_data_unaligned(&vb, 4);
		Bit s0(false, PUBLIC), s1(true, PUBLIC);
		BitVec A(W, va, ALICE);
		BitVec B(W, vb, BOB);
		if (A.select(s0, B).reveal<uint32_t>(PUBLIC) != va) return false;
		if (A.select(s1, B).reveal<uint32_t>(PUBLIC) != vb) return false;
	}
	return true;
}

static bool check_reveal_string() {
	BitVec a(8, (uint32_t)0xA5, ALICE);   // 1010 0101
	std::string s = a.reveal<std::string>(PUBLIC);
	if (s != "10100101") {
		cout << "  FAIL reveal<string>  got=\"" << s << "\"\n";
		return false;
	}
	return true;
}

static bool check_raw_pointer_io() {
	uint8_t in[4]  = {0xDE, 0xAD, 0xBE, 0xEF};
	uint8_t out[4] = {};
	BitVec a(32, in, ALICE);
	a.reveal(out, PUBLIC);
	for (int i = 0; i < 4; ++i)
		if (in[i] != out[i]) {
			cout << "  FAIL raw IO  i=" << i << "\n";
			return false;
		}
	return true;
}

static bool run_correctness() {
	cout << "=== bitvec correctness ===\n";
	bool ok = true;
	auto run = [&](const char *name, bool r) {
		cout << "  " << name << (r ? "  OK\n" : "  FAIL\n");
		ok &= r;
	};
	run("& | ^ ~",          check_bitwise_random());
	run("<<  >>  static",   check_shifts_static());
	run("concat / slice",   check_concat_slice());
	run("equal",            check_equal());
	run("select",           check_select());
	run("reveal<string>",   check_reveal_string());
	run("raw byte IO",      check_raw_pointer_io());
	return ok;
}

int main(int /*argc*/, char ** /*argv*/) {
	setup_clear_backend();
	example();
	bool ok = run_correctness();
	cout << "AND gates: " << backend->num_and() << "\n";
	finalize_clear_backend();
	return ok ? 0 : 1;
}
