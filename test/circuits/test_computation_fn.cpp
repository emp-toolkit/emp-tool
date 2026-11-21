// circuits/frontend/computation_fn.h — circuit mode, typed: a compiled Circuit
// plus a role for its return value and for each argument, prepared offline and
// run online through a ComputationSession. Read example() first; the rest is
// verification, against frontend::run(ctx, circuit, ...) over ClearCtx.
//
// What's in computation_fn.h:
//   to(recipient) / from(owner)          roles: a party id >= 1 or PUBLIC
//   computation(circ, to(r), from(o)...) Computation<RetV, ArgVs...>
//   Computation::handles()               the return's handle, then one per argument
//   Computation::artifact() / id()       the shared ComputationArtifact / identity
//   bind(input_handle, value)            one encoded input value
//   prepare(sess, comp)                  Job: offline, single-use, tied to sess
//   run(sess, std::move(job), binds...)  Result: online
//   Result[output_handle]                std::optional value, where received

#include "emp-tool/emp-tool.h"
#include <cstdint>
#include <cstdio>
#include <optional>
#include <random>
#include <type_traits>
#include <utility>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace emp;
namespace cf = emp::frontend;
using Ctx = ClearSession::ctx_t;

static int fails = 0;
static void check(bool ok, const char* what) {
	if (!ok) { std::printf("  [FAIL] %s\n", what); ++fails; }
}

// A rejection is fatal by design (docs/api_conventions.md), so it shows up as
// the death of a child process with stderr silenced.
template <class F>
static bool dies(F&& f) {
	pid_t pid = fork();
	if (pid < 0) return false;
	if (pid == 0) {
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) dup2(devnull, 2);
		f();
		_exit(0);
	}
	int st = 0;
	waitpid(pid, &st, 0);
	return !(WIFEXITED(st) && WEXITSTATUS(st) == 0);
}

static void example() {
	std::printf("== example ==\n");
	auto mix = cf::compile<rec::UInt<32>, rec::UInt<32>>(
	    [](auto x, auto k) { return (x + k) ^ (x & k); });
	// x comes from ALICE, k is public, and the result goes to BOB.
	auto comp = cf::computation(mix, cf::to(BOB), cf::from(ALICE), cf::from(PUBLIC));
	auto [out, x_in, k_in] = comp.handles();

	ClearSession sess;                       // the plaintext reference: one process holds every party
	auto job = cf::prepare(sess, comp);      // offline: every role is known
	auto res = cf::run(sess, std::move(job), cf::bind(x_in, 1000u), cf::bind(k_in, 77u));
	std::printf("  (1000 + 77) ^ (1000 & 77) = %u\n", (unsigned)*res[out]);
}

