// circuits/signed_int.h — runtime-width two's-complement signed integer.
// Read example() first; the rest is verification against int{32,64}_t.
//
// What's in signed_int.h:
//   SignedInt_T<Wire>(width, value, party)   feed an int{N}_t-shaped value
//   + - * / %                                wrap mod 2^N (matches int{N}_t HW)
//   - (unary)                                two's-complement negate
//   < <= > >= == !=                          signed comparisons
//   & | ^ ~                                  bitwise (inherited from BitVec)
//   << (size_t / UnsignedInt)                logical shift left
//   >> (size_t / UnsignedInt)                arithmetic shift right (sign-fill)
//   resize(W)                                sign-extend / truncate
//   abs()                                    branchless |x|; abs(MIN) bit-pattern
//   reveal<int32_t / int64_t / string>()
//   as_unsigned()                            bit-cast to UnsignedInt
//
// All arithmetic is verified to match int32_t / int64_t modulo two's
// complement (i.e. the actual hardware behavior; C UB on signed overflow
// is sidestepped by reference implementations that cast through unsigned).

#include "emp-tool/emp-tool.h"
#include <climits>
#include <cstdint>
#include <iostream>
#include <type_traits>
#include <vector>

using namespace emp;
using std::cout;

// ---- example -------------------------------------------------------------

static void example() {
	SignedInt a(32, -7, ALICE);
	SignedInt b(32,  3, BOB);
	cout << "a + b = " << (a + b).reveal<int32_t>(PUBLIC) << "\n";   // -4
	cout << "a - b = " << (a - b).reveal<int32_t>(PUBLIC) << "\n";   // -10
	cout << "a / b = " << (a / b).reveal<int32_t>(PUBLIC) << "\n";   // -2 (truncate toward 0)
	cout << "a % b = " << (a % b).reveal<int32_t>(PUBLIC) << "\n";   // -1 (sign matches dividend)
	cout << "(a >= 0) = " << (a >= SignedInt(32, 0, PUBLIC)).reveal<bool>(PUBLIC) << "\n"; // 0
	cout << "abs(a) = " << a.abs().reveal<uint32_t>(PUBLIC) << "\n"; // 7
	cout << "a >> 1 = " << (a >> 1).reveal<int32_t>(PUBLIC) << "\n"; // -4 (ASR)
}

// ---- reference: deterministic wrapping signed arithmetic -----------------

static int32_t add_w(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }
static int32_t sub_w(int32_t a, int32_t b) { return (int32_t)((uint32_t)a - (uint32_t)b); }
static int32_t mul_w(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }
static int32_t neg_w(int32_t a)            { return (int32_t)(0u - (uint32_t)a); }
static int32_t shl_w(int32_t a, unsigned s) {
	if (s >= 32) return 0;
	return (int32_t)((uint32_t)a << s);
}
static int32_t shr_w(int32_t a, unsigned s) {  // arithmetic shift right
	if (s >= 32) return a < 0 ? -1 : 0;
	// implementation-defined in C++ pre-20; relies on hw-typical ASR
	return a >> (int)s;
}
static int32_t div_w(int32_t a, int32_t b) {
	// INT_MIN / -1 is UB in C; circuit returns INT_MIN. Skip in caller.
	return a / b;
}
static int32_t mod_w(int32_t a, int32_t b) { return a % b; }

// ---- correctness checks --------------------------------------------------

template <typename Op>
static bool check_random_pair(const char *name, Op op,
                              int32_t (*ref)(int32_t, int32_t),
                              size_t runs = 2000) {
	PRG prg;
	for (size_t i = 0; i < runs; ++i) {
		int32_t ia, ib;
		prg.random_data_unaligned(&ia, 4);
		prg.random_data_unaligned(&ib, 4);
		SignedInt a(32, ia, ALICE);
		SignedInt b(32, ib, BOB);
		int32_t got = op(a, b).template reveal<int32_t>(PUBLIC);
		int32_t want = ref(ia, ib);
		if (got != want) {
			cout << "  FAIL " << name << "  ia=" << ia << " ib=" << ib
			     << " want=" << want << " got=" << got << "\n";
			return false;
		}
	}
	return true;
}

static bool check_div_random() {
	// Avoid b==0 and the INT_MIN/-1 case (UB in C).
	PRG prg;
	for (size_t i = 0; i < 2000; ++i) {
		int32_t ia, ib;
		prg.random_data_unaligned(&ia, 4);
		prg.random_data_unaligned(&ib, 4);
		if (ib == 0) continue;
		if (ia == INT32_MIN && ib == -1) continue;
		SignedInt a(32, ia, ALICE);
		SignedInt b(32, ib, BOB);
		int32_t got_d = (a / b).reveal<int32_t>(PUBLIC);
		int32_t got_m = (a % b).reveal<int32_t>(PUBLIC);
		if (got_d != div_w(ia, ib) || got_m != mod_w(ia, ib)) {
			cout << "  FAIL div/mod  ia=" << ia << " ib=" << ib
			     << " want=" << div_w(ia, ib) << "," << mod_w(ia, ib)
			     << " got=" << got_d << "," << got_m << "\n";
			return false;
		}
	}
	return true;
}

