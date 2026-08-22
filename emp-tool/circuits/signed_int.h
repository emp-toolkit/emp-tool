#ifndef EMP_CIRCUIT_SIGNED_INT_H__
#define EMP_CIRCUIT_SIGNED_INT_H__

// Fixed- and runtime-width two's-complement integers over a BooleanContext.
// Int_T<Ctx,N> has a positive compile-time width; DynamicInt_T<Ctx> has a
// positive runtime width. Both wrap modulo 2^width and divide with truncation
// toward zero.

#include "emp-tool/circuits/bit.h"
#include "emp-tool/circuits/unsigned_int.h"
#include "emp-tool/circuits/numeric_kernels.h"
#include "emp-tool/runtime/core/utils.h"
#include <array>
#include <cstdint>
#include <vector>

namespace emp {

template <BooleanContext Ctx, int N> requires (N > 0)
class Int_T {
public:
    using Wire         = typename Ctx::Wire;
    using context_type = Ctx;
    using clear_t      = int64_t;
    template <BooleanContext C2> using rebind = Int_T<C2, N>;
    std::array<Wire, (std::size_t)N> w{};

    Int_T() = default;
    explicit Int_T(Ctx& c) : ctx_(&c) {}

    static Int_T constant(Ctx& c, int64_t v) {
        const uint64_t u = (uint64_t)v;
        Int_T r(c);
        for (int i = 0; i < N; ++i)
            r.w[(std::size_t)i] = c.public_bit((u >> (i < 64 ? i : 63)) & 1);
        return r;
    }
    static Int_T from_wires(Ctx& c, const Wire* in) {
        Int_T r(c);
        for (int i = 0; i < N; ++i) r.w[(std::size_t)i] = in[i];
        return r;
    }

    Ctx* context() const { return ctx_; }
    const Wire* data() const { return w.data(); }
    Wire* data() { return w.data(); }
    Int_T constant(int64_t v) const { return constant(*ctx_, v); }
    Bit_T<Ctx> operator[](int i) const { return Bit_T<Ctx>(*ctx_, w[(std::size_t)i]); }

    Int_T operator+(const Int_T& o) const { check_same_context(*this, o); Int_T r(*ctx_); kernel::ripple_add<Ctx>(*ctx_, data(), o.data(), r.data(), N); return r; }
    Int_T operator-(const Int_T& o) const { check_same_context(*this, o); Int_T r(*ctx_); kernel::ripple_sub<Ctx>(*ctx_, data(), o.data(), r.data(), N); return r; }
    Int_T operator-() const { return zeros_() - *this; }

    Bit_T<Ctx> operator<(const Int_T& o) const {
        check_same_context(*this, o);
        Wire ub = kernel::less_than<Ctx>(*ctx_, data(), o.data(), N);
        Wire sa = w[(std::size_t)(N - 1)], sb = o.w[(std::size_t)(N - 1)];
        Wire diff = ctx_->xor_gate(sa, sb);
        return Bit_T<Ctx>(*ctx_, kernel::mux(*ctx_, diff, sa, ub));
    }
    Bit_T<Ctx> operator>(const Int_T& o) const { return o < *this; }
    Bit_T<Ctx> operator==(const Int_T& o) const { check_same_context(*this, o); return Bit_T<Ctx>(*ctx_, kernel::equal<Ctx>(*ctx_, data(), o.data(), N)); }
    Bit_T<Ctx> operator!=(const Int_T& o) const { return !(*this == o); }
    Bit_T<Ctx> operator<=(const Int_T& o) const { return !(*this > o); }
    Bit_T<Ctx> operator>=(const Int_T& o) const { return !(*this < o); }

