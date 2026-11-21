// ir/computation.h — a BooleanProgram plus the roles of its input and output
// slots — with the raw circuit mode of ir/session/computation_session.h over
// ClearSession. Read example() first; the rest is verification.
//
// What's in computation.h:
//   InputKind / OutputKind          Party or Public
//   InputRole / OutputRole          width + kind + owner / recipient
//   IoSignature                     the input slots and the output slots, in order
//   ComputationArtifact             shared program + IoSignature
//   validate_computation(a)         structural and role checks (fatal)
//   input_slot_offsets(io)          first wire of each input slot (+ the total)
//   output_slot_offsets(io)         first output position of each output slot (+ the total)
//   canonical_encoding(a)           versioned bytes that identify a computation
// What's in computation_session.h:
//   emp::session::Bits                   one slot's byte-bools
//   emp::session::RawBindings            per input slot: this party's bits, or empty
//   emp::session::RawResult              per output slot: the bits, where received
//   emp::session::binds / receives       whether a party supplies / receives a slot
//   emp::session::check_bindings(a, p, b) the binding rule (fatal)
//   ComputationSession<S>           prepare_computation + run_computation
// ClearSession's circuit mode: prepare_computation validates; run_computation
// takes every slot and fills every output slot.

#include "emp-tool/emp-tool.h"
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace emp;
using namespace emp::circuit;

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

static Gate gate(Op op, uint32_t in0, uint32_t in1, uint32_t out) {
	Gate g;
	g.op = op; g.in0 = in0; g.in1 = in1; g.out = out;
	return g;
}

// Inputs a (wire 0), b (1), k (2); t = a AND b (3); u = t XOR k (4).
// Outputs: [u, a].
static std::shared_ptr<const BooleanProgram> sample_program() {
	auto p = std::make_shared<BooleanProgram>();
	p->num_inputs = 3;
	p->num_wires = 5;
	p->gates = {gate(Op::And, 0, 1, 3), gate(Op::Xor, 3, 2, 4)};
	p->outputs = {4, 0};
	return p;
}

// a from ALICE, b from BOB, k public; u to BOB, a to everyone.
static ComputationArtifact sample_artifact() {
	ComputationArtifact a;
	a.program = sample_program();
	a.io.inputs = {{1, InputKind::Party, ALICE}, {1, InputKind::Party, BOB},
	               {1, InputKind::Public, PUBLIC}};
	a.io.outputs = {{1, OutputKind::Party, BOB}, {1, OutputKind::Public, PUBLIC}};
	return a;
}

static emp::session::RawBindings bits3(uint8_t a, uint8_t b, uint8_t k) {
	emp::session::RawBindings r;
	r.inputs = {emp::session::Bits{a}, emp::session::Bits{b}, emp::session::Bits{k}};
	return r;
}

static void example() {
	std::printf("== example ==\n");
	ComputationArtifact a = sample_artifact();
	validate_computation(a);
	ClearSession sess;   // all parties in one process: it binds every slot
	auto job = sess.prepare_computation(std::make_shared<const ComputationArtifact>(a));
	emp::session::RawResult r = sess.run_computation(std::move(job), bits3(1, 1, 0));
	std::printf("  u = (a & b) ^ k = %d (to BOB), a = %d (public)\n",
	            (int)(*r.outputs[0])[0], (int)(*r.outputs[1])[0]);
	std::printf("  canonical encoding: %zu bytes\n", canonical_encoding(a).size());
}

