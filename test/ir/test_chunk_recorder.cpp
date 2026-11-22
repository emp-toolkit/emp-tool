// Unit test for the shared chunked-session recorder (ir/session/chunk_recorder.h)
// and planner (ir/session/flush_plan.h) — the substrate the chunked sessions
// build on. No crypto, no protocol. Covers:
//   * RAII/scope-tracked refcounting: copy pins, destroy unpins, move steals,
//     self-assignment is safe, and refcounts survive std::vector reallocation
//     (the reason ChunkWire move-steals); in_scope follows C++ object lifetime.
//   * live_pending_ids as the implicit flush keep-set, and const-bit dedup +
//     reset on drop_chunk.
//   * context-bound Float replay releases thread-local workspace wire values
//     before recorder teardown.
//   * plan_flush: DCE (incl. a dead gate's private operand), RecordCtx-canonical
//     compaction, const-only programs, duplicate-keep dedup, stale detection,
//     and rejection of malformed recorder streams.
//   * is_pending is membership in the current chunk's outputs, const wires
//     included, and empties on drop_chunk.
//   * plan_flush agrees field by field with an independent reference planner (a
//     DFS over ordered maps) on recorded AES / AES-CTR chunks, random recordings
//     and hand-built streams. Those cover both of its gate indexes (a recorded
//     chunk's narrow output span, and the sorted table a sparse stream takes),
//     and its rejection of duplicate outputs, forward references and stale keep
//     ids on each.
// C++20.

#include "emp-tool/ir/session/chunk_recorder.h"
#include "emp-tool/ir/session/flush_plan.h"
#include "emp-tool/circuits/crypto/aes128.h"
#include "emp-tool/circuits/float.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <set>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace emp;
using circuit::Gate;
using circuit::Op;

static int fails = 0;
static void check(bool ok, const char* what) {
  if (!ok) { std::printf("  BAD: %s\n", what); ++fails; }
}

// ---- reference planner: the same contract as plan_flush, as a plain DFS over
// ordered maps (output id -> gate, recorder id -> compact id) ----
struct RefPlan {
  bool ok = false;
  circuit::BooleanProgram prog;
  std::vector<uint32_t> input_ids, output_ids;
};

static std::vector<uint32_t> operands_of(const Gate& g) {
  std::vector<uint32_t> v;
  if (!g.is_const()) {
    v.push_back(g.in0);
    if (!g.is_not()) v.push_back(g.in1);
  }
  return v;
}

template <class IsMat>
static RefPlan reference_plan(const std::vector<Gate>& chunk,
                              const std::vector<uint32_t>& keep, IsMat&& is_mat) {
  RefPlan r;
  std::map<uint32_t, std::size_t> producer;   // output id -> gate index
  for (std::size_t gi = 0; gi < chunk.size(); ++gi) {
    const Gate& g = chunk[gi];
    bool canonical = false;
    if (g.op == Op::And || g.op == Op::Xor) canonical = g.in0 != 0 && g.in1 != 0;
    else if (g.op == Op::Not) canonical = g.in0 != 0 && g.in1 == 0;
    else if (g.op == Op::Const0 || g.op == Op::Const1) canonical = g.in0 == 0 && g.in1 == 0;
    if (!canonical || g.out == 0 || g.in0 == g.out || g.in1 == g.out) return r;
    if (!producer.emplace(g.out, gi).second) return r;   // two gates, one output
  }
  for (uint32_t id : keep)
    if (id == 0 || (!producer.count(id) && !is_mat(id))) return r;

  std::vector<char> needed(chunk.size(), 0);
  std::vector<uint32_t> stack(keep.begin(), keep.end());
  while (!stack.empty()) {
    const uint32_t w = stack.back();
    stack.pop_back();
    auto it = producer.find(w);
    if (it == producer.end() || needed[it->second]) continue;
    needed[it->second] = 1;
    for (uint32_t v : operands_of(chunk[it->second])) stack.push_back(v);
  }

  std::map<uint32_t, uint32_t> compact;   // recorder id -> compact id
  for (std::size_t gi = 0; gi < chunk.size(); ++gi) {
    if (!needed[gi]) continue;
    for (uint32_t v : operands_of(chunk[gi])) {
      if (producer.count(v) || compact.count(v)) continue;
      if (!is_mat(v)) return r;
      compact[v] = (uint32_t)r.input_ids.size();
      r.input_ids.push_back(v);
    }
  }
  r.prog.num_inputs = (uint32_t)r.input_ids.size();
  uint32_t next = r.prog.num_inputs;
  for (std::size_t gi = 0; gi < chunk.size(); ++gi) {
    if (!needed[gi]) continue;
    const Gate& g = chunk[gi];
    Gate c{0, 0, next, g.op};
    const std::vector<uint32_t> ops = operands_of(g);
    for (std::size_t k = 0; k < ops.size(); ++k) {
      auto it = compact.find(ops[k]);
      if (it == compact.end()) return r;   // read before the gate producing it
      (k == 0 ? c.in0 : c.in1) = it->second;
    }
    compact[g.out] = next++;
    r.prog.gates.push_back(c);
  }
  r.prog.num_wires = next;
  std::set<uint32_t> emitted;
  for (uint32_t id : keep) {
    if (!producer.count(id) || !emitted.insert(id).second) continue;
    r.prog.outputs.push_back(compact.at(id));
    r.output_ids.push_back(id);
  }
  r.ok = true;
  return r;
}