    Int_T operator*(const Int_T& o) const { check_same_context(*this, o); Int_T r(*ctx_); kernel::mul_full<Ctx>(*ctx_, r.data(), data(), o.data(), N); return r; }
    Int_T operator&(const Int_T& o) const { check_same_context(*this, o); Int_T r(*ctx_); for (int i = 0; i < N; ++i) r.w[(std::size_t)i] = ctx_->and_gate(w[(std::size_t)i], o.w[(std::size_t)i]); return r; }
    Int_T operator^(const Int_T& o) const { check_same_context(*this, o); Int_T r(*ctx_); for (int i = 0; i < N; ++i) r.w[(std::size_t)i] = ctx_->xor_gate(w[(std::size_t)i], o.w[(std::size_t)i]); return r; }
    Int_T operator|(const Int_T& o) const { check_same_context(*this, o); Int_T r(*ctx_); for (int i = 0; i < N; ++i) r.w[(std::size_t)i] = kernel::or_gate(*ctx_, w[(std::size_t)i], o.w[(std::size_t)i]); return r; }
    Int_T operator~() const               { Int_T r(*ctx_); for (int i = 0; i < N; ++i) r.w[(std::size_t)i] = ctx_->not_gate(w[(std::size_t)i]); return r; }

    Int_T operator/(const Int_T& o) const {
        check_same_context(*this, o);
        Bit_T<Ctx> sa = (*this)[N - 1], sb = o[N - 1];
        Int_T ua = this->select(sa, -*this), ub = o.select(sb, -o);
        Int_T uq(*ctx_); kernel::div_full<Ctx>(*ctx_, uq.data(), nullptr, ua.data(), ub.data(), N);
        return uq.select(sa != sb, -uq);
    }
    Int_T operator%(const Int_T& o) const {
        check_same_context(*this, o);
        Bit_T<Ctx> sa = (*this)[N - 1], sb = o[N - 1];
        Int_T ua = this->select(sa, -*this), ub = o.select(sb, -o);
        Int_T ur(*ctx_); kernel::div_full<Ctx>(*ctx_, nullptr, ur.data(), ua.data(), ub.data(), N);
        return ur.select(sa, -ur);
    }

    Int_T select(const Bit_T<Ctx>& sel, const Int_T& t) const {
        check_same_context(*this, sel); check_same_context(*this, t);
        Int_T r(*ctx_);
        for (int i = 0; i < N; ++i)
            r.w[(std::size_t)i] = kernel::mux(*ctx_, sel.w, t.w[(std::size_t)i], w[(std::size_t)i]);
        return r;
    }

    UInt_T<Ctx, N> as_unsigned() const { return UInt_T<Ctx, N>::from_wires(*ctx_, data()); }
    DynamicInt_T<Ctx> to_dynamic() const;

    Int_T operator<<(int s) const {
        expecting(s >= 0, "Int_T::operator<<: shift amount must be >= 0");
        Int_T r(*ctx_); Wire z = ctx_->public_bit(false);
        for (int i = 0; i < N; ++i) r.w[(std::size_t)i] = (i >= s) ? w[(std::size_t)(i - s)] : z;
        return r;
    }
    Int_T operator>>(int s) const {
        expecting(s >= 0, "Int_T::operator>>: shift amount must be >= 0");
        if (s > N) s = N;
        Int_T r(*ctx_); Wire sign = w[(std::size_t)(N - 1)];
        for (int i = 0; i < N; ++i) r.w[(std::size_t)i] = (i + s < N) ? w[(std::size_t)(i + s)] : sign;
        return r;
    }

    Int_T operator<<(const UInt_T<Ctx, N>& shamt) const {
        check_same_context(*this, shamt);
        Int_T res(*this);
        constexpr int use = (N <= 1) ? 0 : kernel::clog2_ceil(N);
        for (int i = 0; i < use; ++i) res = res.select(shamt[i], res << (1 << i));
        Bit_T<Ctx> overflow = Bit_T<Ctx>::constant(*ctx_, false);
        for (int i = use; i < N; ++i) overflow = overflow | shamt[i];
        return res.select(overflow, Int_T::constant(*ctx_, 0));
    }
    Int_T operator>>(const UInt_T<Ctx, N>& shamt) const {
        check_same_context(*this, shamt);
        Int_T res(*this);
        constexpr int use = (N <= 1) ? 0 : kernel::clog2_ceil(N);
        for (int i = 0; i < use; ++i) res = res.select(shamt[i], res >> (1 << i));
        Bit_T<Ctx> overflow = Bit_T<Ctx>::constant(*ctx_, false);
        for (int i = use; i < N; ++i) overflow = overflow | shamt[i];
        Int_T allsign(*ctx_);
        for (int i = 0; i < N; ++i) allsign.w[(std::size_t)i] = w[(std::size_t)(N - 1)];
        return res.select(overflow, allsign);
    }

