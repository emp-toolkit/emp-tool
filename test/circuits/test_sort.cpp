// Data-oblivious sort over circuit values (sort.h): values are fed and the
// sorted results revealed through a ClearSession, checked against std::sort.
// Covers a non-power-of-2 length with duplicates, descending, key+payload,
// random lengths and values for the integer and float families, the network
// on every 0-1 input, and the AND count of a compare-swap.
#include "emp-tool/circuits/float.h"
#include "emp-tool/circuits/signed_int.h"
#include "emp-tool/circuits/sort.h"
#include "emp-tool/circuits/unsigned_int.h"
#include "emp-tool/ir/context/count.h"
#include "emp-tool/ir/session/clear_session.h"
#include "emp-tool/runtime/core/constants.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <random>
#include <utility>
#include <vector>
using namespace emp;
using Ctx = ClearSession::ctx_t;

static int bad = 0;
static void chk(const char* w, bool ok) { if (!ok) { printf("  [FAIL] %s\n", w); ++bad; } }

// ---- example: feed values, sort obliviously, reveal the ordered result ----

static void example() {
  ClearSession sess;
  using U = UInt_T<Ctx, 16>;
  std::vector<U> v;
  for (uint16_t x : {42, 7, 100, 3}) v.push_back(sess.input<U>(ALICE, x));
  sort(v);
  uint16_t want[] = {3, 7, 42, 100};
  bool ok = true;
  for (int i = 0; i < 4; ++i) ok = ok && (sess.reveal(v[i], PUBLIC).value() == want[i]);
  chk("example sort", ok);
}

template <int N, class Clear>
static void check_sort(const std::vector<Clear>& in, const char* name) {
  ClearSession sess;
  using U = UInt_T<Ctx, N>;
  std::vector<U> v;
  for (auto x : in) v.push_back(sess.input<U>(ALICE, x));
  sort(v);
  std::vector<Clear> ref = in;
  std::sort(ref.begin(), ref.end());
  bool ok = true;
  for (size_t i = 0; i < ref.size(); ++i)
    ok = ok && ((uint64_t)sess.reveal(v[i], PUBLIC).value() == (uint64_t)ref[i]);
  chk(name, ok);
}

// ---- random lengths and values against std::sort ----
// Small value ranges force duplicates. sort_by_key is not stable, so its result
// is checked as sorted keys carrying the same (key, payload) pairs.
static void sweep_random() {
  std::mt19937_64 rng(2024);
  bool ok_u = true, ok_i = true, ok_f = true, ok_kv = true;
  for (int n = 0; n <= 40; ++n)
    for (int t = 0; t < 4; ++t) {
      ClearSession sess;
      using U = UInt_T<Ctx, 16>; using I = Int_T<Ctx, 16>; using F = Float_T<Ctx, 32>;
      const bool asc = t < 2;
      std::vector<uint16_t> u(n); std::vector<int16_t> s(n); std::vector<float> f(n);
      for (int i = 0; i < n; ++i) {
        u[i] = (uint16_t)(t % 2 ? rng() % 5 : rng());
        s[i] = (int16_t)(t % 2 ? (int)(rng() % 7) - 3 : (int)(uint16_t)rng());
        f[i] = (float)((int)(rng() % 9) - 4) / 2.0f + (t % 2 ? 0.0f : 0.25f);
        if (f[i] == 0.0f) f[i] = 0.0f;   // +0 only: -0 == +0 leaves their order open
      }
      std::vector<U> cu; std::vector<I> ci; std::vector<F> cf; std::vector<U> ck, cd;
      for (int i = 0; i < n; ++i) {
        cu.push_back(sess.input<U>(ALICE, u[i])); ci.push_back(sess.input<I>(ALICE, s[i]));
        cf.push_back(sess.input<F>(ALICE, f[i]));
        ck.push_back(sess.input<U>(ALICE, u[i])); cd.push_back(sess.input<U>(ALICE, (uint16_t)i));
      }
      sort(cu, asc); sort(ci, asc); sort(cf, asc); sort_by_key(ck, cd, asc);
      auto ru = u; auto rs = s; auto rf = f;
      std::sort(ru.begin(), ru.end()); std::sort(rs.begin(), rs.end()); std::sort(rf.begin(), rf.end());
      if (!asc) { std::reverse(ru.begin(), ru.end()); std::reverse(rs.begin(), rs.end()); std::reverse(rf.begin(), rf.end()); }
      std::multimap<uint16_t, uint16_t> want, got;
      for (int i = 0; i < n; ++i) {
        ok_u = ok_u && sess.reveal(cu[i], PUBLIC).value() == ru[i];
        ok_i = ok_i && sess.reveal(ci[i], PUBLIC).value() == rs[i];
        ok_f = ok_f && sess.reveal(cf[i], PUBLIC).value() == rf[i];
        const uint16_t k = (uint16_t)sess.reveal(ck[i], PUBLIC).value();
        ok_kv = ok_kv && k == ru[i];
        want.insert({u[i], (uint16_t)i});
        got.insert({k, (uint16_t)sess.reveal(cd[i], PUBLIC).value()});
      }
      std::vector<std::pair<uint16_t, uint16_t>> w(want.begin(), want.end()), g(got.begin(), got.end());
      std::sort(w.begin(), w.end()); std::sort(g.begin(), g.end());
      ok_kv = ok_kv && w == g;
    }
  chk("random uint16 sort", ok_u);
  chk("random int16 sort", ok_i);
  chk("random float sort", ok_f);
  chk("random sort_by_key", ok_kv);
}

