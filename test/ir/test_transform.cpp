// Test for ir/transform.h: make_compact (Linear/Full) preserves the function
// computed (execute-compare vs the dense original), compacts the wire count,
// roundtrips through .empbc, and uncompact inverts it. Pure IR test, no backend.

#include "emp-tool/ir/program.h"
#include "emp-tool/ir/validate.h"
#include "emp-tool/ir/execute.h"
#include "emp-tool/ir/empbc.h"
#include "emp-tool/ir/transform.h"
#include "emp-tool/ir/builtins.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace emp::circuit;

template <class F>
static bool dies(F&& f) {
	pid_t pid = fork();
	if (pid == 0) { int dn = open("/dev/null", O_WRONLY); if (dn >= 0) dup2(dn, 2); f(); _exit(0); }
	int st = 0; waitpid(pid, &st, 0);
	return !(WIFEXITED(st) && WEXITSTATUS(st) == 0);
}

struct ByteD {
	void and_gate(uint8_t& o, const uint8_t& a, const uint8_t& b) { o = a & b; }
	void xor_gate(uint8_t& o, const uint8_t& a, const uint8_t& b) { o = a ^ b; }
	void not_gate(uint8_t& o, const uint8_t& a)                   { o = a ^ 1; }
	void const_gate(uint8_t& o, bool v)                           { o = v ? 1 : 0; }
};

static std::vector<uint8_t> run_bytes(const BooleanProgram& p, const std::vector<uint8_t>& in) {
	std::vector<uint8_t> out(p.outputs.size());
	CircuitScratch<uint8_t> sc;
	execute_program<uint8_t>(p, in.data(), in.size(), out.data(), out.size(), sc, ByteD{});
	return out;
}

// random dense circuit: N gates, ~1/6 AND, operands within a window W of earlier
// wires, last OUTW wires are outputs.
static BooleanProgram rand_circuit(uint32_t NIN, uint32_t N, int W, int OUTW, std::mt19937& rng) {
	BooleanProgram p; p.num_inputs = NIN; p.num_wires = NIN + N; p.gates.reserve(N);
	for (uint32_t g = 0; g < N; ++g) {
		uint32_t id = NIN + g; int span = (int)std::min<uint32_t>(W, id);
		uint32_t a = id - 1 - (rng() % span), b = id - 1 - (rng() % span);
		Op op; uint32_t r = rng() % 12;
		if (r < 2) op = Op::And; else if (r < 10) op = Op::Xor; else if (r < 11) op = Op::Not;
		else op = (rng() & 1) ? Op::Const1 : Op::Const0;
		Gate gt; gt.op = op; gt.out = id;
		gt.in0 = (op == Op::Const0 || op == Op::Const1) ? 0 : a;
		gt.in1 = (op == Op::Const0 || op == Op::Const1 || op == Op::Not) ? 0 : b;
		p.gates.push_back(gt);
	}
	for (int i = 0; i < OUTW; ++i) p.outputs.push_back(p.num_wires - 1 - i);
	return p;
}