static bool check_unary_neg() {
	std::vector<int32_t> vs = {0, 1, -1, 7, -7, INT32_MAX, INT32_MIN, INT32_MIN + 1};
	for (auto v : vs) {
		SignedInt a(32, v, ALICE);
		if ((-a).reveal<int32_t>(PUBLIC) != neg_w(v)) {
			cout << "  FAIL unary -  v=" << v << "\n";
			return false;
		}
	}
	return true;
}

static bool check_compare_random() {
	PRG prg;
	for (size_t i = 0; i < 2000; ++i) {
		int32_t ia, ib;
		prg.random_data_unaligned(&ia, 4);
		prg.random_data_unaligned(&ib, 4);
		SignedInt a(32, ia, ALICE);
		SignedInt b(32, ib, BOB);
		bool g_lt = (a < b).reveal<bool>(PUBLIC);
		bool g_le = (a <= b).reveal<bool>(PUBLIC);
		bool g_gt = (a > b).reveal<bool>(PUBLIC);
		bool g_ge = (a >= b).reveal<bool>(PUBLIC);
		bool g_eq = (a == b).reveal<bool>(PUBLIC);
		bool g_ne = (a != b).reveal<bool>(PUBLIC);
		if (g_lt != (ia < ib) || g_le != (ia <= ib) ||
		    g_gt != (ia > ib) || g_ge != (ia >= ib) ||
		    g_eq != (ia == ib) || g_ne != (ia != ib)) {
			cout << "  FAIL compare  ia=" << ia << " ib=" << ib << "\n";
			return false;
		}
	}
	return true;
}

static bool check_shift_static() {
	std::vector<int32_t> vs = {0, 1, -1, 7, -7, INT32_MAX, INT32_MIN, 0x55555555, (int32_t)0xAAAAAAAA};
	for (auto v : vs)
		for (unsigned s = 0; s <= 33; ++s) {  // include shamt > width
			SignedInt a(32, v, ALICE);
			int32_t want_l = shl_w(v, s);
			int32_t want_r = shr_w(v, s);
			int32_t got_l = (a << (size_t)s).reveal<int32_t>(PUBLIC);
			int32_t got_r = (a >> (size_t)s).reveal<int32_t>(PUBLIC);
			if (got_l != want_l) {
				cout << "  FAIL <<  v=" << v << " s=" << s
				     << " want=" << want_l << " got=" << got_l << "\n";
				return false;
			}
			if (got_r != want_r) {
				cout << "  FAIL >>  v=" << v << " s=" << s
				     << " want=" << want_r << " got=" << got_r << "\n";
				return false;
			}
		}
	return true;
}

static bool check_resize() {
	// Sign-extend up.
	for (int32_t v : {0, 1, -1, 7, -7, INT32_MIN, INT32_MAX}) {
		SignedInt a(32, v, ALICE);
		int64_t got = a.resize(64).reveal<int64_t>(PUBLIC);
		int64_t want = (int64_t)v;
		if (got != want) {
			cout << "  FAIL resize-up  v=" << v << " want=" << want << " got=" << got << "\n";
			return false;
		}
	}
	// Truncate down.
	int64_t down_cases[] = {0LL, 1LL, -1LL, (int64_t)0xDEADBEEFCAFEBABEULL, INT64_MIN, INT64_MAX};
	for (int64_t v : down_cases) {
		SignedInt a(64, v, ALICE);
		int32_t got = a.resize(32).reveal<int32_t>(PUBLIC);
		int32_t want = (int32_t)v;  // C++ defines this as bit-truncation for two's complement
		if (got != want) {
			cout << "  FAIL resize-down  v=" << v << " want=" << want << " got=" << got << "\n";
			return false;
		}
	}
	return true;
}

static bool check_boundaries() {
	// Wrap behavior at the boundaries.
	struct { int32_t a, b; } cases[] = {
		{INT32_MAX, 1},   {INT32_MIN, -1},
		{INT32_MAX, INT32_MAX}, {INT32_MIN, INT32_MIN},
		{0, 0}, {-1, -1}, {1, -1},
	};
	for (auto c : cases) {
		SignedInt A(32, c.a, ALICE);
		SignedInt B(32, c.b, BOB);
		if ((A + B).reveal<int32_t>(PUBLIC) != add_w(c.a, c.b)) return false;
		if ((A - B).reveal<int32_t>(PUBLIC) != sub_w(c.a, c.b)) return false;
		if ((A * B).reveal<int32_t>(PUBLIC) != mul_w(c.a, c.b)) return false;
	}
	// abs(INT_MIN): hardware returns INT_MIN bit-pattern; we expose it via
	// .as_unsigned().reveal<uint32_t>() = 0x80000000.
	if (SignedInt(32, INT32_MIN, ALICE).abs().reveal<uint32_t>(PUBLIC) != 0x80000000u)
		return false;
	return true;
}

