#ifndef EMP_CIRCUIT_SIGNED_INT_H__
#define EMP_CIRCUIT_SIGNED_INT_H__

// Int_T<Ctx,N>: a two's-complement signed integer over a BooleanContext. As with
// UInt_T, N > 0 is a fixed-width WireValue and N == 0 (== runtime_width) is a
// runtime-width value. Add/sub/multiply share the unsigned ripple kernels (low N
// bits are identical); comparison is signed; right shift is arithmetic
// (sign-extending); division / remainder truncate toward zero. The fixed-only
// surface (clear codec, compile-time width, secret-amount shifts, sext/trunc) is
// `requires (N > 0)`; the runtime-only surface (width ctor/constant, resize) is
// `requires (N == 0)`.

#include "emp-tool/circuits/bit.h"
#include "emp-tool/circuits/unsigned_int.h"
#include "emp-tool/circuits/numeric_kernels.h"
#include "emp-tool/runtime/core/utils.h"
#include <array>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace emp {

template <BooleanContext Ctx, int N>
class Int_T {
public:
    using Wire         = typename Ctx::Wire;
    using context_type = Ctx;
    using clear_t      = int64_t;
    template <BooleanContext C2> using rebind = Int_T<C2, N>;
    static constexpr bool is_dynamic = (N == 0);
    using storage = std::conditional_t<is_dynamic, std::vector<Wire>, std::array<Wire, (N > 0 ? N : 1)>>;
    storage w{};

    Int_T() = default;
    explicit Int_T(Ctx& c) : ctx_(&c) {}
    Int_T(Ctx& c, int width) requires (N == 0) : ctx_(&c) { validate_width_(width); w.resize((std::size_t)width); }

    // Public constant. Bits beyond 64 sign-extend (replicate bit 63).
    static Int_T constant(Ctx& c, int64_t v) requires (N > 0) {
        uint64_t u = (uint64_t)v;   // shift the bit pattern as unsigned (signed >> is impl-defined)
        Int_T r(c); for (int i = 0; i < N; ++i) r.w[i] = c.public_bit((u >> (i < 64 ? i : 63)) & 1); return r;
    }
    static Int_T constant(Ctx& c, int width, int64_t v) requires (N == 0) {
        uint64_t u = (uint64_t)v;
        Int_T r(c, width); for (int i = 0; i < width; ++i) r.w[i] = c.public_bit((u >> (i < 64 ? i : 63)) & 1); return r;
    }
    static Int_T from_wires(Ctx& c, const Wire* in) requires (N > 0) {
        Int_T r(c); for (int i = 0; i < N; ++i) r.w[i] = in[i]; return r;
    }
    static Int_T from_wires(Ctx& c, const Wire* in, int n) requires (N == 0) {
        Int_T r(c, n); for (int i = 0; i < n; ++i) r.w[i] = in[i]; return r;
    }

    Ctx* context() const { return ctx_; }
    const Wire* data() const { return w.data(); }
    Wire* data() { return w.data(); }
    Int_T constant(int64_t v) const requires (N > 0) { return constant(*ctx_, v); }   // same-context sugar
    Bit_T<Ctx> operator[](int i) const { return Bit_T<Ctx>(*ctx_, w[i]); }

    Int_T operator+(const Int_T& o) const { check_same_context(*this, o); same_width_(o); Int_T r = blank_(); kernel::ripple_add<Ctx>(*ctx_, w.data(), o.w.data(), r.w.data(), n_()); return r; }
    Int_T operator-(const Int_T& o) const { check_same_context(*this, o); same_width_(o); Int_T r = blank_(); kernel::ripple_sub<Ctx>(*ctx_, w.data(), o.w.data(), r.w.data(), n_()); return r; }
    Int_T operator-() const { return zeros_() - *this; }

