// circuits/unsigned_int.h — runtime-width unsigned integer.
// Read example() first; the rest is verification against uint{32,64}_t.
//
// What's in unsigned_int.h:
//   UnsignedInt_T<Wire>(width, value, party)  feed a uint{N}_t-shaped value
//   + - * / %                                 wrap mod 2^N (matches uint{N}_t)
//   - (unary)                                 two's-complement negate
//   < <= > >= == !=                           unsigned comparisons
//   & | ^ ~                                   bitwise (inherited from BitVec)
//   << (size_t / UnsignedInt)                 logical shift left
//   >> (size_t / UnsignedInt)                 logical shift right (zero-fill)
//   resize(W)                                 zero-extend / truncate
//   leading_zeros(), hamming_weight()         bit-counts as UnsignedInt
//   mod_exp(p, q)                             (*this)^p mod q
//   reveal<uint32_t / uint64_t / string>()
//   as_signed()                               bit-cast to SignedInt

#include "emp-tool/emp-tool.h"
#include <climits>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace emp;
using std::cout;

// ---- example -------------------------------------------------------------

static void example() {
	UnsignedInt a(32, 7u, ALICE);
	UnsignedInt b(32, 3u, BOB);
	cout << "a + b = " << (a + b).reveal<uint32_t>(PUBLIC) << "\n";  // 10
	cout << "a / b = " << (a / b).reveal<uint32_t>(PUBLIC) << "\n";  // 2
	cout << "a % b = " << (a % b).reveal<uint32_t>(PUBLIC) << "\n";  // 1
	cout << "(a >= b) = " << (a >= b).reveal<bool>(PUBLIC) << "\n";  // 1
	UnsignedInt big(32, 0xFFFFFFFFu, ALICE);
	cout << "0xFFFFFFFF + 1 = " << (big + UnsignedInt(32, 1u, PUBLIC)).reveal<uint32_t>(PUBLIC) << "\n"; // 0
}

// ---- references: deterministic wrap (uint arithmetic is well-defined) ---

static uint32_t add_w(uint32_t a, uint32_t b) { return a + b; }
static uint32_t sub_w(uint32_t a, uint32_t b) { return a - b; }
static uint32_t mul_w(uint32_t a, uint32_t b) { return a * b; }
static uint32_t shl_w(uint32_t a, unsigned s) { return s >= 32 ? 0 : a << s; }
static uint32_t shr_w(uint32_t a, unsigned s) { return s >= 32 ? 0 : a >> s; }

// ---- correctness checks --------------------------------------------------

template <typename Op>
static bool check_random_pair(const char *name, Op op,
                              uint32_t (*ref)(uint32_t, uint32_t),
                              size_t runs = 2000) {
	PRG prg;
	for (size_t i = 0; i < runs; ++i) {
		uint32_t ia, ib;
		prg.random_data_unaligned(&ia, 4);
		prg.random_data_unaligned(&ib, 4);
		UnsignedInt a(32, ia, ALICE);
		UnsignedInt b(32, ib, BOB);
		uint32_t got = op(a, b).template reveal<uint32_t>(PUBLIC);
		uint32_t want = ref(ia, ib);
		if (got != want) {
			cout << "  FAIL " << name << "  ia=" << ia << " ib=" << ib
			     << " want=" << want << " got=" << got << "\n";
			return false;
		}
	}
	return true;
}

static bool check_div_random() {
	PRG prg;
	for (size_t i = 0; i < 2000; ++i) {
		uint32_t ia, ib;
		prg.random_data_unaligned(&ia, 4);
		prg.random_data_unaligned(&ib, 4);
		if (ib == 0) continue;
		UnsignedInt a(32, ia, ALICE);
		UnsignedInt b(32, ib, BOB);
		uint32_t got_d = (a / b).reveal<uint32_t>(PUBLIC);
		uint32_t got_m = (a % b).reveal<uint32_t>(PUBLIC);
		if (got_d != ia / ib || got_m != ia % ib) {
			cout << "  FAIL div/mod  ia=" << ia << " ib=" << ib << "\n";
			return false;
		}
	}
	return true;
}

static bool check_unary_neg() {
	for (uint32_t v : {0u, 1u, 7u, UINT32_MAX, 0x80000000u}) {
		UnsignedInt a(32, v, ALICE);
		uint32_t got = (-a).reveal<uint32_t>(PUBLIC);
		uint32_t want = (uint32_t)(-(int32_t)v);  // = 0 - v mod 2^32
		if (got != want) {
			cout << "  FAIL unary -  v=" << v << "\n";
			return false;
		}
	}
	return true;
}