// Each run's output equals the same circuit run in direct mode on another
// ClearSession: inputs through the session, frontend::run on its context,
// reveal.
static void sweep_oracle() {
	std::printf("== against direct mode ==\n");
	std::mt19937_64 rng(77);

	auto add8 = cf::compile<rec::UInt<8>, rec::UInt<8>>([](auto a, auto b) { return a + b; });
	auto mul16 = cf::compile<rec::Int<16>, rec::Int<16>, rec::Int<16>>(
	    [](auto a, auto b, auto c) { return (a - b) * c; });
	auto lt = cf::compile<rec::UInt<32>, rec::UInt<32>>([](auto a, auto b) { return a < b; });
	auto mask = cf::compile<rec::BitVec<128>, rec::BitVec<128>>([](auto v, auto w) { return (v ^ w) & ~v; });
	auto seven = cf::compile<>([](auto& ctx) { return UInt_T<RecordCtx, 8>::constant(ctx, 7); });

	ClearSession sess;   // circuit mode
	ClearSession ds;     // direct mode, the oracle
	using U8 = UInt_T<Ctx, 8>;
	using I16 = Int_T<Ctx, 16>;
	using V128 = BitVec_T<Ctx, 128>;
	for (int t = 0; t < 32; ++t) {
		const uint8_t a = (uint8_t)rng(), b = (uint8_t)rng();
		auto c = cf::computation(add8, cf::to(PUBLIC), cf::from(ALICE), cf::from(BOB));
		auto [o, ia, ib] = c.handles();
		auto r = cf::run(sess, cf::prepare(sess, c), cf::bind(ia, a), cf::bind(ib, b));
		const auto want = *ds.reveal(cf::run(ds.ctx(), add8, ds.input<U8>(ALICE, a), ds.input<U8>(BOB, b)), PUBLIC);
		check(r[o].has_value() && *r[o] == want, "UInt<8> add");
	}
	for (int t = 0; t < 32; ++t) {
		const int16_t a = (int16_t)rng(), b = (int16_t)rng(), m = (int16_t)rng();
		auto c = cf::computation(mul16, cf::to(3), cf::from(1), cf::from(2), cf::from(4));
		auto [o, ia, ib, ic] = c.handles();
		auto job = cf::prepare(sess, c);
		auto bb = cf::bind(ib, b);   // an lvalue binding
		auto r = cf::run(sess, std::move(job), cf::bind(ia, a), bb, cf::bind(ic, m));
		const auto want = *ds.reveal(cf::run(ds.ctx(), mul16, ds.input<I16>(1, a), ds.input<I16>(2, b),
		                                     ds.input<I16>(4, m)), 3);
		check(r[o].has_value() && *r[o] == want, "Int<16> four-party expression");
	}
	for (int t = 0; t < 32; ++t) {
		const uint32_t a = (uint32_t)rng(), b = (uint32_t)rng();
		auto c = cf::computation(lt, cf::to(ALICE), cf::from(PUBLIC), cf::from(PUBLIC));
		auto [o, ia, ib] = c.handles();
		auto r = cf::run(sess, cf::prepare(sess, c), cf::bind(ib, b), cf::bind(ia, a));   // any order
		check(r[o].has_value() && *r[o] == (a < b), "Bit comparison with Public inputs");
	}
	{
		V128::clear_t v{}, w{};
		for (auto& x : v) x = (rng() & 1) != 0;
		for (auto& x : w) x = (rng() & 1) != 0;
		auto c = cf::computation(mask, cf::to(BOB), cf::from(ALICE), cf::from(PUBLIC));
		auto [o, iv, iw] = c.handles();
		auto r = cf::run(sess, cf::prepare(sess, c), cf::bind(iv, v), cf::bind(iw, w));
		const auto want = *ds.reveal(cf::run(ds.ctx(), mask, ds.input<V128>(ALICE, v), ds.input<V128>(PUBLIC, w)), BOB);
		check(r[o].has_value() && *r[o] == want, "BitVec<128> mask");
	}
	{
		auto c = cf::computation(seven, cf::to(PUBLIC));
		auto [o] = c.handles();
		auto r = cf::run(sess, cf::prepare(sess, c));
		check(r[o].has_value() && *r[o] == 7, "a nullary computation");
	}
	{
		// One Computation serves several jobs, and copies share its identity.
		auto c = cf::computation(add8, cf::to(BOB), cf::from(ALICE), cf::from(ALICE));
		auto copy = c;
		check(copy.id() == c.id() && copy.artifact() == c.artifact(), "a copy has its own identity");
		auto [o, ia, ib] = c.handles();
		for (int t = 0; t < 4; ++t) {
			auto r = cf::run(sess, cf::prepare(sess, copy), cf::bind(ia, (uint8_t)t), cf::bind(ib, (uint8_t)1));
			check(*r[o] == (uint8_t)(t + 1), "a computation run several times");
		}
	}
}

