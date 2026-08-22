#ifndef EMP_CIRCUIT_UNSIGNED_INT_H__
#define EMP_CIRCUIT_UNSIGNED_INT_H__

// Fixed- and runtime-width unsigned integers over a BooleanContext.
// UInt_T<Ctx,N> has a positive compile-time width and inline wire storage.
// DynamicUInt_T<Ctx> has a positive runtime width and vector storage. Both use
// the same runtime-sized numeric kernels.

#include "emp-tool/circuits/bit.h"
#include "emp-tool/circuits/numeric_kernels.h"
#include "emp-tool/runtime/core/utils.h"
#include <array>
#include <cstdint>
#include <vector>

namespace emp {

template <BooleanContext Ctx, int N> requires (N > 0) class Int_T;
template <BooleanContext Ctx> class DynamicInt_T;
template <BooleanContext Ctx> class DynamicUInt_T;

template <BooleanContext Ctx, int N> requires (N > 0)
class UInt_T {
public:
    using Wire         = typename Ctx::Wire;
    using context_type = Ctx;
    using clear_t      = uint64_t;
    template <BooleanContext C2> using rebind = UInt_T<C2, N>;
    std::array<Wire, (std::size_t)N> w{};

    UInt_T() = default;
    explicit UInt_T(Ctx& c) : ctx_(&c) {}

    static UInt_T constant(Ctx& c, uint64_t v) {
        UInt_T r(c);
        for (int i = 0; i < N; ++i)
            r.w[(std::size_t)i] = c.public_bit(i < 64 ? (v >> i) & 1 : 0);
        return r;
    }
    static UInt_T from_wires(Ctx& c, const Wire* in) {
        UInt_T r(c);
        for (int i = 0; i < N; ++i) r.w[(std::size_t)i] = in[i];
        return r;
    }

    Ctx* context() const { return ctx_; }
    const Wire* data() const { return w.data(); }
    Wire* data() { return w.data(); }
    UInt_T constant(uint64_t v) const { return constant(*ctx_, v); }
    Bit_T<Ctx> operator[](int i) const { return Bit_T<Ctx>(*ctx_, w[(std::size_t)i]); }

    UInt_T operator+(const UInt_T& o) const { check_same_context(*this, o); UInt_T r(*ctx_); kernel::ripple_add<Ctx>(*ctx_, w.data(), o.w.data(), r.w.data(), N); return r; }
    UInt_T operator-(const UInt_T& o) const { check_same_context(*this, o); UInt_T r(*ctx_); kernel::ripple_sub<Ctx>(*ctx_, w.data(), o.w.data(), r.w.data(), N); return r; }
    UInt_T operator&(const UInt_T& o) const { check_same_context(*this, o); UInt_T r(*ctx_); for (int i = 0; i < N; ++i) r.w[(std::size_t)i] = ctx_->and_gate(w[(std::size_t)i], o.w[(std::size_t)i]); return r; }
    UInt_T operator^(const UInt_T& o) const { check_same_context(*this, o); UInt_T r(*ctx_); for (int i = 0; i < N; ++i) r.w[(std::size_t)i] = ctx_->xor_gate(w[(std::size_t)i], o.w[(std::size_t)i]); return r; }
    UInt_T operator|(const UInt_T& o) const { check_same_context(*this, o); UInt_T r(*ctx_); for (int i = 0; i < N; ++i) r.w[(std::size_t)i] = kernel::or_gate(*ctx_, w[(std::size_t)i], o.w[(std::size_t)i]); return r; }
    UInt_T operator~() const                { UInt_T r(*ctx_); for (int i = 0; i < N; ++i) r.w[(std::size_t)i] = ctx_->not_gate(w[(std::size_t)i]); return r; }

    UInt_T operator*(const UInt_T& o) const { check_same_context(*this, o); UInt_T r(*ctx_); kernel::mul_full<Ctx>(*ctx_, r.w.data(), w.data(), o.w.data(), N); return r; }
    UInt_T operator/(const UInt_T& o) const { check_same_context(*this, o); UInt_T r(*ctx_); kernel::div_full<Ctx>(*ctx_, r.w.data(), nullptr, w.data(), o.w.data(), N); return r; }
    UInt_T operator%(const UInt_T& o) const { check_same_context(*this, o); UInt_T r(*ctx_); kernel::div_full<Ctx>(*ctx_, nullptr, r.w.data(), w.data(), o.w.data(), N); return r; }

