// BitVec_T<Ctx,N>: the idiomatic fixed-width bit-block (the value crypto
// blocks like AES/SHA take and return). Read example() first; it shows how a
// user feeds a clear value as an owner's input, computes with bitwise/shift/
// slice/concat ops, and reveals. The rest sweeps each operation against the
// matching std::bitset / native-integer semantics. Inputs are fed and results
// revealed through a ClearSession — the I/O boundary; the values themselves are
// pure context-bound circuit values.
//
// BitVec is the natural multi-bit block; for an arithmetic view of the same
// wires use as_uint() (zero gates). clear_t is std::array<bool,N>, LSB at
// index 0.
#include "emp-tool/ir/session/clear_session.h"
#include "emp-tool/runtime/core/constants.h"
#include "emp-tool/circuits/bitvec.h"
#include "emp-tool/circuits/unsigned_int.h"
#include <array>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <random>
using namespace emp;
using Ctx = ClearSession::ctx_t;

template <int N> using BV  = BitVec_T<Ctx, N>;
using BV128 = BitVec_T<Ctx, 128>;

static int g_fail = 0;
static void check(const char* name, bool ok) {
  if (!ok) { printf("  [FAIL] %s\n", name); ++g_fail; }
}
static void check_eq(const char* name, uint64_t got, uint64_t want) {
  if (got != want) {
    printf("  [FAIL] %s: got %llu want %llu\n", name,
           (unsigned long long)got, (unsigned long long)want);
    ++g_fail;
  }
}

// clear value <-> bit array, LSB at index 0 (the BitVec clear_t convention).
template <int N> static std::array<bool, N> to_bits(uint64_t v) {
  std::array<bool, N> b{};
  for (int i = 0; i < N; ++i) b[i] = (i < 64) ? ((v >> i) & 1) : 0;
  return b;
}
template <int N> static uint64_t from_bits(const std::array<bool, N>& b) {
  uint64_t v = 0;
  for (int i = 0; i < N && i < 64; ++i) v |= (uint64_t)(b[i] & 1) << i;
  return v;
}

// ---- example -------------------------------------------------------------
// How a normal user works with BitVec: build from a clear value owned by a
// party, combine with operators, read windows out, and reveal.
static void example() {
  ClearSession sess;
  using Byte = BitVec_T<Ctx, 8>;
  using Word = BitVec_T<Ctx, 32>;

  // Feed two 32-bit values as ALICE's and BOB's inputs (public wires here).
  auto a = sess.input<Word>(ALICE, to_bits<32>(0xCAFEBABE));
  auto b = sess.input<Word>(BOB, to_bits<32>(0xDEADBEEF));

  check("example xor", from_bits<32>(sess.reveal(a ^ b, PUBLIC).value()) == (0xCAFEBABEu ^ 0xDEADBEEFu));
  check("example and", from_bits<32>(sess.reveal(a & b, PUBLIC).value()) == (0xCAFEBABEu & 0xDEADBEEFu));
  check("example not", from_bits<32>(sess.reveal(~a, PUBLIC).value()) == (uint32_t)~0xCAFEBABEu);
  check("example shl", from_bits<32>(sess.reveal(a << 8, PUBLIC).value()) == (uint32_t)(0xCAFEBABEu << 8));

  // Pull the low byte out as its own block, and read a single bit as a Bit_T.
  Byte lo = a.slice<0, 8>();
  check("example slice", from_bits<8>(sess.reveal(lo, PUBLIC).value()) == 0xBE);
  check("example index", sess.reveal(a[0], PUBLIC).value() == ((0xCAFEBABEu) & 1));

  // The same wires viewed as an unsigned integer (zero gates) — how crypto
  // output flows into arithmetic.
  check("example as_uint", sess.reveal<uint32_t>(a.as_uint(), PUBLIC).value() == 0xCAFEBABEu);
}