    template <int M>
    auto sext() const {
        static_assert(M >= N, "Int_T::sext<M>: M must be >= width");
        Int_T<Ctx, M> r(*ctx_);
        for (int i = 0; i < M; ++i) r.w[(std::size_t)i] = (i < N) ? w[(std::size_t)i] : w[(std::size_t)(N - 1)];
        return r;
    }
    template <int M>
    auto trunc() const {
        static_assert(0 < M && M <= N, "Int_T::trunc<M>: requires 0 < M <= width");
        Int_T<Ctx, M> r(*ctx_);
        for (int i = 0; i < M; ++i) r.w[(std::size_t)i] = w[(std::size_t)i];
        return r;
    }

    static constexpr int width() { return N; }
    void pack_wires(Wire* out) const { for (int i = 0; i < N; ++i) out[i] = w[(std::size_t)i]; }
    static std::array<bool, (std::size_t)N> encode(int64_t v) requires (N <= 64) {
        const uint64_t u = (uint64_t)v;
        std::array<bool, (std::size_t)N> b{};
        for (int i = 0; i < N; ++i) b[(std::size_t)i] = (u >> i) & 1;
        return b;
    }
    static int64_t decode(const bool* bits) requires (N <= 64) {
        uint64_t v = 0;
        for (int i = 0; i < N; ++i) v |= (uint64_t)(bits[i] ? 1 : 0) << i;
        if constexpr (N < 64) {
            if ((v >> (N - 1)) & 1) v |= ~((uint64_t(1) << N) - 1);
        }
        return (int64_t)v;
    }

private:
    Ctx* ctx_ = nullptr;
    Int_T zeros_() const {
        Int_T r(*ctx_); Wire z = ctx_->public_bit(false);
        for (int i = 0; i < N; ++i) r.w[(std::size_t)i] = z;
        return r;
    }
};

template <BooleanContext Ctx>
class DynamicInt_T {
public:
    using Wire         = typename Ctx::Wire;
    using context_type = Ctx;
    using clear_t      = int64_t;
    template <BooleanContext C2> using rebind = DynamicInt_T<C2>;
    static constexpr bool is_dynamic = true;

    // Unbound assignment slot; the width-taking constructor allocates scratch.
    DynamicInt_T() = default;
    DynamicInt_T(Ctx& c, int width) : ctx_(&c), w_(checked_size_(width)) {}

    static DynamicInt_T constant(Ctx& c, int width, int64_t v) {
        const uint64_t u = (uint64_t)v;
        DynamicInt_T r(c, width);
        for (int i = 0; i < width; ++i)
            r.w_[(std::size_t)i] = c.public_bit((u >> (i < 64 ? i : 63)) & 1);
        return r;
    }
    static DynamicInt_T from_wires(Ctx& c, const Wire* in, int width) {
        DynamicInt_T r(c, width);
        for (int i = 0; i < width; ++i) r.w_[(std::size_t)i] = in[i];
        return r;
    }

    Ctx* context() const { return ctx_; }
    int width() const { return (int)w_.size(); }
    const Wire* data() const { return w_.data(); }
    Wire* data() { return w_.data(); }
    DynamicInt_T constant(int64_t v) const { return constant(*ctx_, width(), v); }
    Bit_T<Ctx> operator[](int i) const { return Bit_T<Ctx>(*ctx_, w_[(std::size_t)i]); }

    DynamicInt_T operator+(const DynamicInt_T& o) const { check_operand_(o); DynamicInt_T r(*ctx_, width()); kernel::ripple_add<Ctx>(*ctx_, data(), o.data(), r.data(), width()); return r; }
    DynamicInt_T operator-(const DynamicInt_T& o) const { check_operand_(o); DynamicInt_T r(*ctx_, width()); kernel::ripple_sub<Ctx>(*ctx_, data(), o.data(), r.data(), width()); return r; }
    DynamicInt_T operator-() const { return zeros_() - *this; }

