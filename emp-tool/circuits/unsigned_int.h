#ifndef EMP_CIRCUIT_UNSIGNED_INT_H__
#define EMP_CIRCUIT_UNSIGNED_INT_H__

// UInt_T<Ctx,N>: an unsigned integer over a BooleanContext. N > 0 is a fixed-width
// value (a WireValue: compile-time width, clear codec, wire layout); N == 0
// (== runtime_width) is a runtime-width value whose width lives in the wire vector
// and is chosen at construction. Both share one set of operators: arithmetic
// (+,-,*,/,%) over the runtime-sized numeric_kernels, comparisons returning
// Bit_T<Ctx>, bitwise ops, and public-amount shifts/rotates (pure wiring). The
// fixed-only surface (clear codec, compile-time width views slice/concat/zext,
// secret-amount barrel shifts, popcount) is `requires (N > 0)`; the runtime-only
// surface (width-taking ctor/constant, resize) is `requires (N == 0)`.

#include "emp-tool/circuits/bit.h"
#include "emp-tool/circuits/numeric_kernels.h"
#include "emp-tool/runtime/core/utils.h"
#include <array>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace emp {

// Width sentinel: UInt_T<Ctx, runtime_width> / Int_T<Ctx, runtime_width> are the
// runtime-width forms.
inline constexpr int runtime_width = 0;

template <BooleanContext Ctx, int N> class Int_T;   // for as_signed (defined in signed_int.h)

template <BooleanContext Ctx, int N>
class UInt_T {
public:
    using Wire         = typename Ctx::Wire;
    using context_type = Ctx;
    using clear_t      = uint64_t;
    template <BooleanContext C2> using rebind = UInt_T<C2, N>;
    static constexpr bool is_dynamic = (N == 0);
    // Fixed width is an inline array; runtime width is a vector sized at construction.
    using storage = std::conditional_t<is_dynamic, std::vector<Wire>, std::array<Wire, (N > 0 ? N : 1)>>;
    storage w{};

    UInt_T() = default;
    explicit UInt_T(Ctx& c) : ctx_(&c) {}
    // Runtime-width construction: width must be >= 1.
    UInt_T(Ctx& c, int width) requires (N == 0) : ctx_(&c) { validate_width_(width); w.resize((std::size_t)width); }

    // Public constant. Bits beyond 64 zero-extend (the carrier is a uint64).
    static UInt_T constant(Ctx& c, uint64_t v) requires (N > 0) {
        UInt_T r(c);
        for (int i = 0; i < N; ++i) r.w[i] = c.public_bit(i < 64 ? (v >> i) & 1 : 0);
        return r;
    }
    static UInt_T constant(Ctx& c, int width, uint64_t v) requires (N == 0) {
        UInt_T r(c, width);
        for (int i = 0; i < width; ++i) r.w[i] = c.public_bit(i < 64 ? (v >> i) & 1 : 0);
        return r;
    }
    static UInt_T from_wires(Ctx& c, const Wire* in) requires (N > 0) {
        UInt_T r(c); for (int i = 0; i < N; ++i) r.w[i] = in[i]; return r;
    }
    static UInt_T from_wires(Ctx& c, const Wire* in, int n) requires (N == 0) {
        UInt_T r(c, n); for (int i = 0; i < n; ++i) r.w[i] = in[i]; return r;
    }

    Ctx* context() const { return ctx_; }
    const Wire* data() const { return w.data(); }
    Wire* data() { return w.data(); }
    UInt_T constant(uint64_t v) const requires (N > 0) { return constant(*ctx_, v); }   // same-context sugar

    Bit_T<Ctx> operator[](int i) const { return Bit_T<Ctx>(*ctx_, w[i]); }

    UInt_T operator+(const UInt_T& o) const { check_same_context(*this, o); same_width_(o); UInt_T r = blank_(); kernel::ripple_add<Ctx>(*ctx_, w.data(), o.w.data(), r.w.data(), n_()); return r; }
    UInt_T operator-(const UInt_T& o) const { check_same_context(*this, o); same_width_(o); UInt_T r = blank_(); kernel::ripple_sub<Ctx>(*ctx_, w.data(), o.w.data(), r.w.data(), n_()); return r; }
    UInt_T operator&(const UInt_T& o) const { check_same_context(*this, o); same_width_(o); UInt_T r = blank_(); for (int i = 0; i < n_(); ++i) r.w[i] = ctx_->and_gate(w[i], o.w[i]); return r; }
    UInt_T operator^(const UInt_T& o) const { check_same_context(*this, o); same_width_(o); UInt_T r = blank_(); for (int i = 0; i < n_(); ++i) r.w[i] = ctx_->xor_gate(w[i], o.w[i]); return r; }
    UInt_T operator|(const UInt_T& o) const { check_same_context(*this, o); same_width_(o); UInt_T r = blank_(); for (int i = 0; i < n_(); ++i) r.w[i] = kernel::or_gate(*ctx_, w[i], o.w[i]); return r; }
    UInt_T operator~() const                { UInt_T r = blank_(); for (int i = 0; i < n_(); ++i) r.w[i] = ctx_->not_gate(w[i]); return r; }