static void sweep_roles_and_artifact() {
	std::printf("== roles and the artifact ==\n");
	auto add8 = cf::compile<rec::UInt<8>, rec::UInt<8>>([](auto a, auto b) { return a + b; });
	auto c = cf::computation(add8, cf::to(BOB), cf::from(ALICE), cf::from(PUBLIC));
	const circuit::ComputationArtifact& a = *c.artifact();
	check(a.io.inputs.size() == 2 && a.io.outputs.size() == 1, "one slot per argument and one for the return");
	check(a.io.inputs[0].kind == circuit::InputKind::Party && a.io.inputs[0].owner == ALICE &&
	      a.io.inputs[0].width == 8, "from(ALICE)");
	check(a.io.inputs[1].kind == circuit::InputKind::Public && a.io.inputs[1].owner == PUBLIC, "from(PUBLIC)");
	check(a.io.outputs[0].kind == circuit::OutputKind::Party && a.io.outputs[0].recipient == BOB &&
	      a.io.outputs[0].width == 8, "to(BOB)");
	check(a.program.get() == add8.program_shared().get(), "the computation shares the circuit's program");
	auto c2 = cf::computation(add8, cf::to(BOB), cf::from(ALICE), cf::from(PUBLIC));
	check(c2.id() != c.id(), "two computations share an identity");
	check(circuit::canonical_encoding(*c2.artifact()) == circuit::canonical_encoding(a),
	      "two identical computations encode differently");

	check(dies([&] { (void)cf::computation(add8, cf::to(XOR), cf::from(ALICE), cf::from(BOB)); }),
	      "the XOR sentinel is accepted as a recipient");
	check(dies([&] { (void)cf::computation(add8, cf::to(BOB), cf::from(-2), cf::from(BOB)); }),
	      "a negative owner is accepted");
}

static void sweep_identity() {
	std::printf("== identity and single use ==\n");
	using Job = decltype(cf::prepare(std::declval<ClearSession&>(),
	    cf::computation(cf::compile<rec::Bit>([](auto b) { return !b; }), cf::to(PUBLIC), cf::from(ALICE))));
	static_assert(!std::is_copy_constructible_v<Job> && std::is_move_constructible_v<Job>);

	auto inv = cf::compile<rec::Bit>([](auto b) { return !b; });
	auto c = cf::computation(inv, cf::to(PUBLIC), cf::from(ALICE));
	auto other = cf::computation(inv, cf::to(PUBLIC), cf::from(ALICE));
	auto [o, i] = c.handles();
	auto [oo, oi] = other.handles();

	check(!dies([&] {
		ClearSession s;
		auto r = cf::run(s, cf::prepare(s, c), cf::bind(i, true));
		if (*r[o] != false) _exit(1);
	}), "an ordinary run fails");
	check(dies([&] {
		ClearSession s1, s2;
		auto job = cf::prepare(s1, c);
		(void)cf::run(s2, std::move(job), cf::bind(i, true));
	}), "a job runs on another session");
	check(dies([&] {
		ClearSession s;
		auto job = cf::prepare(s, c);
		auto moved = std::move(job);
		(void)cf::run(s, std::move(moved), cf::bind(i, true));
		(void)cf::run(s, std::move(job), cf::bind(i, true));   // spent by the move
	}), "a moved-from job runs");
	check(dies([&] {
		ClearSession s;
		(void)cf::run(s, cf::prepare(s, c), cf::bind(oi, true));
	}), "a handle of another computation binds");
	check(dies([&] {
		ClearSession s;
		(void)cf::run(s, cf::prepare(s, c), cf::bind(i, true), cf::bind(i, false));
	}), "a slot is bound twice");
	check(dies([&] {
		ClearSession s;
		(void)cf::run(s, cf::prepare(s, c));
	}), "a run leaves a slot unbound on a whole-process session");
	check(dies([&] {
		ClearSession s;
		auto r = cf::run(s, cf::prepare(s, c), cf::bind(i, true));
		(void)r[oo];
	}), "a result is read with another computation's handle");
}

int main() {
	example();
	sweep_oracle();
	sweep_roles_and_artifact();
	sweep_identity();
	if (fails) { std::printf("FAIL: %d check(s)\n", fails); return 1; }
	std::printf("OK\n");
	return 0;
}
