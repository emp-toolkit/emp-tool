#ifndef EMP_IR_PASSES_H__
#define EMP_IR_PASSES_H__

// Protocol-neutral analysis passes over a recorded BooleanProgram. Each is a
// pure function over the program; nothing here caches — a caller that replays
// repeatedly computes the pass once and keeps the result itself. They describe
// graph facts every backend can use; a backend reads the ones it needs
// (ag2pc uses liveness; round-sensitive protocols can use the schedule) and
// ignores the rest.
//
// Wire ids in the IR are uint32_t, but GATE indices are carried as int64_t
// where a -1 "none" sentinel is needed (first def / last use) — gate counts up
// to 2^32-1 are representable, so a plain int would silently overflow past
// 2^31. This is local analysis state, never serialized. An input wire is any
// w < num_inputs (it has no producing gate).

#include "emp-tool/ir/program.h"
#include "emp-tool/runtime/core/utils.h"   // error()
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace emp {
namespace circuit {

// liveness/schedule/layout index per-wire arrays last-writer-wins; on a reused
// id they would silently collapse distinct lifetimes and misorder the replay.
// They are defined only for dense (WireReuse::None) programs — compaction
// (ir/transform.h) runs them on its dense input, never on its reuse output.
inline void require_dense(const BooleanProgram& p, const char* who) {
	expecting(p.wire_reuse == WireReuse::None, [&] {
		return std::string(who) + ": requires a dense (WireReuse::None) program";
	});
}

// AND-depth scheduling metadata (filled by schedule_pass). Useful to protocols
// whose latency/round count depends on minimizing AND-depth; unused by
// constant-round backends.
struct LevelInfo {
	std::vector<std::vector<uint32_t>> and_gate_indices;  // gate indices per AND-depth level
	int depth = 0;                                        // max AND-depth
};

struct CountStats {
	int64_t num_and = 0, num_xor = 0, num_not = 0, num_const = 0;
	int64_t num_input_bits = 0, num_output_bits = 0;
	int64_t num_wire = 0, num_gate = 0;
};

inline CountStats count_pass(const BooleanProgram& p) {
	CountStats s;
	s.num_wire = (int64_t)p.num_wires;
	s.num_gate = (int64_t)p.num_gate();
	s.num_input_bits  = (int64_t)p.total_input_bits();
	s.num_output_bits = (int64_t)p.total_output_bits();
	for (const auto& g : p.gates) {
		switch (g.op) {
			case Op::And:    ++s.num_and;   break;
			case Op::Xor:    ++s.num_xor;   break;
			case Op::Not:    ++s.num_not;   break;
			case Op::Const0:
			case Op::Const1: ++s.num_const; break;
		}
	}
	return s;
}

struct LivenessStats {
	std::vector<int64_t> last_use;   // [num_wires] last gate index reading w, -1 if never read
	std::vector<int64_t> first_def;  // [num_wires] producing gate index, -1 if an input wire
};

inline LivenessStats liveness_pass(const BooleanProgram& p) {
	require_dense(p, "liveness_pass");
	LivenessStats s;
	s.last_use.assign(p.num_wires, -1);
	s.first_def.assign(p.num_wires, -1);
	const uint32_t G = p.num_gate();
	for (uint32_t gi = 0; gi < G; ++gi) {
		const Gate& g = p.gates[gi];
		s.first_def[g.out] = (int64_t)gi;
		if (g.is_const()) continue;                       // CONST reads nothing
		s.last_use[g.in0] = (int64_t)gi;
		if (!g.is_not()) s.last_use[g.in1] = (int64_t)gi;
	}
	return s;
}

struct ScheduleStats {
	std::vector<int> wire_level;   // [num_wires] AND-depth at which w becomes available
	LevelInfo        levels;       // AND gate indices grouped per level + max depth
};

inline ScheduleStats schedule_pass(const BooleanProgram& p) {
	require_dense(p, "schedule_pass");
	ScheduleStats s;
	s.wire_level.assign(p.num_wires, 0);
	const uint32_t G = p.num_gate();
	int depth = 0;
	for (uint32_t gi = 0; gi < G; ++gi) {
		const Gate& g = p.gates[gi];
		int lv = 0;
		switch (g.op) {
			case Op::Const0:
			case Op::Const1: lv = 0; break;
			case Op::Not:    lv = s.wire_level[g.in0]; break;
			case Op::Xor:    lv = std::max(s.wire_level[g.in0], s.wire_level[g.in1]); break;
			case Op::And:    lv = 1 + std::max(s.wire_level[g.in0], s.wire_level[g.in1]); break;
		}
		s.wire_level[g.out] = lv;
		depth = std::max(depth, lv);
	}
	s.levels.depth = depth;
	s.levels.and_gate_indices.assign(depth + 1, {});
	for (uint32_t gi = 0; gi < G; ++gi) {
		const Gate& g = p.gates[gi];
		if (g.is_and()) s.levels.and_gate_indices[s.wire_level[g.out]].push_back(gi);
	}
	return s;
}

struct LayoutStats {
	// Forward-consumable liveness: frees[gi] lists wire ids whose last read is gate
	// gi (the inverse of LivenessStats::last_use), so a forward gate walk can
	// recycle slots inline without scanning the array. Outputs and never-read
	// wires are absent (they are roots / dead-on-arrival).
	std::vector<std::vector<uint32_t>> frees;   // [num_gate]
	// Output-rooted dead-code-elimination sizes (what a backend rooted on the
	// revealed/returned outputs would actually execute).
	int64_t reachable_wire = 0;
	int64_t reachable_gate = 0;
	int64_t reachable_and  = 0;
};

inline LayoutStats layout_pass(const BooleanProgram& p) {
	require_dense(p, "layout_pass");
	const LivenessStats live = liveness_pass(p);
	LayoutStats s;
	// Output wires are roots: they must survive to output assembly, so they are
	// never freed — even if an internal gate also read them earlier (e.g. a body
	// that returns one of its inputs which a dead sub-expression also used).
	std::vector<char> is_output(p.num_wires, 0);
	for (uint32_t w : p.outputs)
		if (w < p.num_wires) is_output[w] = 1;
	s.frees.assign(p.num_gate(), {});
	for (uint32_t w = 0; w < p.num_wires; ++w)
		if (live.last_use[w] >= 0 && !is_output[w]) s.frees[(size_t)live.last_use[w]].push_back(w);

	// Output-rooted DCE: mark wires reachable backward from the output wires.
	std::vector<char> reach(p.num_wires, 0);
	std::vector<uint32_t> stack;
	for (uint32_t w : p.outputs)
		if (w < p.num_wires && !reach[w]) { reach[w] = 1; stack.push_back(w); }
	while (!stack.empty()) {
		uint32_t w = stack.back(); stack.pop_back();
		int64_t pg = live.first_def[w];
		if (pg < 0) continue;                  // input wire: no producer to walk
		const Gate& g = p.gates[(size_t)pg];
		auto visit = [&](uint32_t v) { if (!reach[v]) { reach[v] = 1; stack.push_back(v); } };
		if (!g.is_const()) {
			visit(g.in0);
			if (!g.is_not()) visit(g.in1);
		}
	}
	for (uint32_t w = 0; w < p.num_wires; ++w) if (reach[w]) ++s.reachable_wire;
	for (const auto& g : p.gates) {
		if (!reach[g.out]) continue;
		++s.reachable_gate;
		if (g.is_and()) ++s.reachable_and;
	}
	return s;
}

}  // namespace circuit
}  // namespace emp
#endif  // EMP_IR_PASSES_H__