    Bit_T<Ctx> operator<(const DynamicInt_T& o) const {
        check_operand_(o);
        Wire ub = kernel::less_than<Ctx>(*ctx_, data(), o.data(), width());
        Wire sa = w_.back(), sb = o.w_.back();
        Wire diff = ctx_->xor_gate(sa, sb);
        return Bit_T<Ctx>(*ctx_, kernel::mux(*ctx_, diff, sa, ub));
    }
    Bit_T<Ctx> operator>(const DynamicInt_T& o) const { return o < *this; }
    Bit_T<Ctx> operator==(const DynamicInt_T& o) const { check_operand_(o); return Bit_T<Ctx>(*ctx_, kernel::equal<Ctx>(*ctx_, data(), o.data(), width())); }
    Bit_T<Ctx> operator!=(const DynamicInt_T& o) const { return !(*this == o); }
    Bit_T<Ctx> operator<=(const DynamicInt_T& o) const { return !(*this > o); }
    Bit_T<Ctx> operator>=(const DynamicInt_T& o) const { return !(*this < o); }

    DynamicInt_T operator*(const DynamicInt_T& o) const { check_operand_(o); DynamicInt_T r(*ctx_, width()); kernel::mul_full<Ctx>(*ctx_, r.data(), data(), o.data(), width()); return r; }
    DynamicInt_T operator&(const DynamicInt_T& o) const { check_operand_(o); DynamicInt_T r(*ctx_, width()); for (int i = 0; i < width(); ++i) r.w_[(std::size_t)i] = ctx_->and_gate(w_[(std::size_t)i], o.w_[(std::size_t)i]); return r; }
    DynamicInt_T operator^(const DynamicInt_T& o) const { check_operand_(o); DynamicInt_T r(*ctx_, width()); for (int i = 0; i < width(); ++i) r.w_[(std::size_t)i] = ctx_->xor_gate(w_[(std::size_t)i], o.w_[(std::size_t)i]); return r; }
    DynamicInt_T operator|(const DynamicInt_T& o) const { check_operand_(o); DynamicInt_T r(*ctx_, width()); for (int i = 0; i < width(); ++i) r.w_[(std::size_t)i] = kernel::or_gate(*ctx_, w_[(std::size_t)i], o.w_[(std::size_t)i]); return r; }
    DynamicInt_T operator~() const { DynamicInt_T r(*ctx_, width()); for (int i = 0; i < width(); ++i) r.w_[(std::size_t)i] = ctx_->not_gate(w_[(std::size_t)i]); return r; }

    DynamicInt_T operator/(const DynamicInt_T& o) const {
        check_operand_(o);
        Bit_T<Ctx> sa = (*this)[width() - 1], sb = o[width() - 1];
        DynamicInt_T ua = this->select(sa, -*this), ub = o.select(sb, -o);
        DynamicInt_T uq(*ctx_, width()); kernel::div_full<Ctx>(*ctx_, uq.data(), nullptr, ua.data(), ub.data(), width());
        return uq.select(sa != sb, -uq);
    }
    DynamicInt_T operator%(const DynamicInt_T& o) const {
        check_operand_(o);
        Bit_T<Ctx> sa = (*this)[width() - 1], sb = o[width() - 1];
        DynamicInt_T ua = this->select(sa, -*this), ub = o.select(sb, -o);
        DynamicInt_T ur(*ctx_, width()); kernel::div_full<Ctx>(*ctx_, nullptr, ur.data(), ua.data(), ub.data(), width());
        return ur.select(sa, -ur);
    }

    DynamicInt_T select(const Bit_T<Ctx>& sel, const DynamicInt_T& t) const {
        check_same_context(*this, sel); check_operand_(t);
        DynamicInt_T r(*ctx_, width());
        for (int i = 0; i < width(); ++i)
            r.w_[(std::size_t)i] = kernel::mux(*ctx_, sel.w, t.w_[(std::size_t)i], w_[(std::size_t)i]);
        return r;
    }

