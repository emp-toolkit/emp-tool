#ifndef EMP_IR_SESSION_CHUNKED_SESSION_H__
#define EMP_IR_SESSION_CHUNKED_SESSION_H__

// ChunkedSession<Derived> — the shared DIRECT/chunked session flow for crypto
// sessions over ChunkRecorderCtx (AG2PCSession, AGMPCSession, ...). It owns the
// gate recorder, the typed input/reveal/checkpoint/run surface, RAII scope-based
// liveness, and the flush machinery — everything that is NOT protocol-specific.
//
// It is deliberately PURELY ID-BASED: it never names a SecureWires bundle or a
// per-wire CarriedState. All protocol/crypto state lives in Derived, reached
// through a small set of CRTP hooks that speak only in recorder-id lists:
//
//   void authenticate_into_(owner, bits, ids)                 // protocol input  -> carried[ids]
//   void batch_authenticate_into_(owners, bits, id_lists)     // one batched input phase
//   void run_program_into_(prog, in_ids, out_ids)             // engine: gather in -> run -> carried[out]
//   std::vector<uint8_t> decode_(ids, recipient)              // protocol output
//   void validate_recipient_(recipient)                       // reject bad recipient pre-flush
//   bool is_materialized_(id)                                 // id in carried state?
//   template<class Pred> void prune_carried_(keep)            // drop carried !keep(id)
//   int party_()
//
// Derived also defines run(body, ...) (its replay strategy differs) and owns the
// protocol/engine members + CarriedState. The base provides input / input_batch /
// reveal / checkpoint / run(circuit) / run_artifact and the flush flow over them.

#include "emp-tool/ir/program.h"                       // circuit::BooleanProgram
#include "emp-tool/circuits/typed.h"                   // value types
#include "emp-tool/circuits/frontend/circuit_fn.h"     // frontend::Circuit / RecordValue
#include "emp-tool/ir/session/chunk_recorder.h"        // ChunkRecorderCtx
#include "emp-tool/ir/session/flush_plan.h"            // session::plan_flush
#include "emp-tool/runtime/core/utils.h"               // error(), PUBLIC
#include <cstdint>
#include <optional>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace emp {

template <class Derived>
class ChunkedSession {
public:
  using ctx_t = ChunkRecorderCtx;
  template <class V> using reveal_t = std::optional<typename V::clear_t>;

  // Gate context for non-I/O value construction (public constants, operators).
  ctx_t& ctx() { return ctx_; }
  // Total ANDs actually run across every flush / run (post-DCE).
  uint64_t num_and() const { return num_and_; }

  // ---- typed input (eager: authenticated immediately -> materialized) ----
  template <WireValue V>
  V input(int owner, const typename V::clear_t& clear) {
    static_assert(std::same_as<typename V::context_type, ctx_t>,
        "ChunkedSession::input<V>: V must be a value over this session's ctx_t");
    const auto eb = V::encode(clear);
    std::vector<uint8_t> bits(eb.begin(), eb.end());
    std::vector<uint32_t> ids = ctx_.reserve_ids(bits.size());
    self().authenticate_into_(owner, bits, ids);
    return from_ids_<V>(ctx_, ids);
  }

  // ---- multi-owner input batch: ONE authentication phase across owners ----
  class InputBatch {
  public:
    template <WireValue V>
    V add(int owner, const typename V::clear_t& clear) {
      expecting(!finished_, "ChunkedSession::input_batch: add() after finish()");
      static_assert(std::same_as<typename V::context_type, ctx_t>,
          "ChunkedSession::input_batch add<V>: V must be a value over this session's ctx_t");
      const auto eb = V::encode(clear);
      std::vector<uint8_t> bits(eb.begin(), eb.end());
      std::vector<uint32_t> ids = sess_->ctx_.reserve_ids(bits.size());
      owners_.push_back(owner);
      bits_.push_back(std::move(bits));
      ids_.push_back(ids);
      return from_ids_<V>(sess_->ctx_, ids);
    }
    void finish() {
      expecting(!finished_,
                "ChunkedSession::input_batch: finish() called twice");
      finished_ = true;
      sess_->self().batch_authenticate_into_(owners_, bits_, ids_);
    }
    // Reserved input ids that were never authenticated would be stale wires; a
    // batch built but dropped without finish() is a usage error, not silent.
    ~InputBatch() {
      expecting(finished_ || owners_.empty(),
                "ChunkedSession::input_batch: destroyed without finish() — "
                "inputs were reserved but never authenticated");
    }
  private:
    friend class ChunkedSession;
    explicit InputBatch(ChunkedSession* s) : sess_(s) {}
    ChunkedSession* sess_;
    std::vector<int> owners_;
    std::vector<std::vector<uint8_t>> bits_;
    std::vector<std::vector<uint32_t>> ids_;
    bool finished_ = false;
  };
  InputBatch input_batch() { return InputBatch(this); }