    // signed a < b: lt = (a[hi] != b[hi]) ? a[hi] : borrow(a-b).
    Bit_T<Ctx> operator<(const Int_T& o) const {
        check_same_context(*this, o); same_width_(o);
        Wire ub = kernel::less_than<Ctx>(*ctx_, w.data(), o.w.data(), n_());   // unsigned a<b borrow
        Wire sa = w[n_() - 1], sb = o.w[n_() - 1];
        Wire diff = ctx_->xor_gate(sa, sb);
        return Bit_T<Ctx>(*ctx_, kernel::mux(*ctx_, diff, sa, ub));            // signed_lt = diff ? sa : ub
    }
    Bit_T<Ctx> operator>(const Int_T& o) const { return o < *this; }
    Bit_T<Ctx> operator==(const Int_T& o) const { check_same_context(*this, o); same_width_(o); return Bit_T<Ctx>(*ctx_, kernel::equal<Ctx>(*ctx_, w.data(), o.w.data(), n_())); }
    Bit_T<Ctx> operator!=(const Int_T& o) const { return !(*this == o); }
    Bit_T<Ctx> operator<=(const Int_T& o) const { return !(*this > o); }
    Bit_T<Ctx> operator>=(const Int_T& o) const { return !(*this < o); }

    Int_T operator*(const Int_T& o) const { check_same_context(*this, o); same_width_(o); Int_T r = blank_(); kernel::mul_full<Ctx>(*ctx_, r.w.data(), w.data(), o.w.data(), n_()); return r; }
    Int_T operator&(const Int_T& o) const { check_same_context(*this, o); same_width_(o); Int_T r = blank_(); for (int i = 0; i < n_(); ++i) r.w[i] = ctx_->and_gate(w[i], o.w[i]); return r; }
    Int_T operator^(const Int_T& o) const { check_same_context(*this, o); same_width_(o); Int_T r = blank_(); for (int i = 0; i < n_(); ++i) r.w[i] = ctx_->xor_gate(w[i], o.w[i]); return r; }
    Int_T operator|(const Int_T& o) const { check_same_context(*this, o); same_width_(o); Int_T r = blank_(); for (int i = 0; i < n_(); ++i) r.w[i] = kernel::or_gate(*ctx_, w[i], o.w[i]); return r; }
    Int_T operator~() const               { Int_T r = blank_(); for (int i = 0; i < n_(); ++i) r.w[i] = ctx_->not_gate(w[i]); return r; }

    // Signed division / remainder (truncate toward zero; remainder takes the
    // dividend's sign): divide the magnitudes, then fix signs. (Division by zero
    // saturates via the unsigned kernel; the most-negative operand is a UB
    // precondition.)
    Int_T operator/(const Int_T& o) const {
        check_same_context(*this, o); same_width_(o);
        Bit_T<Ctx> sa = (*this)[n_() - 1], sb = o[n_() - 1];
        Int_T ua = this->select(sa, -*this), ub = o.select(sb, -o);   // magnitudes
        Int_T uq = blank_(); kernel::div_full<Ctx>(*ctx_, uq.w.data(), nullptr, ua.w.data(), ub.w.data(), n_());
        return uq.select(sa != sb, -uq);
    }
    Int_T operator%(const Int_T& o) const {
        check_same_context(*this, o); same_width_(o);
        Bit_T<Ctx> sa = (*this)[n_() - 1], sb = o[n_() - 1];
        Int_T ua = this->select(sa, -*this), ub = o.select(sb, -o);
        Int_T ur = blank_(); kernel::div_full<Ctx>(*ctx_, nullptr, ur.w.data(), ua.w.data(), ub.w.data(), n_());
        return ur.select(sa, -ur);
    }

    // sel ? t : *this. The selector's context is debug-checked alongside t.
    Int_T select(const Bit_T<Ctx>& sel, const Int_T& t) const {
        check_same_context(*this, sel); check_same_context(*this, t); same_width_(t);
        Int_T r = blank_(); for (int i = 0; i < n_(); ++i) r.w[i] = kernel::mux(*ctx_, sel.w, t.w[i], w[i]); return r;
    }

