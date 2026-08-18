// SHA3-256 / Keccak-f[1600] over the BooleanContext value layer, example-driven.
//
//   example()              -- sha3_256("abc") on Ctx via BitVec_T inputs
//   keccak_f1600 checks    -- the permutation vs a plain-C reference (all-zero
//                             state + seeded-random states), lane by lane
//   SHA3-256 KAT vectors   -- "" (a7ffc6f8..) and "abc" (3a985da7..)
//   OpenSSL cross-check     -- vs EVP_sha3_256 at rate-block boundary lengths
//                             (rate = 136 bytes): 0,1,135,136,137,271,272
//   AND count               -- sha3_256(BitVec<256>) == 38400, plus record==live
//
// The readable sections feed inputs and reveal results through a ClearSession —
// the I/O boundary — and call the kernels on sess.ctx(). A tiny generic driver
// is kept ONLY for the record/replay equivalence and the gate count (legitimate
// low-level IR plumbing, not a user-facing example).
#include "emp-tool/ir/session/clear_session.h"
#include "emp-tool/runtime/core/constants.h"
#include "emp-tool/circuits/crypto/keccak.h"
#include "test_crypto_common.h"
#include <openssl/evp.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <type_traits>
#include <vector>
using namespace emp;
using namespace emp::circuit::crypto;
using namespace test_crypto;
using Ctx = ClearSession::ctx_t;

using BV24  = BitVec_T<Ctx, 24>;
using BV256 = BitVec_T<Ctx, 256>;
using U64   = UInt_T<Ctx, 64>;

// --- local check helpers ---------------------------------------------------
static int g_fail = 0;
static void check(const char* name, bool ok) {
  if (!ok) { printf("  [FAIL] %s\n", name); ++g_fail; }
}
static void check_u64(const char* name, uint64_t got, uint64_t want) {
  if (got != want) {
    printf("  [FAIL] %s: got=%016llx want=%016llx\n", name,
           (unsigned long long)got, (unsigned long long)want);
    ++g_fail;
  }
}

// --- plain-C Keccak-f[1600] reference (FIPS-202) ---------------------------
// lane (x,y) is s[5*y + x]; bit z is (s[..] >> z) & 1 — matches keccak.h.
static inline uint64_t rotl64(uint64_t v, int n) {
  n &= 63;
  return n ? (v << n) | (v >> (64 - n)) : v;
}
static void keccak_f_ref(uint64_t s[25]) {
  static const int RHO[5][5] = {
      {  0,  1, 62, 28, 27 }, { 36, 44,  6, 55, 20 }, {  3, 10, 43, 25, 39 },
      { 41, 45, 15, 21,  8 }, { 18,  2, 61, 56, 14 } };
  for (int r = 0; r < 24; ++r) {
    uint64_t C[5], D[5];
    for (int x = 0; x < 5; ++x)
      C[x] = s[x] ^ s[x + 5] ^ s[x + 10] ^ s[x + 15] ^ s[x + 20];
    for (int x = 0; x < 5; ++x) D[x] = C[(x + 4) % 5] ^ rotl64(C[(x + 1) % 5], 1);
    for (int x = 0; x < 5; ++x)
      for (int y = 0; y < 5; ++y) s[x + 5 * y] ^= D[x];

    uint64_t B[25] = {0};
    for (int x = 0; x < 5; ++x)
      for (int y = 0; y < 5; ++y)
        B[y + 5 * ((2 * x + 3 * y) % 5)] = rotl64(s[x + 5 * y], RHO[y][x]);

    for (int y = 0; y < 5; ++y) {
      uint64_t row[5];
      for (int x = 0; x < 5; ++x) row[x] = B[x + 5 * y];
      for (int x = 0; x < 5; ++x)
        s[x + 5 * y] = row[x] ^ ((~row[(x + 1) % 5]) & row[(x + 2) % 5]);
    }
    s[0] ^= KECCAK_RC[r];   // round constants shared with the circuit header
  }
}

// --- example: a normal user hashing "abc" ----------------------------------
// Feed the 3 message bytes as ALICE's input (a public wire vector in the clear),
// compute the digest with sha3_256, and reveal it back.
static void example() {
  ClearSession sess;

  // bits_from_hex gives LSB-first-within-byte bits; pack into the input array.
  std::vector<uint8_t> abc = bits_from_hex("616263");
  std::array<bool, 24> in{};
  for (int i = 0; i < 24; ++i) in[i] = abc[i] != 0;

  auto msg = sess.input<BV24>(ALICE, in);     // feed "abc" as ALICE's input
  BV256 dig = sha3_256(sess.ctx(), msg);

  std::array<bool, 256> bits = sess.reveal(dig, PUBLIC).value();
  std::vector<uint8_t> ov(bits.begin(), bits.end());
  check("example sha3_256(\"abc\")",
        hex_from_bits(ov) ==
            "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532");
}

// --- keccak_f1600: permutation vs the C reference --------------------------
// Drive UInt_T<Ctx,64> A[25] with the 25 lanes fed as ALICE inputs, permute,
// reveal each lane, and compare against keccak_f_ref on the same start state.
static void run_permute_case(const char* label, const uint64_t start[25]) {
  ClearSession sess;
  U64 A[25];
  for (int l = 0; l < 25; ++l) A[l] = sess.input<U64>(ALICE, start[l]);

  keccak_f1600(sess.ctx(), A);

  uint64_t want[25];
  std::memcpy(want, start, sizeof want);
  keccak_f_ref(want);

  for (int l = 0; l < 25; ++l) {
    char nm[64];
    std::snprintf(nm, sizeof nm, "%s lane %d", label, l);
    check_u64(nm, sess.reveal(A[l], PUBLIC).value(), want[l]);
  }
}