    // Multiply is an O(N^2) shift-add; division/mod is restoring division (the
    // largest kernel here). Division by zero saturates (see kernel::div_full).
    UInt_T operator*(const UInt_T& o) const { check_same_context(*this, o); same_width_(o); UInt_T r = blank_(); kernel::mul_full<Ctx>(*ctx_, r.w.data(), w.data(), o.w.data(), n_()); return r; }
    UInt_T operator/(const UInt_T& o) const { check_same_context(*this, o); same_width_(o); UInt_T r = blank_(); kernel::div_full<Ctx>(*ctx_, r.w.data(), nullptr, w.data(), o.w.data(), n_()); return r; }
    UInt_T operator%(const UInt_T& o) const { check_same_context(*this, o); same_width_(o); UInt_T r = blank_(); kernel::div_full<Ctx>(*ctx_, nullptr, r.w.data(), w.data(), o.w.data(), n_()); return r; }

    Bit_T<Ctx> operator==(const UInt_T& o) const { check_same_context(*this, o); same_width_(o); return Bit_T<Ctx>(*ctx_, kernel::equal<Ctx>(*ctx_, w.data(), o.w.data(), n_())); }
    Bit_T<Ctx> operator!=(const UInt_T& o) const { return !(*this == o); }
    Bit_T<Ctx> operator<(const UInt_T& o)  const { check_same_context(*this, o); same_width_(o); return Bit_T<Ctx>(*ctx_, kernel::less_than<Ctx>(*ctx_, w.data(), o.w.data(), n_())); }
    Bit_T<Ctx> operator>(const UInt_T& o)  const { return o < *this; }
    Bit_T<Ctx> operator<=(const UInt_T& o) const { return !(*this > o); }
    Bit_T<Ctx> operator>=(const UInt_T& o) const { return !(*this < o); }

    // sel ? t : *this. The selector's context is debug-checked alongside t.
    UInt_T select(const Bit_T<Ctx>& sel, const UInt_T& t) const {
        check_same_context(*this, sel); check_same_context(*this, t); same_width_(t);
        UInt_T r = blank_(); for (int i = 0; i < n_(); ++i) r.w[i] = kernel::mux(*ctx_, sel.w, t.w[i], w[i]); return r;
    }

    // --- shifts / rotates by a PUBLIC constant amount (pure wiring, no gates).
    //     Shift amount must be >= 0; logical (zero-fill) for both directions. ---
    UInt_T operator<<(int s) const {
        expecting(s >= 0, "UInt_T::operator<<: shift amount must be >= 0");
        UInt_T r = blank_(); Wire z = ctx_->public_bit(false);
        for (int i = 0; i < n_(); ++i) r.w[i] = (i >= s) ? w[i - s] : z;
        return r;
    }
    UInt_T operator>>(int s) const {
        expecting(s >= 0, "UInt_T::operator>>: shift amount must be >= 0");
        UInt_T r = blank_(); Wire z = ctx_->public_bit(false);
        for (int i = 0; i < n_(); ++i) r.w[i] = (i + s < n_()) ? w[i + s] : z;
        return r;
    }
    UInt_T rotl(int s) const { int n = n_(); UInt_T r = blank_(); s = ((s % n) + n) % n; for (int i = 0; i < n; ++i) r.w[i] = w[((i - s) % n + n) % n]; return r; }
    UInt_T rotr(int s) const { int n = n_(); UInt_T r = blank_(); s = ((s % n) + n) % n; for (int i = 0; i < n; ++i) r.w[i] = w[(i + s) % n]; return r; }

