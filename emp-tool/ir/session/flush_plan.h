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
#include "emp-tool/ir/validate.h"
#include <cstddef>
#include <cstdint>
#include <limits>
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
  const char* error = nullptr;
};

// Given the recorded chunk gates, the recorder ids to keep, and a predicate telling
// whether a NON-chunk id is materialized, compute the compacted canonical program
// plus the recorder-id ordering for its inputs and outputs. `ok` is false when
// the recorded stream is structurally invalid or references stale state; `error`
// gives the short reason for the caller's fatal diagnostic.
template <class IsMaterialized>
inline FlushPlan plan_flush(const std::vector<circuit::Gate>& chunk_gates,
                            const std::vector<uint32_t>& keep_ids,
                            IsMaterialized&& is_materialized) {
  FlushPlan plan;
  auto fail = [&](const char* why) {
    plan.ok = false;
    plan.error = why;
  };

  if (chunk_gates.size() > std::numeric_limits<uint32_t>::max()) {
    fail("too many gates for uint32 wire indices");
    return plan;
  }

  const std::size_t G = chunk_gates.size();
  std::unordered_map<uint32_t, uint32_t> wire_to_gate;
  wire_to_gate.reserve(G);   // holds exactly one entry per gate
  for (std::size_t gi = 0; gi < G; ++gi) {
    const circuit::Gate& g = chunk_gates[gi];
    switch (g.op) {
      case circuit::Op::And:
      case circuit::Op::Xor:
        if (g.in0 == 0 || g.in1 == 0) {
          fail("non-constant gate has a null operand");
          return plan;
        }
        break;
      case circuit::Op::Not:
        if (g.in0 == 0 || g.in1 != 0) {
          fail("Not gate has a null or non-canonical operand");
          return plan;
        }
        break;
      case circuit::Op::Const0:
      case circuit::Op::Const1:
        if (g.in0 != 0 || g.in1 != 0) {
          fail("constant gate has non-canonical operands");
          return plan;
        }
        break;
      default:
        fail("unknown gate operation");
        return plan;
    }
    if (g.out == 0) {
      fail("gate writes the null recorder id");
      return plan;
    }
    if (!wire_to_gate.emplace(g.out, static_cast<uint32_t>(gi)).second) {
      fail("recorder id is produced by more than one gate");
      return plan;
    }
  }

  for (uint32_t id : keep_ids) {
    if (id == 0) {
      fail("keep set contains the null recorder id");
      return plan;
    }
    if (!wire_to_gate.count(id) && !is_materialized(id)) {
      fail("keep set contains a stale recorder id");
      return plan;
    }
  }

  // Reachability from the pending keep ids.
  std::vector<char> needed(G, 0);
  std::vector<uint32_t> stack;
  for (uint32_t id : keep_ids)
    if (wire_to_gate.count(id)) stack.push_back(id);
  while (!stack.empty()) {
    uint32_t w = stack.back(); stack.pop_back();
    auto it = wire_to_gate.find(w);
    if (it == wire_to_gate.end()) continue;     // carried operand (program input)
    uint32_t gi = it->second;
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
  auto note_input = [&](uint32_t v) -> bool {
    if (wire_to_gate.count(v)) return true;       // chunk-local, not an input
    if (remap.count(v)) return true;
    if (!is_materialized(v)) {
      fail("gate operand has no carried state");
      return false;
    }
    if (plan.input_ids.size() >= std::numeric_limits<uint32_t>::max()) {
      fail("too many carried inputs for uint32 wire indices");
      return false;
    }
    remap.emplace(v, static_cast<uint32_t>(plan.input_ids.size()));
    plan.input_ids.push_back(v);
    return true;
  };
  std::size_t needed_gates = 0;
  for (std::size_t gi = 0; gi < G; ++gi) {
    if (!needed[gi]) continue;
    ++needed_gates;
    const circuit::Gate& g = chunk_gates[gi];
    if (!g.is_const()) {
      if (!note_input(g.in0)) return plan;
      if (!g.is_not() && !note_input(g.in1)) return plan;
    }
  }
  const uint32_t num_inputs = static_cast<uint32_t>(plan.input_ids.size());

  if (needed_gates > std::numeric_limits<uint32_t>::max() - num_inputs) {
    fail("compacted program exceeds uint32 wire indices");
    return plan;
  }

  // Compact the needed gates in emission order; out_c == num_inputs + index, so
  // the program is canonical (what the engine requires).
  uint32_t cid = num_inputs;
  auto mapped = [&](uint32_t id, uint32_t& compact) -> bool {
    auto it = remap.find(id);
    if (it == remap.end()) {
      fail("needed wire has no compact mapping");
      return false;
    }
    compact = it->second;
    return true;
  };
  for (std::size_t gi = 0; gi < G; ++gi) {
    if (!needed[gi]) continue;
    const circuit::Gate& g = chunk_gates[gi];
    uint32_t out_c = cid++;
    remap.emplace(g.out, out_c);
    uint32_t ni0 = 0;
    uint32_t ni1 = 0;
    if (!g.is_const() && !mapped(g.in0, ni0)) return plan;
    if (!g.is_const() && !g.is_not() && !mapped(g.in1, ni1)) return plan;
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
    uint32_t output = 0;
    if (!mapped(id, output)) return plan;
    plan.prog.outputs.push_back(output);
    plan.output_ids.push_back(id);
  }
#ifndef NDEBUG
  circuit::validate_program(plan.prog);
#endif
  return plan;
}

}  // namespace session
}  // namespace emp
#endif  // EMP_IR_SESSION_FLUSH_PLAN_H__