// Field-by-field agreement. A rejected stream must be rejected by both, with a
// reason; the reasons may be worded differently.
static bool same_plan(const session::FlushPlan& p, const RefPlan& r) {
  if (p.ok != r.ok) return false;
  if (!p.ok) return p.error != nullptr;
  const circuit::BooleanProgram& a = p.prog;
  const circuit::BooleanProgram& b = r.prog;
  if (p.input_ids != r.input_ids || p.output_ids != r.output_ids ||
      a.num_inputs != b.num_inputs || a.num_wires != b.num_wires ||
      a.wire_reuse != b.wire_reuse || a.outputs != b.outputs ||
      a.gates.size() != b.gates.size())
    return false;
  for (std::size_t i = 0; i < a.gates.size(); ++i) {
    const Gate& x = a.gates[i];
    const Gate& y = b.gates[i];
    if (x.in0 != y.in0 || x.in1 != y.in1 || x.out != y.out || x.op != y.op) return false;
  }
  return true;
}

template <class IsMat>
static bool planner_agrees(const std::vector<Gate>& chunk,
                           const std::vector<uint32_t>& keep, IsMat&& is_mat) {
  return same_plan(session::plan_flush(chunk, keep, is_mat),
                   reference_plan(chunk, keep, is_mat));
}

static bool rejected_as(const session::FlushPlan& p, const char* why) {
  return !p.ok && p.error != nullptr && std::strcmp(p.error, why) == 0;
}

// plan_flush indexes a chunk's output span directly when it holds fewer than
// 4 * gates + 4096 ids, and takes the sorted table otherwise. The sweeps below use
// this to show that both indexes were exercised.
static bool takes_direct_index(const std::vector<Gate>& chunk) {
  if (chunk.empty()) return false;
  uint32_t lo = UINT32_MAX, hi = 0;
  for (const Gate& g : chunk) {
    lo = std::min(lo, g.out);
    hi = std::max(hi, g.out);
  }
  return (uint64_t)(hi - lo) < 4 * (uint64_t)chunk.size() + 4096;
}

template <class V>
static std::vector<uint32_t> ids_of(const V& v) {
  std::vector<uint32_t> ids;
  for (const auto& w : v.w) ids.push_back((uint32_t)w);
  return ids;
}

// ---- is_pending: membership in the current chunk's outputs ----
static void sweep_is_pending() {
  using W = ChunkRecorderCtx::Wire;
  ChunkRecorderCtx ctx;   // first, so every wire below dies before it
  // Every allocated id, and a few past them, against the chunk's gate outputs.
  auto agrees = [&](const char* what) {
    std::set<uint32_t> outs;
    for (const Gate& g : ctx.chunk_gates()) outs.insert(g.out);
    bool ok = true;
    for (uint32_t id = 0; id < (uint32_t)ctx.rc_.size() + 4; ++id)
      ok = ok && ctx.is_pending(id) == (outs.count(id) != 0);
    check(ok, what);
  };

  std::vector<uint32_t> in = ctx.reserve_ids(4);
  W a(in[0]), b(in[1]);
  agrees("is_pending: nothing is pending in an empty chunk");
  W c0 = ctx.public_bit(false);
  W x = ctx.and_gate(a, b);
  std::vector<uint32_t> hole = ctx.reserve_ids(3);   // reserved between gates
  W c1 = ctx.public_bit(true);
  W y = ctx.xor_gate(x, c1);
  W c1_again = ctx.public_bit(true);                 // deduped: no second gate
  W z = ctx.not_gate(y);
  check(ctx.is_pending(c0) && ctx.is_pending(c1) && (uint32_t)c1_again == (uint32_t)c1,
        "is_pending: const wires are pending and deduped within a chunk");
  check(ctx.is_pending(x) && ctx.is_pending(y) && ctx.is_pending(z),
        "is_pending: gate outputs are pending");
  check(!ctx.is_pending(in[0]) && !ctx.is_pending(hole[0]) && !ctx.is_pending(hole[2]),
        "is_pending: reserved ids are not pending, before or inside the chunk");
  agrees("is_pending: a chunk with const wires and a reservation between gates");

  const uint32_t old_x = x, old_c1 = c1;
  ctx.drop_chunk();
  agrees("is_pending: nothing is pending after drop_chunk");
  check(!ctx.is_pending(old_x) && !ctx.is_pending(old_c1),
        "is_pending: a dropped chunk's gate and const outputs are not pending");

  // The next chunk: a fresh const wire, and a gate over the dropped chunk's wire.
  W c1_next = ctx.public_bit(true);
  W w = ctx.and_gate(z, c1_next);
  check((uint32_t)c1_next != old_c1 && ctx.is_pending(c1_next) && ctx.is_pending(w),
        "is_pending: the next chunk's const wire is a fresh pending id");
  agrees("is_pending: the next chunk");

  // A long chunk with reservations between gates.
  std::mt19937 rng(11);
  std::vector<W> held = {a, b, z, w};
  for (int i = 0; i < 3000; ++i) {
    const unsigned r = rng() % 8;
    if (r == 0) {
      (void)ctx.reserve_ids(1 + rng() % 5);
      continue;
    }
    W p = held[rng() % held.size()];
    W q = held[rng() % held.size()];
    if (r < 4) held.push_back(ctx.and_gate(p, q));
    else if (r < 6) held.push_back(ctx.xor_gate(p, q));
    else if (r == 6) held.push_back(ctx.not_gate(p));
    else held.push_back(ctx.public_bit(rng() % 2 != 0));
  }
  agrees("is_pending: a long chunk with reservations between gates");
  ctx.drop_chunk();
  agrees("is_pending: nothing is pending after dropping the long chunk");
}