  // ---- reveal: settle v and decode it to recipient. RAII liveness — every
  // pending wire still in C++ scope is kept (materialized), so revealing one
  // value never invalidates another you still hold. keep... is an optional
  // override of the implicit in-scope keep-set.
  template <WireValue V, class... Keep>
  reveal_t<V> reveal(const V& v, int recipient, const Keep&... keep) {
    static_assert(std::same_as<typename V::context_type, ctx_t>,
        "ChunkedSession::reveal<V>: V must be a value over this session's ctx_t");
    self().validate_recipient_(recipient);   // reject before the networked flush
    std::vector<uint32_t> keep_ids = ctx_.live_pending_ids();
    std::vector<uint32_t> vids = value_ids_(v);
    keep_ids.insert(keep_ids.end(), vids.begin(), vids.end());
    (append_ids_(keep_ids, keep), ...);
    flush_(keep_ids);
    std::vector<uint8_t> bits = self().decode_(vids, recipient);
    constexpr int W = V::width();
    if ((int)bits.size() != W) return std::nullopt;   // non-recipient
    std::array<bool, (std::size_t)W> bb{};
    for (int i = 0; i < W; ++i) bb[(std::size_t)i] = (bool)bits[(std::size_t)i];
    return V::decode(bb.data());
  }

  // ---- checkpoint: the flush + prune barrier. checkpoint() keeps exactly what
  // is still in C++ scope and drops everything else; checkpoint(keep...) prunes
  // to the given values explicitly.
  template <class... Keep>
  void checkpoint(const Keep&... keep) {
    if constexpr (sizeof...(keep) == 0) {
      flush_(ctx_.live_pending_ids());
      self().prune_carried_([this](uint32_t id) { return ctx_.in_scope(id); });
    } else {
      std::vector<uint32_t> keep_ids;
      (append_ids_(keep_ids, keep), ...);
      flush_(keep_ids);
      std::unordered_set<uint32_t> keepset(keep_ids.begin(), keep_ids.end());
      self().prune_carried_([&keepset](uint32_t id) { return keepset.count(id) != 0; });
    }
  }

  // ---- COMPILED replay: a stored typed Circuit run standalone over materialized args.
  template <frontend::RecordValue RetV, frontend::RecordValue... ArgVs>
  typename RetV::template rebind<ctx_t>
  run(const frontend::Circuit<RetV, ArgVs...>& c,
      const typename ArgVs::template rebind<ctx_t>&... args) {
    std::vector<uint32_t> in_ids;
    (append_arg_ids_(in_ids, args), ...);
    expecting((uint32_t)in_ids.size() == c.program().num_inputs,
              "ChunkedSession::run: total argument width != circuit input count");
    return run_program_<typename RetV::template rebind<ctx_t>>(c.program(), in_ids);
  }

  // ---- typed raw-program runner over materialized args, explicit RetV.
  template <WireValue RetV, class... Args>
  RetV run_artifact(const circuit::BooleanProgram& prog, const Args&... args) {
    static_assert(std::same_as<typename RetV::context_type, ctx_t>,
        "ChunkedSession::run_artifact<RetV>: RetV must be a value over this session's ctx_t");
    std::vector<uint32_t> in_ids;
    (append_arg_ids_(in_ids, args), ...);
    expecting((uint32_t)in_ids.size() == prog.num_inputs,
              "ChunkedSession::run_artifact: total argument width != program num_inputs");
    expecting((uint32_t)RetV::width() == prog.outputs.size(),
              "ChunkedSession::run_artifact: RetV width != program output count");
    return run_program_<RetV>(prog, in_ids);
  }

protected:
  ChunkRecorderCtx ctx_;
  uint64_t num_and_ = 0;

