#ifndef EMP_CIRCUIT_NUMERIC_KERNELS_H__
#define EMP_CIRCUIT_NUMERIC_KERNELS_H__

// Bare-wire structured arithmetic kernels over a BooleanContext: the small,
// inlineable, compiler-fusible primitives the value types build on (ripple
// add/sub, mux/select, comparators, multiply, restoring division). They operate
// on Ctx::Wire arrays directly (no per-bit context pointer). LSB-first throughout.

#include "emp-tool/ir/context/concept.h"
#include "emp-tool/runtime/core/utils.h"
#include <vector>

namespace emp {
namespace kernel {

// ceil(log2(n)) for n >= 1 — the number of bits a shift amount in [0, n) needs.
constexpr int clog2_ceil(int n) { int b = 0; while ((1 << b) < n) ++b; return b; }
// Smallest width that holds the values 0..n (e.g. a popcount of n bits).
constexpr int bits_for(int n) { int b = 1; while ((1 << b) <= n) ++b; return b; }

// OR via the free-XOR identity: a | b = a ^ b ^ (a & b) (one AND).
template <BooleanContext Ctx>
inline typename Ctx::Wire or_gate(Ctx& c, typename Ctx::Wire a, typename Ctx::Wire b) {
    return c.xor_gate(c.xor_gate(a, b), c.and_gate(a, b));
}

// The variable-length carry/borrow arithmetic — size-optimal 1-AND-per-full-adder
// primitives. `dest` may alias op1: each step reads op1[i]/op2[i] before writing dest[i].

// dest <- op1 + op2 + carryIn (mod 2^size). If carryOut != nullptr it receives
// the carry-out and the top bit is not folded into dest; otherwise the final
// carry is dropped and the top bit takes no AND (so an N-bit add is N-1 ANDs).
template <BooleanContext Ctx>
inline void add_full(Ctx& c, typename Ctx::Wire* dest, typename Ctx::Wire* carryOut,
                     const typename Ctx::Wire* op1, const typename Ctx::Wire* op2,
                     const typename Ctx::Wire* carryIn, int size) {
    using W = typename Ctx::Wire;
    if (size == 0) { if (carryIn && carryOut) *carryOut = *carryIn; return; }
    W carry = carryIn ? *carryIn : c.public_bit(false);
    int skipLast = (carryOut == nullptr) ? 1 : 0;
    int i = 0;
    while (size-- > skipLast) {
        W axc = c.xor_gate(op1[i], carry);
        W bxc = c.xor_gate(op2[i], carry);
        dest[i] = c.xor_gate(op1[i], bxc);
        W t = c.and_gate(axc, bxc);
        carry = c.xor_gate(carry, t);
        ++i;
    }
    if (carryOut) *carryOut = carry;
    else dest[i] = c.xor_gate(c.xor_gate(carry, op2[i]), op1[i]);
}

// dest <- op1 - op2 - borrowIn (mod 2^size). borrowOut as in add_full.
template <BooleanContext Ctx>
inline void sub_full(Ctx& c, typename Ctx::Wire* dest, typename Ctx::Wire* borrowOut,
                     const typename Ctx::Wire* op1, const typename Ctx::Wire* op2,
                     const typename Ctx::Wire* borrowIn, int size) {
    using W = typename Ctx::Wire;
    if (size == 0) { if (borrowIn && borrowOut) *borrowOut = *borrowIn; return; }
    W borrow = borrowIn ? *borrowIn : c.public_bit(false);
    int skipLast = (borrowOut == nullptr) ? 1 : 0;
    int i = 0;
    while (size-- > skipLast) {
        W bxa = c.xor_gate(op1[i], op2[i]);
        W bxc = c.xor_gate(borrow, op2[i]);
        dest[i] = c.xor_gate(bxa, borrow);
        W t = c.and_gate(bxa, bxc);
        borrow = c.xor_gate(borrow, t);
        ++i;
    }
    if (borrowOut) *borrowOut = borrow;
    else dest[i] = c.xor_gate(c.xor_gate(op1[i], op2[i]), borrow);
}

// out <- a + b (mod 2^N). 1 AND per full adder, MSB carry-out dropped -> N-1 ANDs.
template <BooleanContext Ctx>
inline void ripple_add(Ctx& c, const typename Ctx::Wire* a,
                       const typename Ctx::Wire* b, typename Ctx::Wire* out, int N) {
    add_full<Ctx>(c, out, nullptr, a, b, nullptr, N);
}

// out <- a - b (mod 2^N). Same 1-AND/bit, MSB borrow dropped -> N-1 ANDs.
template <BooleanContext Ctx>
inline void ripple_sub(Ctx& c, const typename Ctx::Wire* a,
                       const typename Ctx::Wire* b, typename Ctx::Wire* out, int N) {
    sub_full<Ctx>(c, out, nullptr, a, b, nullptr, N);
}

// sel ? t : f, per wire: f ^ (sel & (t ^ f)) — one AND per bit.
template <BooleanContext Ctx>
inline typename Ctx::Wire mux(Ctx& c, typename Ctx::Wire sel,
                              typename Ctx::Wire t, typename Ctx::Wire f) {
    return c.xor_gate(f, c.and_gate(sel, c.xor_gate(t, f)));
}

// unsigned a < b: the borrow-out of (a - b), without materializing difference
// bits. One AND per input bit.
template <BooleanContext Ctx>
inline typename Ctx::Wire less_than(Ctx& c, const typename Ctx::Wire* a,
                                    const typename Ctx::Wire* b, int N) {
    using W = typename Ctx::Wire;
    W borrow = c.public_bit(false);
    for (int i = 0; i < N; ++i) {
        W bxa = c.xor_gate(a[i], b[i]);
        W bxc = c.xor_gate(borrow, b[i]);
        W t = c.and_gate(bxa, bxc);
        borrow = c.xor_gate(borrow, t);
    }
    return borrow;
}

// all bits equal: AND of XNORs.
template <BooleanContext Ctx>
inline typename Ctx::Wire equal(Ctx& c, const typename Ctx::Wire* a,
                                const typename Ctx::Wire* b, int N) {
    using W = typename Ctx::Wire;
    if (N == 0) return c.public_bit(true);
    W acc = c.not_gate(c.xor_gate(a[0], b[0]));
    for (int i = 1; i < N; ++i)
        acc = c.and_gate(acc, c.not_gate(c.xor_gate(a[i], b[i])));   // acc & ~(a^b)
    return acc;
}

// dest[i] <- cond ? tsrc[i] : fsrc[i]. dest may alias tsrc or fsrc.
template <BooleanContext Ctx>
inline void if_then_else(Ctx& c, typename Ctx::Wire* dest, const typename Ctx::Wire* tsrc,
                         const typename Ctx::Wire* fsrc, int size, typename Ctx::Wire cond) {
    using W = typename Ctx::Wire;
    int i = 0;
    while (size-- > 0) {
        W x = c.xor_gate(tsrc[i], fsrc[i]);
        W a = c.and_gate(cond, x);
        dest[i] = c.xor_gate(a, fsrc[i]);
        ++i;
    }
}

// dest <- low N bits of op1 * op2 (unsigned/signed identical at the low N bits).
template <BooleanContext Ctx>
inline void mul_full(Ctx& c, typename Ctx::Wire* dest,
                     const typename Ctx::Wire* op1, const typename Ctx::Wire* op2, int N) {
    using W = typename Ctx::Wire;
    std::vector<W> sum(N);
    std::vector<W> tmp(N);
    for (int k = 0; k < N; ++k) sum[k] = c.and_gate(op1[k], op2[0]);
    for (int i = 1; i < N; ++i) {
        for (int k = 0; k < N - i; ++k) tmp[k] = c.and_gate(op1[k], op2[i]);
        add_full<Ctx>(c, sum.data() + i, nullptr, sum.data() + i, tmp.data(), nullptr, N - i);
    }
    for (int i = 0; i < N; ++i) dest[i] = sum[i];
}

// vquot, vrem <- op1 / op2, op1 % op2 (both unsigned). Either out may be null.
// op2 == 0 saturates (restoring-division circuit, not C semantics).
template <BooleanContext Ctx>
inline void div_full(Ctx& c, typename Ctx::Wire* vquot, typename Ctx::Wire* vrem,
                     const typename Ctx::Wire* op1, const typename Ctx::Wire* op2, int N) {
    using W = typename Ctx::Wire;
    std::vector<W> overflow(N), tmp(N), rem(N);
    W b;
    for (int i = 0; i < N; ++i) rem[i] = op1[i];
    overflow[0] = c.public_bit(false);
    for (int i = 1; i < N; ++i) overflow[i] = or_gate(c, overflow[i - 1], op2[N - i]);
    for (int i = N - 1; i >= 0; --i) {
        sub_full<Ctx>(c, tmp.data(), &b, rem.data() + i, op2, nullptr, N - i);
        b = or_gate(c, b, overflow[i]);
        if_then_else<Ctx>(c, rem.data() + i, rem.data() + i, tmp.data(), N - i, b);
        overflow[i] = b;
    }
    if (vrem)  for (int i = 0; i < N; ++i) vrem[i]  = rem[i];
    if (vquot) for (int i = 0; i < N; ++i) vquot[i] = c.not_gate(overflow[i]);
}

}  // namespace kernel
}  // namespace emp
#endif  // EMP_CIRCUIT_NUMERIC_KERNELS_H__