// ---- the network alone: every 0-1 input (0-1 principle), random lengths ----
static size_t comparators(int64_t n) { size_t k = 0; batcher_network(n, [&](size_t, size_t) { ++k; }); return k; }
static void sweep_network() {
  bool ok01 = true;
  for (int n = 0; n <= 16; ++n)
    for (uint32_t x = 0; x < (1u << n) && ok01; ++x) {
      std::vector<int> v(n);
      for (int i = 0; i < n; ++i) v[i] = (x >> i) & 1;
      batcher_network(n, [&](size_t i, size_t j) { if (v[i] > v[j]) std::swap(v[i], v[j]); });
      ok01 = std::is_sorted(v.begin(), v.end());
    }
  chk("network sorts every 0-1 input, n <= 16", ok01);
  std::mt19937_64 rng(7);
  bool ok = true;
  for (int n = 17; n <= 300 && ok; ++n)
    for (int t = 0; t < 8 && ok; ++t) {
      std::vector<uint64_t> v(n);
      for (auto& x : v) x = t % 2 ? rng() % 4 : rng();
      batcher_network(n, [&](size_t i, size_t j) { if (v[i] > v[j]) std::swap(v[i], v[j]); });
      ok = std::is_sorted(v.begin(), v.end());
    }
  chk("network sorts random inputs, n = 17..300", ok);
  // The smallest known networks for 9..16 inputs, merged with balanced halves.
  const size_t small[] = {25, 29, 35, 39, 45, 51, 56, 60};
  bool sizes = true;
  for (int n = 9; n <= 16; ++n) sizes = sizes && comparators(n) == small[n - 9];
  chk("comparators: smallest known for n = 9..16", sizes);
  chk("comparators: 73 / 185 / 23871 at n = 17 / 32 / 1024",
      comparators(17) == 73 && comparators(32) == 185 && comparators(1024) == 23871);
}

// ---- cost: a compare-swap is one comparison plus one AND per exchanged bit ----
static void gate_counts() {
  using U32 = UInt_T<CountCtx, 32>;
  static CountCtx::Wire z[32] = {};
  auto ands = [](auto&& build) { CountCtx c; build(c); return c.ands; };
  using I32 = Int_T<CountCtx, 32>;
  // Integers decide the exchange with a 31-AND comparison that may answer either
  // way on equal values; carrying a payload needs the exact 32-AND comparison.
  chk("compare_swap uint32: 63 ANDs",
      ands([](CountCtx& c) { U32 a = U32::from_wires(c, z), b = U32::from_wires(c, z); compare_swap(a, b); }) == 63);
  chk("compare_swap int32: 63 ANDs",
      ands([](CountCtx& c) { I32 a = I32::from_wires(c, z), b = I32::from_wires(c, z); compare_swap(a, b); }) == 63);
  chk("sort 16 x uint32: 60 compare-swaps",
      ands([](CountCtx& c) { std::vector<U32> v(16, U32::from_wires(c, z)); sort(v); }) == 60 * 63);
  chk("sort_by_key 16 x uint32 -> uint32",
      ands([](CountCtx& c) { std::vector<U32> k(16, U32::from_wires(c, z)), d = k; sort_by_key(k, d); }) == 60 * 96);
}