// ---- plan_flush rejections, on both gate indexes ----
static void sweep_rejections() {
  const char* const dup = "recorder id is produced by more than one gate";
  const char* const fwd = "gate reads a later gate's output";
  const char* const stale = "keep set contains a stale recorder id";
  auto mat2 = [](uint32_t id) { return id == 1 || id == 2; };
  const uint32_t B = 3000000000u;   // with ids near 10: a span for the sorted table
  auto rejects = [&](const std::vector<Gate>& chunk, const std::vector<uint32_t>& keep,
                     const char* why, const char* what) {
    check(rejected_as(session::plan_flush(chunk, keep, mat2), why) &&
              planner_agrees(chunk, keep, mat2),
          what);
  };

  // (b) Two gates writing one id, adjacent or apart in emission order, needed or not.
  rejects({{1, 2, 3, Op::And}, {1, 2, 4, Op::Xor}, {1, 2, 3, Op::Xor}}, {4}, dup,
          "duplicate output rejected (direct index)");
  rejects({{1, 2, 3, Op::And}, {1, 2, 3, Op::Xor}}, {3}, dup,
          "adjacent duplicate output rejected (direct index)");
  rejects({{1, 2, B, Op::And}, {1, 2, 10, Op::Xor}, {1, 2, B, Op::Xor}}, {10}, dup,
          "duplicate output rejected (sorted table)");
  rejects({{1, 2, 10, Op::And}, {1, 2, B, Op::Xor}, {2, 1, B, Op::And}}, {B}, dup,
          "adjacent duplicate output rejected (sorted table)");

  // (c) A needed gate that reads a later gate's output: directly, through a
  // two-gate cycle, or through a gate that is also needed on its own.
  rejects({{4, 1, 3, Op::And}, {1, 2, 4, Op::Xor}}, {3}, fwd,
          "forward reference rejected (direct index)");
  rejects({{4, 0, 3, Op::Not}, {3, 0, 4, Op::Not}}, {3}, fwd,
          "two-gate cycle rejected (direct index)");
  rejects({{1, 4, 3, Op::And}, {1, 2, 4, Op::Xor}}, {4, 3}, fwd,
          "forward reference to a needed gate rejected (direct index)");
  rejects({{B + 7, 1, B, Op::And}, {1, 2, B + 7, Op::Xor}, {1, 2, 10, Op::And}}, {B}, fwd,
          "forward reference rejected (sorted table)");
  rejects({{1, 2, 10, Op::And}, {B + 5, 0, B, Op::Not}, {B, 0, B + 5, Op::Not}}, {B}, fwd,
          "two-gate cycle rejected (sorted table)");
  // A dead gate's forward reference is not read, so the plan stands.
  for (const std::vector<Gate>& dead : {
           std::vector<Gate>{{1, 2, 3, Op::And}, {5, 1, 4, Op::Xor}, {1, 2, 5, Op::Xor}},
           std::vector<Gate>{{1, 2, 3, Op::And}, {B, 1, 4, Op::Xor}, {1, 2, B, Op::Xor}}}) {
    session::FlushPlan p = session::plan_flush(dead, {3}, mat2);
    check(p.ok && p.prog.gates.size() == 1 && planner_agrees(dead, {3}, mat2),
          "a dead gate's forward reference is ignored");
  }

  // (d) A keep id that no chunk gate outputs and that is not materialized: outside
  // the output span, in a hole inside it, or in a sparse span.
  const std::vector<Gate> span = {{1, 2, 10, Op::And}, {10, 2, 12, Op::Xor}};   // 11: hole
  rejects(span, {12, 99}, stale, "stale keep id outside the span rejected");
  rejects(span, {11, 12}, stale, "stale keep id in a span hole rejected");
  rejects({{1, 2, 10, Op::And}, {10, 2, B, Op::Xor}}, {B, 11}, stale,
          "stale keep id rejected (sorted table)");
  rejects({{1, 2, 10, Op::And}, {10, 2, B, Op::Xor}}, {B, B + 1}, stale,
          "stale keep id next to a sparse output rejected (sorted table)");
  // A materialized id in a span hole is a carried keep id: kept, not an output.
  auto mat_hole = [](uint32_t id) { return id == 1 || id == 2 || id == 11; };
  session::FlushPlan carried = session::plan_flush(span, {11, 12}, mat_hole);
  check(carried.ok && carried.output_ids == std::vector<uint32_t>{12} &&
            carried.prog.outputs.size() == 1 && planner_agrees(span, {11, 12}, mat_hole),
        "a materialized id in a span hole is carried, not planned");
}

