#ifndef EMP_CIRCUIT_SORT_H__
#define EMP_CIRCUIT_SORT_H__

// Data-oblivious sorting over circuit values. Works for any value V supporting
// `Bit_T<Ctx> operator<(const V&)` and `V select(Bit_T<Ctx>, V)` (UInt_T / Int_T /
// Float_T / BitVec_T's as_uint, …). The compare-swap schedule is a fixed sorting
// network (any length), so the gate stream is identical for every input —
// independent of the data and of `ascending`.
//
// A compare-swap is one comparison plus one AND per exchanged bit: with
// d = s & (a ^ b), the outputs a ^ d and b ^ d equal a.select(s, b) and
// b.select(s, a), two muxes that would each compute that AND. Fixed-width
// values (WireBundle) exchange at the wire level, runtime-width integers as
// a.select(s, b) and a ^ b ^ that, and any other value with two selects.
//
// For the integer families the exchange bit only has to be right when the two
// values differ, which saves one AND per compare-swap in sort() (sort_by_key
// keeps the exact comparison, so equal keys never exchange their payloads).

#include "emp-tool/circuits/signed_int.h"     // Int_T (exchange_bit)
#include "emp-tool/circuits/unsigned_int.h"   // UInt_T (exchange_bit)
#include "emp-tool/ir/context/checks.h"    // check_same_context
#include "emp-tool/ir/wire_value.h"        // WireBundle
#include "emp-tool/runtime/core/utils.h"   // error()
#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace emp {