int main() {
  example();

  check_sort<16>(std::vector<uint16_t>{42, 7, 100, 3, 3, 255, 1, 99, 50, 12}, "uint16 x10");
  check_sort<8>(std::vector<uint8_t>{5, 4, 3, 2, 1}, "uint8 x5 reverse");
  check_sort<16>(std::vector<uint16_t>{1}, "singleton");
  check_sort<16>(std::vector<uint16_t>{9, 9, 9, 9}, "all-equal");

  // descending.
  {
    ClearSession sess; using U = UInt_T<Ctx, 16>;
    std::vector<uint16_t> in{5, 1, 9, 3, 7};
    std::vector<U> v; for (auto x : in) v.push_back(sess.input<U>(ALICE, x));
    sort(v, /*ascending=*/false);
    std::sort(in.rbegin(), in.rend());
    bool ok = true;
    for (size_t i = 0; i < in.size(); ++i)
      ok = ok && (sess.reveal(v[i], PUBLIC).value() == in[i]);
    chk("descending", ok);
  }

  // key + payload (data carried alongside the key).
  {
    ClearSession sess; using U = UInt_T<Ctx, 16>;
    std::vector<U> keys, data;
    for (uint16_t x : {30, 10, 20}) keys.push_back(sess.input<U>(ALICE, x));
    for (uint16_t x : {300, 100, 200}) data.push_back(sess.input<U>(ALICE, x));
    sort_by_key(keys, data);                            // ascending by key
    uint16_t ek[] = {10, 20, 30}, ed[] = {100, 200, 300};
    bool ok = true;
    for (int i = 0; i < 3; ++i)
      ok = ok && (sess.reveal(keys[i], PUBLIC).value() == ek[i] &&
                  sess.reveal(data[i], PUBLIC).value() == ed[i]);
    chk("key+payload", ok);
  }

  // Pointer overloads operate directly on the supplied arrays.
  {
    ClearSession sess; using U = UInt_T<Ctx, 16>;
    U values[] = {
        sess.input<U>(ALICE, 8), sess.input<U>(ALICE, 2),
        sess.input<U>(ALICE, 5), sess.input<U>(ALICE, 1)};
    sort(values, 4);
    uint16_t want[] = {1, 2, 5, 8};
    bool ok = true;
    for (int i = 0; i < 4; ++i)
      ok = ok && (sess.reveal(values[i], PUBLIC).value() == want[i]);
    chk("pointer sort", ok);
  }
  {
    ClearSession sess; using U = UInt_T<Ctx, 16>;
    U keys[] = {
        sess.input<U>(ALICE, 4), sess.input<U>(ALICE, 1), sess.input<U>(ALICE, 3)};
    U data[] = {
        sess.input<U>(ALICE, 40), sess.input<U>(ALICE, 10), sess.input<U>(ALICE, 30)};
    sort_by_key(keys, data, 3);
    uint16_t want_keys[] = {1, 3, 4}, want_data[] = {10, 30, 40};
    bool ok = true;
    for (int i = 0; i < 3; ++i)
      ok = ok && (sess.reveal(keys[i], PUBLIC).value() == want_keys[i] &&
                  sess.reveal(data[i], PUBLIC).value() == want_data[i]);
    chk("pointer sort_by_key", ok);
  }
  {
    ClearSession sess; using U = UInt_T<Ctx, 16>;
    U values[] = {
        sess.input<U>(ALICE, 3), sess.input<U>(ALICE, 9),
        sess.input<U>(ALICE, 3), sess.input<U>(ALICE, 7), sess.input<U>(ALICE, 1)};
    sort(values, 5, /*ascending=*/false);
    uint16_t want[] = {9, 7, 3, 3, 1};
    bool ok = true;
    for (int i = 0; i < 5; ++i)
      ok = ok && (sess.reveal(values[i], PUBLIC).value() == want[i]);
    chk("pointer sort descending", ok);
  }
  {
    ClearSession sess; using U = UInt_T<Ctx, 16>;
    U keys[] = {
        sess.input<U>(ALICE, 2), sess.input<U>(ALICE, 6), sess.input<U>(ALICE, 4)};
    U data[] = {
        sess.input<U>(ALICE, 20), sess.input<U>(ALICE, 60), sess.input<U>(ALICE, 40)};
    sort_by_key(keys, data, 3, /*ascending=*/false);
    uint16_t want_keys[] = {6, 4, 2}, want_data[] = {60, 40, 20};
    bool ok = true;
    for (int i = 0; i < 3; ++i)
      ok = ok && (sess.reveal(keys[i], PUBLIC).value() == want_keys[i] &&
                  sess.reveal(data[i], PUBLIC).value() == want_data[i]);
    chk("pointer sort_by_key descending", ok);
  }
  // The payload array may be the key array itself.
  {
    ClearSession sess; using U = UInt_T<Ctx, 16>;
    U keys[] = {
        sess.input<U>(ALICE, 5), sess.input<U>(ALICE, 1), sess.input<U>(ALICE, 4),
        sess.input<U>(ALICE, 2), sess.input<U>(ALICE, 3)};
    sort_by_key(keys, keys, 5);
    bool ok = true;
    for (int i = 0; i < 5; ++i)
      ok = ok && (sess.reveal(keys[i], PUBLIC).value() == (uint16_t)(i + 1));
    chk("pointer sort_by_key with keys as payload", ok);
  }
  {
    using U = UInt_T<Ctx, 16>;
    U* empty = nullptr;
    sort(empty, 0);
    sort(empty, 0, /*ascending=*/false);
    sort_by_key(empty, empty, 0);
    sort_by_key(empty, empty, 0, /*ascending=*/false);
    chk("empty pointer ranges", true);
  }
  sweep_random();
  sweep_network();
  gate_counts();

  printf("test_sort: %s\n", bad ? "FAILED" : "PASS");
  return bad ? 1 : 0;
}
