// Test for ir/passes.h — count / liveness / schedule / layout over a
// BooleanProgram. Two layers of checking: a tiny hand-built program where every
// stat is verified against a worked-by-hand answer, then a larger RecordCtx
// program where the structural properties (frees is the exact inverse of
// last_use, outputs are never freed, DCE reach matches an independent walk)
// must hold wholesale.

#include "emp-tool/ir/program.h"
#include "emp-tool/ir/passes.h"
#include "emp-tool/ir/validate.h"
#include "emp-tool/ir/context/record.h"
#include "emp-tool/ir/transform.h"     // make_compact (for the wire_reuse guard test)
#include <cstdio>
#include <vector>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace emp;
using namespace emp::circuit;

// error() is fatal (_Exit), so "this pass refused the input" shows up as child
// death. Run f in a fork with stderr silenced and report whether it died.
template <class F>
static bool dies(F&& f) {
	pid_t pid = fork();
	if (pid == 0) { int dn = open("/dev/null", O_WRONLY); if (dn >= 0) dup2(dn, 2); f(); _exit(0); }
	int st = 0; waitpid(pid, &st, 0);
	return !(WIFEXITED(st) && WEXITSTATUS(st) == 0);
}

// 3 inputs a,b,c (wires 0..2); a live cone and a dead AND:
//   g0: w3 = a AND b
//   g1: w4 = w3 XOR c
//   g2: w5 = NOT w4          <- the only output
//   g3: w6 = Const1
//   g4: w7 = c AND w6        <- dead: w7 is never read and not an output
static BooleanProgram sample() {
	BooleanProgram p;
	p.num_wires  = 8;
	p.num_inputs = 3;
	p.gates = {
		Gate{0, 1, 3, Op::And},
		Gate{3, 2, 4, Op::Xor},
		Gate{4, 0, 5, Op::Not},
		Gate{0, 0, 6, Op::Const1},
		Gate{2, 6, 7, Op::And},
	};
	p.outputs = {5};
	return p;
}

