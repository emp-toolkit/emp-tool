// Test for frontend/compose.h + ir/context/compose.h: run() as an opaque call + the
// ComposePlan. Two checks per shape: (1) the plan is HIERARCHICAL — it records one
// instance per call, not the flattened gates (the win property); (2) materializing
// it (flatten_compose) computes the SAME function as inlining the units directly
// (compile inlines the units), over random inputs. Pure IR test, no backend.

#include "emp-tool/emp-tool.h"
#include "emp-tool/circuits/frontend/compose.h"
#include <array>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <random>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace emp;
namespace cf = emp::frontend;
using R8 = UInt_T<RecordCtx, 8>;

struct ByteD {
	void and_gate(uint8_t& o, const uint8_t& a, const uint8_t& b) { o = a & b; }
	void xor_gate(uint8_t& o, const uint8_t& a, const uint8_t& b) { o = a ^ b; }
	void not_gate(uint8_t& o, const uint8_t& a)                   { o = a ^ 1; }
	void const_gate(uint8_t& o, bool v)                           { o = v ? 1 : 0; }
};

static std::vector<uint8_t> run_bytes(const circuit::BooleanProgram& p, const std::vector<uint8_t>& in) {
	std::vector<uint8_t> out(p.outputs.size());
	circuit::CircuitScratch<uint8_t> sc;
	execute_program<uint8_t>(p, in.data(), in.size(), out.data(), out.size(), sc, ByteD{});
	return out;
}

template <class F>
static bool dies(F&& f) {
	pid_t pid = fork();
	if (pid < 0) return false;
	if (pid == 0) {
		(void)std::freopen("/dev/null", "w", stderr);
		f();
		std::_Exit(0);
	}
	int status = 0;
	return waitpid(pid, &status, 0) == pid &&
	       (!WIFEXITED(status) || WEXITSTATUS(status) != 0);
}

