#ifndef EMP_IR_VALIDATE_H__
#define EMP_IR_VALIDATE_H__

// Validation for the Boolean-circuit IR. ONE path, run at construction / load /
// before executing an externally supplied program — never inside the execution
// hot loop. error()s (fatal, see utils.hpp) on the first violation. Enforces the IR's
// invariants so every downstream consumer (frontend replay, float builtins,
// ag2pc) may trust the structure without re-checking.

#include "emp-tool/ir/program.h"
#include "emp-tool/runtime/core/utils.h"   // error()
#include <cstdint>
#include <string>
#include <vector>

namespace emp {
namespace circuit {

// Operand-canonicality is the same in every wire-reuse mode; share it.
inline void validate_gate_operands(const Gate& g, size_t gi) {
	auto at = [&](uint32_t w) {
		return " (gate " + std::to_string(gi) + ", wire " + std::to_string(w) + ")";
	};
	switch (g.op) {
		case Op::And: case Op::Xor: case Op::Not:
			if (g.op == Op::Not && g.in1 != 0)
				error(("validate_program: Not gate has non-canonical in1 (must be 0)" + at(g.in1)).c_str());
			break;
		case Op::Const0: case Op::Const1:
			if (g.in0 != 0 || g.in1 != 0)
				error(("validate_program: const gate has non-canonical operands (must be 0)" + at(g.out)).c_str());
			break;
		// The one sanctioned `default:` among the op switches (see visit.h):
		// g.op here may be a raw byte from a loaded file, so out-of-range values
		// are a runtime condition, not a missed enum case.
		default:
			error("validate_program: unknown gate op");
	}
}

inline void validate_program(const BooleanProgram& p) {
	const uint64_t NW = p.num_wires;
	if (p.num_inputs > NW)
		error("validate_program: num_inputs exceeds num_wires");

	auto at = [](size_t gi, uint32_t w) {
		return " (gate " + std::to_string(gi) + ", wire " + std::to_string(w) + ")";
	};

	if (p.wire_reuse != WireReuse::None) {
		// Reuse program (Linear/Full are structurally identical — the And-pinning
		// distinction is a *producer* property this check cannot verify). No
		// dense-count check and no single-definition check: a wire id is rewritten
		// on purpose. This proves the program is *executable* (every read sees a
		// written or input wire), NOT that it is equivalent to its dense source —
		// liveness-safety of a reuse is the producer's contract (ir/transform.h),
		// covered by execute-compare + digest tests.
		//
		// A reuse program never has MORE distinct wire ids than its dense
		// equivalent, so num_wires is bounded by num_inputs + num_gates; enforce
		// that (it also bounds the written[] allocation against a hostile count).
		if (NW > p.num_inputs + (uint64_t)p.gates.size())
			error("validate_program: reuse program num_wires exceeds num_inputs + num_gates");
		// Track only NON-input slots, exactly like the dense path: input wires are
		// "written" up front by definition (w < num_inputs), so a malformed file
		// cannot force allocation/iteration proportional to a huge declared input
		// count. Redefinition of a non-input slot is allowed (the point of reuse).
		std::vector<char> written((size_t)(NW - p.num_inputs), 0);
		auto is_written = [&](uint32_t w) {
			return w < p.num_inputs || written[w - p.num_inputs];
		};
		size_t gi = 0;
		auto chk_read = [&](uint32_t w, const char* what) {
			if (w >= NW) error((std::string("validate_program: ") + what + " index out of range" + at(gi, w)).c_str());
			if (!is_written(w)) error((std::string("validate_program: ") + what + " read before written" + at(gi, w)).c_str());
		};
		for (; gi < p.gates.size(); ++gi) {
			const Gate& g = p.gates[gi];
			validate_gate_operands(g, gi);
			if (g.op == Op::And || g.op == Op::Xor) { chk_read(g.in0, "gate in0"); chk_read(g.in1, "gate in1"); }
			else if (g.op == Op::Not)              { chk_read(g.in0, "gate in0"); }
			if (g.out >= NW)
				error("validate_program: gate out index out of range");
			if (g.out < p.num_inputs)
				error("validate_program: gate writes an input wire");
			written[g.out - p.num_inputs] = 1;   // safe: g.out in [num_inputs, NW)
		}
		for (uint32_t w : p.outputs) {
			if (w >= NW) error("validate_program: output index out of range");
			if (!is_written(w)) error("validate_program: output wire never written");
		}
		return;
	}

	const uint64_t NG = p.gates.size();
	const uint64_t non_inputs = NW - p.num_inputs;
	if (NG != non_inputs)
		error("validate_program: num_wires must equal num_inputs + num_gates (dense program)");

	// A non-input wire is "defined" once its producer has been seen; input
	// wires are defined up front. Track only non-input wires, not all
	// num_wires, so a malformed file cannot force allocation proportional to a
	// huge declared input count during validation.
	std::vector<char> defined_non_input((size_t)non_inputs, 0);

	size_t gi = 0;
	auto chk_read = [&](uint32_t w, const char* what) {
		if (w >= NW) error((std::string("validate_program: ") + what + " index out of range" + at(gi, w)).c_str());
		if (w >= p.num_inputs && !defined_non_input[w - p.num_inputs])
			error((std::string("validate_program: ") + what + " read before defined" + at(gi, w)).c_str());
	};
	for (; gi < p.gates.size(); ++gi) {
		const Gate& g = p.gates[gi];
		validate_gate_operands(g, gi);
		if (g.op == Op::And || g.op == Op::Xor) { chk_read(g.in0, "gate in0"); chk_read(g.in1, "gate in1"); }
		else if (g.op == Op::Not)              { chk_read(g.in0, "gate in0"); }
		if (g.out >= NW)
			error("validate_program: gate out index out of range");
		if (g.out < p.num_inputs)
			error("validate_program: gate writes an input wire");
		const uint32_t out_idx = g.out - p.num_inputs;
		if (defined_non_input[out_idx])
			error("validate_program: wire defined more than once");
		defined_non_input[out_idx] = 1;
	}

	for (uint32_t w : p.outputs) {
		if (w >= NW) error("validate_program: output index out of range");
		if (w >= p.num_inputs && !defined_non_input[w - p.num_inputs])
			error("validate_program: output wire never defined");
	}
}

}  // namespace circuit
}  // namespace emp
#endif  // EMP_IR_VALIDATE_H__