static bool check_compare_random() {
	PRG prg;
	for (size_t i = 0; i < 2000; ++i) {
		uint32_t ia, ib;
		prg.random_data_unaligned(&ia, 4);
		prg.random_data_unaligned(&ib, 4);
		UnsignedInt a(32, ia, ALICE);
		UnsignedInt b(32, ib, BOB);
		if ((a < b).reveal<bool>(PUBLIC)  != (ia < ib))  return false;
		if ((a <= b).reveal<bool>(PUBLIC) != (ia <= ib)) return false;
		if ((a > b).reveal<bool>(PUBLIC)  != (ia > ib))  return false;
		if ((a >= b).reveal<bool>(PUBLIC) != (ia >= ib)) return false;
		if ((a == b).reveal<bool>(PUBLIC) != (ia == ib)) return false;
		if ((a != b).reveal<bool>(PUBLIC) != (ia != ib)) return false;
	}
	return true;
}

static bool check_shift_static() {
	std::vector<uint32_t> vs = {0, 1, 0x80000000u, UINT32_MAX, 0x55555555u, 0xAAAAAAAAu};
	for (auto v : vs)
		for (unsigned s = 0; s <= 33; ++s) {
			UnsignedInt a(32, v, ALICE);
			if ((a << (size_t)s).reveal<uint32_t>(PUBLIC) != shl_w(v, s)) {
				cout << "  FAIL <<  v=" << v << " s=" << s << "\n";
				return false;
			}
			if ((a >> (size_t)s).reveal<uint32_t>(PUBLIC) != shr_w(v, s)) {
				cout << "  FAIL >>  v=" << v << " s=" << s << "\n";
				return false;
			}
		}
	return true;
}

static bool check_resize() {
	// Zero-extend up.
	for (uint32_t v : {0u, 1u, UINT32_MAX, 0x80000000u}) {
		UnsignedInt a(32, v, ALICE);
		uint64_t got = a.resize(64).reveal<uint64_t>(PUBLIC);
		uint64_t want = (uint64_t)v;
		if (got != want) {
			cout << "  FAIL resize-up  v=" << v << "\n";
			return false;
		}
	}
	// Truncate down. Explicit array (not a braced-init-list) because
	// UINT64_MAX is `unsigned long` under glibc and `unsigned long long`
	// on macOS — both 64 bits but distinct types, which trips
	// initializer_list deduction when mixed with `ull` literals.
	uint64_t down_cases[] = {0ull, 1ull, UINT64_MAX, 0xDEADBEEFCAFEBABEull};
	for (uint64_t v : down_cases) {
		UnsignedInt a(64, v, ALICE);
		uint32_t got = a.resize(32).reveal<uint32_t>(PUBLIC);
		uint32_t want = (uint32_t)v;
		if (got != want) {
			cout << "  FAIL resize-down  v=" << v << "\n";
			return false;
		}
	}
	return true;
}

static bool check_hamming_lz() {
	for (uint32_t v : {0u, 1u, UINT32_MAX, 0x80000000u, 0x0F0F0F0Fu, 0xCAFEBABEu}) {
		UnsignedInt a(32, v, ALICE);
		uint32_t got_hw = a.hamming_weight().reveal<uint32_t>(PUBLIC);
		uint32_t want_hw = __builtin_popcount(v);
		if (got_hw != want_hw) {
			cout << "  FAIL hamming  v=" << v << " want=" << want_hw << " got=" << got_hw << "\n";
			return false;
		}
		uint32_t got_lz = a.leading_zeros().reveal<uint32_t>(PUBLIC);
		uint32_t want_lz = v == 0 ? 32 : __builtin_clz(v);
		if (got_lz != want_lz) {
			cout << "  FAIL leading_zeros  v=" << v << " want=" << want_lz << " got=" << got_lz << "\n";
			return false;
		}
	}
	return true;
}

static bool check_boundaries() {
	struct { uint32_t a, b; } cases[] = {
		{UINT32_MAX, 1}, {0, 1}, {0x80000000u, 0x80000000u},
		{UINT32_MAX, UINT32_MAX}, {0, 0},
	};
	for (auto c : cases) {
		UnsignedInt A(32, c.a, ALICE);
		UnsignedInt B(32, c.b, BOB);
		if ((A + B).reveal<uint32_t>(PUBLIC) != add_w(c.a, c.b)) return false;
		if ((A - B).reveal<uint32_t>(PUBLIC) != sub_w(c.a, c.b)) return false;
		if ((A * B).reveal<uint32_t>(PUBLIC) != mul_w(c.a, c.b)) return false;
	}
	return true;
}