// ---- plan_flush against the reference on recorded chunks of real circuits ----
// AES-128 and AES-128-CTR through the circuits library, recorded the way a direct
// session records them: inputs reserved and materialized (some between gates),
// the gates pending, and a later chunk reading wires an earlier flush kept.
static void sweep_recorded_circuits() {
  using W = ChunkRecorderCtx::Wire;
  using Block = BitVec_T<ChunkRecorderCtx, 128>;
  ChunkRecorderCtx ctx;
  std::unordered_set<uint32_t> mat;
  auto is_mat = [&](uint32_t id) { return mat.count(id) != 0; };
  auto input = [&]<int N>(std::integral_constant<int, N>) {
    std::vector<uint32_t> ids = ctx.reserve_ids(N);
    mat.insert(ids.begin(), ids.end());
    std::vector<W> wires(ids.begin(), ids.end());
    return BitVec_T<ChunkRecorderCtx, N>::from_wires(ctx, wires.data());
  };
  using B128 = std::integral_constant<int, 128>;

  // Chunk 1: a key schedule and two blocks folded into an accumulator; each
  // plaintext is reserved after the gates before it.
  Block key = input(B128{});
  Block acc = input(B128{});
  BitVec_T<ChunkRecorderCtx, 1408> ek = circuit::crypto::aes128_key_schedule(ctx, key);
  for (int b = 0; b < 2; ++b)
    acc = acc ^ circuit::crypto::aes128_encrypt_block(ctx, input(B128{}), ek);
  {
    const std::vector<Gate>& chunk = ctx.chunk_gates();
    check(takes_direct_index(chunk), "a recorded AES chunk takes the direct index");
    std::vector<uint32_t> every;
    for (const Gate& g : chunk) every.push_back(g.out);
    std::vector<uint32_t> mixed;   // reversed, strided, duplicated, plus carried ids
    for (std::size_t i = every.size(); i > 0; i -= std::min<std::size_t>(i, 7))
      mixed.push_back(every[i - 1]);
    mixed.push_back(every[every.size() / 2]);
    mixed.push_back(every[every.size() / 2]);
    for (uint32_t id : ids_of(key)) if (id % 3 == 0) mixed.push_back(id);
    check(planner_agrees(chunk, ctx.live_pending_ids(), is_mat),
          "AES chunk, keep = live pending wires");
    check(planner_agrees(chunk, {ids_of(acc)[5]}, is_mat),
          "AES chunk, keep = one accumulator bit (most gates dead)");
    check(planner_agrees(chunk, mixed, is_mat), "AES chunk, mixed keep set");
    check(planner_agrees(chunk, every, is_mat), "AES chunk, keep every gate");
    check(planner_agrees(chunk, {}, is_mat), "AES chunk, empty keep set");
    // The live keep set is the accumulator and the pending part of the key
    // schedule (its first round key is the materialized key itself).
    std::set<uint32_t> live;
    for (const std::vector<uint32_t>& v : {ids_of(acc), ids_of(ek)})
      for (uint32_t id : v)
        if (ctx.is_pending(id)) live.insert(id);
    session::FlushPlan p = session::plan_flush(chunk, ctx.live_pending_ids(), is_mat);
    check(p.ok && live.size() == 128 + 1408 - 128 && p.output_ids.size() == live.size(),
          "AES chunk keeps the accumulator and the pending key schedule");
    mat.insert(p.output_ids.begin(), p.output_ids.end());   // as the session materializes them
  }
  ctx.drop_chunk();

  // Chunk 2: AES-CTR over 2.5 blocks (its counter increments record const
  // wires), and a block encrypted under the key schedule chunk 1 kept.
  Block iv = input(B128{});
  BitVec_T<ChunkRecorderCtx, 320> data = input(std::integral_constant<int, 320>{});
  BitVec_T<ChunkRecorderCtx, 320> ct = circuit::crypto::aes128_ctr(ctx, key, iv, data, 7);
  Block ct2 = circuit::crypto::aes128_encrypt_block(ctx, acc, ek);
  {
    const std::vector<Gate>& chunk = ctx.chunk_gates();
    bool consts = false;
    for (const Gate& g : chunk) consts = consts || g.is_const();
    check(consts, "the AES-CTR chunk records const wires");
    check(planner_agrees(chunk, ctx.live_pending_ids(), is_mat),
          "AES-CTR chunk, keep = live pending wires");
    check(planner_agrees(chunk, ids_of(ct2), is_mat),
          "AES-CTR chunk, keep = the block over carried wires");
    std::vector<uint32_t> tail = ids_of(ct);
    tail.erase(tail.begin(), tail.begin() + 200);
    check(planner_agrees(chunk, tail, is_mat), "AES-CTR chunk, keep = the last 120 output bits");
  }
}

