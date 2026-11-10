#ifndef EMP_CONTEXT_CLEAR_H__
#define EMP_CONTEXT_CLEAR_H__

// ClearCtx — plaintext evaluation. Wire = uint8_t bit (0/1), no folding, so gate
// counts match a recorded program exactly. The crypto-free static arm.

#include "emp-tool/ir/context/concept.h"
#include "emp-tool/runtime/core/error.h"
#include <cstddef>
#include <cstdint>

namespace emp {

// ClearCtx is pure circuit execution — gates only, no protocol I/O. Input and
// reveal are a Session concern (session/clear_session.h owns a ClearCtx and the
// I/O boundary); ClearCtx stays usable on its own for internal / analysis work.
struct ClearCtx {
    using Wire = uint8_t;
    Wire public_bit(bool v)       { return v ? 1 : 0; }
    Wire and_gate(Wire a, Wire b) { return a & b; }
    Wire xor_gate(Wire a, Wire b) { return a ^ b; }
    Wire not_gate(Wire a)         { return a ^ 1; }
    // Bulk hook (a real backend would batch AES/OT here); default scalar loop.
    void and_many(Wire* o, const Wire* a, const Wire* b, int64_t n) {
        expecting(n >= 0, "ClearCtx::and_many: negative count");
        for (int64_t i = 0; i < n; ++i) o[i] = a[i] & b[i];
    }
};

static_assert(BooleanContext<ClearCtx>);

}  // namespace emp
#endif  // EMP_CONTEXT_CLEAR_H__
