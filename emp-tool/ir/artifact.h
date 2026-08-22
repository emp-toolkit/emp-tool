#ifndef EMP_IR_ARTIFACT_H__
#define EMP_IR_ARTIFACT_H__

// The canonical persisted/compiled circuit: a BooleanProgram plus the argument
// signature needed to feed it. NO baked analyses — count/liveness/schedule/layout
// are free functions over the program (ir/passes.h), cached separately by
// the optional frontend `Circuit` wrapper when wanted.

#include "emp-tool/ir/program.h"
#include "emp-tool/ir/validate.h"
#include "emp-tool/runtime/core/utils.h"   // error()
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace emp {
namespace circuit {

// Argument boundaries the flat IR doesn't carry: each argument's bit width and
// the return value's width. total_input_bits() must equal program.num_inputs;
// return_width must equal program.outputs.size().
struct CircuitSignature {
	std::vector<uint32_t> arg_widths;
	uint32_t return_width = 0;

	uint64_t total_input_bits() const {
		uint64_t s = 0;
		for (uint32_t w : arg_widths) {
			expecting(w <= UINT32_MAX - s,
			          "CircuitSignature::total_input_bits: sum exceeds UINT32_MAX");
			s += w;
		}
		return s;
	}
};

struct CircuitArtifact {
	BooleanProgram   program;
	CircuitSignature signature;
};

// Structural + signature consistency. Runs validate_program() and checks the
// signature matches the program's I/O. error()s (fatal) on mismatch.
inline void validate_artifact(const CircuitArtifact& a) {
	validate_program(a.program);
	for (std::size_t i = 0; i < a.signature.arg_widths.size(); ++i)
		expecting(a.signature.arg_widths[i] != 0, [&] {
			return "validate_artifact: argument " + std::to_string(i) +
			       " has zero width";
		});
	expecting(a.signature.return_width != 0,
	          "validate_artifact: return width must be positive");
	const uint64_t ins = a.signature.total_input_bits();
	expecting(ins == (uint64_t)a.program.num_inputs,
	          "validate_artifact: sum(arg_widths) != program.num_inputs");
	expecting((uint64_t)a.signature.return_width ==
	              (uint64_t)a.program.outputs.size(),
	          "validate_artifact: return_width != program.outputs size");
}

}  // namespace circuit
}  // namespace emp
#endif  // EMP_IR_ARTIFACT_H__