static bool run_correctness() {
	cout << "=== signed_int correctness ===\n";
	bool ok = true;
	auto run = [&](const char *name, bool r) {
		cout << "  " << name << (r ? "  OK\n" : "  FAIL\n");
		ok &= r;
	};
	run("+ (random)",        check_random_pair("+", [](auto&a, auto&b){return a + b;}, add_w));
	run("- (random)",        check_random_pair("-", [](auto&a, auto&b){return a - b;}, sub_w));
	run("* (random)",        check_random_pair("*", [](auto&a, auto&b){return a * b;}, mul_w));
	run("/ % (random)",      check_div_random());
	run("- (unary boundary)",check_unary_neg());
	run("< <= > >= == !=",   check_compare_random());
	run("<<  >>  (static)",  check_shift_static());
	run("resize sign-extend",check_resize());
	run("boundary wrap",     check_boundaries());
	return ok;
}

// ---- fixed-width aliases (Int8 / Int16 / Int32 / Int64) -----------------
// `Int32` is `SignedInt_T<block, 32>` — width baked into the type.

static bool run_fixed_width() {
	cout << "=== fixed-width aliases ===\n";
	bool ok = true;

	// Construction without width arg.
	Int32 a(-7, ALICE);
	Int32 b(3, BOB);
	Int32 c = a + b;        // Int32 + Int32 -> Int32
	if (c.reveal<int32_t>(PUBLIC) != -4) {
		cout << "  FAIL Int32 +  got=" << c.reveal<int32_t>(PUBLIC) << "\n"; ok = false;
	}

	// Default-ctor sizes the wire vector to N.
	Int64 z;
	if (z.size() != 64) { cout << "  FAIL Int64 default-ctor size\n"; ok = false; }

	// 64-bit signed via implicit width.
	Int64 v(-1ll, ALICE);
	if (v.reveal<int64_t>(PUBLIC) != -1ll) {
		cout << "  FAIL Int64 ctor + reveal\n"; ok = false;
	}

	// Signed comparison: -7 < 3.
	if ( (a >= b).reveal<bool>(PUBLIC)) { cout << "  FAIL Int32 >= (signed)\n"; ok = false; }
	if (!(a <  b).reveal<bool>(PUBLIC)) { cout << "  FAIL Int32 <  (signed)\n"; ok = false; }
	if ( (a == b).reveal<bool>(PUBLIC)) { cout << "  FAIL Int32 ==\n"; ok = false; }

	// abs() returns the matching unsigned alias (UInt32 here).
	UInt32 ua = a.abs();
	if (ua.reveal<uint32_t>(PUBLIC) != 7u) {
		cout << "  FAIL Int32::abs\n"; ok = false;
	}

	// Random arithmetic.
	PRG prg;
	for (int i = 0; i < 200; ++i) {
		int32_t ia, ib;
		prg.random_data_unaligned(&ia, 4);
		prg.random_data_unaligned(&ib, 4);
		Int32 A(ia, ALICE), B(ib, BOB);
		if ((A + B).reveal<int32_t>(PUBLIC) != (int32_t)((uint32_t)ia + (uint32_t)ib)) { ok = false; break; }
		if ((A * B).reveal<int32_t>(PUBLIC) != (int32_t)((uint32_t)ia * (uint32_t)ib)) { ok = false; break; }
		if ((A < B).reveal<bool>(PUBLIC)    != (ia < ib)) { ok = false; break; }
	}

	// Sort signed values.
	const int n = 6;
	Int32 keys[n];
	int32_t in_vals[n] = {3, -5, 0, -2, 7, -9};
	for (int i = 0; i < n; ++i) keys[i] = Int32(in_vals[i], ALICE);
	sort(keys, n);
	int32_t want_sorted[n] = {-9, -5, -2, 0, 3, 7};
	for (int i = 0; i < n; ++i)
		if (keys[i].reveal<int32_t>(PUBLIC) != want_sorted[i]) {
			cout << "  FAIL sort at i=" << i << " got="
			     << keys[i].reveal<int32_t>(PUBLIC) << " want=" << want_sorted[i] << "\n";
			ok = false;
		}

	cout << (ok ? "  fixed-width: OK\n" : "  fixed-width: FAIL\n");
	return ok;
}

int main(int /*argc*/, char ** /*argv*/) {
	setup_clear_backend();
	example();
	bool ok = run_correctness();
	ok &= run_fixed_width();
	cout << "AND gates: " << backend->num_and() << "\n";
	finalize_clear_backend();
	return ok ? 0 : 1;
}