    Bit_T<Ctx> operator==(const UInt_T& o) const { check_same_context(*this, o); return Bit_T<Ctx>(*ctx_, kernel::equal<Ctx>(*ctx_, w.data(), o.w.data(), N)); }
    Bit_T<Ctx> operator!=(const UInt_T& o) const { return !(*this == o); }
    Bit_T<Ctx> operator<(const UInt_T& o)  const { check_same_context(*this, o); return Bit_T<Ctx>(*ctx_, kernel::less_than<Ctx>(*ctx_, w.data(), o.w.data(), N)); }
    Bit_T<Ctx> operator>(const UInt_T& o)  const { return o < *this; }
    Bit_T<Ctx> operator<=(const UInt_T& o) const { return !(*this > o); }
    Bit_T<Ctx> operator>=(const UInt_T& o) const { return !(*this < o); }

    UInt_T select(const Bit_T<Ctx>& sel, const UInt_T& t) const {
        check_same_context(*this, sel); check_same_context(*this, t);
        UInt_T r(*ctx_);
        for (int i = 0; i < N; ++i)
            r.w[(std::size_t)i] = kernel::mux(*ctx_, sel.w, t.w[(std::size_t)i], w[(std::size_t)i]);
        return r;
    }

    UInt_T operator<<(int s) const {
        expecting(s >= 0, "UInt_T::operator<<: shift amount must be >= 0");
        UInt_T r(*ctx_); Wire z = ctx_->public_bit(false);
        for (int i = 0; i < N; ++i) r.w[(std::size_t)i] = (i >= s) ? w[(std::size_t)(i - s)] : z;
        return r;
    }
    UInt_T operator>>(int s) const {
        expecting(s >= 0, "UInt_T::operator>>: shift amount must be >= 0");
        if (s > N) s = N;
        UInt_T r(*ctx_); Wire z = ctx_->public_bit(false);
        for (int i = 0; i < N; ++i) r.w[(std::size_t)i] = (i + s < N) ? w[(std::size_t)(i + s)] : z;
        return r;
    }
    UInt_T rotl(int s) const {
        UInt_T r(*ctx_); s = ((s % N) + N) % N;
        for (int i = 0; i < N; ++i) r.w[(std::size_t)i] = w[(std::size_t)(((i - s) % N + N) % N)];
        return r;
    }
    UInt_T rotr(int s) const {
        UInt_T r(*ctx_); s = ((s % N) + N) % N;
        for (int i = 0; i < N; ++i) r.w[(std::size_t)i] = w[(std::size_t)((i + s) % N)];
        return r;
    }

    UInt_T operator<<(const UInt_T& shamt) const {
        check_same_context(*this, shamt);
        UInt_T res(*this);
        constexpr int use = (N <= 1) ? 0 : kernel::clog2_ceil(N);
        for (int i = 0; i < use; ++i) res = res.select(shamt[i], res << (1 << i));
        Bit_T<Ctx> overflow = Bit_T<Ctx>::constant(*ctx_, false);
        for (int i = use; i < N; ++i) overflow = overflow | shamt[i];
        return res.select(overflow, UInt_T::constant(*ctx_, 0));
    }
    UInt_T operator>>(const UInt_T& shamt) const {
        check_same_context(*this, shamt);
        UInt_T res(*this);
        constexpr int use = (N <= 1) ? 0 : kernel::clog2_ceil(N);
        for (int i = 0; i < use; ++i) res = res.select(shamt[i], res >> (1 << i));
        Bit_T<Ctx> overflow = Bit_T<Ctx>::constant(*ctx_, false);
        for (int i = use; i < N; ++i) overflow = overflow | shamt[i];
        return res.select(overflow, UInt_T::constant(*ctx_, 0));
    }

