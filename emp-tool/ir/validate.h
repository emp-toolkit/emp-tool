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
			expecting(g.op != Op::Not || g.in1 == 0, [&] {
				return "validate_program: Not gate has non-canonical in1 (must be 0)" + at(g.in1);
			});
			break;
		case Op::Const0: case Op::Const1:
			expecting(g.in0 == 0 && g.in1 == 0, [&] {
				return "validate_program: const gate has non-canonical operands (must be 0)" + at(g.out);
			});
			break;
		// The one sanctioned `default:` among the op switches (see visit.h):
		// g.op here may be a raw byte from a loaded file, so out-of-range values
		// are a runtime condition, not a missed enum case.
		default:
			expecting(false, "validate_program: unknown gate op");
	}
}

inline void validate_program(const BooleanProgram& p) {
	const uint64_t NW = p.num_wires;
	expecting(p.num_inputs <= NW,
	          "validate_program: num_inputs exceeds num_wires");
	expecting(p.wire_reuse == WireReuse::None ||
	              p.wire_reuse == WireReuse::Linear ||
	              p.wire_reuse == WireReuse::Full,
	          "validate_program: unknown wire_reuse value");

	auto at = [](size_t gi, uint32_t w) {
		return " (gate " + std::to_string(gi) + ", wire " + std::to_string(w) + ")";
	};

	if (p.wire_reuse != WireReuse::None) {
		// Reuse programs may rewrite slots. Linear reserves every And-output slot
		// exclusively; Full permits all output slots to be reused.
		expecting(NW <= p.num_inputs + (uint64_t)p.gates.size(),
		          "validate_program: reuse program num_wires exceeds num_inputs + num_gates");
		// Non-input slot state: 0 = unwritten, 1 = linear/const output,
		// 2 = And output.
		std::vector<char> slot_state((size_t)(NW - p.num_inputs), 0);
		auto is_written = [&](uint32_t w) {
			return w < p.num_inputs || slot_state[w - p.num_inputs] != 0;
		};
		size_t gi = 0;
		auto chk_read = [&](uint32_t w, const char* what) {
			expecting(w < NW, [&] {
				return std::string("validate_program: ") + what + " index out of range" + at(gi, w);
			});
			expecting(is_written(w), [&] {
				return std::string("validate_program: ") + what + " read before written" + at(gi, w);
			});
		};
		for (; gi < p.gates.size(); ++gi) {
			const Gate& g = p.gates[gi];
			validate_gate_operands(g, gi);
			if (g.op == Op::And || g.op == Op::Xor) { chk_read(g.in0, "gate in0"); chk_read(g.in1, "gate in1"); }
			else if (g.op == Op::Not)              { chk_read(g.in0, "gate in0"); }
			expecting(g.out < NW,
			          "validate_program: gate out index out of range");
			expecting(g.out >= p.num_inputs,
			          "validate_program: gate writes an input wire");
			char& state = slot_state[g.out - p.num_inputs];
			if (p.wire_reuse == WireReuse::Linear) {
				expecting(state != 2,
				          "validate_program: Linear program overwrites an And-output wire");
				if (g.op == Op::And)
					expecting(state == 0,
					          "validate_program: Linear program assigns an And output to a recycled wire");
			}
			state = g.op == Op::And ? 2 : 1;
		}
		for (uint32_t w : p.outputs) {
			expecting(w < NW,
			          "validate_program: output index out of range");
			expecting(is_written(w),
			          "validate_program: output wire never written");
		}
		return;
	}

	const uint64_t NG = p.gates.size();
	const uint64_t non_inputs = NW - p.num_inputs;
	expecting(NG == non_inputs,
	          "validate_program: num_wires must equal num_inputs + num_gates (dense program)");

	// A non-input wire is "defined" once its producer has been seen; input
	// wires are defined up front. Track only non-input wires, not all
	// num_wires, so a malformed file cannot force allocation proportional to a
	// huge declared input count during validation.
	std::vector<char> defined_non_input((size_t)non_inputs, 0);

	size_t gi = 0;
	auto chk_read = [&](uint32_t w, const char* what) {
		expecting(w < NW, [&] {
			return std::string("validate_program: ") + what + " index out of range" + at(gi, w);
		});
		expecting(w < p.num_inputs || defined_non_input[w - p.num_inputs], [&] {
			return std::string("validate_program: ") + what + " read before defined" + at(gi, w);
		});
	};
	for (; gi < p.gates.size(); ++gi) {
		const Gate& g = p.gates[gi];
		validate_gate_operands(g, gi);
		if (g.op == Op::And || g.op == Op::Xor) { chk_read(g.in0, "gate in0"); chk_read(g.in1, "gate in1"); }
		else if (g.op == Op::Not)              { chk_read(g.in0, "gate in0"); }
		expecting(g.out < NW,
		          "validate_program: gate out index out of range");
		expecting(g.out >= p.num_inputs,
		          "validate_program: gate writes an input wire");
		const uint32_t out_idx = g.out - p.num_inputs;
		expecting(!defined_non_input[out_idx],
		          "validate_program: wire defined more than once");
		defined_non_input[out_idx] = 1;
	}

	for (uint32_t w : p.outputs) {
		expecting(w < NW, "validate_program: output index out of range");
		expecting(w < p.num_inputs || defined_non_input[w - p.num_inputs],
		          "validate_program: output wire never defined");
	}
}

}  // namespace circuit
}  // namespace emp
#endif  // EMP_IR_VALIDATE_H__