int main() {
	bool ok = true;
	auto check = [&](bool c, const char* m) { if (!c) { printf("FAIL: %s\n", m); ok = false; } };
	std::mt19937 rng(2024);

	// ---- random dense circuits: compaction preserves function + compacts wires ----
	for (int t = 0; t < 8; ++t) {
		BooleanProgram dense = rand_circuit(64, 4000, 256, 64, rng);
		CompactResult lin = make_compact(dense, WireReuse::Linear);
		CompactResult ful = make_compact(dense, WireReuse::Full);
		check(lin.prog.wire_reuse == WireReuse::Linear, "Linear level not set");
		check(ful.prog.wire_reuse == WireReuse::Full, "Full level not set");
		check(ful.prog.num_wires <= lin.prog.num_wires, "Full should compact <= Linear");
		check(lin.prog.num_wires < dense.num_wires, "Linear should compact below dense");
		for (int k = 0; k < 4; ++k) {
			std::vector<uint8_t> in(dense.num_inputs);
			for (auto& x : in) x = rng() & 1;
			std::vector<uint8_t> o0 = run_bytes(dense, in);
			check(run_bytes(lin.prog, in) == o0, "Linear compaction changed outputs");
			check(run_bytes(ful.prog, in) == o0, "Full compaction changed outputs");
			check(run_bytes(uncompact(lin.prog), in) == o0, "uncompact(Linear) changed outputs");
		}
	}

	// ---- compact .empbc roundtrip preserves level + behavior ----
	{
		std::mt19937 r2(7);
		BooleanProgram dense = rand_circuit(32, 2000, 128, 32, r2);
		BooleanProgram lin = make_compact(dense, WireReuse::Linear).prog;
		BooleanProgram q = load_empbc(save_empbc(lin));
		check(q.wire_reuse == WireReuse::Linear, "compact .empbc roundtrip lost level");
		std::vector<uint8_t> in(dense.num_inputs, 1);
		check(run_bytes(q, in) == run_bytes(dense, in), "compact .empbc roundtrip changed outputs");
	}

	// ---- dead-on-arrival outputs recycle immediately; a surviving linear output
	//      may claim that free slot instead of forcing a fresh one ----
	{
		BooleanProgram dense;
		dense.num_inputs = 1;
		dense.num_wires = 4;
		dense.gates = {
			{0, 0, 1, Op::Not},   // dead immediately
			{0, 0, 2, Op::Not},   // dead immediately
			{0, 0, 3, Op::Not},   // returned
		};
		dense.outputs = {3};
		BooleanProgram lin = make_compact(dense, WireReuse::Linear).prog;
		BooleanProgram ful = make_compact(dense, WireReuse::Full).prog;
		check(lin.num_wires == 2 && ful.num_wires == 2,
		      "dead linear results should reuse one non-input slot");
		for (uint8_t bit : {uint8_t{0}, uint8_t{1}}) {
			std::vector<uint8_t> in{bit};
			check(run_bytes(lin, in) == run_bytes(dense, in),
			      "dead-result Linear compaction changed outputs");
			check(run_bytes(ful, in) == run_bytes(dense, in),
			      "dead-result Full compaction changed outputs");
		}
	}

	// A Linear AND output still needs a never-used slot, even when a dead linear
	// result made a slot available. Full may reuse it.
	{
		BooleanProgram dense;
		dense.num_inputs = 1;
		dense.num_wires = 3;
		dense.gates = {
			{0, 0, 1, Op::Not},
			{0, 0, 2, Op::And},
		};
		dense.outputs = {2};
		BooleanProgram lin = make_compact(dense, WireReuse::Linear).prog;
		BooleanProgram ful = make_compact(dense, WireReuse::Full).prog;
		check(lin.num_wires == 3 && lin.gates[0].out != lin.gates[1].out,
		      "Linear AND output should receive a fresh slot");
		check(ful.num_wires == 2 && ful.gates[0].out == ful.gates[1].out,
		      "Full AND output should reuse the dead linear slot");
		for (uint8_t bit : {uint8_t{0}, uint8_t{1}}) {
			std::vector<uint8_t> in{bit};
			check(run_bytes(lin, in) == run_bytes(dense, in),
			      "fresh-AND Linear compaction changed outputs");
			check(run_bytes(ful, in) == run_bytes(dense, in),
			      "reused-AND Full compaction changed outputs");
		}
	}

	// ---- make_compact rejects a non-dense input and a None target ----
	{
		std::mt19937 r3(9);
		BooleanProgram lin = make_compact(rand_circuit(16, 500, 64, 16, r3), WireReuse::Linear).prog;
		check(dies([&] { make_compact(lin, WireReuse::Full); }), "double-compaction not rejected");
		std::mt19937 r3b(13);
		check(dies([&] { make_compact(rand_circuit(16, 500, 64, 16, r3b), WireReuse::None); }),
		      "target None not rejected");
	}

	// ---- a real builtin: compact == dense over random inputs ----
	{
		const BooleanProgram& sha = builtin_circuit("sha256_256");
		BooleanProgram lin = make_compact(sha, WireReuse::Linear).prog;
		check(lin.num_wires < sha.num_wires, "sha256 Linear should compact");
		std::mt19937 r4(11);
		for (int k = 0; k < 3; ++k) {
			std::vector<uint8_t> in(sha.num_inputs);
			for (auto& x : in) x = r4() & 1;
			check(run_bytes(lin, in) == run_bytes(sha, in), "sha256 Linear compaction changed outputs");
		}
		printf("  sha256_256: wires %u -> %u (%.2fx), %zu gates\n",
		       sha.num_wires, lin.num_wires, (double)sha.num_wires / lin.num_wires, sha.gates.size());
	}

	if (ok) printf("test_transform: all checks passed\n");
	return ok ? 0 : 1;
}