int main() {
	bool ok = true;
	auto check = [&](bool c, const char* msg) {
		if (!c) { printf("FAIL: %s\n", msg); ok = false; }
	};

	BooleanProgram p = sample();
	validate_program(p);

	// ---- count_pass: every counter, by hand ----
	{
		CountStats s = count_pass(p);
		check(s.num_and == 2 && s.num_xor == 1 && s.num_not == 1 && s.num_const == 1,
		      "count_pass: op counts");
		check(s.num_wire == 8 && s.num_gate == 5, "count_pass: dims");
		check(s.num_input_bits == 3 && s.num_output_bits == 1, "count_pass: io bits");
	}

	// ---- liveness_pass: per-wire last_use / first_def, by hand ----
	{
		LivenessStats s = liveness_pass(p);
		const int64_t lu[8] = {0, 0, 4, 1, 2, -1, 4, -1};   // w5/w7 never read
		const int64_t fd[8] = {-1, -1, -1, 0, 1, 2, 3, 4};  // inputs have no producer
		for (int w = 0; w < 8; ++w) {
			check(s.last_use[w] == lu[w],  "liveness_pass: last_use");
			check(s.first_def[w] == fd[w], "liveness_pass: first_def");
		}
	}

	// ---- schedule_pass: AND-depth levels, by hand ----
	{
		ScheduleStats s = schedule_pass(p);
		check(s.levels.depth == 1, "schedule_pass: depth");
		const int lv[8] = {0, 0, 0, 1, 1, 1, 0, 1};
		for (int w = 0; w < 8; ++w)
			check(s.wire_level[w] == lv[w], "schedule_pass: wire_level");
		check(s.levels.and_gate_indices[0].empty(), "schedule_pass: level-0 ANDs");
		check(s.levels.and_gate_indices[1] == std::vector<uint32_t>({0, 4}),
		      "schedule_pass: level-1 ANDs");
	}

	// ---- layout_pass: frees + DCE, by hand ----
	{
		LayoutStats s = layout_pass(p, liveness_pass(p));
		check(s.frees[0] == std::vector<uint32_t>({0, 1}), "layout_pass: frees[0]");
		check(s.frees[1] == std::vector<uint32_t>({3}),    "layout_pass: frees[1]");
		check(s.frees[2] == std::vector<uint32_t>({4}),    "layout_pass: frees[2]");
		check(s.frees[3].empty(),                          "layout_pass: frees[3]");
		check(s.frees[4] == std::vector<uint32_t>({2, 6}), "layout_pass: frees[4]");
		// Live cone of w5 is {0,1,2,3,4,5}; the dead AND (g4 -> w7) and its
		// const operand stay out.
		check(s.reachable_wire == 6, "layout_pass: reachable_wire");
		check(s.reachable_and  == 1, "layout_pass: reachable_and");
	}

	// ---- structural properties on a recorded program ----
	{
		// A real recorded body: a multiplier-ish live chain plus a dead branch.
		RecordCtx rc;
		uint32_t base = rc.external_input(8);
		std::vector<uint32_t> w(8);
		for (int i = 0; i < 8; ++i) w[i] = base + i;
		uint32_t acc = rc.and_gate(w[0], w[1]);
		for (int i = 2; i < 8; ++i) {
			acc = rc.xor_gate(acc, rc.and_gate(acc, w[i]));
			acc = rc.not_gate(acc);
		}
		uint32_t dead = rc.and_gate(w[0], rc.public_bit(true));   // never read
		dead = rc.xor_gate(dead, w[7]);                            // still dead
		(void)dead;
		uint32_t outs[1] = {acc};
		const BooleanProgram& q = rc.finish(outs);

		LivenessStats live = liveness_pass(q);
		LayoutStats   lay  = layout_pass(q, live);

		// frees must be the EXACT inverse of last_use restricted to non-outputs:
		// each such wire appears once, at frees[last_use[w]], and nothing else.
		std::vector<char> is_output(q.num_wires, 0);
		for (uint32_t o : q.outputs) is_output[o] = 1;
		std::vector<int64_t> freed_at(q.num_wires, -1);
		size_t total_freed = 0;
		for (size_t gi = 0; gi < lay.frees.size(); ++gi)
			for (uint32_t fw : lay.frees[gi]) {
				check(freed_at[fw] == -1, "layout property: wire freed twice");
				freed_at[fw] = (int64_t)gi;
				++total_freed;
			}
		size_t expect_freed = 0;
		for (uint32_t wi = 0; wi < q.num_wires; ++wi) {
			if (live.last_use[wi] >= 0 && !is_output[wi]) {
				++expect_freed;
				check(freed_at[wi] == live.last_use[wi],
				      "layout property: freed at wrong gate");
			} else {
				check(freed_at[wi] == -1,
				      "layout property: output or never-read wire freed");
			}
		}
		check(total_freed == expect_freed, "layout property: frees cardinality");

		// DCE reach must match an independent backward walk.
		std::vector<char> reach(q.num_wires, 0);
		std::vector<uint32_t> stk(q.outputs.begin(), q.outputs.end());
		for (uint32_t o : q.outputs) reach[o] = 1;
		while (!stk.empty()) {
			uint32_t v = stk.back(); stk.pop_back();
			if (v < q.num_inputs) continue;
			const Gate& g = q.gates[v - q.num_inputs];   // record-canonical layout
			auto push = [&](uint32_t u) { if (!reach[u]) { reach[u] = 1; stk.push_back(u); } };
			if (!g.is_const()) push(g.in0);
			if (!g.is_const() && !g.is_not()) push(g.in1);
		}
		int64_t rw = 0, ra = 0;
		for (uint32_t wi = 0; wi < q.num_wires; ++wi) rw += reach[wi] ? 1 : 0;
		for (const Gate& g : q.gates) ra += (g.is_and() && reach[g.out]) ? 1 : 0;
		check(lay.reachable_wire == rw, "layout property: reachable_wire");
		check(lay.reachable_and  == ra, "layout property: reachable_and");
		check(lay.reachable_and < count_pass(q).num_and,
		      "layout property: dead AND not excluded");

		// Schedule sanity on the recorded program: every AND is bucketed exactly
		// once and at the level of its output wire.
		ScheduleStats sch = schedule_pass(q);
		int64_t bucketed = 0;
		for (int lv = 0; lv <= sch.levels.depth; ++lv)
			for (uint32_t gi : sch.levels.and_gate_indices[lv]) {
				check(sch.wire_level[q.gates[gi].out] == lv,
				      "schedule property: AND bucketed at wrong level");
				++bucketed;
			}
		check(bucketed == count_pass(q).num_and, "schedule property: AND bucket count");
	}

	// ---- the dense-only passes refuse a wire_reuse program ----
	// (they index per-wire arrays last-writer-wins; on a reused id they would
	// silently miscompute, so the guard turns that into a loud failure).
	{
		BooleanProgram lin = make_compact(sample(), WireReuse::Linear).prog;
		check(lin.wire_reuse != WireReuse::None, "make_compact should produce a reuse program");
		check(dies([&] { liveness_pass(lin); }), "liveness_pass accepted a reuse program");
		check(dies([&] { schedule_pass(lin); }), "schedule_pass accepted a reuse program");
		check(dies([&] { layout_pass(lin, LivenessStats{}); }), "layout_pass accepted a reuse program");
	}

	if (ok) printf("test_passes: all checks passed\n");
	return ok ? 0 : 1;
}