static void sweep_validate() {
	std::printf("== validate_computation ==\n");
	check(!dies([] { validate_computation(sample_artifact()); }), "a valid computation is rejected");
	{
		ComputationArtifact a = sample_artifact();
		a.io.inputs.clear();
		a.io.outputs.clear();
		a.program = std::make_shared<BooleanProgram>();   // no inputs, no outputs
		check(!dies([&] { validate_computation(a); }), "the empty computation is rejected");
	}
	check(dies([] { ComputationArtifact a = sample_artifact(); a.program = nullptr; validate_computation(a); }),
	      "a null program is accepted");
	check(dies([] {
		ComputationArtifact a = sample_artifact();
		auto bad = std::make_shared<BooleanProgram>(*a.program);
		bad->outputs[0] = 99;
		a.program = bad;
		validate_computation(a);
	}), "an invalid program is accepted");
	check(dies([] { ComputationArtifact a = sample_artifact(); a.io.inputs[0].kind = (InputKind)7; validate_computation(a); }),
	      "an unknown input kind is accepted");
	check(dies([] { ComputationArtifact a = sample_artifact(); a.io.outputs[1].kind = (OutputKind)2; validate_computation(a); }),
	      "an unknown output kind is accepted");
	check(dies([] { ComputationArtifact a = sample_artifact(); a.io.inputs[1].width = 0; validate_computation(a); }),
	      "a zero-width input slot is accepted");
	check(dies([] { ComputationArtifact a = sample_artifact(); a.io.outputs[0].width = 0; validate_computation(a); }),
	      "a zero-width output slot is accepted");
	check(dies([] { ComputationArtifact a = sample_artifact(); a.io.inputs[0].owner = PUBLIC; validate_computation(a); }),
	      "a Party input with owner PUBLIC is accepted");
	check(dies([] { ComputationArtifact a = sample_artifact(); a.io.inputs[0].owner = XOR; validate_computation(a); }),
	      "a Party input with a negative owner is accepted");
	check(dies([] { ComputationArtifact a = sample_artifact(); a.io.inputs[2].owner = ALICE; validate_computation(a); }),
	      "a Public input with an owner is accepted");
	check(dies([] { ComputationArtifact a = sample_artifact(); a.io.outputs[0].recipient = PUBLIC; validate_computation(a); }),
	      "a Party output with recipient PUBLIC is accepted");
	check(dies([] { ComputationArtifact a = sample_artifact(); a.io.outputs[1].recipient = BOB; validate_computation(a); }),
	      "a Public output with a recipient is accepted");
	check(dies([] { ComputationArtifact a = sample_artifact(); a.io.inputs.pop_back(); validate_computation(a); }),
	      "input widths short of num_inputs are accepted");
	check(dies([] { ComputationArtifact a = sample_artifact(); a.io.inputs[2].width = 2; validate_computation(a); }),
	      "input widths past num_inputs are accepted");
	check(dies([] { ComputationArtifact a = sample_artifact(); a.io.outputs.pop_back(); validate_computation(a); }),
	      "output widths short of the outputs are accepted");
	check(dies([] {
		ComputationArtifact a = sample_artifact();
		a.io.inputs = {{0x80000000u, InputKind::Public, PUBLIC}, {0x80000000u, InputKind::Public, PUBLIC},
		               {3, InputKind::Public, PUBLIC}};
		validate_computation(a);
	}), "overflowing input widths are accepted");
}

static void sweep_offsets_and_encoding() {
	std::printf("== slot offsets and canonical encoding ==\n");
	const ComputationArtifact a = sample_artifact();
	check(input_slot_offsets(a.io) == std::vector<uint32_t>({0, 1, 2, 3}), "input_slot_offsets");
	check(output_slot_offsets(a.io) == std::vector<uint32_t>({0, 1, 2}), "output_slot_offsets");

	const std::vector<uint8_t> e = canonical_encoding(a);
	check(e.size() > 6 && e[0] == 'E' && e[1] == 'M' && e[2] == 'P' && e[3] == 'C' && e[4] == 1 && e[5] == 0,
	      "the encoding starts with its tag and version");
	check(canonical_encoding(sample_artifact()) == e, "two recordings encode differently");
	ComputationArtifact r = sample_artifact();
	r.io.outputs[0].recipient = ALICE;
	check(canonical_encoding(r) != e, "a different recipient encodes the same");
	ComputationArtifact k = sample_artifact();
	k.io.inputs[1] = {1, InputKind::Public, PUBLIC};
	check(canonical_encoding(k) != e, "a different kind encodes the same");
	ComputationArtifact g = sample_artifact();
	auto prog = std::make_shared<BooleanProgram>(*g.program);
	prog->gates[1].op = Op::And;
	g.program = prog;
	check(canonical_encoding(g) != e, "a different program encodes the same");
}