// ---- bitwise: & | ^ ~ ----------------------------------------------------
template <int N> static void sweep_bitwise(std::mt19937_64& rng, const char* tag) {
  ClearSession sess;
  using V = BitVec_T<Ctx, N>;
  const uint64_t mask = (N >= 64) ? ~0ull : ((1ull << N) - 1);
  for (int it = 0; it < 1000; ++it) {
    uint64_t ia = rng() & mask, ib = rng() & mask;
    auto a = sess.input<V>(ALICE, to_bits<N>(ia));
    auto b = sess.input<V>(BOB, to_bits<N>(ib));
    check_eq(tag, from_bits<N>(sess.reveal(a & b, PUBLIC).value()), ia & ib);
    check_eq(tag, from_bits<N>(sess.reveal(a | b, PUBLIC).value()), ia | ib);
    check_eq(tag, from_bits<N>(sess.reveal(a ^ b, PUBLIC).value()), ia ^ ib);
    check_eq(tag, from_bits<N>(sess.reveal(~a, PUBLIC).value()), (~ia) & mask);
  }
}

// ---- equality: == / != ---------------------------------------------------
template <int N> static void sweep_equality(std::mt19937_64& rng) {
  ClearSession sess;
  using V = BitVec_T<Ctx, N>;
  const uint64_t mask = (N >= 64) ? ~0ull : ((1ull << N) - 1);
  for (int it = 0; it < 500; ++it) {
    uint64_t v = rng() & mask;
    auto a = sess.input<V>(ALICE, to_bits<N>(v));
    auto same = sess.input<V>(BOB, to_bits<N>(v));
    auto diff = sess.input<V>(BOB, to_bits<N>((v ^ 1) & mask));   // flip one bit
    check("eq same", sess.reveal(a == same, PUBLIC).value() == true);
    check("eq same !ne", sess.reveal(a != same, PUBLIC).value() == false);
    check("ne diff", sess.reveal(a != diff, PUBLIC).value() == true);
    check("ne diff !eq", sess.reveal(a == diff, PUBLIC).value() == false);
  }
}

// ---- select (oblivious bitwise mux) --------------------------------------
template <int N> static void sweep_select(std::mt19937_64& rng) {
  ClearSession sess;
  Ctx& ctx = sess.ctx();
  using V = BitVec_T<Ctx, N>;
  using B = Bit_T<Ctx>;
  const uint64_t mask = (N >= 64) ? ~0ull : ((1ull << N) - 1);
  for (int it = 0; it < 500; ++it) {
    uint64_t va = rng() & mask, vb = rng() & mask;
    auto a = sess.input<V>(ALICE, to_bits<N>(va));
    auto b = sess.input<V>(BOB, to_bits<N>(vb));
    // a.select(sel, t): sel ? t : a.
    check_eq("select false", from_bits<N>(sess.reveal(a.select(B::constant(ctx, false), b), PUBLIC).value()), va);
    check_eq("select true",  from_bits<N>(sess.reveal(a.select(B::constant(ctx, true),  b), PUBLIC).value()), vb);
  }
}

// ---- public-amount shifts, including >= width ----------------------------
static void sweep_shifts() {
  ClearSession sess;
  using V = BitVec_T<Ctx, 32>;
  const uint32_t vs[] = {0u, 1u, 0xFFFFFFFFu, 0x80000000u, 0xCAFEBABEu, 0x00000001u};
  for (uint32_t v : vs)
    for (int s = 0; s <= 33; ++s) {   // 32 and 33 exercise shift >= width
      auto a = sess.input<V>(ALICE, to_bits<32>(v));
      uint32_t want_l = (s >= 32) ? 0u : (uint32_t)(v << s);
      uint32_t want_r = (s >= 32) ? 0u : (uint32_t)(v >> s);
      check_eq("shl", from_bits<32>(sess.reveal(a << s, PUBLIC).value()), want_l);
      check_eq("shr", from_bits<32>(sess.reveal(a >> s, PUBLIC).value()), want_r);
    }
  for (uint32_t v : vs) {
    // Extreme public amount: saturates to zero — and the i + s index
    // arithmetic must not overflow int.
    auto a = sess.input<V>(ALICE, to_bits<32>(v));
    check_eq("shl INT_MAX", from_bits<32>(sess.reveal(a << INT_MAX, PUBLIC).value()), 0u);
    auto b = sess.input<V>(ALICE, to_bits<32>(v));
    check_eq("shr INT_MAX", from_bits<32>(sess.reveal(b >> INT_MAX, PUBLIC).value()), 0u);
  }
}