  Derived& self() { return static_cast<Derived&>(*this); }
  const Derived& self() const { return static_cast<const Derived&>(*this); }

  // Wrap raw recorder ids in (refcounted) wires to build a value.
  template <class V>
  static V from_ids_(ctx_t& c, const std::vector<uint32_t>& ids) {
    std::vector<typename ctx_t::Wire> wb(ids.begin(), ids.end());
    return V::from_wires(c, wb.data());
  }
  template <class V>
  std::vector<uint32_t> value_ids_(const V& v) const {
    // ag2pc and agmpc both alias ChunkRecorderCtx, so the compile-time ctx_t check
    // cannot tell two live sessions apart. This is a session-boundary check, not a
    // gate-path check, so keep the single pointer comparison in every build.
    expecting(v.context() == &ctx_,
              "ChunkedSession: value is bound to a different session's context");
    std::vector<typename ctx_t::Wire> wb((std::size_t)V::width());
    v.pack_wires(wb.data());
    std::vector<uint32_t> ids(wb.size());
    for (std::size_t i = 0; i < wb.size(); ++i) ids[i] = (uint32_t)wb[i];
    return ids;
  }
  template <class V>
  void append_ids_(std::vector<uint32_t>& dst, const V& v) const {
    std::vector<uint32_t> ids = value_ids_(v);
    dst.insert(dst.end(), ids.begin(), ids.end());
  }
  // run() argument ids: must be fully materialized (loud error otherwise).
  template <class A>
  void append_arg_ids_(std::vector<uint32_t>& dst, const A& a) const {
    static_assert(std::is_same_v<typename A::context_type, ctx_t>,
        "ChunkedSession::run: argument must be a value over this session's ctx_t");
    std::vector<uint32_t> ids = value_ids_(a);
    for (uint32_t id : ids)
      expecting(self().is_materialized_(id),
                "ChunkedSession::run: argument is unflushed — checkpoint it first");
    dst.insert(dst.end(), ids.begin(), ids.end());
  }

  // Run a program over materialized inputs into fresh materialized outputs.
  template <class RetV>
  RetV run_program_(const circuit::BooleanProgram& prog, const std::vector<uint32_t>& in_ids) {
    std::vector<uint32_t> out_ids = ctx_.reserve_ids(prog.outputs.size());
    self().run_program_into_(prog, in_ids, out_ids);
    count_ands_(prog);
    return from_ids_<RetV>(ctx_, out_ids);
  }
  void count_ands_(const circuit::BooleanProgram& prog) {
    for (const auto& g : prog.gates) if (g.op == circuit::Op::And) ++num_and_;
  }

  // Run the pending chunk, materializing exactly the PENDING keep ids; clear the
  // chunk. If no keep id is pending, drop the chunk without running.
  void flush_(const std::vector<uint32_t>& keep_ids) {
    for (uint32_t id : keep_ids)
      expecting(ctx_.is_pending(id) || self().is_materialized_(id),
                "ChunkedSession: keep/reveal wire is stale");

    bool any_pending = false;
    for (uint32_t id : keep_ids) if (ctx_.is_pending(id)) { any_pending = true; break; }
    if (!any_pending) { ctx_.drop_chunk(); return; }

    session::FlushPlan plan = session::plan_flush(
        ctx_.chunk_gates(), keep_ids,
        [this](uint32_t id) { return self().is_materialized_(id); });
    expecting(plan.ok,
              "ChunkedSession::flush: gate operand has no carried state (stale wire)");

    self().run_program_into_(plan.prog, plan.input_ids, plan.output_ids);
    count_ands_(plan.prog);
    ctx_.drop_chunk();
  }
};

}  // namespace emp
#endif  // EMP_IR_SESSION_CHUNKED_SESSION_H__
