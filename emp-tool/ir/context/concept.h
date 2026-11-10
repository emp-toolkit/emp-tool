#ifndef EMP_CONTEXT_CONCEPT_H__
#define EMP_CONTEXT_CONCEPT_H__

// The C++20 BooleanContext contract. A circuit kernel is
// `template<BooleanContext Ctx> auto k(Ctx&, wires...) -> wires`; gates are
// value-return over a cheap, copyable `Ctx::Wire` handle, so dispatch is static
// and inlineable. Heavy protocol state lives in the context, keyed by the handle.
// The reusable contexts that realize it live in context/{clear,count,digest,
// record}.h; large circuits are recorded once (RecordCtx) into an ir
// BooleanProgram and replayed through any context via execute_program (ir/execute.h).

#include <concepts>
#include <cstddef>
#include <cstdint>

namespace emp {

// The contract. Wire is a cheap, copyable, default-constructible handle (no
// move-only: fan-out needs copies). semiregular, not regular: nothing in the
// framework compares wires — equality on a garbled label is semantically
// meaningless anyway — so raw block (__m128i, which has no operator==) works
// as a Wire directly. Gates are value-return.
template <class Ctx>
concept BooleanContext =
    std::semiregular<typename Ctx::Wire> &&
    requires(Ctx& c, typename Ctx::Wire a, typename Ctx::Wire b, bool v) {
        { c.public_bit(v) } -> std::same_as<typename Ctx::Wire>;
        { c.and_gate(a, b) } -> std::same_as<typename Ctx::Wire>;
        { c.xor_gate(a, b) } -> std::same_as<typename Ctx::Wire>;
        { c.not_gate(a) }    -> std::same_as<typename Ctx::Wire>;
    };

// A BulkBooleanContext can evaluate a whole AND layer at once (batched AES/OT in a
// real backend). The default-loop and_many on a scalar context still works; the
// win comes when a backend overrides it (see ir/schedule.h).
template <class Ctx>
concept BulkBooleanContext =
    BooleanContext<Ctx> &&
    requires(Ctx& c, typename Ctx::Wire* o, const typename Ctx::Wire* a, int64_t n) {
        c.and_many(o, a, a, n);
    };

}  // namespace emp
#endif  // EMP_CONTEXT_CONCEPT_H__