    template <int R> requires (R >= kernel::bits_for(N))
    UInt_T<Ctx, R> popcount() const {
        UInt_T<Ctx, R> acc = UInt_T<Ctx, R>::constant(*ctx_, 0);
        acc.w[0] = w[0];
        for (int i = 1; i < N; ++i) {
            UInt_T<Ctx, R> b = UInt_T<Ctx, R>::constant(*ctx_, 0);
            b.w[0] = w[(std::size_t)i];
            acc = acc + b;
        }
        return acc;
    }
    UInt_T<Ctx, kernel::bits_for(N)> hamming_weight() const { return popcount<kernel::bits_for(N)>(); }
    UInt_T<Ctx, kernel::bits_for(N)> leading_zeros() const {
        UInt_T sat(*this);
        for (int i = N - 2; i >= 0; --i)
            sat.w[(std::size_t)i] = kernel::or_gate(*ctx_, sat.w[(std::size_t)(i + 1)], sat.w[(std::size_t)i]);
        for (int i = 0; i < N; ++i) sat.w[(std::size_t)i] = ctx_->not_gate(sat.w[(std::size_t)i]);
        return sat.hamming_weight();
    }

    Int_T<Ctx, N> as_signed() const;
    DynamicUInt_T<Ctx> to_dynamic() const;

    template <int Lo, int Hi>
    auto slice() const {
        static_assert(0 <= Lo && Lo < Hi && Hi <= N, "UInt_T::slice<Lo,Hi>: requires 0 <= Lo < Hi <= width");
        UInt_T<Ctx, Hi - Lo> r(*ctx_);
        for (int i = 0; i < Hi - Lo; ++i) r.w[(std::size_t)i] = w[(std::size_t)(Lo + i)];
        return r;
    }
    template <int Base, int Width>
    auto extract() const {
        static_assert(Width > 0, "UInt_T::extract<Base,Width>: Width must be > 0");
        return slice<Base, Base + Width>();
    }
    template <int M> UInt_T<Ctx, N + M> concat(const UInt_T<Ctx, M>& hi) const {
        check_same_context(*this, hi);
        UInt_T<Ctx, N + M> r(*ctx_);
        for (int i = 0; i < N; ++i) r.w[(std::size_t)i] = w[(std::size_t)i];
        for (int i = 0; i < M; ++i) r.w[(std::size_t)(N + i)] = hi.w[(std::size_t)i];
        return r;
    }
    template <int M>
    auto zext() const {
        static_assert(M >= N, "UInt_T::zext<M>: M must be >= width");
        UInt_T<Ctx, M> r(*ctx_); Wire z = ctx_->public_bit(false);
        for (int i = 0; i < M; ++i) r.w[(std::size_t)i] = (i < N) ? w[(std::size_t)i] : z;
        return r;
    }
    template <int M>
    auto trunc() const {
        static_assert(0 < M && M <= N, "UInt_T::trunc<M>: requires 0 < M <= width");
        UInt_T<Ctx, M> r(*ctx_);
        for (int i = 0; i < M; ++i) r.w[(std::size_t)i] = w[(std::size_t)i];
        return r;
    }

    static constexpr int width() { return N; }
    void pack_wires(Wire* out) const { for (int i = 0; i < N; ++i) out[i] = w[(std::size_t)i]; }
    static std::array<bool, (std::size_t)N> encode(uint64_t v) requires (N <= 64) {
        std::array<bool, (std::size_t)N> b{};
        for (int i = 0; i < N; ++i) b[(std::size_t)i] = (v >> i) & 1;
        return b;
    }
    static uint64_t decode(const bool* bits) requires (N <= 64) {
        uint64_t v = 0;
        for (int i = 0; i < N; ++i) v |= (uint64_t)(bits[i] ? 1 : 0) << i;
        return v;
    }

private:
    Ctx* ctx_ = nullptr;
};

template <BooleanContext Ctx>
class DynamicUInt_T {
public:
    using Wire         = typename Ctx::Wire;
    using context_type = Ctx;
    using clear_t      = uint64_t;
    template <BooleanContext C2> using rebind = DynamicUInt_T<C2>;
    static constexpr bool is_dynamic = true;

    // Unbound assignment slot; the width-taking constructor allocates scratch.
    DynamicUInt_T() = default;
    DynamicUInt_T(Ctx& c, int width) : ctx_(&c), w_(checked_size_(width)) {}

    static DynamicUInt_T constant(Ctx& c, int width, uint64_t v) {
        DynamicUInt_T r(c, width);
        for (int i = 0; i < width; ++i)
            r.w_[(std::size_t)i] = c.public_bit(i < 64 ? (v >> i) & 1 : 0);
        return r;
    }
    static DynamicUInt_T from_wires(Ctx& c, const Wire* in, int width) {
        DynamicUInt_T r(c, width);
        for (int i = 0; i < width; ++i) r.w_[(std::size_t)i] = in[i];
        return r;
    }