int main() {
	bool ok = true;
	auto check = [&](bool c, const char* m) { if (!c) { printf("FAIL: %s\n", m); ok = false; } };
	std::mt19937 rng(99);

	// A unit with ANDs + a const: f(s) = s*3 + 1  (UInt8, truncating).
	auto unit = cf::compile_linear<R8>([](auto s) { return s * s.constant(3u) + s.constant(1u); });
	check(unit.program().wire_reuse == circuit::WireReuse::Linear, "unit not Linear");

	// The raw plan stays public. Generic flattening fully validates it before
	// indexing; these mutations exercise that boundary independently of typed
	// frontend ownership checks.
	{
		auto one_call = [&](auto& ctx, auto s) { return cf::run(ctx, unit, s); };
		ComposePlan good = cf::compose<R8>(one_call);
		validate_compose(good);
		auto rejects = [&](auto mutate, const char* what) {
			ComposePlan bad = good;
			mutate(bad);
			check(dies([&] { (void)flatten_compose(bad); }), what);
		};

		rejects([](ComposePlan& p) { p.events[0].instance = -2; },
		        "compose: invalid negative event tag accepted");
		rejects([](ComposePlan& p) { p.events[0].instance = (int)p.instances.size(); },
		        "compose: out-of-range instance event accepted");
		rejects([](ComposePlan& p) { p.events.push_back(p.events[0]); },
		        "compose: duplicate instance event accepted");
		rejects([](ComposePlan& p) { p.instances[0].in_ids[0] = p.num_wires; },
		        "compose: undefined instance input accepted");
		rejects([](ComposePlan& p) { p.instances[0].out_ids.pop_back(); },
		        "compose: mismatched instance output width accepted");
		rejects([](ComposePlan& p) { p.instances[0].unit.reset(); },
		        "compose: null instance unit accepted");
		rejects([](ComposePlan& p) { ++p.num_wires; },
		        "compose: inconsistent num_wires accepted");
		rejects([](ComposePlan& p) { p.outputs[0] = p.num_wires; },
		        "compose: undefined plan output accepted");
		rejects([](ComposePlan& p) {
			auto invalid = std::make_shared<circuit::BooleanProgram>(*p.instances[0].unit);
			invalid->outputs[0] = invalid->num_wires;
			p.instances[0].unit = std::move(invalid);
		}, "compose: malformed unit program accepted");

		ComposePlan glue;
		glue.num_inputs = 2;
		glue.num_wires = 3;
		glue.events.push_back(ComposeEvent{{0, 1, 2, circuit::Op::Xor}, -1});
		glue.outputs = {2};
		validate_compose(glue);
		glue.events[0].gate.in0 = 3;
		check(dies([&] { validate_compose_structure(glue); }),
		      "compose: forward glue input accepted");

		check(dies([] {
			ComposeCtx ctx;
			ComposeCtx::Wire input = ctx.external_input(1);
			ComposeCtx::Wire undefined = input + 1;
			ctx.finish(std::span<const ComposeCtx::Wire>(&undefined, 1));
		}), "ComposeCtx::finish accepted an undefined output");
	}

	// ---- chain: call the unit 3 times, output->input ----
	{
		auto body = [&](auto& ctx, auto s) {
			for (int i = 0; i < 3; ++i) s = cf::run(ctx, unit, s);
			return s;
		};
		auto inlined = cf::compile<R8>(body).program();      // run inlines (flat)
		ComposePlan plan = cf::compose<R8>(body);            // run is opaque -> instances
		auto flat = flatten_compose(plan);

		check(plan.instances.size() == 3, "chain: expected 3 recorded instances");
		check(plan.events.size() == 3, "chain: plan should be O(#instances), not flattened gates");
		// flat inlines each instance's gates incl. its own consts; `inlined` (compile)
		// dedups consts across instances, so flat has >= as many gates. Function is what matters.
		check(flat.gates.size() >= inlined.gates.size(), "chain: flatten smaller than inline");
		for (int t = 0; t < 6; ++t) {
			std::vector<uint8_t> in(8); for (auto& x : in) x = rng() & 1;
			check(run_bytes(flat, in) == run_bytes(inlined, in), "chain: flatten != inline");
		}
	}

	// ---- SIMD + glue: two independent instances, results added (glue gates) ----
	{
		auto body = [&](auto& ctx, auto a, auto b) {
			auto x = cf::run(ctx, unit, a);
			auto y = cf::run(ctx, unit, b);
			return x + y;
		};
		auto inlined = cf::compile<R8, R8>(body).program();
		ComposePlan plan = cf::compose<R8, R8>(body);
		auto flat = flatten_compose(plan);

		check(plan.instances.size() == 2, "simd: expected 2 instances");
		check(flat.gates.size() >= inlined.gates.size(), "simd: flatten smaller than inline");
		for (int t = 0; t < 6; ++t) {
			std::vector<uint8_t> in(16); for (auto& x : in) x = rng() & 1;
			check(run_bytes(flat, in) == run_bytes(inlined, in), "simd: flatten != inline");
		}
	}

	// ---- lifetime: the plan co-owns its units; flatten after they die ----
	{
		std::vector<uint8_t> in(8); for (auto& x : in) x = rng() & 1;
		std::vector<uint8_t> want;
		ComposePlan plan;
		{
			// Build the unit and plan in an inner scope, then let the unit
			// Circuit go out of scope BEFORE flattening. The plan must keep
			// the unit program alive — a non-owning pointer would dangle here
			// (an ASan use-after-free on the sanitize leg).
			auto local_unit = cf::compile_linear<R8>(
			    [](auto s) { return s * s.constant(3u) + s.constant(1u); });
			auto body = [&](auto& ctx, auto s) {
				for (int i = 0; i < 3; ++i) s = cf::run(ctx, local_unit, s);
				return s;
			};
			want = run_bytes(cf::compile<R8>(body).program(), in);
			plan = cf::compose<R8>(body);
		}
		auto flat = flatten_compose(plan);
		check(plan.instances.size() == 3, "lifetime: expected 3 instances");
		check(run_bytes(flat, in) == want, "lifetime: flatten after unit scope != inline");
	}

	// A composition body may not return a value from another ComposeCtx. This
	// is checked once while finishing the plan, not while replaying its gates.
	{
		ComposeCtx other;
		std::array<ComposeCtx::Wire, 8> wires{};
		ComposeCtx::Wire base = other.external_input(wires.size());
		for (std::size_t i = 0; i < wires.size(); ++i)
			wires[i] = base + static_cast<ComposeCtx::Wire>(i);
		auto foreign = UInt_T<ComposeCtx, 8>::from_wires(other, wires.data());
		check(dies([&] {
			(void)cf::compose<R8>([&](auto& ctx, auto value) {
				(void)ctx;
				(void)value;
				return foreign;
			});
		}), "compose: foreign-context return accepted");
	}

	if (ok) printf("test_compose: all checks passed\n");
	return ok ? 0 : 1;
}
