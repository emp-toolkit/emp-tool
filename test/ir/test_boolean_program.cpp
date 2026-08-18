// Test for circuits/boolean_program.h + circuits/empbc.h — the canonical IR,
// its validator, the for_each_gate / execute_program execution split, and the
// .empbc codec (u16/u32 roundtrip + malformed rejection). No backend needed:
// these are pure data-structure / serialization checks.

#include "emp-tool/ir/program.h"
#include "emp-tool/ir/validate.h"
#include "emp-tool/ir/visit.h"
#include "emp-tool/ir/execute.h"
#include "emp-tool/ir/empbc.h"
#include "emp-tool/ir/artifact.h"
#include "emp-tool/ir/context/digest.h"
#include <cassert>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace emp::circuit;

// Rejection is fatal by design — error() _Exit(1)s the process (utils.hpp) —
// so "this input is rejected" shows up as child-process death, not an
// exception. Run `f` in a fork with stderr silenced (the diagnostic is
// expected) and report whether the child exited nonzero.
template <class F>
static int child_status(F&& f) {
	pid_t pid = fork();
	if (pid == 0) {
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) dup2(devnull, 2);
		f();
		_exit(0);
	}
	int st = 0;
	waitpid(pid, &st, 0);
	return st;
}

template <class F>
static bool dies(F&& f) {
	int st = child_status(std::forward<F>(f));
	return !(WIFEXITED(st) && WEXITSTATUS(st) == 0);
}

template <class F>
static bool rejects_cleanly(F&& f) {
	int st = child_status(std::forward<F>(f));
	return WIFEXITED(st) && WEXITSTATUS(st) == 1;
}

static bool throws(void (*fn)(const BooleanProgram&), const BooleanProgram& p) {
	return dies([&] { fn(p); });
}

// A regular file is stdio-buffered, so a tiny RLIMIT_FSIZE lets fwrite accept
// the bytes and makes the completion error surface from fclose.
static bool rejects_write_completion(const BooleanProgram& p) {
	char path[] = "/tmp/empbc-write-XXXXXX";
	int fd = mkstemp(path);
	if (fd < 0) return false;
	close(fd);
	pid_t pid = fork();
	if (pid == 0) {
		std::signal(SIGXFSZ, SIG_IGN);
		struct rlimit limit = {1, 1};
		if (setrlimit(RLIMIT_FSIZE, &limit) != 0) _exit(2);
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) dup2(devnull, 2);
		save_empbc_file(path, p);
		_exit(0);
	}
	int st = 0;
	waitpid(pid, &st, 0);
	unlink(path);
	return WIFEXITED(st) && WEXITSTATUS(st) == 1;
}

// A 2-input program: out = (a AND b) XOR 1, plus a passthrough of `a`.
// wires: 0,1 inputs; 2 = a&b; 3 = const1; 4 = 2 ^ 3. outputs = {4, 0}.
static BooleanProgram sample() {
	BooleanProgram p;
	p.num_wires  = 5;
	p.num_inputs = 2;
	p.gates = {
		Gate{0, 1, 2, Op::And},
		Gate{0, 0, 3, Op::Const1},
		Gate{2, 3, 4, Op::Xor},
	};
	p.outputs = {4, 0};
	return p;
}

// Compute dispatcher over plain bytes (0/1) so we can evaluate concretely.
struct ByteDispatcher {
	void and_gate(uint8_t& o, const uint8_t& a, const uint8_t& b) { o = a & b; }
	void xor_gate(uint8_t& o, const uint8_t& a, const uint8_t& b) { o = a ^ b; }
	void not_gate(uint8_t& o, const uint8_t& a)                   { o = a ^ 1; }
	void const_gate(uint8_t& o, bool v)                           { o = v ? 1 : 0; }
};

