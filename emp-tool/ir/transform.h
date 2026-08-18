#ifndef EMP_IR_TRANSFORM_H__
#define EMP_IR_TRANSFORM_H__

// Wire-id compaction: rewrite a dense (WireReuse::None) program into one that
// REUSES wire ids — a wire's id is recycled once it is dead, so a later gate
// overwrites that slot. Driven by the IR's own liveness (computed on the dense
// input, never on a reuse program — see ir/passes.h). make_compact moves the
// register allocation a backend would otherwise do at runtime to compile time;
// uncompact is the inverse. Compaction NEVER changes the gate sequence or the
// function computed — only the wire ids — so execute_program over the result
// yields identical outputs (covered by ir tests).
//
// Levels (ir/program.h WireReuse): Linear pins And outputs (a multi-pass backend
// stores their value across passes, so their ids must not be recycled); Full
// recycles And outputs too (maximal, correct only for single-/recompute-pass
// execution). Unlike the analysis passes, this is a transform: it produces a new
// program, so it lives in its own header (passes.h is pure analysis).

#include "emp-tool/ir/program.h"
#include "emp-tool/ir/passes.h"           // liveness_pass, LivenessStats
#include "emp-tool/ir/validate.h"         // validate_program
#include "emp-tool/runtime/core/utils.h"  // error()
#include <cstdint>
#include <vector>

namespace emp {
namespace circuit {

struct CompactResult {
	BooleanProgram        prog;   // .wire_reuse == the requested target
	std::vector<uint32_t> nid;    // nid[dense_id] = compact_id  (size = dense.num_wires)
};

// Compact a dense program to `target` (Linear or Full). Precondition: `dense` is
// a valid WireReuse::None program (errors otherwise). The freelist discipline
// (LIFO, recycle a wire only AFTER the gate that last reads it) mirrors what a
// runtime register allocator would do, so a Linear result reproduces a backend's
// own slot assignment.
inline CompactResult make_compact(const BooleanProgram& dense, WireReuse target = WireReuse::Full) {
	expecting(target != WireReuse::None,
	          "make_compact: target must be WireReuse::Linear or WireReuse::Full");
	expecting(dense.wire_reuse == WireReuse::None,
	          "make_compact: input must be a dense (WireReuse::None) program");
	validate_program(dense);
	const LivenessStats live = liveness_pass(dense);   // dense input: well-defined

	const uint32_t NW = dense.num_wires;
	std::vector<char> persist(NW, 0);
	for (uint32_t i = 0; i < dense.num_inputs; ++i) persist[i] = 1;     // inputs persist
	for (uint32_t w : dense.outputs) if (w < NW) persist[w] = 1;        // outputs persist
	if (target == WireReuse::Linear)
		for (const Gate& g : dense.gates) if (g.is_and()) persist[g.out] = 1;  // pin And outputs

	// frees[gi] = non-persistent wires whose last read is gate gi (recyclable
	// once gate gi has executed).
	std::vector<std::vector<uint32_t>> frees(dense.num_gate());
	for (uint32_t w = 0; w < NW; ++w)
		if (live.last_use[w] >= 0 && !persist[w]) frees[(size_t)live.last_use[w]].push_back(w);

	CompactResult r;
	r.nid.assign(NW, UINT32_MAX);
	r.prog.num_inputs = dense.num_inputs;
	r.prog.wire_reuse = target;
	r.prog.gates.reserve(dense.gates.size());
	r.prog.outputs.reserve(dense.outputs.size());

	uint32_t next = 0;
	std::vector<uint32_t> freelist;
	for (uint32_t i = 0; i < dense.num_inputs; ++i) r.nid[i] = next++;
	for (uint32_t gi = 0; gi < dense.num_gate(); ++gi) {
		const Gate& g = dense.gates[gi];
		uint32_t out;
		// A Linear AND output needs a never-used slot: a multi-pass backend may
		// retain per-AND state there. Every other output may take a free slot even
		// when the new value itself must survive to the program outputs.
		const bool needs_fresh = target == WireReuse::Linear && g.is_and();
		if (!needs_fresh && !freelist.empty()) { out = freelist.back(); freelist.pop_back(); }
		else out = next++;
		r.nid[g.out] = out;
		Gate ng; ng.op = g.op; ng.out = out;
		ng.in0 = g.is_const() ? 0 : r.nid[g.in0];
		ng.in1 = (g.is_const() || g.is_not()) ? 0 : r.nid[g.in1];
		r.prog.gates.push_back(ng);
		for (uint32_t w : frees[gi]) freelist.push_back(r.nid[w]);   // recycle AFTER the gate
		// A result with no reader and no output/Linear-AND persistence is dead as
		// soon as this gate finishes, so make its slot available to the next gate.
		if (!persist[g.out] && live.last_use[g.out] < 0) freelist.push_back(out);
	}
	r.prog.num_wires = next;
	for (uint32_t w : dense.outputs) r.prog.outputs.push_back(r.nid[w]);
	validate_program(r.prog);
	return r;
}

// Inverse: rewrite any program to dense WireReuse::None by relabeling each gate
// output to a fresh monotonic id. One forward walk; cur[id] tracks the dense id
// currently living in (reuse) slot `id`. No liveness needed. (On a program that
// is already dense this is the identity, modulo a fresh validate.)
inline BooleanProgram uncompact(const BooleanProgram& src) {
	validate_program(src);
	BooleanProgram d;
	d.num_inputs = src.num_inputs;
	d.wire_reuse = WireReuse::None;
	d.gates.reserve(src.gates.size());
	d.outputs.reserve(src.outputs.size());
	std::vector<uint32_t> cur(src.num_wires, UINT32_MAX);
	uint32_t next = 0;
	for (uint32_t i = 0; i < src.num_inputs; ++i) cur[i] = next++;
	for (const Gate& g : src.gates) {
		Gate ng; ng.op = g.op; ng.out = next++;
		ng.in0 = g.is_const() ? 0 : cur[g.in0];
		ng.in1 = (g.is_const() || g.is_not()) ? 0 : cur[g.in1];
		d.gates.push_back(ng);
		cur[g.out] = ng.out;
	}
	d.num_wires = next;
	for (uint32_t w : src.outputs) d.outputs.push_back(cur[w]);
	validate_program(d);
	return d;
}

}  // namespace circuit
}  // namespace emp
#endif  // EMP_IR_TRANSFORM_H__