    // Reinterpret the same wires as an unsigned integer (zero gates).
    UInt_T<Ctx, N> as_unsigned() const requires (N > 0) { return UInt_T<Ctx, N>::from_wires(*ctx_, w.data()); }
    UInt_T<Ctx, 0> as_unsigned() const requires (N == 0) { return UInt_T<Ctx, 0>::from_wires(*ctx_, w.data(), n_()); }

    // --- shifts by a PUBLIC constant amount (s >= 0): logical left, ARITHMETIC
    //     right (sign-extending). ---
    Int_T operator<<(int s) const {
        expecting(s >= 0, "Int_T::operator<<: shift amount must be >= 0");
        Int_T r = blank_(); Wire z = ctx_->public_bit(false);
        for (int i = 0; i < n_(); ++i) r.w[i] = (i >= s) ? w[i - s] : z;
        return r;
    }
    Int_T operator>>(int s) const {                 // arithmetic: fill with sign bit
        expecting(s >= 0, "Int_T::operator>>: shift amount must be >= 0");
        // Amounts >= width sign-fill (docs/numeric_semantics.md); clamp
        // before the i + s index test can overflow int.
        if (s > n_()) s = n_();
        Int_T r = blank_(); Wire sgn = w[n_() - 1];
        for (int i = 0; i < n_(); ++i) r.w[i] = (i + s < n_()) ? w[i + s] : sgn;
        return r;
    }

    // --- shifts by a SECRET (unsigned) amount (fixed width only): logical left,
    //     ARITHMETIC right. Barrel shifter over the low ceil(log2 N) bits; a higher
    //     set bit drives the result to 0 (left) or all-sign-bits (right). ---
    Int_T operator<<(const UInt_T<Ctx, N>& shamt) const requires (N > 0) {
        Int_T res(*this);
        constexpr int use = (N <= 1) ? 0 : kernel::clog2_ceil(N);
        for (int i = 0; i < use; ++i) res = res.select(shamt[i], res << (1 << i));
        Bit_T<Ctx> overflow = Bit_T<Ctx>::constant(*ctx_, false);
        for (int i = use; i < N; ++i) overflow = overflow | shamt[i];
        return res.select(overflow, Int_T::constant(*ctx_, 0));
    }
    Int_T operator>>(const UInt_T<Ctx, N>& shamt) const requires (N > 0) {
        Int_T res(*this);
        constexpr int use = (N <= 1) ? 0 : kernel::clog2_ceil(N);
        for (int i = 0; i < use; ++i) res = res.select(shamt[i], res >> (1 << i));
        Bit_T<Ctx> overflow = Bit_T<Ctx>::constant(*ctx_, false);
        for (int i = use; i < N; ++i) overflow = overflow | shamt[i];
        Int_T allsign(*ctx_); for (int j = 0; j < N; ++j) allsign.w[j] = w[N - 1];
        return res.select(overflow, allsign);
    }
    template <int M> Int_T<Ctx, M> sext() const requires (N > 0) {
        static_assert(M >= N, "Int_T::sext<M>: M must be >= width");
        Int_T<Ctx, M> r(*ctx_); for (int i = 0; i < M; ++i) r.w[i] = (i < N) ? w[i] : w[N - 1]; return r;
    }
    template <int M> Int_T<Ctx, M> trunc() const requires (N > 0) {
        static_assert(M <= N, "Int_T::trunc<M>: M must be <= width");
        Int_T<Ctx, M> r(*ctx_); for (int i = 0; i < M; ++i) r.w[i] = w[i]; return r;
    }
    // Runtime resize sign-extends (or truncates) to a runtime width.
    Int_T resize(int m) const requires (N == 0) {
        validate_width_(m);
        Int_T r(*ctx_, m); Wire sgn = w[n_() - 1];
        for (int i = 0; i < m; ++i) r.w[i] = (i < n_()) ? w[i] : sgn;
        return r;
    }

