#ifndef EMP_CONTEXT_COMPOSE_H__
#define EMP_CONTEXT_COMPOSE_H__

// ComposeCtx — a BooleanContext that records a HIERARCHICAL circuit: ordinary
// gates ("glue") interleaved with OPAQUE unit calls (call_unit), in emission
// order. It is where run(ctx, unit, args) records when ctx is a ComposeCtx: calling
// a pre-compiled unit records a reference + its wiring (O(width)) instead of inlining
// the unit's gates, so a composition of N units records O(N) instances, never the flattened
// N*gates program. A backend that wants the flat circuit replays each instance's
// gate stream on the fly (e.g. emp-ag2pc's trust path) — nothing is materialized.
//
// Like RecordCtx: reserve inputs via external_input() BEFORE any gate/call, then
// finish(outputs). Wires are uint32_t logical ids in the composition's own space.

#include "emp-tool/ir/context/concept.h"
#include "emp-tool/ir/program.h"
#include "emp-tool/ir/validate.h"          // validate_program (flatten_compose)
#include "emp-tool/runtime/core/utils.h"   // error()
#include <cstdint>
#include <span>
#include <vector>

namespace emp {

// One opaque unit call: a reference to a (Linear) unit program plus the
// composition wire ids it reads (in_ids, one per unit input) and the fresh
// composition wire ids it produces (out_ids, one per unit output bit).
struct ComposeInstance {
	const circuit::BooleanProgram* unit = nullptr;
	std::vector<uint32_t> in_ids;
	std::vector<uint32_t> out_ids;
};

// An event in emission order: a glue gate (instance < 0) or a unit call
// (instance = index into ComposePlan::instances).
struct ComposeEvent {
	circuit::Gate gate{};   // meaningful iff instance < 0
	int instance = -1;
};

// The recorded composition: external inputs are wire ids [0, num_inputs); every
// other id is produced by a glue gate or a unit-call output; `events` is the
// emission order a flattener must follow.
struct ComposePlan {
	uint32_t num_inputs = 0;   // external inputs are wires [0, num_inputs)
	uint32_t num_wires  = 0;   // total composition wire ids
	std::vector<ComposeEvent>    events;
	std::vector<ComposeInstance> instances;
	std::vector<uint32_t>        outputs;
};

struct ComposeCtx {
	using Wire = uint32_t;
	ComposePlan plan;
	uint32_t next_id = 0, num_inputs = 0;
	int64_t c0 = -1, c1 = -1;     // dedup the two constant wires (glue)
	bool inputs_closed = false;

	uint32_t alloc_() {
		inputs_closed = true;
		expecting(next_id != UINT32_MAX, "ComposeCtx: wire id overflow");
		return next_id++;
	}
	Wire external_input(size_t n) {
		expecting(!inputs_closed,
		          "ComposeCtx::external_input: called after a gate/call was emitted");
		expecting(n != 0, "ComposeCtx::external_input: zero-width argument");
		expecting(n <= static_cast<size_t>(UINT32_MAX - next_id),
		          "ComposeCtx::external_input: wire id overflow");
		Wire base = next_id; next_id += (uint32_t)n; num_inputs += (uint32_t)n; return base;
	}

	void emit_(circuit::Gate g) { plan.events.push_back(ComposeEvent{g, -1}); }
	Wire public_bit(bool v) {
		int64_t& c = v ? c1 : c0;
		if (c < 0) { c = alloc_(); emit_({0, 0, (uint32_t)c, v ? circuit::Op::Const1 : circuit::Op::Const0}); }
		return (Wire)c;
	}
	Wire and_gate(Wire a, Wire b) { Wire o = alloc_(); emit_({a, b, o, circuit::Op::And}); return o; }
	Wire xor_gate(Wire a, Wire b) { Wire o = alloc_(); emit_({a, b, o, circuit::Op::Xor}); return o; }
	Wire not_gate(Wire a)         { Wire o = alloc_(); emit_({a, 0, o, circuit::Op::Not}); return o; }

	// Record one opaque call of `unit` reading `in` (length == unit.num_inputs);
	// returns fresh wire ids for the unit's outputs. The unit's gates are NOT
	// walked here — only the reference + wiring are kept.
	std::vector<Wire> call_unit(const circuit::BooleanProgram& unit, const Wire* in, size_t n) {
		inputs_closed = true;
		expecting(n == unit.num_inputs,
		          "ComposeCtx::call_unit: argument width != unit num_inputs");
		std::vector<Wire> outs(unit.outputs.size());
		for (auto& o : outs) o = alloc_();
		int idx = (int)plan.instances.size();
		plan.instances.push_back(ComposeInstance{&unit, std::vector<uint32_t>(in, in + n), outs});
		plan.events.push_back(ComposeEvent{circuit::Gate{}, idx});
		return outs;
	}

	ComposePlan& finish(std::span<const Wire> outputs) {
		plan.num_inputs = num_inputs;
		plan.num_wires  = next_id;
		plan.outputs.assign(outputs.begin(), outputs.end());
		return plan;
	}
};

static_assert(BooleanContext<ComposeCtx>);

// Materialize a ComposePlan into a flat dense BooleanProgram by inlining every
// instance's unit (each instance renumbered into a fresh dense block; unit inputs
// alias the wired composition ids; reused unit ids densify naturally). This is the
// generic fallback for a backend with no on-the-fly flattener, and the oracle the
// trust path is checked against. It DOES materialize (O(total gates)) — the win
// path (emp-ag2pc) consumes the plan directly instead.
inline circuit::BooleanProgram flatten_compose(const ComposePlan& plan) {
	circuit::BooleanProgram d;
	d.num_inputs = plan.num_inputs;
	std::vector<uint32_t> cmap(plan.num_wires, UINT32_MAX);   // composition id -> dense id
	uint32_t next = 0;
	for (uint32_t c = 0; c < plan.num_inputs; ++c) cmap[c] = next++;
	for (const ComposeEvent& ev : plan.events) {
		if (ev.instance < 0) {
			const circuit::Gate& g = ev.gate;
			circuit::Gate ng = g;
			ng.in0 = g.is_const() ? 0 : cmap[g.in0];
			ng.in1 = (g.is_const() || g.is_not()) ? 0 : cmap[g.in1];
			ng.out = next++;
			cmap[g.out] = ng.out;
			d.gates.push_back(ng);
		} else {
			const ComposeInstance& inst = plan.instances[(size_t)ev.instance];
			const circuit::BooleanProgram& U = *inst.unit;
			std::vector<uint32_t> umap(U.num_wires, UINT32_MAX);   // unit id -> dense id
			for (uint32_t j = 0; j < U.num_inputs; ++j) umap[j] = cmap[inst.in_ids[j]];
			for (const circuit::Gate& g : U.gates) {
				circuit::Gate ng = g;
				ng.in0 = g.is_const() ? 0 : umap[g.in0];
				ng.in1 = (g.is_const() || g.is_not()) ? 0 : umap[g.in1];
				ng.out = next++;
				umap[g.out] = ng.out;   // reused unit ids densify (last writer wins)
				d.gates.push_back(ng);
			}
			for (size_t k = 0; k < U.outputs.size(); ++k) cmap[inst.out_ids[k]] = umap[U.outputs[k]];
		}
	}
	d.num_wires = next;
	for (uint32_t c : plan.outputs) d.outputs.push_back(cmap[c]);
	circuit::validate_program(d);
	return d;
}

}  // namespace emp
#endif  // EMP_CONTEXT_COMPOSE_H__