static void sweep_bindings() {
	std::printf("== the binding rule ==\n");
	const ComputationArtifact a = sample_artifact();
	check(emp::session::binds(a.io.inputs[0], ALICE) && !emp::session::binds(a.io.inputs[0], BOB) &&
	      emp::session::binds(a.io.inputs[2], BOB) && emp::session::binds(a.io.inputs[1], PUBLIC), "binds");
	check(emp::session::receives(a.io.outputs[0], BOB) && !emp::session::receives(a.io.outputs[0], ALICE) &&
	      emp::session::receives(a.io.outputs[1], ALICE) && emp::session::receives(a.io.outputs[0], PUBLIC), "receives");

	emp::session::RawBindings alice;
	alice.inputs = {emp::session::Bits{1}, std::nullopt, emp::session::Bits{0}};
	emp::session::RawBindings bob;
	bob.inputs = {std::nullopt, emp::session::Bits{1}, emp::session::Bits{0}};
	check(!dies([&] { emp::session::check_bindings(a, ALICE, alice); }), "ALICE's own bindings are rejected");
	check(!dies([&] { emp::session::check_bindings(a, BOB, bob); }), "BOB's own bindings are rejected");
	check(!dies([&] { emp::session::check_bindings(a, PUBLIC, bits3(0, 1, 1)); }), "binding every slot is rejected");
	check(dies([&] { emp::session::check_bindings(a, 3, bits3(0, 0, 0)); }),
	      "party 3 may bind other parties' slots");
	check(dies([&] { emp::session::check_bindings(a, ALICE, bob); }), "ALICE may leave her slot unbound");
	check(dies([&] { emp::session::check_bindings(a, ALICE, bits3(1, 1, 0)); }), "ALICE may bind BOB's slot");
	check(dies([&] { emp::session::RawBindings b = alice; b.inputs[2].reset(); emp::session::check_bindings(a, ALICE, b); }),
	      "a Public slot may stay unbound");
	check(dies([&] { emp::session::RawBindings b = alice; b.inputs[0] = emp::session::Bits{1, 0}; emp::session::check_bindings(a, ALICE, b); }),
	      "a slot may be bound with the wrong width");
	check(dies([&] { emp::session::RawBindings b = alice; b.inputs.pop_back(); emp::session::check_bindings(a, ALICE, b); }),
	      "the bindings may miss a slot entry");
	check(dies([&] { emp::session::check_bindings(a, PUBLIC, alice); }),
	      "a whole-process session may leave a slot unbound");
}

