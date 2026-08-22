// Unit test for the shared chunked-session recorder (ir/session/chunk_recorder.h)
// and planner (ir/session/flush_plan.h) — the substrate AG2PCSession / AGMPCSession
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
// C++20.

#include "emp-tool/ir/session/chunk_recorder.h"
#include "emp-tool/ir/session/flush_plan.h"
#include "emp-tool/circuits/float.h"
#include <cstdio>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

using namespace emp;

static int fails = 0;
static void check(bool ok, const char* what) {
  if (!ok) { std::printf("  BAD: %s\n", what); ++fails; }
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
