// Test for frontend/compose.h + ir/context/compose.h: run() as an opaque call + the
// ComposePlan. Two checks per shape: (1) the plan is HIERARCHICAL — it records one
// instance per call, not the flattened gates (the win property); (2) materializing
// it (flatten_compose) computes the SAME function as inlining the units directly
// (compile inlines the units), over random inputs. Pure IR test, no backend.

#include "emp-tool/emp-tool.h"
#include "emp-tool/circuits/frontend/compose.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <random>
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

int main() {
	bool ok = true;
	auto check = [&](bool c, const char* m) { if (!c) { printf("FAIL: %s\n", m); ok = false; } };
	std::mt19937 rng(99);

	// A unit with ANDs + a const: f(s) = s*3 + 1  (UInt8, truncating).
	auto unit = cf::compile_linear<R8>([](auto s) { return s * s.constant(3u) + s.constant(1u); });
	check(unit.program().wire_reuse == circuit::WireReuse::Linear, "unit not Linear");

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

	if (ok) printf("test_compose: all checks passed\n");
	return ok ? 0 : 1;
}