namespace sort_detail {

// (a, b) <- s ? (b, a) : (a, b), one AND per bit pair.
template <class S, class V>
inline void swap_if(const S& s, V& a, V& b) {
    if constexpr (WireBundle<V>) {
        check_same_context(a, s); check_same_context(a, b);
        using Wire = typename V::Wire;
        std::array<Wire, (std::size_t)V::width()> wa, wb;
        a.pack_wires(wa.data()); b.pack_wires(wb.data());
        auto& c = *a.context();
        for (std::size_t i = 0; i < wa.size(); ++i) {
            Wire d = c.and_gate(s.w, c.xor_gate(wa[i], wb[i]));
            wa[i] = c.xor_gate(wa[i], d);
            wb[i] = c.xor_gate(wb[i], d);
        }
        a = V::from_wires(c, wa.data());
        b = V::from_wires(c, wb.data());
    } else if constexpr (requires(const V& x) { requires V::is_dynamic; { x ^ x } -> std::same_as<V>; }) {
        V lo = a.select(s, b);
        V hi = a ^ b ^ lo;
        a = lo; b = hi;
    } else {
        V lo = a.select(s, b), hi = b.select(s, a);
        a = lo; b = hi;
    }
}

// The integer families, where equal values are equal bit patterns.
template <class V> inline constexpr int int_kind = 0;   // 1 unsigned, 2 signed
template <BooleanContext Ctx, int N> inline constexpr int int_kind<UInt_T<Ctx, N>> = 1;
template <BooleanContext Ctx, int N> inline constexpr int int_kind<Int_T<Ctx, N>> = 2;
template <BooleanContext Ctx> inline constexpr int int_kind<DynamicUInt_T<Ctx>> = 1;
template <BooleanContext Ctx> inline constexpr int int_kind<DynamicInt_T<Ctx>> = 2;

// A bit that is [x < y] whenever x != y. Exchanging equal integers changes
// nothing, so the bit may be anything when x == y: the borrow chain of x - y
// starts at bit 1 with borrow y[0] (the bit-0 borrow wherever x[0] != y[0]),
// one AND fewer than an exact comparison. Signed values compare as unsigned
// with both sign bits complemented.
template <class V>
inline auto exchange_bit(const V& x, const V& y) {
    using Ctx = typename V::context_type;
    using Wire = typename V::Wire;
    check_same_context(x, y);
    int n;
    if constexpr (WireBundle<V>) n = V::width();
    else {
        n = x.width();
        expecting(n > 0 && n == y.width(), "compare_swap: operands are unbound or have different widths");
    }
    Ctx& c = *x.context();
    const Wire* xs = x.data();
    const Wire* ys = y.data();
    auto y_at = [&](int i) { return (int_kind<V> == 2 && i == n - 1) ? c.not_gate(ys[i]) : ys[i]; };
    Wire borrow = y_at(0);
    for (int i = 1; i < n; ++i) {
        Wire t = c.and_gate(c.xor_gate(xs[i], ys[i]), c.xor_gate(borrow, y_at(i)));
        borrow = c.xor_gate(borrow, t);
    }
    return Bit_T<Ctx>(c, borrow);
}

// The smallest known sorting networks for 9..16 inputs (D. Knuth, TAOCP vol. 3,
// §5.3.4; B. Dobbelaere, "Smallest and fastest sorting networks"), as (i, j)
// pairs with i < j, one network after another.
inline constexpr uint8_t kSmallNet[][2] = {
    // n = 9: 25 comparators
    {0,3}, {1,7}, {2,5}, {4,8}, {0,7}, {2,4}, {3,8}, {5,6}, {0,2}, {1,3}, {4,5}, {7,8},
    {1,4}, {3,6}, {5,7}, {0,1}, {2,4}, {3,5}, {6,8}, {2,3}, {4,5}, {6,7}, {1,2}, {3,4},
    {5,6},
    // n = 10: 29 comparators
    {0,8}, {1,9}, {2,7}, {3,5}, {4,6}, {0,2}, {1,4}, {5,8}, {7,9}, {0,3}, {2,4}, {5,7},
    {6,9}, {0,1}, {3,6}, {8,9}, {1,5}, {2,3}, {4,8}, {6,7}, {1,2}, {3,5}, {4,6}, {7,8},
    {2,3}, {4,5}, {6,7}, {3,4}, {5,6},
    // n = 11: 35 comparators
    {0,9}, {1,6}, {2,4}, {3,7}, {5,8}, {0,1}, {3,5}, {4,10}, {6,9}, {7,8}, {1,3}, {2,5},
    {4,7}, {8,10}, {0,4}, {1,2}, {3,7}, {5,9}, {6,8}, {0,1}, {2,6}, {4,5}, {7,8}, {9,10},
    {2,4}, {3,6}, {5,7}, {8,9}, {1,2}, {3,4}, {5,6}, {7,8}, {2,3}, {4,5}, {6,7},
    // n = 12: 39 comparators
    {0,8}, {1,7}, {2,6}, {3,11}, {4,10}, {5,9}, {0,1}, {2,5}, {3,4}, {6,9}, {7,8}, {10,11},
    {0,2}, {1,6}, {5,10}, {9,11}, {0,3}, {1,2}, {4,6}, {5,7}, {8,11}, {9,10}, {1,4}, {3,5},
    {6,8}, {7,10}, {1,3}, {2,5}, {6,9}, {8,10}, {2,3}, {4,5}, {6,7}, {8,9}, {4,6}, {5,7},
    {3,4}, {5,6}, {7,8},
    // n = 13: 45 comparators
    {0,12}, {1,10}, {2,9}, {3,7}, {5,11}, {6,8}, {1,6}, {2,3}, {4,11}, {7,9}, {8,10},
    {0,4}, {1,2}, {3,6}, {7,8}, {9,10}, {11,12}, {4,6}, {5,9}, {8,11}, {10,12}, {0,5},
    {3,8}, {4,7}, {6,11}, {9,10}, {0,1}, {2,5}, {6,9}, {7,8}, {10,11}, {1,3}, {2,4}, {5,6},
    {9,10}, {1,2}, {3,4}, {5,7}, {6,8}, {2,3}, {4,5}, {6,7}, {8,9}, {3,4}, {5,6},
    // n = 14: 51 comparators
    {0,1}, {2,3}, {4,5}, {6,7}, {8,9}, {10,11}, {12,13}, {0,2}, {1,3}, {4,8}, {5,9},
    {10,12}, {11,13}, {0,4}, {1,2}, {3,7}, {5,8}, {6,10}, {9,13}, {11,12}, {0,6}, {1,5},
    {3,9}, {4,10}, {7,13}, {8,12}, {2,10}, {3,11}, {4,6}, {7,9}, {1,3}, {2,8}, {5,11},
    {6,7}, {10,12}, {1,4}, {2,6}, {3,5}, {7,11}, {8,10}, {9,12}, {2,4}, {3,6}, {5,8},
    {7,10}, {9,11}, {3,4}, {5,6}, {7,8}, {9,10}, {6,7},
    // n = 15: 56 comparators
    {1,2}, {3,10}, {4,14}, {5,8}, {6,13}, {7,12}, {9,11}, {0,14}, {1,5}, {2,8}, {3,7},
    {6,9}, {10,12}, {11,13}, {0,7}, {1,6}, {2,9}, {4,10}, {5,11}, {8,13}, {12,14}, {0,6},
    {2,4}, {3,5}, {7,11}, {8,10}, {9,12}, {13,14}, {0,3}, {1,2}, {4,7}, {5,9}, {6,8},
    {10,11}, {12,13}, {0,1}, {2,3}, {4,6}, {7,9}, {10,12}, {11,13}, {1,2}, {3,5}, {8,10},
    {11,12}, {3,4}, {5,6}, {7,8}, {9,10}, {2,3}, {4,5}, {6,7}, {8,9}, {10,11}, {5,6},
    {7,8},
    // n = 16: 60 comparators
    {0,13}, {1,12}, {2,15}, {3,14}, {4,8}, {5,6}, {7,11}, {9,10}, {0,5}, {1,7}, {2,9},
    {3,4}, {6,13}, {8,14}, {10,15}, {11,12}, {0,1}, {2,3}, {4,5}, {6,8}, {7,9}, {10,11},
    {12,13}, {14,15}, {0,2}, {1,3}, {4,10}, {5,11}, {6,7}, {8,9}, {12,14}, {13,15}, {1,2},
    {3,12}, {4,6}, {5,7}, {8,10}, {9,11}, {13,14}, {1,4}, {2,6}, {5,8}, {7,10}, {9,13},
    {11,14}, {2,4}, {3,6}, {9,12}, {11,13}, {3,5}, {6,8}, {7,9}, {10,12}, {3,4}, {5,6},
    {7,8}, {9,10}, {11,12}, {6,7}, {8,9},
};
inline constexpr uint16_t kSmallNetOffset[] = {0, 25, 54, 89, 128, 173, 224, 280, 340};

// Batcher's odd-even merge of two sorted runs of any lengths (D. Knuth, TAOCP
// vol. 3, §5.3.4): A on channels a0 + k*s (k < ma), B on b0 + k*s (k < mb),
// every channel of A below every channel of B. The even ranks of both runs and
// the odd ranks are merged recursively; then each comparator puts the smaller
// value on the lower channel, which leaves the merged ranks on the channels of
// A and B in ascending channel order.
template <class Swap>
inline void odd_even_merge(std::size_t a0, std::size_t ma, std::size_t b0, std::size_t mb,
                           std::size_t s, Swap& swap) {
    if (ma == 0 || mb == 0) return;
    if (ma == 1 && mb == 1) { swap(a0, b0); return; }
    odd_even_merge(a0, (ma + 1) / 2, b0, (mb + 1) / 2, 2 * s, swap);   // even ranks: v
    odd_even_merge(a0 + s, ma / 2, b0 + s, mb / 2, 2 * s, swap);       // odd ranks: w
    // v = A's even ranks then B's, w = A's odd ranks then B's; compare w[k], v[k+1].
    const std::size_t ae = (ma + 1) / 2, ao = ma / 2;
    const std::size_t nv = ae + (mb + 1) / 2, nw = ao + mb / 2;
    for (std::size_t k = 0; k < nw && k + 1 < nv; ++k) {
        const std::size_t w = k < ao ? a0 + (2 * k + 1) * s : b0 + (2 * (k - ao) + 1) * s;
        const std::size_t v = k + 1 < ae ? a0 + 2 * (k + 1) * s : b0 + 2 * (k + 1 - ae) * s;
        swap(std::min(w, v), std::max(w, v));
    }
}

// Sort channels [lo, lo + n): sort the halves (the larger one first), then merge.
template <class Swap>
inline void sort_range(std::size_t lo, std::size_t n, Swap& swap) {
    if (n < 2) return;
    if (n >= 9 && n <= 16) {
        for (std::size_t k = kSmallNetOffset[n - 9]; k < kSmallNetOffset[n - 8]; ++k)
            swap(lo + kSmallNet[k][0], lo + kSmallNet[k][1]);
        return;
    }
    const std::size_t m = (n + 1) / 2;
    sort_range(lo, m, swap);
    sort_range(lo + m, n - m, swap);
    odd_even_merge(lo, m, lo + m, n - m, 1, swap);
}

}  // namespace sort_detail

