#ifndef EMP_CIRCUIT_BITVEC_H__
#define EMP_CIRCUIT_BITVEC_H__

// BitVec_T<Ctx,N>: a fixed-width bit vector over a BooleanContext — the natural
// value for crypto blocks (AES/SHA I/O) and for assembling / disassembling wider
// values bit by bit. clear_t is the N bools. It is interconvertible with
// UInt_T<Ctx,N> (same wires, zero gates).

#include "emp-tool/circuits/bit.h"
#include "emp-tool/circuits/unsigned_int.h"
#include "emp-tool/circuits/numeric_kernels.h"   // kernel::or_gate/equal/mux
#include "emp-tool/ir/context/checks.h"             // check_same_context
#include "emp-tool/runtime/core/utils.h"                  // error()
#include <array>
#include <vector>

namespace emp {

template <BooleanContext Ctx, int N>
class BitVec_T {
public:
    using Wire         = typename Ctx::Wire;
    using context_type = Ctx;
    using clear_t      = std::array<bool, N>;
    template <BooleanContext C2> using rebind = BitVec_T<C2, N>;
    std::array<Wire, N> w{};

    BitVec_T() = default;                          // uninitialized (null ctx) until assigned/from_wires
    explicit BitVec_T(Ctx& c) : ctx_(&c) {}
    static BitVec_T constant(Ctx& c, const clear_t& v) {
        BitVec_T r(c); for (int i = 0; i < N; ++i) r.w[i] = c.public_bit(v[i]); return r;
    }
    // Assemble from N Bit_T<Ctx> (e.g. a kernel's output bits).
    static BitVec_T from_bit_values(Ctx& c, const Bit_T<Ctx>* b) {
        BitVec_T r(c); for (int i = 0; i < N; ++i) r.w[i] = b[i].w; return r;
    }
    static BitVec_T from_wires(Ctx& c, const Wire* in) { BitVec_T r(c); for (int i = 0; i < N; ++i) r.w[i] = in[i]; return r; }

    Ctx* context() const { return ctx_; }
    BitVec_T constant(const clear_t& v) const { return constant(*ctx_, v); }   // same-context sugar
    Bit_T<Ctx> operator[](int i) const { return Bit_T<Ctx>(*ctx_, w[i]); }

    // Reinterpret the same wires as an unsigned integer (zero gates).
    UInt_T<Ctx, N> as_uint() const { return UInt_T<Ctx, N>::from_wires(*ctx_, w.data()); }

    // --- bitwise ops / equality / select / logical shifts (public amount) ---
    BitVec_T operator&(const BitVec_T& o) const { check_same_context(*this, o); BitVec_T r(*ctx_); for (int i = 0; i < N; ++i) r.w[i] = ctx_->and_gate(w[i], o.w[i]); return r; }
    BitVec_T operator|(const BitVec_T& o) const { check_same_context(*this, o); BitVec_T r(*ctx_); for (int i = 0; i < N; ++i) r.w[i] = kernel::or_gate(*ctx_, w[i], o.w[i]); return r; }
    BitVec_T operator^(const BitVec_T& o) const { check_same_context(*this, o); BitVec_T r(*ctx_); for (int i = 0; i < N; ++i) r.w[i] = ctx_->xor_gate(w[i], o.w[i]); return r; }
    BitVec_T operator~() const                  { BitVec_T r(*ctx_); for (int i = 0; i < N; ++i) r.w[i] = ctx_->not_gate(w[i]); return r; }
    Bit_T<Ctx> operator==(const BitVec_T& o) const { check_same_context(*this, o); return Bit_T<Ctx>(*ctx_, kernel::equal<Ctx>(*ctx_, w.data(), o.w.data(), N)); }
    Bit_T<Ctx> operator!=(const BitVec_T& o) const { return !(*this == o); }
    BitVec_T select(const Bit_T<Ctx>& sel, const BitVec_T& t) const {
        check_same_context(*this, sel); check_same_context(*this, t);
        BitVec_T r(*ctx_); for (int i = 0; i < N; ++i) r.w[i] = kernel::mux(*ctx_, sel.w, t.w[i], w[i]); return r;
    }
    BitVec_T operator<<(int s) const {
        expecting(s >= 0, "BitVec_T::operator<<: shift amount must be >= 0");
        BitVec_T r(*ctx_); Wire z = ctx_->public_bit(false);
        for (int i = 0; i < N; ++i) r.w[i] = (i >= s) ? w[i - s] : z;
        return r;
    }
    BitVec_T operator>>(int s) const {
        expecting(s >= 0, "BitVec_T::operator>>: shift amount must be >= 0");
        BitVec_T r(*ctx_); Wire z = ctx_->public_bit(false);
        for (int i = 0; i < N; ++i) r.w[i] = (i + s < N) ? w[i + s] : z;
        return r;
    }

    template <int Lo, int Hi> BitVec_T<Ctx, Hi - Lo> slice() const {
        static_assert(0 <= Lo && Lo <= Hi && Hi <= N, "BitVec_T::slice<Lo,Hi>: out of range");
        BitVec_T<Ctx, Hi - Lo> r(*ctx_); for (int i = 0; i < Hi - Lo; ++i) r.w[i] = w[Lo + i]; return r;
    }
    template <int M> BitVec_T<Ctx, N + M> concat(const BitVec_T<Ctx, M>& hi) const {  // this is the LOW half
        check_same_context(*this, hi);
        BitVec_T<Ctx, N + M> r(*ctx_);
        for (int i = 0; i < N; ++i) r.w[i] = w[i];
        for (int i = 0; i < M; ++i) r.w[N + i] = hi.w[i];
        return r;
    }

    static constexpr int width() { return N; }
    void pack_wires(Wire* out) const { for (int i = 0; i < N; ++i) out[i] = w[i]; }
    static std::array<bool, (std::size_t)N> encode(const clear_t& v) { return v; }
    static clear_t decode(const bool* b) { clear_t v{}; for (int i = 0; i < N; ++i) v[i] = b[i]; return v; }

private:
    Ctx* ctx_ = nullptr;
};

}  // namespace emp
#endif  // EMP_CIRCUIT_BITVEC_H__
