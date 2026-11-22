#ifndef EMP_IR_SESSION_FLUSH_PLAN_H__
#define EMP_IR_SESSION_FLUSH_PLAN_H__

// Pure, network-free planning for a direct-chunk flush: DCE + compaction + stale
// detection over the recorded chunk, producing a canonical BooleanProgram plus the
// recorder-id input/output ordering. No protocol / carried state / sockets, so the
// trickiest direct-chunk logic is protocol- and party-count-agnostic and unit-
// testable. The owning Session calls it with an is_materialized predicate over
// its carried_ before any crypto runs. Shared by every chunked session over
// ChunkRecorderCtx.

#include "emp-tool/ir/program.h"   // circuit::Gate, BooleanProgram
#include "emp-tool/ir/validate.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
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
//
// Gates are looked up by output id. A recorder allocates ids from one monotonic
// counter, so a recorded chunk's outputs lie in a span no wider than the ids
// allocated while it was recorded; such a span is indexed directly (one 4-byte
// slot per id, like the recorder's own refcount table). A chunk whose outputs are
// too sparse for that, such as a hand-built stream, is indexed by a sorted
// (id, gate) table instead, and both indexes give the same plan. Reachability is
// one reverse sweep: in a recorded chunk a gate's chunk-local operands are outputs
// of earlier gates, and a needed gate that reads a later gate's output is
// rejected.
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

  // NONE marks "no gate" / "no compact id": gate indices are below G, and compact
  // ids below UINT32_MAX (checked before compaction), so neither reaches it.
  constexpr uint32_t NONE = std::numeric_limits<uint32_t>::max();
  const std::size_t G = chunk_gates.size();
  uint32_t lo = NONE, hi = 0;
  for (const circuit::Gate& g : chunk_gates) {
    lo = std::min(lo, g.out);
    hi = std::max(hi, g.out);
  }
  const bool dense = G != 0 && (uint64_t)(hi - lo) < 4 * (uint64_t)G + 4096;
  std::vector<uint32_t> dense_index(dense ? (std::size_t)(hi - lo) + 1 : 0, NONE);
  std::vector<std::pair<uint32_t, uint32_t>> sparse_index;   // (out id, gate), sorted
  if (!dense) sparse_index.reserve(G);
  // The chunk gate that outputs id, or NONE when no chunk gate does.
  auto gate_of = [&](uint32_t id) -> uint32_t {
    if (dense) return (id >= lo && id <= hi) ? dense_index[id - lo] : NONE;
    auto it = std::lower_bound(sparse_index.begin(), sparse_index.end(),
                               std::make_pair(id, 0u));
    return (it != sparse_index.end() && it->first == id) ? it->second : NONE;
  };

  for (std::size_t gi = 0; gi < G; ++gi) {
    const circuit::Gate& g = chunk_gates[gi];
    if (static_cast<unsigned>(g.op) > static_cast<unsigned>(circuit::Op::Const1)) {
      fail("unknown gate operation");
      return plan;
    }
    switch (g.op) {   // no default: -Werror=switch flags a new Op here (visit.h)
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
    }
    if (g.out == 0) {
      fail("gate writes the null recorder id");
      return plan;
    }
    if (g.in0 == g.out || g.in1 == g.out) {
      fail("gate reads its own output");
      return plan;
    }
    if (dense) {
      uint32_t& slot = dense_index[g.out - lo];
      if (slot != NONE) {
        fail("recorder id is produced by more than one gate");
        return plan;
      }
      slot = static_cast<uint32_t>(gi);
    } else {
      sparse_index.emplace_back(g.out, static_cast<uint32_t>(gi));
    }
  }
  if (!dense) {
    std::sort(sparse_index.begin(), sparse_index.end());
    for (std::size_t i = 1; i < sparse_index.size(); ++i) {
      if (sparse_index[i].first == sparse_index[i - 1].first) {
        fail("recorder id is produced by more than one gate");
        return plan;
      }
    }
  }

  for (uint32_t id : keep_ids) {
    if (id == 0) {
      fail("keep set contains the null recorder id");
      return plan;
    }
    if (gate_of(id) == NONE && !is_materialized(id)) {
      fail("keep set contains a stale recorder id");
      return plan;
    }
  }

  // Reachability from the pending keep ids, one reverse sweep. Only a later gate
  // marks an earlier one, so a gate's mark is final when the sweep reaches it.
  std::vector<char> needed(G, 0);
  for (uint32_t id : keep_ids) {
    const uint32_t gi = gate_of(id);
    if (gi != NONE) needed[gi] = 1;
  }
  for (std::size_t gi = G; gi-- > 0;) {
    if (!needed[gi]) continue;
    const circuit::Gate& g = chunk_gates[gi];
    if (g.is_const()) continue;                  // const operands are normalized-0 dummies
    const uint32_t a = gate_of(g.in0);
    const uint32_t b = g.is_not() ? NONE : gate_of(g.in1);
    if ((a != NONE && a > gi) || (b != NONE && b > gi)) {
      fail("gate reads a later gate's output");
      return plan;
    }
    if (a != NONE) needed[a] = 1;
    if (b != NONE) needed[b] = 1;
  }

  // Carried (materialized) operands of needed gates become program inputs,
  // numbered [0, num_inputs) in first-seen order.
  std::unordered_map<uint32_t, uint32_t> input_index;   // carried recorder id -> input
  auto note_input = [&](uint32_t v) -> bool {
    if (gate_of(v) != NONE) return true;         // chunk-local, not an input
    if (input_index.count(v)) return true;
    if (!is_materialized(v)) {
      fail("gate operand has no carried state");
      return false;
    }
    if (plan.input_ids.size() >= std::numeric_limits<uint32_t>::max()) {
      fail("too many carried inputs for uint32 wire indices");
      return false;
    }
    input_index.emplace(v, static_cast<uint32_t>(plan.input_ids.size()));
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
  std::vector<uint32_t> compact(G, NONE);        // chunk gate -> compact output id
  uint32_t cid = num_inputs;
  auto mapped = [&](uint32_t id, uint32_t& c) -> bool {
    const uint32_t gi = gate_of(id);
    if (gi != NONE) {
      c = compact[gi];
    } else {
      auto it = input_index.find(id);
      c = it == input_index.end() ? NONE : it->second;
    }
    if (c == NONE) {
      fail("needed wire has no compact mapping");
      return false;
    }
    return true;
  };
  plan.prog.gates.reserve(needed_gates);
  for (std::size_t gi = 0; gi < G; ++gi) {
    if (!needed[gi]) continue;
    const circuit::Gate& g = chunk_gates[gi];
    // Operands resolve before the gate's own compact id is assigned, so a gate
    // can only read wires that earlier gates or inputs define.
    uint32_t ni0 = 0;
    uint32_t ni1 = 0;
    if (!g.is_const() && !mapped(g.in0, ni0)) return plan;
    if (!g.is_const() && !g.is_not() && !mapped(g.in1, ni1)) return plan;
    const uint32_t out_c = cid++;
    compact[gi] = out_c;
    plan.prog.gates.push_back({ni0, ni1, out_c, g.op});
  }
  plan.prog.num_inputs = num_inputs;
  plan.prog.num_wires  = cid;

  // Outputs = the pending keep ids (each is a reachability root, hence needed),
  // deduped, in keep order.
  std::vector<char> emitted(G, 0);
  for (uint32_t id : keep_ids) {
    const uint32_t gi = gate_of(id);
    if (gi == NONE) continue;                    // already materialized
    if (emitted[gi]) continue;
    emitted[gi] = 1;
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
