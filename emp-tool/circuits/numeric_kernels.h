#ifndef EMP_CIRCUIT_NUMERIC_KERNELS_H__
#define EMP_CIRCUIT_NUMERIC_KERNELS_H__

// Bare-wire structured arithmetic kernels over a BooleanContext: the small,
// inlineable, compiler-fusible primitives the value types build on (ripple
// add/sub, mux/select, comparators, negation, bit counts, secret-amount shifts,
// multiply, division). They operate on Ctx::Wire arrays directly (no per-bit
// context pointer). LSB-first throughout.

#include "emp-tool/ir/context/concept.h"
#include "emp-tool/runtime/core/utils.h"
#include <algorithm>
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

// signed a < b. Flipping both sign bits maps two's-complement order onto
// unsigned order, so this is the unsigned borrow-out XOR both sign bits: N ANDs.
template <BooleanContext Ctx>
inline typename Ctx::Wire signed_less_than(Ctx& c, const typename Ctx::Wire* a,
                                           const typename Ctx::Wire* b, int N) {
    typename Ctx::Wire ub = less_than<Ctx>(c, a, b, N);
    return c.xor_gate(c.xor_gate(ub, a[N - 1]), b[N - 1]);
}

// all bits equal: AND of XNORs as a balanced tree, N-1 ANDs at AND depth
// ceil(log2 N).
template <BooleanContext Ctx>
inline typename Ctx::Wire equal(Ctx& c, const typename Ctx::Wire* a,
                                const typename Ctx::Wire* b, int N) {
    using W = typename Ctx::Wire;
    if (N == 0) return c.public_bit(true);
    std::vector<W> v((std::size_t)N);
    for (int i = 0; i < N; ++i) v[(std::size_t)i] = c.not_gate(c.xor_gate(a[i], b[i]));
    for (int n = N; n > 1;) {
        int k = 0;
        for (int i = 0; i + 1 < n; i += 2) v[(std::size_t)k++] = c.and_gate(v[(std::size_t)i], v[(std::size_t)(i + 1)]);
        if (n & 1) v[(std::size_t)k++] = v[(std::size_t)(n - 1)];
        n = k;
    }
    return v[0];
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

// dest <- -x (mod 2^N). Bit i is x_i ^ OR(x_0..x_{i-1}), so only the prefix
// ORs cost ANDs: N-2 for N >= 2. dest may alias x.
template <BooleanContext Ctx>
inline void negate(Ctx& c, typename Ctx::Wire* dest, const typename Ctx::Wire* x, int N) {
    using W = typename Ctx::Wire;
    if (N <= 0) return;
    W any = x[0];                          // OR of the bits below i
    dest[0] = x[0];
    for (int i = 1; i < N; ++i) {
        W xi = x[i];
        dest[i] = c.xor_gate(xi, any);
        if (i + 1 < N) any = or_gate(c, any, xi);
    }
}

// dest <- s ? -x : x (mod 2^N), as (x ^ s) + s: a half-adder chain, N-1 ANDs.
// dest may alias x.
template <BooleanContext Ctx>
inline void cond_negate(Ctx& c, typename Ctx::Wire* dest, const typename Ctx::Wire* x,
                        int N, typename Ctx::Wire s) {
    using W = typename Ctx::Wire;
    if (N <= 0) return;
    W carry = c.and_gate(c.xor_gate(x[0], s), s);
    dest[0] = x[0];
    for (int i = 1; i < N; ++i) {
        W y = c.xor_gate(x[i], s);
        dest[i] = c.xor_gate(y, carry);
        if (i + 1 < N) carry = c.and_gate(y, carry);
    }
}

// OR of v[0..n) as a balanced tree, using v as scratch: n-1 ANDs, AND depth
// ceil(log2 n). Returns the public 0 for n == 0.
template <BooleanContext Ctx>
inline typename Ctx::Wire or_reduce_inplace(Ctx& c, typename Ctx::Wire* v, int n) {
    if (n <= 0) return c.public_bit(false);
    while (n > 1) {
        int k = 0;
        for (int i = 0; i + 1 < n; i += 2) v[k++] = or_gate(c, v[i], v[i + 1]);
        if (n & 1) v[k++] = v[n - 1];
        n = k;
    }
    return v[0];
}

// out[0..R) <- the number of ones in x[0..n), R >= bits_for(n). Each weight
// column is reduced to one bit by full adders (three bits to a sum and a
// carry, one AND) and at most one half adder; the column of weight 2^k holds
// m_k bits (m_0 = n, m_{k+1} = floor(m_k/2)) and costs floor(m_k/2) ANDs, so
// the count takes n - popcount(n) ANDs, the multiplicative complexity of the
// Hamming weight, at AND depth bits_for(n) - 1.
template <BooleanContext Ctx>
inline void popcount(Ctx& c, typename Ctx::Wire* out, int R,
                     const typename Ctx::Wire* x, int n) {
    using W = typename Ctx::Wire;
    std::vector<W> col(x, x + n), carries;
    int k = 0;
    while (!col.empty()) {
        carries.clear();
        W s = col[0];
        std::size_t i = 1;
        for (; i + 1 < col.size(); i += 2) {          // full adder: s + col[i] + col[i+1]
            W sa = c.xor_gate(s, col[i]), sb = c.xor_gate(s, col[i + 1]);
            carries.push_back(c.xor_gate(s, c.and_gate(sa, sb)));   // majority
            s = c.xor_gate(sa, col[i + 1]);
        }
        if (i < col.size()) {                          // half adder
            carries.push_back(c.and_gate(s, col[i]));
            s = c.xor_gate(s, col[i]);
        }
        out[k++] = s;
        col.swap(carries);
    }
    if (k < R) { W z = c.public_bit(false); for (; k < R; ++k) out[k] = z; }
}

// out[0..R) <- the number of leading (most-significant) zero bits of x[0..n),
// R >= bits_for(n). z_i = [x_i..x_{n-1} all zero] is a thermometer code of the
// count L (z_{n-m} = [L >= m]), so bit j of L is the XOR of z_{n - m*2^j} over
// m >= 1 and only the suffix chain costs ANDs: n-1, the degree of [x == 0].
template <BooleanContext Ctx>
inline void leading_zeros(Ctx& c, typename Ctx::Wire* out, int R,
                          const typename Ctx::Wire* x, int n) {
    using W = typename Ctx::Wire;
    std::vector<W> z((std::size_t)n);
    z[(std::size_t)(n - 1)] = c.not_gate(x[n - 1]);
    for (int i = n - 2; i >= 0; --i)
        z[(std::size_t)i] = c.and_gate(z[(std::size_t)(i + 1)], c.not_gate(x[i]));
    for (int j = 0; j < R; ++j) {
        bool any = false;
        W acc{};
        for (long m = 1; (m << j) <= n; ++m) {
            W t = z[(std::size_t)(n - (m << j))];
            acc = any ? c.xor_gate(acc, t) : t;
            any = true;
        }
        out[j] = any ? acc : c.public_bit(false);
    }
}

// dest <- x shifted by the secret N-bit unsigned amount sh; amounts >= N give
// zero, or all sign bits for ArithmeticRight (docs/numeric_semantics.md).
// Barrel stages handle amount bits below use-1 (use = ceil(log2 N)). The top
// stage also applies the overflow test (an amount bit at or above use set):
// a position whose shifted source is out of range takes one AND instead of
// a stage mux plus a final mask. dest must not alias x or sh.
enum class ShiftKind { Left, LogicalRight, ArithmeticRight };
template <BooleanContext Ctx>
inline void shift_by(Ctx& c, typename Ctx::Wire* dest, const typename Ctx::Wire* x,
                     const typename Ctx::Wire* sh, int N, ShiftKind kind) {
    using W = typename Ctx::Wire;
    const bool left = kind == ShiftKind::Left, arith = kind == ShiftKind::ArithmeticRight;
    const int use = (N <= 1) ? 0 : clog2_ceil(N);
    const W sign = x[N - 1];
    const W fill = arith ? sign : c.public_bit(false);
    std::vector<W> buf((std::size_t)(3 * N - use));
    W* cur = buf.data();
    W* next = cur + N;
    W* high = next + N;                   // amount bits at or above use
    for (int j = 0; j < N; ++j) cur[j] = x[j];
    for (int i = 0; i + 1 < use; ++i) {
        const int dist = 1 << i;
        for (int j = 0; j < N; ++j) {
            if (arith && j == N - 1) { next[j] = sign; continue; }
            const int src = left ? j - dist : j + dist;
            next[j] = mux(c, sh[i], (src >= 0 && src < N) ? cur[src] : fill, cur[j]);
        }
        std::swap(cur, next);
    }
    for (int i = use; i < N; ++i) high[i - use] = sh[i];
    const W over = or_reduce_inplace(c, high, N - use);
    if (use == 0) {                       // N == 1: the amount is 0 or overflows
        dest[0] = arith ? x[0] : c.and_gate(x[0], c.not_gate(over));
        return;
    }
    const int dist = 1 << (use - 1);
    const W s = sh[use - 1];
    const W moved = or_gate(c, over, s);  // shifted by dist, or overflowed
    const W keep = c.not_gate(moved);     // neither: position keeps its bit
    const W take = c.xor_gate(moved, over);   // s & ~over: take the shifted bit
    for (int j = 0; j < N; ++j) {
        const int src = left ? j - dist : j + dist;
        const bool in_range = src >= 0 && src < N;
        if (arith) {
            if (j == N - 1) dest[j] = sign;
            else if (!in_range) dest[j] = mux(c, moved, sign, cur[j]);
            else dest[j] = mux(c, over, sign, mux(c, s, cur[src], cur[j]));
        } else {
            W kept = c.and_gate(cur[j], keep);
            dest[j] = in_range ? c.xor_gate(kept, c.and_gate(cur[src], take)) : kept;
        }
    }
}

namespace detail {

// A sum of weighted bits modulo 2^width, by column compression. The bits come
// from rows (v[j] has weight 2^(off+j)), from partial-product blocks (x_i y_j
// has weight 2^(off+i+j), the AND made when its column is reached), and from
// public ones (ones[k] != 0 adds 2^k, each column at most once). Each column is
// reduced to one bit by full adders (one AND each) and at most one half adder,
// carries moving to the next column; the top column only needs its parity. A
// public one meeting an odd column is a free half adder (a + 1 = ~a + 2a).
template <BooleanContext Ctx>
struct ColumnSum {
    using W = typename Ctx::Wire;
    struct Row { int off; const W* v; int n; };
    struct Block { int off; const W* x; int n; const W* y; int m; };
    Row rows[8];
    int nrows = 0;
    Block blocks[2];
    int nblocks = 0;
    void row(int off, const W* v, int n) { rows[nrows++] = {off, v, n}; }
    void block(int off, const W* x, int n, const W* y, int m) { blocks[nblocks++] = {off, x, n, y, m}; }

    void sum(Ctx& c, W* out, int width, const uint8_t* ones = nullptr) {
        int cap = 4;
        for (int r = 0; r < nrows; ++r) cap += 1;
        for (int b = 0; b < nblocks; ++b) cap += std::min(blocks[b].n, blocks[b].m);
        cap += cap + width;   // incoming carries never exceed the column height plus the width
        std::vector<W> buf((std::size_t)(3 * cap));
        W* col = buf.data();
        W* carry_in = col + cap;
        W* carry_out = carry_in + cap;
        int nin = 0;
        for (int k = 0; k < width; ++k) {
            const bool one = ones && ones[k];
            int m = one ? 1 : 0;              // slot 0 is kept for a public one
            for (int b = 0; b < nblocks; ++b) {
                const Block& B = blocks[b];
                const int d = k - B.off;
                for (int j = std::max(0, d - (B.n - 1)); j <= std::min(B.m - 1, d); ++j)
                    col[m++] = c.and_gate(B.x[d - j], B.y[j]);
            }
            for (int r = 0; r < nrows; ++r) {
                const int j = k - rows[r].off;
                if (j >= 0 && j < rows[r].n) col[m++] = rows[r].v[j];
            }
            for (int t = 0; t < nin; ++t) col[m++] = carry_in[t];
            W* v = col + (one ? 1 : 0);
            int len = m - (one ? 1 : 0);
            if (len == 0) { out[k] = c.public_bit(one); nin = 0; continue; }
            if (k == width - 1) {
                W s = v[0];
                for (int i = 1; i < len; ++i) s = c.xor_gate(s, v[i]);
                out[k] = one ? c.not_gate(s) : s;
                return;
            }
            int nc = 0;
            if (one) {
                if (len & 1) {                              // a + 1 = ~a + 2a
                    W a = v[len - 1];
                    v[len - 1] = c.not_gate(a);
                    carry_out[nc++] = a;
                } else {                                    // full adder with the constant
                    col[0] = c.public_bit(true);
                    v = col; ++len;
                }
            }
            W s = v[0];
            int i = 1;
            for (; i + 1 < len; i += 2) {
                W sa = c.xor_gate(s, v[i]), sb = c.xor_gate(s, v[i + 1]);
                carry_out[nc++] = c.xor_gate(s, c.and_gate(sa, sb));
                s = c.xor_gate(sa, v[i + 1]);
            }
            if (i < len) {
                carry_out[nc++] = c.and_gate(s, v[i]);
                s = c.xor_gate(s, v[i]);
            }
            out[k] = s;
            std::swap(carry_in, carry_out);
            nin = nc;
        }
    }
};

// Size thresholds of the product recursion below.
constexpr int kKaratsubaMinBits = 12;     // wide products of at least this many bits split
constexpr int kShortProductMinBits = 24;  // truncated products of at least this many bits split
constexpr int kFusedCrossMaxBits = 20;    // narrower short-product cross terms stay partial products

// out[0..width) <- x * y mod 2^width for n-bit x, y and n <= width <= 2n.
// Truncated (width == n) products from kShortProductMinBits bits split x and y
// at h = ceil(9n/16) (a short product): lo_n(x0 y0) + 2^h lo_{n-h}(x0 y1 + x1 y0).
// Wider products from kKaratsubaMinBits bits use subtractive Karatsuba:
// x y = z0 + 2^h z1 + 2^2h z2 with z1 = z0 + z2 - (x1-x0)(y1-y0), where the
// middle product multiplies |x1-x0| by |y1-y0| and the sign is applied in the
// final sum. Everything else is schoolbook partial products summed by columns.
template <BooleanContext Ctx>
inline void product(Ctx& c, typename Ctx::Wire* out, const typename Ctx::Wire* x,
                    const typename Ctx::Wire* y, int n, int width) {
    using W = typename Ctx::Wire;
    ColumnSum<Ctx> sum;
    if (width == n && n >= kShortProductMinBits) {
        const int h = std::min(n - 1, (9 * n + 15) / 16), lo = n - h;   // 2h >= n
        const bool fused = lo <= kFusedCrossMaxBits;
        std::vector<W> tmp((std::size_t)(n + (fused ? 0 : 2 * lo)));
        product(c, tmp.data(), x, y, h, n);
        sum.row(0, tmp.data(), n);
        if (fused) {   // cross-term partial products go straight into the sum
            sum.block(h, x, lo, y + h, lo);
            sum.block(h, x + h, lo, y, lo);
        } else {
            W* t1 = tmp.data() + n; W* t2 = t1 + lo;
            product(c, t1, x, y + h, lo, lo);
            product(c, t2, x + h, y, lo, lo);
            sum.row(h, t1, lo);
            sum.row(h, t2, lo);
        }
        sum.sum(c, out, n);
    } else if (width > n && n >= kKaratsubaMinBits) {
        const int h = n / 2, hi = n - h;
        const int n0 = std::min(2 * h, width), n2 = std::min(2 * hi, width - h);   // z2 is read at 2^h and 2^2h
        std::vector<W> buf((std::size_t)(n0 + 2 * n2 + 4 * hi + 2));
        W* z0 = buf.data(); W* z2 = z0 + n0; W* m = z2 + n2;
        W* x0 = m + n2; W* y0 = x0 + hi; W* dx = y0 + hi; W* dy = dx + hi; W* tt = dy + hi;
        product(c, z0, x, y, h, n0);
        product(c, z2, x + h, y + h, hi, n2);
        // |x1 - x0| and [x1 < x0] over hi bits (x0 zero-extended), same for y.
        const W zero = c.public_bit(false);
        for (int j = 0; j < hi; ++j) { x0[j] = j < h ? x[j] : zero; y0[j] = j < h ? y[j] : zero; }
        W bx, by;
        sub_full<Ctx>(c, dx, &bx, x + h, x0, nullptr, hi);
        cond_negate(c, dx, dx, hi, bx);
        sub_full<Ctx>(c, dy, &by, y + h, y0, nullptr, hi);
        cond_negate(c, dy, dy, hi, by);
        product(c, m, dx, dy, hi, n2);
        // z1 = z0 + z2 - (-1)^(bx^by) |x1-x0||y1-y0|: add (m ^ t) + t with
        // t = ~(bx ^ by), m zero-extended, i.e. t from bit |m| upward, which is
        // ~t at bit |m| plus ones above (t(2^w - 2^k) = -t 2^k mod 2^w).
        const W t = c.not_gate(c.xor_gate(bx, by));
        for (int j = 0; j < n2; ++j) m[j] = c.xor_gate(m[j], t);
        tt[0] = t; tt[1] = c.not_gate(t);
        const int top = h + n2;
        std::vector<uint8_t> ones((std::size_t)width, 0);
        for (int j = top; j < width; ++j) ones[(std::size_t)j] = 1;
        sum.row(0, z0, n0);
        sum.row(2 * h, z2, n2);
        sum.row(h, z0, n0);
        sum.row(h, z2, n2);
        sum.row(h, m, n2);
        sum.row(h, tt, 1);
        if (top < width) sum.row(top, tt + 1, 1);
        sum.sum(c, out, width, ones.data());
    } else {
        sum.block(0, x, n, y, n);
        sum.sum(c, out, width);
    }
}

}  // namespace detail

// dest <- low N bits of op1 * op2 (unsigned/signed identical at the low N bits).
template <BooleanContext Ctx>
inline void mul_full(Ctx& c, typename Ctx::Wire* dest,
                     const typename Ctx::Wire* op1, const typename Ctx::Wire* op2, int N) {
    std::vector<typename Ctx::Wire> r((std::size_t)N);
    detail::product(c, r.data(), op1, op2, N, N);
    for (int i = 0; i < N; ++i) dest[i] = r[(std::size_t)i];
}

// vquot, vrem <- op1 / op2, op1 % op2 (both unsigned). Either out may be null,
// and either may alias an operand. op2 == 0 gives an all-ones quotient and
// remainder op1. Non-restoring division over a growing window: after the top w
// dividend bits, with T the restoring partial remainder of those bits
// (T < 2^w), the circuit keeps only U = T - op2 mod 2^(w+1). When op2 < 2^w the
// top bit of U is the sign of T - op2 and decides the quotient bit; when
// op2 >= 2^w the bit is 0. The next window's U is 2U + x - op2 after a 1 bit
// and 2U + x + op2 after a 0 bit (adding op2 back restores T), so no step needs
// a restoring multiplexer. The quotient takes N(N+1)/2 + 2N - 3 ANDs (N >= 2);
// the remainder, U + op2 when the last U is negative, takes 2N - 1 more.
template <BooleanContext Ctx>
inline void div_full(Ctx& c, typename Ctx::Wire* vquot, typename Ctx::Wire* vrem,
                     const typename Ctx::Wire* op1, const typename Ctx::Wire* op2, int N) {
    using W = typename Ctx::Wire;
    const W zero = c.public_bit(false);
    std::vector<W> buf((std::size_t)(6 * N + 3), zero);
    W* big = buf.data();                  // big[w] = [op2 >= 2^w] = OR(op2[w..N-1]), 1 <= w < N
    W* u = big + N;                       // N+1 bits
    W* a = u + N + 1;                     // N+1 bits
    W* b = a + N + 1;                     // N+1 bits
    W* q = b + N + 1;                     // N bits
    W* r = q + N;                         // N bits
    if (N >= 2) {
        big[N - 1] = op2[N - 1];
        for (int w = N - 2; w >= 1; --w) big[w] = or_gate(c, big[w + 1], op2[w]);
    }
    W sub = c.public_bit(true);           // 1: subtract op2 next, 0: add it back
    for (int w = 1; w <= N; ++w) {
        const int i = N - w;              // dividend bit entering the window
        a[0] = op1[i];                    // a = 2U + x_i over w+1 bits
        for (int j = w; j >= 1; --j) a[j] = u[j - 1];
        for (int j = 0; j <= w; ++j) b[j] = c.xor_gate(j < N ? op2[j] : zero, sub);
        add_full<Ctx>(c, u, nullptr, a, b, &sub, w + 1);
        const W neg = u[w];
        q[i] = c.not_gate(w < N ? or_gate(c, neg, big[w]) : neg);
        sub = q[i];
    }
    if (vrem) {                           // U + op2 when the last U is negative
        const W neg = u[N];
        for (int j = 0; j < N; ++j) b[j] = c.and_gate(op2[j], neg);
        add_full<Ctx>(c, r, nullptr, u, b, nullptr, N);
    }
    // Written last, so either output may alias either operand.
    if (vquot) for (int i = 0; i < N; ++i) vquot[i] = q[i];
    if (vrem)  for (int i = 0; i < N; ++i) vrem[i] = r[i];
}

}  // namespace kernel
}  // namespace emp
#endif  // EMP_CIRCUIT_NUMERIC_KERNELS_H__