// ---- plan_flush against the reference on random recordings ----
// Every op over earlier wires, const wires, ids reserved between gates (holes in
// the output span, some materialized, some never), wires carried from earlier
// chunks, and occasionally a stale operand or keep id. Every fourth chunk
// reserves a block of ids between two gates wide enough for the sorted table.
static void sweep_random_recordings() {
  using W = ChunkRecorderCtx::Wire;
  ChunkRecorderCtx ctx;
  std::mt19937 rng(2026);
  std::unordered_set<uint32_t> mat;
  auto is_mat = [&](uint32_t id) { return mat.count(id) != 0; };
  std::vector<uint32_t> carried, stale;   // ids from earlier chunks and reservations
  int direct = 0, sorted = 0, rejected = 0;
  for (int trial = 0; trial < 400; ++trial) {
    std::vector<W> held;   // operand candidates, alive for the whole chunk
    for (uint32_t id : ctx.reserve_ids(1 + rng() % 6)) {
      mat.insert(id);
      held.emplace_back(id);
    }
    for (int k = 0; k < 4 && !carried.empty(); ++k)
      held.emplace_back(carried[rng() % carried.size()]);
    const bool wide = trial % 4 == 3;
    const int n = 2 + (int)(rng() % 160);
    // In one chunk of four, one gate reads a stale operand.
    const int poison_at = (rng() % 4 == 0 && !stale.empty()) ? (int)(rng() % (unsigned)n) : -1;
    for (int i = 0; i < n; ++i) {
      if (wide && i == n / 2) {   // a gate on each side of a wide block of ids
        held.push_back(ctx.xor_gate(held.back(), held.front()));
        std::vector<uint32_t> ids = ctx.reserve_ids(8 * (int64_t)n + 8192);
        mat.insert(ids.front());
        held.emplace_back(ids.front());
        stale.push_back(ids.back());
        held.push_back(ctx.and_gate(held.back(), held.front()));
      }
      auto pick = [&]() -> W {
        if (i == poison_at) return W(stale[rng() % stale.size()]);
        return held[rng() % held.size()];
      };
      const unsigned r = rng() % 16;
      if (r == 0) {
        for (uint32_t id : ctx.reserve_ids(1 + rng() % 4)) {
          if (rng() % 2) {
            mat.insert(id);
            held.emplace_back(id);
          } else {
            stale.push_back(id);
          }
        }
        continue;
      }
      W p = pick();
      W q = pick();
      if (r < 6) held.push_back(ctx.and_gate(p, q));
      else if (r < 11) held.push_back(ctx.xor_gate(p, q));
      else if (r < 14) held.push_back(ctx.not_gate(p));
      else held.push_back(ctx.public_bit(r == 15));
    }

    std::vector<uint32_t> keep;
    if (rng() % 3 == 0) {   // the session's implicit keep set: what is still in scope
      for (W& w : held)
        if (rng() % 2) w = W();
      keep = ctx.live_pending_ids();
    } else {
      for (int k = (int)(rng() % 8); k > 0; --k) keep.push_back(held[rng() % held.size()]);
      if (!keep.empty() && rng() % 2) keep.push_back(keep.front());
      if (!stale.empty() && rng() % 16 == 0) keep.push_back(stale[rng() % stale.size()]);
    }
    const std::vector<Gate>& chunk = ctx.chunk_gates();
    session::FlushPlan plan = session::plan_flush(chunk, keep, is_mat);
    check(same_plan(plan, reference_plan(chunk, keep, is_mat)),
          "random recording: plan_flush == reference");
    if (!plan.ok) {
      ++rejected;
    } else {
      if (takes_direct_index(chunk)) ++direct; else ++sorted;
      mat.insert(plan.output_ids.begin(), plan.output_ids.end());
      carried.insert(carried.end(), plan.output_ids.begin(), plan.output_ids.end());
    }
    for (const Gate& g : chunk)
      if (!is_mat(g.out)) stale.push_back(g.out);   // dropped with the chunk
    held.clear();
    ctx.drop_chunk();
  }
  check(direct > 150 && sorted > 50 && rejected > 10,
        "random recordings plan on both indexes and include rejections");
}

