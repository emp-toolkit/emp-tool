#ifndef EMP_IR_PROGRAM_H__
#define EMP_IR_PROGRAM_H__

// The canonical, protocol-neutral Boolean-circuit IR for emp-tool. When a
// circuit is materialized as IR, it has one in-memory representation and one
// gate encoding. A program's inputs are wires [0, num_inputs); every other wire
// is produced by exactly one gate, in topological (emission) order; the return
// value is the explicit `outputs` wire list. The IR knows
// nothing about parties, owners, argument boundaries, labels, MACs, or any
// protocol — those live in the layers that produce/consume it (the compiled
// Circuit's signature carries per-argument widths; a protocol backend carries
// ownership). Validation (ir/validate.h), the gate-walk primitive (ir/visit.h),
// and value execution (ir/execute.h) are split into their own headers.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace emp {
namespace circuit {

// Public constants are first-class Const0/Const1 gates (no operands) so the IR
// stays protocol-neutral — each backend realizes a known 0/1 wire however its
// protocol prescribes. The on-disk .empbc op codes are exactly these values.
enum class Op : uint8_t { And = 0, Xor = 1, Not = 2, Const0 = 3, Const1 = 4 };

// in1 is unused for Not/Const0/Const1 and is normalized to 0; out is the wire id
// produced. Validation and execution only read the operands relevant to the op,
// so the normalized 0 is never interpreted as a real wire.
struct Gate {
	uint32_t in0 = 0, in1 = 0, out = 0;
	Op       op  = Op::And;
	bool is_and()    const { return op == Op::And; }
	bool is_xor()    const { return op == Op::Xor; }
	bool is_not()    const { return op == Op::Not; }
	bool is_const()  const { return op == Op::Const0 || op == Op::Const1; }
	bool is_linear() const { return op == Op::Xor  || op == Op::Not; }
};

// How wire ids relate to gates. The dividing line is whether a consumer that
// makes several passes over the program *stores* a gate's value across passes:
// linear gates (Xor/Not/Const) are recomputed every pass, so their output ids
// are always safe to recycle; a non-linear (And) output may hold stored state
// (e.g. a fresh authenticated mask in a garbling backend), so recycling an And
// id can clobber a value a later pass still needs.
//   None   : dense/SSA — every non-input wire is the output of exactly one gate,
//            ids monotonic in emission order (num_wires == num_inputs+num_gates).
//   Linear : ids are reused once dead, but And outputs are never recycled. Safe
//            for every consumer, including multi-pass backends.
//   Full   : any wire id may be reused once dead, And outputs included. Maximal
//            compaction; correct only for single-/recompute-pass execution.
enum class WireReuse : uint8_t { None = 0, Linear = 1, Full = 2 };

struct BooleanProgram {
	uint32_t num_wires  = 0;   // total wire ids; valid ids are [0, num_wires)
	uint32_t num_inputs = 0;   // input wires are [0, num_inputs)
	WireReuse wire_reuse = WireReuse::None;   // None = dense/SSA (the default contract)

	std::vector<Gate>     gates;     // topological (emission) order
	std::vector<uint32_t> outputs;   // wire ids of the return value (flattened)

	uint32_t total_input_bits()  const { return num_inputs; }
	uint32_t total_output_bits() const { return (uint32_t)outputs.size(); }
	uint32_t num_gate()          const { return (uint32_t)gates.size(); }
};

}  // namespace circuit
}  // namespace emp
#endif  // EMP_IR_PROGRAM_H__