    // WireBundle contract (fixed width only — N == 0 models neither concept).
    static constexpr int width() requires (N > 0) { return N; }
    int width() const requires (N == 0) { return (int)w.size(); }
    void pack_wires(Wire* out) const requires (N > 0) { for (int i = 0; i < N; ++i) out[i] = w[i]; }
    // Clear codec (the WireValue half): rides an int64_t, so it exists only for
    // N <= 64 — a wider Int_T is a WireBundle but not a WireValue (see
    // unsigned_int.h for the same note).
    static std::array<bool, (std::size_t)(N > 0 ? N : 1)> encode(int64_t v) requires (N > 0 && N <= 64) {
        uint64_t u = (uint64_t)v;
        std::array<bool, (std::size_t)(N > 0 ? N : 1)> b{};
        for (int i = 0; i < N; ++i) b[(std::size_t)i] = (u >> i) & 1; return b;
    }
    static int64_t decode(const bool* bits) requires (N > 0 && N <= 64) {
        uint64_t v = 0; for (int i = 0; i < N; ++i) v |= (uint64_t)(bits[i] ? 1 : 0) << i;
        if (N < 64 && ((v >> (N - 1)) & 1)) v |= ~((uint64_t(1) << N) - 1);
        return (int64_t)v;
    }

    // Runtime-width contract (RuntimeWidthValue; session-I/O only, never compiled).
    // Byte-bool (uint8_t 0/1) runtime-sized codec; sessions copy into real bool
    // storage at the engine boundary, never reinterpret uint8_t* as bool*. Caller
    // supplies width; two's-complement: encode sign-extends past 64, decode
    // sign-interprets the top bit of `width`.
    void pack_wires(Wire* out) const requires (N == 0) { for (int i = 0; i < n_(); ++i) out[i] = w[i]; }
    static std::vector<uint8_t> encode(int64_t v, int width) requires (N == 0) {
        uint64_t u = (uint64_t)v;
        std::vector<uint8_t> b((std::size_t)width);
        for (int i = 0; i < width; ++i) b[(std::size_t)i] = (uint8_t)((u >> (i < 64 ? i : 63)) & 1);
        return b;
    }
    static int64_t decode(const uint8_t* bits, int width) requires (N == 0) {
        uint64_t v = 0; for (int i = 0; i < width && i < 64; ++i) v |= (uint64_t)(bits[i] ? 1 : 0) << i;
        int sb = (width < 64) ? width : 64;
        if (sb < 64 && ((v >> (sb - 1)) & 1)) v |= ~((uint64_t(1) << sb) - 1);
        return (int64_t)v;
    }

private:
    Ctx* ctx_ = nullptr;
    int n_() const { return (int)w.size(); }
    static void validate_width_(int width) { expecting(width >= 1, "Int_T: runtime width must be >= 1"); }
    Int_T blank_() const { Int_T r(*ctx_); if constexpr (N == 0) r.w.resize(w.size()); return r; }
    Int_T zeros_() const { Int_T r = blank_(); Wire z = ctx_->public_bit(false); for (int i = 0; i < n_(); ++i) r.w[i] = z; return r; }
    void same_width_(const Int_T& o) const {
        if constexpr (N == 0)
            expecting(w.size() == o.w.size(), "Int_T: operands have different runtime widths");
    }
};

// UInt_T::as_signed, out of line now that Int_T is complete (same wires, zero gates).
template <BooleanContext Ctx, int N>
inline Int_T<Ctx, N> UInt_T<Ctx, N>::as_signed() const requires (N > 0) {
    return Int_T<Ctx, N>::from_wires(*ctx_, w.data());
}
template <BooleanContext Ctx, int N>
inline Int_T<Ctx, 0> UInt_T<Ctx, N>::as_signed() const requires (N == 0) {
    return Int_T<Ctx, 0>::from_wires(*ctx_, w.data(), (int)w.size());
}

}  // namespace emp
#endif  // EMP_CIRCUIT_SIGNED_INT_H__