// ---- plan_flush against the reference on hand-built streams ----
// Output ids need not come from a recorder: a shuffled narrow range (the direct
// index, also at the very top of the uint32 range) or ids spread over the whole
// range (the sorted table; UINT32_MAX itself appears as a wire). Some streams are
// broken: a forward reference, a duplicate output, a stale operand or keep id.
static void sweep_hand_built_streams() {
  std::mt19937 rng(7);
  int direct = 0, sorted = 0, rejected = 0;
  for (int trial = 0; trial < 900; ++trial) {
    const int shape = trial % 3;   // 0: narrow, 1: narrow at the top, 2: spread
    const int n_in = 1 + (int)(rng() % 6);
    const int n = (int)(rng() % 80);
    const uint32_t width = (uint32_t)(2 * (n_in + n) + 8);
    std::set<uint32_t> seen;
    std::vector<uint32_t> ids;
    if (shape == 2 && trial % 4 == 0) ids.push_back(UINT32_MAX);
    auto fresh = [&] {
      for (;;) {
        uint32_t id = shape == 0 ? 1000 + rng() % width
                    : shape == 1 ? UINT32_MAX - rng() % width
                                 : 1 + rng() % (UINT32_MAX - 1);
        if (!seen.count(id)) return id;
      }
    };
    seen.insert(ids.begin(), ids.end());
    while ((int)ids.size() < n_in + n) {
      ids.push_back(fresh());
      seen.insert(ids.back());
    }
    std::shuffle(ids.begin(), ids.end(), rng);
    const std::vector<uint32_t> inputs(ids.begin(), ids.begin() + n_in);
    std::vector<Gate> chunk;
    std::vector<uint32_t> defined = inputs;
    for (int i = 0; i < n; ++i) {
      Gate g;
      g.out = ids[(std::size_t)(n_in + i)];
      g.op = static_cast<Op>(rng() % 5);
      if (!g.is_const()) g.in0 = defined[rng() % defined.size()];
      if (g.op == Op::And || g.op == Op::Xor) g.in1 = defined[rng() % defined.size()];
      chunk.push_back(g);
      defined.push_back(g.out);
    }
    std::vector<uint32_t> keep;
    for (int k = (int)(rng() % 6); k > 0; --k) keep.push_back(defined[rng() % defined.size()]);
    if (!keep.empty() && rng() % 2) keep.push_back(keep.front());

    const unsigned defect = rng() % 8;
    if (defect == 0 && n >= 2) {          // an earlier gate reads a later output
      const std::size_t i = rng() % (std::size_t)(n - 1);
      const std::size_t j = i + 1 + rng() % (std::size_t)(n - 1 - (int)i);
      if (!chunk[i].is_const()) chunk[i].in0 = chunk[j].out;
      if (rng() % 2) keep.push_back(chunk[i].out);
    } else if (defect == 1 && n >= 2) {   // two gates write one id
      const std::size_t i = rng() % (std::size_t)(n - 1);
      const std::size_t j = i + 1 + rng() % (std::size_t)(n - 1 - (int)i);
      chunk[j].out = chunk[i].out;
    } else if (defect == 2 && n >= 1) {   // an operand nobody defines
      Gate& g = chunk[rng() % (std::size_t)n];
      if (!g.is_const()) g.in0 = fresh();
      if (rng() % 2) keep.push_back(g.out);
    } else if (defect == 3) {             // a keep id nobody defines
      keep.push_back(fresh());
    }
    auto is_mat = [&](uint32_t id) {
      return std::find(inputs.begin(), inputs.end(), id) != inputs.end();
    };
    session::FlushPlan plan = session::plan_flush(chunk, keep, is_mat);
    check(same_plan(plan, reference_plan(chunk, keep, is_mat)),
          "hand-built stream: plan_flush == reference");
    if (!plan.ok) ++rejected;
    else if (!chunk.empty() && takes_direct_index(chunk)) ++direct;
    else if (!chunk.empty()) ++sorted;
  }
  check(direct > 250 && sorted > 120 && rejected > 100,
        "hand-built streams plan on both indexes and include rejections");
}

template <class F>
static bool dies(F&& f) {
  pid_t pid = fork();
  if (pid < 0) return false;
  if (pid == 0) {
    (void)std::freopen("/dev/null", "w", stderr);
    f();
    _exit(0);
  }
  int status = 0;
  return waitpid(pid, &status, 0) == pid &&
         (!WIFEXITED(status) || WEXITSTATUS(status) != 0);
}

