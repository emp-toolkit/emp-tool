#ifndef EMP_IR_ANALYSIS_H__
#define EMP_IR_ANALYSIS_H__

// Aggregate, serialization-friendly analytics for BooleanProgram / .empbc
// inspection. Vector-valued passes remain in passes.h; this header reduces
// them to stable scalar metrics suitable for CLIs, manifests, and CI budgets.

#include "emp-tool/ir/context/digest.h"
#include "emp-tool/ir/passes.h"
#include "emp-tool/ir/transform.h"
#include "emp-tool/ir/validate.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace emp {
namespace circuit {

struct ProgramStats {
	WireReuse wire_reuse = WireReuse::None;
	uint64_t num_inputs = 0;
	uint64_t num_outputs = 0;
	uint64_t stored_num_wires = 0;
	uint64_t dense_num_wires = 0;
	uint64_t num_gates = 0;
	uint64_t num_and = 0;
	uint64_t num_xor = 0;
	uint64_t num_not = 0;
	uint64_t num_const = 0;
	uint64_t and_depth = 0;
	uint64_t max_and_per_level = 0;
	uint64_t reachable_wires = 0;
	uint64_t reachable_gates = 0;
	uint64_t reachable_and = 0;
	uint64_t peak_live_wires = 0;
	uint64_t max_fanout = 0;          // gate consumers; output roots excluded
	uint64_t stored_program_digest = 0;
	uint64_t dense_program_digest = 0;
};

inline const char *wire_reuse_name(WireReuse reuse) {
	switch (reuse) {
		case WireReuse::None:   return "none";
		case WireReuse::Linear: return "linear";
		case WireReuse::Full:   return "full";
	}
	return "unknown";
}

namespace analysis_detail {

inline uint64_t peak_live_wires(const BooleanProgram& dense,
								const LivenessStats& live) {
	std::vector<char> is_output(dense.num_wires, 0);
	for (uint32_t w : dense.outputs) is_output[w] = 1;

	uint64_t active = 0;
	for (uint32_t w = 0; w < dense.num_inputs; ++w)
		if (live.last_use[w] >= 0 || is_output[w]) ++active;
	uint64_t peak = active;

	for (size_t gi = 0; gi < dense.gates.size(); ++gi) {
		const Gate& g = dense.gates[gi];
		const bool keep_output = live.last_use[g.out] >= 0 || is_output[g.out];
		// The result exists while the gate is evaluated even when it is
		// dead-on-arrival and can be discarded immediately afterward.
		peak = std::max(peak, active + 1);
		if (keep_output) ++active;

		auto release = [&](uint32_t w) {
			if (!is_output[w] && live.last_use[w] == static_cast<int64_t>(gi))
				--active;
		};
		if (!g.is_const()) release(g.in0);
		if (!g.is_const() && !g.is_not() && g.in1 != g.in0) release(g.in1);
	}
	return peak;
}

}  // namespace analysis_detail

inline ProgramStats analyze_program(const BooleanProgram& stored) {
	validate_program(stored);
	std::optional<BooleanProgram> expanded;
	const BooleanProgram *dense = &stored;
	if (stored.wire_reuse != WireReuse::None) {
		expanded.emplace(uncompact(stored));
		dense = &*expanded;
	}

	const CountStats count = count_pass(*dense);
	const LivenessStats live = liveness_pass(*dense);
	const ScheduleStats schedule = schedule_pass(*dense);
	const LayoutStats layout = layout_pass(*dense, live);

	ProgramStats out;
	out.wire_reuse = stored.wire_reuse;
	out.num_inputs = stored.num_inputs;
	out.num_outputs = stored.outputs.size();
	out.stored_num_wires = stored.num_wires;
	out.dense_num_wires = dense->num_wires;
	out.num_gates = count.num_gate;
	out.num_and = count.num_and;
	out.num_xor = count.num_xor;
	out.num_not = count.num_not;
	out.num_const = count.num_const;
	out.and_depth = static_cast<uint64_t>(schedule.levels.depth);
	for (const auto& level : schedule.levels.and_gate_indices)
		out.max_and_per_level = std::max<uint64_t>(out.max_and_per_level, level.size());
	out.reachable_wires = layout.reachable_wire;
	out.reachable_gates = layout.reachable_gate;
	out.reachable_and = layout.reachable_and;
	out.peak_live_wires = analysis_detail::peak_live_wires(*dense, live);
	out.stored_program_digest = digest_program(stored);
	out.dense_program_digest = digest_program(*dense);

	std::vector<uint64_t> fanout(dense->num_wires, 0);
	for (const Gate& g : dense->gates) {
		if (g.is_const()) continue;
		++fanout[g.in0];
		if (!g.is_not()) ++fanout[g.in1];
	}
	for (uint64_t n : fanout) out.max_fanout = std::max(out.max_fanout, n);
	return out;
}

}  // namespace circuit
}  // namespace emp

#endif  // EMP_IR_ANALYSIS_H__
