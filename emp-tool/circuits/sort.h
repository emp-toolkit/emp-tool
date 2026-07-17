#ifndef EMP_CIRCUIT_SORT_H__
#define EMP_CIRCUIT_SORT_H__

// Data-oblivious sorting over circuit values. Works for any value V supporting
// `Bit_T<Ctx> operator<(const V&)` and `V select(Bit_T<Ctx>, V)` (UInt_T / Int_T /
// Float_T / BitVec_T's as_uint, …). The compare-swap schedule is a fixed Batcher
// odd-even mergesort network (any length), so the gate stream is identical for
// every input — independent of the data and of `ascending`.

#include "emp-tool/runtime/core/utils.h"   // error()
#include <cstddef>
#include <vector>

namespace emp {

// Order (a, b): ascending -> a = min, b = max; descending -> the reverse.
template <class V>
inline void compare_swap(V& a, V& b, bool ascending = true) {
    auto out_of_order = ascending ? (b < a) : (a < b);
    V lo = a.select(out_of_order, b);
    V hi = b.select(out_of_order, a);
    a = lo; b = hi;
}

// Key + payload: the compare-swap decision comes from the keys; the same
// conditional swap is applied to the carried data (which needs only `select`).
template <class K, class P>
inline void compare_swap(K& ka, K& kb, P& pa, P& pb, bool ascending = true) {
    auto out_of_order = ascending ? (kb < ka) : (ka < kb);
    K nka = ka.select(out_of_order, kb), nkb = kb.select(out_of_order, ka);
    P npa = pa.select(out_of_order, pb), npb = pb.select(out_of_order, pa);
    ka = nka; kb = nkb; pa = npa; pb = npb;
}

// Batcher odd-even mergesort network: invoke swap(i, j) for each compare-swap.
template <class Swap>
inline void batcher_network(std::size_t n, Swap&& swap) {
    for (std::size_t p = 1; p < n; p *= 2)
        for (std::size_t k = p; k >= 1; k /= 2)
            for (std::size_t j = k % p; j + k < n; j += 2 * k)
                for (std::size_t i = 0; i < k && i + j + k < n; ++i)
                    if (((i + j) / (2 * p)) == ((i + j + k) / (2 * p)))
                        swap(i + j, i + j + k);
}

// Sort `v` in place (ascending by default).
template <class V>
inline void sort(std::vector<V>& v, bool ascending = true) {
    batcher_network(v.size(), [&](std::size_t i, std::size_t j) { compare_swap(v[i], v[j], ascending); });
}
template <class V>
inline void sort(V* a, std::size_t n, bool ascending = true) {
    std::vector<V> v(a, a + n); sort(v, ascending);
    for (std::size_t i = 0; i < n; ++i) a[i] = v[i];
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
inline void sort_by_key(K* keys, P* data, std::size_t n, bool ascending = true) {
    std::vector<K> kv(keys, keys + n); std::vector<P> dv(data, data + n);
    sort_by_key(kv, dv, ascending);
    for (std::size_t i = 0; i < n; ++i) { keys[i] = kv[i]; data[i] = dv[i]; }
}

}  // namespace emp
#endif  // EMP_CIRCUIT_SORT_H__
