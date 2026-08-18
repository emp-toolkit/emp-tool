#ifndef EMP_IR_SESSION_CHUNK_RECORDER_H__
#define EMP_IR_SESSION_CHUNK_RECORDER_H__

// ChunkRecorderCtx records pending gates for direct/chunked sessions. Refcounted
// recorder wires track which logical ids remain in C++ scope; the owning session
// materializes them at flush boundaries. Id 0 is the null sentinel.
//
// One process-global recorder may be active at a time. Recording and recorder-wire
// lifetime operations are single-threaded, and values must not outlive the recorder.

#include "emp-tool/ir/program.h"           // circuit::Gate, Op
#include "emp-tool/ir/context/concept.h"   // BooleanContext
#include "emp-tool/ir/context/checks.h"    // EMP_CONTEXT_CHECKS
#include "emp-tool/runtime/core/utils.h"   // error()
#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace emp {

class ChunkRecorderCtx;
inline ChunkRecorderCtx* g_chunk_rec = nullptr;   // current recorder (process-global; see CONSTRAINT)
inline void chunk_pin(uint32_t id) noexcept;
inline void chunk_unpin(uint32_t id) noexcept;

// 4-byte refcounted recorder wire. pin/unpin hit the current recorder's count
// table; id 0 is the null sentinel (default / moved-from), skipped by pin/unpin.
struct ChunkWire {
  uint32_t id = 0;
  ChunkWire() noexcept = default;
  ChunkWire(uint32_t i) noexcept : id(i) { chunk_pin(id); }
  ChunkWire(const ChunkWire& o) noexcept : id(o.id) { chunk_pin(id); }
  ChunkWire(ChunkWire&& o) noexcept : id(o.id) { o.id = 0; }   // steal: no table touch
  ChunkWire& operator=(const ChunkWire& o) noexcept {
    if (id != o.id) { chunk_unpin(id); id = o.id; chunk_pin(id); }
    return *this;
  }
  ChunkWire& operator=(ChunkWire&& o) noexcept {
    if (this != &o) { chunk_unpin(id); id = o.id; o.id = 0; }
    return *this;
  }
  ~ChunkWire() noexcept { chunk_unpin(id); }
  operator uint32_t() const noexcept { return id; }
};

class ChunkRecorderCtx {
public:
  using Wire = ChunkWire;
  std::vector<int> rc_;       // refcount table, indexed by id; rc_.size() == next_id_

  ChunkRecorderCtx() {
    // Recorder wires reach their count table through the current recorder.
    expecting(g_chunk_rec == nullptr,
              "ChunkRecorderCtx: another chunked-session recorder is already active; "
              "one must be destroyed before another records (use separate processes "
              "for concurrent sessions)");
    rc_.reserve(1u << 16);
    rc_.push_back(0);         // id 0 = null sentinel
    next_id_ = 1;
    g_chunk_rec = this;
  }
  ~ChunkRecorderCtx() {
    // A live refcount at teardown means a value still holds a wire into this table;
    // once g_chunk_rec is cleared (or repointed at the next recorder) that wire's
    // dtor would underflow/corrupt the wrong table. Fail loudly on the violation
    // ("values must not outlive their recorder") instead of corrupting silently.
    for (uint32_t id = 1; id < rc_.size(); ++id)
      expecting(rc_[id] == 0,
                "ChunkRecorderCtx: destroyed while a value still holds a recorder wire "
                "(a value outlived its recorder — RAII lifetime violation)");
    if (g_chunk_rec == this) g_chunk_rec = nullptr;
  }
  ChunkRecorderCtx(const ChunkRecorderCtx&) = delete;
  ChunkRecorderCtx& operator=(const ChunkRecorderCtx&) = delete;