    Ctx* context() const { return ctx_; }
    int width() const { return (int)w_.size(); }
    const Wire* data() const { return w_.data(); }
    Wire* data() { return w_.data(); }
    DynamicUInt_T constant(uint64_t v) const { return constant(*ctx_, width(), v); }
    Bit_T<Ctx> operator[](int i) const { return Bit_T<Ctx>(*ctx_, w_[(std::size_t)i]); }

    DynamicUInt_T operator+(const DynamicUInt_T& o) const { check_operand_(o); DynamicUInt_T r(*ctx_, width()); kernel::ripple_add<Ctx>(*ctx_, data(), o.data(), r.data(), width()); return r; }
    DynamicUInt_T operator-(const DynamicUInt_T& o) const { check_operand_(o); DynamicUInt_T r(*ctx_, width()); kernel::ripple_sub<Ctx>(*ctx_, data(), o.data(), r.data(), width()); return r; }
    DynamicUInt_T operator&(const DynamicUInt_T& o) const { check_operand_(o); DynamicUInt_T r(*ctx_, width()); for (int i = 0; i < width(); ++i) r.w_[(std::size_t)i] = ctx_->and_gate(w_[(std::size_t)i], o.w_[(std::size_t)i]); return r; }
    DynamicUInt_T operator^(const DynamicUInt_T& o) const { check_operand_(o); DynamicUInt_T r(*ctx_, width()); for (int i = 0; i < width(); ++i) r.w_[(std::size_t)i] = ctx_->xor_gate(w_[(std::size_t)i], o.w_[(std::size_t)i]); return r; }
    DynamicUInt_T operator|(const DynamicUInt_T& o) const { check_operand_(o); DynamicUInt_T r(*ctx_, width()); for (int i = 0; i < width(); ++i) r.w_[(std::size_t)i] = kernel::or_gate(*ctx_, w_[(std::size_t)i], o.w_[(std::size_t)i]); return r; }
    DynamicUInt_T operator~() const { DynamicUInt_T r(*ctx_, width()); for (int i = 0; i < width(); ++i) r.w_[(std::size_t)i] = ctx_->not_gate(w_[(std::size_t)i]); return r; }

    DynamicUInt_T operator*(const DynamicUInt_T& o) const { check_operand_(o); DynamicUInt_T r(*ctx_, width()); kernel::mul_full<Ctx>(*ctx_, r.data(), data(), o.data(), width()); return r; }
    DynamicUInt_T operator/(const DynamicUInt_T& o) const { check_operand_(o); DynamicUInt_T r(*ctx_, width()); kernel::div_full<Ctx>(*ctx_, r.data(), nullptr, data(), o.data(), width()); return r; }
    DynamicUInt_T operator%(const DynamicUInt_T& o) const { check_operand_(o); DynamicUInt_T r(*ctx_, width()); kernel::div_full<Ctx>(*ctx_, nullptr, r.data(), data(), o.data(), width()); return r; }

    Bit_T<Ctx> operator==(const DynamicUInt_T& o) const { check_operand_(o); return Bit_T<Ctx>(*ctx_, kernel::equal<Ctx>(*ctx_, data(), o.data(), width())); }
    Bit_T<Ctx> operator!=(const DynamicUInt_T& o) const { return !(*this == o); }
    Bit_T<Ctx> operator<(const DynamicUInt_T& o) const { check_operand_(o); return Bit_T<Ctx>(*ctx_, kernel::less_than<Ctx>(*ctx_, data(), o.data(), width())); }
    Bit_T<Ctx> operator>(const DynamicUInt_T& o) const { return o < *this; }
    Bit_T<Ctx> operator<=(const DynamicUInt_T& o) const { return !(*this > o); }
    Bit_T<Ctx> operator>=(const DynamicUInt_T& o) const { return !(*this < o); }

    DynamicUInt_T select(const Bit_T<Ctx>& sel, const DynamicUInt_T& t) const {
        check_same_context(*this, sel); check_operand_(t);
        DynamicUInt_T r(*ctx_, width());
        for (int i = 0; i < width(); ++i)
            r.w_[(std::size_t)i] = kernel::mux(*ctx_, sel.w, t.w_[(std::size_t)i], w_[(std::size_t)i]);
        return r;
    }