static void test_keccak_f1600() {
  // all-zero state
  uint64_t zero[25] = {0};
  run_permute_case("keccak-f zero", zero);

  // a known fixed nonzero state (lane l = a per-lane pattern)
  uint64_t known[25];
  for (int l = 0; l < 25; ++l)
    known[l] = 0x0123456789abcdefULL * (uint64_t)(l + 1) + (uint64_t)l;
  run_permute_case("keccak-f known", known);

  // seeded random states (fixed seed -> deterministic)
  std::mt19937_64 rng(0xBADA55ULL);
  for (int t = 0; t < 4; ++t) {
    uint64_t st[25];
    for (int l = 0; l < 25; ++l) st[l] = rng();
    char lbl[32];
    std::snprintf(lbl, sizeof lbl, "keccak-f rand%d", t);
    run_permute_case(lbl, st);
  }
}

// --- SHA3-256 over a clear byte message via BitVec_T -----------------------
// Templated on the compile-time bit width (sha3_256<N> needs a known N).
template <int Bytes>
static std::vector<uint8_t> sha3_clear(const uint8_t* msg) {
  constexpr int N = Bytes * 8;
  ClearSession sess;
  using Msg = BitVec_T<Ctx, N>;
  std::array<bool, N> in{};
  for (int b = 0; b < Bytes; ++b)
    for (int k = 0; k < 8; ++k) in[b * 8 + k] = ((msg[b] >> k) & 1) != 0;

  auto m = [&] {
    if constexpr (Bytes == 0) return Msg(sess.ctx());
    else return sess.input<Msg>(ALICE, in);
  }();
  BV256 dig = sha3_256(sess.ctx(), m);
  std::array<bool, 256> bits = sess.reveal(dig, PUBLIC).value();
  return std::vector<uint8_t>(bits.begin(), bits.end());
}

static void test_sha3_vectors() {
  // "" -> a7ffc6f8..
  std::vector<uint8_t> empty = sha3_clear<0>(nullptr);
  check("sha3_256(\"\")",
        hex_from_bits(empty) ==
            "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a");

  // "abc" -> 3a985da7..
  const uint8_t abc[3] = {'a', 'b', 'c'};
  std::vector<uint8_t> dabc = sha3_clear<3>(abc);
  check("sha3_256(\"abc\")",
        hex_from_bits(dabc) ==
            "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532");
}

// --- OpenSSL cross-check at rate-block boundaries --------------------------
static std::string openssl_sha3_256(const uint8_t* msg, size_t len) {
  uint8_t md[32];
  unsigned int mdlen = 0;
  EVP_Digest(msg, len, md, &mdlen, EVP_sha3_256(), nullptr);
  std::vector<uint8_t> bits;
  for (int i = 0; i < 32; ++i)
    for (int k = 0; k < 8; ++k) bits.push_back((uint8_t)((md[i] >> k) & 1));
  return hex_from_bits(bits);
}

template <int Bytes>
static void crosscheck_len(std::mt19937_64& rng) {
  uint8_t msg[Bytes > 0 ? Bytes : 1];
  for (int i = 0; i < Bytes; ++i) msg[i] = (uint8_t)(rng() & 0xff);
  std::string want = openssl_sha3_256(msg, (size_t)Bytes);
  std::string got = hex_from_bits(sha3_clear<Bytes>(msg));
  char nm[48];
  std::snprintf(nm, sizeof nm, "sha3 vs OpenSSL len=%d", Bytes);
  if (got != want) {
    printf("  [FAIL] %s\n    got:  %s\n    want: %s\n", nm, got.c_str(), want.c_str());
    ++g_fail;
  }
}

// rate = 1088 bits = 136 bytes; probe around 0/1/135/136/137 and the next block.
static void test_openssl_crosscheck() {
  std::mt19937_64 rng(0xC0FFEEULL);
  crosscheck_len<0>(rng);
  crosscheck_len<1>(rng);
  crosscheck_len<135>(rng);
  crosscheck_len<136>(rng);
  crosscheck_len<137>(rng);
  crosscheck_len<271>(rng);
  crosscheck_len<272>(rng);
}

// --- AND count + record/replay equivalence (low-level IR plumbing) ---------
// Deliberately low-level: a generic Ctx-templated driver over Wire* / from_wires
// / pack_wires, driven through clear_run / record_replay / count_ands. This is
// IR plumbing, not a user-facing example — the readable sections above use the
// ClearSession I/O boundary instead.
static void test_and_count() {
  // Fixed 256-bit message (one Keccak-f) for the circuit-shape checks.
  auto drv256 = [](auto& ctx, auto in, auto out) {
    using Ctx = std::decay_t<decltype(ctx)>;
    using Msg = BitVec_T<Ctx, 256>;
    Msg msg = Msg::from_wires(ctx, in);
    auto dig = sha3_256(ctx, msg);
    dig.pack_wires(out);
  };

  std::vector<uint8_t> msg256(256);
  for (int i = 0; i < 256; ++i) msg256[i] = (uint8_t)((i * 3 + 2) & 1);

  std::vector<uint8_t> live = clear_run(drv256, msg256, 256);
  std::vector<uint8_t> rep = record_replay(drv256, msg256, 256);
  uint64_t ands = count_ands(drv256, 256, 256);

  check("record==live (256-bit msg)", rep == live);
  check_u64("sha3_256 AND count", ands, 38400);
}

int main() {
  example();
  test_keccak_f1600();
  test_sha3_vectors();
  test_openssl_crosscheck();
  test_and_count();

  if (g_fail == 0) {
    printf("test_crypto_keccak: PASS\n");
    return 0;
  }
  printf("test_crypto_keccak: FAILED (%d checks)\n", g_fail);
  return 1;
}
