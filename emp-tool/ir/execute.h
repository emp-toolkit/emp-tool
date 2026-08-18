#ifndef EMP_IR_EXECUTE_H__
#define EMP_IR_EXECUTE_H__

// Replay an IR program over wire-valued slots. Two layers:
//   circuit::execute_program<Wire,Dispatcher> — the low-level in/out primitive: seed
//     input slots, walk once via for_each_gate, read output slots. Generic over
//     the wire slot type and a Dispatcher that realizes each op on slots.
//   execute_program(ctx, program, inputs[, ws]) — the value-return bridge that
//     drives the primitive from any BooleanContext (the way stored .empbc
//     builtins run through ClearCtx / a protocol backend / etc.).

#include "emp-tool/ir/program.h"
#include "emp-tool/ir/visit.h"
#include "emp-tool/ir/context/concept.h"
#include "emp-tool/runtime/core/utils.h"   // error()
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace emp {
namespace circuit {

// Reusable wire buffer so repeated executions of cached programs (e.g. float
// builtins) don't reallocate. Caller-owned or thread_local — never one shared
// mutable process-wide instance (immutable loaded *programs* may be shared, the
// scratch may not).
template <class Wire>
struct CircuitScratch {
	std::vector<Wire> wires;
	void ensure(uint32_t n) { if (wires.size() < n) wires.resize(n); }
};

// The in/out wrapper over for_each_gate, for callers whose wires carry values:
// copy `num_in` input values into wire slots [0, num_inputs), walk once, copy the
// output slots into `outputs`. Generic over the wire slot type and a Dispatcher
// that realizes each op on slots:
//
//   void and_gate(Wire& out, const Wire& a, const Wire& b);
//   void xor_gate(Wire& out, const Wire& a, const Wire& b);
//   void not_gate(Wire& out, const Wire& a);
//   void const_gate(Wire& out, bool value);
//
	// Pointer+count keeps this low-level primitive easy to call from adapters.
template <class Wire, class Dispatcher>
inline void execute_program(const BooleanProgram& p,
                            const Wire* inputs, size_t num_in,
                            Wire* outputs, size_t num_out,
                            CircuitScratch<Wire>& scratch,
                            Dispatcher&& dispatch) {
	expecting(num_in == p.num_inputs,
	          "execute_program: input count != program num_inputs");
	expecting(num_out == p.outputs.size(),
	          "execute_program: output count != program outputs");

	scratch.ensure(p.num_wires);
	Wire* w = scratch.wires.data();
	for (size_t i = 0; i < num_in; ++i) w[i] = inputs[i];

	// Bridge wire ids (from for_each_gate) to value slots (for the Dispatcher).
	struct Bridge {
		Wire* w; Dispatcher& d;
		void and_gate(uint32_t o, uint32_t a, uint32_t b) { d.and_gate(w[o], w[a], w[b]); }
		void xor_gate(uint32_t o, uint32_t a, uint32_t b) { d.xor_gate(w[o], w[a], w[b]); }
		void not_gate(uint32_t o, uint32_t a)             { d.not_gate(w[o], w[a]); }
		void const_gate(uint32_t o, bool v)               { d.const_gate(w[o], v); }
	};
	for_each_gate(p, Bridge{w, dispatch});

	for (size_t i = 0; i < num_out; ++i) outputs[i] = w[p.outputs[i]];
}

}  // namespace circuit

// ---------------------------------------------------------------------------
// Reusable replay workspace — avoids per-call allocation once protocols depend
// on replay. `tmp_inputs` is for callers that assemble an input vector before
// replay (e.g. Float::binop_ concatenates 2*W operand wires).
// ---------------------------------------------------------------------------
template <class Wire>
struct ProgramWorkspace {
	circuit::CircuitScratch<Wire> scratch;   // wire slots
	std::vector<Wire> out;                     // output wires
	std::vector<Wire> tmp_inputs;              // caller-assembled inputs
	std::vector<Wire> ba, bb, bo;              // scheduled AND-batch buffers
	std::vector<uint32_t> bouts;
};

// Adapter: drive the in-place IR primitive from a value-return BooleanContext.
template <class Ctx>
struct CtxReplayAdapter {
	using W = typename Ctx::Wire;
	Ctx& c;
	void and_gate(W& o, const W& a, const W& b) { o = c.and_gate(a, b); }
	void xor_gate(W& o, const W& a, const W& b) { o = c.xor_gate(a, b); }
	void not_gate(W& o, const W& a)             { o = c.not_gate(a); }
	void const_gate(W& o, bool v)               { o = c.public_bit(v); }
};

// Value-return replay bridge (workspace form): writes outputs into ws.out and
// returns a reference to it (no allocation when ws is reused). This is how stored
// builtins run through any context.
template <BooleanContext Ctx>
inline const std::vector<typename Ctx::Wire>& execute_program(
    Ctx& ctx, const circuit::BooleanProgram& p,
    std::span<const typename Ctx::Wire> inputs,
    ProgramWorkspace<typename Ctx::Wire>& ws) {
	using W = typename Ctx::Wire;
	ws.out.resize(p.outputs.size());
	CtxReplayAdapter<Ctx> adapter{ctx};
	circuit::execute_program<W>(p, inputs.data(), inputs.size(),
	                            ws.out.data(), ws.out.size(), ws.scratch, adapter);
	return ws.out;
}

// Convenience: allocate a one-shot workspace and return the outputs by value.
template <BooleanContext Ctx>
inline std::vector<typename Ctx::Wire> execute_program(
    Ctx& ctx, const circuit::BooleanProgram& p,
    std::span<const typename Ctx::Wire> inputs) {
	ProgramWorkspace<typename Ctx::Wire> ws;
	execute_program(ctx, p, inputs, ws);
	return std::move(ws.out);
}

}  // namespace emp
#endif  // EMP_IR_EXECUTE_H__
