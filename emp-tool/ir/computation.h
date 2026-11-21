#ifndef EMP_IR_COMPUTATION_H__
#define EMP_IR_COMPUTATION_H__

// A computation: a BooleanProgram plus the role of every input and output slot.
// The program stays party-free (ir/program.h); the roles live beside it, here.
//
// An input slot is a run of consecutive input wires, and the slots cover
// [0, num_inputs) in order. An output slot is a run of consecutive entries of
// program.outputs, and the slots cover that list in order. Each slot is
//   Party(p) -- input: party p supplies the bits; output: party p learns them;
//   Public   -- input: every party supplies the same bits; output: every party
//               learns them.
// Output slots may name the same wires, and may name input wires.
//
// A session that runs computations (ir/session/computation_session.h) sees
// every role before any value is bound, so a protocol can move the work that
// depends on owners and recipients offline. The parties never compare their
// computations at run time: every party builds the same one, and a session
// agrees on its configuration once, at setup.

#include "emp-tool/ir/empbc.h"                // save_empbc (canonical encoding)
#include "emp-tool/ir/program.h"
#include "emp-tool/ir/validate.h"
#include "emp-tool/runtime/core/constants.h"  // PUBLIC
#include "emp-tool/runtime/core/utils.h"      // expecting
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace emp {
namespace circuit {

// Later kinds are appended, so the values of these two never change.
enum class InputKind  : uint8_t { Party = 0, Public = 1 };
enum class OutputKind : uint8_t { Party = 0, Public = 1 };

// owner (recipient) is a party id >= 1 for Party, and PUBLIC (0) for Public.
struct InputRole {
	uint32_t  width = 0;
	InputKind kind  = InputKind::Party;
	int       owner = PUBLIC;
};
struct OutputRole {
	uint32_t   width     = 0;
	OutputKind kind      = OutputKind::Party;
	int        recipient = PUBLIC;
};

struct IoSignature {
	std::vector<InputRole>  inputs;
	std::vector<OutputRole> outputs;
};

// Shareable: one program can serve a Circuit and several computations.
struct ComputationArtifact {
	std::shared_ptr<const BooleanProgram> program;
	IoSignature io;
};

// Structural and role checks; error()s (fatal) on the first violation:
//   - a non-null program that passes validate_program;
//   - every kind in range and every width positive;
//   - input widths summing to num_inputs, output widths summing to the number
//     of outputs, accumulated without overflow;
//   - party fields >= 1 exactly for the Party kinds, and PUBLIC otherwise.
// A session also rejects, before any traffic, party ids above its party count
// and anything else it cannot run.
inline void validate_computation(const ComputationArtifact& a) {
	expecting(a.program != nullptr, "validate_computation: null program");
	validate_program(*a.program);

	uint64_t in_bits = 0;
	for (std::size_t i = 0; i < a.io.inputs.size(); ++i) {
		const InputRole& r = a.io.inputs[i];
		const auto where = [&] { return "validate_computation: input slot " + std::to_string(i); };
		expecting(r.kind == InputKind::Party || r.kind == InputKind::Public,
		          [&] { return where() + " has an unknown kind"; });
		expecting(r.width != 0, [&] { return where() + " has zero width"; });
		if (r.kind == InputKind::Party)
			expecting(r.owner >= 1, [&] { return where() + " is Party but its owner is not a party id >= 1"; });
		else
			expecting(r.owner == PUBLIC, [&] { return where() + " is Public but its owner is not PUBLIC"; });
		expecting(r.width <= UINT32_MAX - in_bits,
		          "validate_computation: input slot widths overflow uint32_t");
		in_bits += r.width;
	}
	expecting(in_bits == (uint64_t)a.program->num_inputs,
	          "validate_computation: input slot widths do not sum to num_inputs");

	uint64_t out_bits = 0;
	for (std::size_t i = 0; i < a.io.outputs.size(); ++i) {
		const OutputRole& r = a.io.outputs[i];
		const auto where = [&] { return "validate_computation: output slot " + std::to_string(i); };
		expecting(r.kind == OutputKind::Party || r.kind == OutputKind::Public,
		          [&] { return where() + " has an unknown kind"; });
		expecting(r.width != 0, [&] { return where() + " has zero width"; });
		if (r.kind == OutputKind::Party)
			expecting(r.recipient >= 1, [&] { return where() + " is Party but its recipient is not a party id >= 1"; });
		else
			expecting(r.recipient == PUBLIC, [&] { return where() + " is Public but its recipient is not PUBLIC"; });
		expecting(r.width <= UINT32_MAX - out_bits,
		          "validate_computation: output slot widths overflow uint32_t");
		out_bits += r.width;
	}
	expecting(out_bits == (uint64_t)a.program->outputs.size(),
	          "validate_computation: output slot widths do not sum to the number of outputs");
}

// First wire of each input slot, plus num_inputs at the end (size inputs + 1).
inline std::vector<uint32_t> input_slot_offsets(const IoSignature& io) {
	std::vector<uint32_t> off(io.inputs.size() + 1, 0);
	for (std::size_t i = 0; i < io.inputs.size(); ++i) off[i + 1] = off[i] + io.inputs[i].width;
	return off;
}

// First position in program.outputs of each output slot, plus the number of
// outputs at the end (size outputs + 1).
inline std::vector<uint32_t> output_slot_offsets(const IoSignature& io) {
	std::vector<uint32_t> off(io.outputs.size() + 1, 0);
	for (std::size_t i = 0; i < io.outputs.size(); ++i) off[i + 1] = off[i] + io.outputs[i].width;
	return off;
}

// A versioned, length-framed byte string that identifies a computation:
//   'E','M','P','C', version u16 (= 1),
//   u32 length + the program's .empbc v1 bytes,
//   u32 input-slot count, then per slot: u32 width, u8 kind, u32 owner,
//   u32 output-slot count, then per slot: u32 width, u8 kind, u32 recipient.
// Integers are little-endian. Two recordings of the same computation give the
// same bytes. Tests compare it; no session exchanges it.
inline std::vector<uint8_t> canonical_encoding(const ComputationArtifact& a) {
	validate_computation(a);
	std::vector<uint8_t> out;
	const auto u8  = [&](uint8_t v) { out.push_back(v); };
	const auto u32 = [&](uint32_t v) {
		for (int i = 0; i < 4; ++i) out.push_back((uint8_t)((v >> (8 * i)) & 0xFF));
	};
	u8('E'); u8('M'); u8('P'); u8('C');
	u8(1); u8(0);
	const std::vector<uint8_t> prog = save_empbc(*a.program);
	expecting(prog.size() <= UINT32_MAX, "canonical_encoding: program encoding exceeds 4 GiB");
	u32((uint32_t)prog.size());
	out.insert(out.end(), prog.begin(), prog.end());
	u32((uint32_t)a.io.inputs.size());
	for (const InputRole& r : a.io.inputs) {
		u32(r.width); u8((uint8_t)r.kind); u32((uint32_t)r.owner);
	}
	u32((uint32_t)a.io.outputs.size());
	for (const OutputRole& r : a.io.outputs) {
		u32(r.width); u8((uint8_t)r.kind); u32((uint32_t)r.recipient);
	}
	return out;
}

}  // namespace circuit
}  // namespace emp
#endif  // EMP_IR_COMPUTATION_H__