    DynamicUInt_T<Ctx> as_unsigned() const { return DynamicUInt_T<Ctx>::from_wires(*ctx_, data(), width()); }

    DynamicInt_T operator<<(int s) const {
        expecting(s >= 0, "DynamicInt_T::operator<<: shift amount must be >= 0");
        DynamicInt_T r(*ctx_, width()); Wire z = ctx_->public_bit(false);
        for (int i = 0; i < width(); ++i) r.w_[(std::size_t)i] = (i >= s) ? w_[(std::size_t)(i - s)] : z;
        return r;
    }
    DynamicInt_T operator>>(int s) const {
        expecting(s >= 0, "DynamicInt_T::operator>>: shift amount must be >= 0");
        if (s > width()) s = width();
        DynamicInt_T r(*ctx_, width()); Wire sign = w_.back();
        for (int i = 0; i < width(); ++i) r.w_[(std::size_t)i] = (i + s < width()) ? w_[(std::size_t)(i + s)] : sign;
        return r;
    }

    DynamicInt_T resize(int new_width) const {
        DynamicInt_T r(*ctx_, new_width); Wire sign = w_.back();
        for (int i = 0; i < new_width; ++i) r.w_[(std::size_t)i] = (i < width()) ? w_[(std::size_t)i] : sign;
        return r;
    }
    template <int M> requires (M > 0)
    Int_T<Ctx, M> to_fixed() const {
        Int_T<Ctx, M> r(*ctx_); Wire sign = w_.back();
        for (int i = 0; i < M; ++i) r.w[(std::size_t)i] = (i < width()) ? w_[(std::size_t)i] : sign;
        return r;
    }

    void pack_wires(Wire* out) const { for (int i = 0; i < width(); ++i) out[i] = w_[(std::size_t)i]; }
    static std::vector<uint8_t> encode(int64_t v, int width) {
        const uint64_t u = (uint64_t)v;
        const std::size_t n = checked_size_(width);
        std::vector<uint8_t> b(n);
        for (int i = 0; i < width; ++i)
            b[(std::size_t)i] = (uint8_t)((u >> (i < 64 ? i : 63)) & 1);
        return b;
    }
    static int64_t decode(const uint8_t* bits, int width) {
        (void)checked_size_(width);
        uint64_t v = 0;
        for (int i = 0; i < width && i < 64; ++i) v |= (uint64_t)(bits[i] ? 1 : 0) << i;
        const int sign_width = (width < 64) ? width : 64;
        if (sign_width < 64 && ((v >> (sign_width - 1)) & 1))
            v |= ~((uint64_t(1) << sign_width) - 1);
        return (int64_t)v;
    }

private:
    Ctx* ctx_ = nullptr;
    std::vector<Wire> w_;

    static std::size_t checked_size_(int width) {
        expecting(width >= 1, "DynamicInt_T: width must be >= 1");
        return (std::size_t)width;
    }
    void check_operand_(const DynamicInt_T& o) const {
        check_same_context(*this, o);
        expecting(width() == o.width(), "DynamicInt_T: operands have different widths");
    }
    DynamicInt_T zeros_() const {
        DynamicInt_T r(*ctx_, width()); Wire z = ctx_->public_bit(false);
        for (int i = 0; i < width(); ++i) r.w_[(std::size_t)i] = z;
        return r;
    }
};

template <BooleanContext Ctx, int N> requires (N > 0)
inline Int_T<Ctx, N> UInt_T<Ctx, N>::as_signed() const {
    return Int_T<Ctx, N>::from_wires(*ctx_, w.data());
}

template <BooleanContext Ctx>
inline DynamicInt_T<Ctx> DynamicUInt_T<Ctx>::as_signed() const {
    return DynamicInt_T<Ctx>::from_wires(*ctx_, data(), width());
}

template <BooleanContext Ctx, int N> requires (N > 0)
inline DynamicInt_T<Ctx> Int_T<Ctx, N>::to_dynamic() const {
    return DynamicInt_T<Ctx>::from_wires(*ctx_, w.data(), N);
}

}  // namespace emp
#endif  // EMP_CIRCUIT_SIGNED_INT_H__