// Order (a, b): ascending -> a = min, b = max; descending -> the reverse.
template <class V>
inline void compare_swap(V& a, V& b, bool ascending = true) {
    if constexpr (sort_detail::int_kind<V> != 0) {
        auto out_of_order = ascending ? sort_detail::exchange_bit(b, a) : sort_detail::exchange_bit(a, b);
        sort_detail::swap_if(out_of_order, a, b);
    } else {
        auto out_of_order = ascending ? (b < a) : (a < b);
        sort_detail::swap_if(out_of_order, a, b);
    }
}

// Key + payload: the compare-swap decision comes from the keys; the same
// conditional swap is applied to the carried data (which needs only `select`).
// Both pairs are exchanged as copies and stored last, so the payload may alias
// the keys.
template <class K, class P>
inline void compare_swap(K& ka, K& kb, P& pa, P& pb, bool ascending = true) {
    auto out_of_order = ascending ? (kb < ka) : (ka < kb);
    K nka = ka, nkb = kb;
    P npa = pa, npb = pb;
    sort_detail::swap_if(out_of_order, nka, nkb);
    sort_detail::swap_if(out_of_order, npa, npb);
    ka = nka; kb = nkb; pa = npa; pb = npb;
}

// The sorting network: invoke swap(i, j) (i < j) for each compare-swap, in an
// order that respects their dependencies. Batcher's odd-even mergesort with
// balanced halves, down to the smallest known networks for 9..16 inputs (for
// n <= 8, Batcher's networks are already the smallest).
template <class Swap>
inline void batcher_network(int64_t count, Swap&& swap) {
    expecting(count >= 0, "batcher_network: negative count");
    sort_detail::sort_range(0, (std::size_t)count, swap);
}