    // --- shifts by a SECRET amount (barrel shifter, fixed width only): log-depth
    //     muxes over the low ceil(log2 N) bits of `shamt`; any higher set bit
    //     zeroes the result. ---
    UInt_T operator<<(const UInt_T& shamt) const requires (N > 0) {
        check_same_context(*this, shamt);
        UInt_T res(*this);
        constexpr int use = (N <= 1) ? 0 : kernel::clog2_ceil(N);
        for (int i = 0; i < use; ++i) res = res.select(shamt[i], res << (1 << i));
        Bit_T<Ctx> overflow = Bit_T<Ctx>::constant(*ctx_, false);
        for (int i = use; i < N; ++i) overflow = overflow | shamt[i];
        return res.select(overflow, UInt_T::constant(*ctx_, 0));
    }
    UInt_T operator>>(const UInt_T& shamt) const requires (N > 0) {
        check_same_context(*this, shamt);
        UInt_T res(*this);
        constexpr int use = (N <= 1) ? 0 : kernel::clog2_ceil(N);
        for (int i = 0; i < use; ++i) res = res.select(shamt[i], res >> (1 << i));
        Bit_T<Ctx> overflow = Bit_T<Ctx>::constant(*ctx_, false);
        for (int i = use; i < N; ++i) overflow = overflow | shamt[i];
        return res.select(overflow, UInt_T::constant(*ctx_, 0));
    }

    // --- bit counting: popcount sums the set bits; hamming_weight picks the
    //     natural result width; leading_zeros counts the high zero run. ---
    template <int R> UInt_T<Ctx, R> popcount() const requires (N > 0) {
        UInt_T<Ctx, R> acc = UInt_T<Ctx, R>::constant(*ctx_, 0);
        for (int i = 0; i < N; ++i) {
            UInt_T<Ctx, R> b = UInt_T<Ctx, R>::constant(*ctx_, 0);
            b.w[0] = w[i];
            acc = acc + b;
        }
        return acc;
    }
    UInt_T<Ctx, kernel::bits_for(N)> hamming_weight() const requires (N > 0) { return popcount<kernel::bits_for(N)>(); }
    UInt_T<Ctx, kernel::bits_for(N)> leading_zeros() const requires (N > 0) {
        UInt_T sat(*this);
        for (int i = N - 2; i >= 0; --i) sat.w[i] = kernel::or_gate(*ctx_, sat.w[i + 1], sat.w[i]);
        for (int i = 0; i < N; ++i) sat.w[i] = ctx_->not_gate(sat.w[i]);   // 1 = leading-zero region
        return sat.hamming_weight();
    }

    // base^p mod q by square-and-multiply over p's bits (q != 0 assumed).
    template <int M> UInt_T mod_exp(const UInt_T<Ctx, M>& p, const UInt_T& q) const requires (N > 0) {
        UInt_T base(*this), res = UInt_T::constant(*ctx_, 1);
        for (int i = 0; i < M; ++i) {
            UInt_T tmp = (res * base) % q;
            res = res.select(p[i], tmp);
            base = (base * base) % q;
        }
        return res;
    }

    // Reinterpret the same wires as a two's-complement signed integer (zero gates).
    Int_T<Ctx, N> as_signed() const requires (N > 0);
    Int_T<Ctx, 0> as_signed() const requires (N == 0);