static bool run_correctness() {
	cout << "=== unsigned_int correctness ===\n";
	bool ok = true;
	auto run = [&](const char *name, bool r) {
		cout << "  " << name << (r ? "  OK\n" : "  FAIL\n");
		ok &= r;
	};
	run("+ (random)",        check_random_pair("+", [](auto&a, auto&b){return a + b;}, add_w));
	run("- (random)",        check_random_pair("-", [](auto&a, auto&b){return a - b;}, sub_w));
	run("* (random)",        check_random_pair("*", [](auto&a, auto&b){return a * b;}, mul_w));
	run("/ % (random)",      check_div_random());
	run("- (unary)",         check_unary_neg());
	run("< <= > >= == !=",   check_compare_random());
	run("<<  >>  (static)",  check_shift_static());
	run("resize zero-extend",check_resize());
	run("hamming/leading_zeros", check_hamming_lz());
	run("boundary wrap",     check_boundaries());
	return ok;
}

// ---- fixed-width aliases (UInt8 / UInt16 / UInt32 / UInt64) -------------
// `UInt32` is `UnsignedInt_T<block, 32>` — width baked into the type, so
// callers don't pass it to the ctor and arithmetic preserves UInt32-ness.

static bool run_fixed_width() {
	cout << "=== fixed-width aliases ===\n";
	bool ok = true;

	// Construction without width arg.
	UInt32 a(7u, ALICE);
	UInt32 b(3u, BOB);
	UInt32 c = a + b;        // UInt32 + UInt32 -> UInt32 (type preserved)
	if (c.reveal<uint32_t>(PUBLIC) != 10u) {
		cout << "  FAIL UInt32 +\n"; ok = false;
	}

	// Default-ctor sizes the wire vector to the template width.
	UInt64 z;
	if (z.size() != 64) { cout << "  FAIL UInt64 default-ctor size\n"; ok = false; }

	// 64-bit width is implicit.
	UInt64 v(0xDEADBEEFCAFEBABEull, ALICE);
	if (v.reveal<uint64_t>(PUBLIC) != 0xDEADBEEFCAFEBABEull) {
		cout << "  FAIL UInt64 ctor + reveal\n"; ok = false;
	}

	// Sortable mixin still works (CRTP refactor).
	if (!(a >= b).reveal<bool>(PUBLIC)) { cout << "  FAIL UInt32 >=\n"; ok = false; }
	if ( (a <  b).reveal<bool>(PUBLIC)) { cout << "  FAIL UInt32 <\n";  ok = false; }
	if ( (a == b).reveal<bool>(PUBLIC)) { cout << "  FAIL UInt32 ==\n"; ok = false; }

	// Random arithmetic — same wrap semantics as the runtime-width path.
	PRG prg;
	for (int i = 0; i < 200; ++i) {
		uint32_t ia, ib;
		prg.random_data_unaligned(&ia, 4);
		prg.random_data_unaligned(&ib, 4);
		UInt32 A(ia, ALICE), B(ib, BOB);
		if ((A + B).reveal<uint32_t>(PUBLIC) != ia + ib) { ok = false; break; }
		if ((A * B).reveal<uint32_t>(PUBLIC) != ia * ib) { ok = false; break; }
		if ((A < B).reveal<bool>(PUBLIC)     != (ia < ib)) { ok = false; break; }
	}

	// emp::sort over a fixed-width array (Sortable / cmp_swap path).
	const int n = 8;
	UInt32 keys[n];
	for (int i = 0; i < n; ++i) keys[i] = UInt32((uint32_t)(n - i), ALICE);
	sort(keys, n);
	for (int i = 0; i < n; ++i)
		if (keys[i].reveal<uint32_t>(PUBLIC) != (uint32_t)(i + 1)) {
			cout << "  FAIL sort at i=" << i << "\n"; ok = false;
		}

	// Fluent-builder selection: If(cond).Then(a).Else(b).
	Bit cond_t(true, ALICE), cond_f(false, ALICE);
	UInt32 av(11u, ALICE), bv(22u, BOB);
	if (If(cond_t).Then(av).Else(bv).reveal<uint32_t>(PUBLIC) != 11u) {
		cout << "  FAIL If(true).Then.Else\n"; ok = false;
	}
	if (If(cond_f).Then(av).Else(bv).reveal<uint32_t>(PUBLIC) != 22u) {
		cout << "  FAIL If(false).Then.Else\n"; ok = false;
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