int main() {
  static_assert(BooleanContext<ChunkRecorderCtx>);
  using W = ChunkRecorderCtx::Wire;

  // The teardown guard is always-on, including Release builds where
  // EMP_CONTEXT_CHECKS defaults off. Continuing here would let the surviving
  // wire unpin against a later recorder with the same numeric id.
  check(dies([] {
    W surviving;
    {
      ChunkRecorderCtx recorder;
      std::vector<uint32_t> ids = recorder.reserve_ids(1);
      surviving = W(ids[0]);
    }
  }), "recorder teardown rejects a surviving wire");

  // Float replay uses a thread-local ProgramWorkspace. Its retained wire values
  // must be released before this recorder dies; otherwise teardown sees pinned
  // scratch wires and a later recorder would inherit stale ids.
  {
    ChunkRecorderCtx float_ctx;
    using F16 = Float_T<ChunkRecorderCtx, 16>;
    F16 a = F16::constant(float_ctx, 1.0f);
    F16 b = F16::constant(float_ctx, 2.0f);
    F16 sum = a + b;
    (void)sum;
  }

  // Each sweep owns the one active recorder while it runs.
  sweep_is_pending();
  sweep_rejections();
  sweep_recorded_circuits();
  sweep_random_recordings();
  sweep_hand_built_streams();

  ChunkRecorderCtx ctx;
  // Two materialized-style "input" wires, held by live value wrappers.
  std::vector<uint32_t> in = ctx.reserve_ids(2);
  W a(in[0]), b(in[1]);
  check(ctx.in_scope(in[0]) && ctx.in_scope(in[1]), "inputs in scope while held");

  // A pending AND gate; its result wire is held -> pending and in scope.
  W x = ctx.and_gate(a, b);
  uint32_t xid = x;
  check(ctx.is_pending(xid), "and-gate output is pending");
  check(ctx.in_scope(xid), "held gate output is in scope");
  check(ctx.rc_[xid] == 1, "one handle -> refcount 1");
  {
    auto lp = ctx.live_pending_ids();
    check(lp.size() == 1 && lp[0] == xid, "live_pending = {x} with one held gate");
  }

  // A second pending gate inside a scope; it leaves live_pending when destroyed.
  {
    W y = ctx.xor_gate(a, b);
    (void)y;
    check(ctx.live_pending_ids().size() == 2, "two pending wires while both held");
  }
  check(ctx.live_pending_ids().size() == 1, "out-of-scope pending wire dropped from live set");
  check(ctx.in_scope(xid), "x still in scope after y left");

  // Copy semantics: a copy pins, so the value stays in scope across one death.
  {
    W x2 = x;            // pin
    check(ctx.rc_[xid] == 2, "copy bumps refcount to 2");
    { W x3 = x2; (void)x3; }   // pin then unpin
    check(ctx.rc_[xid] == 2, "nested copy pin/unpin nets to 2");
    check(ctx.in_scope(xid), "x in scope while a copy lives");
  }
  check(ctx.rc_[xid] == 1 && ctx.in_scope(xid), "back to one handle after copies die");

  // Move semantics: a move steals the id (no table touch); source becomes null.
  {
    W src = x;                       // rc 2
    W dst = std::move(src);          // steal: rc unchanged, src -> id 0
    check(ctx.rc_[xid] == 2, "move does not change the refcount");
    check((uint32_t)src == 0, "moved-from wire is the null sentinel");
    check((uint32_t)dst == xid, "moved-to wire holds the id");
  }
  check(ctx.rc_[xid] == 1, "moved temporaries unwound to one handle");

  // Self-assignment must be a no-op, not a double-unpin/corruption.
  { W s = x; s = s; check(ctx.rc_[xid] == 2 && (uint32_t)s == xid, "self copy-assign is a no-op"); }
  { W s = x; s = std::move(s); check((uint32_t)s == xid, "self move-assign is a no-op"); }
  check(ctx.rc_[xid] == 1, "self-assignment left the refcount intact");

  // std::vector reallocation move-constructs its elements; the move-steal must keep
  // the refcount exact (this is the hazard the move ctor exists for).
  {
    std::vector<W> v;
    for (int i = 0; i < 100; ++i) v.push_back(x);   // forces several reallocations
    check(ctx.rc_[xid] == 1 + 100, "100 copies through vector realloc -> exact refcount");
  }
  check(ctx.rc_[xid] == 1, "vector destroyed -> refcount back to 1");

  // Const-bit dedup: public_bit(v) caches one wire per value per chunk.
  uint32_t cid;
  {
    W c1 = ctx.public_bit(true);
    W c2 = ctx.public_bit(true);
    cid = c1;
    check((uint32_t)c1 == (uint32_t)c2, "public_bit(true) dedups to one wire in a chunk");
    check(ctx.is_pending(cid) && ctx.rc_[cid] == 2, "two handles to the deduped const -> rc 2");
  }
  check(ctx.rc_[cid] == 0, "const wire unpinned once its handles die");

  // ---- plan_flush over the recorded chunk (recorder integration) ----
  // From x: the xor gate (out of scope, unreferenced) is DCE'd; the and gate
  // compacts to a 2-input / 1-gate / 1-output canonical program.
  auto is_mat = [&](uint32_t id) { return id == in[0] || id == in[1]; };
  session::FlushPlan plan = session::plan_flush(ctx.chunk_gates(), {xid}, is_mat);
  check(plan.ok, "plan ok");
  check(plan.input_ids.size() == 2, "two carried inputs");
  check(plan.prog.gates.size() == 1 && plan.prog.num_inputs == 2, "one gate after DCE");
  check(plan.prog.gates[0].out == 2, "canonical out id == num_inputs + 0");
  check(plan.output_ids.size() == 1 && plan.output_ids[0] == xid, "single output = x");
  check(plan.prog.gates[0].op == circuit::Op::And, "the kept gate is the AND");

  // Stale detection: a needed operand that is neither chunk-local nor materialized.
  auto none_mat = [](uint32_t) { return false; };
  check(!session::plan_flush(ctx.chunk_gates(), {xid}, none_mat).ok,
        "plan not ok when operands are unmaterialized");

  // ---- plan_flush pure cases (hand-built gate streams) ----
  {
    using circuit::Gate; using circuit::Op;
    // DCE: gate 4 is dead; it and its private operand (2) are dropped, keeping 3.
    std::vector<Gate> chunk = {{1, 2, 4, Op::And}, {1, 3, 5, Op::And}};
    auto p = session::plan_flush(chunk, {4}, [](uint32_t id) { return id >= 1 && id <= 3; });
    check(p.ok && p.prog.gates.size() == 1 && p.output_ids.size() == 1 &&
          p.output_ids[0] == 4 && p.input_ids.size() == 2,
          "DCE drops the dead gate and its private operand");

    // Const-only program: a kept Const1 with no inputs.
    std::vector<Gate> conly = {{0, 0, 5, Op::Const1}};
    auto pc = session::plan_flush(conly, {5}, [](uint32_t) { return false; });
    check(pc.ok && pc.prog.num_inputs == 0 && pc.prog.gates.size() == 1 &&
          pc.prog.outputs.size() == 1 && pc.output_ids.size() == 1,
          "const-only program: no inputs, one gate, one output");

    // Duplicate keep ids collapse to a single output.
    std::vector<Gate> one = {{1, 2, 3, Op::And}};
    auto pd = session::plan_flush(one, {3, 3}, [](uint32_t id) { return id == 1 || id == 2; });
    check(pd.ok && pd.output_ids.size() == 1, "duplicate keep ids dedup to one output");

    std::vector<Gate> duplicate = {
        {1, 2, 3, Op::And},
        {1, 2, 3, Op::Xor},
    };
    auto pdup = session::plan_flush(duplicate, {3}, [](uint32_t id) { return id <= 2; });
    check(!pdup.ok && pdup.error != nullptr,
          "duplicate recorder definitions are rejected");

    std::vector<Gate> forward = {
        {4, 1, 3, Op::And},
        {1, 2, 4, Op::Xor},
    };
    auto pfwd = session::plan_flush(forward, {3}, [](uint32_t id) { return id <= 2; });
    check(!pfwd.ok && pfwd.error != nullptr,
          "forward chunk-local references are rejected");

    // A gate that reads its own output, through either operand or a Not.
    for (const Gate& self : {Gate{3, 1, 3, Op::And}, Gate{1, 3, 3, Op::Xor},
                             Gate{3, 0, 3, Op::Not}}) {
      std::vector<Gate> loop = {self};
      auto ps = session::plan_flush(loop, {3}, [](uint32_t id) { return id <= 2; });
      check(!ps.ok && ps.error != nullptr,
            "a gate that reads its own output is rejected");
    }

    std::vector<Gate> null_operand = {{0, 1, 2, Op::And}};
    auto pnull = session::plan_flush(null_operand, {2}, [](uint32_t id) { return id == 1; });
    check(!pnull.ok && pnull.error != nullptr,
          "null recorder operands are rejected");
  }

  // drop_chunk clears the chunk and resets the const dedup.
  ctx.drop_chunk();
  check(!ctx.is_pending(xid), "chunk dropped -> nothing pending");
  check(ctx.live_pending_ids().empty(), "no live pending after drop");
  W c_after = ctx.public_bit(true);
  check((uint32_t)c_after != cid, "const dedup is reset after drop_chunk (fresh id)");

  std::printf("test_chunk_recorder: %s\n", fails == 0 ? "GOOD!" : "BAD!");
  return fails == 0 ? 0 : 1;
}