// Sort `v` in place (ascending by default).
template <class V>
inline void sort(std::vector<V>& v, bool ascending = true) {
    batcher_network(v.size(), [&](std::size_t i, std::size_t j) { compare_swap(v[i], v[j], ascending); });
}
template <class V>
inline void sort(V* a, int64_t n, bool ascending = true) {
    expecting(n >= 0, "sort: negative count");
    batcher_network(n,
                    [&](std::size_t i, std::size_t j) { compare_swap(a[i], a[j], ascending); });
}

// Sort `keys` (ascending by default), moving `data[i]` alongside `keys[i]`.
// (Distinct name from sort() so an unqualified call over std::vector doesn't
// collide with std::sort(first, last) via ADL.)
template <class K, class P>
inline void sort_by_key(std::vector<K>& keys, std::vector<P>& data, bool ascending = true) {
    expecting(keys.size() == data.size(),
              "sort_by_key: keys and data must have equal length");
    batcher_network(keys.size(),
                    [&](std::size_t i, std::size_t j) { compare_swap(keys[i], keys[j], data[i], data[j], ascending); });
}
template <class K, class P>
inline void sort_by_key(K* keys, P* data, int64_t n, bool ascending = true) {
    expecting(n >= 0, "sort_by_key: negative count");
    batcher_network(n,
                    [&](std::size_t i, std::size_t j) {
                        compare_swap(keys[i], keys[j], data[i], data[j], ascending);
                    });
}

}  // namespace emp
#endif  // EMP_CIRCUIT_SORT_H__
