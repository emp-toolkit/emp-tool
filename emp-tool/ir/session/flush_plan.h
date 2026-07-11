#ifndef EMP_IR_SESSION_FLUSH_PLAN_H__
#define EMP_IR_SESSION_FLUSH_PLAN_H__

// Pure, network-free planning for a direct-chunk flush: DCE + compaction + stale
// detection over the recorded chunk, producing a canonical BooleanProgram plus the
// recorder-id input/output ordering. No protocol / carried state / sockets, so the
// trickiest direct-chunk logic is protocol- and party-count-agnostic and unit-
// testable. The owning Session (e.g. AG2PCSession, AGMPCSession) calls it with an
// is_materialized predicate over its carried_ before any crypto runs. Shared by
// every chunked session over ChunkRecorderCtx.

#include "emp-tool/ir/program.h"   // circuit::Gate, BooleanProgram
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace emp {
namespace session {

struct FlushPlan {
  circuit::BooleanProgram prog;        // compacted; gate i has out == num_inputs + i
  std::vector<uint32_t> input_ids;     // recorder ids feeding program inputs, in order
  std::vector<uint32_t> output_ids;    // pending keep recorder ids = program outputs, in order
  bool ok = true;
};

// Given the recorded chunk gates, the recorder ids to keep, and a predicate telling
// whether a NON-chunk id is materialized, compute the compacted canonical program
// plus the recorder-id ordering for its inputs and outputs. `ok` is false iff a
// needed gate operand is neither chunk-local nor materialized (a stale wire) — the
// caller turns that into a loud error.
template <class IsMaterialized>
inline FlushPlan plan_flush(const std::vector<circuit::Gate>& chunk_gates,
                            const std::vector<uint32_t>& keep_ids,
                            IsMaterialized&& is_materialized) {
  FlushPlan plan;
  const int G = (int)chunk_gates.size();
  std::unordered_map<uint32_t, int> wire_to_gate;
  wire_to_gate.reserve((size_t)G);   // holds exactly one entry per gate
  for (int gi = 0; gi < G; ++gi) wire_to_gate[chunk_gates[gi].out] = gi;

  // Reachability from the pending keep ids.
  std::vector<char> needed((size_t)G, 0);
  std::vector<uint32_t> stack;
  for (uint32_t id : keep_ids)
    if (wire_to_gate.count(id)) stack.push_back(id);
  while (!stack.empty()) {
    uint32_t w = stack.back(); stack.pop_back();
    auto it = wire_to_gate.find(w);
    if (it == wire_to_gate.end()) continue;     // carried operand (program input)
    int gi = it->second;
    if (needed[gi]) continue;
    needed[gi] = 1;
    const circuit::Gate& g = chunk_gates[gi];
    if (!g.is_const()) {                         // const operands are normalized-0 dummies
      stack.push_back(g.in0);
      if (!g.is_not()) stack.push_back(g.in1);
    }
  }

  // Carried (materialized) operands of needed gates become program inputs,
  // numbered [0, num_inputs) in first-seen order.
  std::unordered_map<uint32_t, uint32_t> remap;   // recorder id -> compact id
  auto note_input = [&](uint32_t v) {
    if (wire_to_gate.count(v)) return;            // chunk-local, not an input
    if (remap.count(v)) return;
    if (!is_materialized(v)) { plan.ok = false; return; }
    remap[v] = (uint32_t)plan.input_ids.size();
    plan.input_ids.push_back(v);
  };
  for (int gi = 0; gi < G; ++gi) {
    if (!needed[gi]) continue;
    const circuit::Gate& g = chunk_gates[gi];
    if (!g.is_const()) { note_input(g.in0); if (!g.is_not()) note_input(g.in1); }
  }
  const uint32_t num_inputs = (uint32_t)plan.input_ids.size();

  // Compact the needed gates in emission order; out_c == num_inputs + index, so
  // the program is canonical (what the engine requires).
  uint32_t cid = num_inputs;
  for (int gi = 0; gi < G; ++gi) {
    if (!needed[gi]) continue;
    const circuit::Gate& g = chunk_gates[gi];
    uint32_t out_c = cid++;
    remap[g.out] = out_c;
    uint32_t ni0 = g.is_const() ? 0u : remap[g.in0];
    uint32_t ni1 = (g.is_const() || g.is_not()) ? 0u : remap[g.in1];
    plan.prog.gates.push_back({ni0, ni1, out_c, g.op});
  }
  plan.prog.num_inputs = num_inputs;
  plan.prog.num_wires  = cid;

  // Outputs = the pending keep ids (each is a reachability root, hence needed),
  // deduped, in keep order.
  std::unordered_set<uint32_t> emitted;
  for (uint32_t id : keep_ids) {
    if (!wire_to_gate.count(id)) continue;        // already materialized
    if (!emitted.insert(id).second) continue;
    plan.prog.outputs.push_back(remap[id]);
    plan.output_ids.push_back(id);
  }
  return plan;
}

}  // namespace session
}  // namespace emp
#endif  // EMP_IR_SESSION_FLUSH_PLAN_H__