// ---- slice / concat ------------------------------------------------------
static void sweep_slice_concat(std::mt19937_64& rng) {
  ClearSession sess;
  using V32 = BitVec_T<Ctx, 32>;
  for (int it = 0; it < 200; ++it) {
    uint32_t lo = (uint32_t)rng(), hi = (uint32_t)rng();
    auto L = sess.input<V32>(ALICE, to_bits<32>(lo));
    auto H = sess.input<V32>(BOB, to_bits<32>(hi));

    // concat: receiver is the low half, argument goes above it.
    BitVec_T<Ctx, 64> c = L.concat(H);
    check("concat width", c.width() == 64);
    uint64_t want = (uint64_t)lo | ((uint64_t)hi << 32);
    check_eq("concat value", from_bits<64>(sess.reveal(c, PUBLIC).value()), want);

    // compile-time slices read fixed windows of the 64-bit block.
    check_eq("slice [0,8)",   from_bits<8>(sess.reveal(c.slice<0, 8>(), PUBLIC).value()),   (want >> 0)  & 0xff);
    check_eq("slice [8,16)",  from_bits<8>(sess.reveal(c.slice<8, 16>(), PUBLIC).value()),  (want >> 8)  & 0xff);
    check_eq("slice [16,32)", from_bits<16>(sess.reveal(c.slice<16, 32>(), PUBLIC).value()),(want >> 16) & 0xffff);
    check_eq("slice [32,64)", from_bits<32>(sess.reveal(c.slice<32, 64>(), PUBLIC).value()),(want >> 32) & 0xffffffff);
  }
}

// ---- as_uint round-trip with UInt_T (same wires, zero gates) -------------
static void sweep_as_uint(std::mt19937_64& rng) {
  ClearSession sess;
  Ctx& ctx = sess.ctx();
  using V = BitVec_T<Ctx, 32>;
  for (int it = 0; it < 500; ++it) {
    uint32_t v = (uint32_t)rng();
    auto a = sess.input<V>(ALICE, to_bits<32>(v));
    // BitVec -> UInt keeps the value and lets it enter arithmetic.
    UInt_T<Ctx, 32> u = a.as_uint();
    check_eq("as_uint value", sess.reveal<uint32_t>(u, PUBLIC).value(), v);
    check_eq("as_uint +1", sess.reveal<uint32_t>(u + UInt_T<Ctx, 32>::constant(ctx, 1), PUBLIC).value(), v + 1u);
  }
}

// ---- operator[] yields a Bit_T at each position --------------------------
static void sweep_index(std::mt19937_64& rng) {
  ClearSession sess;
  using V = BitVec_T<Ctx, 32>;
  for (int it = 0; it < 200; ++it) {
    uint32_t v = (uint32_t)rng();
    auto a = sess.input<V>(ALICE, to_bits<32>(v));
    for (int i = 0; i < 32; ++i) {
      Bit_T<Ctx> bit = a[i];   // a Bit_T reference to position i
      check("index bit", sess.reveal(bit, PUBLIC).value() == (bool)((v >> i) & 1));
    }
  }
}

// ---- encode/decode codec round-trip --------------------------------------
// The codec is a pure static value-level transform (no I/O boundary): it maps
// clear_t <-> wire bits, so it is exercised directly, not through the session.
static void sweep_codec() {
  using V = BitVec_T<Ctx, 8>;
  for (int v = 0; v < 256; ++v) {
    auto clear = to_bits<8>((uint64_t)v);
    auto enc = V::encode(clear);
    bool buf[8];
    for (int i = 0; i < 8; ++i) buf[i] = enc[i];
    check("codec round-trip", V::decode(buf) == clear);
  }
}

int main() {
  example();

  std::mt19937_64 rng(0xB17EC0DEull);   // fixed seed: deterministic sweeps

  // Bitwise / equality / select at width 8, 32, and 128 (the AES block width).
  sweep_bitwise<8>(rng, "bitwise w8");
  sweep_bitwise<32>(rng, "bitwise w32");
  sweep_bitwise<128>(rng, "bitwise w128");
  sweep_equality<8>(rng);
  sweep_equality<32>(rng);
  sweep_equality<128>(rng);
  sweep_select<8>(rng);
  sweep_select<32>(rng);
  sweep_select<128>(rng);

  sweep_shifts();
  sweep_slice_concat(rng);
  sweep_as_uint(rng);
  sweep_index(rng);
  sweep_codec();

  printf("test_bitvec: %s\n", g_fail ? "FAILED" : "PASS");
  return g_fail ? 1 : 0;
}