    DynamicUInt_T operator<<(int s) const {
        expecting(s >= 0, "DynamicUInt_T::operator<<: shift amount must be >= 0");
        DynamicUInt_T r(*ctx_, width()); Wire z = ctx_->public_bit(false);
        for (int i = 0; i < width(); ++i) r.w_[(std::size_t)i] = (i >= s) ? w_[(std::size_t)(i - s)] : z;
        return r;
    }
    DynamicUInt_T operator>>(int s) const {
        expecting(s >= 0, "DynamicUInt_T::operator>>: shift amount must be >= 0");
        if (s > width()) s = width();
        DynamicUInt_T r(*ctx_, width()); Wire z = ctx_->public_bit(false);
        for (int i = 0; i < width(); ++i) r.w_[(std::size_t)i] = (i + s < width()) ? w_[(std::size_t)(i + s)] : z;
        return r;
    }
    DynamicUInt_T rotl(int s) const {
        const int n = width(); DynamicUInt_T r(*ctx_, n); s = ((s % n) + n) % n;
        for (int i = 0; i < n; ++i) r.w_[(std::size_t)i] = w_[(std::size_t)(((i - s) % n + n) % n)];
        return r;
    }
    DynamicUInt_T rotr(int s) const {
        const int n = width(); DynamicUInt_T r(*ctx_, n); s = ((s % n) + n) % n;
        for (int i = 0; i < n; ++i) r.w_[(std::size_t)i] = w_[(std::size_t)((i + s) % n)];
        return r;
    }

    DynamicUInt_T resize(int new_width) const {
        DynamicUInt_T r(*ctx_, new_width); Wire z = ctx_->public_bit(false);
        for (int i = 0; i < new_width; ++i) r.w_[(std::size_t)i] = (i < width()) ? w_[(std::size_t)i] : z;
        return r;
    }
    DynamicUInt_T hamming_weight() const {
        const int result_width = kernel::bits_for(width());
        DynamicUInt_T acc = constant(*ctx_, result_width, 0);
        acc.w_[0] = w_[0];
        for (int i = 1; i < width(); ++i) {
            DynamicUInt_T bit = constant(*ctx_, result_width, 0);
            bit.w_[0] = w_[(std::size_t)i];
            acc = acc + bit;
        }
        return acc;
    }
    template <int M> requires (M > 0)
    UInt_T<Ctx, M> to_fixed() const {
        UInt_T<Ctx, M> r(*ctx_); Wire z = ctx_->public_bit(false);
        for (int i = 0; i < M; ++i) r.w[(std::size_t)i] = (i < width()) ? w_[(std::size_t)i] : z;
        return r;
    }
    DynamicInt_T<Ctx> as_signed() const;

    void pack_wires(Wire* out) const { for (int i = 0; i < width(); ++i) out[i] = w_[(std::size_t)i]; }
    static std::vector<uint8_t> encode(uint64_t v, int width) {
        const std::size_t n = checked_size_(width);
        std::vector<uint8_t> b(n);
        for (int i = 0; i < width; ++i) b[(std::size_t)i] = (uint8_t)(i < 64 ? ((v >> i) & 1) : 0);
        return b;
    }
    static uint64_t decode(const uint8_t* bits, int width) {
        (void)checked_size_(width);
        uint64_t v = 0;
        for (int i = 0; i < width && i < 64; ++i) v |= (uint64_t)(bits[i] ? 1 : 0) << i;
        return v;
    }

private:
    Ctx* ctx_ = nullptr;
    std::vector<Wire> w_;

    static std::size_t checked_size_(int width) {
        expecting(width >= 1, "DynamicUInt_T: width must be >= 1");
        return (std::size_t)width;
    }
    void check_operand_(const DynamicUInt_T& o) const {
        check_same_context(*this, o);
        expecting(width() == o.width(), "DynamicUInt_T: operands have different widths");
    }
};

template <BooleanContext Ctx, int N> requires (N > 0)
inline DynamicUInt_T<Ctx> UInt_T<Ctx, N>::to_dynamic() const {
    return DynamicUInt_T<Ctx>::from_wires(*ctx_, w.data(), N);
}

}  // namespace emp
#endif  // EMP_CIRCUIT_UNSIGNED_INT_H__
