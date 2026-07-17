// ir/analysis.h — aggregate BooleanProgram statistics for tools and manifests.
// This test checks counts, reachability, depth, liveness, fanout, and compact
// versus dense representation reporting against a hand-built program.

#include "emp-tool/ir/analysis.h"

#include <iostream>

using namespace emp::circuit;

static int failures = 0;

static void check(bool ok, const char *what) {
	std::cout << "  " << what << "  " << (ok ? "OK" : "FAIL") << "\n";
	if (!ok) ++failures;
}

static BooleanProgram example_program() {
	BooleanProgram p;
	p.num_inputs = 2;
	p.num_wires = 5;
	p.gates = {
		{0, 1, 2, Op::And},
		{0, 1, 3, Op::Xor},  // intentionally unreachable from the output
		{2, 0, 4, Op::Not},
	};
	p.outputs = {4};
	validate_program(p);
	return p;
}

int main() {
	std::cout << "=== analysis ===\n";
	BooleanProgram dense = example_program();
	ProgramStats s = analyze_program(dense);
	check(s.num_inputs == 2 && s.num_outputs == 1, "I/O widths");
	check(s.num_gates == 3 && s.num_and == 1 && s.num_xor == 1 &&
	      s.num_not == 1 && s.num_const == 0, "gate counts");
	check(s.and_depth == 1 && s.max_and_per_level == 1, "AND schedule");
	check(s.reachable_wires == 4 && s.reachable_gates == 2 &&
	      s.reachable_and == 1, "output-rooted reachability");
	check(s.peak_live_wires == 4, "peak live wires");
	check(s.max_fanout == 2, "maximum fanout");
	check(s.stored_num_wires == 5 && s.dense_num_wires == 5,
	      "dense representation sizes");

	BooleanProgram compact = make_compact(dense, WireReuse::Full).prog;
	ProgramStats c = analyze_program(compact);
	check(c.wire_reuse == WireReuse::Full, "wire-reuse mode");
	check(c.dense_num_wires == 5 && c.num_gates == s.num_gates,
	      "compact logical expansion");
	check(c.num_and == s.num_and && c.and_depth == s.and_depth &&
	      c.reachable_gates == s.reachable_gates,
	      "compact logical statistics");

	return failures == 0 ? 0 : 1;
}