// Random dense programs with several slots, against execute_program directly.
static void sweep_clear_session() {
	std::printf("== ClearSession circuit mode ==\n");
	static_assert(ComputationSession<ClearSession>);
	std::mt19937_64 rng(20261121);
	for (int trial = 0; trial < 64; ++trial) {
		auto p = std::make_shared<BooleanProgram>();
		const uint32_t ni = 1 + (uint32_t)(rng() % 12);
		const uint32_t ng = (uint32_t)(rng() % 40);
		p->num_inputs = ni;
		p->num_wires = ni + ng;
		for (uint32_t g = 0; g < ng; ++g) {
			const uint32_t out = ni + g, live = out;
			const Op op = (Op)(rng() % 5);
			Gate x;
			x.op = op; x.out = out;
			if (op == Op::And || op == Op::Xor) { x.in0 = (uint32_t)(rng() % live); x.in1 = (uint32_t)(rng() % live); }
			else if (op == Op::Not) x.in0 = (uint32_t)(rng() % live);
			p->gates.push_back(x);
		}
		const uint32_t no = 1 + (uint32_t)(rng() % 10);
		for (uint32_t o = 0; o < no; ++o) p->outputs.push_back((uint32_t)(rng() % p->num_wires));
		validate_program(*p);

		ComputationArtifact a;
		a.program = p;
		for (uint32_t left = ni; left != 0; ) {   // random slot widths and kinds
			const uint32_t w = 1 + (uint32_t)(rng() % left);
			a.io.inputs.push_back(rng() % 2 ? InputRole{w, InputKind::Public, PUBLIC}
			                                : InputRole{w, InputKind::Party, 1 + (int)(rng() % 4)});
			left -= w;
		}
		for (uint32_t left = no; left != 0; ) {
			const uint32_t w = 1 + (uint32_t)(rng() % left);
			a.io.outputs.push_back(rng() % 2 ? OutputRole{w, OutputKind::Public, PUBLIC}
			                                 : OutputRole{w, OutputKind::Party, 1 + (int)(rng() % 4)});
			left -= w;
		}

		std::vector<uint8_t> in(ni);
		for (uint8_t& b : in) b = (uint8_t)(rng() & 1);
		emp::session::RawBindings bind;
		const std::vector<uint32_t> ioff = input_slot_offsets(a.io);
		for (std::size_t s = 0; s < a.io.inputs.size(); ++s)
			bind.inputs.emplace_back(emp::session::Bits(in.begin() + ioff[s], in.begin() + ioff[s + 1]));

		ClearCtx cx;
		const std::vector<uint8_t> want = execute_program(cx, *p, std::span<const uint8_t>(in.data(), in.size()));
		ClearSession sess;
		emp::session::RawResult r = sess.run_computation(
		    sess.prepare_computation(std::make_shared<const ComputationArtifact>(a)), bind);
		bool same = r.outputs.size() == a.io.outputs.size();
		const std::vector<uint32_t> ooff = output_slot_offsets(a.io);
		for (std::size_t s = 0; same && s < a.io.outputs.size(); ++s) {
			same = r.outputs[s].has_value() && r.outputs[s]->size() == a.io.outputs[s].width;
			for (uint32_t k = 0; same && k < a.io.outputs[s].width; ++k)
				same = (*r.outputs[s])[k] == (want[ooff[s] + k] & 1);
		}
		check(same, "ClearSession's circuit mode differs from execute_program");
	}

	check(dies([] { ClearSession s; (void)s.prepare_computation(nullptr); }), "a null computation is prepared");
	check(dies([] {
		ClearSession s;
		ComputationArtifact a = sample_artifact();
		a.io.inputs[0].owner = PUBLIC;
		(void)s.prepare_computation(std::make_shared<const ComputationArtifact>(a));
	}), "an invalid computation is prepared");
	check(dies([] {
		ClearSession s;
		auto job = s.prepare_computation(std::make_shared<const ComputationArtifact>(sample_artifact()));
		(void)s.run_computation(std::move(job), bits3(1, 1, 1));
		(void)s.run_computation(std::move(job), bits3(1, 1, 1));   // the moved-from job is spent
	}), "a job runs twice");
	check(dies([] {
		ClearSession s;
		emp::session::RawBindings b = bits3(1, 1, 1);
		b.inputs[1].reset();
		(void)s.run_computation(
		    s.prepare_computation(std::make_shared<const ComputationArtifact>(sample_artifact())), b);
	}), "ClearSession runs with a slot unbound");
}

int main() {
	example();
	sweep_validate();
	sweep_offsets_and_encoding();
	sweep_bindings();
	sweep_clear_session();
	if (fails) { std::printf("FAIL: %d check(s)\n", fails); return 1; }
	std::printf("OK\n");
	return 0;
}