  // ---- BooleanContext gate ops: record into the current chunk (pending) ----
  Wire public_bit(bool v) {
    int idx = v ? 1 : 0;
    if (const_wire_[idx] < 0) {
      uint32_t o = alloc_id_();
      chunk_gates_.push_back({0, 0, o, v ? circuit::Op::Const1 : circuit::Op::Const0});
      pending_.insert(o);
      const_wire_[idx] = (int64_t)o;
    }
    return (Wire)(uint32_t)const_wire_[idx];
  }
  // Operands const-ref: no pin/unpin of a by-value copy on every gate.
  Wire and_gate(const Wire& a, const Wire& b) { require_allocated_(a); require_allocated_(b); return record_(a, b, circuit::Op::And); }
  Wire xor_gate(const Wire& a, const Wire& b) { require_allocated_(a); require_allocated_(b); return record_(a, b, circuit::Op::Xor); }
  Wire not_gate(const Wire& a)                { require_allocated_(a); return record_(a, 0, circuit::Op::Not); }

  // ---- recorder substrate the owning Session drives (not user-facing) ----
  // Allocate n fresh (unpinned) ids for a materialized bundle the Session fills.
  std::vector<uint32_t> reserve_ids(std::size_t n) {
    std::vector<uint32_t> ids(n);
    for (std::size_t i = 0; i < n; ++i) ids[i] = alloc_id_();
    return ids;
  }
  bool is_pending(uint32_t id) const { return pending_.count(id) != 0; }
  // A live C++ value still references id (refcount > 0) — i.e. it is in scope.
  bool in_scope(uint32_t id) const { return id < rc_.size() && rc_[id] > 0; }
  // Pending wires still referenced by a live value — the implicit keep-set for a flush.
  std::vector<uint32_t> live_pending_ids() const {
    std::vector<uint32_t> r;
    for (const circuit::Gate& g : chunk_gates_)
      if (g.out < rc_.size() && rc_[g.out] > 0) r.push_back(g.out);
    return r;
  }
  const std::vector<circuit::Gate>& chunk_gates() const { return chunk_gates_; }
  void drop_chunk() {
    chunk_gates_.clear();
    pending_.clear();
    const_wire_[0] = const_wire_[1] = -1;
  }

private:
  uint32_t next_id_ = 1;                         // monotonic recorder id (1-based; 0 = null)
  std::vector<circuit::Gate> chunk_gates_;       // current chunk (recorder ids)
  std::unordered_set<uint32_t> pending_;         // ids produced in the current chunk
  int64_t const_wire_[2] = {-1, -1};             // per-chunk Const0/Const1 dedup

  uint32_t alloc_id_() {
    expecting(next_id_ != UINT32_MAX,
              "ChunkRecorderCtx: recorder wire id overflow");
    rc_.push_back(0);                            // grow count table in lockstep with ids
    return next_id_++;
  }
  void require_allocated_(const Wire& a) const {
    expecting(a < next_id_,
              "ChunkRecorderCtx: operand id was never allocated (corrupt wire)");
  }
  Wire record_(const Wire& a, const Wire& b, circuit::Op op) {
    uint32_t o = alloc_id_();
    chunk_gates_.push_back({a, b, o, op});
    pending_.insert(o);
    return o;
  }
};

// Hot path: a single indexed inc/dec on the current recorder's count table.
// EMP_CONTEXT_CHECKS adds a debug-only bounds guard that catches an id outside the
// active recorder's range — e.g. a wire from a value that outlived its recorder and
// is now pinning/unpinning against a different (smaller) table.
#if EMP_CONTEXT_CHECKS
inline void chunk_rc_bounds_(uint32_t id) noexcept {
  expecting(id < g_chunk_rec->rc_.size(),
            "ChunkRecorderCtx: wire id out of range for the active recorder "
            "(a value outlived its recorder?)");
}
#endif
inline void chunk_pin(uint32_t id) noexcept {
  if (id && g_chunk_rec) {
#if EMP_CONTEXT_CHECKS
    chunk_rc_bounds_(id);
#endif
    g_chunk_rec->rc_[id]++;
  }
}
inline void chunk_unpin(uint32_t id) noexcept {
  if (id && g_chunk_rec) {
#if EMP_CONTEXT_CHECKS
    chunk_rc_bounds_(id);
#endif
    g_chunk_rec->rc_[id]--;
  }
}

static_assert(BooleanContext<ChunkRecorderCtx>);

}  // namespace emp
#endif  // EMP_IR_SESSION_CHUNK_RECORDER_H__
