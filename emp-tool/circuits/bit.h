#ifndef EMP_CIRCUIT_BIT_H__
#define EMP_CIRCUIT_BIT_H__

// Bit_T<Ctx>: a single circuit bit over a BooleanContext. It carries one Ctx*
// (private; reached via context()) plus its wire, and overloads the boolean
// operators as value-return gates on the context (static dispatch, no global
// backend). The shared cross-context guard check_same_context lives in
// context/checks.h, since every value type uses it.

#include "emp-tool/ir/context/concept.h"
#include "emp-tool/ir/context/checks.h"             // check_same_context
#include "emp-tool/circuits/numeric_kernels.h"   // kernel::mux for select
#include "emp-tool/runtime/core/utils.h"                  // error()
#include <array>
#include <vector>

namespace emp {

template <BooleanContext Ctx>
class Bit_T {
public:
    using Wire         = typename Ctx::Wire;
    using context_type = Ctx;
    using clear_t      = bool;
    template <BooleanContext C2> using rebind = Bit_T<C2>;
    Wire w{};

    // Default ctor: UNINITIALIZED — null context, no usable wire. This supports
    // narrow scalar scratch; use BitVec_T for multi-bit values. Assign / from_wires
    // before any op. Operating on a default-constructed value is UB, like a
    // moved-from one. (Same contract for UInt_T / Int_T / Float_T / BitVec_T.)
    Bit_T() = default;
    Bit_T(Ctx& c, Wire wire) : w(wire), ctx_(&c) {}
    static Bit_T constant(Ctx& c, bool v) { return Bit_T(c, c.public_bit(v)); }

    Ctx* context() const { return ctx_; }
    Bit_T  constant(bool v) const { return constant(*ctx_, v); }   // same-context sugar

    Bit_T operator&(const Bit_T& o) const { check_same_context(*this, o); return Bit_T(*ctx_, ctx_->and_gate(w, o.w)); }
    Bit_T operator^(const Bit_T& o) const { check_same_context(*this, o); return Bit_T(*ctx_, ctx_->xor_gate(w, o.w)); }
    Bit_T operator|(const Bit_T& o) const { return *this ^ o ^ (*this & o); }
    Bit_T operator!() const                { return Bit_T(*ctx_, ctx_->not_gate(w)); }
    Bit_T operator==(const Bit_T& o) const { return !(*this ^ o); }   // XNOR
    Bit_T operator!=(const Bit_T& o) const { return *this ^ o; }      // XOR
    // sel ? t : *this
    Bit_T select(const Bit_T& sel, const Bit_T& t) const {
        check_same_context(*this, sel); check_same_context(*this, t);
        return Bit_T(*ctx_, kernel::mux(*ctx_, sel.w, t.w, w));
    }

    static constexpr int width() { return 1; }
    void pack_wires(Wire* out) const { out[0] = w; }
    static Bit_T from_wires(Ctx& c, const Wire* in) { return Bit_T(c, in[0]); }
    static std::array<bool, 1> encode(bool v) { return {v}; }
    static bool decode(const bool* b)       { return b[0]; }

private:
    Ctx* ctx_ = nullptr;
};

}  // namespace emp
#endif  // EMP_CIRCUIT_BIT_H__