    // --- width-changing views. Fixed: compile-time slice/concat/zext/trunc.
    //     Runtime: resize (zero-extend or truncate) to a runtime width. ---
    template <int Lo, int Hi> UInt_T<Ctx, Hi - Lo> slice() const requires (N > 0) {
        static_assert(0 <= Lo && Lo <= Hi && Hi <= N, "UInt_T::slice<Lo,Hi>: out of range");
        UInt_T<Ctx, Hi - Lo> r(*ctx_); for (int i = 0; i < Hi - Lo; ++i) r.w[i] = w[Lo + i]; return r;
    }
    template <int Base, int Width> UInt_T<Ctx, Width> extract() const requires (N > 0) { return slice<Base, Base + Width>(); }
    template <int M> UInt_T<Ctx, N + M> concat(const UInt_T<Ctx, M>& hi) const requires (N > 0) {
        check_same_context(*this, hi);
        UInt_T<Ctx, N + M> r(*ctx_);
        for (int i = 0; i < N; ++i) r.w[i] = w[i];
        for (int i = 0; i < M; ++i) r.w[N + i] = hi.w[i];
        return r;
    }
    template <int M> UInt_T<Ctx, M> zext() const requires (N > 0) {
        static_assert(M >= N, "UInt_T::zext<M>: M must be >= width");
        UInt_T<Ctx, M> r(*ctx_); Wire z = ctx_->public_bit(false);
        for (int i = 0; i < M; ++i) r.w[i] = (i < N) ? w[i] : z;
        return r;
    }
    template <int M> UInt_T<Ctx, M> trunc() const requires (N > 0) {
        static_assert(M <= N, "UInt_T::trunc<M>: M must be <= width");
        UInt_T<Ctx, M> r(*ctx_); for (int i = 0; i < M; ++i) r.w[i] = w[i]; return r;
    }
    // Fixed -> runtime, runtime -> fixed<M>, and runtime resize.
    UInt_T<Ctx, 0> to_dynamic() const requires (N > 0) { return UInt_T<Ctx, 0>::from_wires(*ctx_, w.data(), N); }
    template <int M> UInt_T<Ctx, M> to_fixed() const requires (N == 0) {
        static_assert(M > 0, "UInt_T::to_fixed<M>: M must be > 0");
        UInt_T<Ctx, M> r(*ctx_); Wire z = ctx_->public_bit(false);
        for (int i = 0; i < M; ++i) r.w[i] = (i < n_()) ? w[i] : z;
        return r;
    }
    UInt_T resize(int m) const requires (N == 0) {
        validate_width_(m);
        UInt_T r(*ctx_, m); Wire z = ctx_->public_bit(false);
        for (int i = 0; i < m; ++i) r.w[i] = (i < n_()) ? w[i] : z;
        return r;
    }
    UInt_T hamming_weight() const requires (N == 0) {
        int rw = kernel::bits_for(n_());
        UInt_T acc = constant(*ctx_, rw, 0);
        for (int i = 0; i < n_(); ++i) { UInt_T b = constant(*ctx_, rw, 0); b.w[0] = w[i]; acc = acc + b; }
        return acc;
    }

    // WireBundle contract (fixed width only — N == 0 models neither concept).
    static constexpr int width() requires (N > 0) { return N; }
    int width() const requires (N == 0) { return (int)w.size(); }
    void pack_wires(Wire* out) const requires (N > 0) { for (int i = 0; i < N; ++i) out[i] = w[i]; }
    // Clear codec (the WireValue half): the clear value rides a uint64_t, so it
    // exists only for N <= 64 — a wider UInt_T is a WireBundle (fine as a
    // circuit argument) but not a WireValue (use BitVec_T for typed session I/O
    // past 64 bits, or grow a limb-array clear_t here if one is ever wanted).
    static std::array<bool, (std::size_t)(N > 0 ? N : 1)> encode(uint64_t v) requires (N > 0 && N <= 64) {
        std::array<bool, (std::size_t)(N > 0 ? N : 1)> b{};
        for (int i = 0; i < N; ++i) b[(std::size_t)i] = (v >> i) & 1; return b;
    }
    static uint64_t decode(const bool* bits) requires (N > 0 && N <= 64) {
        uint64_t v = 0; for (int i = 0; i < N; ++i) v |= (uint64_t)(bits[i] ? 1 : 0) << i; return v;
    }

    // Runtime-width contract (RuntimeWidthValue; session-I/O only, never compiled).
    // The codec rides byte-bools (uint8_t 0/1) in a runtime-sized vector; sessions
    // must copy into real bool storage at the engine boundary, never reinterpret
    // uint8_t* as bool*. Caller supplies width; bits beyond 64 zero-extend (matches
    // the old input_int(width, uint64_t, owner) behavior).
    void pack_wires(Wire* out) const requires (N == 0) { for (int i = 0; i < n_(); ++i) out[i] = w[i]; }
    static std::vector<uint8_t> encode(uint64_t v, int width) requires (N == 0) {
        std::vector<uint8_t> b((std::size_t)width);
        for (int i = 0; i < width; ++i) b[(std::size_t)i] = (uint8_t)(i < 64 ? ((v >> i) & 1) : 0);
        return b;
    }
    static uint64_t decode(const uint8_t* bits, int width) requires (N == 0) {
        uint64_t v = 0; for (int i = 0; i < width && i < 64; ++i) v |= (uint64_t)(bits[i] ? 1 : 0) << i; return v;
    }

private:
    Ctx* ctx_ = nullptr;
    int n_() const { return (int)w.size(); }
    static void validate_width_(int width) { expecting(width >= 1, "UInt_T: runtime width must be >= 1"); }
    UInt_T blank_() const { UInt_T r(*ctx_); if constexpr (N == 0) r.w.resize(w.size()); return r; }
    void same_width_(const UInt_T& o) const {
        if constexpr (N == 0)
            expecting(w.size() == o.w.size(), "UInt_T: operands have different runtime widths");
    }
};

}  // namespace emp
#endif  // EMP_CIRCUIT_UNSIGNED_INT_H__
