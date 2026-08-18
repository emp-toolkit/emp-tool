#ifndef EMP_CONTEXT_COMPOSE_H__
#define EMP_CONTEXT_COMPOSE_H__

// ComposeCtx records glue gates and referenced unit calls in emission order.
// Calling a compiled unit records its wiring without inlining its gate stream.
//
// Like RecordCtx: reserve inputs via external_input() BEFORE any gate/call, then
// finish(outputs). Wires are uint32_t logical ids in the composition's own space.

#include "emp-tool/ir/context/concept.h"
#include "emp-tool/ir/program.h"
#include "emp-tool/ir/validate.h"          // validate_program (flatten_compose)
#include "emp-tool/runtime/core/utils.h"   // error()
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <unordered_set>
#include <vector>

namespace emp {

// One opaque unit call: a shared, co-owning handle on a unit program plus the
// composition wire ids it reads (in_ids, one per unit
// input) and the fresh composition wire ids it produces (out_ids, one
// per unit output bit). The plan co-owns each unit, so it stays valid
// after the source circuits go out of scope; identical units (a tiled
// chain) share one control block.
struct ComposeInstance {
	std::shared_ptr<const circuit::BooleanProgram> unit;
	std::vector<uint32_t> in_ids;
	std::vector<uint32_t> out_ids;
};

// An event in emission order: a glue gate (instance == -1) or a unit call
// (instance = index into ComposePlan::instances).
struct ComposeEvent {
	circuit::Gate gate{};   // meaningful iff instance == -1
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

// Check the plan-level wiring without walking any unit gate stream. ComposeCtx
// emits fresh logical ids monotonically, and each instance is emitted exactly
// once, so those canonical properties make availability and single-definition
// checks a single pass over the events and their wiring.
inline void validate_compose_structure(const ComposePlan& plan) {
	expecting(plan.num_inputs <= plan.num_wires,
	          "validate_compose: num_inputs exceeds num_wires");
	constexpr uint64_t max_instance_count =
	    static_cast<uint64_t>(std::numeric_limits<int>::max()) + 1;
	expecting(plan.instances.size() <= max_instance_count,
	          "validate_compose: too many instances for event indices");

	std::vector<uint8_t> seen(plan.instances.size(), 0);
	uint64_t next = plan.num_inputs;
	auto require_defined = [&](uint32_t id, const char* what) {
		expecting(id < next, what);
	};
	for (std::size_t ei = 0; ei < plan.events.size(); ++ei) {
		const ComposeEvent& ev = plan.events[ei];
		if (ev.instance == -1) {
			const circuit::Gate& g = ev.gate;
			circuit::validate_gate_operands(g, ei);
			if (!g.is_const()) {
				require_defined(g.in0,
				                "validate_compose: glue input is not yet defined");
				if (!g.is_not())
					require_defined(g.in1,
					                "validate_compose: glue input is not yet defined");
			}
			expecting(next < std::numeric_limits<uint32_t>::max(),
			          "validate_compose: composition wire id overflow");
			expecting(g.out == next,
			          "validate_compose: glue output is not the next canonical wire id");
			++next;
			continue;
		}

		expecting(ev.instance >= 0,
		          "validate_compose: invalid negative event instance");
		const std::size_t ii = static_cast<std::size_t>(ev.instance);
		expecting(ii < plan.instances.size(),
		          "validate_compose: event instance index out of range");
		expecting(seen[ii] == 0,
		          "validate_compose: instance is emitted more than once");
		seen[ii] = 1;
		const ComposeInstance& inst = plan.instances[ii];
		expecting(inst.unit != nullptr,
		          "validate_compose: instance has a null unit");
		expecting(inst.in_ids.size() == inst.unit->num_inputs,
		          "validate_compose: instance input width does not match its unit");
		expecting(inst.out_ids.size() == inst.unit->outputs.size(),
		          "validate_compose: instance output width does not match its unit");
		for (uint32_t id : inst.in_ids)
			require_defined(id,
			                "validate_compose: instance input is not yet defined");
		expecting(inst.out_ids.size() <=
		              std::numeric_limits<uint32_t>::max() - next,
		          "validate_compose: composition wire id overflow");
		for (uint32_t id : inst.out_ids) {
			expecting(id == next,
			          "validate_compose: instance output is not the next canonical wire id");
			++next;
		}
	}

	for (uint8_t was_seen : seen)
		expecting(was_seen != 0,
		          "validate_compose: instance has no event");
	expecting(next == plan.num_wires,
	          "validate_compose: num_wires does not match the emitted plan");
	for (uint32_t id : plan.outputs)
		require_defined(id,
		                "validate_compose: output wire is not defined");
}

// Full consumption boundary: also validate each immutable unit once, even when
// a tiled plan references it many times.
inline void validate_compose(const ComposePlan& plan) {
	validate_compose_structure(plan);
	std::unordered_set<const circuit::BooleanProgram*> checked;
	for (const ComposeInstance& inst : plan.instances)
		if (checked.insert(inst.unit.get()).second)
			circuit::validate_program(*inst.unit);
}

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

	// Record one call of `unit` reading `in` (length == unit.num_inputs) and
	// return fresh wire ids for its outputs.
	std::vector<Wire> call_unit(std::shared_ptr<const circuit::BooleanProgram> unit,
	                            const Wire* in, size_t n) {
		inputs_closed = true;
		expecting(unit != nullptr, "ComposeCtx::call_unit: null unit");
		expecting(n == unit->num_inputs,
		          "ComposeCtx::call_unit: argument width != unit num_inputs");
		expecting(n == 0 || in != nullptr,
		          "ComposeCtx::call_unit: null input array");
		expecting(plan.instances.size() <=
		              static_cast<size_t>(std::numeric_limits<int>::max()),
		          "ComposeCtx::call_unit: instance index overflow");
		expecting(unit->outputs.size() <=
		              static_cast<size_t>(UINT32_MAX - next_id),
		          "ComposeCtx::call_unit: wire id overflow");
		std::vector<Wire> outs(unit->outputs.size());
		for (auto& o : outs) o = alloc_();
		int idx = (int)plan.instances.size();
		std::vector<uint32_t> in_ids;
		if (n != 0) in_ids.assign(in, in + n);
		plan.instances.push_back(
		    ComposeInstance{std::move(unit), std::move(in_ids), outs});
		plan.events.push_back(ComposeEvent{circuit::Gate{}, idx});
		return outs;
	}

	ComposePlan& finish(std::span<const Wire> outputs) {
		plan.num_inputs = num_inputs;
		plan.num_wires  = next_id;
		plan.outputs.assign(outputs.begin(), outputs.end());
		validate_compose_structure(plan);
		return plan;
	}
};

static_assert(BooleanContext<ComposeCtx>);

// Materialize a ComposePlan into a flat dense BooleanProgram by inlining every
// unit instance into a fresh dense wire block.
inline circuit::BooleanProgram flatten_compose(const ComposePlan& plan) {
	validate_compose(plan);
	uint64_t flat_wires = plan.num_inputs;
	for (const ComposeEvent& ev : plan.events) {
		const uint64_t added = ev.instance == -1
		    ? 1
		    : plan.instances[static_cast<std::size_t>(ev.instance)].unit->gates.size();
		expecting(added <= std::numeric_limits<uint32_t>::max() - flat_wires,
		          "flatten_compose: flattened wire id overflow");
		flat_wires += added;
	}
	circuit::BooleanProgram d;
	d.num_inputs = plan.num_inputs;
	d.gates.reserve(static_cast<std::size_t>(flat_wires - plan.num_inputs));
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