int main() {
	bool ok = true;
	auto check = [&](bool c, const char* msg) {
		if (!c) { printf("FAIL: %s\n", msg); ok = false; }
	};

	// ---- validation: the well-formed program passes ----
	BooleanProgram p = sample();
	check(!dies([&] { validate_program(p); }), "valid program rejected");

	// ---- validation: each invariant is enforced ----
	{ BooleanProgram b = sample(); b.gates[0].in1 = 99;  check(throws(validate_program, b), "in-range read not enforced"); }
	{ BooleanProgram b = sample(); b.outputs[0] = 99;    check(throws(validate_program, b), "output bound not enforced"); }
	{ BooleanProgram b = sample(); b.gates[2].out = 0;   check(throws(validate_program, b), "write-to-input not enforced"); }
	{ BooleanProgram b = sample(); b.gates[2].out = 2;   check(throws(validate_program, b), "single-definition not enforced"); }
	{ BooleanProgram b = sample(); b.gates[0].out = 3; b.gates[1].out = 2;
	  b.gates[2].in0 = 3; b.gates[2].in1 = 2;
	  check(throws(validate_program, b), "non-canonical dense output numbering not rejected"); }
	{ BooleanProgram b = sample(); b.gates[0].in0 = 4;   check(throws(validate_program, b), "read-before-define not enforced"); }
	{ BooleanProgram b = sample(); b.gates[1].in0 = 1;   check(throws(validate_program, b), "non-canonical const operand not rejected"); }  // gate 1 is Const1
	{ BooleanProgram b; b.num_wires = 2; b.num_inputs = 1; b.gates = { Gate{0, 7, 1, Op::Not} }; b.outputs = {1};
	  check(throws(validate_program, b), "non-canonical Not in1 not rejected"); }
	{ BooleanProgram b; b.num_wires = 3; b.num_inputs = 1;
	  b.gates = { Gate{0, 0, 1, Op::Xor} }; b.outputs = {1};  // wire 2 counted but never defined
	  check(throws(validate_program, b), "non-dense program (hole) not rejected"); }
	{ BooleanProgram b = sample(); b.wire_reuse = (WireReuse)99;
	  check(throws(validate_program, b), "unknown wire_reuse value not rejected"); }

	// ---- diagnostic identities: gate trace vs complete program structure ----
	{
		const uint64_t trace = emp::digest_gate_stream(p);
		const uint64_t full = emp::digest_program(p);

		BooleanProgram reordered = p;
		reordered.outputs = {0, 4};
		validate_program(reordered);
		check(emp::digest_gate_stream(reordered) == trace,
		      "gate-stream digest should ignore output selection");
		check(emp::digest_program(reordered) != full,
		      "program digest should include ordered outputs");

		BooleanProgram linear = p;
		linear.wire_reuse = WireReuse::Linear;
		validate_program(linear);
		check(emp::digest_program(linear) != full,
		      "program digest should include wire reuse mode");

		BooleanProgram reused;
		reused.num_inputs = 1; reused.num_wires = 2;
		reused.wire_reuse = WireReuse::Full;
		reused.gates = {Gate{0,0,1,Op::Not}, Gate{1,0,1,Op::Not}};
		reused.outputs = {1};
		validate_program(reused);
		BooleanProgram padded = reused;
		padded.num_wires = 3;   // valid but structurally distinct unused slot
		validate_program(padded);
		check(emp::digest_gate_stream(reused) == emp::digest_gate_stream(padded),
		      "gate-stream digest should ignore storage dimensions");
		check(emp::digest_program(reused) != emp::digest_program(padded),
		      "program digest should include num_wires");
	}

	// DigestCtx closes the aggregate input prefix at the first emitted gate.
	check(dies([] {
		emp::DigestCtx d;
		uint32_t in = d.external_input(1);
		d.not_gate(in);
		d.external_input(1);
	}), "DigestCtx accepted an input reservation after a gate");

	// ---- artifact signatures use positive-width arguments and return values ----
	{
		CircuitArtifact good{p, CircuitSignature{{2}, 2}};
		check(!dies([&] { validate_artifact(good); }), "valid artifact rejected");

		CircuitArtifact zero_arg = good;
		zero_arg.signature.arg_widths = {0, 2};
		check(dies([&] { validate_artifact(zero_arg); }),
		      "zero-width artifact argument not rejected");

		CircuitArtifact overflow = good;
		overflow.signature.arg_widths = {UINT32_MAX, 1};
		check(dies([&] { validate_artifact(overflow); }),
		      "overflowing artifact input width not rejected");

		BooleanProgram no_output = p;
		no_output.outputs.clear();
		CircuitArtifact zero_return{no_output, CircuitSignature{{2}, 0}};
		check(dies([&] { validate_artifact(zero_return); }),
		      "zero-width artifact return not rejected");

		BooleanProgram nullary;
		nullary.num_wires = 1;
		nullary.gates = {Gate{0,0,0,Op::Const1}};
		nullary.outputs = {0};
		CircuitArtifact no_args{nullary, CircuitSignature{{}, 1}};
		check(!dies([&] { validate_artifact(no_args); }),
		      "nullary artifact with an empty argument list rejected");
	}

	// ---- execute_program matches a hand evaluation for all 4 input combos ----
	for (int a = 0; a < 2; ++a) for (int b = 0; b < 2; ++b) {
		uint8_t in[2]  = { (uint8_t)a, (uint8_t)b };
		uint8_t out[2] = { 0, 0 };
		CircuitScratch<uint8_t> sc;
		execute_program<uint8_t>(p, in, 2, out, 2, sc, ByteDispatcher{});
		check(out[0] == ((uint8_t)((a & b) ^ 1)), "execute_program: out0 wrong");
		check(out[1] == (uint8_t)a,                "execute_program: passthrough wrong");
	}

	// ---- .empbc roundtrip (u16 form, since num_wires is tiny) ----
	{
		std::vector<uint8_t> bytes = save_empbc(p);
		check(bytes[4] == 1 && bytes[5] == 0, "empbc version mismatch");
		check(bytes[6] == 2, "small program should use 16-bit index width");
		BooleanProgram q = load_empbc(bytes);
		check(q.num_wires == p.num_wires && q.num_inputs == p.num_inputs, "u16 roundtrip header");
		check(q.gates.size() == p.gates.size() && q.outputs == p.outputs,  "u16 roundtrip body");
		check(q.gates[2].op == Op::Xor && q.gates[2].in0 == 2,             "u16 roundtrip gate");
	}

	// ---- .empbc u32 form: force a wide program (>65535 wires) ----
	{
		BooleanProgram big;
		big.num_inputs = 2;
		// One XOR chain long enough to push past the 16-bit boundary.
		uint32_t prev0 = 0, prev1 = 1, w = 2;
		const uint32_t N = 70000;
		for (uint32_t i = 0; i < N; ++i) { big.gates.push_back(Gate{prev0, prev1, w, Op::Xor}); prev1 = prev0; prev0 = w; ++w; }
		big.num_wires = w;
		big.outputs = { w - 1 };
		std::vector<uint8_t> bytes = save_empbc(big);
		check(bytes[6] == 4, "wide program should use 32-bit index width");
		BooleanProgram q = load_empbc(bytes);
		check(q.num_wires == big.num_wires && q.gates.size() == N, "u32 roundtrip");
	}

	// ---- malformed/truncated .empbc are rejected ----
	{
		std::vector<uint8_t> bytes = save_empbc(p);
		auto rejects = [&](std::vector<uint8_t> b) {
			return dies([&] { load_empbc(b); });
		};
		check(rejects({}),                                         "empty buffer not rejected");
		{ auto b = bytes; b[0] = 'X';            check(rejects(b), "bad magic not rejected"); }
		{ auto b = bytes; b.pop_back();          check(rejects(b), "truncated tail not rejected"); }
		{ auto b = bytes; b.push_back(0);        check(rejects(b), "trailing byte not rejected"); }
		{ auto b = bytes; b[6] = 7;              check(rejects(b), "bad index_width not rejected"); }
		// Corrupt an op code (first gate's op byte: header 24 + 3*2 = byte 30).
		{ auto b = bytes; b[24 + 3 * 2] = 0x7F;  check(rejects(b), "bad op code not rejected"); }
		{ auto b = bytes; b[24 + 3 * 2 + 1] = 1; check(rejects(b), "nonzero u16 gate reserved byte not rejected"); }
		check(rejects_cleanly([&] { load_empbc(nullptr, 1); }),
		      "null nonempty buffer not rejected cleanly");
		if (std::numeric_limits<size_t>::max() > std::numeric_limits<uint32_t>::max()) {
			const size_t too_many = (size_t)std::numeric_limits<uint32_t>::max() + 1;
			check(dies([&] { empbc_detail::checked_u32_count(too_many, "test records"); }),
			      "u32 record-count overflow not rejected");
		}

		// The u32 record has three reserved bytes after its op.
		BooleanProgram wide;
		wide.num_inputs = 70000;
		wide.num_wires = 70001;
		wide.gates = {Gate{0, 69999, 70000, Op::Xor}};
		wide.outputs = {70000};
		auto wide_bytes = save_empbc(wide);
		check(wide_bytes[6] == 4, "reserved-byte test should use u32 records");
		for (size_t off = 24 + 3 * 4 + 1; off < 24 + 4 * 4; ++off) {
			auto b = wide_bytes;
			b[off] = 1;
			check(rejects(b), "nonzero u32 gate reserved byte not rejected");
		}
		// Huge declared wire count with no encoded gates must be rejected by the
		// dense-count check before validation allocates per-wire scratch.
		std::vector<uint8_t> huge = {
			'E','M','P','B',
			1,0,        // version
			4,0,        // u32 indices, flags
			0xff,0xff,0xff,0xff,  // num_wires
			0,0,0,0,              // num_inputs
			0,0,0,0,              // num_outputs
			0,0,0,0               // num_gates
		};
		check(rejects(huge), "huge sparse header not rejected before allocation");

		// The reuse (flags=1) validation path must apply the SAME anti-DoS
		// discipline as the dense path. Two hostile reuse headers:
		// (a) num_inputs=0, huge num_wires, no gates: rejected up front by the
		//     reuse bound num_wires <= num_inputs + num_gates.
		std::vector<uint8_t> huge_reuse_a = {
			'E','M','P','B',
			1,0,        // version
			4,1,        // u32 indices, flags=1 (Linear)
			0xff,0xff,0xff,0xff,  // num_wires
			0,0,0,0,              // num_inputs
			0,0,0,0,              // num_outputs
			0,0,0,0               // num_gates
		};
		check(rejects(huge_reuse_a), "huge reuse header (0 inputs) not rejected before allocation");
		// (b) num_inputs == num_wires == 0xFFFFFFFF, no gates: passes the bound
		//     (NW == num_inputs + 0), so validation must NOT size written[] to
		//     num_wires nor loop over num_inputs — either would be a ~4 GB
		//     alloc + 4e9-iteration DoS. It must load cheaply as a no-op
		//     program (sizing only the non-input slots, here zero).
		std::vector<uint8_t> huge_reuse_b = {
			'E','M','P','B',
			1,0,        // version
			4,1,        // u32 indices, flags=1 (Linear)
			0xff,0xff,0xff,0xff,  // num_wires
			0xff,0xff,0xff,0xff,  // num_inputs == num_wires
			0,0,0,0,              // num_outputs
			0,0,0,0               // num_gates
		};
		check(!dies([&]{ load_empbc(huge_reuse_b); }),
		      "huge reuse header (inputs==wires) must load cheaply, not allocate per-wire");
	}

	// ---- wire_reuse (compact) programs: validation + .empbc roundtrip ----
	{
		// Linear: recycle a fabric (Xor/Not) wire id; the And output stays distinct.
		// wires: 0,1 inputs; 2 = 0^1; 2 = NOT 2 (reuse); 3 = 0 & 2. out = {3}.
		BooleanProgram c;
		c.num_inputs = 2; c.num_wires = 4; c.wire_reuse = WireReuse::Linear;
		c.gates = { Gate{0,1,2,Op::Xor}, Gate{2,0,2,Op::Not}, Gate{0,2,3,Op::And} };
		c.outputs = {3};
		check(!dies([&]{ validate_program(c); }), "valid Linear reuse program rejected");
		// The SAME structure must FAIL dense validation (wire 2 defined twice).
		{ BooleanProgram d = c; d.wire_reuse = WireReuse::None;
		  check(throws(validate_program, d), "reuse structure wrongly accepted as dense"); }
		// Roundtrip preserves the level; the flags byte (offset 7) carries it.
		std::vector<uint8_t> bytes = save_empbc(c);
		check(bytes[7] == 1, "Linear flags byte should be 1");
		BooleanProgram q = load_empbc(bytes);
		check(q.wire_reuse == WireReuse::Linear, "Linear level not roundtripped");
		check(q.num_wires == 4 && q.gates.size() == 3 && q.outputs == c.outputs, "Linear reuse roundtrip body");

		// Full: recycle an And output id (correct for single-pass value execution).
		BooleanProgram f;
		f.num_inputs = 2; f.num_wires = 3; f.wire_reuse = WireReuse::Full;
		f.gates = { Gate{0,1,2,Op::And}, Gate{0,2,2,Op::Xor} };
		f.outputs = {2};
		check(!dies([&]{ validate_program(f); }), "valid Full reuse program rejected");
		check(save_empbc(f)[7] == 2, "Full flags byte should be 2");
		check(load_empbc(save_empbc(f)).wire_reuse == WireReuse::Full, "Full level not roundtripped");

		// Malformed reuse / flags.
		auto rejects = [&](std::vector<uint8_t> b){ return dies([&]{ load_empbc(b); }); };
		{ auto b = bytes; b[7] = 3; check(rejects(b), "reserved wire_reuse value 3 not rejected"); }
		{ auto b = bytes; b[7] = 4; check(rejects(b), "unknown flags bit (2) not rejected"); }
		{ BooleanProgram b = c; b.gates[0].in0 = 3;  check(throws(validate_program, b), "reuse read-before-written not enforced"); }
		{ BooleanProgram b = c; b.gates[2].out = 0;  check(throws(validate_program, b), "reuse write-to-input not enforced"); }
		{ BooleanProgram b = c; b.gates[2].out = 99; check(throws(validate_program, b), "reuse out-of-range not enforced"); }
		{ BooleanProgram b = c; b.num_wires = 100;   check(throws(validate_program, b), "reuse num_wires over dense bound not enforced"); }

		// In Linear mode an And output's slot is exclusive across the whole gate
		// stream. Either direction of reuse breaks multi-pass consumers: a later
		// write clobbers stored state, while an earlier write clobbers it when a
		// later pass replays the prefix.
		{ BooleanProgram b = f; b.wire_reuse = WireReuse::Linear;
		  check(throws(validate_program, b), "Linear overwrite of And-output slot not rejected"); }
		{ BooleanProgram b;
		  b.num_inputs = 2; b.num_wires = 3; b.wire_reuse = WireReuse::Linear;
		  b.gates = {Gate{0,1,2,Op::Xor}, Gate{0,1,2,Op::And}}; b.outputs = {2};
		  check(throws(validate_program, b), "Linear And output assigned to recycled slot not rejected");
		  b.wire_reuse = WireReuse::Full;
		  check(!dies([&]{ validate_program(b); }), "Full reuse of slot before And wrongly rejected"); }
	}

	check(rejects_write_completion(p), "empbc write-completion failure not rejected");

	if (ok) printf("test_boolean_program: all checks passed\n");
	return ok ? 0 : 1;
}
